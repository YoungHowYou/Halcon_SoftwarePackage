#include "cvr/cvr_region.hpp"
#include <algorithm>
#include <cstring>

namespace cvr {

namespace {
thread_local std::string g_cvr_last_error;
} // namespace

const char* cvr_last_error() noexcept {
    return g_cvr_last_error.c_str();
}

void cvr_set_last_error(const std::string& msg) noexcept {
    g_cvr_last_error = msg;
}

void cvr_clear_last_error() noexcept {
    g_cvr_last_error.clear();
}

// ----------------------------------------------------------------------------
void cvr_region_invalidate(CvrRegion& r) noexcept {
    r.feature.flags.reset();
    // 同时重置 shape 等派生标记，避免被误用
    r.feature.shape = 0;
    r.feature.is_convex = false;
    r.feature.is_filled = false;
    r.feature.is_connected4 = false;
    r.feature.is_connected8 = false;
    r.feature.is_thin = false;
}

// ----------------------------------------------------------------------------
bool cvr_region_normalize(CvrRegion& r) {
    if (r.runs.empty()) {
        cvr_region_invalidate(r);
        return true;
    }

    // 仅当未按 (r, cb) 有序时才排序：交/并/形态学等多数算子输出本就有序，
    // 可避免 O(n log n)。有序性检查是 O(n)。
    if (!std::is_sorted(r.runs.begin(), r.runs.end(), cvr_run_less))
        std::sort(r.runs.begin(), r.runs.end(), cvr_run_less);

    std::vector<CvrRun> out;
    out.reserve(r.runs.size());

    CvrRun cur = r.runs[0];
    if (cur.cb > cur.ce) {
        cvr_set_last_error("cvr_region_normalize: invalid run with cb > ce");
        return false;
    }

    for (size_t i = 1; i < r.runs.size(); ++i) {
        const CvrRun& nxt = r.runs[i];
        if (nxt.cb > nxt.ce) {
            cvr_set_last_error("cvr_region_normalize: invalid run with cb > ce");
            return false;
        }
        if (nxt.r == cur.r && nxt.cb <= cur.ce + 1) {
            // 同一行且相接或重叠，合并
            if (nxt.ce > cur.ce) cur.ce = nxt.ce;
        } else {
            out.push_back(cur);
            cur = nxt;
        }
    }
    out.push_back(cur);

    r.runs = std::move(out);
    cvr_region_invalidate(r);
    return true;
}

// ----------------------------------------------------------------------------
bool cvr_region_is_empty(const CvrRegion& r, CvrCoord w, CvrCoord h) noexcept {
    if (w <= 0 || h <= 0) {
        // 无定义域时，普通 region 空当且仅当 runs 空
        return !r.is_compl && r.runs.empty();
    }
    if (!r.is_compl) return r.runs.empty();
    // 补集：空当且仅当定义域面积 == runs 面积
    CvrChords domain_area = static_cast<CvrChords>(w) * static_cast<CvrChords>(h);
    CvrChords fg_area = 0;
    for (const auto& rr : r.runs) fg_area += static_cast<CvrChords>(rr.ce - rr.cb + 1);
    return fg_area >= domain_area;
}

// ----------------------------------------------------------------------------
bool cvr_region_materialize(const CvrRegion& r, CvrCoord w, CvrCoord h,
                            CvrRegion& out) {
    cvr_clear_last_error();
    if (w <= 0 || h <= 0) {
        cvr_set_last_error("cvr_region_materialize: invalid domain size");
        return false;
    }

    out.runs.clear();
    out.is_compl = false;

    if (!r.is_compl) {
        out.runs = r.runs;
        cvr_region_invalidate(out);
        return cvr_region_normalize(out);
    }

    // 补集：逐行生成背景段
    out.runs.reserve(static_cast<size_t>(h));
    size_t idx = 0;
    const auto& R = r.runs;

    for (CvrCoord row = 0; row < h; ++row) {
        CvrCoord cursor = 0;
        while (idx < R.size() && R[idx].r < row) ++idx;
        size_t j = idx;
        while (j < R.size() && R[j].r == row) {
            if (R[j].cb > cursor) {
                out.runs.push_back({row, cursor, R[j].cb - 1});
            }
            cursor = std::max(cursor, R[j].ce + 1);
            if (cursor >= w) break;
            ++j;
        }
        if (cursor < w) {
            out.runs.push_back({row, cursor, w - 1});
        }
        idx = j;
    }

    cvr_region_invalidate(out);
    return cvr_region_normalize(out);
}

// ----------------------------------------------------------------------------
bool cvr_region_bbox(const CvrRegion& r, CvrCoord& row1, CvrCoord& col1,
                     CvrCoord& row2, CvrCoord& col2) {
    cvr_clear_last_error();
    if (r.runs.empty()) {
        cvr_set_last_error("cvr_region_bbox: empty region");
        return false;
    }

    row1 = r.runs.front().r;
    row2 = r.runs.back().r;
    col1 = r.runs.front().cb;
    col2 = r.runs.front().ce;

    for (const auto& rr : r.runs) {
        if (rr.cb < col1) col1 = rr.cb;
        if (rr.ce > col2) col2 = rr.ce;
    }
    return true;
}

// ----------------------------------------------------------------------------
bool cvr_region_row_ranges(const CvrRegion& r, CvrCoord row,
                           size_t& begin, size_t& end) {
    cvr_clear_last_error();
    begin = 0;
    end = 0;

    // 简单线性扫描；runs 已排序，可用二分优化
    size_t i = 0;
    while (i < r.runs.size() && r.runs[i].r < row) ++i;
    begin = i;
    while (i < r.runs.size() && r.runs[i].r == row) ++i;
    end = i;
    return true;
}

// ----------------------------------------------------------------------------
bool cvr_region_check_invariants(const CvrRegion& r) noexcept {
    if (r.runs.empty()) return true;

    for (size_t i = 0; i < r.runs.size(); ++i) {
        const auto& rr = r.runs[i];
        if (rr.cb > rr.ce) return false;
        if (i > 0) {
            const auto& prev = r.runs[i - 1];
            if (cvr_run_less(rr, prev)) return false;
            if (rr.r == prev.r && rr.cb <= prev.ce + 1) return false;
        }
    }
    return true;
}

} // namespace cvr
