// 本文件只与 yolo_capi（纯 C、不带 OpenVINO 头）打交道，不再直接包含 OpenVINO 头。
// （yolo_core 内部由 yolo.hpp 自己定义 NOMINMAX 并把 OpenVINO 头放在最前）
#include <opencv2/opencv.hpp>      // cv::Mat / cv::Rect / cvtColor：字典里对象载荷的类型
                                   // （本包的 Halcon_Def.h 不带 OpenCV，必须自己包含）

#include "Halcon_Def.h"
#include "Halcon_SoftwarePackage.h"  // 本算子函数的 extern "C" 声明（C 侧包装按 C 名字引用）
#include "Halcon_DictObject.h"      // HDict 里“算子产出对象”的引用载荷（见该头文件说明）
#include "HalconCpp.h"             // HObject / HTuple / 算子调用（本包 Halcon_Def.h 只带 C 接口）
#include "yolo/yolo_capi.h"        // yolo_core 的字典式 C 接口（HALCON-free，与算子 1:1）
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace HalconCpp;

// 算子的字典参数就是 HDict 句柄（句柄里直接装 hdict_core 的 DictPtr）：本文件不做
// 字典镜像，HDict 的 dictPtr 原样喂给 yolo_capi —— 算子与 C 接口读写同一个字典。
// 模型加载 / 输出格式识别 / argmax / NMS / 缩放 / 滑窗 / 掩膜 全在 yolo_core 库内。
//
// 对象载荷（同一个键，两侧各自的自然类型，就地在字典里转换）：
//   yolo_capi 侧：InputImage / ClassLabelImage = shared_ptr<cv::Mat>（8UC1/8UC3、CV_16UC1）
//                 BoundingBoxes = shared_ptr<std::vector<cv::Rect>>、OutputImage = shared_ptr<ov::Tensor>
//                 （OutputImage 只由库里通用的 yolo_openvino_infer 写，包内已无对应算子）
//   HALCON 侧：上述对象键 = shared_ptr<HObject>，用 hdict_set_object / hdict_get_object 存取

// TAG 分配见 README.md「句柄 TAG 分配表」：0xC0FFEE70 = 自研AIcpu（即本 OpenVINO 推理模块）
#define H_Openvino_TAG 0xC0FFEE70
#define H_Openvino_SEM_TYPE "Openvino"
extern "C"
{
    typedef std::mutex OpenvinoMutex;

    typedef struct
    {
        YoloModel OpenVINOModels;   // yolo_capi 的模型句柄（void*）
        HTuple    Keeper;           // 原生 dict：算子产出对象的保活容器（见 StoreHObject）
        OpenvinoMutex Mtx;          // 句柄级互斥：yolo::Model 内只有一个 ov::InferRequest，
                                    // 同一句柄不能并发调用（见 README「线程安全」）
    } OpenvinoHUserHandleData;

    static Herror OpenvinoHUserHandleDestructor(Hproc_handle ph, OpenvinoHUserHandleData *data)
    {
        if (data->OpenVINOModels) yolo_model_destroy(data->OpenVINOModels);
        data->OpenVINOModels = nullptr;
        data->Keeper.~HTuple();   // HAlloc 原始内存：HTuple 需显式析构（释放保活 dict 句柄）
        data->Mtx.~OpenvinoMutex();
        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo OpenvinoHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_Openvino_TAG, H_Openvino_SEM_TYPE, OpenvinoHUserHandleDestructor, NULL, NULL);
}

using MLIQMetrics::hdict::DictPtr;

/*---------------------------------------------------------------------------
 * 返回错误码，并把详细原因挂到该错误码上
 *   必须用 HSetExtendedErrorInfo（带 proc_handle）：老的 HSetErrText 没有句柄参数，
 *   宿主（HDevelop / hrun）看不到我们写的文本，只会显示 "unknown extension error"。
 *-------------------------------------------------------------------------*/
