/*=============================================================================
 * Halcon_OpenCV.cpp — HALCON OpenCV & exiv2 Extension Operators
 * 从 Halcon_YouloBe 迁移的非 OpenVINO 功能
 *
 * 涵盖算子:
 *   remap, PNGIn, PNGOut,
 *   add_roi, mul_roi, sub_B_roi, div_B_roi, div_A_roi, sub_A_roi,
 *   CLAHE_image, write_image_exif,
 *   cv_orb_detect, cv_akaze_detect, cv_sift_detect, cv_bf_knn_match,
 *   cv_estimate_affine_partial2d, cv_estimate_rigid_2d
 *===========================================================================*/

#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h>
  #include <conio.h>
#endif

#include <stdio.h>
#include <iostream>
#include <opencv2/opencv.hpp>
#include "cvr/cvr_measure.hpp"
#include "HalconCpp.h"
#include "HDevThread.h"
#include <string>
#include <vector>
#include <memory>
#include <stdlib.h>
#include <string>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <exiv2/exiv2.hpp>
#include "Halcon_SoftwarePackage.h"

#if !defined(_WIN32) && !defined(_WIN64)
  typedef int64_t  INT64;
  typedef uint64_t UINT64;
#endif

// exiv2 0.28 以上把 Image::AutoPtr / Value::AutoPtr 改名为 UniquePtr
#if defined(EXIV2_TEST_VERSION) && EXIV2_TEST_VERSION(0,28,0)
  using ExivImagePtr = Exiv2::Image::UniquePtr;
  using ExivValuePtr = Exiv2::Value::UniquePtr;
#else
  using ExivImagePtr = Exiv2::Image::AutoPtr;
  using ExivValuePtr = Exiv2::Value::AutoPtr;
#endif

using namespace std;
using namespace HalconCpp;

/*=============================================================================
 * OpenCV 枚举字符串映射
 *   DEF 中相应控制参数 type_list 为 string,integer（向后兼容整数输入）。
 *   字符串不区分大小写；小表（≤16 项）线性查找——比 unordered_map 快且无
 *   首次构造开销。数值字符串（如 "4"）自动按整数解析（HALCON 可能把整数
 *   参数转成字符串再传给 supply）。
 *===========================================================================*/
struct CvEnumEntry { const char* name; int value; };

static bool enum_from_string(const CvEnumEntry* table, size_t n,
                             const char* s, int& out)
{
    if (!s || !*s) return false;
    for (size_t i = 0; i < n; ++i) {
        const char* p = s;
        const char* q = table[i].name;
        while (*p && *q) {
            char a = *p, b = *q;
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            ++p; ++q;
        }
        if (*p == '\0' && *q == '\0') { out = table[i].value; return true; }
    }
    return false;
}

/* 读"字符串或整数"参数。HGetSPar 宏失败时会 return，故拆到小函数里。
 * 注意：不在这里分配字符串内存——每个算子开头统一 HAllocStringMem 一次
 * （多次 HAllocStringMem 会泄漏 temp memory 块，触发 HALCON 弹窗检查）。 */
static Herror fetch_str(Hproc_handle ph, INT4_8 par, std::string& out)
{
    Hcpar p;
    HGetSPar(ph, par, STRING_PAR, &p, 1);
    out = p.par.s ? p.par.s : "";
    return H_MSG_TRUE;
}
static Herror fetch_long(Hproc_handle ph, INT4_8 par, long& out)
{
    Hcpar p;
    HGetSPar(ph, par, LONG_PAR, &p, 1);
    out = p.par.l;
    return H_MSG_TRUE;
}

/* 读取枚举参数：字符串按表映射（大小写不敏感），数值字符串/整数直用。
 * 失败返回 false（调用方应 HSetErrText + 返回参数错误码）。 */
static bool read_enum_param(Hproc_handle ph, INT4_8 par,
                            const CvEnumEntry* table, size_t n, int* out)
{
    std::string s;
    if (fetch_str(ph, par, s) == H_MSG_OK && !s.empty()) {
        if (enum_from_string(table, n, s.c_str(), *out)) return true;
        char* end = nullptr;
        const long v = std::strtol(s.c_str(), &end, 10);
        if (end && *end == '\0') { *out = (int)v; return true; }
        return false;
    }
    long v = 0;
    if (fetch_long(ph, par, v) == H_MSG_OK) { *out = (int)v; return true; }
    return false;
}

/* 预定义枚举表（值 = OpenCV 常量） */
static const CvEnumEntry kBorderTypeTable[] = {
    { "constant",  cv::BORDER_CONSTANT },
    { "replicate", cv::BORDER_REPLICATE },
    { "reflect",   cv::BORDER_REFLECT },
    { "wrap",      cv::BORDER_WRAP },
    { "reflect101", cv::BORDER_REFLECT101 },
    { "default",   cv::BORDER_DEFAULT },
    { "transparent", cv::BORDER_TRANSPARENT },
    { "isolated",  cv::BORDER_ISOLATED },
    { "border_constant",   cv::BORDER_CONSTANT },
    { "border_replicate",  cv::BORDER_REPLICATE },
    { "border_reflect",    cv::BORDER_REFLECT },
    { "border_wrap",       cv::BORDER_WRAP },
    { "border_reflect101", cv::BORDER_REFLECT101 },
    { "border_default",    cv::BORDER_DEFAULT },
    { "border_transparent", cv::BORDER_TRANSPARENT },
    { "border_isolated",   cv::BORDER_ISOLATED },
};
static const size_t kBorderTypeN = sizeof(kBorderTypeTable) / sizeof(kBorderTypeTable[0]);

static const CvEnumEntry kDdepthTable[] = {
    { "src",   -1 },
    { "same",  -1 },
    { "u8",    CV_8U },  { "uint8",  CV_8U },
    { "s8",    CV_8S },  { "int8",   CV_8S },
    { "u16",   CV_16U }, { "uint16", CV_16U },
    { "s16",   CV_16S }, { "int16",  CV_16S },
    { "s32",   CV_32S }, { "int32",  CV_32S },
    { "f32",   CV_32F }, { "float",  CV_32F }, { "float32", CV_32F },
    { "f64",   CV_64F }, { "double", CV_64F }, { "float64", CV_64F },
};
static const size_t kDdepthN = sizeof(kDdepthTable) / sizeof(kDdepthTable[0]);

static const CvEnumEntry kMatchMethodTable[] = {
    { "sqdiff", 0 }, { "sqdiff_normed", 1 },
    { "ccorr", 2 },  { "ccorr_normed", 3 },
    { "ccoeff", 4 }, { "ccoeff_normed", 5 },
    { "tm_sqdiff", 0 }, { "tm_sqdiff_normed", 1 },
    { "tm_ccorr", 2 },  { "tm_ccorr_normed", 3 },
    { "tm_ccoeff", 4 }, { "tm_ccoeff_normed", 5 },
};
static const size_t kMatchMethodN = sizeof(kMatchMethodTable) / sizeof(kMatchMethodTable[0]);

static const CvEnumEntry kKmeansFlagsTable[] = {
    { "random", 0 }, { "random_centers", 0 },
    { "pp", 1 }, { "pp_centers", 1 },
    { "use_initial_labels", 2 },
};
static const size_t kKmeansFlagsN = sizeof(kKmeansFlagsTable) / sizeof(kKmeansFlagsTable[0]);

static const CvEnumEntry kGemmFlagsTable[] = {
    { "none", 0 },
    { "transpose_a", 1 }, { "a_trans", 1 }, { "a_t", 1 },
    { "transpose_b", 2 }, { "b_trans", 2 }, { "b_t", 2 },
    { "transpose_c", 4 }, { "c_trans", 4 }, { "c_t", 4 },
};
static const size_t kGemmFlagsN = sizeof(kGemmFlagsTable) / sizeof(kGemmFlagsTable[0]);

static const CvEnumEntry kMorphOpTable[] = {
    { "erode", 0 }, { "dilate", 1 },
    { "open", 2 },  { "close", 3 },
    { "gradient", 4 }, { "tophat", 5 }, { "blackhat", 6 },
    { "morph_erode", 0 }, { "morph_dilate", 1 },
    { "morph_open", 2 },  { "morph_close", 3 },
    { "morph_gradient", 4 }, { "morph_tophat", 5 }, { "morph_blackhat", 6 },
};
static const size_t kMorphOpN = sizeof(kMorphOpTable) / sizeof(kMorphOpTable[0]);

static const CvEnumEntry kMorphShapeTable[] = {
    { "rect", cv::MORPH_RECT }, { "cross", cv::MORPH_CROSS },
    { "ellipse", cv::MORPH_ELLIPSE },
    { "morph_rect", cv::MORPH_RECT }, { "morph_cross", cv::MORPH_CROSS },
    { "morph_ellipse", cv::MORPH_ELLIPSE },
};
static const size_t kMorphShapeN = sizeof(kMorphShapeTable) / sizeof(kMorphShapeTable[0]);

/*=============================================================================
 * remap 算子
 *===========================================================================*/
Herror HCremap(Hproc_handle proc_handle)
{
    const Hcpar* dict;
    INT4_8 num;
    HAllocStringMem(proc_handle, 1024);
    HGetPPar(proc_handle, 1, &dict, &num);

    HTuple hv_DictHandle(const_cast<Hcpar*>(dict), 1);
    HTuple HandleIndex;

    HObject iMAGE;
    HObject dstiMAGE;
    HObject MapXiMAGE;
    HObject MapYiMAGE;

    GetDictObject(&iMAGE, hv_DictHandle, u8"输入图");
    GetDictObject(&dstiMAGE, hv_DictHandle, u8"输出图");
    GetDictObject(&MapXiMAGE, hv_DictHandle, u8"MapX");
    GetDictObject(&MapYiMAGE, hv_DictHandle, u8"MapY");

    HTuple  hv_Pointer, hv_Type, hv_Width, hv_Height;
    GetImagePointer1(iMAGE, &hv_Pointer, &hv_Type, &hv_Width, &hv_Height);

    HTuple  dsthv_Pointer, dsthv_Type, dsthv_Width, dsthv_Height;
    GetImagePointer1(dstiMAGE, &dsthv_Pointer, &dsthv_Type, &dsthv_Width, &dsthv_Height);

    HTuple  Mapxhv_Pointer, Mapxhv_Type, Mapxhv_Width, Mapxhv_Height;
    GetImagePointer1(MapXiMAGE, &Mapxhv_Pointer, &Mapxhv_Type, &Mapxhv_Width, &Mapxhv_Height);

    HTuple  Mapyhv_Pointer, Mapyhv_Type, Mapyhv_Width, Mapyhv_Height;
    GetImagePointer1(MapYiMAGE, &Mapyhv_Pointer, &Mapyhv_Type, &Mapyhv_Width, &Mapyhv_Height);

    cv::Mat srcImage((int)hv_Height.L(), (int)hv_Width.L(), CV_16UC1, (char*)hv_Pointer.L());
    cv::Mat dstImage((int)dsthv_Height.L(), (int)dsthv_Width.L(), CV_16UC1, (char*)dsthv_Pointer.L());

    cv::Mat xMapArra((int)Mapxhv_Height.L(), (int)Mapxhv_Width.L(), CV_32FC1, (char*)Mapxhv_Pointer.L());
    cv::Mat yMapArra((int)Mapyhv_Height.L(), (int)Mapyhv_Width.L(), CV_32FC1, (char*)Mapyhv_Pointer.L());

    cv::remap(srcImage, dstImage, xMapArra, yMapArra, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));

    return H_MSG_TRUE;
}

/*=============================================================================
 * PNGIn — 将图像编码为 PNG 存入 Halcon 图像（"乱码图"）
 *
 *   单通道：直接 PNG 编码原始像素
 *   三通道：R/G/B 纵向拼接为一张高=3h 的单通道图，再 PNG 编码
 *   输出格式：[8字节压缩后大小][1字节通道数][PNG数据...]
 *   解码端通过通道数字段还原 1 或 3 通道。
 *===========================================================================*/
