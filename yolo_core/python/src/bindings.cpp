/*
 * bindings.cpp — yolo_core 的 pybind11 绑定（YOLO26-seg / OpenVINO）
 *
 * 只暴露两件事（与 HALCON 扩展包现有的 2 个算子一致）：
 *   Model(model_xml, device='CPU')   加载 OpenVINO 模型（等价 OpenvinoLoadModel）
 *   Model.detect(image, ...)         推理 + 后处理（等价 yolo_seg_detect）
 *
 * 图像用 numpy 交换（uint8，灰度 (H,W) 或 BGR (H,W,3)）；结果以 numpy 返回：
 *   num / boxes(N,4 int32 xyxy) / scores(N,) / class_ids(N,) /
 *   label_image(H,W uint16，0=背景，值=class_id+1) / masks(list，框内二值掩膜 0/255)
 *
 * 约定：
 *   * 输入数组**不做拷贝**（只保证 C 连续，必要时 OpenCV/pybind11 会拷一次）；
 *   * 推理期间释放 GIL（CPU 密集），因此多个线程可以用**不同** Model 实例并行；
 *     同一个 Model 内部只有一个 ov::InferRequest，**不能**并发调用（Python 侧请自行串行，
 *     或每个线程建一个 Model）。
 *   * 失败抛 RuntimeError（消息来自 C++ 侧 err：含 OpenVINO 原文）。
 */
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "yolo/yolo.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace py = pybind11;

namespace {

/* 已 ensure 过的 uint8 数组 -> cv::Mat 浅视图
   （调用方必须保证 arr 在 Mat 使用期间存活；本文件里都在同一作用域）*/
cv::Mat MatFromArray(const py::array &arr)
{
    const py::buffer_info info = arr.request();
    if (info.ndim == 2)
        return cv::Mat(static_cast<int>(info.shape[0]), static_cast<int>(info.shape[1]),
                       CV_8UC1, info.ptr);
    if (info.ndim == 3 && info.shape[2] == 3)
        return cv::Mat(static_cast<int>(info.shape[0]), static_cast<int>(info.shape[1]),
                       CV_8UC3, info.ptr);
    throw py::value_error("image 形状必须是 (H,W) 灰度或 (H,W,3) BGR");
}

/* step / window 参数：None、单个整数，或 (x, y) */
void ParsePair(const py::object &obj, int &a, int &b, const char *name)
{
    if (obj.is_none()) return;
    if (py::isinstance<py::tuple>(obj) || py::isinstance<py::list>(obj))
    {
        const py::sequence seq = py::reinterpret_borrow<py::sequence>(obj);
        if (py::len(seq) != 2)
            throw py::value_error(std::string(name) + " 需要 1 个整数或 (x, y) 两个整数");
        a = py::cast<int>(seq[0]);
        b = py::cast<int>(seq[1]);
        return;
    }
    a = b = py::cast<int>(obj);
}

py::dict Detect(yolo::Model &self, const py::object &image, float conf_threshold,
                float mask_threshold, float nms_threshold, float overlap,
                const py::object &step, const py::object &window)
{
    // 1) numpy -> cv::Mat（严格 uint8，避免 float 被静默截断成 8 位）
    py::array raw = py::array::ensure(image);
    if (!raw) throw py::type_error("image 必须是 numpy.ndarray");
    if (!py::isinstance<py::array_t<uint8_t>>(raw))
        throw py::type_error("image 的 dtype 必须是 uint8（灰度图或 BGR 图）");
    py::array arr = py::array::ensure(raw, py::array::c_style | py::array::forcecast);
    const cv::Mat mat = MatFromArray(arr);   // 视图：arr 在本函数内一直存活

    yolo::InferParams params;
    params.conf_threshold = conf_threshold;
    params.mask_threshold = mask_threshold;
    params.nms_threshold  = nms_threshold;
    params.overlap        = overlap;
    ParsePair(step,   params.step_x, params.step_y, "step");
    ParsePair(window, params.win_w,  params.win_h,  "window");

    // 2) 推理（释放 GIL）
    std::vector<yolo::Detection> dets;
    std::string err;
    bool ok = false;
    {
        py::gil_scoped_release rel;
        ok = self.detect(mat, params, dets, err);
    }
    if (!ok) throw std::runtime_error(err.empty() ? std::string("detect 失败") : err);

    // 3) 结果 -> numpy
    const int n     = static_cast<int>(dets.size());
    const int rows  = mat.rows;
    const int cols  = mat.cols;

    py::array_t<int32_t> boxes({n, 4});
    py::array_t<float>   scores(n);
    py::array_t<int32_t> class_ids(n);
    int32_t *b = boxes.mutable_data();
    float   *s = scores.mutable_data();
    int32_t *c = class_ids.mutable_data();
    for (int i = 0; i < n; ++i)
    {
        const cv::Rect &r = dets[i].box;
        b[i * 4 + 0] = r.x;
        b[i * 4 + 1] = r.y;
        b[i * 4 + 2] = r.x + r.width;    // 右/下为开区间
        b[i * 4 + 3] = r.y + r.height;
        s[i] = dets[i].conf;
        c[i] = dets[i].class_id;
    }

    // 标签图：与原图等大，0 = 背景，像素值 = 类别 ID + 1（与 C 接口 WriteResults 一致）
    py::array_t<uint16_t> label({rows, cols});
    uint16_t *lab = label.mutable_data();
    std::memset(lab, 0, sizeof(uint16_t) * static_cast<size_t>(rows) * static_cast<size_t>(cols));

    py::list masks;
    for (const yolo::Detection &d : dets)
    {
        if (d.mask.empty()) continue;

        py::array_t<uint8_t> m({d.mask.rows, d.mask.cols});
        std::memcpy(m.mutable_data(), d.mask.data,
                    static_cast<size_t>(d.mask.rows) * static_cast<size_t>(d.mask.cols));
        masks.append(m);

        if (d.box.x < 0 || d.box.y < 0 ||
            d.box.x + d.mask.cols > cols || d.box.y + d.mask.rows > rows)
            continue;   // 防御：detect 内部已 clamp，正常走不到
        uint16_t *dst0 = lab + static_cast<size_t>(d.box.y) * cols + d.box.x;
        for (int y = 0; y < d.mask.rows; ++y)
        {
            const uint8_t *src = d.mask.ptr<uint8_t>(y);
            uint16_t      *dst = dst0 + static_cast<size_t>(y) * cols;
            for (int x = 0; x < d.mask.cols; ++x)
                if (src[x] != 0) dst[x] = static_cast<uint16_t>(d.class_id + 1);
        }
    }

    py::dict out;
    out["num"]         = n;
    out["boxes"]       = boxes;
    out["scores"]      = scores;
    out["class_ids"]   = class_ids;
    out["label_image"] = label;
    out["masks"]       = masks;
    return out;
}

}  // namespace

