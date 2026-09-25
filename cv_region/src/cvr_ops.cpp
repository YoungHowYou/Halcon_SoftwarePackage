#include "cvr/cvr_ops.hpp"
#include <algorithm>
#include <cstdint>

namespace cvr {

// ---------- 内部: 同行的区间求交 ----------
static void intersect_line(const std::vector<CvrRun>& A, size_t aBeg, size_t aEnd,
                           const std::vector<CvrRun>& B, size_t bBeg, size_t bEnd,
                           CvrCoord r, std::vector<CvrRun>& out)
{
    size_t p = aBeg, q = bBeg;
    while (p < aEnd && q < bEnd) {
        CvrCoord cb = std::max(A[p].cb, B[q].cb);
        CvrCoord ce = std::min(A[p].ce, B[q].ce);
        if (cb <= ce) out.push_back({r, cb, ce});
        if (A[p].ce < B[q].ce) ++p; else ++q;
    }
}

// ---------- 内部: 把单个 region 按定义域物化 ----------
static bool materialize_if_needed(const CvrRegion& r, CvrCoord w, CvrCoord h,
                                  CvrRegion& out, bool& used)
{
    used = false;
    if (!r.is_compl) {
        out = r;
        return true;
    }
    used = true;
    return cvr_region_materialize(r, w, h, out);
}

bool cvr_union1(const std::vector<CvrRegion>& regions,
                CvrCoord w, CvrCoord h, CvrRegion& out)
{
    cvr_clear_last_error();
    out.runs.clear();
    out.is_compl = false;

    if (regions.empty()) return true;

    size_t total = 0;
    for (const auto& r : regions) total += r.runs.size();
    out.runs.reserve(total);

    for (const auto& r : regions) {
        if (r.is_compl) {
            CvrRegion m;
            if (!cvr_region_materialize(r, w, h, m)) return false;
            out.runs.insert(out.runs.end(), m.runs.begin(), m.runs.end());
        } else {
            out.runs.insert(out.runs.end(), r.runs.begin(), r.runs.end());
        }
    }
    return cvr_region_normalize(out);
}

bool cvr_union2(const CvrRegion& r1, const CvrRegion& r2,
                CvrCoord w, CvrCoord h, CvrRegion& out)
{
    cvr_clear_last_error();
    CvrRegion A, B;
    bool ua, ub;
    if (!materialize_if_needed(r1, w, h, A, ua)) return false;
    if (!materialize_if_needed(r2, w, h, B, ub)) return false;

    // 两个输入均为 normalize 后的有序 run 链表：线性归并（O(n)），无需排序。
    // 同行相接/重叠的 run 在下方单次扫描内合并。
    out.runs.clear();
    out.is_compl = false;
    const auto& AR = A.runs;
    const auto& BR = B.runs;
    out.runs.reserve(AR.size() + BR.size());

    CvrRun cur;
    bool has = false;
    auto push_run = [&](CvrRun v) {
        if (!has) { cur = v; has = true; return; }
        if (v.r == cur.r && v.cb <= cur.ce + 1) {
            if (v.ce > cur.ce) cur.ce = v.ce;
        } else {
            out.runs.push_back(cur);
            cur = v;
        }
    };

    size_t i = 0, j = 0;
    while (i < AR.size() || j < BR.size()) {
        if (j >= BR.size() || (i < AR.size() &&
            (AR[i].r < BR[j].r || (AR[i].r == BR[j].r && AR[i].cb <= BR[j].cb)))) {
            push_run(AR[i++]);
        } else {
            push_run(BR[j++]);
        }
    }
    if (has) out.runs.push_back(cur);

    cvr_region_invalidate(out);
    return true;
}

bool cvr_intersection(const CvrRegion& r1, const CvrRegion& r2,
                      CvrCoord w, CvrCoord h, CvrRegion& out)
{
    cvr_clear_last_error();
    CvrRegion A, B;
    bool ua, ub;
    if (!materialize_if_needed(r1, w, h, A, ua)) return false;
    if (!materialize_if_needed(r2, w, h, B, ub)) return false;

    out.runs.clear();
    out.is_compl = false;

    const auto& AR = A.runs;
    const auto& BR = B.runs;

    size_t i = 0, j = 0;
    while (i < AR.size() && j < BR.size()) {
        if (AR[i].r < BR[j].r) { ++i; continue; }
        if (AR[i].r > BR[j].r) { ++j; continue; }

        CvrCoord r = AR[i].r;
        size_t iEnd = i; while (iEnd < AR.size() && AR[iEnd].r == r) ++iEnd;
        size_t jEnd = j; while (jEnd < BR.size() && BR[jEnd].r == r) ++jEnd;

        intersect_line(AR, i, iEnd, BR, j, jEnd, r, out.runs);

        i = iEnd;
        j = jEnd;
    }
    return cvr_region_normalize(out);
}

bool cvr_difference(const CvrRegion& r1, const CvrRegion& r2,
                    CvrCoord w, CvrCoord h, CvrRegion& out)
{
    cvr_clear_last_error();
    CvrRegion A, B;
    bool ua, ub;
    if (!materialize_if_needed(r1, w, h, A, ua)) return false;
    if (!materialize_if_needed(r2, w, h, B, ub)) return false;

    out.runs.clear();
    out.is_compl = false;

    const auto& AR = A.runs;
    const auto& BR = B.runs;

    size_t i = 0, j = 0;
    while (i < AR.size()) {
        CvrCoord r = AR[i].r;
        size_t iEnd = i; while (iEnd < AR.size() && AR[iEnd].r == r) ++iEnd;

        while (j < BR.size() && BR[j].r < r) ++j;
        size_t jBeg = j;
        size_t jEnd = j; while (jEnd < BR.size() && BR[jEnd].r == r) ++jEnd;

        for (size_t p = i; p < iEnd; ++p) {
            CvrCoord curCb = AR[p].cb, curCe = AR[p].ce;
            CvrCoord cursor = curCb;
            for (size_t q = jBeg; q < jEnd; ++q) {
                CvrCoord bc = BR[q].cb, be = BR[q].ce;
                if (be < cursor) continue;
                if (bc > curCe) break;
                if (bc > cursor) out.runs.push_back({r, cursor, bc - 1});
                cursor = std::max(cursor, be + 1);
                if (cursor > curCe) break;
            }
            if (cursor <= curCe) out.runs.push_back({r, cursor, curCe});
        }

        i = iEnd;
        j = jEnd;
    }
    return cvr_region_normalize(out);
}

bool cvr_complement(const CvrRegion& r, CvrRegion& out)
{
    cvr_clear_last_error();
    out = r;
    out.is_compl = !out.is_compl;
    cvr_region_invalidate(out);
    return true;
}

bool cvr_symm_difference(const CvrRegion& r1, const CvrRegion& r2,
                         CvrCoord w, CvrCoord h, CvrRegion& out)
{
    cvr_clear_last_error();
    CvrRegion d1, d2;
    if (!cvr_difference(r1, r2, w, h, d1)) return false;
    if (!cvr_difference(r2, r1, w, h, d2)) return false;
    return cvr_union2(d1, d2, w, h, out);
}

} // namespace cvr
