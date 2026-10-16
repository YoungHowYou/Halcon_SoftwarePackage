/*
 * yolo_capi.cpp — yolo_core 的纯 C 接口实现（HALCON-free）
 *
 * 三个函数与 HALCON 扩展包 `Halcon_YouloBe` 的三个算子一一对应：
 *   yolo_openvino_load_model ↔ OpenvinoLoadModel(::DictHandle:Handle)
 *   yolo_seg_detect          ↔ yolo_seg_detect(::Handle,DictHandle:)
 *   yolo_openvino_infer      —— 通用推理：扩展包侧已无对应算子（纯 C/C++ 工程用）
 *
 * 字典直接复用 Halcon_SoftwarePackage 的 hdict_core（DictPtr），本文件只做
 * "字典键 ⇄ 推理参数"的搬运 + 结果回写；推理算法本体一律留在 yolo::Model。
 *
 * 中文键名统一用 yolo_capi.h 里的 UTF-8 转义宏，避免源码编码差异导致键名不匹配。
 */
#include "yolo/yolo_capi.h"
#include "yolo/yolo.hpp"
#include "hdict_core.h"
#include "json.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using MLIQMetrics::hdict::DictPtr;

namespace {

inline DictPtr& AsDict(YoloDict dict)
{
    return *static_cast<DictPtr*>(dict);
}

/*---------------------------------------------------------------------------
 * 字典读取小工具
 *   缺键 / 类型不符 → 返回 false（调用方保留默认值）
 *   语义与 HALCON 侧 `try { GetDictTuple(...) } catch (...) {}` 一致
 *-------------------------------------------------------------------------*/

bool GetInt(const DictPtr& dict, const char* key, int& out)
{
    try {
        if (!MLIQMetrics::hdict::key_exists(dict, key)) return false;
        const nlohmann::json v = MLIQMetrics::hdict::get_dict_tuple(dict, key);
        if (!v.is_number()) return false;
        out = v.get<int>();
        return true;
    } catch (...) {
        return false;
    }
}

bool GetFloat(const DictPtr& dict, const char* key, float& out)
{
    try {
        if (!MLIQMetrics::hdict::key_exists(dict, key)) return false;
        const nlohmann::json v = MLIQMetrics::hdict::get_dict_tuple(dict, key);
        if (!v.is_number()) return false;
        out = v.get<float>();
        return true;
    } catch (...) {
        return false;
    }
}

bool GetString(const DictPtr& dict, const char* key, std::string& out)
{
    try {
        if (!MLIQMetrics::hdict::key_exists(dict, key)) return false;
        const nlohmann::json v = MLIQMetrics::hdict::get_dict_tuple(dict, key);
        if (!v.is_string()) return false;
        out = v.get<std::string>();
        return true;
    } catch (...) {
        return false;
    }
}

/* 取图像对象（shared_ptr<cv::Mat>，零拷贝拿到句柄） */
bool GetImage(const DictPtr& dict, const char* key, std::shared_ptr<cv::Mat>& out)
{
    try {
        out = MLIQMetrics::hdict::get_dict_object_as<std::shared_ptr<cv::Mat>>(dict, key);
        return out && !out->empty();
    } catch (...) {
        return false;
    }
}

/*---------------------------------------------------------------------------
 * 结果回写（键名 / 语义与 HALCON 算子完全相同）
 *   NumDetections    恒写
 *   ClassIDs / Confidences / ClassLabelImage / BoundingBoxes  仅 n > 0 时写
 *-------------------------------------------------------------------------*/
void WriteResults(const DictPtr& dict, const std::vector<yolo::Detection>& results,
                  int orig_w, int orig_h)
{
    MLIQMetrics::hdict::set_dict_tuple(dict, YOLO_DICT_NUM_DETECTIONS,
                                       static_cast<int>(results.size()));
    if (results.empty()) return;

    nlohmann::json class_ids = nlohmann::json::array();
    nlohmann::json confs     = nlohmann::json::array();

    // 标签图：与原图等大，0 = 背景，像素值 = 类别ID + 1（与 HALCON 侧一致）
    auto label = std::make_shared<cv::Mat>(orig_h, orig_w, CV_16UC1, cv::Scalar(0));
    auto boxes = std::make_shared<std::vector<cv::Rect>>();
    boxes->reserve(results.size());

    for (const auto& r : results) {
        class_ids.push_back(r.class_id);
        confs.push_back(static_cast<double>(r.conf));
        boxes->push_back(r.box);

        if (r.mask.empty()) continue;
        if (r.box.x < 0 || r.box.y < 0 ||
            r.box.x + r.box.width > orig_w || r.box.y + r.box.height > orig_h) {
            continue;  // 越界保护（detect 内已 clamp，正常不会走到这里）
        }
        if (r.mask.rows != r.box.height || r.mask.cols != r.box.width) continue;

        cv::Mat dst_roi = (*label)(r.box);
        cv::Mat fill_mat(r.box.height, r.box.width, CV_16UC1,
                         cv::Scalar(static_cast<ushort>(r.class_id + 1)));
        fill_mat.copyTo(dst_roi, r.mask);
    }

    MLIQMetrics::hdict::set_dict_tuple(dict, YOLO_DICT_CLASS_IDS, std::move(class_ids));
    MLIQMetrics::hdict::set_dict_tuple(dict, YOLO_DICT_CONFIDENCES, std::move(confs));
    MLIQMetrics::hdict::set_dict_object_shared(dict, YOLO_DICT_CLASS_LABEL, label);
    MLIQMetrics::hdict::set_dict_object_shared(dict, YOLO_DICT_BOUNDING_BOXES, boxes);
}

}  // namespace

