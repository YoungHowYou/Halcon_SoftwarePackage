#include "cvr/cvr_morph.hpp"
#include "cvr/cvr_ops.hpp"
#include "cvr/cvr_feat.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace cvr {

// ---------- 结构元生成 ----------
CvrRegion cvr_gen_circle(CvrCoord row, CvrCoord col, double radius) {
    // HALCON 语义：像素 (r,c) ∈ 圆盘  iff  (r-row)^2 + (c-col)^2 <= radius^2
    CvrRegion se;
    const double r2 = radius * radius;
    const int rr = (int)std::floor(radius);
    for (int dr = -rr; dr <= rr; ++dr) {
        const double rem = r2 - (double)dr * dr;
        if (rem < 0) continue;   // 该行无像素，不产生 run
        int w = (int)std::floor(std::sqrt(rem));
        se.runs.push_back({row + dr, col - w, col + w});
    }
    return se;
}

CvrRegion cvr_gen_rectangle1(CvrCoord r1, CvrCoord c1, CvrCoord r2, CvrCoord c2) {
    if (r1 > r2) std::swap(r1, r2);
    if (c1 > c2) std::swap(c1, c2);
    CvrRegion se;
    for (CvrCoord r = r1; r <= r2; ++r)
        se.runs.push_back({r, c1, c2});
    return se;
}

CvrRegion cvr_se_disk(int radius) {
    return cvr_gen_circle(0, 0, (double)radius);
}
CvrRegion cvr_se_rect(int w, int h) {
    // 从 0 生成（w 列 h 行），居中交给 center_se 按四舍五入质心处理。
    // 这样偶数尺寸与 HALCON erosion_rectangle1/dilation_rectangle1 一致：
    // 质心 (h-1)/2 经 lround 后为 ceil((h-1)/2)，不会偏向负方向。
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    return cvr_gen_rectangle1(0, 0, h - 1, w - 1);
}
CvrRegion cvr_se_cross(int radius) {
    CvrRegion se;
    for (int d = -radius; d <= radius; ++d) se.runs.push_back({d, 0, 0});
    for (int d = -radius; d <= radius; ++d) if (d != 0) se.runs.push_back({0, d, d});
    return se;
}

bool cvr_se_transpose(const CvrRegion& se, CvrRegion& out) {
    cvr_clear_last_error();
    out.runs.clear();
    out.is_compl = false;
    out.runs.reserve(se.runs.size());
    for (const auto& rr : se.runs) {
        // (r, cb, ce) -> (-r, -ce, -cb)
        out.runs.push_back({-rr.r, -rr.ce, -rr.cb});
    }
    return cvr_region_normalize(out);
}

// ---------- SE 居中（HALCON 参考点约定：四舍五入质心移到原点） ----------
static bool center_se(const CvrRegion& se, CvrRegion& out)
{
    double row = 0.0, col = 0.0;
    CvrChords area = 0;
    if (!cvr_feature_area_center(se, row, col, area)) return false;
    if (area <= 0) {
        cvr_set_last_error("center_se: empty structuring element");
        return false;
    }
    const CvrCoord sr = (CvrCoord)std::lround(row);
    const CvrCoord sc = (CvrCoord)std::lround(col);
    out.runs.clear();
    out.is_compl = false;
    out.runs.reserve(se.runs.size());
    for (const auto& rr : se.runs)
        out.runs.push_back({rr.r - sr, rr.cb - sc, rr.ce - sc});
    return cvr_region_normalize(out);
}

// 居中 + 反射（HALCON dilation 用反射后的居中 SE）
static bool center_se_reflected(const CvrRegion& se, CvrRegion& out)
{
    CvrRegion c;
    if (!center_se(se, c)) return false;
    return cvr_se_transpose(c, out);
}