static Herror FailWithText(Hproc_handle proc_handle, Herror code, const char *text)
{
    if (text != nullptr && *text != '\0')
    {
        (void)HSetExtendedErrorInfo(proc_handle, code, text);
    }
    return code;
}

/*---------------------------------------------------------------------------
 * HALCON 图像 → cv::Mat（yolo_capi 的 InputImage 载荷：8UC1 / 8UC3-BGR）
 *   单通道：转成 8UC3 BGR（自持内存——字典里不能再引用 HALCON 的像素缓冲区，
 *           否则字典改键时对象被释放，Mat 就成了悬空视图）
 *   三通道：R/G/B 平面按 BGR 合并成 8UC3
 *-------------------------------------------------------------------------*/
static bool HalconImageToMat(const HObject &image, std::shared_ptr<cv::Mat> &out, std::string &err)
{
    HTuple hv_Channels;
    CountChannels(image, &hv_Channels);
    if (hv_Channels.I() == 3)
    {
        HTuple ptrR, ptrG, ptrB, type, w, h;
        GetImagePointer3(image, &ptrR, &ptrG, &ptrB, &type, &w, &h);
        if (std::string(type[0].S()) != "byte")
        {
            // 只认 8 位图：uint2 / int4 / real 等被当成 8 位读会得到静默错误结果
            err = "InputImage 必须是 byte（8 位）图像";
            return false;
        }
        const int width = w.I();
        const int height = h.I();
        cv::Mat planes[3] = {
            cv::Mat(height, width, CV_8UC1, (uchar *)ptrB.L()),
            cv::Mat(height, width, CV_8UC1, (uchar *)ptrG.L()),
            cv::Mat(height, width, CV_8UC1, (uchar *)ptrR.L())};
        auto bgr = std::make_shared<cv::Mat>();
        cv::merge(planes, 3, *bgr);
        out = bgr;
        return !out->empty();
    }

    HTuple ptr, type, w, h;
    GetImagePointer1(image, &ptr, &type, &w, &h);
    if (std::string(type.S()) != "byte")
    {
        err = "InputImage 必须是 byte（8 位）图像";
        return false;
    }
    cv::Mat gray(h.I(), w.I(), CV_8UC1, (uchar *)ptr.L());
    auto bgr = std::make_shared<cv::Mat>();
    cv::cvtColor(gray, *bgr, cv::COLOR_GRAY2BGR);
    out = bgr;
    return !out->empty();
}

/*---------------------------------------------------------------------------
 * 把字典里的 InputImage 归一化成 cv::Mat（yolo_capi 只认 cv::Mat）
 *   ① 纯 C/C++ 侧建的字典：本来就是 shared_ptr<cv::Mat>（调用方自己的内存）
 *   ② HALCON 侧：hdict_set_object 塞的是 HObject → 转成 cv::Mat 并就地覆盖该键
 *      （InputImage 是纯输入键，算子内部归一化后 HALCON 侧无需再读它）
 *-------------------------------------------------------------------------*/
static bool NormalizeInputImage(const DictPtr &dict, std::shared_ptr<cv::Mat> &out, std::string &err)
{
    try
    {
        out = MLIQMetrics::hdict::get_dict_object_as<std::shared_ptr<cv::Mat>>(
            dict, YOLO_DICT_INPUT_IMAGE);
        if (out && !out->empty()) return true;
    }
    catch (...)
    {
    }

    try
    {
        auto obj = MLIQMetrics::hdict::get_dict_object_as<std::shared_ptr<HObject>>(
            dict, YOLO_DICT_INPUT_IMAGE);
        if (!obj || !obj->IsInitialized())
        {
            err = "字典里没有可用的 'InputImage'（需 byte 灰度或 3 通道图，用 hdict_set_object 写入）";
            return false;
        }

        std::shared_ptr<cv::Mat> mat;
        if (!HalconImageToMat(*obj, mat, err)) return false;
        MLIQMetrics::hdict::set_dict_object_shared(dict, YOLO_DICT_INPUT_IMAGE, mat);
        out = mat;
        return true;
    }
    catch (...)
    {
        err = "'InputImage' 的载荷类型无法识别（既不是 cv::Mat 也不是 HALCON 图像）";
        return false;
    }
}

