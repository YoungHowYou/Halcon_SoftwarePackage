/*
 * yolo.cpp — YOLO26-seg 推理核心实现
 *
 * 从 Halcon_YouloBe 算子 supply 层提炼（与桌面 yolobe 工程同源），
 * 已经过 ultralytics best.pt 真值逐框比对验证（keyence 数据集 3 类全中，坐标误差 ≤1px）。
 */
#include "yolo/yolo.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace yolo {

bool Model::load(const std::string& xml_path, const std::string& device, std::string& err)
{
    try {
        auto model = core_.read_model(xml_path);

        ov::AnyMap config;
        // 只有非 CPU 设备才强制 f32 + ACCURACY：Intel GPU 会自动降精度为 FP16（坐标会溢出）；
        // CPU 插件上 ACCURACY 模式通常比默认 PERFORMANCE 慢，不设。
        const bool is_cpu = device.rfind("CPU", 0) == 0;   // "CPU" / "CPU.0" / "CPU.1" …
        if (!is_cpu && model->input().get_element_type() == ov::element::f32) {
            config[ov::hint::inference_precision.name()] = ov::element::f32;
            config[ov::hint::execution_mode.name()] = ov::hint::ExecutionMode::ACCURACY;
        }

        compiled_model_ = core_.compile_model(model, device, config);

        auto shape = compiled_model_.input().get_shape();
        if (shape.size() != 4) {
            err = "模型输入不是 4 维 NCHW 图像张量";
            return false;
        }
        input_h_ = static_cast<int>(shape[2]);
        input_w_ = static_cast<int>(shape[3]);

        infer_request_ = compiled_model_.create_infer_request();
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

bool Model::infer_raw(const cv::Mat& image, ov::Tensor& output, std::string* err)
{
    try {
        cv::Mat blob;
        cv::dnn::blobFromImage(image, blob, 1.0 / 255.0,
                               cv::Size(input_w_, input_h_), cv::Scalar(0, 0, 0),
                               true, false, CV_32F);

        ov::Tensor input_tensor = infer_request_.get_input_tensor();
        std::memcpy(input_tensor.data<float>(), blob.ptr<float>(), blob.total() * sizeof(float));

        infer_request_.infer();

        // 取第 0 个输出：无参版 get_output_tensor() 要求模型恰好只有一个输出，
        // 而 YOLO-seg 系模型有 2 个输出（检测 + 分割原型），必须显式给下标
        auto tensor = infer_request_.get_output_tensor(0);
        output = ov::Tensor(tensor.get_element_type(), tensor.get_shape());
        tensor.copy_to(output);
        return true;
    } catch (const std::exception& e) {
        if (err != nullptr) *err = e.what();
        return false;
    }
}

bool Model::infer_yolo_seg(const cv::Mat& input_image,
                           ov::Tensor& det_output, ov::Tensor& proto_output,
                           std::string* err)
{
    try {
        cv::Mat blob;
        cv::dnn::blobFromImage(input_image, blob, 1.0 / 255.0,
                               cv::Size(input_w_, input_h_), cv::Scalar(0, 0, 0),
                               true, false, CV_32F);

        ov::Tensor input_tensor = infer_request_.get_input_tensor();
        std::memcpy(input_tensor.data<float>(), blob.ptr<float>(), blob.total() * sizeof(float));

        infer_request_.infer();

        // 输出0: [1, N, D] 或 [1, D, N] 检测；输出1: [1, 32, H, W] 分割原型
        for (size_t i = 0; i < 2; ++i) {
            auto tensor = infer_request_.get_output_tensor(i);
            auto shape  = tensor.get_shape();
            ov::Tensor& dst = (shape.size() == 3) ? det_output : proto_output;
            dst = ov::Tensor(tensor.get_element_type(), shape);
            std::memcpy(dst.data<float>(), tensor.data<float>(), tensor.get_byte_size());
        }
        return true;
    } catch (const std::exception& e) {
        if (err != nullptr) *err = e.what();
        return false;
    }
}

/* 全类 NMS（cv::dnn::NMSBoxes 语义，保留 mask） */
static void applyNMS(std::vector<Detection>& results, float nms_thresh)
{
    if (results.empty()) return;

    std::vector<int> indices;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;
    for (const auto& r : results) {
        confidences.push_back(r.conf);
        boxes.push_back(r.box);
    }
    cv::dnn::NMSBoxes(boxes, confidences, 0.0f, nms_thresh, indices);

    std::vector<Detection> kept;
    kept.reserve(indices.size());
    for (int idx : indices) kept.push_back(std::move(results[idx]));
    results = std::move(kept);
}

/* 跨窗口去重的判据：IoS = 交集 / 较小框面积
   被滑窗边界切开的同一个目标会得到“一大一小”两个框：IoU 很低（小框只占大框一部分），
   但小框几乎全在大框里 ⇒ IoS ≈ 1。所以跨窗口必须用 IoS 才能合掉这种重复框。*/
static float intersectionOverSmaller(const cv::Rect& a, const cv::Rect& b)
{
    const int ix1 = std::max(a.x, b.x);
    const int iy1 = std::max(a.y, b.y);
    const int ix2 = std::min(a.x + a.width,  b.x + b.width);
    const int iy2 = std::min(a.y + a.height, b.y + b.height);
    const int iw = ix2 - ix1;
    const int ih = iy2 - iy1;
    if (iw <= 0 || ih <= 0) return 0.0f;

    const float inter   = static_cast<float>(iw) * static_cast<float>(ih);
    const float smaller = static_cast<float>(std::min(a.area(), b.area()));
    return smaller > 0.0f ? inter / smaller : 0.0f;
}

/* 跨窗口去重：IoS 贪心抑制（只比较同类），按置信度从高到低保留 */
static void applyIosNMS(std::vector<Detection>& results, float ios_thresh)
{
    if (results.size() < 2) return;

    std::vector<int> order(results.size());
    for (int i = 0; i < static_cast<int>(results.size()); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&results](int a, int b) { return results[a].conf > results[b].conf; });

    std::vector<char> suppressed(results.size(), 0);
    std::vector<Detection> kept;
    kept.reserve(results.size());
    for (size_t oi = 0; oi < order.size(); ++oi) {
        const int i = order[oi];
        if (suppressed[i]) continue;

        const int      keep_cls = results[i].class_id;
        const cv::Rect keep_box = results[i].box;
        kept.push_back(std::move(results[i]));

        for (size_t oj = oi + 1; oj < order.size(); ++oj) {
            const int j = order[oj];
            if (suppressed[j] || results[j].class_id != keep_cls) continue;
            if (intersectionOverSmaller(keep_box, results[j].box) > ios_thresh) suppressed[j] = 1;
        }
    }
    results = std::move(kept);
}

bool Model::detect(const cv::Mat& image, const InferParams& params,
                   std::vector<Detection>& out, std::string& err)
{
    // 异常兜底：本函数既被任意 C++ 工程直接调，也被 yolo_capi（C ABI）与 HALCON supply 层调，
    // 任何 C++ 异常都不能穿出这条链（HALCON 宿主会 fail-fast 崩溃 0xe06d7363 / 0xc0000409）。
    try {
        return detectImpl(image, params, out, err);
    } catch (const std::exception& e) {
        err = e.what();
        out.clear();
        return false;
    } catch (...) {
        err = "未知的 C++ 异常";
        out.clear();
        return false;
    }
}

bool Model::detectImpl(const cv::Mat& image, const InferParams& params,
                       std::vector<Detection>& out, std::string& err)
{
    if (image.empty() || (image.channels() != 1 && image.channels() != 3) || image.depth() != CV_8U) {
        err = "输入图像必须是 8 位灰度或 8 位 3 通道 BGR";
        return false;
    }

    cv::Mat bgr = image.channels() == 3 ? image
                                        : [&] { cv::Mat m; cv::cvtColor(image, m, cv::COLOR_GRAY2BGR); return m; }();

    // 窗口尺寸：默认等于模型输入尺寸；显式传入时按窗口裁剪（如字典 宽/高 覆盖的场景）
    const int win_w = params.win_w > 0 ? params.win_w : input_w_;
    const int win_h = params.win_h > 0 ? params.win_h : input_h_;

    // 滑窗步长：显式给了步长就用步长，否则由重叠度算（默认 0.2 → 640 窗口步长 512）
    const double overlap = std::min(0.9, std::max(0.0, static_cast<double>(params.overlap)));
    const int step_x = params.step_x > 0
                           ? params.step_x
                           : std::max(1, static_cast<int>(std::lround(win_w * (1.0 - overlap))));
    const int step_y = params.step_y > 0
                           ? params.step_y
                           : std::max(1, static_cast<int>(std::lround(win_h * (1.0 - overlap))));

    // 原图尺寸：结果裁剪 / 标签图都按它（下面 pad 出来的黑边不算原图）
    const int orig_w = bgr.cols;
    const int orig_h = bgr.rows;

    std::vector<Detection> all;
    all.reserve(64);

    // 滑窗推理/输出解析失败的窗口数：有失败就整体报错，绝不静默给出"漏检"结果
    int         failed_windows = 0;
    std::string last_err;

    // 内部 Lambda：推理单窗口并解析结果
    auto doInference = [&](const cv::Mat& input_img, int offset_x, int offset_y,
                           float ratio_x, float ratio_y) {
        ov::Tensor  det_output, proto_output;
        std::string win_err;
        if (!infer_yolo_seg(input_img, det_output, proto_output, &win_err)) {
            ++failed_windows;
            if (last_err.empty()) last_err = win_err.empty() ? std::string("推理失败") : win_err;
            return;
        }

        const ov::Shape det_shape   = det_output.get_shape();
        const ov::Shape proto_shape = proto_output.get_shape();
        if (det_shape.size() != 3 || proto_shape.size() != 4) {
            ++failed_windows;
            if (last_err.empty())
                last_err = "模型输出形状不符（期望 3 维检测张量 + 4 维分割原型）";
            return;
        }

        float* det_data   = det_output.data<float>();
        float* proto_data = proto_output.data<float>();
        const int proto_c = static_cast<int>(proto_shape[1]);
        const int proto_h = static_cast<int>(proto_shape[2]);
        const int proto_w = static_cast<int>(proto_shape[3]);

        cv::Mat proto_mat(proto_c, proto_h * proto_w, CV_32FC1, proto_data);

        // 窗口坐标 -> 原图坐标的合并缩放系数（infer 内部会把窗口 resize 到模型输入尺寸）
        const float fx = (static_cast<float>(input_img.cols) / input_w_) * ratio_x;
        const float fy = (static_cast<float>(input_img.rows) / input_h_) * ratio_y;

        // ---- 输出格式自动识别 ----
        // end2end: [1, 300, D]；raw: [1, D, 8400]
        const bool raw_format = (det_shape[2] > det_shape[1]);

        std::vector<Detection> window_results;  // raw 格式窗口内先收集，NMS 后并入总结果

        int num_dets = 0, det_dims = 0, num_classes = 0, mask_off = 6;
        cv::Mat det_transposed;
        if (raw_format) {
            det_dims    = static_cast<int>(det_shape[1]);   // 4 + nc + proto_c
            num_dets    = static_cast<int>(det_shape[2]);   // 8400
            num_classes = det_dims - 4 - proto_c;
            if (num_classes <= 0) return;
            mask_off = 4 + num_classes;
            cv::transpose(cv::Mat(det_dims, num_dets, CV_32FC1, det_data), det_transposed);
        } else {
            num_dets = static_cast<int>(det_shape[1]);      // 300
            det_dims = static_cast<int>(det_shape[2]);
        }

        for (int i = 0; i < num_dets; ++i) {
            const float* det = raw_format ? det_transposed.ptr<float>(i)
                                          : det_data + i * det_dims;

            float conf;
            int   cls;
            if (raw_format) {
                // 取 nc 个类别分中的最大值
                const float* scores = det + 4;
                cls  = 0;
                conf = scores[0];
                for (int c = 1; c < num_classes; ++c)
                    if (scores[c] > conf) { conf = scores[c]; cls = c; }
                if (conf < params.conf_threshold) continue;
            } else {
                conf = det[4];
                if (conf < params.conf_threshold) continue;
                cls = static_cast<int>(det[5]);
            }

            // 统一转换为角点坐标（模型输入坐标系下）
            float px1, py1, px2, py2;
            if (raw_format) {
                const float pcx = det[0], pcy = det[1], pw = det[2], ph = det[3];
                px1 = pcx - pw * 0.5f; py1 = pcy - ph * 0.5f;
                px2 = pcx + pw * 0.5f; py2 = pcy + ph * 0.5f;
            } else {
                px1 = det[0]; py1 = det[1]; px2 = det[2]; py2 = det[3];
            }

            int x1 = static_cast<int>(px1 * fx) + offset_x;
            int y1 = static_cast<int>(py1 * fy) + offset_y;
            int x2 = static_cast<int>(px2 * fx) + offset_x;
            int y2 = static_cast<int>(py2 * fy) + offset_y;

            // 右/下边界是开区间（cv::Rect 的 x+width / y+height），所以 x2/y2 允许到 orig_w/orig_h，
            // 否则贴边目标会少一列/一行像素
            x1 = std::max(0, std::min(x1, orig_w - 1));
            y1 = std::max(0, std::min(y1, orig_h - 1));
            x2 = std::max(0, std::min(x2, orig_w));
            y2 = std::max(0, std::min(y2, orig_h));
            if (x2 - x1 <= 0 || y2 - y1 <= 0) continue;

            // 掩膜：系数 @ 原型 -> 裁剪框区域（原型图分辨率）-> 上采样 -> sigmoid 阈值化
            const float mask_scale = static_cast<float>(proto_w) / input_w_;  // 通常为 1/4
            int mx1 = std::max(0, static_cast<int>(px1 * mask_scale));
            int my1 = std::max(0, static_cast<int>(py1 * mask_scale));
            int mx2 = std::min(proto_w - 1, static_cast<int>(px2 * mask_scale));
            int my2 = std::min(proto_h - 1, static_cast<int>(py2 * mask_scale));
            if (mx2 - mx1 <= 0 || my2 - my1 <= 0) continue;

            cv::Mat coeffs(1, proto_c, CV_32FC1, const_cast<float*>(det) + mask_off);
            cv::Mat logits = coeffs * proto_mat;                    // [1, proto_h*proto_w]
            cv::Mat logits2d = logits.reshape(1, proto_h);          // [proto_h, proto_w]
            cv::Mat roi = logits2d(cv::Rect(mx1, my1, mx2 - mx1, my2 - my1)).clone();

            cv::Mat mask_up;
            cv::resize(roi, mask_up, cv::Size(x2 - x1, y2 - y1), 0, 0, cv::INTER_LINEAR);
            cv::Mat mask_bin;
            cv::threshold(mask_up, mask_bin, params.mask_threshold, 255, cv::THRESH_BINARY);
            mask_bin.convertTo(mask_bin, CV_8UC1);

            Detection res;
            res.class_id = cls;
            res.conf     = conf;
            res.box      = cv::Rect(x1, y1, x2 - x1, y2 - y1);
            res.mask     = mask_bin;
            if (raw_format) window_results.push_back(std::move(res));
            else            all.push_back(std::move(res));
        }

        // raw（one-to-many）单窗口内即大量重叠，必须 NMS；end2end（one-to-one）无需 NMS
        if (raw_format && !window_results.empty()) {
            applyNMS(window_results, params.nms_threshold);
            std::move(window_results.begin(), window_results.end(), std::back_inserter(all));
        }
    };

    // ---- 不缩放：小图 pad 到窗口尺寸（左上对齐、填 0），然后统一滑窗 ----
    //   小图 pad 保持目标在原始像素尺度上的大小（不会像缩放那样把目标放大/压扁）；
    //   大图按重叠度滑窗，窗口内始终是原分辨率像素。
    cv::Mat work = bgr;
    if (orig_w < win_w || orig_h < win_h) {
        cv::copyMakeBorder(bgr, work, 0, std::max(0, win_h - orig_h),
                           0, std::max(0, win_w - orig_w),
                           cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    }

    int windows = 0;
    for (int y = 0; y < work.rows; y += step_y) {
        const int start_y = std::min(y, std::max(0, work.rows - win_h));
        for (int x = 0; x < work.cols; x += step_x) {
            const int start_x = std::min(x, std::max(0, work.cols - win_w));

            doInference(work(cv::Rect(start_x, start_y, win_w, win_h)),
                        start_x, start_y, 1.0f, 1.0f);
            ++windows;

            if (start_x + win_w >= work.cols) break;
        }
        if (start_y + win_h >= work.rows) break;
    }

    // 只有多窗口才需要跨窗口去重：raw 单窗口已在 doInference 内做过 NMS，end2end 输出本身不重叠。
    // 跨窗口用 IoS（交集 / 较小框面积）：被窗口边界切开的同一目标框一大一小，IoU 很低但 IoS≈1。
    if (windows > 1) applyIosNMS(all, params.nms_threshold);

    // 有窗口失败就整体报错（宁可报错，也不要静默给出漏检结果）
    if (failed_windows > 0) {
        err = "有 " + std::to_string(failed_windows) + " / " + std::to_string(windows) +
              " 个滑窗推理失败";
        if (!last_err.empty()) err += "（" + last_err + "）";
        out.clear();
        return false;
    }

    out = std::move(all);
    return true;
}

} // namespace yolo