// ---------- 内部：平移 region（带定义域裁剪） ----------
static bool shift_region(const CvrRegion& src, CvrCoord dr, CvrCoord dc,
                         CvrCoord w, CvrCoord h,
                         CvrRegion& out)
{
    out.runs.clear();
    out.is_compl = false;
    out.runs.reserve(src.runs.size());
    for (const auto& rr : src.runs) {
        CvrCoord nr = rr.r + dr;
        if (nr < 0 || nr >= h) continue;
        CvrCoord cb = rr.cb + dc;
        CvrCoord ce = rr.ce + dc;
        if (ce < 0 || cb >= w) continue;
        cb = std::max<CvrCoord>(0, cb);
        ce = std::min<CvrCoord>(w - 1, ce);
        if (cb <= ce) out.runs.push_back({nr, cb, ce});
    }
    return cvr_region_normalize(out);
}

// ============================================================================
// 按行（RLE-native）快速形态学。
// 思路：腐蚀/膨胀都是"行方向的 1D 运算 + 跨 SE 行的交/并"。
// 对每行输出 y，腐蚀 = ∩_{(dr,[a,b])∈SE} shrink(R_{y+dr},[a,b])，
// 膨胀 = ∪_{(dr,[a,b])∈SE} shift(R_{y-dr},[a,b])。
// 复杂度 O(n × SE行数)，无逐像素循环、无中途排序。
// 要求 SE 每行连续（单 run）；圆盘/矩形/十字均满足，任意 SE 回退旧路径。
// ============================================================================

/* SE 行连续判定 + 展开：rows 按 dr 升序（se 已 normalize，天然按行有序） */
static bool se_row_intervals(const CvrRegion& se, std::vector<CvrRun>& rows) {
    rows.clear();
    rows.reserve(se.runs.size());
    for (const auto& rr : se.runs) {
        if (!rows.empty() && rr.r == rows.back().r) return false; // 同行多段
        rows.push_back(rr);
    }
    return true;
}

/* 输入 region 的行索引（runs 已按 (r,cb) 有序） */
struct CvrRowInfo { CvrCoord r; int start; int count; };
static void build_row_index(const std::vector<CvrRun>& runs,
                            std::vector<CvrRowInfo>& idx) {
    idx.clear();
    size_t i = 0, n = runs.size();
    while (i < n) {
        size_t j = i;
        while (j < n && runs[j].r == runs[i].r) ++j;
        idx.push_back({runs[i].r, (int)i, (int)(j - i)});
        i = j;
    }
}
static int find_row_bin(const std::vector<CvrRowInfo>& idx, CvrCoord r) {
    int lo = 0, hi = (int)idx.size() - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (idx[mid].r < r) lo = mid + 1;
        else if (idx[mid].r > r) hi = mid - 1;
        else return mid;
    }
    return -1;
}

/* acc ∩ { [cb-a, ce-b] | [cb,ce]∈B }（腐蚀单行收缩）；结果行号 y，裁剪到 [0,w-1]。
   scratch 为调用方复用的暂存（swap 后保留容量，避免每行堆分配） */
static void erode_row_intersect(std::vector<CvrRun>& acc, const CvrRun* B, int nb,
                                CvrCoord a, CvrCoord b, CvrCoord y, CvrCoord w,
                                std::vector<CvrRun>& scratch) {
    scratch.clear();
    size_t i = 0, j = 0;
    while (i < acc.size() && j < (size_t)nb) {
        CvrCoord bcb = B[j].cb - a;
        CvrCoord bce = B[j].ce - b;
        if (bcb < 0) bcb = 0;
        if (bce > w - 1) bce = w - 1;
        if (bcb <= bce) {
            CvrCoord cb = std::max(acc[i].cb, bcb);
            CvrCoord ce = std::min(acc[i].ce, bce);
            if (cb <= ce) scratch.push_back({y, cb, ce});
            if (acc[i].ce < bce) ++i; else ++j;
        } else {
            ++j;
        }
    }
    acc.swap(scratch);
}