PYBIND11_MODULE(_yolo, m)
{
    m.doc() = "yolo_core 的 Python 扩展模块（YOLO26-seg / OpenVINO，HALCON-free）";

    py::class_<yolo::Model>(m, "Model",
                            "YOLO26 实例分割模型（OpenVINO）。\n\n"
                            "Model(model_xml, device='CPU') -> 加载 .xml/.bin（同名 .bin 需在同目录）\n"
                            "1 个 Model 持有一个 ov::InferRequest：**不要**在多线程里同时调用它。")
        .def(py::init([](const std::string &model_xml, const std::string &device) {
                 auto model = std::make_unique<yolo::Model>();
                 std::string err;
                 bool ok = false;
                 {
                     py::gil_scoped_release rel;   // 加载/编译模型较慢，释放 GIL
                     ok = model->load(model_xml, device, err);
                 }
                 if (!ok)
                     throw std::runtime_error(err.empty() ? std::string("模型加载失败") : err);
                 return model;
             }),
             py::arg("model_xml"), py::arg("device") = "CPU",
             "加载 OpenVINO IR 模型：model_xml = .xml 路径，device = 'CPU' / 'GPU' / 'NPU'")
        .def_property_readonly("input_width", &yolo::Model::input_width,
                               "模型输入宽度（像素）")
        .def_property_readonly("input_height", &yolo::Model::input_height,
                               "模型输入高度（像素）")
        .def("detect", &Detect, py::arg("image"),
             py::arg("conf_threshold") = 0.25f, py::arg("mask_threshold") = 0.5f,
             py::arg("nms_threshold") = 0.45f, py::arg("overlap") = 0.2f,
             py::arg("step") = py::none(), py::arg("window") = py::none(),
             "检测/实例分割。\n\n"
             "image: uint8 ndarray，灰度 (H,W) 或 BGR (H,W,3)\n"
             "conf_threshold / mask_threshold / nms_threshold: 阈值（窗口内 IoU-NMS，跨窗口 IoS）\n"
             "overlap: 滑窗重叠度（默认 0.2 → 640 窗口步长 512）\n"
             "step: 显式滑窗步长（int 或 (sx, sy)），给了就优先于 overlap\n"
             "window: 窗口尺寸（int 或 (w, h)），默认 = 模型输入尺寸\n"
             "返回 dict: num / boxes / scores / class_ids / label_image / masks");
}
