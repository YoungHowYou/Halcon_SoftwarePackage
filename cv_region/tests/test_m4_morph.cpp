#include "cvr/cvr_morph.hpp"
#include "cvr/cvr_region.hpp"
#include <cassert>
#include <iostream>

#ifdef CVR_WITH_OPENCV
#include "cvr/cvr_io.hpp"
#include <opencv2/imgproc.hpp>
#endif

using namespace cvr;

// Release 模式下 assert 会被优化掉，因此把有副作用的 cvr 调用放在 assert 外执行
#define CVR_CHECK(expr)                                          \
    do {                                                         \
        bool _ok = (expr);                                       \
        assert(_ok);                                             \
        if (!_ok) {                                              \
            std::cerr << "CHECK failed: " #expr "\n";            \
            return 1;                                            \
        }                                                        \
    } while (0)

int main() {
    const CvrCoord W = 20, H = 20;

#ifdef CVR_WITH_OPENCV
    cv::Mat mask = cv::Mat::zeros(H, W, CV_8U);
    cv::circle(mask, {10, 10}, 8, cv::Scalar(255), 2);   // 圆环

    CvrRegion ring;
    CVR_CHECK(cvr_region_from_mask(mask, ring));
#else
    // 无 OpenCV 时构造一个简单空心矩形环
    CvrRegion ring;
    for (CvrCoord r = 2; r <= 17; ++r) ring.runs.push_back({r, 2, 17});
    for (CvrCoord r = 5; r <= 14; ++r) ring.runs.push_back({r, 5, 14}); // 挖掉内部
    CVR_CHECK(cvr_region_normalize(ring));
#endif

    // fill_up 应该把孔洞填上
    CvrRegion filled;
    CVR_CHECK(cvr_fill_up(ring, W, H, filled));
    assert(cvr_region_check_invariants(filled));

#ifdef CVR_WITH_OPENCV
    cv::Mat mFill;
    CVR_CHECK(cvr_region_to_mask(filled, W, H, mFill));
    assert(mFill.at<uchar>(10, 10) == 255);
#endif

    // dilation1 用 3x3 矩形
    CvrRegion se = cvr_se_rect(3, 3);
    CvrRegion dil;
    CVR_CHECK(cvr_dilation1(ring, se, 1, W, H, dil));
    assert(cvr_region_check_invariants(dil));

#ifdef CVR_WITH_OPENCV
    cv::Mat refDil;
    cv::Mat kernel = cv::Mat::ones(3, 3, CV_8U);
    cv::dilate(mask, refDil, kernel);
    cv::Mat mDil;
    CVR_CHECK(cvr_region_to_mask(dil, W, H, mDil));
    assert(!mDil.empty() && !refDil.empty());
    assert(mDil.size() == refDil.size());
    cv::Mat diffMat;
    cv::compare(mDil, refDil, diffMat, cv::CMP_NE);
    int diff = cv::countNonZero(diffMat);
    std::cout << "dilation pixel diff vs cv::dilate: " << diff << "\n";
    assert(diff < 300);
#endif

    // opening / closing 跑通不崩
    CvrRegion op, cl;
    CVR_CHECK(cvr_opening(ring, se, W, H, op));
    assert(cvr_region_check_invariants(op));
    CVR_CHECK(cvr_closing(ring, se, W, H, cl));
    assert(cvr_region_check_invariants(cl));
    assert(!op.runs.empty());

    std::cout << "[OK] fill_up / dilation1 / opening / closing\n";
    std::cout << "M4 ALL PASS\n";
    return 0;
}