/* acc ∪ { [cb+a, ce+b] | [cb,ce]∈B }（膨胀单行平移合并）；结果行号 y，裁剪到 [0,w-1] */
static void dilate_row_union(std::vector<CvrRun>& acc, const CvrRun* B, int nb,
                             CvrCoord a, CvrCoord b, CvrCoord y, CvrCoord w,
                             std::vector<CvrRun>& scratch) {
    scratch.clear();
    scratch.reserve(acc.size() + (size_t)nb);
    size_t i = 0, j = 0;
    CvrRun cur{0, 0, 0};
    bool has = false;
    auto push = [&](CvrCoord cb, CvrCoord ce) {
        if (!has) { cur = {y, cb, ce}; has = true; return; }
        if (cb <= cur.ce + 1) { if (ce > cur.ce) cur.ce = ce; }
        else { scratch.push_back(cur); cur = {y, cb, ce}; }
    };
    while (i < acc.size() || j < (size_t)nb) {
        CvrCoord cb, ce;
        bool fromA;
        if (j >= (size_t)nb) { fromA = true; }
        else if (i >= acc.size()) { fromA = false; }
        else fromA = (acc[i].cb <= B[j].cb + a);
        if (fromA) { cb = acc[i].cb; ce = acc[i].ce; ++i; }
        else {
            cb = B[j].cb + a; ce = B[j].ce + b; ++j;
            if (ce < 0) continue;
            if (cb < 0) cb = 0;
            if (ce > w - 1) ce = w - 1;
            if (cb > ce) continue;
        }
        push(cb, ce);
    }
    if (has) scratch.push_back(cur);
    acc.swap(scratch);
}

/* 按行腐蚀（iterations 次） */
static bool erosion_core_rows(const CvrRegion& r, const std::vector<CvrRun>& seRows,
                              int iterations, CvrCoord w, CvrCoord h, CvrRegion& out) {
    CvrRegion cur;
    if (r.is_compl) {
        if (!cvr_region_materialize(r, w, h, cur)) return false;
    } else {
        cur = r;
    }

    const CvrCoord drMin = seRows.front().r;
    const CvrCoord drMax = seRows.back().r;
    std::vector<CvrRowInfo> idx;
    std::vector<CvrRun> acc;
    std::vector<CvrRun> scratch;   // 双缓冲复用，避免每行堆分配

    for (int it = 0; it < iterations; ++it) {
        build_row_index(cur.runs, idx);
        if (idx.empty()) { out.runs.clear(); cvr_region_invalidate(out); return true; }
        const CvrCoord minR = idx.front().r, maxR = idx.back().r;
        const CvrCoord yLo = std::max<CvrCoord>(0, (CvrCoord)(minR - drMax));
        const CvrCoord yHi = std::min<CvrCoord>(h - 1, (CvrCoord)(maxR - drMin));

        CvrRegion next;
        next.is_compl = false;
        next.runs.reserve(cur.runs.size());

        for (CvrCoord y = yLo; y <= yHi; ++y) {
            acc.clear();
            bool first = true;
            bool dead = false;
            for (const CvrRun& sre : seRows) {
                int k = find_row_bin(idx, y + sre.r);
                if (k < 0) { dead = true; break; } // 某 SE 行输入缺失 -> 该行腐蚀为空
                const CvrRowInfo& ri = idx[k];
                if (first) {
                    // 首行：直接生成收缩后的候选
                    acc.reserve((size_t)ri.count);
                    for (int t = 0; t < ri.count; ++t) {
                        CvrCoord cb = cur.runs[ri.start + t].cb - sre.cb;
                        CvrCoord ce = cur.runs[ri.start + t].ce - sre.ce;
                        if (cb < 0) cb = 0;
                        if (ce > w - 1) ce = w - 1;
                        if (cb <= ce) acc.push_back({y, cb, ce});
                    }
                    first = false;
                    if (acc.empty()) { dead = true; break; }
                } else {
                    erode_row_intersect(acc, cur.runs.data() + ri.start, ri.count,
                                        sre.cb, sre.ce, y, w, scratch);
                    if (acc.empty()) { dead = true; break; }
                }
            }
            if (!dead && !acc.empty()) {
                next.runs.insert(next.runs.end(), acc.begin(), acc.end());
            }
        }
        cur = std::move(next);
    }
    out = std::move(cur);
    return true;
}

