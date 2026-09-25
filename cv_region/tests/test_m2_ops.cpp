#include "cvr/cvr_ops.hpp"
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

static CvrRegion rect(CvrCoord r1, CvrCoord c1, CvrCoord r2, CvrCoord c2) {
    CvrRegion r;
    for (CvrCoord rr = r1; rr <= r2; ++rr) r.runs.push_back({rr, c1, c2});
    return r;
}

int main() {
    const CvrCoord W = 10, H = 10;

    // A: (1..3, 1..3), B: (2..4, 2..4)
    CvrRegion A = rect(1, 1, 3, 3);
    CvrRegion B = rect(2, 2, 4, 4);

    CvrRegion U, I, D, C, S;
    CVR_CHECK(cvr_union2(A, B, W, H, U));
    CVR_CHECK(cvr_intersection(A, B, W, H, I));
    CVR_CHECK(cvr_difference(A, B, W, H, D));
    CVR_CHECK(cvr_complement(A, C));
    CVR_CHECK(cvr_symm_difference(A, B, W, H, S));

    // union 后行 1 应该是 (1,1,3)，行 4 应该是 (4,2,4)
    assert(U.runs.front().r == 1 && U.runs.front().cb == 1 && U.runs.front().ce == 3);
    // intersection 应该是 (2..3, 2..3) -> 2 行
    assert(I.runs.size() == 2);
    assert(I.runs[0].r == 2 && I.runs[0].cb == 2 && I.runs[0].ce == 3);
    // difference = A - B = (1,1..3), (2,1), (3,1)
    assert(D.runs.size() == 3);
    // complement 有 is_compl 标记
    assert(C.is_compl);

    assert(cvr_region_check_invariants(U));
    assert(cvr_region_check_invariants(I));
    assert(cvr_region_check_invariants(D));
    assert(cvr_region_check_invariants(C));
    assert(cvr_region_check_invariants(S));

#ifdef CVR_WITH_OPENCV
    cv::Mat mA, mB;
    CVR_CHECK(cvr_region_to_mask(A, W, H, mA));
    CVR_CHECK(cvr_region_to_mask(B, W, H, mB));

    cv::Mat mU; CVR_CHECK(cvr_region_to_mask(U, W, H, mU));
    cv::Mat refU; cv::bitwise_or(mA, mB, refU);
    assert(cv::countNonZero(mU != refU) == 0);

    cv::Mat mI; CVR_CHECK(cvr_region_to_mask(I, W, H, mI));
    cv::Mat refI; cv::bitwise_and(mA, mB, refI);
    assert(cv::countNonZero(mI != refI) == 0);

    cv::Mat mD; CVR_CHECK(cvr_region_to_mask(D, W, H, mD));
    cv::Mat notB; cv::bitwise_not(mB, notB);
    cv::Mat refD; cv::bitwise_and(mA, notB, refD);
    assert(cv::countNonZero(mD != refD) == 0);

    cv::Mat mS; CVR_CHECK(cvr_region_to_mask(S, W, H, mS));
    cv::Mat notA; cv::bitwise_not(mA, notA);
    cv::Mat notSA; cv::bitwise_and(mB, notA, notSA);
    cv::Mat notSB; cv::bitwise_and(mA, notB, notSB);
    cv::Mat refS; cv::bitwise_or(notSA, notSB, refS);
    assert(cv::countNonZero(mS != refS) == 0);
#endif

    std::cout << "[OK] ops: union/intersection/difference/complement/symm_diff\n";
    std::cout << "M2 ALL PASS\n";
    return 0;
}
