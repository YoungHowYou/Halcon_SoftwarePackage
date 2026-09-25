#include "cvr/cvr_select.hpp"
#include "cvr/cvr_feat.hpp"
#include "cvr/cvr_region.hpp"
#include <cassert>
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
    std::vector<CvrRegion> regs;
    regs.push_back(rect(1, 1, 3, 3));   // area 9
    regs.push_back(rect(1, 1, 5, 5));   // area 25
    regs.push_back(rect(1, 1, 7, 7));   // area 49

    std::vector<CvrRegion> sel;
    CVR_CHECK(cvr_select_shape_single(regs, "area", 20.0, 50.0, sel));
    assert(sel.size() == 1);

    sel.clear();
    CVR_CHECK(cvr_select_shape(regs, {"area"}, "and", {10.0}, {30.0}, sel));
    assert(sel.size() == 2);

    sel.clear();
    CVR_CHECK(cvr_select_shape(regs, {"area"}, "or", {40.0}, {100.0}, sel));
    assert(sel.size() == 1);

    // 未知特征应返回 false
    double v;
    bool ok = cvr_get_feature(regs[0], "unknown_feature", v);
    assert(!ok);

    std::cout << "[OK] select_shape: single/and/or filtering\n";
    std::cout << "M7 ALL PASS\n";
    return 0;
}