/* 按行膨胀（iterations 次） */
static bool dilation_core_rows(const CvrRegion& r, const std::vector<CvrRun>& seRows,
                               int iterations, CvrCoord w, CvrCoord h, CvrRegion& out) {
    CvrRegion cur;
    if (r.is_compl) {
        if (!cvr_region_materialize(r, w, h, cur)) return false;
    } else {
        cur = r;
    }

    const CvrCoord drMin = seRows.front().r;
    const CvrCoord drMax = seRows.back().r;
    std::vector<CvrRowInfo> idx;
    std::vector<CvrRun> acc;
    std::vector<CvrRun> scratch;   // 双缓冲复用

    for (int it = 0; it < iterations; ++it) {
        build_row_index(cur.runs, idx);
        if (idx.empty()) { out.runs.clear(); cvr_region_invalidate(out); return true; }
        const CvrCoord minR = idx.front().r, maxR = idx.back().r;
        const CvrCoord yLo = std::max<CvrCoord>(0, (CvrCoord)(minR + drMin));
        const CvrCoord yHi = std::min<CvrCoord>(h - 1, (CvrCoord)(maxR + drMax));

        CvrRegion next;
        next.is_compl = false;
        next.runs.reserve(cur.runs.size());

        for (CvrCoord y = yLo; y <= yHi; ++y) {
            acc.clear();
            bool first = true;
            for (const CvrRun& sre : seRows) {
                int k = find_row_bin(idx, y - sre.r);
                if (k < 0) continue; // 该 SE 行无输入，跳过（并对集无贡献）
                const CvrRowInfo& ri = idx[k];
                if (first) {
                    acc.reserve((size_t)ri.count);
                    for (int t = 0; t < ri.count; ++t) {
                        CvrCoord cb = cur.runs[ri.start + t].cb + sre.cb;
                        CvrCoord ce = cur.runs[ri.start + t].ce + sre.ce;
                        if (cb < 0) cb = 0;
                        if (ce > w - 1) ce = w - 1;
                        if (cb <= ce) acc.push_back({y, cb, ce});
                    }
                    first = false;
                } else {
                    dilate_row_union(acc, cur.runs.data() + ri.start, ri.count,
                                     sre.cb, sre.ce, y, w, scratch);
                }
            }
            if (!acc.empty())
                next.runs.insert(next.runs.end(), acc.begin(), acc.end());
        }
        cur = std::move(next);
    }
    out = std::move(cur);
    return true;
}

// ---------- 膨胀核心（不居中，SE 由调用方准备；Minkowski 和 {x+s}） ----------
static bool dilation_core(const CvrRegion& r, const CvrRegion& se,
                          int iterations, CvrCoord w, CvrCoord h,
                          CvrRegion& out)
{
    CvrRegion cur;
    if (r.is_compl) {
        if (!cvr_region_materialize(r, w, h, cur)) return false;
    } else {
        cur = r;
    }

    for (int it = 0; it < iterations; ++it) {
        CvrRegion next;
        next.runs.reserve(cur.runs.size() * se.runs.size());

        for (const auto& rr : cur.runs) {
            for (const auto& sr : se.runs) {
                CvrCoord nr = rr.r + sr.r;
                if (nr < 0 || nr >= h) continue;
                CvrCoord cb = rr.cb + sr.cb;
                CvrCoord ce = rr.ce + sr.ce;
                if (ce < 0 || cb >= w) continue;
                cb = std::max<CvrCoord>(0, cb);
                ce = std::min<CvrCoord>(w - 1, ce);
                if (cb <= ce) next.runs.push_back({nr, cb, ce});
            }
        }
        if (!cvr_region_normalize(next)) return false;
        cur = std::move(next);
    }
    out = std::move(cur);
    return true;
}