Herror HPNGIn(Hproc_handle proc_handle)
{
    Hcpar  acceleration;

    Hkey      in_obj_key, out_obj_key, out_image_key;
    Himage    outimage;
    HGetSPar(proc_handle, 1, LONG_PAR, &acceleration, 1);

    HGetObj(proc_handle, 1, 1, &in_obj_key);

    // 判断输入通道数：依次尝试读取，能读到第3通道则为3通道
    INT4_8 num_channels = 1;
    {
        Himage chk2, chk3;
        HGetDImage(proc_handle, in_obj_key, 2, &chk2);
        HGetDImage(proc_handle, in_obj_key, 3, &chk3);
        // 通道3的像素指针非空则认为存在
        if (chk3.pixel.b != NULL) {
            num_channels = 3;
        } else if (chk2.pixel.b != NULL) {
            num_channels = 2;
        }
    }

    cv::Mat img;
    int out_width = 0;
    int out_height = 0;

    if (num_channels == 1)
    {
        // ---- 单通道：原逻辑 ----
        Himage inimage;
        HGetDImage(proc_handle, in_obj_key, 1, &inimage);
        out_width  = inimage.width;
        out_height = inimage.height;

        switch (inimage.kind)
        {
        case UINT2_IMAGE:
            img = cv::Mat(out_height, out_width, CV_16UC1, inimage.pixel.u.p).clone();
            break;
        case BYTE_IMAGE:
            img = cv::Mat(out_height, out_width, CV_8UC1,  inimage.pixel.b).clone();
            break;
        default: return H__LINE__ * 10000;
        }
    }
    else if (num_channels == 3)
    {
        // ---- 三通道：R/G/B 纵向拼接为单通道图 ----
        Himage R, G, B;
        HGetDImage(proc_handle, in_obj_key, 1, &R);
        HGetDImage(proc_handle, in_obj_key, 2, &G);
        HGetDImage(proc_handle, in_obj_key, 3, &B);
        out_width  = R.width;
        out_height = R.height;   // 原始单通道高度，输出时会 ×3

        if (R.kind == UINT2_IMAGE)
        {
            cv::Mat chR(out_height, out_width, CV_16UC1, R.pixel.u.p);
            cv::Mat chG(out_height, out_width, CV_16UC1, G.pixel.u.p);
            cv::Mat chB(out_height, out_width, CV_16UC1, B.pixel.u.p);
            img = cv::Mat(out_height * 3, out_width, CV_16UC1);
            chR.copyTo(img(cv::Rect(0, 0,                 out_width, out_height)));
            chG.copyTo(img(cv::Rect(0, out_height,         out_width, out_height)));
            chB.copyTo(img(cv::Rect(0, out_height * 2,     out_width, out_height)));
        }
        else if (R.kind == BYTE_IMAGE)
        {
            cv::Mat chR(out_height, out_width, CV_8UC1, R.pixel.b);
            cv::Mat chG(out_height, out_width, CV_8UC1, G.pixel.b);
            cv::Mat chB(out_height, out_width, CV_8UC1, B.pixel.b);
            img = cv::Mat(out_height * 3, out_width, CV_8UC1);
            chR.copyTo(img(cv::Rect(0, 0,                 out_width, out_height)));
            chG.copyTo(img(cv::Rect(0, out_height,         out_width, out_height)));
            chB.copyTo(img(cv::Rect(0, out_height * 2,     out_width, out_height)));
        }
        else { return H__LINE__ * 10000; }
    }
    else { return H__LINE__ * 10000; }

    // ---- PNG 编码 ----
    std::vector<uchar> png_buf;
    std::vector<int> params = { cv::IMWRITE_PNG_COMPRESSION, (int)acceleration.par.l };
    cv::imencode(".png", img, png_buf, params);

    // ---- 打包为“乱码图”：8字节size + 1字节通道数 + PNG数据 ----
    size_t   cmpBytes = png_buf.size();
    uint8_t  chFlag   = (uint8_t)num_channels;   // 1 或 3
    size_t   hdrSize  = 8 + 1;                   // size(8) + channel(1)

    int dstHeight = (cmpBytes + hdrSize + out_width - 1) / out_width;
    Herror err = HNewImage(proc_handle, &outimage, BYTE_IMAGE, out_width, dstHeight);
    HCkP(err);
    memset(outimage.pixel.b, 0, out_width * dstHeight);
    memcpy(outimage.pixel.b,       &cmpBytes, 8);
    memcpy(outimage.pixel.b + 8,   &chFlag,   1);
    memcpy(outimage.pixel.b + hdrSize, png_buf.data(), cmpBytes);

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

// PNGOut — 从"乱码图"中解码 PNG，还原 1 或 3 通道图像
Herror HPNGOut(Hproc_handle proc_handle)
{
    Hkey      in_obj_key, out_obj_key, out_image_key;
    Himage    inimage;
    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    // ---- 读取头部：8字节size + 1字节通道数 ----
    size_t  cmpBytes;
    uint8_t chFlag = 1;   // 兼容旧格式（无通道数字段）
    memcpy(&cmpBytes, inimage.pixel.b, 8);

    size_t hdrSize = 8 + 1;
    if (cmpBytes + hdrSize <= (size_t)(inimage.width * inimage.height))
    {
        chFlag = inimage.pixel.b[8];   // 新格式有通道数
    }
    else
    {
        hdrSize = 8;                   // 旧格式只有 size
    }

    // ---- 提取 PNG 数据并解码 ----
    std::vector<uchar> png_buf(inimage.pixel.b + hdrSize,
                               inimage.pixel.b + hdrSize + cmpBytes);
    cv::Mat decoded = cv::imdecode(png_buf, cv::IMREAD_UNCHANGED);
    if (decoded.empty()) return H__LINE__ * 10000;

    HCrObj(proc_handle, 1, &out_obj_key);

    if (chFlag == 1)
    {
        // ---- 单通道输出 ----
        Himage outimage;
        switch (decoded.type())
        {
        case CV_16UC1: {
            Herror e = HNewImage(proc_handle, &outimage, UINT2_IMAGE, decoded.cols, decoded.rows);
            HCkP(e);
            memcpy(outimage.pixel.u.p, decoded.data, decoded.cols * decoded.rows * 2);
            break; }
        case CV_8UC1: {
            Herror e = HNewImage(proc_handle, &outimage, BYTE_IMAGE, decoded.cols, decoded.rows);
            HCkP(e);
            memcpy(outimage.pixel.b, decoded.data, decoded.cols * decoded.rows);
            break; }
        default: return H__LINE__ * 10000;
        }
        HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
        HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);
    }
    else if (chFlag == 3)
    {
        // ---- 三通道输出：纵向 1/3 拆分 R/G/B ----
        int w = decoded.cols;
        int h = decoded.rows / 3;
        if (h * 3 != decoded.rows) return H__LINE__ * 10000;

        Himage outR, outG, outB;

        // 提取 R 通道（顶部）
        {
            cv::Mat roi = decoded(cv::Rect(0, 0, w, h));
            if (decoded.depth() == CV_16U) {
                Herror e = HNewImage(proc_handle, &outR, UINT2_IMAGE, w, h);
                HCkP(e);
                memcpy(outR.pixel.u.p, roi.data, w * h * 2);
            } else {
                Herror e = HNewImage(proc_handle, &outR, BYTE_IMAGE, w, h);
                HCkP(e);
                memcpy(outR.pixel.b, roi.data, w * h);
            }
        }
        // 提取 G 通道（中部）
        {
            cv::Mat roi = decoded(cv::Rect(0, h, w, h));
            if (decoded.depth() == CV_16U) {
                Herror e = HNewImage(proc_handle, &outG, UINT2_IMAGE, w, h);
                HCkP(e);
                memcpy(outG.pixel.u.p, roi.data, w * h * 2);
            } else {
                Herror e = HNewImage(proc_handle, &outG, BYTE_IMAGE, w, h);
                HCkP(e);
                memcpy(outG.pixel.b, roi.data, w * h);
            }
        }
        // 提取 B 通道（底部）
        {
            cv::Mat roi = decoded(cv::Rect(0, h * 2, w, h));
            if (decoded.depth() == CV_16U) {
                Herror e = HNewImage(proc_handle, &outB, UINT2_IMAGE, w, h);
                HCkP(e);
                memcpy(outB.pixel.u.p, roi.data, w * h * 2);
            } else {
                Herror e = HNewImage(proc_handle, &outB, BYTE_IMAGE, w, h);
                HCkP(e);
                memcpy(outB.pixel.b, roi.data, w * h);
            }
        }

        HPutDImage(proc_handle, out_obj_key, 1, &outR, FALSE, &out_image_key);
        HPutDImage(proc_handle, out_obj_key, 2, &outG, FALSE, &out_image_key);
        HPutDImage(proc_handle, out_obj_key, 3, &outB, FALSE, &out_image_key);
        HPutRect(proc_handle, out_obj_key, w, h);
    }
    else { return H__LINE__ * 10000; }

    return H_MSG_TRUE;
}

/*=============================================================================
 * ROI 算术运算辅助函数
 *===========================================================================*/
int roi_error(Himage small_image, Himage big_image, int x, int y, int w, int h)
{
    if (small_image.kind != UINT2_IMAGE) return 1;
    if (big_image.kind != UINT2_IMAGE) return 2;
    if ((x < 0) || (y < 0) || (w < 0) || (h < 0)) return 3;
    if (x + w > big_image.width) return 4;
    if (y + h > big_image.height) return 5;
    if (small_image.width != w) return 6;
    if (small_image.height != h) return 7;
    return 0;
}

// A + (B ∩ Roi)
int add_roi(Himage small_image, Himage big_image, int x, int y, int w, int h)
{
    int error = roi_error(small_image, big_image, x, y, w, h);
    if (error != 0) return error;
    cv::Mat small_imagein((int)small_image.height, (int)small_image.width, CV_16UC1, small_image.pixel.u.p);
    cv::Mat big_imagein((int)big_image.height, (int)big_image.width, CV_16UC1, big_image.pixel.u.p);
    cv::add(small_imagein, big_imagein(cv::Rect(x, y, w, h)), big_imagein(cv::Rect(x, y, w, h)));
    return 0;
}

// A * (B ∩ Roi)
int mul_roi(Himage small_image, Himage big_image, int x, int y, int w, int h)
{
    int error = roi_error(small_image, big_image, x, y, w, h);
    if (error != 0) return error;
    cv::Mat small_imagein((int)small_image.height, (int)small_image.width, CV_16UC1, small_image.pixel.u.p);
    cv::Mat big_imagein((int)big_image.height, (int)big_image.width, CV_16UC1, big_image.pixel.u.p);
    cv::multiply(small_imagein, big_imagein(cv::Rect(x, y, w, h)), big_imagein(cv::Rect(x, y, w, h)));
    return 0;
}

// A - (B ∩ Roi)
int sub_B_roi(Himage small_image, Himage big_image, int x, int y, int w, int h)
{
    int error = roi_error(small_image, big_image, x, y, w, h);
    if (error != 0) return error;
    cv::Mat small_imagein((int)small_image.height, (int)small_image.width, CV_16UC1, small_image.pixel.u.p);
    cv::Mat big_imagein((int)big_image.height, (int)big_image.width, CV_16UC1, big_image.pixel.u.p);
    cv::subtract(small_imagein, big_imagein(cv::Rect(x, y, w, h)), big_imagein(cv::Rect(x, y, w, h)));
    return 0;
}

// A / (B ∩ Roi)
int div_B_roi(Himage small_image, Himage big_image, int x, int y, int w, int h)
{
    int error = roi_error(small_image, big_image, x, y, w, h);
    if (error != 0) return error;
    cv::Mat small_imagein((int)small_image.height, (int)small_image.width, CV_16UC1, small_image.pixel.u.p);
    cv::Mat big_imagein((int)big_image.height, (int)big_image.width, CV_16UC1, big_image.pixel.u.p);
    cv::divide(small_imagein, big_imagein(cv::Rect(x, y, w, h)), big_imagein(cv::Rect(x, y, w, h)));
    return 0;
}

// (A ∩ Roi) / B
int div_A_roi(Himage big_image, Himage small_image, int x, int y, int w, int h)
{
    int error = roi_error(small_image, big_image, x, y, w, h);
    if (error != 0) return error;
    cv::Mat small_imagein((int)small_image.height, (int)small_image.width, CV_16UC1, small_image.pixel.u.p);
    cv::Mat big_imagein((int)big_image.height, (int)big_image.width, CV_16UC1, big_image.pixel.u.p);
    cv::divide(big_imagein(cv::Rect(x, y, w, h)), small_imagein, big_imagein(cv::Rect(x, y, w, h)));
    return 0;
}

// (A ∩ Roi) - B
int sub_A_roi(Himage big_image, Himage small_image, int x, int y, int w, int h)
{
    int error = roi_error(small_image, big_image, x, y, w, h);
    if (error != 0) return error;
    cv::Mat small_imagein((int)small_image.height, (int)small_image.width, CV_16UC1, small_image.pixel.u.p);
    cv::Mat big_imagein((int)big_image.height, (int)big_image.width, CV_16UC1, big_image.pixel.u.p);
    cv::subtract(big_imagein(cv::Rect(x, y, w, h)), small_imagein, big_imagein(cv::Rect(x, y, w, h)));
    return 0;
}

/*=============================================================================
 * add_roi 算子
 *===========================================================================*/
Herror HCadd_roi(Hproc_handle proc_handle)
{
    Hkey in_smallobj_key, in_bigobj_key, out_image_key;
    Himage    insmallimage;
    Himage    inbig_image;
    Hcpar sy, sx, ew, eh;
    INT4_8 iRes;
    HGetSPar(proc_handle, 1, LONG_PAR, &sy, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &sx, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &ew, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &eh, 1);

    HGetObj(proc_handle, 1, 1, &in_smallobj_key);
    HGetDImage(proc_handle, in_smallobj_key, 1, &insmallimage);
    HGetObj(proc_handle, 2, 1, &in_bigobj_key);
    HGetDImage(proc_handle, in_bigobj_key, 1, &inbig_image);

    iRes = add_roi(insmallimage, inbig_image, sx.par.l, sy.par.l, ew.par.l, eh.par.l);
    if (0 != iRes) return 30000 + iRes;
    return H_MSG_TRUE;
}

/*=============================================================================
 * mul_roi 算子
 *===========================================================================*/
Herror HCmul_roi(Hproc_handle proc_handle)
{
    Hkey in_smallobj_key, in_bigobj_key, out_image_key;
    Himage    insmallimage;
    Himage    inbig_image;
    Hcpar sy, sx, ew, eh;
    INT4_8 iRes;
    HGetSPar(proc_handle, 1, LONG_PAR, &sy, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &sx, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &ew, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &eh, 1);

    HGetObj(proc_handle, 1, 1, &in_smallobj_key);
    HGetDImage(proc_handle, in_smallobj_key, 1, &insmallimage);
    HGetObj(proc_handle, 2, 1, &in_bigobj_key);
    HGetDImage(proc_handle, in_bigobj_key, 1, &inbig_image);

    iRes = mul_roi(insmallimage, inbig_image, sx.par.l, sy.par.l, ew.par.l, eh.par.l);
    if (0 != iRes) return 30000 + iRes;
    return H_MSG_TRUE;
}

/*=============================================================================
 * sub_B_roi 算子
 *===========================================================================*/
Herror HCsub_B_roi(Hproc_handle proc_handle)
{
    Hkey in_smallobj_key, in_bigobj_key, out_image_key;
    Himage    insmallimage;
    Himage    inbig_image;
    Hcpar sy, sx, ew, eh;
    INT4_8 iRes;
    HGetSPar(proc_handle, 1, LONG_PAR, &sy, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &sx, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &ew, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &eh, 1);

    HGetObj(proc_handle, 1, 1, &in_smallobj_key);
    HGetDImage(proc_handle, in_smallobj_key, 1, &insmallimage);
    HGetObj(proc_handle, 2, 1, &in_bigobj_key);
    HGetDImage(proc_handle, in_bigobj_key, 1, &inbig_image);

    iRes = sub_B_roi(insmallimage, inbig_image, sx.par.l, sy.par.l, ew.par.l, eh.par.l);
    if (0 != iRes) return 30000 + iRes;
    return H_MSG_TRUE;
}

/*=============================================================================
 * div_B_roi 算子
 *===========================================================================*/
Herror HCdiv_B_roi(Hproc_handle proc_handle)
{
    Hkey in_smallobj_key, in_bigobj_key, out_image_key;
    Himage    insmallimage;
    Himage    inbig_image;
    Hcpar sy, sx, ew, eh;
    INT4_8 iRes;
    HGetSPar(proc_handle, 1, LONG_PAR, &sy, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &sx, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &ew, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &eh, 1);

    HGetObj(proc_handle, 1, 1, &in_smallobj_key);
    HGetDImage(proc_handle, in_smallobj_key, 1, &insmallimage);
    HGetObj(proc_handle, 2, 1, &in_bigobj_key);
    HGetDImage(proc_handle, in_bigobj_key, 1, &inbig_image);

    iRes = div_B_roi(insmallimage, inbig_image, sx.par.l, sy.par.l, ew.par.l, eh.par.l);
    if (0 != iRes) return 30000 + iRes;
    return H_MSG_TRUE;
}

/*=============================================================================
 * div_A_roi 算子
 *===========================================================================*/
Herror HCdiv_A_roi(Hproc_handle proc_handle)
{
    Hkey in_smallobj_key, in_bigobj_key, out_image_key;
    Himage    insmallimage;
    Himage    inbig_image;
    Hcpar sy, sx, ew, eh;
    INT4_8 iRes;
    HGetSPar(proc_handle, 1, LONG_PAR, &sy, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &sx, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &ew, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &eh, 1);

    /* DEF 对象顺序为 (smallimage, bigimage)，与 B 系算子保持一致 */
    HGetObj(proc_handle, 1, 1, &in_smallobj_key);
    HGetDImage(proc_handle, in_smallobj_key, 1, &insmallimage);
    HGetObj(proc_handle, 2, 1, &in_bigobj_key);
    HGetDImage(proc_handle, in_bigobj_key, 1, &inbig_image);

    iRes = div_A_roi(inbig_image, insmallimage, sx.par.l, sy.par.l, ew.par.l, eh.par.l);
    if (0 != iRes) return 30000 + iRes;
    return H_MSG_TRUE;
}

/*=============================================================================
 * sub_A_roi 算子
 *===========================================================================*/
Herror HCsub_A_roi(Hproc_handle proc_handle)
{
    Hkey in_smallobj_key, in_bigobj_key, out_image_key;
    Himage    insmallimage;
    Himage    inbig_image;
    Hcpar sy, sx, ew, eh;
    INT4_8 iRes;
    HGetSPar(proc_handle, 1, LONG_PAR, &sy, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &sx, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &ew, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &eh, 1);

    /* DEF 对象顺序为 (smallimage, bigimage)，与 B 系算子保持一致 */
    HGetObj(proc_handle, 1, 1, &in_smallobj_key);
    HGetDImage(proc_handle, in_smallobj_key, 1, &insmallimage);
    HGetObj(proc_handle, 2, 1, &in_bigobj_key);
    HGetDImage(proc_handle, in_bigobj_key, 1, &inbig_image);

    iRes = sub_A_roi(inbig_image, insmallimage, sx.par.l, sy.par.l, ew.par.l, eh.par.l);
    if (0 != iRes) return 30000 + iRes;
    return H_MSG_TRUE;
}

/*=============================================================================
 * CLAHE_image 算子
 *===========================================================================*/
