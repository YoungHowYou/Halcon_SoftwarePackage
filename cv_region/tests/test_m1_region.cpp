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
    for (CvrCoord rr = r1; rr <= r2; ++rr) {
        r.runs.push_back({rr, c1, c2});
    }
    return r;
}

int main() {
    // ---- normalize：合并同行相接 run ----
    {
        CvrRegion r;
        r.runs = {{1, 1, 3}, {1, 4, 6}, {2, 2, 5}};
        CVR_CHECK(cvr_region_normalize(r));
        assert(r.runs.size() == 2);
        assert(r.runs[0].r == 1 && r.runs[0].cb == 1 && r.runs[0].ce == 6);
        assert(r.runs[1].r == 2 && r.runs[1].cb == 2 && r.runs[1].ce == 5);
        assert(cvr_region_check_invariants(r));
        std::cout << "[OK] normalize merge\n";
    }

    // ---- materialize 补集 ----
    {
        CvrRegion r = rect(1, 1, 3, 3);
        r.is_compl = true;
        CvrRegion m;
        CVR_CHECK(cvr_region_materialize(r, 5, 5, m));
        assert(!m.is_compl);
        // 定义域 5x5 = 25，前景 3x3 = 9，背景 16 像素 => 16 条 runs
        CvrChords area = 0;
        for (const auto& rr : m.runs) area += rr.ce - rr.cb + 1;
        assert(area == 16);
        assert(cvr_region_check_invariants(m));
        std::cout << "[OK] complement materialize\n";
    }

    // ---- bbox ----
    {
        CvrRegion r = rect(2, 3, 7, 9);
        CvrCoord r1, c1, r2, c2;
        CVR_CHECK(cvr_region_bbox(r, r1, c1, r2, c2));
        assert(r1 == 2 && c1 == 3 && r2 == 7 && c2 == 9);
        std::cout << "[OK] bbox\n";
    }

    // ---- is_empty ----
    {
        CvrRegion empty;
        assert(cvr_region_is_empty(empty, 10, 10));

        CvrRegion full;
        full.is_compl = true;
        full.runs.clear();
        assert(cvr_region_is_empty(full, 10, 10)); // 补空 = 全图
        std::cout << "[OK] is_empty\n";
    }

    // ---- invalid run 检测 ----
    {
        CvrRegion r;
        r.runs = {{1, 5, 3}}; // cb > ce
        bool ok = cvr_region_normalize(r);
        assert(!ok);
        assert(std::string(cvr_last_error()).find("cb > ce") != std::string::npos);
        std::cout << "[OK] invalid run detection\n";
    }

    std::cout << "M1 ALL PASS\n";
    return 0;
}
