#include "cvr/cvr_conn.hpp"
#include <algorithm>
#include <numeric>
#include <unordered_map>

namespace cvr {

namespace {

int dsu_find(std::vector<int>& p, int x) {
    while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; }
    return x;
}

void dsu_union(std::vector<int>& p, int a, int b) {
    p[dsu_find(p, a)] = dsu_find(p, b);
}

} // namespace

bool cvr_connection(const CvrRegion& r, int connectivity,
                    CvrCoord w, CvrCoord h,
                    std::vector<CvrRegion>& out)
{
    cvr_clear_last_error();
    if (connectivity != 4 && connectivity != 8) {
        cvr_set_last_error("cvr_connection: connectivity must be 4 or 8");
        return false;
    }
    if (r.is_compl && (w <= 0 || h <= 0)) {
        cvr_set_last_error("cvr_connection: invalid domain size for complement region");
        return false;
    }

    out.clear();

    CvrRegion mat;
    if (r.is_compl) {
        if (!cvr_region_materialize(r, w, h, mat)) return false;
    } else {
        mat = r;
        if (!cvr_region_normalize(mat)) return false;
    }

    const auto& R = mat.runs;
    const size_t n = R.size();
    if (n == 0) return true;

    // 并查集
    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);

    const int expand = (connectivity == 8) ? 1 : 0;

    // runs 已按 (r, cb) 有序：顺序扫描按行分组，相邻行双指针 sweep 找交叠。
    // O(n + Σ 相邻行(nA+nB))，替代旧的 O(nA×nB) pairwise + 每行 unordered_map。
    size_t rowStart = 0;
    while (rowStart < n) {
        size_t rowEnd = rowStart;
        const CvrCoord r1 = R[rowStart].r;
        while (rowEnd < n && R[rowEnd].r == r1) ++rowEnd;

        // 找下一行（r1+1）的区间 [nxtStart, nxtEnd)
        size_t nxtStart = rowEnd;
        if (nxtStart < n && R[nxtStart].r == r1 + 1) {
            size_t nxtEnd = nxtStart;
            while (nxtEnd < n && R[nxtEnd].r == r1 + 1) ++nxtEnd;

            size_t i = rowStart, j = nxtStart;
            while (i < rowEnd && j < nxtEnd) {
                const CvrRun& a = R[i];
                const CvrRun& b = R[j];
                if (a.cb - expand <= b.ce && b.cb - expand <= a.ce) {
                    dsu_union(parent, (int)i, (int)j);
                    if (a.ce < b.ce) ++i; else ++j;
                } else if (a.ce < b.cb - expand) {
                    ++i;
                } else {
                    ++j;
                }
            }
        }
        rowStart = rowEnd;
    }

    // 按根分组：计数排序（根 ∈ [0,n)），避免 unordered_map 的每连通域堆分配。
    // 输出顺序按"各连通域最小 run 下标"升序，run 在组内天然有序。
    std::vector<int> rootOf(n);
    int maxRoot = 0;
    for (size_t i = 0; i < n; ++i) {
        rootOf[i] = dsu_find(parent, (int)i);
        if (rootOf[i] > maxRoot) maxRoot = rootOf[i];
    }

    std::vector<int> compOfRoot(maxRoot + 1, -1);
    int nComp = 0;
    std::vector<int> compOfRun(n);
    for (size_t i = 0; i < n; ++i) {
        int& slot = compOfRoot[rootOf[i]];
        if (slot < 0) slot = nComp++;
        compOfRun[i] = slot;
    }

    std::vector<size_t> compStart(nComp + 1, 0);
    for (size_t i = 0; i < n; ++i) compStart[compOfRun[i] + 1]++;
    for (int c = 0; c < nComp; ++c) compStart[c + 1] += compStart[c];

    std::vector<size_t> cursor(compStart.begin(), compStart.end() - 1);
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[cursor[compOfRun[i]]++] = i;

    out.reserve(nComp);
    for (int c = 0; c < nComp; ++c) {
        CvrRegion rr;
        rr.is_compl = false;
        rr.runs.reserve(compStart[c + 1] - compStart[c]);
        for (size_t k = compStart[c]; k < compStart[c + 1]; ++k)
            rr.runs.push_back(R[order[k]]);
        cvr_region_invalidate(rr);
        out.push_back(std::move(rr));
    }
    return true;
}

} // namespace cvr