// ---------- 腐蚀核心（不居中；结果 = {p : p+SE ⊆ R}） ----------
static bool erosion_core(const CvrRegion& r, const CvrRegion& se,
                         int iterations, CvrCoord w, CvrCoord h,
                         CvrRegion& out)
{
    CvrRegion cur;
    if (r.is_compl) {
        if (!cvr_region_materialize(r, w, h, cur)) return false;
    } else {
        cur = r;
    }

    for (int it = 0; it < iterations; ++it) {
        CvrRegion acc = cur;
        for (const auto& sr : se.runs) {
            CvrRegion sh;
            if (!shift_region(cur, -sr.r, -sr.cb, w, h, sh)) return false;

            // 同一 run 内剩余列继续求交
            for (CvrCoord dc = sr.cb + 1; dc <= sr.ce; ++dc) {
                CvrRegion sh2;
                if (!shift_region(cur, -sr.r, -dc, w, h, sh2)) return false;
                CvrRegion tmp;
                if (!cvr_intersection(sh, sh2, w, h, tmp)) return false;
                sh = std::move(tmp);
                if (sh.runs.empty()) break;
            }
            CvrRegion tmp;
            if (!cvr_intersection(acc, sh, w, h, tmp)) return false;
            acc = std::move(tmp);
            if (acc.runs.empty()) break;
        }
        cur = std::move(acc);
    }
    out = std::move(cur);
    return true;
}

// ---------- 膨胀 ----------
bool cvr_dilation1(const CvrRegion& r, const CvrRegion& se, int iterations,
                   CvrCoord w, CvrCoord h, CvrRegion& out)
{
    cvr_clear_last_error();
    if (iterations < 1) {
        cvr_set_last_error("cvr_dilation1: iterations must be >= 1");
        return false;
    }
    if (w <= 0 || h <= 0) {
        cvr_set_last_error("cvr_dilation1: invalid domain size");
        return false;
    }

    // HALCON 约定：SE 参考点 = 四舍五入质心；膨胀用居中+反射后的 SE
    CvrRegion seR;
    if (!center_se_reflected(se, seR)) return false;

    // 快速路径：SE 每行连续时按行 O(n×SE行数) 计算
    std::vector<CvrRun> seRows;
    if (se_row_intervals(seR, seRows))
        return dilation_core_rows(r, seRows, iterations, w, h, out);

    return dilation_core(r, seR, iterations, w, h, out);
}

// ---------- 腐蚀（用平移-相交实现） ----------
bool cvr_erosion1(const CvrRegion& r, const CvrRegion& se, int iterations,
                  CvrCoord w, CvrCoord h, CvrRegion& out)
{
    cvr_clear_last_error();
    if (iterations < 1) {
        cvr_set_last_error("cvr_erosion1: iterations must be >= 1");
        return false;
    }
    if (w <= 0 || h <= 0) {
        cvr_set_last_error("cvr_erosion1: invalid domain size");
        return false;
    }

    // HALCON 约定：SE 参考点 = 四舍五入质心；腐蚀用居中后的 SE 原样
    CvrRegion sec;
    if (!center_se(se, sec)) return false;

    // 快速路径：SE 每行连续时按行 O(n×SE行数) 计算
    std::vector<CvrRun> seRows;
    if (se_row_intervals(sec, seRows))
        return erosion_core_rows(r, seRows, iterations, w, h, out);

    return erosion_core(r, sec, iterations, w, h, out);
}

// ---------- 开闭 ----------
bool cvr_opening(const CvrRegion& r, const CvrRegion& se,
                 CvrCoord w, CvrCoord h, CvrRegion& out)
{
    cvr_clear_last_error();
    if (w <= 0 || h <= 0) {
        cvr_set_last_error("cvr_opening: invalid domain size");
        return false;
    }
    CvrRegion sec, seR, ero;
    if (!center_se(se, sec)) return false;
    if (!cvr_se_transpose(sec, seR)) return false;
    if (!erosion_core(r, sec, 1, w, h, ero)) return false;
    return dilation_core(ero, seR, 1, w, h, out);
}