Herror HCCLAHE_image(Hproc_handle proc_handle)
{
    Hkey      in_obj_key, out_obj_key, out_image_key;
    Himage    inimage;
    Himage    outimage;
    Hcpar     k_width, k_height, clipLimit;

    HGetSPar(proc_handle, 1, LONG_PAR, &k_width, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &k_height, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &clipLimit, 1);

    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    if (k_width.par.l <= 0 || k_height.par.l <= 0 || clipLimit.par.l < 0)
        return 30001;

    if (inimage.kind != BYTE_IMAGE && inimage.kind != UINT2_IMAGE)
        return 30002;

    int  cvType      = (inimage.kind == BYTE_IMAGE) ? CV_8UC1 : CV_16UC1;
    void* inPixels   = (inimage.kind == BYTE_IMAGE) ? (void*)inimage.pixel.b
                                                    : (void*)inimage.pixel.u.p;

    cv::Mat imageIn(inimage.height, inimage.width, cvType, inPixels);

    HCkP(HNewImage(proc_handle, &outimage, inimage.kind, inimage.width, inimage.height));
    void* outPixels  = (inimage.kind == BYTE_IMAGE) ? (void*)outimage.pixel.b
                                                    : (void*)outimage.pixel.u.p;
    cv::Mat imageOut(outimage.height, outimage.width, cvType, outPixels);

    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(
        static_cast<double>(clipLimit.par.l),
        cv::Size(static_cast<int>(k_width.par.l), static_cast<int>(k_height.par.l))
    );

    try
    {
        clahe->apply(imageIn, imageOut);
    }
    catch (const cv::Exception& e)
    {
        return 30004;
    }

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * write_image_exif — EXIF 元数据写入
 *===========================================================================*/

#if defined(_WIN32) || defined(_WIN64)
string utf8Path(const wstring& wpath)
{
    if (wpath.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

wstring widePath(const string& path)
{
    if (path.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &result[0], size);
    return result;
}
#endif

string decimalToExifString(double decimal)
{
    double abs_val = fabs(decimal);
    int32_t deg = static_cast<int32_t>(abs_val);
    double min_float = (abs_val - deg) * 60.0;
    int32_t min = static_cast<int32_t>(min_float);
    double sec = (min_float - min) * 60.0;

    int32_t sec_num = static_cast<int32_t>(sec * 100);
    int32_t sec_den = 100;

    auto gcd = [](int32_t a, int32_t b) {
        while (b != 0) { int32_t t = b; b = a % b; a = t; }
        return a;
    };
    int32_t g = gcd(sec_num, sec_den);

    return to_string(deg) + "/1 " + to_string(min) + "/1 " +
           to_string(sec_num / g) + "/" + to_string(sec_den / g);
}

void eraseExifKey(Exiv2::ExifData& exif, const char* key)
{
    auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it != exif.end()) exif.erase(it);
}

bool writeImageExif(const string& imagePath,
                    double latitude, double longitude, double altitude,
                    double aperture, double shutterSpeed, int iso,
                    double focalLength, const string& dateTime,
                    const string& make, const string& model)
{
    try {
        ExivImagePtr image = Exiv2::ImageFactory::open(imagePath);
        if (!image.get()) {
            cerr << "错误：无法打开图像 " << imagePath << endl;
            return false;
        }

        image->readMetadata();
        Exiv2::ExifData& exif = image->exifData();

        // GPS 纬度
        eraseExifKey(exif, "Exif.GPSInfo.GPSLatitude");
        exif["Exif.GPSInfo.GPSLatitudeRef"] = (latitude >= 0) ? "N" : "S";
        ExivValuePtr latVal = Exiv2::Value::create(Exiv2::unsignedRational);
        latVal->read(decimalToExifString(latitude));
        exif.add(Exiv2::ExifKey("Exif.GPSInfo.GPSLatitude"), latVal.get());

        // GPS 经度
        eraseExifKey(exif, "Exif.GPSInfo.GPSLongitude");
        exif["Exif.GPSInfo.GPSLongitudeRef"] = (longitude >= 0) ? "E" : "W";
        ExivValuePtr lonVal = Exiv2::Value::create(Exiv2::unsignedRational);
        lonVal->read(decimalToExifString(longitude));
        exif.add(Exiv2::ExifKey("Exif.GPSInfo.GPSLongitude"), lonVal.get());

        // 海拔
        eraseExifKey(exif, "Exif.GPSInfo.GPSAltitude");
        exif["Exif.GPSInfo.GPSAltitudeRef"] = static_cast<uint16_t>(0);
        int32_t alt_num = static_cast<int32_t>(fabs(altitude) * 100);
        exif["Exif.GPSInfo.GPSAltitude"] = Exiv2::Rational(alt_num, 100);

        exif["Exif.GPSInfo.GPSVersionID"] = static_cast<uint16_t>(2);

        // 光圈
        if (aperture > 0) {
            int32_t fnum = static_cast<int32_t>(aperture * 10);
            exif["Exif.Photo.FNumber"] = Exiv2::Rational(fnum, 10);
            exif["Exif.Photo.ApertureValue"] = Exiv2::Rational(fnum, 10);
        }

        // 曝光时间
        if (shutterSpeed > 0) {
            if (shutterSpeed >= 1.0) {
                exif["Exif.Photo.ExposureTime"] = Exiv2::Rational(static_cast<int32_t>(shutterSpeed), 1);
            } else {
                int32_t denom = static_cast<int32_t>(1.0 / shutterSpeed);
                exif["Exif.Photo.ExposureTime"] = Exiv2::Rational(1, denom);
            }
        }

        // ISO
        if (iso > 0) {
            exif["Exif.Photo.ISOSpeedRatings"] = static_cast<uint16_t>(iso);
        }

        // 焦距
        if (focalLength > 0) {
            int32_t focal = static_cast<int32_t>(focalLength * 10);
            exif["Exif.Photo.FocalLength"] = Exiv2::Rational(focal, 10);
        }

        // 拍摄时间
        if (!dateTime.empty()) {
            exif["Exif.Photo.DateTimeOriginal"] = dateTime;
            exif["Exif.Photo.DateTimeDigitized"] = dateTime;
            exif["Exif.Image.DateTime"] = dateTime;
        }

        // 相机厂商/型号
        if (!make.empty()) exif["Exif.Image.Make"] = make;
        if (!model.empty()) exif["Exif.Image.Model"] = model;

        image->setExifData(exif);
        image->writeMetadata();

        cout << "成功写入 EXIF: " << imagePath << endl;
        return true;

    } catch (Exiv2::Error& e) {
        cerr << "EXIF 错误: " << e.what() << endl;
        return false;
    } catch (exception& e) {
        cerr << "标准错误: " << e.what() << endl;
        return false;
    }
}

Herror HCWriteImageExif(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024);

    char const* const* imagePath;
    double const* latitude;
    double const* longitude;
    double const* altitude;
    double const* aperture;
    double const* shutterSpeed;
    INT4_8 const* iso;
    double const* focalLength;
    char const* const* dateTime;
    char const* const* make;
    char const* const* model;
    INT4_8 num;

    HGetPElemS(proc_handle, 1, CONV_NONE, &imagePath, &num);
    HGetPElemD(proc_handle, 2, CONV_NONE, &latitude, &num);
    HGetPElemD(proc_handle, 3, CONV_NONE, &longitude, &num);
    HGetPElemD(proc_handle, 4, CONV_NONE, &altitude, &num);
    HGetPElemD(proc_handle, 5, CONV_NONE, &aperture, &num);
    HGetPElemD(proc_handle, 6, CONV_NONE, &shutterSpeed, &num);
    HGetPElemL(proc_handle, 7, CONV_NONE, &iso, &num);
    HGetPElemD(proc_handle, 8, CONV_NONE, &focalLength, &num);
    HGetPElemS(proc_handle, 9, CONV_NONE, &dateTime, &num);
    HGetPElemS(proc_handle, 10, CONV_NONE, &make, &num);
    HGetPElemS(proc_handle, 11, CONV_NONE, &model, &num);

    bool result = writeImageExif(
        string(imagePath[0]),
        latitude[0], longitude[0], altitude[0],
        aperture[0], shutterSpeed[0],
        static_cast<int>(iso[0]),
        focalLength[0],
        string(dateTime[0]),
        string(make[0]),
        string(model[0])
    );

    if (!result) return 30001;
    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_orb_detect — ORB 特征检测
 *===========================================================================*/
Herror HCcv_orb_detect(Hproc_handle proc_handle)
{
    const Hcpar *dict;
    INT4_8 num;
    HAllocStringMem(proc_handle, 1024);
    HGetPPar(proc_handle, 1, &dict, &num);
    HTuple hv_DictHandle(const_cast<Hcpar*>(dict), 1);

    HObject ho_InputImage;
    GetDictObject(&ho_InputImage, hv_DictHandle, "InputImage");

    HTuple hv_Pointer, hv_Type, hv_Width, hv_Height;
    GetImagePointer1(ho_InputImage, &hv_Pointer, &hv_Type, &hv_Width, &hv_Height);

    cv::Mat img((int)hv_Height.L(), (int)hv_Width.L(), CV_8UC1,
                (uchar *)hv_Pointer.L());

    HTuple hv_NFeatures;
    try { GetDictTuple(hv_DictHandle, "NFeatures", &hv_NFeatures); }
    catch (...) { hv_NFeatures = 3000; }
    int nFeatures = (int)hv_NFeatures.L();

    /* ---- ORB 全开放参数（从 dict 键读取，带默认值） ---- */
    HTuple t;
    double scaleFactor  = 1.2;
    int    nlevels      = 8;
    int    edgeThreshold = 31;
    int    firstLevel   = 0;
    int    WTA_K        = 2;
    int    scoreType    = 0;   // 0=HARRIS, 1=FAST
    int    patchSize    = 31;
    int    fastThreshold = 20;
    try { GetDictTuple(hv_DictHandle, "ScaleFactor", &t);   scaleFactor   = t.D(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "NLevels", &t);       nlevels       = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "EdgeThreshold", &t); edgeThreshold = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "FirstLevel", &t);    firstLevel    = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "WTA_K", &t);         WTA_K         = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "ScoreType", &t);     scoreType     = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "PatchSize", &t);     patchSize     = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "FastThreshold", &t); fastThreshold = (int)t.L(); } catch (...) {}

    cv::Ptr<cv::ORB> orb = cv::ORB::create(
        nFeatures, (float)scaleFactor, nlevels, edgeThreshold,
        firstLevel, WTA_K, (cv::ORB::ScoreType)scoreType, patchSize,
        fastThreshold);

    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;
    orb->detectAndCompute(img, cv::noArray(), keypoints, descriptors);

    int nKP = (int)keypoints.size();

    HTuple hv_Rows, hv_Cols;
    for (int i = 0; i < nKP; i++)
    {
        hv_Rows[i] = (double)keypoints[i].pt.y;
        hv_Cols[i] = (double)keypoints[i].pt.x;
    }
    SetDictTuple(hv_DictHandle, "KeypointsRow", hv_Rows);
    SetDictTuple(hv_DictHandle, "KeypointsCol", hv_Cols);
    SetDictTuple(hv_DictHandle, "NumKeypoints", (Hlong)nKP);

    if (nKP > 0 && !descriptors.empty())
    {
        HObject ho_Desc;
        GenImage1(&ho_Desc, "byte", 32, nKP, (Hlong)descriptors.data);
        SetDictObject(ho_Desc, hv_DictHandle, "Descriptors");
    }

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_akaze_detect — AKAZE 特征检测
 *===========================================================================*/
Herror HCcv_akaze_detect(Hproc_handle proc_handle)
{
    const Hcpar *dict;
    INT4_8 num;
    HAllocStringMem(proc_handle, 1024);
    HGetPPar(proc_handle, 1, &dict, &num);
    HTuple hv_DictHandle(const_cast<Hcpar*>(dict), 1);

    HObject ho_InputImage;
    GetDictObject(&ho_InputImage, hv_DictHandle, "InputImage");

    HTuple hv_Pointer, hv_Type, hv_Width, hv_Height;
    GetImagePointer1(ho_InputImage, &hv_Pointer, &hv_Type, &hv_Width, &hv_Height);

    cv::Mat img((int)hv_Height.L(), (int)hv_Width.L(), CV_8UC1,
                (uchar *)hv_Pointer.L());

    /* ---- AKAZE 全开放参数（从 dict 键读取，带默认值） ---- */
    HTuple t;
    int    descriptorType     = 4;    // 4=MLDB（默认）；0=KAZE,1=KAZE_UPRIGHT,2=MLDB_UPRIGHT
    int    descriptorSize     = 0;
    int    descriptorChannels = 3;
    double threshold          = 0.001;
    int    nOctaves           = 4;
    int    nOctaveLayers      = 4;
    int    diffusivity        = 1;    // 1=PM_G2；0=PM_G1,2=WEICKERT,3=CHARBONNIER
    try { GetDictTuple(hv_DictHandle, "DescriptorType", &t);     descriptorType     = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "DescriptorSize", &t);     descriptorSize     = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "DescriptorChannels", &t); descriptorChannels = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "Threshold", &t);          threshold          = t.D(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "NOctaves", &t);           nOctaves           = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "NOctaveLayers", &t);      nOctaveLayers      = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "Diffusivity", &t);        diffusivity        = (int)t.L(); } catch (...) {}

    cv::Ptr<cv::AKAZE> akaze = cv::AKAZE::create(
        (cv::AKAZE::DescriptorType)descriptorType, descriptorSize,
        descriptorChannels, (float)threshold, nOctaves, nOctaveLayers,
        (cv::KAZE::DiffusivityType)diffusivity);

    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;
    akaze->detectAndCompute(img, cv::noArray(), keypoints, descriptors);

    int nKP = (int)keypoints.size();

    HTuple hv_Rows, hv_Cols;
    for (int i = 0; i < nKP; i++)
    {
        hv_Rows[i] = (double)keypoints[i].pt.y;
        hv_Cols[i] = (double)keypoints[i].pt.x;
    }
    SetDictTuple(hv_DictHandle, "KeypointsRow", hv_Rows);
    SetDictTuple(hv_DictHandle, "KeypointsCol", hv_Cols);
    SetDictTuple(hv_DictHandle, "NumKeypoints", (Hlong)nKP);

    if (nKP > 0 && !descriptors.empty())
    {
        int descWidth = descriptors.cols;

        cv::Mat descU8;
        if (descriptors.type() != CV_8UC1)
            descriptors.convertTo(descU8, CV_8UC1);
        else
            descU8 = descriptors;

        HObject ho_Desc;
        GenImage1(&ho_Desc, "byte", descWidth, nKP, (Hlong)descU8.data);
        SetDictObject(ho_Desc, hv_DictHandle, "Descriptors");
        SetDictTuple(hv_DictHandle, "DescWidth", (Hlong)descWidth);
    }

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_sift_detect — SIFT 特征检测
 *===========================================================================*/
