#include "cvr/cvr_conn.hpp"
#include "cvr/cvr_region.hpp"
#include <cassert>
#include <iostream>

#ifdef CVR_WITH_OPENCV
#include "cvr/cvr_io.hpp"
#include <opencv2/imgproc.hpp>
#endif

using namespace cvr;

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
    const CvrCoord W = 10, H = 10;

#ifdef CVR_WITH_OPENCV
    cv::Mat mask = cv::Mat::zeros(H, W, CV_8U);
    cv::rectangle(mask, {1, 1, 3, 3}, cv::Scalar(255), cv::FILLED);   // blob 1
    cv::rectangle(mask, {6, 6, 2, 2}, cv::Scalar(255), cv::FILLED);   // blob 2
    mask.at<uchar>(4, 4) = 255;                                        // 孤立点

    CvrRegion reg;
    CVR_CHECK(cvr_region_from_mask(mask, reg));
#else
    // 无 OpenCV 时手工构造同样的 region
    CvrRegion reg;
    for (CvrCoord r = 1; r <= 3; ++r) reg.runs.push_back({r, 1, 3});
    for (CvrCoord r = 6; r <= 7; ++r) reg.runs.push_back({r, 6, 7});
    reg.runs.push_back({4, 4, 4});
#endif

    std::vector<CvrRegion> parts;
    CVR_CHECK(cvr_connection(reg, 8, W, H, parts));
    assert(parts.size() == 3);

    for (const auto& p : parts) assert(cvr_region_check_invariants(p));

#ifdef CVR_WITH_OPENCV
    // 对照 OpenCV connectedComponentsWithStats
    cv::Mat lb, st, ce;
    int n = cv::connectedComponentsWithStats(mask, lb, st, ce, 8);
    assert(n == 4); // 背景 + 3 个区域
    std::cout << "[OK] connection: 3 regions (matches OpenCV n-1)\n";
#else
    std::cout << "[OK] connection: 3 regions\n";
#endif

    std::cout << "M3 ALL PASS\n";
    return 0;
}
