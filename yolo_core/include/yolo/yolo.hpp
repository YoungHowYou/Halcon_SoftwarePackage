/*
 * yolo.hpp — YOLO26 实例分割推理核心（内部接口，供扩展包 supply 层与其它 C++ 工程复用）
 *
 * 输出格式自动识别（已经过 ultralytics 官方推理逐框比对验证）：
 *   - end2end（YOLO26 原生 NMS-free，TopK 已烘焙进图）: [1, 300, D]
 *       每行 [x1,y1,x2,y2, conf, class_id, mask 系数 x proto_c]（角点坐标，无需 NMS）
 *   - raw（ultralytics 常规导出 end2end=False）:        [1, D, 8400]（channel-first）
 *       每列 [cx,cy,w,h, nc 个类别分(已 sigmoid), mask 系数 x proto_c]（中心点+宽高）
 */
#pragma once

// Windows.h 的 min/max 宏会破坏 std::min/std::max，必须在任何 windows 头之前定义
#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
#endif

#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>
#include <string>
#include <vector>

namespace yolo {

struct Detection {
    int    class_id = 0;
    float  conf     = 0.f;
    cv::Rect box;        // 原图绝对坐标
    cv::Mat  mask;       // 框范围内的二值掩膜（CV_8UC1），可能为空
};

struct InferParams {
    float conf_threshold = 0.25f;
    float nms_threshold  = 0.45f;
    float mask_threshold = 0.5f;
    int   step_x         = 0;   // 滑窗步长（像素），0 = 窗口宽度
    int   step_y         = 0;   // 同上，Y 方向；0 = 窗口高度
    int   win_w          = 0;   // 滑窗/裁剪窗口宽度，0 = 模型输入宽度
    int   win_h          = 0;   // 同上，高度
};

class Model {
public:
    bool load(const std::string& xml_path, const std::string& device, std::string& err);
    bool detect(const cv::Mat& image, const InferParams& params,
                std::vector<Detection>& out, std::string& err);

    // 通用推理（脱离 HALCON 的工程用；扩展包侧已无对应算子）：输入 3 通道 BGR，输出原始输出张量（深拷贝）
    bool infer_raw(const cv::Mat& image, ov::Tensor& output);

    int input_width()  const { return input_w_; }
    int input_height() const { return input_h_; }

private:
    // detect() 的实体（带异常兜底的公开入口转调它，见 yolo.cpp）
    bool detectImpl(const cv::Mat& image, const InferParams& params,
                    std::vector<Detection>& out, std::string& err);

    bool infer_yolo_seg(const cv::Mat& input_image,
                        ov::Tensor& det_output, ov::Tensor& proto_output);

    ov::Core           core_;
    ov::CompiledModel  compiled_model_;
    ov::InferRequest   infer_request_;
    int input_w_  = 0;
    int input_h_  = 0;
};

} // namespace yolo
