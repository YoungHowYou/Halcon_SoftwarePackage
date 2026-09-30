/*=============================================================================
 * cvflow/roi_arith.hpp — ROI 区域算术流程（A ⊕ (B ∩ Rect) 族）
 * 由 Halcon_OpenCV.cpp 的 add_roi / mul_roi / sub_* / div_* 迁入，算法未动。
 * 输入均为 CV_16UC1；big 就地修改（与原 Himage 就地语义一致）。
 * 返回码与原实现一致：0 成功；3..7 几何校验错误（1/2 的像素类型校验由调用方做）。
 *===========================================================================*/
#pragma once

#include <opencv2/core.hpp>

namespace cvflow {

// 几何校验：3=负坐标/尺寸，4=超宽，5=超高，6=小图宽≠w，7=小图高≠h
int roi_validate(int smallW, int smallH, int bigW, int bigH,
                 int x, int y, int w, int h);

int roi_add (const cv::Mat& small16, cv::Mat& big16, int x, int y, int w, int h); // A + (B∩Roi)
int roi_mul (const cv::Mat& small16, cv::Mat& big16, int x, int y, int w, int h); // A * (B∩Roi)
int roi_subB(const cv::Mat& small16, cv::Mat& big16, int x, int y, int w, int h); // A - (B∩Roi)
int roi_divB(const cv::Mat& small16, cv::Mat& big16, int x, int y, int w, int h); // A / (B∩Roi)
int roi_divA(const cv::Mat& big16, const cv::Mat& small16, int x, int y, int w, int h); // (A∩Roi) / B
int roi_subA(const cv::Mat& big16, const cv::Mat& small16, int x, int y, int w, int h); // (A∩Roi) - B

} // namespace cvflow