Herror HCcv_sift_detect(Hproc_handle proc_handle)
{
    const Hcpar *dict;
    INT4_8 num;
    HAllocStringMem(proc_handle, 1024);
    HGetPPar(proc_handle, 1, &dict, &num);
    HTuple hv_DictHandle(const_cast<Hcpar*>(dict), 1);

    HObject ho_InputImage;
    GetDictObject(&ho_InputImage, hv_DictHandle, "InputImage");

    HTuple hv_Pointer, hv_Type, hv_Width, hv_Height;
    GetImagePointer1(ho_InputImage, &hv_Pointer, &hv_Type, &hv_Width, &hv_Height);

    cv::Mat img((int)hv_Height.L(), (int)hv_Width.L(), CV_8UC1,
                (uchar *)hv_Pointer.L());

    HTuple hv_NFeatures;
    try { GetDictTuple(hv_DictHandle, "NFeatures", &hv_NFeatures); }
    catch (...) { hv_NFeatures = 0; }
    int nFeatures = (int)hv_NFeatures.L();

    /* ---- SIFT 全开放参数（从 dict 键读取，带默认值） ---- */
    HTuple t;
    int    nOctaveLayers     = 3;
    double contrastThreshold = 0.04;
    double edgeThreshold     = 10.0;
    double sigma             = 1.6;
    try { GetDictTuple(hv_DictHandle, "NOctaveLayers", &t);     nOctaveLayers     = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "ContrastThreshold", &t); contrastThreshold = t.D(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "EdgeThreshold", &t);     edgeThreshold     = t.D(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "Sigma", &t);             sigma             = t.D(); } catch (...) {}

    // descriptorType 取 CV_8U，使描述子与 cv_bf_knn_match（NORM_HAMMING）直接兼容
    cv::Ptr<cv::SIFT> sift = cv::SIFT::create(
        nFeatures, nOctaveLayers, contrastThreshold, edgeThreshold,
        sigma, CV_8U);

    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;
    sift->detectAndCompute(img, cv::noArray(), keypoints, descriptors);

    int nKP = (int)keypoints.size();

    HTuple hv_Rows, hv_Cols;
    for (int i = 0; i < nKP; i++)
    {
        hv_Rows[i] = (double)keypoints[i].pt.y;
        hv_Cols[i] = (double)keypoints[i].pt.x;
    }
    SetDictTuple(hv_DictHandle, "KeypointsRow", hv_Rows);
    SetDictTuple(hv_DictHandle, "KeypointsCol", hv_Cols);
    SetDictTuple(hv_DictHandle, "NumKeypoints", (Hlong)nKP);

    if (nKP > 0 && !descriptors.empty())
    {
        cv::Mat descU8;
        if (descriptors.type() != CV_8UC1)
            descriptors.convertTo(descU8, CV_8UC1);
        else
            descU8 = descriptors;

        int descWidth = descU8.cols;
        HObject ho_Desc;
        GenImage1(&ho_Desc, "byte", descWidth, nKP, (Hlong)descU8.data);
        SetDictObject(ho_Desc, hv_DictHandle, "Descriptors");
        SetDictTuple(hv_DictHandle, "DescWidth", (Hlong)descWidth);
    }

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_bf_knn_match — BF 暴力匹配 + Lowe's Ratio Test
 *===========================================================================*/
Herror HCcv_bf_knn_match(Hproc_handle proc_handle)
{
    const Hcpar *dict;
    INT4_8 num;
    HAllocStringMem(proc_handle, 1024);
    HGetPPar(proc_handle, 1, &dict, &num);
    HTuple hv_DictHandle(const_cast<Hcpar*>(dict), 1);

    HObject ho_DescRef;
    GetDictObject(&ho_DescRef, hv_DictHandle, "DescriptorsRef");
    HTuple ptrRef, typeRef, wRef, hRef;
    GetImagePointer1(ho_DescRef, &ptrRef, &typeRef, &wRef, &hRef);

    HObject ho_DescTarget;
    GetDictObject(&ho_DescTarget, hv_DictHandle, "DescriptorsTarget");
    HTuple ptrTarget, typeTarget, wTarget, hTarget;
    GetImagePointer1(ho_DescTarget, &ptrTarget, &typeTarget, &wTarget, &hTarget);

    HTuple hv_DescWidth;
    try { GetDictTuple(hv_DictHandle, "DescWidth", &hv_DescWidth); }
    catch (...) { hv_DescWidth = 32; }
    int descWidth = (int)hv_DescWidth.L();

    HTuple hv_RatioThresh;
    try { GetDictTuple(hv_DictHandle, "RatioThresh", &hv_RatioThresh); }
    catch (...) { hv_RatioThresh = 0.75; }
    double ratioThresh = hv_RatioThresh.D();

    /* ---- BF 匹配全开放参数（从 dict 键读取，带默认值） ---- */
    HTuple t;
    int normType  = 4;   // 4=NORM_HAMMING；1=L1,2=L2,6=NORM_HAMMING2
    int crossCheck = 0;  // 0=否，1=是（此时忽略 ratio 过滤，返回最佳匹配）
    int knnK      = 2;   // knnMatch 的 k（ratio 过滤需要 >= 2）
    try { GetDictTuple(hv_DictHandle, "NormType", &t);   normType   = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "CrossCheck", &t); crossCheck = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "K", &t);          knnK       = (int)t.L(); } catch (...) {}

    int nRef = (int)hRef.L();
    int nTarget = (int)hTarget.L();

    cv::Mat matRef(nRef, descWidth, CV_8UC1, (uchar *)ptrRef.L());
    cv::Mat matTarget(nTarget, descWidth, CV_8UC1, (uchar *)ptrTarget.L());

    cv::BFMatcher bf(normType, crossCheck != 0);
    std::vector<std::vector<cv::DMatch>> knnMatches;

    if (nRef < 2 || nTarget < 2)
    {
        SetDictTuple(hv_DictHandle, "NumGoodMatches", (Hlong)0);
        return H_MSG_TRUE;
    }

    try { bf.knnMatch(matRef, matTarget, knnMatches, knnK); }
    catch (const cv::Exception &) {
        SetDictTuple(hv_DictHandle, "NumGoodMatches", (Hlong)0);
        return H_MSG_TRUE;
    }

    std::vector<int> goodIdxRef, goodIdxTarget;
    if (crossCheck)
    {
        /* crossCheck 模式：必须用 match()（crossCheck 仅在 match/k=1 语义下生效），
           BFMatcher(crossCheck=true) 的 match 只返回互为最佳的匹配 */
        std::vector<cv::DMatch> ccMatches;
        try { bf.match(matRef, matTarget, ccMatches); }
        catch (const cv::Exception &) {
            SetDictTuple(hv_DictHandle, "NumGoodMatches", (Hlong)0);
            return H_MSG_TRUE;
        }
        for (size_t i = 0; i < ccMatches.size(); i++)
        {
            goodIdxRef.push_back(ccMatches[i].queryIdx);
            goodIdxTarget.push_back(ccMatches[i].trainIdx);
        }
    }
    else
    {
        /* 常规模式：Lowe's Ratio Test（需 knnK >= 2） */
        for (size_t i = 0; i < knnMatches.size(); i++)
        {
            if (knnMatches[i].size() == 2)
            {
                const cv::DMatch &m = knnMatches[i][0];
                const cv::DMatch &n = knnMatches[i][1];
                if (m.distance < ratioThresh * n.distance)
                {
                    goodIdxRef.push_back(m.queryIdx);
                    goodIdxTarget.push_back(m.trainIdx);
                }
            }
        }
    }

    int nGood = (int)goodIdxRef.size();

    HTuple hv_IdxRef, hv_IdxTarget;
    for (int i = 0; i < nGood; i++)
    {
        hv_IdxRef[i] = (Hlong)goodIdxRef[i];
        hv_IdxTarget[i] = (Hlong)goodIdxTarget[i];
    }
    SetDictTuple(hv_DictHandle, "MatchIdxRef", hv_IdxRef);
    SetDictTuple(hv_DictHandle, "MatchIdxTarget", hv_IdxTarget);
    SetDictTuple(hv_DictHandle, "NumGoodMatches", (Hlong)nGood);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_estimate_affine_partial2d — RANSAC 部分仿射估计
 *===========================================================================*/
Herror HCcv_estimate_affine_partial2d(Hproc_handle proc_handle)
{
    const Hcpar *dict;
    INT4_8 num;
    HAllocStringMem(proc_handle, 1024);
    HGetPPar(proc_handle, 1, &dict, &num);
    HTuple hv_DictHandle(const_cast<Hcpar*>(dict), 1);

    HTuple hv_SrcRow, hv_SrcCol, hv_DstRow, hv_DstCol;
    GetDictTuple(hv_DictHandle, "SrcRow", &hv_SrcRow);
    GetDictTuple(hv_DictHandle, "SrcCol", &hv_SrcCol);
    GetDictTuple(hv_DictHandle, "DstRow", &hv_DstRow);
    GetDictTuple(hv_DictHandle, "DstCol", &hv_DstCol);

    int nPts = (int)hv_SrcRow.Length();
    if (nPts < 4)
    {
        SetDictTuple(hv_DictHandle, "Success", (Hlong)0);
        SetDictTuple(hv_DictHandle, "InlierCount", (Hlong)0);
        return H_MSG_TRUE;
    }

    HTuple hv_RansacThresh;
    try { GetDictTuple(hv_DictHandle, "RansacThreshold", &hv_RansacThresh); }
    catch (...) { hv_RansacThresh = 3.0; }
    double ransacThresh = hv_RansacThresh.D();

    /* ---- RANSAC 全开放参数（从 dict 键读取，带默认值） ---- */
    HTuple t;
    int    method     = 8;      // 8=RANSAC, 4=LMEDS
    int    maxIters   = 2000;
    double confidence = 0.99;
    int    refineIters = 10;
    try { GetDictTuple(hv_DictHandle, "Method", &t);      method      = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "MaxIters", &t);    maxIters    = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "Confidence", &t);  confidence  = t.D(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "RefineIters", &t); refineIters = (int)t.L(); } catch (...) {}

    std::vector<cv::Point2f> srcPts(nPts), dstPts(nPts);
    for (int i = 0; i < nPts; i++)
    {
        srcPts[i] = cv::Point2f((float)hv_SrcCol[i].D(), (float)hv_SrcRow[i].D());
        dstPts[i] = cv::Point2f((float)hv_DstCol[i].D(), (float)hv_DstRow[i].D());
    }

    cv::Mat inlierMask;
    cv::Mat M = cv::estimateAffinePartial2D(
        srcPts, dstPts, inlierMask, method, ransacThresh,
        (size_t)maxIters, confidence, (size_t)refineIters
    );

    if (M.empty())
    {
        SetDictTuple(hv_DictHandle, "Success", (Hlong)0);
        SetDictTuple(hv_DictHandle, "InlierCount", (Hlong)0);
        return H_MSG_TRUE;
    }

    int inlierCount = 0;
    if (!inlierMask.empty())
    {
        for (int i = 0; i < inlierMask.rows; i++)
            if (inlierMask.at<uchar>(i, 0) != 0) inlierCount++;
    }

    double a00 = M.at<double>(0, 0);
    double a01 = M.at<double>(0, 1);
    double a02 = M.at<double>(0, 2);
    double a10 = M.at<double>(1, 0);
    double a11 = M.at<double>(1, 1);
    double a12 = M.at<double>(1, 2);

    double angle = std::atan2(a10, a00);
    double scale = std::sqrt(a00 * a00 + a10 * a10);

    HTuple hv_HomMat2D;
    hv_HomMat2D[0] = a11;
    hv_HomMat2D[1] = a10;
    hv_HomMat2D[2] = a12;
    hv_HomMat2D[3] = a01;
    hv_HomMat2D[4] = a00;
    hv_HomMat2D[5] = a02;

    SetDictTuple(hv_DictHandle, "HomMat2D", hv_HomMat2D);
    SetDictTuple(hv_DictHandle, "Success", (Hlong)1);
    SetDictTuple(hv_DictHandle, "InlierCount", (Hlong)inlierCount);
    SetDictTuple(hv_DictHandle, "TranslateRow", a12);
    SetDictTuple(hv_DictHandle, "TranslateCol", a02);
    SetDictTuple(hv_DictHandle, "Angle", angle);
    SetDictTuple(hv_DictHandle, "Scale", scale);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_estimate_rigid_2d — RANSAC 刚体变换估计（平移 + 旋转，无缩放）
 *   与 cv_estimate_affine_partial2d 的区别：不含缩放自由度，更贴合
 *   SIFT/ORB 匹配后的图像配准（尺度一致的场景）。
 *===========================================================================*/
