/*=============================================================================
 * Halcon_OpenCV.cpp — HALCON OpenCV & exiv2 Extension Operators
 * 从 Halcon_YouloBe 迁移的非 OpenVINO 功能
 *
 * 涵盖算子:
 *   remap, PNGIn, PNGOut,
 *   add_roi, mul_roi, sub_B_roi, div_B_roi, div_A_roi, sub_A_roi,
 *   CLAHE_image, write_image_exif,
 *   cv_orb_detect, cv_akaze_detect, cv_bf_knn_match, cv_estimate_affine_partial2d
 *===========================================================================*/

#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h>
  #include <conio.h>
#endif

#include <stdio.h>
#include <iostream>
#include <opencv2/opencv.hpp>
#include "HalconCpp.h"
#include "HDevThread.h"
#include <string>
#include <vector>
#include <memory>
#include <stdlib.h>
#include <string>
#include <fstream>
#include <algorithm>
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

    HGetObj(proc_handle, 2, 1, &in_smallobj_key);
    HGetDImage(proc_handle, in_smallobj_key, 1, &insmallimage);
    HGetObj(proc_handle, 1, 1, &in_bigobj_key);
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

    HGetObj(proc_handle, 2, 1, &in_smallobj_key);
    HGetDImage(proc_handle, in_smallobj_key, 1, &insmallimage);
    HGetObj(proc_handle, 1, 1, &in_bigobj_key);
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

    if (inimage.kind != BYTE_IMAGE)
        return 30002;

    cv::Mat imageIn(inimage.height, inimage.width, CV_8UC1, inimage.pixel.b);

    HCkP(HNewImage(proc_handle, &outimage, BYTE_IMAGE, inimage.width, inimage.height));
    cv::Mat imageOut(outimage.height, outimage.width, CV_8UC1, outimage.pixel.b);

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

    cv::Ptr<cv::ORB> orb = cv::ORB::create(nFeatures);

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

    cv::Ptr<cv::AKAZE> akaze = cv::AKAZE::create();

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

    int nRef = (int)hRef.L();
    int nTarget = (int)hTarget.L();

    cv::Mat matRef(nRef, descWidth, CV_8UC1, (uchar *)ptrRef.L());
    cv::Mat matTarget(nTarget, descWidth, CV_8UC1, (uchar *)ptrTarget.L());

    cv::BFMatcher bf(cv::NORM_HAMMING, false);
    std::vector<std::vector<cv::DMatch>> knnMatches;

    if (nRef < 2 || nTarget < 2)
    {
        SetDictTuple(hv_DictHandle, "NumGoodMatches", (Hlong)0);
        return H_MSG_TRUE;
    }

    try { bf.knnMatch(matRef, matTarget, knnMatches, 2); }
    catch (const cv::Exception &) {
        SetDictTuple(hv_DictHandle, "NumGoodMatches", (Hlong)0);
        return H_MSG_TRUE;
    }

    std::vector<int> goodIdxRef, goodIdxTarget;
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

    std::vector<cv::Point2f> srcPts(nPts), dstPts(nPts);
    for (int i = 0; i < nPts; i++)
    {
        srcPts[i] = cv::Point2f((float)hv_SrcCol[i].D(), (float)hv_SrcRow[i].D());
        dstPts[i] = cv::Point2f((float)hv_DstCol[i].D(), (float)hv_DstRow[i].D());
    }

    cv::Mat inlierMask;
    cv::Mat M = cv::estimateAffinePartial2D(
        srcPts, dstPts, inlierMask, cv::RANSAC, ransacThresh
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