/*---------------------------------------------------------------------------
 * 算子产出对象 → HDict（HALCON 侧用 hdict_get_object 取）
 *   HALCON 会在算子返回时回收“本次调用期间创建的对象”，自定义句柄里的键会失效
 *   （下次 hdict_get_object 报 #4052 wrong object-ID）；**原生 dict 是 HALCON 里
 *   唯一能正确持有对象、跨调用存活的容器**，所以先用句柄自带的 Keeper 兜底保活，
 *   再把同一个对象（引用）写进 HDict。
 *-------------------------------------------------------------------------*/
static bool StoreHObject(const DictPtr &dict, HTuple &keeper, const char *key, const HObject &obj)
{
    try
    {
        SetDictObject(obj, keeper, HTuple(key));   // 原生 dict：对象归 dict 所有，跨算子调用存活
    }
    catch (...)
    {
        return false;
    }
    // HDict 里存「原生 dict + 键」的引用，而不是 HObject 拷贝：
    // 算子内部产出的 Region 等对象会随算子上下文回收（拷贝出的键会变成死键 → #4052），
    // 交给 hdict_get_object 在调用方上下文里 GetDictObject 取回才算真存活。
    HDictNativeObjectRefPtr ref = std::make_shared<HDictNativeObjectRef>();
    ref->dict = keeper;
    ref->key  = key;
    MLIQMetrics::hdict::set_dict_object_shared(dict, key, ref);
    return true;
}

/* 从字典取整数键（缺键/类型不符 → 用默认值）*/
static int DictInt(const DictPtr &dict, const char *key, int def = 0)
{
    try
    {
        return MLIQMetrics::hdict::get_dict_tuple(dict, key).get<int>();
    }
    catch (...)
    {
        return def;
    }
}

/*---------------------------------------------------------------------------
 * 结果里的对象键 → HObject（供 HALCON 侧 hdict_get_object 取）
 *   ClassLabelImage：CV_16UC1 标签图（0=背景，像素值=类别ID+1）→ uint2 图像
 *   BoundingBoxes  ：vector<cv::Rect> → 矩形 Region 数组
 *-------------------------------------------------------------------------*/
static bool ConvertResultObjects(const DictPtr &dict, HTuple &keeper)
{
    try
    {
        auto label = MLIQMetrics::hdict::get_dict_object_as<std::shared_ptr<cv::Mat>>(
            dict, YOLO_DICT_CLASS_LABEL);
        if (label && !label->empty())
        {
            HObject ho_ClassLabelImage;
            GenImage1(&ho_ClassLabelImage, "uint2", label->cols, label->rows, (int64)label->data);
            StoreHObject(dict, keeper, YOLO_DICT_CLASS_LABEL, ho_ClassLabelImage);
        }

        auto boxes = MLIQMetrics::hdict::get_dict_object_as<std::shared_ptr<std::vector<cv::Rect>>>(
            dict, YOLO_DICT_BOUNDING_BOXES);
        if (boxes && !boxes->empty())
        {
            std::vector<double> hv_Row1, hv_Col1, hv_Row2, hv_Col2;
            hv_Row1.reserve(boxes->size());
            hv_Col1.reserve(boxes->size());
            hv_Row2.reserve(boxes->size());
            hv_Col2.reserve(boxes->size());
            for (const cv::Rect &b : *boxes)
            {
                hv_Row1.push_back((double)b.y);
                hv_Col1.push_back((double)b.x);
                hv_Row2.push_back((double)(b.y + b.height));
                hv_Col2.push_back((double)(b.x + b.width));
            }
            HObject ho_Rects;
            GenRectangle1(&ho_Rects,
                          HTuple(hv_Row1.data(), (Hlong)hv_Row1.size()),
                          HTuple(hv_Col1.data(), (Hlong)hv_Col1.size()),
                          HTuple(hv_Row2.data(), (Hlong)hv_Row2.size()),
                          HTuple(hv_Col2.data(), (Hlong)hv_Col2.size()));
            StoreHObject(dict, keeper, YOLO_DICT_BOUNDING_BOXES, ho_Rects);
        }
    }
    catch (...)
    {
        return false;
    }
    return true;
}

