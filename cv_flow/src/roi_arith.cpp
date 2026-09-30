/*=============================================================================
 * cv_flow/src/roi_arith.cpp — ROI 算术实现（由 Halcon_OpenCV.cpp 迁入，算法未动）
 *===========================================================================*/
#include "cvflow/roi_arith.hpp"

#include <opencv2/imgproc.hpp>   // cv::add/subtract/multiply/divide

namespace cvflow {

int roi_validate(int smallW, int smallH, int bigW, int bigH,
                 int x, int y, int w, int h)
{
    if ((x < 0) || (y < 0) || (w < 0) || (h < 0)) return 3;
    if (x + w > bigW)  return 4;
    if (y + h > bigH)  return 5;
    if (smallW != w)   return 6;
    if (smallH != h)   return 7;
    return 0;
}

int roi_add(const cv::Mat& small16, cv::Mat& big16, int x, int y, int w, int h)
{
    int e = roi_validate(small16.cols, small16.rows, big16.cols, big16.rows, x, y, w, h);
    if (e) return e;
    cv::add(small16, big16(cv::Rect(x, y, w, h)), big16(cv::Rect(x, y, w, h)));
    return 0;
}

int roi_mul(const cv::Mat& small16, cv::Mat& big16, int x, int y, int w, int h)
{
    int e = roi_validate(small16.cols, small16.rows, big16.cols, big16.rows, x, y, w, h);
    if (e) return e;
    cv::multiply(small16, big16(cv::Rect(x, y, w, h)), big16(cv::Rect(x, y, w, h)));
    return 0;
}

int roi_subB(const cv::Mat& small16, cv::Mat& big16, int x, int y, int w, int h)
{
    int e = roi_validate(small16.cols, small16.rows, big16.cols, big16.rows, x, y, w, h);
    if (e) return e;
    cv::subtract(small16, big16(cv::Rect(x, y, w, h)), big16(cv::Rect(x, y, w, h)));
    return 0;
}

int roi_divB(const cv::Mat& small16, cv::Mat& big16, int x, int y, int w, int h)
{
    int e = roi_validate(small16.cols, small16.rows, big16.cols, big16.rows, x, y, w, h);
    if (e) return e;
    cv::divide(small16, big16(cv::Rect(x, y, w, h)), big16(cv::Rect(x, y, w, h)));
    return 0;
}

int roi_divA(const cv::Mat& big16, const cv::Mat& small16, int x, int y, int w, int h)
{
    int e = roi_validate(small16.cols, small16.rows, big16.cols, big16.rows, x, y, w, h);
    if (e) return e;
    cv::divide(big16(cv::Rect(x, y, w, h)), small16, big16(cv::Rect(x, y, w, h)));
    return 0;
}

int roi_subA(const cv::Mat& big16, const cv::Mat& small16, int x, int y, int w, int h)
{
    int e = roi_validate(small16.cols, small16.rows, big16.cols, big16.rows, x, y, w, h);
    if (e) return e;
    cv::subtract(big16(cv::Rect(x, y, w, h)), small16, big16(cv::Rect(x, y, w, h)));
    return 0;
}

} // namespace cvflow
