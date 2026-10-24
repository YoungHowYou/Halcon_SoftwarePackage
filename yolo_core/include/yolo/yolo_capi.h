/*
 * yolo_capi.h — yolo_core 的纯 C 接口（HALCON-free，可脱离 HALCON 独立调用）
 *
 * 接口形式与 HALCON 扩展包 `Halcon_YouloBe` 的三个算子 1:1 一致（同字典、同键名、
 * 同默认值、同输出），因此 HALCON 侧 supply 层只是"搬运字典"，本 C 接口也能被
 * 任意 C/C++ 工程直接调用：
 *
 *   HALCON 算子                                  本 C 接口
 *   ---------------------------------------------------------------------------
 *   OpenvinoLoadModel(::DictHandle:Handle)   ↔   yolo_openvino_load_model(dict, &model)
 *   yolo_seg_detect(::Handle,DictHandle:)    ↔   yolo_seg_detect(model, dict)
 *   （通用推理没有 HALCON 侧算子：yolo_openvino_infer 只供纯 C/C++ 工程用）
 *
 * 本头文件是**纯 C**：不包含 OpenCV / OpenVINO / hdict 的任何头，只靠 void* 句柄
 * 传递，可被纯 C 编译单元直接 include。
 *
 * ---------------------------------------------------------------------------
 * 字典：直接复用 Halcon_SoftwarePackage 的 hdict_core（HALCON dict 的免 HALCON 复刻）
 *
 *   YoloDict 实际类型 = MLIQMetrics::hdict::DictPtr*（即 shared_ptr<Dict> 的地址）
 *   调用方用 hdict_core 原 API 建字典 / 填值 / 取值，例如：
 *
 *     auto dict = MLIQMetrics::hdict::create_dict();       // 等价 HALCON create_dict
 *     MLIQMetrics::hdict::set_dict_tuple(dict, "工程路径", path);
 *     MLIQMetrics::hdict::set_dict_object_shared(dict, "InputImage",
 *                                                std::make_shared<cv::Mat>(bgr));
 *     YoloModel model = nullptr;
 *     yolo_openvino_load_model(&dict, &model);             // 传 &dict
 *     yolo_seg_detect(model, &dict);
 *     int n = MLIQMetrics::hdict::get_dict_tuple(dict, "NumDetections").get<int>();
 *     yolo_model_destroy(model);
 *
 *   字典句柄只在调用期间被借用，函数返回后不再被引用（字典归调用方所有）。
 *
 * ---------------------------------------------------------------------------
 * 字典里"对象"的载荷约定（HALCON 侧对应的是 HObject / Region 数组）：
 *
 *   InputImage        std::shared_ptr<cv::Mat>                8UC1(灰度) 或 8UC3(BGR)
 *   ClassLabelImage   std::shared_ptr<cv::Mat>                CV_16UC1，0=背景，像素值=类别ID+1
 *   BoundingBoxes     std::shared_ptr<std::vector<cv::Rect>>  原图绝对坐标
 *   OutputImage       std::shared_ptr<ov::Tensor>             （仅 yolo_openvino_infer，原始输出张量）
 *
 * 取对象：std::any_cast<std::shared_ptr<cv::Mat>>(hdict::get_dict_object(dict, "…"))
 *         或 hdict::get_dict_object_as<std::shared_ptr<cv::Mat>>(dict, "…")
 * ---------------------------------------------------------------------------
 */

#ifndef YOLO_CAPI_H
#define YOLO_CAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(YOLO_CAPI_SHARED)
  #ifdef YOLO_CAPI_BUILD
    #define YOLO_CAPI_API __declspec(dllexport)
  #else
    #define YOLO_CAPI_API __declspec(dllimport)
  #endif
#else
  #define YOLO_CAPI_API
#endif

/*-----------------------------------------------------------------------------
 * 句柄
 *---------------------------------------------------------------------------*/
typedef void *YoloDict;    /* 字典句柄：MLIQMetrics::hdict::DictPtr*（调用方所有）*/
typedef void *YoloModel;   /* 模型句柄：yolo_openvino_load_model 产出，yolo_model_destroy 释放 */

/*-----------------------------------------------------------------------------
 * 返回码（0 = 成功；与 HALCON 算子的语义一致：出错时 HALCON 侧返回 10000*行号）
 *---------------------------------------------------------------------------*/
#define YOLO_OK                  0   /* 成功 */
#define YOLO_ERR_PARAM           1   /* 句柄/字典为空，或字典对象类型不符 */
#define YOLO_ERR_DICT_KEY        2   /* 字典缺少必填 tuple 键（'工程路径' / '使用设备'）*/
#define YOLO_ERR_DICT_IMAGE      3   /* 字典缺少 InputImage 图像对象 */
#define YOLO_ERR_LOAD_MODEL      4   /* OpenVINO 读模型 / 编译模型失败 */
#define YOLO_ERR_INFER           5   /* 推理或结果解析失败 */

/*-----------------------------------------------------------------------------
 * 字典键（与 HALCON 算子完全一致）
 *   中文键写成 UTF-8 转义，避免调用方源码编码（GBK/UTF-8）影响键名匹配
 *---------------------------------------------------------------------------*/