#define Def_INOpenvinoObject(pos, pUserData) \
    OpenvinoHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &OpenvinoHandleTypeUser, &(pUserData))

#define Def_OUTOpenvinoObject(pos, pUserData)                                        \
    OpenvinoHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &OpenvinoHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(OpenvinoHUserHandleData), (void **)(pUserData)))
#define OUTOpenvinoObject(pUserData) (*(pUserData))

Herror Hopenvino_load_model(Hproc_handle proc_handle)
try
{
    // 输入控制 1 = HDict（字典就是 hdict：句柄里的 dictPtr 就是 yolo_capi 的 YoloDict）
    Def_INHDictObjectShared(1, pDict);
    if (!pDict->dictPtr) return 10000 * H__LINE__;

    // 调 C 接口（等价 OpenvinoLoadModel 算子）：'工程路径' / '使用设备' 由它在字典里读，
    // 模型输入尺寸也会写回同一字典的 '宽' / '高'
    YoloModel model = nullptr;
    if (yolo_openvino_load_model(pDict->dictPtr, &model) != YOLO_OK || model == nullptr)
    {
        // 把 C 接口（OpenVINO 原文）的错误原因带进 HALCON 的错误文本，别只给错误码
        return FailWithText(proc_handle, 10000 * H__LINE__, yolo_last_error());
    }

    // 存句柄（HAlloc 是原始内存，字段必须显式赋值）
    Def_OUTOpenvinoObject(1, handle_data);
    OUTOpenvinoObject(handle_data)->OpenVINOModels = model;

    // 原生 dict 保活容器（算子产出对象用，见 StoreHObject）
    new (&(OUTOpenvinoObject(handle_data)->Keeper)) HTuple();
    // 句柄级互斥量（同上：yolo::Model 内只有一个 ov::InferRequest）
    new (&(OUTOpenvinoObject(handle_data)->Mtx)) OpenvinoMutex();
    try
    {
        CreateDict(&OUTOpenvinoObject(handle_data)->Keeper);
    }
    catch (...)
    {
        return 10000 * H__LINE__;
    }

    return H_MSG_TRUE;
}
catch (HalconCpp::HException &e)
{
    // HALCON/C++ 的低层错误：直接沿用其错误码（HALCON 自带文本，不再覆盖）。
    // 绝不能让 C++ 异常穿出 supply —— HDevelop 会 fail-fast 崩溃（0xe06d7363 / 0xc0000409）。
    const Herror code = e.ErrorCode();
    if (code != H_MSG_TRUE) return code;
    const char *msg = e.ErrorMessage();
    return FailWithText(proc_handle, 30050,
                        msg != nullptr && *msg != '\0' ? msg : "HALCON C++ exception");
}
catch (const std::exception &e)
{
    return FailWithText(proc_handle, 30051, e.what());
}
catch (...)
{
    return FailWithText(proc_handle, 30052, "Hopenvino_load_model: unknown C++ exception");
}