Herror HCcv_estimate_rigid_2d(Hproc_handle proc_handle)
{
    const Hcpar *dict;
    INT4_8 num;
    HAllocStringMem(proc_handle, 1024);
    HGetPPar(proc_handle, 1, &dict, &num);
    HTuple hv_DictHandle(const_cast<Hcpar*>(dict), 1);

    HTuple hv_SrcRow, hv_SrcCol, hv_DstRow, hv_DstCol;
    GetDictTuple(hv_DictHandle, "SrcRow", &hv_SrcRow);
    GetDictTuple(hv_DictHandle, "SrcCol", &hv_SrcCol);
    GetDictTuple(hv_DictHandle, "DstRow", &hv_DstRow);
    GetDictTuple(hv_DictHandle, "DstCol", &hv_DstCol);

    int nPts = (int)hv_SrcRow.Length();
    if (nPts < 2)
    {
        SetDictTuple(hv_DictHandle, "Success", (Hlong)0);
        SetDictTuple(hv_DictHandle, "InlierCount", (Hlong)0);
        return H_MSG_TRUE;
    }

    HTuple hv_RansacThresh;
    try { GetDictTuple(hv_DictHandle, "RansacThreshold", &hv_RansacThresh); }
    catch (...) { hv_RansacThresh = 3.0; }
    double ransacThresh = hv_RansacThresh.D();
    const double thresh2 = ransacThresh * ransacThresh;

    /* ---- RANSAC 全开放参数（从 dict 键读取，带默认值） ---- */
    HTuple t;
    int maxIter = 500;
    int seed    = 12345;
    try { GetDictTuple(hv_DictHandle, "MaxIter", &t); maxIter = (int)t.L(); } catch (...) {}
    try { GetDictTuple(hv_DictHandle, "Seed", &t);    seed    = (int)t.L(); } catch (...) {}

    std::vector<cv::Point2f> srcPts(nPts), dstPts(nPts);
    for (int i = 0; i < nPts; i++)
    {
        srcPts[i] = cv::Point2f((float)hv_SrcCol[i].D(), (float)hv_SrcRow[i].D());
        dstPts[i] = cv::Point2f((float)hv_DstCol[i].D(), (float)hv_DstRow[i].D());
    }

    // ---- RANSAC：每次随机取 2 点求刚体变换，统计内点 ----
    double bestCos = 1.0, bestSin = 0.0, bestTx = 0.0, bestTy = 0.0;
    int bestInliers = 0;

    cv::RNG rng((uint64)seed);
    for (int iter = 0; iter < maxIter; ++iter)
    {
        int i1 = rng.uniform(0, nPts);
        int i2 = rng.uniform(0, nPts);
        if (i1 == i2) continue;

        double sx = (double)srcPts[i2].x - srcPts[i1].x;
        double sy = (double)srcPts[i2].y - srcPts[i1].y;
        double dx = (double)dstPts[i2].x - dstPts[i1].x;
        double dy = (double)dstPts[i2].y - dstPts[i1].y;

        double lenSrc2 = sx * sx + sy * sy;
        double lenDst2 = dx * dx + dy * dy;
        if (lenSrc2 < 1e-12 || lenDst2 < 1e-12) continue;

        // 旋转角：cos = (s·d)/(|s||d|), sin = (s×d)/(|s||d|)
        double dot   = sx * dx + sy * dy;
        double cross = sx * dy - sy * dx;
        double inv   = 1.0 / std::sqrt(lenSrc2 * lenDst2);
        double cosT  = dot * inv;
        double sinT  = cross * inv;

        double tx = (double)dstPts[i1].x - (cosT * srcPts[i1].x - sinT * srcPts[i1].y);
        double ty = (double)dstPts[i1].y - (sinT * srcPts[i1].x + cosT * srcPts[i1].y);

        int inliers = 0;
        for (int i = 0; i < nPts; ++i)
        {
            double px = cosT * srcPts[i].x - sinT * srcPts[i].y + tx;
            double py = sinT * srcPts[i].x + cosT * srcPts[i].y + ty;
            double ex = px - dstPts[i].x;
            double ey = py - dstPts[i].y;
            if (ex * ex + ey * ey <= thresh2) ++inliers;
        }

        if (inliers > bestInliers)
        {
            bestInliers = inliers;
            bestCos = cosT; bestSin = sinT;
            bestTx = tx;    bestTy = ty;
        }
    }

    if (bestInliers < 2)
    {
        SetDictTuple(hv_DictHandle, "Success", (Hlong)0);
        SetDictTuple(hv_DictHandle, "InlierCount", (Hlong)0);
        return H_MSG_TRUE;
    }

    // ---- 用内点做最小二乘精化（刚体，scale=1）----
    std::vector<cv::Point2f> inSrc, inDst;
    inSrc.reserve((size_t)bestInliers);
    inDst.reserve((size_t)bestInliers);
    for (int i = 0; i < nPts; ++i)
    {
        double px = bestCos * srcPts[i].x - bestSin * srcPts[i].y + bestTx;
        double py = bestSin * srcPts[i].x + bestCos * srcPts[i].y + bestTy;
        double ex = px - dstPts[i].x;
        double ey = py - dstPts[i].y;
        if (ex * ex + ey * ey <= thresh2)
        {
            inSrc.push_back(srcPts[i]);
            inDst.push_back(dstPts[i]);
        }
    }

    int nIn = (int)inSrc.size();
    double cSx = 0.0, cSy = 0.0, cDx = 0.0, cDy = 0.0;
    for (int i = 0; i < nIn; ++i)
    {
        cSx += inSrc[i].x; cSy += inSrc[i].y;
        cDx += inDst[i].x; cDy += inDst[i].y;
    }
    cSx /= nIn; cSy /= nIn; cDx /= nIn; cDy /= nIn;

    // H = Σ dst_i' * src_i'^T，刚体旋转角闭式解
    double h00 = 0.0, h01 = 0.0, h10 = 0.0, h11 = 0.0;
    for (int i = 0; i < nIn; ++i)
    {
        double sx = inSrc[i].x - cSx;
        double sy = inSrc[i].y - cSy;
        double dx = inDst[i].x - cDx;
        double dy = inDst[i].y - cDy;
        h00 += dx * sx;
        h01 += dx * sy;
        h10 += dy * sx;
        h11 += dy * sy;
    }

    double angle = std::atan2(h10 - h01, h00 + h11);
    double cosT  = std::cos(angle);
    double sinT  = std::sin(angle);
    double tx = cDx - (cosT * cSx - sinT * cSy);
    double ty = cDy - (sinT * cSx + cosT * cSy);

    // HomMat2D 排列与 cv_estimate_affine_partial2d 保持一致
    //   a00=cos, a01=-sin, a02=tx, a10=sin, a11=cos, a12=ty
    HTuple hv_HomMat2D;
    hv_HomMat2D[0] = cosT;   // a11
    hv_HomMat2D[1] = sinT;   // a10
    hv_HomMat2D[2] = ty;     // a12
    hv_HomMat2D[3] = -sinT;  // a01
    hv_HomMat2D[4] = cosT;   // a00
    hv_HomMat2D[5] = tx;     // a02

    SetDictTuple(hv_DictHandle, "HomMat2D", hv_HomMat2D);
    SetDictTuple(hv_DictHandle, "Success", (Hlong)1);
    SetDictTuple(hv_DictHandle, "InlierCount", (Hlong)nIn);
    SetDictTuple(hv_DictHandle, "TranslateRow", ty);
    SetDictTuple(hv_DictHandle, "TranslateCol", tx);
    SetDictTuple(hv_DictHandle, "Angle", angle);
    SetDictTuple(hv_DictHandle, "Scale", 1.0);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_write_image — 将图像保存为 PNG 文件
 *   支持 8/16 位、单/三通道 图像
 *===========================================================================*/
Herror HCcv_write_image(Hproc_handle proc_handle)
{
    Hcpar filename, compression;
    HAllocStringMem(proc_handle, 1024);
    HGetSPar(proc_handle, 1, STRING_PAR, &filename, 1);
    HGetSPar(proc_handle, 2, LONG_PAR,  &compression, 1);

    Hkey  in_obj_key;
    HGetObj(proc_handle, 1, 1, &in_obj_key);

    // 检测通道数
    INT4_8 num_channels = 1;
    {
        Himage chk2, chk3;
        HGetDImage(proc_handle, in_obj_key, 2, &chk2);
        HGetDImage(proc_handle, in_obj_key, 3, &chk3);
        if (chk3.pixel.b != NULL)      num_channels = 3;
        else if (chk2.pixel.b != NULL) num_channels = 2;
    }

    cv::Mat cv_img;
    if (num_channels == 1)
    {
        Himage inimage;
        HGetDImage(proc_handle, in_obj_key, 1, &inimage);
        switch (inimage.kind)
        {
        case UINT2_IMAGE:
            cv_img = cv::Mat(inimage.height, inimage.width, CV_16UC1, inimage.pixel.u.p).clone();
            break;
        case BYTE_IMAGE:
            cv_img = cv::Mat(inimage.height, inimage.width, CV_8UC1,  inimage.pixel.b).clone();
            break;
        default: return H__LINE__ * 10000;
        }
    }
    else if (num_channels == 3)
    {
        Himage R, G, B;
        HGetDImage(proc_handle, in_obj_key, 1, &R);
        HGetDImage(proc_handle, in_obj_key, 2, &G);
        HGetDImage(proc_handle, in_obj_key, 3, &B);
        if (R.kind == UINT2_IMAGE)
        {
            cv::Mat chR(R.height, R.width, CV_16UC1, R.pixel.u.p);
            cv::Mat chG(G.height, G.width, CV_16UC1, G.pixel.u.p);
            cv::Mat chB(B.height, B.width, CV_16UC1, B.pixel.u.p);
            std::vector<cv::Mat> channels = {chR.clone(), chG.clone(), chB.clone()};
            cv::merge(channels, cv_img);
        }
        else
        {
            cv::Mat chR(R.height, R.width, CV_8UC1, R.pixel.b);
            cv::Mat chG(G.height, G.width, CV_8UC1, G.pixel.b);
            cv::Mat chB(B.height, B.width, CV_8UC1, B.pixel.b);
            std::vector<cv::Mat> channels = {chR.clone(), chG.clone(), chB.clone()};
            cv::merge(channels, cv_img);
        }
    }
    else { return H__LINE__ * 10000; }

    std::vector<int> params = { cv::IMWRITE_PNG_COMPRESSION, (int)compression.par.l };
    if (!cv::imwrite(filename.par.s, cv_img, params))
        return H__LINE__ * 10000;

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_mat_mul — 两个 16 位单通道图像做矩阵乘法，输出 16 位单通道图
 *   C = A * B   其中 A: m×k, B: k×n → C: m×n
 *   要求 A 的列数 == B 的行数
 *===========================================================================*/
Herror HCcv_mat_mul(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024);  // 枚举字符串读取统一在此分配一次（多次分配会泄漏 temp memory）
    Hkey  inA_obj_key, inB_obj_key, out_obj_key, out_image_key;
    Himage inA, inB, outimage;
    Hcpar  alpha, beta;

    HGetSPar(proc_handle, 1, DOUBLE_PAR, &alpha, 1);
    HGetSPar(proc_handle, 2, DOUBLE_PAR, &beta, 1);
    int fl = 0;
    if (!read_enum_param(proc_handle, 3, kGemmFlagsTable, kGemmFlagsN, &fl))
    {
        HSetErrText(const_cast<char*>(
            "cv_mat_mul: unknown Flags (\"none\"/\"transpose_a\"/\"transpose_b\"/\"transpose_c\" or 0/1/2/4)"));
        return 30003;
    }

    HGetObj(proc_handle, 1, 1, &inA_obj_key);
    HGetObj(proc_handle, 2, 1, &inB_obj_key);
    HGetDImage(proc_handle, inA_obj_key, 1, &inA);
    HGetDImage(proc_handle, inB_obj_key, 1, &inB);

    if (inA.kind != UINT2_IMAGE || inB.kind != UINT2_IMAGE)
        return 30001;

    cv::Mat A(inA.height, inA.width, CV_16UC1, inA.pixel.u.p);
    cv::Mat B(inB.height, inB.width, CV_16UC1, inB.pixel.u.p);

    if (fl < 0 || fl > 7)
        return 30003;

    cv::Mat Af, Bf, Cf;
    A.convertTo(Af, CV_64F);
    B.convertTo(Bf, CV_64F);
    try { cv::gemm(Af, Bf, alpha.par.d, cv::noArray(), beta.par.d, Cf, fl); }
    catch (const cv::Exception&) { return 30004; }

    cv::Mat C;
    Cf.convertTo(C, CV_16UC1);

    int outW = C.cols, outH = C.rows;

    HCkP(HNewImage(proc_handle, &outimage, UINT2_IMAGE, outW, outH));
    memcpy(outimage.pixel.u.p, C.data, (size_t)outW * outH * 2);

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_median_blur — 中值滤波（cv::medianBlur）
 *   支持 8 位 / 16 位 / 浮点(real) 单通道图像
 *   ksize: 滤波核尺寸，须为 ≥3 的奇数；16 位与浮点图仅支持 3 或 5
 *===========================================================================*/
Herror HCcv_median_blur(Hproc_handle proc_handle)
{
    Hkey   in_obj_key, out_obj_key, out_image_key;
    Himage inimage, outimage;
    Hcpar  ksize;

    HGetSPar(proc_handle, 1, LONG_PAR, &ksize, 1);
    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    int k = (int)ksize.par.l;
    if (k < 3 || (k % 2) == 0)
        return 30001;   // ksize 必须为 ≥3 的奇数

    int   cvType;
    void* inPixels;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:
        cvType   = CV_8UC1;
        inPixels = (void*)inimage.pixel.b;
        break;
    case UINT2_IMAGE:
        if (k > 5) return 30002;   // 16 位图仅支持 ksize=3/5
        cvType   = CV_16UC1;
        inPixels = (void*)inimage.pixel.u.p;
        break;
    case FLOAT_IMAGE:
        if (k > 5) return 30003;   // 浮点图仅支持 ksize=3/5
        cvType   = CV_32FC1;
        inPixels = (void*)inimage.pixel.f;
        break;
    default:
        return 30004;   // 仅支持 byte / uint2 / real 图像
    }

    cv::Mat imageIn(inimage.height, inimage.width, cvType, inPixels);

    HCkP(HNewImage(proc_handle, &outimage, inimage.kind, inimage.width, inimage.height));
    void* outPixels;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:  outPixels = (void*)outimage.pixel.b;   break;
    case UINT2_IMAGE: outPixels = (void*)outimage.pixel.u.p; break;
    default:          outPixels = (void*)outimage.pixel.f;   break;
    }
    cv::Mat imageOut(outimage.height, outimage.width, cvType, outPixels);

    try
    {
        cv::medianBlur(imageIn, imageOut, k);
    }
    catch (const cv::Exception&)
    {
        return 30005;
    }

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_reshape — 矩阵重塑（cv::reshape，单通道）
 *   支持 8 位 / 16 位 / 浮点(real) 单通道图像
 *   rows: 输出图像的新高度；cols = 总像素数 / rows（须能整除）
 *   rows <= 0 时保持原高度不变
 *===========================================================================*/
Herror HCcv_reshape(Hproc_handle proc_handle)
{
    Hkey   in_obj_key, out_obj_key, out_image_key;
    Himage inimage, outimage;
    Hcpar  rows;

    HGetSPar(proc_handle, 1, LONG_PAR, &rows, 1);
    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    int  cvType;
    void* inPixels;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:
        cvType   = CV_8UC1;
        inPixels = (void*)inimage.pixel.b;
        break;
    case UINT2_IMAGE:
        cvType   = CV_16UC1;
        inPixels = (void*)inimage.pixel.u.p;
        break;
    case FLOAT_IMAGE:
        cvType   = CV_32FC1;
        inPixels = (void*)inimage.pixel.f;
        break;
    default:
        return 30001;   // 仅支持 byte / uint2 / real 单通道图像
    }

    int newRows = (int)rows.par.l;
    if (newRows < 0) return 30002;   // 行数不能为负
    if (newRows == 0) newRows = (int)inimage.height;

    int total = (int)inimage.width * (int)inimage.height;
    if (total % newRows != 0) return 30003;   // 总像素数须能被 rows 整除
    int newCols = total / newRows;

    cv::Mat imageIn(inimage.height, inimage.width, cvType, inPixels);
    cv::Mat reshaped = imageIn.reshape(1, newRows);   // 仅换头，不复制数据

    HCkP(HNewImage(proc_handle, &outimage, inimage.kind, newCols, newRows));
    void* outPixels;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:  outPixels = (void*)outimage.pixel.b;   break;
    case UINT2_IMAGE: outPixels = (void*)outimage.pixel.u.p; break;
    default:          outPixels = (void*)outimage.pixel.f;   break;
    }
    cv::Mat imageOut(newRows, newCols, cvType, outPixels);
    reshaped.copyTo(imageOut);

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_filter2d — 图像卷积（cv::filter2D）
 *   支持 8 位 / 16 位 / 浮点(real) 单通道图像
 *   kernel: 单通道 real 图像，作为卷积核
 *===========================================================================*/
Herror HCcv_filter2d(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024);  // 枚举字符串读取统一在此分配一次（多次分配会泄漏 temp memory）
    Hkey   in_obj_key, k_obj_key, out_obj_key, out_image_key;
    Himage inimage, kimage, outimage;

    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);
    HGetObj(proc_handle, 2, 1, &k_obj_key);
    HGetDImage(proc_handle, k_obj_key, 1, &kimage);

    if (kimage.kind != FLOAT_IMAGE)
        return 30001;   // kernel 必须为 real 单通道图像

    int  cvType;
    void* inPixels;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:
        cvType   = CV_8UC1;
        inPixels = (void*)inimage.pixel.b;
        break;
    case UINT2_IMAGE:
        cvType   = CV_16UC1;
        inPixels = (void*)inimage.pixel.u.p;
        break;
    case FLOAT_IMAGE:
        cvType   = CV_32FC1;
        inPixels = (void*)inimage.pixel.f;
        break;
    default:
        return 30002;   // 仅支持 byte / uint2 / real 单通道图像
    }

    cv::Mat imageIn(inimage.height, inimage.width, cvType, inPixels);
    cv::Mat kernel(kimage.height, kimage.width, CV_32FC1, kimage.pixel.f);

/* 全开放参数：ddepth / anchor / delta / borderType（ddepth/borderType 支持字符串枚举或整数） */
Hcpar anchorX, anchorY, delta;
HGetSPar(proc_handle, 2, LONG_PAR, &anchorX, 1);
HGetSPar(proc_handle, 3, LONG_PAR, &anchorY, 1);
HGetSPar(proc_handle, 4, DOUBLE_PAR, &delta, 1);
int ddepth = -1;
if (!read_enum_param(proc_handle, 1, kDdepthTable, kDdepthN, &ddepth))
{
    HSetErrText(const_cast<char*>(
        "cv_filter2d: unknown Ddepth (e.g. \"same\",\"s16\",\"f32\" or integer)"));
    return 30004;
}
int borderType = cv::BORDER_DEFAULT;
if (!read_enum_param(proc_handle, 5, kBorderTypeTable, kBorderTypeN, &borderType))
{
    HSetErrText(const_cast<char*>(
        "cv_filter2d: unknown BorderType (string or integer expected)"));
    return 30003;
}

    HCkP(HNewImage(proc_handle, &outimage, inimage.kind, inimage.width, inimage.height));
    void* outPixels;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:  outPixels = (void*)outimage.pixel.b;   break;
    case UINT2_IMAGE: outPixels = (void*)outimage.pixel.u.p; break;
    default:          outPixels = (void*)outimage.pixel.f;   break;
    }
    cv::Mat imageOut(outimage.height, outimage.width, cvType, outPixels);

    try
    {
cv::filter2D(imageIn, imageOut, ddepth, kernel,
cv::Point((int)anchorX.par.l, (int)anchorY.par.l),
delta.par.d, borderType);
    }
    catch (const cv::Exception&)
    {
        return 30003;
    }

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_measure_pos — 复刻 HALCON measure_pos（OpenCV 实现）
 *
 *   在矩形测量区域内沿主轴抽取一维剖面，用高斯一阶导数卷积定位边缘，
 *   支持亚像素插值、振幅阈值与过渡方向过滤。
 *
 *   输入 : image       输入图像（byte / uint2 / real 单通道）
 *          Column      测量矩形中心列坐标 (x)
 *          Row         测量矩形中心行坐标 (y)
 *          Phi         主轴角度（弧度，相对水平方向，逆时针为正）
 *          Length1     沿主轴半长（扫描方向，单位像素）
 *          Length2     垂直主轴半宽（投影平均方向，单位像素）
 *          Sigma       高斯平滑标准差
 *          Threshold   振幅阈值（边缘两侧灰度差的下限）
 *          Transition  0=全部 1=暗→亮(正) -1=亮→暗(负)
 *   输出 : RowEdge, ColumnEdge, Amplitude
 *===========================================================================*/