/* 输入：模型 */
#define YOLO_DICT_MODEL_PATH     "\xe5\xb7\xa5\xe7\xa8\x8b\xe8\xb7\xaf\xe5\xbe\x84"  /* 工程路径 */
#define YOLO_DICT_DEVICE         "\xe4\xbd\xbf\xe7\x94\xa8\xe8\xae\xbe\xe5\xa4\x87"  /* 使用设备 */

/* 输入：阈值 / 滑窗（选填，缺省值与 yolo::InferParams 一致）*/
#define YOLO_DICT_CONF_THRESHOLD "ConfThreshold"   /* 0.25 */
#define YOLO_DICT_MASK_THRESHOLD "MaskThreshold"   /* 0.5  */
#define YOLO_DICT_NMS_THRESHOLD  "NMSThreshold"    /* 0.45 */
#define YOLO_DICT_OVERLAP        "Overlap"         /* 0.2（滑窗重叠度 0~0.9；步长 = 窗口×(1-重叠度)）*/
#define YOLO_DICT_STEP           "\xe6\xad\xa5\xe9\x95\xbf"                    /* 步长（X/Y 同值）*/
#define YOLO_DICT_STEP_X         "\xe6\xad\xa5\xe9\x95\xbfX"                   /* 步长X */
#define YOLO_DICT_STEP_Y         "\xe6\xad\xa5\xe9\x95\xbfY"                   /* 步长Y */
#define YOLO_DICT_WIDTH          "\xe5\xae\xbd"                                /* 宽（窗口宽 / 模型输入宽）*/
#define YOLO_DICT_HEIGHT         "\xe9\xab\x98"                                /* 高（窗口高 / 模型输入高）*/

/* 输入 / 输出：图像与结果 */
#define YOLO_DICT_INPUT_IMAGE    "InputImage"        /* 对象：shared_ptr<cv::Mat>       */
#define YOLO_DICT_OUTPUT_IMAGE   "OutputImage"       /* 对象：shared_ptr<ov::Tensor>    */
#define YOLO_DICT_NUM_DETECTIONS "NumDetections"     /* tuple：int                      */
#define YOLO_DICT_CLASS_IDS      "ClassIDs"          /* tuple：整数数组                 */
#define YOLO_DICT_CONFIDENCES    "Confidences"       /* tuple：实数数组                 */
#define YOLO_DICT_CLASS_LABEL    "ClassLabelImage"   /* 对象：shared_ptr<cv::Mat> CV_16UC1 */
#define YOLO_DICT_BOUNDING_BOXES "BoundingBoxes"     /* 对象：shared_ptr<vector<cv::Rect>> */

/*=============================================================================
 * OpenvinoLoadModel(::DictHandle:Handle)
 *   读字典：'工程路径'（必填）、'使用设备'（必填）
 *   写字典：'宽' / '高'（模型输入尺寸）
 *   产出 handle，用完必须 yolo_model_destroy
 *===========================================================================*/
YOLO_CAPI_API int yolo_openvino_load_model(YoloDict dict, YoloModel *model);

/*=============================================================================
 * yolo_openvino_infer(model, dict)   —— 纯 C/C++ 侧的通用推理
 *   （扩展包侧已无对应算子，留给脱离 HALCON 的工程用）
 *   读字典：'InputImage'（必填，8UC1/8UC3）
 *   写字典：'OutputImage'（shared_ptr<ov::Tensor>，模型第 0 个输出的原始张量）
 *===========================================================================*/
YOLO_CAPI_API int yolo_openvino_infer(YoloModel model, YoloDict dict);

/*=============================================================================
 * yolo_seg_detect(::Handle,DictHandle:)
 *   读字典：'InputImage'（必填）、'ConfThreshold' 'MaskThreshold' 'NMSThreshold'
 *           'Overlap' '宽' '高' '步长' '步长X' '步长Y'（均选填）
 *   不缩放：小图 pad 到窗口尺寸（左上对齐、填 0），大图按重叠度滑窗（默认 0.2）
 *   写字典：'NumDetections'（恒写）、'ClassIDs' 'Confidences' 'ClassLabelImage'
 *           'BoundingBoxes'（仅 NumDetections > 0 时写）
 *===========================================================================*/
YOLO_CAPI_API int yolo_seg_detect(YoloModel model, YoloDict dict);

/*=============================================================================
 * 释放模型句柄（等价于 HALCON 侧 handle 的析构）
 *===========================================================================*/
YOLO_CAPI_API void yolo_model_destroy(YoloModel model);

/*=============================================================================
 * 最近一次失败的详细原因（thread_local 文本，例如 OpenVINO 的报错原文）
 *   每次本库调用失败时写入（如 "Available frontends: ..."）；成功调用不清空。
 *   返回的指针在下一次本库调用前有效（HALCON supply 层用它填 HSetErrText）。
 *===========================================================================*/
YOLO_CAPI_API const char *yolo_last_error(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* YOLO_CAPI_H */