/*=============================================================================
 * 算子: Openvino_YOLO_Seg_Detect
 *
 * 模型输出格式（自动识别两种）:
 * - end2end（YOLO26 原生 NMS-free 导出，TopK 已烘焙进图）:
 *     检测结果 [1, 300, D]: [x1,y1,x2,y2, conf, class_id, mask 系数 x32]
 *     无需 NMS（one-to-one 输出不重叠）
 * - raw（ultralytics 常规导出 end2end=False）:
 *   channel-first，每列 [cx,cy,w,h, nc 个类别分(已 sigmoid), mask(proto_c 个系数)...]，框为中心点+宽高
 *     单窗口内先做类别 argmax + 置信度阈值 + NMS（one-to-many 输出大量重叠）
 * - 分割原型 [1, 32, 160, 160]
 *   框坐标均为模型输入尺寸（默认 640x640）下的绝对坐标，算子内映射回原图
 *
 * 输出:
 *   - BoundingBoxes: Region 数组 (每个检测一个矩形Region)
 *   - ClassLabelImage: 灰度图像 (与原图同大小, 像素值=类别ID)
 *   - NumDetections: 检测数量
 *   - Confidences: 置信度数组
 *   - ClassIDs: 类别ID数组
 *
 * 新增功能:
 *   - 支持滑窗检测大图 (通过字典参数 "步长X"/"步长Y" 或 "步长" 设置)
 *   - 支持小图自动缩放到模型尺寸
 *   - 滑窗模式下自动应用 NMS 去重
 *
 * 实现：字典参数就是 HDict 句柄（句柄里就是 hdict），直接交给 yolo_capi 的
 *       yolo_seg_detect；算法与 HALCON 外调用完全一致
 *===========================================================================*/
Herror Hyolo_seg_detect(Hproc_handle proc_handle)
try
{
    // 输入控制 1 = Handle（模型句柄），2 = HDict
    Def_INOpenvinoObject(1, handle_data);
    Def_INHDictObjectShared(2, pDict);
    if (!pDict->dictPtr) return 10000 * H__LINE__;

    DictPtr dict = HDictGetDict(pDict);

    // 同一句柄串行（模型内只有一个 ov::InferRequest）
    std::lock_guard<OpenvinoMutex> guard(handle_data->Mtx);

    // 1. 字典里的 InputImage 归一化成 cv::Mat
    std::shared_ptr<cv::Mat> image;
    std::string img_err;
    if (!NormalizeInputImage(dict, image, img_err))
        return FailWithText(proc_handle, 10000 * H__LINE__, img_err.c_str());

    // 2. 调 C 接口（等价 yolo_seg_detect 算子）：阈值 / 窗口尺寸 / 滑窗步长由它在字典里读，
    //    输出格式自动识别 / argmax / NMS / 缩放 / 滑窗 / 掩膜也在 yolo_core 库内
    if (yolo_seg_detect(handle_data->OpenVINOModels, pDict->dictPtr) != YOLO_OK)
    {
        // 带上 C 接口的具体原因（含失败窗口数 / OpenVINO 异常文本）
        return FailWithText(proc_handle, 10000 * H__LINE__, yolo_last_error());
    }

    // 3. 结果回写字典：NumDetections / ClassIDs / Confidences 本就是元组，yolo_capi 已写好；
    //    只有对象键（ClassLabelImage / BoundingBoxes）需要转成 HObject 供 HALCON 侧取用
    if (DictInt(dict, YOLO_DICT_NUM_DETECTIONS) > 0)
    {
        if (!ConvertResultObjects(dict, handle_data->Keeper)) return 10000 * H__LINE__;
    }

    return H_MSG_TRUE;
}
catch (HalconCpp::HException &e)
{
    // HALCON/C++ 的低层错误：直接沿用其错误码（HALCON 自带文本，不再覆盖）。
    // 绝不能让 C++ 异常穿出 supply —— HDevelop 会 fail-fast 崩溃（0xe06d7363 / 0xc0000409）。
    const Herror code = e.ErrorCode();
    if (code != H_MSG_TRUE) return code;
    const char *msg = e.ErrorMessage();
    return FailWithText(proc_handle, 30050,
                        msg != nullptr && *msg != '\0' ? msg : "HALCON C++ exception");
}
catch (const std::exception &e)
{
    return FailWithText(proc_handle, 30051, e.what());
}
catch (...)
{
    return FailWithText(proc_handle, 30052, "Hyolo_seg_detect: unknown C++ exception");
}