Herror HCcv_measure_pos(Hproc_handle proc_handle)
{
    Hkey   in_obj_key;
    Himage inimage;
    Hcpar  column, row, phi, length1, length2, sigma, threshold, transition;

    HGetSPar(proc_handle, 1, DOUBLE_PAR, &column, 1);
    HGetSPar(proc_handle, 2, DOUBLE_PAR, &row, 1);
    HGetSPar(proc_handle, 3, DOUBLE_PAR, &phi, 1);
    HGetSPar(proc_handle, 4, DOUBLE_PAR, &length1, 1);
    HGetSPar(proc_handle, 5, DOUBLE_PAR, &length2, 1);
    HGetSPar(proc_handle, 6, DOUBLE_PAR, &sigma, 1);
    HGetSPar(proc_handle, 7, DOUBLE_PAR, &threshold, 1);
    HGetSPar(proc_handle, 8, LONG_PAR, &transition, 1);

    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    cv::Mat gray;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:
        gray = cv::Mat(inimage.height, inimage.width, CV_8UC1, inimage.pixel.b);
        break;
    case UINT2_IMAGE:
    {
        cv::Mat tmp(inimage.height, inimage.width, CV_16UC1, inimage.pixel.u.p);
        tmp.convertTo(gray, CV_8UC1, 1.0 / 257.0);
        break;
    }
    case FLOAT_IMAGE:
    {
        cv::Mat tmp(inimage.height, inimage.width, CV_32FC1, inimage.pixel.f);
        tmp.convertTo(gray, CV_8UC1);
        break;
    }
    default:
        return 30001;   // 仅支持 byte / uint2 / real 单通道图像
    }

    cvr::CvrMeasureResult mres;
    cvr::cvr_measure_pos(gray.data, gray.cols, gray.rows,
                         column.par.d, row.par.d, phi.par.d,
                         length1.par.d, length2.par.d, sigma.par.d,
                         threshold.par.d, static_cast<int>(transition.par.l),
                         mres);

    const INT4_8 n = (INT4_8)mres.row.size();

    HPutElem(proc_handle, 1, mres.row.data(), n, DOUBLE_PAR);
    HPutElem(proc_handle, 2, mres.col.data(), n, DOUBLE_PAR);
    HPutElem(proc_handle, 3, mres.amplitude.data(), n, DOUBLE_PAR);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_blur — 均值滤波（cv::blur）
 *   支持 8 位 / 16 位 / 浮点(real) 单通道图像
 *   kwidth/kheight: 滤波核宽/高，须为 ≥1 的奇数
 *===========================================================================*/
Herror HCcv_blur(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024);  // 枚举字符串读取统一在此分配一次（多次分配会泄漏 temp memory）
    Hkey   in_obj_key, out_obj_key, out_image_key;
    Himage inimage, outimage;
    Hcpar  k_width, k_height;

    HGetSPar(proc_handle, 1, LONG_PAR, &k_width, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &k_height, 1);
    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    int kw = (int)k_width.par.l;
    int kh = (int)k_height.par.l;
    if (kw < 1 || (kw % 2) == 0 || kh < 1 || (kh % 2) == 0)
        return 30001;   // 滤波核宽/高必须为 ≥1 的奇数

    /* 全开放参数：anchor / borderType（borderType 支持字符串枚举或整数） */
    Hcpar anchorX, anchorY;
    HGetSPar(proc_handle, 3, LONG_PAR, &anchorX, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &anchorY, 1);
    int borderType = cv::BORDER_DEFAULT;
    if (!read_enum_param(proc_handle, 5, kBorderTypeTable, kBorderTypeN, &borderType))
    {
        HSetErrText(const_cast<char*>(
            "cv_blur: unknown BorderType (string or integer expected)"));
        return 30003;
    }

    int  cvType;
    void* inPixels;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:
        cvType   = CV_8UC1;
        inPixels = (void*)inimage.pixel.b;
        break;
    case UINT2_IMAGE:
        cvType   = CV_16UC1;
        inPixels = (void*)inimage.pixel.u.p;
        break;
    case FLOAT_IMAGE:
        cvType   = CV_32FC1;
        inPixels = (void*)inimage.pixel.f;
        break;
    default:
        return 30002;   // 仅支持 byte / uint2 / real 单通道图像
    }

    cv::Mat imageIn(inimage.height, inimage.width, cvType, inPixels);

    HCkP(HNewImage(proc_handle, &outimage, inimage.kind, inimage.width, inimage.height));
    void* outPixels;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:  outPixels = (void*)outimage.pixel.b;   break;
    case UINT2_IMAGE: outPixels = (void*)outimage.pixel.u.p; break;
    default:          outPixels = (void*)outimage.pixel.f;   break;
    }
    cv::Mat imageOut(outimage.height, outimage.width, cvType, outPixels);

    try
    {
        cv::blur(imageIn, imageOut, cv::Size(kw, kh),
                 cv::Point((int)anchorX.par.l, (int)anchorY.par.l),
                 borderType);
    }
    catch (const cv::Exception&)
    {
        return 30003;
    }

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_subtract — 图像相减（cv::subtract，饱和截断）
 *   支持 8 位 / 16 位 / 浮点(real) 单通道图像，两图类型与尺寸须一致
 *   imageOut = saturate(imageA - imageB)
 *===========================================================================*/