/*===========================================================================
 * OpenvinoLoadModel(::DictHandle:Handle)
 *=========================================================================*/
int yolo_openvino_load_model(YoloDict dict, YoloModel* model)
{
    if (!dict || !model) return YOLO_ERR_PARAM;
    *model = nullptr;

    DictPtr& d = AsDict(dict);

    std::string path, device;
    if (!GetString(d, YOLO_DICT_MODEL_PATH, path)) return YOLO_ERR_DICT_KEY;
    if (!GetString(d, YOLO_DICT_DEVICE, device))     return YOLO_ERR_DICT_KEY;

    // 异常兜底：本文件是纯 C 接口（extern "C"），异常绝不能穿出 C ABI
    try {
        auto* handle = new yolo::Model();
        std::string err;
        if (!handle->load(path, device, err)) {
            delete handle;
            return YOLO_ERR_LOAD_MODEL;
        }

        // 与 HALCON 算子一致：把模型输入尺寸写回字典
        MLIQMetrics::hdict::set_dict_tuple(d, YOLO_DICT_WIDTH,  handle->input_width());
        MLIQMetrics::hdict::set_dict_tuple(d, YOLO_DICT_HEIGHT, handle->input_height());

        *model = handle;
    } catch (...) {
        return YOLO_ERR_LOAD_MODEL;
    }
    return YOLO_OK;
}

/*===========================================================================
 * yolo_openvino_infer(model, dict)   —— 通用推理（纯 C/C++ 侧；扩展包已无该算子）
 *=========================================================================*/
int yolo_openvino_infer(YoloModel model, YoloDict dict)
{
    if (!model || !dict) return YOLO_ERR_PARAM;

    DictPtr& d = AsDict(dict);
    std::shared_ptr<cv::Mat> image;
    if (!GetImage(d, YOLO_DICT_INPUT_IMAGE, image)) return YOLO_ERR_DICT_IMAGE;

    // infer_raw 的约定是 3 通道 BGR（与 HALCON 侧原实现一致：灰度先转 BGR）
    cv::Mat gray2bgr;
    const cv::Mat* input = image.get();
    if (image->channels() == 1)
    {
        cv::cvtColor(*image, gray2bgr, cv::COLOR_GRAY2BGR);
        input = &gray2bgr;
    }

    ov::Tensor output;
    try {
        if (!static_cast<yolo::Model*>(model)->infer_raw(*input, output)) return YOLO_ERR_INFER;

        auto tensor = std::make_shared<ov::Tensor>(std::move(output));
        MLIQMetrics::hdict::set_dict_object_shared(d, YOLO_DICT_OUTPUT_IMAGE, tensor);
    } catch (...) {
        return YOLO_ERR_INFER;   // 异常不穿出 C ABI
    }
    return YOLO_OK;
}

/*===========================================================================
 * yolo_seg_detect(::Handle,DictHandle:)
 *=========================================================================*/
int yolo_seg_detect(YoloModel model, YoloDict dict)
{
    if (!model || !dict) return YOLO_ERR_PARAM;

    DictPtr& d = AsDict(dict);
    std::shared_ptr<cv::Mat> image;
    if (!GetImage(d, YOLO_DICT_INPUT_IMAGE, image)) return YOLO_ERR_DICT_IMAGE;

    auto* m = static_cast<yolo::Model*>(model);

    // 阈值（未设置时用 yolo::InferParams 默认值）
    yolo::InferParams params;
    GetFloat(d, YOLO_DICT_CONF_THRESHOLD, params.conf_threshold);
    GetFloat(d, YOLO_DICT_MASK_THRESHOLD, params.mask_threshold);
    GetFloat(d, YOLO_DICT_NMS_THRESHOLD,  params.nms_threshold);

    // 窗口尺寸与滑窗步长：默认 = 模型输入尺寸，字典可覆盖
    int win_w = m->input_width();
    int win_h = m->input_height();
    GetInt(d, YOLO_DICT_WIDTH,  win_w);
    GetInt(d, YOLO_DICT_HEIGHT, win_h);

    int step_x = win_w;
    int step_y = win_h;
    if (GetInt(d, YOLO_DICT_STEP, step_x)) step_y = step_x;  // '步长' 同时覆盖 X/Y
    GetInt(d, YOLO_DICT_STEP_X, step_x);
    GetInt(d, YOLO_DICT_STEP_Y, step_y);

    params.win_w  = win_w;
    params.win_h  = win_h;
    params.step_x = step_x;
    params.step_y = step_y;

    // 输出格式识别 / argmax / NMS / 缩放 / 滑窗 / 掩膜 全在 yolo::Model::detect 内
    std::vector<yolo::Detection> results;
    std::string err;
    try {
        if (!m->detect(*image, params, results, err)) return YOLO_ERR_INFER;

        WriteResults(d, results, image->cols, image->rows);
    } catch (...) {
        return YOLO_ERR_INFER;   // 异常不穿出 C ABI
    }
    return YOLO_OK;
}

/*===========================================================================
 * 释放模型句柄
 *=========================================================================*/
void yolo_model_destroy(YoloModel model)
{
    delete static_cast<yolo::Model*>(model);
}