bool cvr_closing(const CvrRegion& r, const CvrRegion& se,
                 CvrCoord w, CvrCoord h, CvrRegion& out)
{
    cvr_clear_last_error();
    if (w <= 0 || h <= 0) {
        cvr_set_last_error("cvr_closing: invalid domain size");
        return false;
    }
    CvrRegion sec, seR, dil;
    if (!center_se(se, sec)) return false;
    if (!cvr_se_transpose(sec, seR)) return false;
    if (!dilation_core(r, seR, 1, w, h, dil)) return false;
    return erosion_core(dil, seR, 1, w, h, out);
}

// ---------- fill_up ----------
bool cvr_fill_up(const CvrRegion& r, CvrCoord w, CvrCoord h, CvrRegion& out)
{
    cvr_clear_last_error();
    if (w <= 0 || h <= 0) {
        cvr_set_last_error("cvr_fill_up: invalid domain size");
        return false;
    }

    CvrRegion mat;
    if (r.is_compl) {
        if (!cvr_region_materialize(r, w, h, mat)) return false;
    } else {
        mat = r;
    }
    if (!cvr_region_normalize(mat)) return false;

    // 按行索引前景
    std::unordered_map<CvrCoord, std::vector<CvrRun>> fg;
    fg.reserve(mat.runs.size());
    for (const auto& rr : mat.runs) fg[rr.r].push_back(rr);

    // 生成背景 runs
    std::vector<CvrRun> bg;
    bg.reserve(mat.runs.size());
    for (CvrCoord row = 0; row < h; ++row) {
        auto it = fg.find(row);
        CvrCoord cursor = 0;
        if (it != fg.end()) {
            auto v = it->second;
            std::sort(v.begin(), v.end(), cvr_run_less);
            for (const auto& fr : v) {
                if (fr.cb > cursor) bg.push_back({row, cursor, fr.cb - 1});
                cursor = std::max(cursor, fr.ce + 1);
            }
        }
        if (cursor < w) bg.push_back({row, cursor, w - 1});
    }
    if (bg.empty()) {
        out = mat;
        return true;
    }

    // 按行分组
    std::unordered_map<CvrCoord, std::vector<size_t>> bgRows;
    bgRows.reserve(bg.size());
    for (size_t i = 0; i < bg.size(); ++i) bgRows[bg[i].r].push_back(i);

    std::vector<CvrCoord> rows;
    rows.reserve(bgRows.size());
    for (auto& kv : bgRows) rows.push_back(kv.first);
    std::sort(rows.begin(), rows.end());

    // 并查集
    std::vector<int> parent(bg.size());
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    auto uni = [&](int a, int b) { parent[find(a)] = find(b); };

    for (size_t k = 0; k + 1 < rows.size(); ++k) {
        CvrCoord r1 = rows[k], r2 = rows[k + 1];
        if (r2 != r1 + 1) continue;
        for (size_t i : bgRows[r1]) {
            for (size_t j : bgRows[r2]) {
                if (bg[i].cb <= bg[j].ce && bg[j].cb <= bg[i].ce)
                    uni((int)i, (int)j);
            }
        }
    }

    // 标记与边界连通 = 外部
    std::vector<bool> outside(bg.size(), false);
    for (size_t i = 0; i < bg.size(); ++i) {
        if (bg[i].r == 0 || bg[i].r == h - 1 ||
            bg[i].cb == 0 || bg[i].ce == w - 1)
        {
            outside[find((int)i)] = true;
        }
    }

    // 输出 = 前景 + 非外部背景
    out = mat;
    out.runs.reserve(mat.runs.size() + bg.size());
    for (size_t i = 0; i < bg.size(); ++i) {
        if (!outside[find((int)i)]) out.runs.push_back(bg[i]);
    }
    return cvr_region_normalize(out);
}

} // namespace cvr
