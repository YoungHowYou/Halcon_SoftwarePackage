#include "cvr/cvr_feat.hpp"
#include "cvr/cvr_region.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

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
    // 10x10 实心矩形 (1,1)-(8,8)，面积 64
    CvrRegion r = rect(1, 1, 8, 8);

    double row, col;
    CvrChords area;
    CVR_CHECK(cvr_feature_area_center(r, row, col, area));
    assert(area == 64);
    assert(std::abs(row - 4.5) < 1e-9 && std::abs(col - 4.5) < 1e-9);

    CvrCoord r1, c1, r2, c2;
    CVR_CHECK(cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2));
    assert(r1 == 1 && c1 == 1 && r2 == 8 && c2 == 8);

    double phi, ra, rb;
    CVR_CHECK(cvr_feature_elliptic_axis(r, ra, rb, phi));
    // 正方形：ra ≈ rb
    assert(std::abs(ra - rb) < 1.0);

    double cl;
    CVR_CHECK(cvr_feature_contlength(r, cl));
    // 8x8 正方形轮廓 = 32
    assert(std::abs(cl - 32.0) < 1.0);

    double conv;
    bool isconv;
    CVR_CHECK(cvr_feature_convexity(r, conv, isconv));
    assert(isconv && conv > 0.99);

    double comp, circ, rect;
    CVR_CHECK(cvr_feature_compactness(r, comp));
    CVR_CHECK(cvr_feature_circularity(r, circ));
    CVR_CHECK(cvr_feature_rectangularity(r, rect));
    assert(rect > 0.99);

    // rectangle2 对正方形应接近 axis-aligned，length1 ≈ length2
    double rrow, rcol, rphi, l1, l2;
    CVR_CHECK(cvr_feature_smallest_rectangle2(r, rrow, rcol, rphi, l1, l2));
    assert(std::abs(l1 - 4.0) < 1.0 && std::abs(l2 - 4.0) < 1.0);

    // cvr_get_feature 查表
    double v_area, v_row, v_phi;
    CVR_CHECK(cvr_get_feature(r, "area", v_area));
    CVR_CHECK(cvr_get_feature(r, "row", v_row));
    CVR_CHECK(cvr_get_feature(r, "phi", v_phi));
    assert(std::abs(v_area - 64.0) < 1e-9);
    assert(std::abs(v_row - 4.5) < 1e-9);

    std::cout << "[OK] features: area/center/rect1/rect2/ellipse/contlength/convexity\n";
    std::cout << "M5 ALL PASS\n";
    return 0;
}
