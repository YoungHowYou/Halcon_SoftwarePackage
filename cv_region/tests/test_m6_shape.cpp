#include "cvr/cvr_shape.hpp"
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
    CvrRegion r = rect(2, 2, 7, 7);

    CvrRegion s1;
    CVR_CHECK(cvr_shape_trans(r, "rectangle1", s1));
    assert(s1.runs.size() == 6);

    CvrRegion s2;
    CVR_CHECK(cvr_shape_trans(r, "rectangle2", s2));
    assert(!s2.runs.empty());

    CvrRegion se;
    CVR_CHECK(cvr_shape_trans(r, "ellipse", se));
    assert(!se.runs.empty());

    CvrRegion sc;
    CVR_CHECK(cvr_shape_trans(r, "outer_circle", sc));
    assert(!sc.runs.empty());

    CvrRegion sconv;
    CVR_CHECK(cvr_shape_trans(r, "convex", sconv));
    assert(!sconv.runs.empty());

    // convex 对凸区域应保持面积不变（近似）
    double row, col;
    CvrChords a1, a2;
    CVR_CHECK(cvr_feature_area_center(r, row, col, a1));
    CVR_CHECK(cvr_feature_area_center(sconv, row, col, a2));
    assert(std::abs((double)a1 - (double)a2) < 1.0);

    std::cout << "[OK] shape_trans: rectangle1/rectangle2/ellipse/outer_circle/convex\n";
    std::cout << "M6 ALL PASS\n";
    return 0;
}