Herror HCcv_subtract(Hproc_handle proc_handle)
{
    Hkey   inA_obj_key, inB_obj_key, out_obj_key, out_image_key;
    Himage inA, inB, outimage;

    HGetObj(proc_handle, 1, 1, &inA_obj_key);
    HGetDImage(proc_handle, inA_obj_key, 1, &inA);
    HGetObj(proc_handle, 2, 1, &inB_obj_key);
    HGetDImage(proc_handle, inB_obj_key, 1, &inB);

    if (inA.kind != BYTE_IMAGE && inA.kind != UINT2_IMAGE && inA.kind != FLOAT_IMAGE)
        return 30001;   // 仅支持 byte / uint2 / real 单通道图像
    if (inB.kind != inA.kind)
        return 30002;   // 两图类型不一致
    if (inA.width != inB.width || inA.height != inB.height)
        return 30003;   // 两图尺寸不一致

    int  cvType;
    void* pixelsA;
    void* pixelsB;
    switch (inA.kind)
    {
    case BYTE_IMAGE:
        cvType  = CV_8UC1;
        pixelsA = (void*)inA.pixel.b;
        pixelsB = (void*)inB.pixel.b;
        break;
    case UINT2_IMAGE:
        cvType  = CV_16UC1;
        pixelsA = (void*)inA.pixel.u.p;
        pixelsB = (void*)inB.pixel.u.p;
        break;
    default:   // FLOAT_IMAGE
        cvType  = CV_32FC1;
        pixelsA = (void*)inA.pixel.f;
        pixelsB = (void*)inB.pixel.f;
        break;
    }

    cv::Mat imageA(inA.height, inA.width, cvType, pixelsA);
    cv::Mat imageB(inB.height, inB.width, cvType, pixelsB);

    HCkP(HNewImage(proc_handle, &outimage, inA.kind, inA.width, inA.height));
    void* outPixels;
    switch (inA.kind)
    {
    case BYTE_IMAGE:  outPixels = (void*)outimage.pixel.b;   break;
    case UINT2_IMAGE: outPixels = (void*)outimage.pixel.u.p; break;
    default:          outPixels = (void*)outimage.pixel.f;   break;
    }
    cv::Mat imageOut(outimage.height, outimage.width, cvType, outPixels);

    try
    {
        cv::subtract(imageA, imageB, imageOut);
    }
    catch (const cv::Exception&)
    {
        return 30004;
    }

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_add_weighted — 加权求和（cv::addWeighted）
 *   支持 8 位 / 16 位 / 浮点(real) 单通道图像，两图类型与尺寸须一致
 *   imageOut = saturate(alpha*imageA + beta*imageB + gamma)
 *===========================================================================*/
Herror HCcv_add_weighted(Hproc_handle proc_handle)
{
    Hkey   inA_obj_key, inB_obj_key, out_obj_key, out_image_key;
    Himage inA, inB, outimage;
    Hcpar  alpha, beta, gamma;

    HGetSPar(proc_handle, 1, DOUBLE_PAR, &alpha, 1);
    HGetSPar(proc_handle, 2, DOUBLE_PAR, &beta, 1);
    HGetSPar(proc_handle, 3, DOUBLE_PAR, &gamma, 1);

    HGetObj(proc_handle, 1, 1, &inA_obj_key);
    HGetDImage(proc_handle, inA_obj_key, 1, &inA);
    HGetObj(proc_handle, 2, 1, &inB_obj_key);
    HGetDImage(proc_handle, inB_obj_key, 1, &inB);

    if (inA.kind != BYTE_IMAGE && inA.kind != UINT2_IMAGE && inA.kind != FLOAT_IMAGE)
        return 30001;   // 仅支持 byte / uint2 / real 单通道图像
    if (inB.kind != inA.kind)
        return 30002;   // 两图类型不一致
    if (inA.width != inB.width || inA.height != inB.height)
        return 30003;   // 两图尺寸不一致

    int  cvType;
    void* pixelsA;
    void* pixelsB;
    switch (inA.kind)
    {
    case BYTE_IMAGE:
        cvType  = CV_8UC1;
        pixelsA = (void*)inA.pixel.b;
        pixelsB = (void*)inB.pixel.b;
        break;
    case UINT2_IMAGE:
        cvType  = CV_16UC1;
        pixelsA = (void*)inA.pixel.u.p;
        pixelsB = (void*)inB.pixel.u.p;
        break;
    default:   // FLOAT_IMAGE
        cvType  = CV_32FC1;
        pixelsA = (void*)inA.pixel.f;
        pixelsB = (void*)inB.pixel.f;
        break;
    }

    cv::Mat imageA(inA.height, inA.width, cvType, pixelsA);
    cv::Mat imageB(inB.height, inB.width, cvType, pixelsB);

    HCkP(HNewImage(proc_handle, &outimage, inA.kind, inA.width, inA.height));
    void* outPixels;
    switch (inA.kind)
    {
    case BYTE_IMAGE:  outPixels = (void*)outimage.pixel.b;   break;
    case UINT2_IMAGE: outPixels = (void*)outimage.pixel.u.p; break;
    default:          outPixels = (void*)outimage.pixel.f;   break;
    }
    cv::Mat imageOut(outimage.height, outimage.width, cvType, outPixels);

    try
    {
        cv::addWeighted(imageA, alpha.par.d, imageB, beta.par.d, gamma.par.d, imageOut);
    }
    catch (const cv::Exception&)
    {
        return 30004;
    }

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_solve — 解线性方程组（cv::solve）
 *   矩阵用 real 单通道图像承载（height=行数, width=列数）
 *   A: m×n，b: m×1 或 m×k → x: n×1 或 n×k
 *   method: DECOMP_LU=0 / DECOMP_SVD=1 / DECOMP_EIG=2 / DECOMP_CHOLESKY=3 / DECOMP_QR=4 / DECOMP_NORMAL=16
 *===========================================================================*/
Herror HCcv_solve(Hproc_handle proc_handle)
{
    Hkey   inA_obj_key, inB_obj_key, out_obj_key, out_image_key;
    Himage inA, inB, outimage;
    Hcpar  method;

    HGetSPar(proc_handle, 1, LONG_PAR, &method, 1);
    HGetObj(proc_handle, 1, 1, &inA_obj_key);
    HGetDImage(proc_handle, inA_obj_key, 1, &inA);
    HGetObj(proc_handle, 2, 1, &inB_obj_key);
    HGetDImage(proc_handle, inB_obj_key, 1, &inB);

    if (inA.kind != FLOAT_IMAGE || inB.kind != FLOAT_IMAGE)
        return 30001;   // 仅支持 real 单通道矩阵

    int m = (int)inA.height, n = (int)inA.width;
    int bcols = (int)inB.width;
    if ((int)inB.height != m)
        return 30002;   // b 的行数须等于 A 的行数

    cv::Mat Af(m, n, CV_32FC1, inA.pixel.f);
    cv::Mat Bf(m, bcols, CV_32FC1, inB.pixel.f);
    cv::Mat A, b, x;
    Af.convertTo(A, CV_64F);
    Bf.convertTo(b, CV_64F);

    try
    {
        if (!cv::solve(A, b, x, (int)method.par.l))
            return 30003;   // 求解失败（奇异/维度不匹配）
    }
    catch (const cv::Exception&)
    {
        return 30004;
    }

    int xrows = x.rows, xcols = x.cols;
    HCkP(HNewImage(proc_handle, &outimage, FLOAT_IMAGE, xcols, xrows));
    cv::Mat xf;
    x.convertTo(xf, CV_32FC1);
    memcpy(outimage.pixel.f, xf.data, (size_t)xrows * xcols * sizeof(float));

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_estimate_affine_2d — 完整仿射变换估计（cv::estimateAffine2D，6 参数）
 *   点坐标用 tuple 输入；输出 HomMat2D(6元素)、Success、InlierCount
 *   method: RANSAC=8 / LMEDS=4
 *===========================================================================*/
Herror HCcv_estimate_affine_2d(Hproc_handle proc_handle)
{
    double const* srcRow;
    double const* srcCol;
    double const* dstRow;
    double const* dstCol;
    INT4_8 nSrc, nDst;
    Hcpar  method, ransacThresh, maxIters, confidence, refineIters;

    HGetSPar(proc_handle, 1, LONG_PAR, &method, 1);
    HGetSPar(proc_handle, 2, DOUBLE_PAR, &ransacThresh, 1);
    HGetPElemD(proc_handle, 3, CONV_NONE, &srcRow, &nSrc);
    HGetPElemD(proc_handle, 4, CONV_NONE, &srcCol, &nSrc);
    HGetPElemD(proc_handle, 5, CONV_NONE, &dstRow, &nDst);
    HGetPElemD(proc_handle, 6, CONV_NONE, &dstCol, &nDst);
    HGetSPar(proc_handle, 7, LONG_PAR, &maxIters, 1);
    HGetSPar(proc_handle, 8, DOUBLE_PAR, &confidence, 1);
    HGetSPar(proc_handle, 9, LONG_PAR, &refineIters, 1);

    std::vector<double> hom(6, 0.0);
    INT4_8 success = 0, inliers = 0;

    if (nSrc == nDst && nSrc >= 3)
    {
        std::vector<cv::Point2f> from((size_t)nSrc), to((size_t)nSrc);
        for (INT4_8 i = 0; i < nSrc; ++i)
        {
            from[(size_t)i] = cv::Point2f((float)srcCol[i], (float)srcRow[i]);
            to[(size_t)i]   = cv::Point2f((float)dstCol[i], (float)dstRow[i]);
        }

        cv::Mat inlierMask, M;
        try
        {
            M = cv::estimateAffine2D(from, to, inlierMask,
                                     (int)method.par.l, ransacThresh.par.d,
                                     (size_t)maxIters.par.l, confidence.par.d,
                                     (size_t)refineIters.par.l);
        }
        catch (const cv::Exception&)
        {
            M = cv::Mat();
        }

        if (!M.empty())
        {
            double a00 = M.at<double>(0, 0), a01 = M.at<double>(0, 1), a02 = M.at<double>(0, 2);
            double a10 = M.at<double>(1, 0), a11 = M.at<double>(1, 1), a12 = M.at<double>(1, 2);
            // HALCON hom_mat2d 顺序 [R00,R10,T0,R01,R11,T1] = [a00,a10,a02,a01,a11,a12]
            hom[0] = a00; hom[1] = a10; hom[2] = a02;
            hom[3] = a01; hom[4] = a11; hom[5] = a12;
            success = 1;
            if (!inlierMask.empty())
                for (int i = 0; i < inlierMask.rows; ++i)
                    if (inlierMask.at<uchar>(i, 0) != 0) ++inliers;
        }
    }

    HPutElem(proc_handle, 1, hom.data(), 6, DOUBLE_PAR);
    HPutElem(proc_handle, 2, &success, 1, LONG_PAR);
    HPutElem(proc_handle, 3, &inliers, 1, LONG_PAR);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_threshold_triangle — 三角法自动阈值（cv::threshold + THRESH_TRIANGLE）
 *   仅支持 8 位单通道输入（OpenCV 限制），输出二值图 + 自动阈值
 *===========================================================================*/
Herror HCcv_threshold_triangle(Hproc_handle proc_handle)
{
    Hkey   in_obj_key, out_obj_key, out_image_key;
    Himage inimage, outimage;

    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    if (inimage.kind != BYTE_IMAGE)
        return 30001;   // THRESH_TRIANGLE 仅支持 8 位单通道

    cv::Mat imageIn(inimage.height, inimage.width, CV_8UC1, inimage.pixel.b);
    HCkP(HNewImage(proc_handle, &outimage, BYTE_IMAGE, inimage.width, inimage.height));
    cv::Mat imageOut(outimage.height, outimage.width, CV_8UC1, outimage.pixel.b);

    double thresh = 0.0;
    try
    {
        thresh = cv::threshold(imageIn, imageOut, 0, 255,
                               cv::THRESH_BINARY | cv::THRESH_TRIANGLE);
    }
    catch (const cv::Exception&)
    {
        return 30002;
    }

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    HPutElem(proc_handle, 1, &thresh, 1, DOUBLE_PAR);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_threshold — 固定阈值分割（cv::threshold）
 *
 *   type 为 OpenCV 枚举整型，直接透传给 cv::threshold：
 *     基础类型（type & 7）：
 *       0 = THRESH_BINARY       （>thresh ? maxval : 0）
 *       1 = THRESH_BINARY_INV   （>thresh ? 0 : maxval）
 *       2 = THRESH_TRUNC        （>thresh ? thresh : 原值）
 *       3 = THRESH_TOZERO       （>thresh ? 原值 : 0）
 *       4 = THRESH_TOZERO_INV   （>thresh ? 0 : 原值）
 *     附加标志（type & ~7，可与基础类型按位或）：
 *       8  = THRESH_OTSU        （大津自动阈值，仅 8 位单通道，thresh 可省略）
 *       16 = THRESH_TRIANGLE    （三角法自动阈值，仅 8 位单通道）
 *     例：8 = 大津+BINARY；9 = 大津+BINARY_INV；17 = 三角法+BINARY_INV
 *
 *   threshUsed 输出实际使用的阈值（OTSU/TRIANGLE 时为自动计算值）。
 *   支持 byte / uint2 / real 单通道（OTSU/TRIANGLE 仅 byte）。
 *===========================================================================*/
Herror HCcv_threshold(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024);  // 枚举字符串读取统一在此分配一次（多次分配会泄漏 temp memory）
    Hkey   in_obj_key, out_obj_key, out_image_key;
    Himage inimage, outimage;
    Hcpar  thresh_par, maxval_par;

    HGetSPar(proc_handle, 1, DOUBLE_PAR, &thresh_par, 1);
    HGetSPar(proc_handle, 2, DOUBLE_PAR, &maxval_par, 1);

    /* Type：支持字符串枚举（可以 "|" 组合，如 "binary|otsu"）或整数（向后兼容） */
    static const CvEnumEntry kThreshTypeTable[] = {
        { "binary", 0 }, { "binary_inv", 1 }, { "trunc", 2 },
        { "tozero", 3 }, { "tozero_inv", 4 },
        { "otsu", 8 }, { "triangle", 16 },
        { "thresh_binary", 0 }, { "thresh_binary_inv", 1 }, { "thresh_trunc", 2 },
        { "thresh_tozero", 3 }, { "thresh_tozero_inv", 4 },
        { "thresh_otsu", 8 }, { "thresh_triangle", 16 },
    };
    const size_t kThreshTypeN = sizeof(kThreshTypeTable) / sizeof(kThreshTypeTable[0]);
    int type = 0;
    {
        std::string s;
        bool ok = false;
        if (fetch_str(proc_handle, 3, s) == H_MSG_OK && !s.empty()) {
            type = 0;
            size_t start = 0;
            for (;;) {
                const size_t sep = s.find('|', start);
                std::string tok = (sep == std::string::npos)
                                      ? s.substr(start)
                                      : s.substr(start, sep - start);
                const size_t b = tok.find_first_not_of(" \t");
                const size_t e = tok.find_last_not_of(" \t");
                tok = (b == std::string::npos) ? "" : tok.substr(b, e - b + 1);
                int v = 0;
                char* end = nullptr;
                const long lv = std::strtol(tok.c_str(), &end, 10);
                if (enum_from_string(kThreshTypeTable, kThreshTypeN, tok.c_str(), v))
                    type |= v;
                else if (end && *end == '\0' && !tok.empty())
                    type |= (int)lv;
                else {
                    HSetErrText(const_cast<char*>(
                        "cv_threshold: unknown Type token (e.g. \"binary\",\"otsu\",\"triangle\")"));
                    return 30003;
                }
                if (sep == std::string::npos) break;
                start = sep + 1;
            }
            ok = true;
        } else {
            long v = 0;
            if (fetch_long(proc_handle, 3, v) == H_MSG_OK) { type = (int)v; ok = true; }
        }
        if (!ok) {
            HSetErrText(const_cast<char*>("cv_threshold: invalid Type"));
            return 30003;
        }
    }

    const double thresh = thresh_par.par.d;
    const double maxval = maxval_par.par.d;

    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    /* type 拆分：基础类型 = type & 7，自动阈值标志 = type & ~7 */
    const int baseType = type & 7;
    const int flags    = type & ~7;
    if (baseType < 0 || baseType > 4) {
        HSetErrText(const_cast<char*>("cv_threshold: invalid base Type (need 0..4)"));
        return 30001;
    }
    if (flags != 0 && flags != 8 && flags != 16) {
        HSetErrText(const_cast<char*>("cv_threshold: unsupported Type flag (need 0/8/16)"));
        return 30002;
    }
    /* OTSU / TRIANGLE 仅支持 8 位单通道 */
    if ((flags & (8 | 16)) && inimage.kind != BYTE_IMAGE) {
        HSetErrText(const_cast<char*>("cv_threshold: OTSU/TRIANGLE require a byte image"));
        return 30003;
    }

    int cvType = -1;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:  cvType = CV_8UC1;  break;
    case UINT2_IMAGE: cvType = CV_16UC1; break;
    case FLOAT_IMAGE: cvType = CV_32FC1; break;
    default:
        HSetErrText(const_cast<char*>("cv_threshold: unsupported image type"));
        return 30004;
    }

    cv::Mat imageIn(inimage.height, inimage.width, cvType, inimage.pixel.b);
    HCkP(HNewImage(proc_handle, &outimage, inimage.kind, inimage.width, inimage.height));
    cv::Mat imageOut(outimage.height, outimage.width, cvType, outimage.pixel.b);

    double used = 0.0;
    try
    {
        used = cv::threshold(imageIn, imageOut, thresh, maxval, type);
    }
    catch (const cv::Exception&)
    {
        HSetErrText(const_cast<char*>("cv_threshold: cv::threshold failed"));
        return 30005;
    }

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    HPutElem(proc_handle, 1, &used, 1, DOUBLE_PAR);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_calc_hist — 计算灰度直方图（cv::calcHist）
 *   支持 8 位 / 16 位 / 浮点单通道；输出 1×HistSize 的 real 直方图
 *===========================================================================*/
Herror HCcv_calc_hist(Hproc_handle proc_handle)
{
    Hkey   in_obj_key, out_obj_key, out_image_key;
    Himage inimage, outimage;
    Hcpar  histSize, rangeMin, rangeMax;

    HGetSPar(proc_handle, 1, LONG_PAR, &histSize, 1);
    HGetSPar(proc_handle, 2, DOUBLE_PAR, &rangeMin, 1);
    HGetSPar(proc_handle, 3, DOUBLE_PAR, &rangeMax, 1);

    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    int bins = (int)histSize.par.l;
    if (bins <= 0) return 30001;

    cv::Mat srcF;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:
    {
        cv::Mat t(inimage.height, inimage.width, CV_8UC1, inimage.pixel.b);
        t.convertTo(srcF, CV_32FC1);
        break;
    }
    case UINT2_IMAGE:
    {
        cv::Mat t(inimage.height, inimage.width, CV_16UC1, inimage.pixel.u.p);
        t.convertTo(srcF, CV_32FC1);
        break;
    }
    case FLOAT_IMAGE:
        srcF = cv::Mat(inimage.height, inimage.width, CV_32FC1, inimage.pixel.f);
        break;
    default:
        return 30002;
    }

    float  rangeArr[2] = { (float)rangeMin.par.d, (float)rangeMax.par.d };
    const float* rangesArr[] = { rangeArr };
    int    channels[] = { 0 };
    int    histSz = bins;
    cv::Mat hist;
    try
    {
        cv::calcHist(&srcF, 1, channels, cv::noArray(), hist, 1, &histSz,
                     rangesArr, true, false);
    }
    catch (const cv::Exception&)
    {
        return 30003;
    }

    HCkP(HNewImage(proc_handle, &outimage, FLOAT_IMAGE, bins, 1));
    cv::Mat histC = hist.isContinuous() ? hist : hist.clone();
    memcpy(outimage.pixel.f, histC.data, (size_t)bins * sizeof(float));

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_match_template — 模板匹配（cv::matchTemplate, TM_CCOEFF_NORMED）
 *   仅支持 8 位 / 浮点单通道（OpenCV 限制）；输出 real 得分图
 *===========================================================================*/
Herror HCcv_match_template(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024);  // 枚举字符串读取统一在此分配一次（多次分配会泄漏 temp memory）
    Hkey   in_obj_key, tpl_obj_key, out_obj_key, out_image_key;
    Himage inimage, tplimage, outimage;

    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);
    HGetObj(proc_handle, 2, 1, &tpl_obj_key);
    HGetDImage(proc_handle, tpl_obj_key, 1, &tplimage);

    int  cvType;
    void *inPixels, *tplPixels;
    if (inimage.kind == BYTE_IMAGE)
    {
        cvType   = CV_8UC1;
        inPixels = (void*)inimage.pixel.b;
    }
    else if (inimage.kind == FLOAT_IMAGE)
    {
        cvType   = CV_32FC1;
        inPixels = (void*)inimage.pixel.f;
    }
    else
        return 30001;   // 仅支持 byte / real 单通道

    if (tplimage.kind != inimage.kind)
        return 30002;   // 模板类型须与图像一致
    if (tplimage.width > inimage.width || tplimage.height > inimage.height)
        return 30003;   // 模板尺寸不能大于图像

    tplPixels = (tplimage.kind == BYTE_IMAGE) ? (void*)tplimage.pixel.b
                                              : (void*)tplimage.pixel.f;

    int method = 5;
    if (!read_enum_param(proc_handle, 1, kMatchMethodTable, kMatchMethodN, &method))
    {
        HSetErrText(const_cast<char*>(
            "cv_match_template: unknown MethodId (e.g. \"sqdiff_normed\",\"ccoeff_normed\" or 0..5)"));
        return 30005;
    }
    if (method < 0 || method > 5)
    {
        HSetErrText(const_cast<char*>("cv_match_template: methodId must be 0..5 (TM_SQDIFF..TM_CCOEFF_NORMED)"));
        return 30005;
    }

    cv::Mat img(inimage.height, inimage.width, cvType, inPixels);
    cv::Mat tpl(tplimage.height, tplimage.width, cvType, tplPixels);
    cv::Mat result;
    try
    {
        cv::matchTemplate(img, tpl, result, method);
    }
    catch (const cv::Exception&)
    {
        return 30004;
    }

    int rw = result.cols, rh = result.rows;
    HCkP(HNewImage(proc_handle, &outimage, FLOAT_IMAGE, rw, rh));
    memcpy(outimage.pixel.f, result.data, (size_t)rw * rh * sizeof(float));

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_kmeans — K 均值聚类（cv::kmeans）
 *   样本矩阵 Samples: real N×D（N 个样本，每行 D 维）
 *   输出 Labels: real N×1（整数标签），Centers: real K×D
 *===========================================================================*/
Herror HCcv_kmeans(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024);  // 枚举字符串读取统一在此分配一次（多次分配会泄漏 temp memory）
    Hkey   in_obj_key, lbl_obj_key, cen_obj_key, out_image_key;
    Himage inimage, lblimage, cenimage;
    Hcpar  K, attempts, termEps, termMaxIter;

    HGetSPar(proc_handle, 1, LONG_PAR, &K, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &attempts, 1);
    HGetSPar(proc_handle, 3, DOUBLE_PAR, &termEps, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &termMaxIter, 1);

    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    if (inimage.kind != FLOAT_IMAGE)
        return 30001;   // 样本矩阵须为 real

    int k = (int)K.par.l;
    int N = (int)inimage.height, D = (int)inimage.width;
    if (k <= 0 || k > N) return 30002;

    /* 全开放参数：flags（支持字符串枚举或整数：0=RANDOM，1=PP，2=USE_INITIAL_LABEL） */
    int kflags = 1;
    if (!read_enum_param(proc_handle, 5, kKmeansFlagsTable, kKmeansFlagsN, &kflags))
    {
        HSetErrText(const_cast<char*>(
            "cv_kmeans: unknown Flags (\"random\"/\"pp\"/\"use_initial_labels\" or 0..2)"));
        return 30004;
    }
    if (kflags < 0 || kflags > 2)
        return 30004;

    cv::Mat data = cv::Mat(N, D, CV_32FC1, inimage.pixel.f).clone();
    cv::Mat labels, centers;
    try
    {
        cv::kmeans(data, k, labels,
                   cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT,
                                    (int)termMaxIter.par.l, termEps.par.d),
                   (int)attempts.par.l, kflags, centers);
    }
    catch (const cv::Exception& e)
    {
        HSetErrText(const_cast<char*>(e.what()));
        return 30003;
    }

    cv::Mat labelsF, centersC;
    labels.convertTo(labelsF, CV_32FC1);
    centersC = centers.isContinuous() ? centers : centers.clone();

    // 输出 Labels: N×1 real
    HCkP(HNewImage(proc_handle, &lblimage, FLOAT_IMAGE, 1, N));
    memcpy(lblimage.pixel.f, labelsF.data, (size_t)N * sizeof(float));
    HCrObj(proc_handle, 1, &lbl_obj_key);
    HPutDImage(proc_handle, lbl_obj_key, 1, &lblimage, FALSE, &out_image_key);
    HPutRect(proc_handle, lbl_obj_key, lblimage.width, lblimage.height);

    // 输出 Centers: k×D real
    HCkP(HNewImage(proc_handle, &cenimage, FLOAT_IMAGE, D, k));
    memcpy(cenimage.pixel.f, centersC.data, (size_t)k * D * sizeof(float));
    HCrObj(proc_handle, 2, &cen_obj_key);
    HPutDImage(proc_handle, cen_obj_key, 1, &cenimage, FALSE, &out_image_key);
    HPutRect(proc_handle, cen_obj_key, cenimage.width, cenimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_multi_frame_median — 多帧逐像素中值（OpenCV 实现）
 *
 *   对 N 张同尺寸 real 图像逐像素取中位数，输出一张 real 中值图。
 *   要求 N >= 2；N 为偶数时取上中位数（vals[N/2]）。
 *
 *   输入 : images      对象数组，N 张 real 单通道图像，尺寸一致
 *   输出 : imageMedian real 单通道中值图
 *===========================================================================*/
static cv::Mat cvMultiFrameMedian(const std::vector<cv::Mat>& images)
{
    const int N = (int)images.size();
    const int H = images[0].rows;
    const int W = images[0].cols;
    const int total = H * W;
    const int mid = N / 2;

    std::vector<cv::Mat> hold(N);
    std::vector<const float*> ptrs(N);
    for (int i = 0; i < N; ++i) {
        if (images[i].isContinuous()) {
            ptrs[i] = images[i].ptr<float>();
        } else {
            hold[i] = images[i].clone();
            ptrs[i] = hold[i].ptr<float>();
        }
    }

    cv::Mat ref(H, W, CV_32F);
    float* out = ref.ptr<float>();

    if (N == 3) {
        const float* p0 = ptrs[0];
        const float* p1 = ptrs[1];
        const float* p2 = ptrs[2];
        for (int i = 0; i < total; ++i) {
            const float a = p0[i];
            const float b = p1[i];
            const float c = p2[i];
            const float lo = a < b ? a : b;
            const float hi = a < b ? b : a;
            out[i] = c < lo ? lo : (c > hi ? hi : c);
        }
        return ref;
    }

    std::vector<float> vals(N);
    for (int i = 0; i < total; ++i) {
        for (int k = 0; k < N; ++k) vals[k] = ptrs[k][i];

        if (N <= 16) {
            for (int a = 1; a < N; ++a) {
                const float key = vals[a];
                int b = a - 1;
                while (b >= 0 && vals[b] > key) {
                    vals[b + 1] = vals[b];
                    --b;
                }
                vals[b + 1] = key;
            }
        } else {
            std::nth_element(vals.begin(), vals.begin() + mid, vals.end());
        }

        out[i] = vals[mid];
    }

    return ref;
}

Herror HCcv_multi_frame_median(Hproc_handle proc_handle)
{
    Hkey   in_obj_key, out_obj_key, out_image_key;
    Himage inimage, outimage;
    INT4_8 num_images = 0;

    HGetObjNum(proc_handle, 1, &num_images);
    if (num_images < 2)
        return 30001;   // 至少需要 2 张图像

    int W = 0, H = 0;
    std::vector<cv::Mat> images;
    images.reserve((size_t)num_images);

    for (INT4_8 k = 1; k <= num_images; ++k)
    {
        HGetObj(proc_handle, 1, k, &in_obj_key);
        HGetDImage(proc_handle, in_obj_key, 1, &inimage);

        if (inimage.kind != FLOAT_IMAGE)
            return 30002;   // 仅支持 real 单通道图像

        if (k == 1)
        {
            W = (int)inimage.width;
            H = (int)inimage.height;
        }
        else if ((int)inimage.width != W || (int)inimage.height != H)
        {
            return 30003;   // 所有图像尺寸必须一致
        }

        images.push_back(cv::Mat(H, W, CV_32FC1, inimage.pixel.f));
    }

    cv::Mat median = cvMultiFrameMedian(images);

    HCkP(HNewImage(proc_handle, &outimage, FLOAT_IMAGE, median.cols, median.rows));
    memcpy(outimage.pixel.f, median.data, (size_t)median.cols * median.rows * sizeof(float));

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_sobel — Sobel 边缘检测（cv::Sobel）
 *   支持 8 位 / 16 位 / 浮点(real) 单通道图像
 *   输出统一为 real（CV_32F），因为梯度结果含符号
 *   Dx/Dy: 导数阶数（0~2，且 Dx+Dy >= 1）
 *   Ksize: 核尺寸（1 / 3 / 5 / 7）
 *   Ddepth: 输出深度（-1 / 3=CV_16S / 5=CV_32F / 6=CV_64F）
 *   Scale / Delta: 缩放系数与偏置
 *   BorderType: OpenCV 边界模式（1=BORDER_REPLICATE 等）
 *===========================================================================*/
Herror HCcv_sobel(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024);  // 枚举字符串读取统一在此分配一次（多次分配会泄漏 temp memory）
    Hkey   in_obj_key, out_obj_key, out_image_key;
    Himage inimage, outimage;
Hcpar  dx, dy, ksize, scale, delta;
HGetSPar(proc_handle, 1, LONG_PAR, &dx, 1);
HGetSPar(proc_handle, 2, LONG_PAR, &dy, 1);
HGetSPar(proc_handle, 3, LONG_PAR, &ksize, 1);
HGetSPar(proc_handle, 5, DOUBLE_PAR, &scale, 1);
HGetSPar(proc_handle, 6, DOUBLE_PAR, &delta, 1);
int ddepth = -1;
if (!read_enum_param(proc_handle, 4, kDdepthTable, kDdepthN, &ddepth))
{
    HSetErrText(const_cast<char*>(
        "cv_sobel: unknown Ddepth (e.g. \"same\",\"s16\",\"f32\" or integer)"));
    return 30003;
}
int borderType = cv::BORDER_DEFAULT;
if (!read_enum_param(proc_handle, 7, kBorderTypeTable, kBorderTypeN, &borderType))
{
    HSetErrText(const_cast<char*>(
        "cv_sobel: unknown BorderType (string or integer expected)"));
    return 30003;
}
    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    int dxi = (int)dx.par.l;
    int dyi = (int)dy.par.l;
    if (dxi < 0 || dxi > 2 || dyi < 0 || dyi > 2 || (dxi + dyi) < 1)
        return 30001;   // Dx/Dy 必须为 0~2，且至少一个非 0

    int k = (int)ksize.par.l;
    if (k != 1 && k != 3 && k != 5 && k != 7)
        return 30002;   // Ksize 必须为 1 / 3 / 5 / 7

const int dd = ddepth;
if (dd != -1 && dd != CV_16S && dd != CV_32F && dd != CV_64F)
        return 30003;   // Ddepth 非法（支持 -1 / CV_16S / CV_32F / CV_64F）

    int  cvType;
    void* inPixels;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:
        cvType   = CV_8UC1;
        inPixels = (void*)inimage.pixel.b;
        break;
    case UINT2_IMAGE:
        cvType   = CV_16UC1;
        inPixels = (void*)inimage.pixel.u.p;
        break;
    case FLOAT_IMAGE:
        cvType   = CV_32FC1;
        inPixels = (void*)inimage.pixel.f;
        break;
    default:
        return 30004;   // 仅支持 byte / uint2 / real 单通道图像
    }

    cv::Mat imageIn(inimage.height, inimage.width, cvType, inPixels);

    cv::Mat grad;
    try
    {
        cv::Sobel(imageIn, grad, dd, dxi, dyi, k, scale.par.d, delta.par.d, borderType);
    }
    catch (const cv::Exception&)
    {
        return 30005;
    }

    cv::Mat out;
    if (grad.type() == CV_32FC1)
        out = grad;
    else
        grad.convertTo(out, CV_32FC1);

    HCkP(HNewImage(proc_handle, &outimage, FLOAT_IMAGE, out.cols, out.rows));
    memcpy(outimage.pixel.f, out.data, (size_t)out.cols * out.rows * sizeof(float));

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_magnitude — 梯度幅值（cv::magnitude）
 *   支持 8 位 / 16 位 / 浮点(real) 单通道图像，两图类型与尺寸须一致
 *   Magnitude = sqrt(X^2 + Y^2)，输出统一为 real
 *===========================================================================*/
Herror HCcv_magnitude(Hproc_handle proc_handle)
{
    Hkey   inX_obj_key, inY_obj_key, out_obj_key, out_image_key;
    Himage inX, inY, outimage;

    HGetObj(proc_handle, 1, 1, &inX_obj_key);
    HGetDImage(proc_handle, inX_obj_key, 1, &inX);
    HGetObj(proc_handle, 2, 1, &inY_obj_key);
    HGetDImage(proc_handle, inY_obj_key, 1, &inY);

    if (inX.kind != BYTE_IMAGE && inX.kind != UINT2_IMAGE && inX.kind != FLOAT_IMAGE)
        return 30001;   // 仅支持 byte / uint2 / real 单通道图像
    if (inY.kind != inX.kind)
        return 30002;   // 两图类型不一致
    if (inX.width != inY.width || inX.height != inY.height)
        return 30003;   // 两图尺寸不一致

    int  cvType;
    void* pixelsX;
    void* pixelsY;
    switch (inX.kind)
    {
    case BYTE_IMAGE:
        cvType  = CV_8UC1;
        pixelsX = (void*)inX.pixel.b;
        pixelsY = (void*)inY.pixel.b;
        break;
    case UINT2_IMAGE:
        cvType  = CV_16UC1;
        pixelsX = (void*)inX.pixel.u.p;
        pixelsY = (void*)inY.pixel.u.p;
        break;
    default:   // FLOAT_IMAGE
        cvType  = CV_32FC1;
        pixelsX = (void*)inX.pixel.f;
        pixelsY = (void*)inY.pixel.f;
        break;
    }

    cv::Mat X(inX.height, inX.width, cvType, pixelsX);
    cv::Mat Y(inY.height, inY.width, cvType, pixelsY);

    cv::Mat mag;
    try
    {
        /* cv::magnitude 仅支持 32F/64F：byte/uint2 先转 float */
        cv::Mat Xf, Yf;
        X.convertTo(Xf, CV_32F);
        Y.convertTo(Yf, CV_32F);
        cv::magnitude(Xf, Yf, mag);
    }
    catch (const cv::Exception&)
    {
        return 30004;
    }

    HCkP(HNewImage(proc_handle, &outimage, FLOAT_IMAGE, mag.cols, mag.rows));
    memcpy(outimage.pixel.f, mag.data, (size_t)mag.cols * mag.rows * sizeof(float));

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * 灰度形态学（cv::morphologyEx）：图像域 erode/dilate/open/close/gradient/
 *   tophat/blackhat。支持 byte / uint2 / real 单通道
 *   （OpenCV morphologyEx 支持 8U / 16U / 32F）。
 *===========================================================================*/
static Herror gray_morph_run(Hproc_handle proc_handle,
                             int op, int shape, int kw, int kh, int iterations)
{
    Hkey   in_obj_key, out_obj_key, out_image_key;
    Himage inimage, outimage;
    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    int  cvType;
    void *inPixels;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:  cvType = CV_8UC1;  inPixels = (void*)inimage.pixel.b;   break;
    case UINT2_IMAGE: cvType = CV_16UC1; inPixels = (void*)inimage.pixel.u.p; break;
    case FLOAT_IMAGE: cvType = CV_32FC1; inPixels = (void*)inimage.pixel.f;   break;
    default:
        return 30001;   // 仅支持 byte / uint2 / real 单通道
    }

    cv::Mat imageIn(inimage.height, inimage.width, cvType, inPixels);
    cv::Mat kernel = cv::getStructuringElement(shape, cv::Size(kw, kh));

    HCkP(HNewImage(proc_handle, &outimage, inimage.kind, inimage.width, inimage.height));
    void *outPixels;
    switch (inimage.kind)
    {
    case BYTE_IMAGE:  outPixels = (void*)outimage.pixel.b;   break;
    case UINT2_IMAGE: outPixels = (void*)outimage.pixel.u.p; break;
    default:          outPixels = (void*)outimage.pixel.f;   break;
    }
    cv::Mat imageOut(outimage.height, outimage.width, cvType, outPixels);

    try
    {
        cv::Mat a, b;
        cv::morphologyEx(imageIn, a, op, kernel);   // morphologyEx 无 iterations 槽，循环实现
        for (int it = 1; it < iterations; ++it)
        {
            cv::morphologyEx(a, b, op, kernel);
            cv::swap(a, b);
        }
        a.copyTo(imageOut);
    }
    catch (const cv::Exception&)
    {
        return 30002;
    }

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);
    return H_MSG_TRUE;
}

/* rect 预设：参数 1/2 = Width/Height */
static Herror gray_morph_rect(Hproc_handle proc_handle, int op)
{
    Hcpar w_par, h_par;
    HGetSPar(proc_handle, 1, LONG_PAR, &w_par, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &h_par, 1);
    const int kw = (int)w_par.par.l;
    const int kh = (int)h_par.par.l;
    if (kw < 1 || kh < 1)
    {
        HSetErrText(const_cast<char*>("gray morph: Width/Height must be >= 1"));
        return 30003;
    }
    return gray_morph_run(proc_handle, op, cv::MORPH_RECT, kw, kh, 1);
}

/* circle 预设：参数 1 = Radius，核尺寸 = 2R+1 椭圆核 */
static Herror gray_morph_circle(Hproc_handle proc_handle, int op)
{
    Hcpar r_par;
    HGetSPar(proc_handle, 1, DOUBLE_PAR, &r_par, 1);
    if (r_par.par.d < 0.0)
    {
        HSetErrText(const_cast<char*>("gray morph: Radius must be >= 0"));
        return 30003;
    }
    const int k = 2 * (int)std::lround(r_par.par.d) + 1;
    return gray_morph_run(proc_handle, op, cv::MORPH_ELLIPSE, k, k, 1);
}

Herror HCcv_gray_erosion_rect(Hproc_handle proc_handle)
{ return gray_morph_rect(proc_handle, cv::MORPH_ERODE); }
Herror HCcv_gray_dilation_rect(Hproc_handle proc_handle)
{ return gray_morph_rect(proc_handle, cv::MORPH_DILATE); }
Herror HCcv_gray_opening_rect(Hproc_handle proc_handle)
{ return gray_morph_rect(proc_handle, cv::MORPH_OPEN); }
Herror HCcv_gray_closing_rect(Hproc_handle proc_handle)
{ return gray_morph_rect(proc_handle, cv::MORPH_CLOSE); }

Herror HCcv_gray_erosion_circle(Hproc_handle proc_handle)
{ return gray_morph_circle(proc_handle, cv::MORPH_ERODE); }
Herror HCcv_gray_dilation_circle(Hproc_handle proc_handle)
{ return gray_morph_circle(proc_handle, cv::MORPH_DILATE); }
Herror HCcv_gray_opening_circle(Hproc_handle proc_handle)
{ return gray_morph_circle(proc_handle, cv::MORPH_OPEN); }
Herror HCcv_gray_closing_circle(Hproc_handle proc_handle)
{ return gray_morph_circle(proc_handle, cv::MORPH_CLOSE); }

/* 通用灰度形态学：Op/Shape 字符串枚举（或整数），Kwidth/Kheight 核尺寸，Iterations 迭代 */
Herror HCcv_morphology_ex(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024);  // 枚举字符串读取统一在此分配一次（多次分配会泄漏 temp memory）
    int op = cv::MORPH_OPEN;
    if (!read_enum_param(proc_handle, 1, kMorphOpTable, kMorphOpN, &op))
    {
        HSetErrText(const_cast<char*>(
            "cv_morphology_ex: unknown Op (\"erode\"/\"dilate\"/\"open\"/\"close\"/\"gradient\"/\"tophat\"/\"blackhat\")"));
        return 30003;
    }
    int shape = cv::MORPH_RECT;
    if (!read_enum_param(proc_handle, 2, kMorphShapeTable, kMorphShapeN, &shape))
    {
        HSetErrText(const_cast<char*>(
            "cv_morphology_ex: unknown Shape (\"rect\"/\"cross\"/\"ellipse\")"));
        return 30003;
    }
    Hcpar kw_par, kh_par, it_par;
    HGetSPar(proc_handle, 3, LONG_PAR, &kw_par, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &kh_par, 1);
    HGetSPar(proc_handle, 5, LONG_PAR, &it_par, 1);
    const int kw = (int)kw_par.par.l;
    const int kh = (int)kh_par.par.l;
    if (kw < 1 || kh < 1)
    {
        HSetErrText(const_cast<char*>("cv_morphology_ex: Kwidth/Kheight must be >= 1"));
        return 30003;
    }
    int iterations = (int)it_par.par.l;
    if (iterations < 1) iterations = 1;

    return gray_morph_run(proc_handle, op, shape, kw, kh, iterations);
}