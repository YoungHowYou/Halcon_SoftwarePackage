/*=============================================================================
 * cv_region — cvr.cpp：全部实现（单文件约定）
 *=============================================================================
 * 说明：cv_region 现在是「1 个头 + 1 个实现」的静态库（cvr_core），本文件由原
 *       12 个 .hpp / 12 个 .cpp 机械合并而成，段落顺序与下表一致（原文件 → 段落）：
 *   src/model_expression.h             表达式模型（muparser）
 *   src/optimizer.h                    LM 全局优化（Eigen）
 *   src/cvr_region.cpp                 region 本体
 *   src/cvr_ops.cpp                    集合运算 / ROI
 *   src/cvr_conn.cpp                   连通域与孔洞
 *   src/cvr_morph.cpp                  形态学
 *   src/cvr_feat.cpp                   形状特征
 *   src/cvr_shape.cpp                  形状变换 / 生成
 *   src/cvr_select.cpp                 select_shape
 *   src/cvr_measure.cpp                1D 测量
 *   src/ransac_core.cpp                ransac 主循环
 *   src/ransac_interface.cpp           ransac C ABI 实现
 *   src/cvr_io.cpp                     OpenCV 桥实现
 *   src/cvr_c_api.cpp                  遗留 C ABI 实现
 *
 * 约定（务必遵守）：
 *   1. 新增算子/函数直接加进本文件对应段落，**非必要不要新建文件**；
 *   2. 不在这里写内部 include：内部依赖已折叠（model_expression.h / optimizer.h /
 *      ransac_core.h 的声明在 cvr.hpp）；
 *   3. 编译依赖：STL + Eigen + muparser（io 段额外需要 OpenCV，见 CVR_WITH_OPENCV）；
 *   4. 本 TU 较大，改一处会整文件重编（这是单文件的既定代价）。
 *===========================================================================*/

#include "cvr/cvr.hpp"

#include <cmath>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <map>
#include <stack>
#include <random>
#include <thread>
#include <atomic>
#include <mutex>
#include <exception>
#include <condition_variable>
#include <functional>
#include <queue>

/*===========================================================================
 * region 本体
 * （原 src/cvr_region.cpp）
 *=========================================================================*/
namespace cvr {

namespace {
thread_local std::string g_cvr_last_error;

// 每个线程一个复用的 scratch buffer（形态学 / 逐行扫描用）
thread_local std::vector<CvrRun> t_scratch;

// 当前线程是否已在并行区内（嵌套并行会死锁，故嵌套一律串行）
thread_local int t_parDepth = 0;

// 常驻线程池：每次并行调用都新建线程的开销约 0.5ms，对轻量负载（如 1000 个小
// region 的单特征选择，串行仅 0.2ms）反而整体变慢，故池化复用。
class CvrPool {
public:
    static CvrPool& get() {
        static CvrPool inst;
        return inst;
    }
    void submit(const std::function<void()>& job) {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_tasks.push(job);
            ++m_pending;
        }
        m_cv.notify_one();
    }
    void waitAll() {
        std::unique_lock<std::mutex> lk(m_mtx);
        m_done.wait(lk, [this] { return m_pending == 0; });
    }
    int executors() const { return (int)m_threads.size() + 1; }   // worker + 调用线程
private:
    CvrPool() {
        int hw = (int)std::thread::hardware_concurrency();
        if (hw <= 1) hw = 1;
        for (int i = 0; i < hw - 1; ++i)
            m_threads.emplace_back([this] { workerLoop(); });
    }
    ~CvrPool() {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_stop = true;
        }
        m_cv.notify_all();
        for (size_t i = 0; i < m_threads.size(); ++i)
            if (m_threads[i].joinable()) m_threads[i].join();
    }
    void workerLoop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(m_mtx);
                m_cv.wait(lk, [this] { return m_stop || !m_tasks.empty(); });
                if (m_tasks.empty()) {
                    if (m_stop) return;
                    continue;
                }
                job = std::move(m_tasks.front());
                m_tasks.pop();
            }
            ++t_parDepth;                    // worker 内视为"已在并行区"：嵌套调用走串行
            job();
            --t_parDepth;
            {
                std::lock_guard<std::mutex> lk(m_mtx);
                if (--m_pending == 0) m_done.notify_all();
            }
        }
    }
    std::vector<std::thread>          m_threads;
    std::queue<std::function<void()>> m_tasks;
    std::mutex                        m_mtx;
    std::condition_variable           m_cv, m_done;
    int                               m_pending = 0;
    bool                              m_stop = false;
};

// 并行 for：粒度 min_grain 以下、或已在并行区内（嵌套）时走串行。
// 语义：fn(i) 对 i ∈ [0, n) 各调用一次；fn 内异常汇聚到主线程重抛。
// 约定：fn 只写自己的本地/独享缓冲，禁止在 fn 内调 cvr_set_last_error。
template <class Fn>
void cvr_parallel_for(int n, int min_grain, Fn&& fn) {
    if (n <= 0) return;

    if (t_parDepth > 0) {          // 嵌套并行 → 串行（避免线程池自锁 + 过度订阅）
        for (int i = 0; i < n; ++i) fn(i);
        return;
    }

    CvrPool& pool = CvrPool::get();
    const int nThreads = std::min(pool.executors(), n);
    if (nThreads <= 1 || n < min_grain) {
        for (int i = 0; i < n; ++i) fn(i);
        return;
    }

    std::atomic<int> next{0};
    std::exception_ptr eptr = nullptr;
    std::mutex mtx;

    auto worker = [&]() {
        try {
            for (;;) {
                int i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= n) break;
                fn(i);
            }
        } catch (...) {
            std::lock_guard<std::mutex> lk(mtx);
            if (!eptr) eptr = std::current_exception();
        }
    };

    ++t_parDepth;                  // 调用线程也在并行区内
    for (int t = 0; t < nThreads - 1; ++t) pool.submit(worker);
    worker();
    pool.waitAll();
    --t_parDepth;
    if (eptr) std::rethrow_exception(eptr);
}
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

    // 已序且无需合并（无 cb>ce、无同行相接/重叠）时直接返回：多数算子输出
    // 天然满足 chord 三条件，省掉一次全表拷贝 + 堆分配。校验语义与原路径一致。
    bool needMerge = false;
    if (r.runs[0].cb > r.runs[0].ce) {
        cvr_set_last_error("cvr_region_normalize: invalid run with cb > ce");
        return false;
    }
    for (size_t i = 1; i < r.runs.size(); ++i) {
        const CvrRun& nxt = r.runs[i];
        if (nxt.cb > nxt.ce) {
            cvr_set_last_error("cvr_region_normalize: invalid run with cb > ce");
            return false;
        }
        if (nxt.r == r.runs[i - 1].r && nxt.cb <= r.runs[i - 1].ce + 1) {
            needMerge = true;
            break;
        }
    }
    if (!needMerge) {
        cvr_region_invalidate(r);
        return true;
    }

    std::vector<CvrRun> out;
    out.reserve(r.runs.size());

    CvrRun cur = r.runs[0];
    for (size_t i = 1; i < r.runs.size(); ++i) {
        const CvrRun& nxt = r.runs[i];
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

    // runs 已按 (r, cb) 有序：二分定位该行区间 O(log n)
    auto lo = std::lower_bound(r.runs.begin(), r.runs.end(), row,
        [](const CvrRun& run, CvrCoord v) { return run.r < v; });
    auto hi = std::upper_bound(r.runs.begin(), r.runs.end(), row,
        [](CvrCoord v, const CvrRun& run) { return v < run.r; });
    begin = (size_t)(lo - r.runs.begin());
    end   = (size_t)(hi - r.runs.begin());
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

/*===========================================================================
 * 集合运算 / ROI
 * （原 src/cvr_ops.cpp）
 *=========================================================================*/
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
// 非补集时零拷贝直接引用输入（out 指向 r 本身）；仅补集时写入 storage。
// 调用方在函数返回前不得修改 r（与原先"整份拷贝"的只读语义一致）。
static bool materialize_if_needed(const CvrRegion& r, CvrCoord w, CvrCoord h,
                                  CvrRegion& storage, const CvrRegion*& out)
{
    if (!r.is_compl) {
        out = &r;
        return true;
    }
    if (!cvr_region_materialize(r, w, h, storage)) return false;
    out = &storage;
    return true;
}

bool cvr_union1(const std::vector<CvrRegion>& regions,
                CvrCoord w, CvrCoord h, CvrRegion& out)
{
    cvr_clear_last_error();
    out.runs.clear();
    out.is_compl = false;

    if (regions.empty()) return true;

    // 补集物化并行（物化互不依赖）；非补集直接引用原 runs
    const int n = (int)regions.size();
    std::vector<char> isC((size_t)n, 0);
    std::vector<CvrRegion> mats((size_t)n);
    for (int i = 0; i < n; ++i) isC[(size_t)i] = regions[(size_t)i].is_compl ? 1 : 0;

    try {
        cvr_parallel_for(n, 2, [&](int i) {
            if (!isC[(size_t)i]) return;
            if (!cvr_region_materialize(regions[(size_t)i], w, h, mats[(size_t)i]))
                throw std::runtime_error("cvr_union1: materialize failed");
        });
    } catch (const std::exception& e) {
        cvr_set_last_error(e.what());
        return false;
    }

    size_t total = 0;
    for (int i = 0; i < n; ++i)
        total += isC[(size_t)i] ? mats[(size_t)i].runs.size()
                                : regions[(size_t)i].runs.size();
    out.runs.reserve(total);

    for (int i = 0; i < n; ++i) {
        const CvrRegion& src = isC[(size_t)i] ? mats[(size_t)i] : regions[(size_t)i];
        out.runs.insert(out.runs.end(), src.runs.begin(), src.runs.end());
    }
    return cvr_region_normalize(out);
}

bool cvr_union2(const CvrRegion& r1, const CvrRegion& r2,
                CvrCoord w, CvrCoord h, CvrRegion& out)
{
    cvr_clear_last_error();
    CvrRegion storA, storB;
    const CvrRegion *A, *B;
    if (!materialize_if_needed(r1, w, h, storA, A)) return false;
    if (!materialize_if_needed(r2, w, h, storB, B)) return false;

    // 两个输入均为 normalize 后的有序 run 链表：线性归并（O(n)），无需排序。
    // 同行相接/重叠的 run 在下方单次扫描内合并。
    out.runs.clear();
    out.is_compl = false;
    const auto& AR = A->runs;
    const auto& BR = B->runs;
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
    CvrRegion storA, storB;
    const CvrRegion *A, *B;
    if (!materialize_if_needed(r1, w, h, storA, A)) return false;
    if (!materialize_if_needed(r2, w, h, storB, B)) return false;

    out.runs.clear();
    out.is_compl = false;

    const auto& AR = A->runs;
    const auto& BR = B->runs;

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
    CvrRegion storA, storB;
    const CvrRegion *A, *B;
    if (!materialize_if_needed(r1, w, h, storA, A)) return false;
    if (!materialize_if_needed(r2, w, h, storB, B)) return false;

    out.runs.clear();
    out.is_compl = false;

    const auto& AR = A->runs;
    const auto& BR = B->runs;

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

/*===========================================================================
 * 连通域与孔洞
 * （原 src/cvr_conn.cpp）
 *=========================================================================*/
namespace cvr {

namespace {

// 并查集：负 size 编码（p[root] = -size），union by size + 路径减半。
// 树更浅 ⇒ 后面的 rootOf 顺序 find 明显更快；连通域划分与编码无关，输出不变。
int dsu_find(std::vector<int>& p, int x) {
    while (p[x] >= 0) {
        const int px = p[x];
        if (p[px] >= 0) p[x] = p[px];   // 减半（px 为根时不越过）
        x = p[x];
    }
    return x;
}

void dsu_union(std::vector<int>& p, int a, int b) {
    a = dsu_find(p, a);
    b = dsu_find(p, b);
    if (a == b) return;
    if (p[a] > p[b]) std::swap(a, b);   // p[a] 更负 = size 更大，a 作新根
    p[a] += p[b];
    p[b] = a;
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

    // 输入准备：非补集且已满足 chord 不变量时零拷贝直接引用（省一次全表拷贝 +
    // normalize）；否则走原路径（补集物化 / 拷贝后 normalize，语义不变）。
    CvrRegion mat;
    const std::vector<CvrRun>* pRuns = nullptr;
    if (r.is_compl) {
        if (!cvr_region_materialize(r, w, h, mat)) return false;
        pRuns = &mat.runs;
    } else if (cvr_region_check_invariants(r)) {
        pRuns = &r.runs;
    } else {
        mat = r;
        if (!cvr_region_normalize(mat)) return false;
        pRuns = &mat.runs;
    }

    const auto& R = *pRuns;
    const size_t n = R.size();
    if (n == 0) return true;

    // 并查集（负 size 编码：-1 = 单元素根的 size）
    std::vector<int> parent(n, -1);

    const int expand = (connectivity == 8) ? 1 : 0;

    // 相邻行（r, r+1）双指针 sweep 求交叠并 union。大图按行条带并行：
    // 条带边界取在整行首，每个条带在全局 parent 数组的独占区间上跑同一 sweep
    // （并查集只在条带内 union ⇒ 根不会越出区间，无共享写、无锁）；
    // 跨越条带的相邻行对由主线程在并行段结束后顺序补并。union 集合与串行版
    // 完全一致（仅树形不同），最终连通域划分位级相同。
    auto sweep_range = [&](size_t beg, size_t end) {
        size_t rowStart = beg;
        while (rowStart < end) {
            size_t rowEnd = rowStart;
            const CvrCoord r1 = R[rowStart].r;
            while (rowEnd < end && R[rowEnd].r == r1) ++rowEnd;

            // 找下一行（r1+1）在 [beg,end) 内的区间
            const size_t nxtStart = rowEnd;
            if (nxtStart < end && R[nxtStart].r == r1 + 1) {
                size_t nxtEnd = nxtStart;
                while (nxtEnd < end && R[nxtEnd].r == r1 + 1) ++nxtEnd;

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
    };

    // 条带切分：每条约 kChunkRuns 个 run，边界对齐到整行首。
    // 小 region（单条带）完全不触碰线程池，保持原串行行为。
    const size_t kChunkRuns = 4096;
    int hw = (int)std::thread::hardware_concurrency();
    if (hw <= 1) hw = 1;
    int nChunks = (int)std::min<size_t>((size_t)hw, n / kChunkRuns + 1);
    if (nChunks < 1) nChunks = 1;

    if (nChunks == 1) {
        sweep_range(0, n);
    } else {
        std::vector<size_t> splits((size_t)nChunks + 1);
        splits[0] = 0;
        splits[(size_t)nChunks] = n;
        for (int t = 1; t < nChunks; ++t) {
            size_t s = n * (size_t)t / (size_t)nChunks;
            if (s < splits[(size_t)t - 1]) s = splits[(size_t)t - 1];
            while (s < n && s > 0 && R[s].r == R[s - 1].r) ++s;  // 对齐行首
            splits[(size_t)t] = s;
        }

        try {
            cvr_parallel_for(nChunks, 2, [&](int t) {
                sweep_range(splits[(size_t)t], splits[(size_t)t + 1]);
            });
        } catch (const std::exception& e) {
            cvr_set_last_error(e.what());
            return false;
        }

        // 条带间边界：chunk t 最后一行 × chunk t+1 第一行（行号恰好差 1 才相交）
        for (int t = 0; t + 1 < nChunks; ++t) {
            const size_t begA = splits[(size_t)t], endA = splits[(size_t)t + 1];
            const size_t begB = endA,           endB = splits[(size_t)t + 2];
            if (begA >= endA || begB >= endB) continue;
            // chunk t 的最后一行组
            size_t a0 = endA;
            while (a0 > begA && R[a0 - 1].r == R[endA - 1].r) --a0;
            const size_t a1 = endA;
            // chunk t+1 的第一行组
            size_t b1 = begB;
            while (b1 < endB && R[b1].r == R[begB].r) ++b1;
            if (R[begB].r != R[a1 - 1].r + 1) continue;

            size_t i = a0, j = begB;
            while (i < a1 && j < b1) {
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
    }

    // 按根分组：计数排序（根 ∈ [0,n)），避免 unordered_map 的每连通域堆分配。
    // 输出顺序按"各连通域最小 run 下标"升序，run 在组内天然有序。
    // rootOf 用顺序带压缩 find：实测两种并行化都是负收益——无压缩并行遍历会被
    // 深链拖垮；CAS 路径压缩则因多线程同时压同一棵树产生缓存行竞争。α(n) 摊还
    // 下顺序版已是近线性，不值得并行。
    // rootOf 融合进分组一趟：find 直接用结果，免 972K×4B 的写出+读回（数 MB 流量）。
    // 根编号 ∈ [0,n)，compOfRoot/compCnt 直接按 n 定长。
    std::vector<int> compOfRoot(n, -1);
    int nComp = 0;
    std::vector<int> compOfRun(n);
    std::vector<size_t> compCnt(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const int root = dsu_find(parent, (int)i);
        int& slot = compOfRoot[(size_t)root];
        if (slot < 0) slot = nComp++;
        compOfRun[i] = slot;
        ++compCnt[(size_t)slot];
    }

    std::vector<size_t> compStart((size_t)nComp + 1, 0);
    for (int c = 0; c < nComp; ++c) compStart[(size_t)c + 1] = compCnt[(size_t)c];
    for (int c = 0; c < nComp; ++c) compStart[(size_t)c + 1] += compStart[(size_t)c];

    // 输出装配：先串行 scatter 出全局 order[]（按 run 序，O(n) 纯下标写，保序），
    // 再按组件并行装配——组件 c 独占 out[c]，逐 run 结果与纯串行版位级一致。
    // 实测逐组件 reserve+拷贝的堆分配是大图主要耗时（57K 组件 ~7ms），并行化必需。
    std::vector<size_t> cursor(compStart.begin(), compStart.end() - 1);
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[cursor[(size_t)compOfRun[i]]++] = i;

    out.resize((size_t)nComp);

    auto assemble = [&](int c) {
        // 直接写入 out[c]，免临时 CvrRegion 的构造/移动/析构——CvrFeature 约
        // 400B，156K 组件时临时对象方案产生 ~200MB 额外内存流量（实测主因）。
        // resize 的值初始化已把 flags/标量清零，语义与 invalidate 后一致。
        CvrRegion& dst = out[(size_t)c];
        dst.is_compl = false;
        dst.runs.reserve(compStart[(size_t)c + 1] - compStart[(size_t)c]);
        for (size_t k = compStart[(size_t)c]; k < compStart[(size_t)c + 1]; ++k)
            dst.runs.push_back(R[order[k]]);
    };
    // 按组件区间 batch 并行：每线程领一段连续组件，避免"每组件一次原子 fetch"
    // 的调度开销（156K 组件时该开销实测 ~8ms，超过装配本体）。
    const int bat = (int)std::min<size_t>((size_t)hw, (size_t)nComp / 256 + 1);
    if (bat > 1) {
        try {
            cvr_parallel_for(bat, 1, [&](int t) {
                const int c0 = (int)((size_t)t * (size_t)nComp / (size_t)bat);
                const int c1 = (int)((size_t)(t + 1) * (size_t)nComp / (size_t)bat);
                for (int c = c0; c < c1; ++c) assemble(c);
            });
        } catch (const std::exception& e) {
            cvr_set_last_error(e.what());
            return false;
        }
    } else {
        for (int c = 0; c < nComp; ++c) assemble(c);
    }
    return true;
}

} // namespace cvr

/*===========================================================================
 * 形态学
 * （原 src/cvr_morph.cpp）
 *=========================================================================*/
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

    for (int it = 0; it < iterations; ++it) {
        build_row_index(cur.runs, idx);
        if (idx.empty()) { out.runs.clear(); cvr_region_invalidate(out); return true; }
        const CvrCoord minR = idx.front().r, maxR = idx.back().r;
        const CvrCoord yLo = std::max<CvrCoord>(0, (CvrCoord)(minR - drMax));
        const CvrCoord yHi = std::min<CvrCoord>(h - 1, (CvrCoord)(maxR - drMin));

        CvrRegion next;
        next.is_compl = false;
        next.runs.reserve(cur.runs.size());

        // 行循环并行：每行只写自己的 perRow[k]，scratch 用 thread_local 复用；
        // 每行结果与串行版逐 run 相同（只读共享输入，无竞态）。
        // 注：yHi < yLo（region 落在定义域之外）时必须跳过——旧串行 for 天然不执行，
        //     而 (size_t)(yHi-yLo+1) 会是天文数字导致 vector 分配抛 length_error。
        const int nRows = (yHi < yLo) ? 0 : (int)(yHi - yLo + 1);
        std::vector<std::vector<CvrRun>> perRow((size_t)nRows);

        if (nRows > 0)
        try {
            cvr_parallel_for(nRows, 128, [&](int kk) {
                const CvrCoord y = yLo + (CvrCoord)kk;
                std::vector<CvrRun>& acc = perRow[(size_t)kk];
                std::vector<CvrRun>& scratch = t_scratch;   // thread_local，容量复用

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
                            CvrCoord cb = cur.runs[(size_t)ri.start + t].cb - sre.cb;
                            CvrCoord ce = cur.runs[(size_t)ri.start + t].ce - sre.ce;
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
                if (dead) acc.clear();
            });
        } catch (const std::exception& e) {
            cvr_set_last_error(e.what());
            return false;
        }

        // 串行合并（perRow 按 y 升序，与串行版同序）
        size_t totalRuns = 0;
        for (size_t i = 0; i < perRow.size(); ++i) totalRuns += perRow[i].size();
        next.runs.reserve(totalRuns);
        for (size_t i = 0; i < perRow.size(); ++i)
            next.runs.insert(next.runs.end(), perRow[i].begin(), perRow[i].end());

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

    for (int it = 0; it < iterations; ++it) {
        build_row_index(cur.runs, idx);
        if (idx.empty()) { out.runs.clear(); cvr_region_invalidate(out); return true; }
        const CvrCoord minR = idx.front().r, maxR = idx.back().r;
        const CvrCoord yLo = std::max<CvrCoord>(0, (CvrCoord)(minR + drMin));
        const CvrCoord yHi = std::min<CvrCoord>(h - 1, (CvrCoord)(maxR + drMax));

        CvrRegion next;
        next.is_compl = false;
        next.runs.reserve(cur.runs.size());

        // 行循环并行（同 erosion_core_rows）：每行独立写 perRow[k]；
        // yHi < yLo（region 在定义域之外）必须跳过，避免 perRow 巨量分配
        const int nRows = (yHi < yLo) ? 0 : (int)(yHi - yLo + 1);
        std::vector<std::vector<CvrRun>> perRow((size_t)nRows);

        if (nRows > 0)
        try {
            cvr_parallel_for(nRows, 128, [&](int kk) {
                const CvrCoord y = yLo + (CvrCoord)kk;
                std::vector<CvrRun>& acc = perRow[(size_t)kk];
                std::vector<CvrRun>& scratch = t_scratch;   // thread_local，容量复用

                bool first = true;
                for (const CvrRun& sre : seRows) {
                    int k = find_row_bin(idx, y - sre.r);
                    if (k < 0) continue; // 该 SE 行无输入，跳过（并对集无贡献）
                    const CvrRowInfo& ri = idx[k];
                    if (first) {
                        acc.reserve((size_t)ri.count);
                        for (int t = 0; t < ri.count; ++t) {
                            CvrCoord cb = cur.runs[(size_t)ri.start + t].cb + sre.cb;
                            CvrCoord ce = cur.runs[(size_t)ri.start + t].ce + sre.ce;
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
            });
        } catch (const std::exception& e) {
            cvr_set_last_error(e.what());
            return false;
        }

        size_t totalRuns = 0;
        for (size_t i = 0; i < perRow.size(); ++i) totalRuns += perRow[i].size();
        next.runs.reserve(totalRuns);
        for (size_t i = 0; i < perRow.size(); ++i)
            next.runs.insert(next.runs.end(), perRow[i].begin(), perRow[i].end());

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
    // 快速路径：SE（及其转置）每行连续时按行 O(n×SE行数) 计算，与 erosion1/
    // dilation1 同一套 _rows 核心，整数 run 运算、逐 run 结果与慢路径一致。
    std::vector<CvrRun> seRowsE, seRowsD;
    if (se_row_intervals(sec, seRowsE) && se_row_intervals(seR, seRowsD)) {
        if (!erosion_core_rows(r, seRowsE, 1, w, h, ero)) return false;
        return dilation_core_rows(ero, seRowsD, 1, w, h, out);
    }
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
    // 快速路径：closing = dilation(SE转置) + erosion(SE转置)，两步都用 seR，
    // 与下方慢路径的 SE 完全一致（位级等价）
    std::vector<CvrRun> seRows;
    if (se_row_intervals(seR, seRows)) {
        if (!dilation_core_rows(r, seRows, 1, w, h, dil)) return false;
        return erosion_core_rows(dil, seRows, 1, w, h, out);
    }
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

    // 1) 一次线性扫描生成背景 runs（mat.runs 已按 (r, cb) 有序）
    //    注：域外行（r < 0）的前景不参与背景扫描，必须先跳过——否则游标 k 会
    //    永远对不上 row，退化成"每行整行背景"，孔洞会被误判成外部。
    std::vector<CvrRun> bg;
    bg.reserve(mat.runs.size() + (size_t)h);
    if (h >= 128) {
        // 按行并行：每行独立算背景 gap（行内语义与串行一致），最后**按行序合并** ⇒ 位级等价。
        // 说明：串行版靠单调游标 k 扫 mat.runs（依赖 (r,cb) 有序）；这里用行视图二分定位，
        //       同样只依赖有序性，且负行/域外行天然被跳过，与串行一致。
        std::vector<std::vector<CvrRun> > bgRow((size_t)h);
        try {
            cvr_parallel_for((int)h, 128, [&](int rr) {
                const CvrCoord row = (CvrCoord)rr;
                size_t b = 0, e = 0;
                cvr_region_row_ranges(mat, row, b, e);
                CvrCoord cursor = 0;
                std::vector<CvrRun>& dst = bgRow[(size_t)rr];
                for (size_t t = b; t < e; ++t) {
                    const CvrRun& fr = mat.runs[t];
                    if (fr.cb > cursor) dst.push_back({row, cursor, fr.cb - 1});
                    if (fr.ce + 1 > cursor) cursor = fr.ce + 1;
                }
                if (cursor < w) dst.push_back({row, cursor, w - 1});
            });
        } catch (const std::exception& e) {
            cvr_set_last_error(e.what());
            return false;
        }
        for (int rr = 0; rr < (int)h; ++rr)
            for (size_t t = 0; t < bgRow[(size_t)rr].size(); ++t)
                bg.push_back(bgRow[(size_t)rr][t]);
    } else {
        size_t k = 0;
        const size_t m = mat.runs.size();
        while (k < m && mat.runs[k].r < 0) ++k;
        for (CvrCoord row = 0; row < h; ++row) {
            CvrCoord cursor = 0;
            while (k < m && mat.runs[k].r == row) {
                if (mat.runs[k].cb > cursor)
                    bg.push_back({row, cursor, mat.runs[k].cb - 1});
                if (mat.runs[k].ce + 1 > cursor)
                    cursor = mat.runs[k].ce + 1;
                ++k;
            }
            if (cursor < w) bg.push_back({row, cursor, w - 1});
        }
    }
    if (bg.empty()) {
        out = mat;
        return true;
    }

    // 2) 并查集
    std::vector<int> parent(bg.size());
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    auto uni = [&](int a, int b) { parent[find(a)] = find(b); };

    // 3) 相邻行双指针合并（bg 已按 (r, cb) 有序）
    size_t rowStart = 0;
    while (rowStart < bg.size()) {
        const CvrCoord r1 = bg[rowStart].r;
        size_t rowEnd = rowStart;
        while (rowEnd < bg.size() && bg[rowEnd].r == r1) ++rowEnd;

        size_t nxtStart = rowEnd;
        if (nxtStart < bg.size() && bg[nxtStart].r == r1 + 1) {
            size_t nxtEnd = nxtStart;
            while (nxtEnd < bg.size() && bg[nxtEnd].r == r1 + 1) ++nxtEnd;
            size_t i = rowStart, j = nxtStart;
            while (i < rowEnd && j < nxtEnd) {
                const auto& a = bg[i];
                const auto& b = bg[j];
                if (a.cb <= b.ce && b.cb <= a.ce) {
                    uni((int)i, (int)j);
                    if (a.ce < b.ce) ++i; else ++j;
                } else if (a.ce < b.cb) {
                    ++i;
                } else {
                    ++j;
                }
            }
        }
        rowStart = rowEnd;
    }

    // 4) 边界连通 = 外部
    std::vector<unsigned char> outside(bg.size(), 0);
    for (size_t i = 0; i < bg.size(); ++i) {
        if (bg[i].r == 0 || bg[i].r == h - 1 ||
            bg[i].cb == 0 || bg[i].ce == w - 1)
            outside[find((int)i)] = 1;
    }

    // 5) 输出 = 前景 + 非外部背景
    out = mat;
    out.runs.reserve(mat.runs.size() + bg.size());
    for (size_t i = 0; i < bg.size(); ++i)
        if (!outside[find((int)i)]) out.runs.push_back(bg[i]);

    return cvr_region_normalize(out);
}

} // namespace cvr

/*===========================================================================
 * 形状特征
 * （原 src/cvr_feat.cpp）
 *=========================================================================*/
namespace cvr {

// ----------------------------------------------------------------------------
// 内部工具
// ----------------------------------------------------------------------------

static double cross(double ax, double ay, double bx, double by) {
    return ax * by - ay * bx;
}

static double dist2(double r1, double c1, double r2, double c2) {
    double dr = r1 - r2, dc = c1 - c2;
    return dr * dr + dc * dc;
}

// 提取边界像素（8-连通边界，每个 run 的端点）。
// 端点天然按 (r, cb) 升序生成（runs 有序 + 行内 cb<ce）⇒ 免排序快路径：
// O(n) 验证有序后只做 O(n) 去重，替代原来的 O(n log n) sort（profile 实测
// 这是大 region 特征计算的头号固定开销）。
static void extract_boundary_points(const CvrRegion& r,
                                    std::vector<std::pair<double, double>>& pts)
{
    pts.clear();
    pts.reserve(r.runs.size() * 2);
    for (const auto& rr : r.runs) {
        pts.push_back({(double)rr.r, (double)rr.cb});
        pts.push_back({(double)rr.r, (double)rr.ce});
    }
    // 有序性 O(n) 检查（不满足才排序），随后 O(n) 去重
    if (!std::is_sorted(pts.begin(), pts.end()))
        std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
}

// 凸包（monotone chain，按列主序）。
// 大输入且坐标全为整数值（region 端点必为 int32 ⇒ double 精确表示）时走
// 64 位键 LSD 基数排序（O(n)，点唯一时结果序与比较排序完全一致 ⇒ 位级等价），
// 替代 O(n log n) 的 std::sort；非整数坐标或小输入回退比较排序。
static void convex_hull(std::vector<std::pair<double, double>>& pts,
                        std::vector<std::pair<double, double>>& hull)
{
    hull.clear();
    if (pts.size() <= 1) { hull = pts; return; }

    // 整数键：(col,row) 各 int32 映射到 uint32（偏移符号位）拼 64 位，col 高位
    auto to_key = [](const std::pair<double, double>& p) -> uint64_t {
        const uint32_t c = (uint32_t)(int32_t)p.second + 0x80000000u;
        const uint32_t r = (uint32_t)(int32_t)p.first  + 0x80000000u;
        return (uint64_t)c << 32 | (uint64_t)r;
    };
    bool integral = pts.size() >= 4096;
    if (integral) {
        for (const auto& p : pts) {
            if (p.first  != std::floor(p.first)  ||
                p.second != std::floor(p.second) ||
                p.first  < -2147483648.0 || p.first  > 2147483647.0 ||
                p.second < -2147483648.0 || p.second > 2147483647.0) {
                integral = false;
                break;
            }
        }
    }
    if (integral) {
        // LSD 基数排序：8 趟 8 位计数排序（稳定 ⇒ 与比较排序同序）
        const size_t n = pts.size();
        std::vector<std::pair<double, double>> tmp(n);
        std::vector<uint64_t> keys(n), tkeys(n);
        for (size_t i = 0; i < n; ++i) keys[i] = to_key(pts[i]);
        std::pair<double, double>* src = pts.data();
        std::pair<double, double>* dst = tmp.data();
        uint64_t* ksrc = keys.data();
        uint64_t* kdst = tkeys.data();
        size_t cnt[256];
        for (int shift = 0; shift < 64; shift += 8) {
            std::memset(cnt, 0, sizeof(cnt));
            for (size_t i = 0; i < n; ++i) ++cnt[(ksrc[i] >> shift) & 0xffu];
            size_t sum = 0;
            for (int b = 0; b < 256; ++b) { const size_t t = cnt[b]; cnt[b] = sum; sum += t; }
            for (size_t i = 0; i < n; ++i) {
                const size_t slot = cnt[(ksrc[i] >> shift) & 0xffu]++;
                dst[slot] = src[i];
                kdst[slot] = ksrc[i];
            }
            std::swap(src, dst);
            std::swap(ksrc, kdst);
        }
        if (src != pts.data())   // 奇数趟（8 趟为偶，正常不触发；保险）
            std::copy(src, src + n, pts.begin());
    } else {
        std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second < b.second;
            return a.first < b.first;
        });
    }

    std::vector<std::pair<double, double>> lower, upper;
    for (const auto& p : pts) {
        while (lower.size() >= 2) {
            auto& q = lower[lower.size() - 1];
            auto& r = lower[lower.size() - 2];
            double cr = cross(q.second - r.second, q.first - r.first,
                              p.second - q.second, p.first - q.first);
            if (cr <= 0) lower.pop_back();
            else break;
        }
        lower.push_back(p);
    }
    for (auto it = pts.rbegin(); it != pts.rend(); ++it) {
        const auto& p = *it;
        while (upper.size() >= 2) {
            auto& q = upper[upper.size() - 1];
            auto& r = upper[upper.size() - 2];
            double cr = cross(q.second - r.second, q.first - r.first,
                              p.second - q.second, p.first - q.first);
            if (cr <= 0) upper.pop_back();
            else break;
        }
        upper.push_back(p);
    }

    lower.pop_back();
    upper.pop_back();
    hull = lower;
    hull.insert(hull.end(), upper.begin(), upper.end());
}

// 多边形面积（Shoelace）
static double polygon_area(const std::vector<std::pair<double, double>>& poly)
{
    if (poly.size() < 3) return 0.0;
    double a = 0.0;
    for (size_t i = 0; i < poly.size(); ++i) {
        size_t j = (i + 1) % poly.size();
        a += poly[i].second * poly[j].first - poly[j].second * poly[i].first;
    }
    return std::abs(a) * 0.5;
}

// ----------------------------------------------------------------------------
static bool area_center_compute(const CvrRegion& r,
                                double& row, double& col, CvrChords& area);

// 缓存包装：命中直接返回缓存值，未命中计算并写回 r.feature
bool cvr_feature_area_center(const CvrRegion& r,
                             double& row, double& col, CvrChords& area)
{
    if (r.feature.flags.center_area) {
        cvr_clear_last_error();
        row  = r.feature.row;
        col  = r.feature.col;
        area = r.feature.area;
        return true;
    }
    if (!area_center_compute(r, row, col, area)) return false;
    r.feature.row  = row;
    r.feature.col  = col;
    r.feature.area = area;
    r.feature.flags.center_area = 1;
    return true;
}

// 内部计算（不写缓存）
static bool area_center_compute(const CvrRegion& r,
                                double& row, double& col, CvrChords& area)
{
    cvr_clear_last_error();
    if (r.runs.empty()) {
        cvr_set_last_error("cvr_feature_area_center: empty region");
        return false;
    }

    area = 0;
    double sum_r = 0.0, sum_c = 0.0;
    for (const auto& rr : r.runs) {
        CvrChords len = rr.ce - rr.cb + 1;
        area += len;
        sum_r += (double)rr.r * len;
        sum_c += 0.5 * (rr.cb + rr.ce) * len;
    }
    if (area == 0) {
        row = col = 0.0;
        return true;
    }
    row = sum_r / (double)area;
    col = sum_c / (double)area;
    return true;
}

// ----------------------------------------------------------------------------
namespace {

// Σ_{c=cb}^{ce} c^k 的闭式幂和（O(1) 每 run，替代逐像素累加）
inline double sum_c1(CvrCoord cb, CvrCoord ce) {
    return 0.5 * (double)(cb + ce) * (double)(ce - cb + 1);
}
inline double sum_c2(CvrCoord cb, CvrCoord ce) {
    auto f = [](double n) { return n * (n + 1.0) * (2.0 * n + 1.0) / 6.0; };
    return f((double)ce) - f((double)cb - 1.0);
}
inline double sum_c3(CvrCoord cb, CvrCoord ce) {
    auto f = [](double n) { double t = n * (n + 1.0) * 0.5; return t * t; };
    return f((double)ce) - f((double)cb - 1.0);
}

} // namespace

// ----------------------------------------------------------------------------
static bool moments_compute(const CvrRegion& r,
                            double& m11, double& m20, double& m02);

// 缓存包装
bool cvr_feature_moments(const CvrRegion& r,
                         double& m11, double& m20, double& m02)
{
    if (r.feature.flags.moments) {
        cvr_clear_last_error();
        m11 = r.feature.m11;
        m20 = r.feature.m20;
        m02 = r.feature.m02;
        return true;
    }
    if (!moments_compute(r, m11, m20, m02)) return false;
    r.feature.m11 = m11;
    r.feature.m20 = m20;
    r.feature.m02 = m02;
    r.feature.flags.moments = 1;
    return true;
}

// 内部计算（不写缓存）
static bool moments_compute(const CvrRegion& r,
                            double& m11, double& m20, double& m02)
{
    cvr_clear_last_error();
    if (r.runs.empty()) {
        cvr_set_last_error("cvr_feature_moments: empty region");
        return false;
    }

    double row, col;
    CvrChords area;
    if (!cvr_feature_area_center(r, row, col, area)) return false;

    m11 = m20 = m02 = 0.0;
    // 量级累加器：闭式幂和在 run 恰好居中于质心时会灾难性抵消，
    // 用它判定"该结果是否已小到抵消误差不可忽略"，必要时回退到逐像素精确累加。
    double a11 = 0.0, a20 = 0.0;
    for (const auto& rr : r.runs) {
        CvrChords len = rr.ce - rr.cb + 1;
        double dr = rr.r - row;

        const double sc1 = sum_c1(rr.cb, rr.ce);
        const double sc2 = sum_c2(rr.cb, rr.ce);
        const double sdc = sc1 - col * (double)len;
        m11 += dr * sdc;

        // sum_{c=cb}^{ce} (c - col)^2 = sum_c2 - 2*col*sum_c + len*col^2
        m20 += sc2 - 2.0 * col * sc1 + (double)len * col * col;

        m02 += dr * dr * (double)len;

        a11 += (std::fabs(sc1) + std::fabs(col) * (double)len) * std::fabs(dr);
        a20 += std::fabs(sc2) + 2.0 * std::fabs(col) * std::fabs(sc1)
                                 + (double)len * col * col;
    }

    // 抵消守卫：闭式路径的误差上界 ≈ 2.2e-16 * a；若它可能超过 1e-9*max(1,|值|)，
    // 就用与旧实现完全相同的逐像素算式重算（保证零偏差），常规数据不会走到这里。
    if (a11 > 1e6 * std::max(1.0, std::fabs(m11)) ||
        a20 > 1e6 * std::max(1.0, std::fabs(m20))) {
        m11 = m20 = 0.0;
        for (const auto& rr : r.runs) {
            const double len = (double)(rr.ce - rr.cb + 1);
            const double dr  = (double)rr.r - row;
            const double sc1 = sum_c1(rr.cb, rr.ce);          // 与旧实现同式
            double sc2 = 0.0;
            for (CvrCoord c = rr.cb; c <= rr.ce; ++c) sc2 += (double)c * c;
            m11 += dr * (sc1 - col * len);
            m20 += sc2 - 2.0 * col * sc1 + len * col * col;
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
static bool elliptic_axis_compute(const CvrRegion& r,
                                  double& ra, double& rb, double& phi);

// 缓存包装：ra/rb 存 feature.ra/rb，phi 存 feature.phi
bool cvr_feature_elliptic_axis(const CvrRegion& r,
                               double& ra, double& rb, double& phi)
{
    if (r.feature.flags.elliptic_axis && r.feature.flags.phi) {
        cvr_clear_last_error();
        ra  = r.feature.ra;
        rb  = r.feature.rb;
        phi = r.feature.phi;
        return true;
    }
    if (!elliptic_axis_compute(r, ra, rb, phi)) return false;
    r.feature.ra  = ra;
    r.feature.rb  = rb;
    r.feature.phi = phi;
    r.feature.flags.elliptic_axis = 1;
    r.feature.flags.phi           = 1;
    return true;
}

// 内部计算（不写缓存）
static bool elliptic_axis_compute(const CvrRegion& r,
                                  double& ra, double& rb, double& phi)
{
    cvr_clear_last_error();
    double m11, m20, m02;
    if (!cvr_feature_moments(r, m11, m20, m02)) return false;

    double row, col;
    CvrChords area;
    if (!cvr_feature_area_center(r, row, col, area) || area == 0) {
        ra = rb = phi = 0.0;
        return true;
    }

    double n20 = m20 / (double)area;
    double n02 = m02 / (double)area;
    double n11 = m11 / (double)area;

    // 协方差矩阵 [[n20, n11],[n11, n02]] 的特征值
    double trace = n20 + n02;
    double diff = n20 - n02;
    double det = diff * diff + 4.0 * n11 * n11;
    double sqrt_det = std::sqrt(det);
    double lambda1 = 0.5 * (trace + sqrt_det);
    double lambda2 = 0.5 * (trace - sqrt_det);

    // Halcon: ra = 2*sqrt(lambda_max), rb = 2*sqrt(lambda_min)
    ra = 2.0 * std::sqrt(std::max(0.0, lambda1));
    rb = 2.0 * std::sqrt(std::max(0.0, lambda2));

    // phi: 主轴方向
    phi = 0.5 * std::atan2(2.0 * n11, n20 - n02);
    return true;
}

// ----------------------------------------------------------------------------
static bool contlength_compute(const CvrRegion& r, double& contlength);

// 缓存包装
bool cvr_feature_contlength(const CvrRegion& r, double& contlength)
{
    if (r.feature.flags.contlength) {
        cvr_clear_last_error();
        contlength = r.feature.contlength;
        return true;
    }
    if (!contlength_compute(r, contlength)) return false;
    r.feature.contlength = contlength;
    r.feature.flags.contlength = 1;
    return true;
}

// 内部计算（不写缓存）
static bool contlength_compute(const CvrRegion& r, double& contlength)
{
    cvr_clear_last_error();
    contlength = 0.0;
    const auto& R = r.runs;
    const size_t n = R.size();
    if (n == 0) return true;
    const double SQ2M1 = std::sqrt(2.0) - 1.0;

    // r.runs 已按 (r, cb) 有序（调用方保证）：单遍扫描，去掉原先的 map 索引。
    size_t rowStart = 0;
    while (rowStart < n) {
        const CvrCoord row = R[rowStart].r;
        size_t rowEnd = rowStart;
        while (rowEnd < n && R[rowEnd].r == row) ++rowEnd;

        // 上一行 = 紧邻的、r == row-1 的一段
        size_t prevStart = rowStart, prevEnd = rowStart;
        if (rowStart > 0 && R[rowStart - 1].r == row - 1) {
            prevStart = rowStart;
            while (prevStart > 0 && R[prevStart - 1].r == row - 1) --prevStart;
            prevEnd = rowStart;
        }
        const bool hasPrev = (prevStart < prevEnd);

        // 当前行与上一行的 runs 均按 cb 有序：双指针 sweep 一趟算完每个当前 run
        // 与上一行的重叠像素数（原先对每个 run 全扫上一行，最坏 O(nA×nB)）。
        // 重叠计数是整数加法，与累加顺序无关，逐 run 结果位级等价。
        std::vector<int> over((size_t)(rowEnd - rowStart), 0);
        if (hasPrev) {
            size_t i = rowStart, p = prevStart;
            while (i < rowEnd && p < prevEnd) {
                const CvrRun& a = R[i];
                const CvrRun& b = R[p];
                const CvrCoord ovCb = std::max(a.cb, b.cb);
                const CvrCoord ovCe = std::min(a.ce, b.ce);
                if (ovCb <= ovCe) over[i - rowStart] += ovCe - ovCb + 1;
                if (a.ce < b.ce) ++i; else ++p;
            }
        }

        for (size_t k = rowStart; k < rowEnd; ++k) {
            const auto& rr = R[k];
            // 左侧边界和右侧边界各贡献 1
            contlength += 2.0;
            const int len = rr.ce - rr.cb + 1;
            if (!hasPrev) {
                // 上一行无前景：整段上边界都是垂直边
                contlength += (double)len;
                continue;
            }
            const int non_over = len - over[k - rowStart];
            if (non_over > 0) {
                // 非重叠部分与上一行边界形成对角/垂直边
                contlength += non_over * SQ2M1;
            }
        }
        rowStart = rowEnd;
    }
    return true;
}

// ----------------------------------------------------------------------------
static bool convexity_compute(const CvrRegion& r, double& convexity, bool& is_convex);

// 缓存包装：convexity 与 is_convex 同属一组
bool cvr_feature_convexity(const CvrRegion& r, double& convexity, bool& is_convex)
{
    if (r.feature.flags.convexity) {
        cvr_clear_last_error();
        convexity = r.feature.convexity;
        is_convex = r.feature.is_convex;
        return true;
    }
    if (!convexity_compute(r, convexity, is_convex)) return false;
    r.feature.convexity = convexity;
    r.feature.is_convex = is_convex;
    r.feature.flags.convexity = 1;
    r.feature.flags.is_convex = 1;
    return true;
}

// 内部计算（不写缓存）
static bool convexity_compute(const CvrRegion& r, double& convexity, bool& is_convex)
{
    cvr_clear_last_error();
    if (r.runs.empty()) {
        convexity = 0.0;
        is_convex = false;
        return true;
    }

    thread_local std::vector<std::pair<double, double>> pts, hull;
    extract_boundary_points(r, pts);
    convex_hull(pts, hull);

    double hull_a = polygon_area(hull);
    double row, col;
    CvrChords area;
    if (!cvr_feature_area_center(r, row, col, area)) return false;

    if (hull_a <= 0.0) {
        convexity = 1.0;
        is_convex = true;
        return true;
    }

    convexity = (double)area / hull_a;
    if (convexity > 1.0) convexity = 1.0;
    is_convex = convexity > 0.995;
    return true;
}

// ----------------------------------------------------------------------------
static bool rectangle1_compute(const CvrRegion& r,
                               CvrCoord& row1, CvrCoord& col1,
                               CvrCoord& row2, CvrCoord& col2);

// 缓存包装
bool cvr_feature_smallest_rectangle1(const CvrRegion& r,
                                     CvrCoord& row1, CvrCoord& col1,
                                     CvrCoord& row2, CvrCoord& col2)
{
    if (r.feature.flags.smallest_rectangle1) {
        cvr_clear_last_error();
        row1 = r.feature.row1;
        col1 = r.feature.col1;
        row2 = r.feature.row2;
        col2 = r.feature.col2;
        return true;
    }
    if (!rectangle1_compute(r, row1, col1, row2, col2)) return false;
    r.feature.row1 = row1;
    r.feature.col1 = col1;
    r.feature.row2 = row2;
    r.feature.col2 = col2;
    r.feature.flags.smallest_rectangle1 = 1;
    return true;
}

// 内部计算（不写缓存）
static bool rectangle1_compute(const CvrRegion& r,
                               CvrCoord& row1, CvrCoord& col1,
                               CvrCoord& row2, CvrCoord& col2)
{
    cvr_clear_last_error();
    if (r.runs.empty()) {
        cvr_set_last_error("cvr_feature_smallest_rectangle1: empty region");
        return false;
    }
    return cvr_region_bbox(r, row1, col1, row2, col2);
}

// ----------------------------------------------------------------------------
// 旋转卡壳求最小面积外接矩形
static bool rectangle2_compute(const CvrRegion& r,
                               double& row, double& col,
                               double& phi, double& length1, double& length2);

// 缓存包装
bool cvr_feature_smallest_rectangle2(const CvrRegion& r,
                                     double& row, double& col,
                                     double& phi, double& length1, double& length2)
{
    if (r.feature.flags.smallest_rectangle2) {
        cvr_clear_last_error();
        row     = r.feature.row_rect;
        col     = r.feature.col_rect;
        phi     = r.feature.phi_rect;
        length1 = r.feature.length1;
        length2 = r.feature.length2;
        return true;
    }
    if (!rectangle2_compute(r, row, col, phi, length1, length2)) return false;
    r.feature.row_rect = row;
    r.feature.col_rect = col;
    r.feature.phi_rect = phi;
    r.feature.length1  = length1;
    r.feature.length2  = length2;
    r.feature.flags.smallest_rectangle2 = 1;
    return true;
}

// 内部计算（不写缓存）
static bool rectangle2_compute(const CvrRegion& r,
                               double& row, double& col,
                               double& phi, double& length1, double& length2)
{
    cvr_clear_last_error();
    if (r.runs.empty()) {
        cvr_set_last_error("cvr_feature_smallest_rectangle2: empty region");
        return false;
    }

    // pts/hull 走 thread_local scratch（批量特征场景免每 region 2 次堆分配）
    thread_local std::vector<std::pair<double, double>> pts, hull;
    extract_boundary_points(r, pts);
    convex_hull(pts, hull);
    if (hull.size() == 1) {
        row = hull[0].first; col = hull[0].second;
        phi = 0.0; length1 = length2 = 0.0;
        return true;
    }
    if (hull.size() == 2) {
        row = 0.5 * (hull[0].first + hull[1].first);
        col = 0.5 * (hull[0].second + hull[1].second);
        phi = std::atan2(hull[1].first - hull[0].first,
                         hull[1].second - hull[0].second);
        length1 = std::sqrt(dist2(hull[0].first, hull[0].second,
                                  hull[1].first, hull[1].second)) * 0.5;
        length2 = 0.0;
        return true;
    }

    size_t n = hull.size();

    // 真旋转卡壳 O(n)：4 个对踵支持点指针随边同步旋转，替代旧的每条边全量投影
    // O(n²)。位级等价论证：支持点处的投影值与「该边全部点的投影极值」是同一批
    // 浮点表达式的同一批位模式（min/max 归约无舍入、与遍历顺序无关；对踵打平时
    // 指针停在前者，值相等），打平规则不变（严格 <、首个最小者胜）⇒ 输出与旧
    // 实现逐位一致。锯齿大凸包（上万顶点）时差距可达两个数量级。
    double min_area = 1e300;
    double best_phi = 0.0, best_l1 = 0.0, best_l2 = 0.0;
    double best_r = 0.0, best_c = 0.0;

    // 边方向单位向量（沿边）与朝外法向；退化边（len<1e-9）跳过（与旧实现一致）
    auto edge_dirs = [&](size_t i, double& ux, double& uy, double& nx, double& ny) -> bool {
        const size_t j = (i + 1) % n;
        const double dr = hull[j].first - hull[i].first;
        const double dc = hull[j].second - hull[i].second;
        const double len = std::sqrt(dr * dr + dc * dc);
        if (len < 1e-9) return false;
        ux = dc / len;  uy = dr / len;
        nx = -uy;       ny = ux;
        return true;
    };
    auto proj_u = [&](size_t k, double ux, double uy, double orr, double oc) {
        return (hull[k].second - oc) * ux + (hull[k].first - orr) * uy;
    };
    auto proj_n = [&](size_t k, double nx, double ny, double orr, double oc) {
        return (hull[k].second - oc) * nx + (hull[k].first - orr) * ny;
    };

    // 初始化：用边 0 方向暴力定位 4 个支持点（仅一次 O(n)）
    double u0x, u0y, n0x, n0y;
    if (!edge_dirs(0, u0x, u0y, n0x, n0y)) { u0x = 1.0; u0y = 0.0; n0x = 0.0; n0y = 1.0; }
    size_t pu_max = 0, pu_min = 0, pn_max = 0, pn_min = 0;
    {
        double bu = proj_u(0, u0x, u0y, hull[0].first, hull[0].second);
        double bl = bu;
        double bt = proj_n(0, n0x, n0y, hull[0].first, hull[0].second);
        double bb = bt;
        for (size_t k = 1; k < n; ++k) {
            const double u  = proj_u(k, u0x, u0y, hull[0].first, hull[0].second);
            const double nv = proj_n(k, n0x, n0y, hull[0].first, hull[0].second);
            if (u > bu)  { bu = u;  pu_max = k; }
            if (u < bl)  { bl = u;  pu_min = k; }
            if (nv > bt) { bt = nv; pn_max = k; }
            if (nv < bb) { bb = nv; pn_min = k; }
        }
    }

    for (size_t i = 0; i < n; ++i) {
        double ux, uy, nx, ny;
        if (!edge_dirs(i, ux, uy, nx, ny)) continue;
        const double orr = hull[i].first, oc = hull[i].second;

        // 推进 4 个对踵指针（比较用差值，与原点无关；取值统一相对 hull[i]）。
        // 回绕用条件自增代替 %n（size_t 除法在热循环里代价高）。
        auto uval = [&](size_t k) { return proj_u(k, ux, uy, orr, oc); };
        auto nval = [&](size_t k) { return proj_n(k, nx, ny, orr, oc); };
        auto adv = [&](size_t& p, auto val, auto better) {
            size_t q = p + 1; if (q == n) q = 0;
            while (better(val(q), val(p))) { p = q; ++q; if (q == n) q = 0; }
        };
        adv(pu_max, uval, [](double a, double b) { return a > b; });
        adv(pu_min, uval, [](double a, double b) { return a < b; });
        adv(pn_max, nval, [](double a, double b) { return a > b; });
        adv(pn_min, nval, [](double a, double b) { return a < b; });

        const double max_u = uval(pu_max), min_u = uval(pu_min);
        const double max_n = nval(pn_max), min_n = nval(pn_min);
        const double w = max_u - min_u;
        const double h = max_n - min_n;
        const double area = w * h;
        if (area < min_area) {
            min_area = area;
            // length1 取长边，因此 phi 必须取「长边所在轴」的角：
            //   w >= h → 主轴为 u 轴，phi = atan2(uy, ux)
            //   w <  h → 主轴为 n 轴（= u 轴逆时针 90°），phi = atan2(ny, nx)
            // （与旧实现同一规则，打平行为不变）
            best_phi = (w >= h) ? std::atan2(uy, ux) : std::atan2(ny, nx);
            best_l1 = 0.5 * std::max(w, h);
            best_l2 = 0.5 * std::min(w, h);
            const double cu = 0.5 * (min_u + max_u);
            const double cn = 0.5 * (min_n + max_n);
            best_c = oc + cu * ux + cn * nx;
            best_r = orr + cu * uy + cn * ny;
        }
    }

    row = best_r; col = best_c; phi = best_phi;
    length1 = best_l1; length2 = best_l2;
    return true;
}

// ----------------------------------------------------------------------------
// 最小外接圆：基于凸包，用 Welzl 的简化确定性版本
// ----------------------------------------------------------------------------
// 最小外接圆：精确算法（增量式 Welzl/Nayuki，O(n) 期望）
//   旧实现是"向最远点微移中心"的启发式，对非凸区域会发散（实测 area=1490 的
//   blob 半径 67.66 vs HALCON 47.07），故替换为精确实现。
//   点坐标用 (row, col) 存于 CvPt.r / CvPt.c
// ----------------------------------------------------------------------------
struct CvPt { double r, c; };

static bool pt_in_circle(const CvPt& p, double cr, double cc, double rad)
{
    const double dr = p.r - cr, dc = p.c - cc;
    return dr * dr + dc * dc <= rad * rad * (1.0 + 1e-12) + 1e-12;
}

static void circle_from_2(const CvPt& a, const CvPt& b,
                          double& cr, double& cc, double& rad)
{
    cr = 0.5 * (a.r + b.r);
    cc = 0.5 * (a.c + b.c);
    rad = 0.5 * std::sqrt(dist2(a.r, a.c, b.r, b.c));
}

// 三点外接圆；共线返回 false
static bool circle_from_3(const CvPt& a, const CvPt& b, const CvPt& c,
                          double& cr, double& cc, double& rad)
{
    const double bx = b.r - a.r, by = b.c - a.c;
    const double cx = c.r - a.r, cy = c.c - a.c;
    const double d = 2.0 * (bx * cy - by * cx);
    if (std::fabs(d) < 1e-12) return false;
    const double b2 = bx * bx + by * by;
    const double c2 = cx * cx + cy * cy;
    cr = a.r + (cy * b2 - by * c2) / d;
    cc = a.c + (bx * c2 - cx * b2) / d;
    rad = std::sqrt(dist2(cr, cc, a.r, a.c));
    return true;
}

static double cross3(const CvPt& a, const CvPt& b, const CvPt& p)
{
    return (b.r - a.r) * (p.c - a.c) - (b.c - a.c) * (p.r - a.r);
}

static void mc_two(const std::vector<CvPt>& pts, size_t n,
                   const CvPt& p, const CvPt& q,
                   double& cr, double& cc, double& rad);

static void mc_one(const std::vector<CvPt>& pts, size_t n, const CvPt& p,
                   double& cr, double& cc, double& rad)
{
    cr = p.r; cc = p.c; rad = 0.0;
    for (size_t i = 0; i < n; ++i) {
        if (pt_in_circle(pts[i], cr, cc, rad)) continue;
        if (rad <= 0.0) circle_from_2(p, pts[i], cr, cc, rad);
        else            mc_two(pts, i, p, pts[i], cr, cc, rad);
    }
}

static void mc_two(const std::vector<CvPt>& pts, size_t n,
                   const CvPt& p, const CvPt& q,
                   double& cr, double& cc, double& rad)
{
    circle_from_2(p, q, cr, cc, rad);
    bool hasLeft = false, hasRight = false;
    double lr = 0, lc = 0, rr = 0, rc = 0;
    double lcross = 0, rcross = 0;
    for (size_t i = 0; i < n; ++i) {
        if (pt_in_circle(pts[i], cr, cc, rad)) continue;
        double t1, t2, t3;
        if (!circle_from_3(p, q, pts[i], t1, t2, t3)) continue;
        const double side = cross3(p, q, pts[i]);
        const double cval = cross3(p, q, CvPt{t1, t2});
        if (side > 0.0) {
            if (!hasLeft || cval > lcross) { hasLeft = true; lr = t1; lc = t2; lcross = cval; }
        } else if (side < 0.0) {
            if (!hasRight || cval < rcross) { hasRight = true; rr = t1; rc = t2; rcross = cval; }
        }
    }
    if (!hasLeft && !hasRight)      return;                       // 直径圆已够
    else if (hasLeft && !hasRight)  { cr = lr; cc = lc; rad = std::sqrt(dist2(lr, lc, p.r, p.c)); }
    else if (!hasLeft && hasRight)  { cr = rr; cc = rc; rad = std::sqrt(dist2(rr, rc, p.r, p.c)); }
    else                            circle_from_2(CvPt{lr, lc}, CvPt{rr, rc}, cr, cc, rad);
}

static bool smallest_circle_impl(const std::vector<std::pair<double, double>>& pts,
                                 double& row, double& col, double& radius)
{
    if (pts.empty()) { row = col = radius = 0.0; return true; }
    if (pts.size() == 1) { row = pts[0].first; col = pts[0].second; radius = 0.0; return true; }

    // 先在凸包上求解（最小外接圆只由凸包顶点决定）。
    // hull/hp 用 thread_local scratch（region_features/select_shape 按 region 并行
    // 时每个线程一份，免每次 2 次堆分配；容量跨调用复用）。
    thread_local std::vector<std::pair<double, double>> hull;
    thread_local std::vector<CvPt> hp;
    hull.clear();
    hp.clear();
    convex_hull(const_cast<std::vector<std::pair<double, double>>&>(pts), hull);
    hp.reserve(hull.size());
    for (const auto& p : hull) hp.push_back({p.first, p.second});

    double cr = hp[0].r, cc = hp[0].c, rad = 0.0;
    for (size_t i = 0; i < hp.size(); ++i) {
        if (pt_in_circle(hp[i], cr, cc, rad)) continue;
        mc_one(hp, i + 1, hp[i], cr, cc, rad);
    }
    row = cr; col = cc; radius = rad;
    return true;
}

static bool circle_compute(const CvrRegion& r,
                           double& row, double& col, double& radius);

// 缓存包装
bool cvr_feature_smallest_circle(const CvrRegion& r,
                                 double& row, double& col, double& radius)
{
    if (r.feature.flags.smallest_circle) {
        cvr_clear_last_error();
        row    = r.feature.row_circle;
        col    = r.feature.col_circle;
        radius = r.feature.radius;
        return true;
    }
    if (!circle_compute(r, row, col, radius)) return false;
    r.feature.row_circle = row;
    r.feature.col_circle = col;
    r.feature.radius     = radius;
    r.feature.flags.smallest_circle = 1;
    return true;
}

// 内部计算（不写缓存）
static bool circle_compute(const CvrRegion& r,
                           double& row, double& col, double& radius)
{
    cvr_clear_last_error();
    if (r.runs.empty()) {
        cvr_set_last_error("cvr_feature_smallest_circle: empty region");
        return false;
    }
    thread_local std::vector<std::pair<double, double>> pts;
    extract_boundary_points(r, pts);
    if (!smallest_circle_impl(pts, row, col, radius)) return false;
    // HALCON smallest_circle 的半径含像素外扩 0.5（实测多形状均恰好差 0.5）
    radius += 0.5;
    return true;
}

// ----------------------------------------------------------------------------
static bool compactness_compute(const CvrRegion& r, double& compactness);

// 缓存包装（内部只用 contlength + center_area，二者均已缓存）
bool cvr_feature_compactness(const CvrRegion& r, double& compactness)
{
    if (r.feature.flags.compactness) {
        cvr_clear_last_error();
        compactness = r.feature.compactness;
        return true;
    }
    if (!compactness_compute(r, compactness)) return false;
    r.feature.compactness = compactness;
    r.feature.flags.compactness = 1;
    return true;
}

// 内部计算（不写缓存）
static bool compactness_compute(const CvrRegion& r, double& compactness)
{
    cvr_clear_last_error();
    double cl;
    CvrChords area;
    double row, col;
    if (!cvr_feature_contlength(r, cl)) return false;
    if (!cvr_feature_area_center(r, row, col, area)) return false;
    if (area == 0) { compactness = 0.0; return true; }
    compactness = (cl * cl) / (4.0 * CVR_PI * (double)area);
    return true;
}

static bool circularity_compute(const CvrRegion& r, double& circularity);

// 缓存包装（内部依赖 convexity + contlength + center_area，均已缓存）
bool cvr_feature_circularity(const CvrRegion& r, double& circularity)
{
    if (r.feature.flags.circularity) {
        cvr_clear_last_error();
        circularity = r.feature.circularity;
        return true;
    }
    if (!circularity_compute(r, circularity)) return false;
    r.feature.circularity = circularity;
    r.feature.flags.circularity = 1;
    return true;
}

// 内部计算（不写缓存）
static bool circularity_compute(const CvrRegion& r, double& circularity)
{
    cvr_clear_last_error();
    double conv;
    bool isconv;
    if (!cvr_feature_convexity(r, conv, isconv)) return false;
    double cl;
    if (!cvr_feature_contlength(r, cl)) return false;
    CvrChords area;
    double row, col;
    if (!cvr_feature_area_center(r, row, col, area)) return false;
    if (area == 0) { circularity = 0.0; return true; }
    // Halcon circularity = contlength^2 / (4*pi*area) 的倒数归一化形式
    double f = cl * cl / (4.0 * CVR_PI * (double)area);
    circularity = 1.0 / std::max(1.0, f);
    return true;
}

static bool rectangularity_compute(const CvrRegion& r, double& rectangularity);

// 缓存包装（内部依赖 convexity + center_area + rectangle2，均已缓存）
bool cvr_feature_rectangularity(const CvrRegion& r, double& rectangularity)
{
    if (r.feature.flags.rectangularity) {
        cvr_clear_last_error();
        rectangularity = r.feature.rectangularity;
        return true;
    }
    if (!rectangularity_compute(r, rectangularity)) return false;
    r.feature.rectangularity = rectangularity;
    r.feature.flags.rectangularity = 1;
    return true;
}

// 内部计算（不写缓存）
static bool rectangularity_compute(const CvrRegion& r, double& rectangularity)
{
    cvr_clear_last_error();
    double conv;
    bool isconv;
    if (!cvr_feature_convexity(r, conv, isconv)) return false;
    CvrChords area;
    double row, col;
    if (!cvr_feature_area_center(r, row, col, area)) return false;
    if (area == 0) { rectangularity = 0.0; return true; }

    double rrow, rcol, phi, l1, l2;
    if (!cvr_feature_smallest_rectangle2(r, rrow, rcol, phi, l1, l2)) return false;
    double rect_area = (2.0 * l1) * (2.0 * l2);
    if (rect_area <= 0.0) { rectangularity = 0.0; return true; }
    rectangularity = (double)area / rect_area;
    if (rectangularity > 1.0) rectangularity = 1.0;
    return true;
}

// ----------------------------------------------------------------------------
// 掩膜光栅 / 轮廓点 / 8-邻域连通标注（供 roundness 类、孔洞类、矩类特征使用）
//   口径与 HALCON 默认 set_system('neighborhood',8) 一致
// ----------------------------------------------------------------------------
struct FeatureMask {
    std::vector<unsigned char> m;   // 1 = 区域内
    CvrCoord r1, c1, r2, c2;
    int rows, cols;
    bool empty;
};

static bool build_mask(const CvrRegion& r, FeatureMask& fm)
{
    fm.empty = true;
    fm.rows = fm.cols = 0;
    CvrCoord r1, c1, r2, c2;
    if (!cvr_region_bbox(r, r1, c1, r2, c2)) return true;   // 空区域
    fm.r1 = r1; fm.c1 = c1; fm.r2 = r2; fm.c2 = c2;
    fm.rows = (int)(r2 - r1 + 1);
    fm.cols = (int)(c2 - c1 + 1);
    fm.m.assign((size_t)fm.rows * (size_t)fm.cols, 0);
    fm.empty = false;
    // 串行分桶（每行→该行 runs），再按行并行光栅化：每线程只写自己的行段
    std::vector<std::vector<CvrRun>> byRow((size_t)fm.rows);
    for (const auto& rr : r.runs) {
        const int ir = (int)(rr.r - r1);
        if (ir < 0 || ir >= fm.rows) continue;
        byRow[(size_t)ir].push_back(rr);
    }
    cvr_parallel_for(fm.rows, 64, [&](int ir) {
        uint8_t* line = fm.m.data() + (size_t)ir * (size_t)fm.cols;
        const std::vector<CvrRun>& rowRuns = byRow[(size_t)ir];
        for (size_t k = 0; k < rowRuns.size(); ++k) {
            int cb = (int)(rowRuns[k].cb - c1), ce = (int)(rowRuns[k].ce - c1);
            if (cb < 0) cb = 0;
            if (ce >= fm.cols) ce = fm.cols - 1;
            if (cb <= ce) std::memset(line + cb, 1, (size_t)(ce - cb + 1));
        }
    });
    return true;
}

// 8-邻域内边界像素（该区域像素至少有一个 8-邻域邻居不在区域内）
static void mask_contour_points(const FeatureMask& fm,
                                std::vector<std::pair<int, int>>& pts)
{
    pts.clear();
    if (fm.empty) return;
    // 按行并行，再按行升序合并 —— 与串行版的点序完全一致（行升序、列升序）
    std::vector<std::vector<std::pair<int, int>>> perRow((size_t)fm.rows);
    cvr_parallel_for(fm.rows, 64, [&](int ir) {
        std::vector<std::pair<int, int>>& v = perRow[(size_t)ir];
        for (int ic = 0; ic < fm.cols; ++ic) {
            if (!fm.m[(size_t)ir * (size_t)fm.cols + ic]) continue;
            bool border = false;
            for (int dr = -1; dr <= 1 && !border; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    if (!dr && !dc) continue;
                    const int nr = ir + dr, nc = ic + dc;
                    if (nr < 0 || nr >= fm.rows || nc < 0 || nc >= fm.cols ||
                        !fm.m[(size_t)nr * (size_t)fm.cols + nc]) { border = true; break; }
                }
            }
            if (border) v.push_back({ir + (int)fm.r1, ic + (int)fm.c1});
        }
    });

    size_t total = 0;
    for (size_t i = 0; i < perRow.size(); ++i) total += perRow[i].size();
    pts.reserve(total);
    for (size_t i = 0; i < perRow.size(); ++i)
        pts.insert(pts.end(), perRow[i].begin(), perRow[i].end());
}

// 连通标注：target=1 标前景，target=0 标背景（孔洞）
//   conn8 = true 用 8 邻域，false 用 4 邻域。
//   注意连通性对偶：前景按 set_system('neighborhood',8)（默认）为 8 邻域时，
//   背景/孔洞必须按 4 邻域统计，否则对角线"夹缝"会被误判成孔洞。
//   each_size 为各分量像素数；touches_border 标记分量是否触及光栅边界
static int mask_label(const FeatureMask& fm, int target, bool conn8,
                      std::vector<double>& each_size,
                      std::vector<unsigned char>* touches_border)
{
    each_size.clear();
    if (touches_border) touches_border->clear();
    if (fm.empty) return 0;

    const size_t n_total = (size_t)fm.rows * (size_t)fm.cols;
    std::vector<int> label(n_total, -1);
    std::vector<std::pair<int, int>> stack;
    int n = 0;
    for (int ir = 0; ir < fm.rows; ++ir) {
        for (int ic = 0; ic < fm.cols; ++ic) {
            const size_t idx = (size_t)ir * (size_t)fm.cols + ic;
            if ((int)fm.m[idx] != target || label[idx] != -1) continue;
            const int cur = n++;
            double size = 0.0;
            bool border = false;
            stack.clear();
            stack.push_back({ir, ic});
            label[idx] = cur;
            while (!stack.empty()) {
                const std::pair<int, int> p = stack.back();
                stack.pop_back();
                ++size;
                if (p.first == 0 || p.second == 0 ||
                    p.first == fm.rows - 1 || p.second == fm.cols - 1) border = true;
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        if (!dr && !dc) continue;
                        if (!conn8 && dr != 0 && dc != 0) continue;   // 4 邻域
                        const int nr = p.first + dr, nc = p.second + dc;
                        if (nr < 0 || nr >= fm.rows || nc < 0 || nc >= fm.cols) continue;
                        const size_t nidx = (size_t)nr * (size_t)fm.cols + nc;
                        if ((int)fm.m[nidx] != target || label[nidx] != -1) continue;
                        label[nidx] = cur;
                        stack.push_back({nr, nc});
                    }
                }
            }
            each_size.push_back(size);
            if (touches_border) touches_border->push_back(border ? 1 : 0);
        }
    }
    return n;
}

// 中心矩（HALCON 约定：M20 = Σ(row-row0)^2，M02 = Σ(col-col0)^2）
//   M11/M20/M02 二阶，M21/M12/M03/M30 三阶（MU = 未归一化）
static bool central_moments_halcon(const CvrRegion& r, double row, double col,
                                   double& m11, double& m20, double& m02,
                                   double& m21, double& m12, double& m03, double& m30)
{
    m11 = m20 = m02 = m21 = m12 = m03 = m30 = 0.0;
    double a11 = 0.0, a21 = 0.0, a02 = 0.0, a12 = 0.0, a03 = 0.0;   // 抵消量级
    for (const auto& rr : r.runs) {
        const double len = (double)(rr.ce - rr.cb + 1);
        const double dr = (double)rr.r - row;

        const double sc1 = sum_c1(rr.cb, rr.ce);
        const double sc2 = sum_c2(rr.cb, rr.ce);
        const double sc3 = sum_c3(rr.cb, rr.ce);

        const double s1 = sc1 - col * len;
        const double s2 = sc2 - 2.0 * col * sc1 + col * col * len;
        const double s3 = sc3 - 3.0 * col * sc2 + 3.0 * col * col * sc1
                               - col * col * col * len;

        m11 += dr * s1;
        m20 += dr * dr * len;
        m02 += s2;
        m30 += dr * dr * dr * len;
        m21 += dr * dr * s1;
        m12 += dr * s2;
        m03 += s3;

        const double q1 = std::fabs(sc1) + std::fabs(col) * len;
        const double q2 = std::fabs(sc2) + 2.0 * std::fabs(col) * std::fabs(sc1)
                          + col * col * len;
        const double q3 = std::fabs(sc3) + 3.0 * std::fabs(col) * std::fabs(sc2)
                          + 3.0 * col * col * std::fabs(sc1)
                          + std::fabs(col * col * col) * len;
        a11 += q1 * std::fabs(dr);
        a21 += q1 * dr * dr;
        a02 += q2;
        a12 += q2 * std::fabs(dr);
        a03 += q3;
    }

    // 抵消守卫：必要时用与旧实现同序的逐像素累加重算全部矩（保证零偏差）
    if (a11 > 1e6 * std::max(1.0, std::fabs(m11)) ||
        a21 > 1e6 * std::max(1.0, std::fabs(m21)) ||
        a02 > 1e6 * std::max(1.0, std::fabs(m02)) ||
        a12 > 1e6 * std::max(1.0, std::fabs(m12)) ||
        a03 > 1e6 * std::max(1.0, std::fabs(m03))) {
        m11 = m20 = m02 = m21 = m12 = m03 = m30 = 0.0;
        for (const auto& rr : r.runs) {
            const double len = (double)(rr.ce - rr.cb + 1);
            const double dr  = (double)rr.r - row;
            double s1 = 0.0, s2 = 0.0, s3 = 0.0;   // Σ(c-col), Σ(c-col)^2, Σ(c-col)^3
            for (CvrCoord c = rr.cb; c <= rr.ce; ++c) {
                const double dc = (double)c - col;
                s1 += dc;
                s2 += dc * dc;
                s3 += dc * dc * dc;
            }
            m11 += dr * s1;
            m20 += dr * dr * len;
            m02 += s2;
            m30 += dr * dr * dr * len;
            m21 += dr * dr * s1;
            m12 += dr * s2;
            m03 += s3;
        }
    }
    return true;
}

// 凸包点集上的最大距离（= 点集直径）
static double point_set_diameter(std::vector<std::pair<double, double>> pts)
{
    if (pts.empty()) return 0.0;
    std::vector<std::pair<double, double>> hull;
    convex_hull(pts, hull);
    double best = 0.0;
    for (size_t i = 0; i < hull.size(); ++i)
        for (size_t j = i + 1; j < hull.size(); ++j) {
            const double d = dist2(hull[i].first, hull[i].second,
                                   hull[j].first, hull[j].second);
            if (d > best) best = d;
        }
    return std::sqrt(best);
}

// ----------------------------------------------------------------------------
bool cvr_get_feature(const CvrRegion& r, const std::string& name, double& value)
{
    cvr_clear_last_error();

    double row, col, ra, rb, phi, cl, conv, comp, circ, rect;
    double rr, rc, rphi, rl1, rl2;
    double cr, cc, crad;
    CvrChords area;
    CvrCoord r1, c1, r2, c2;
    bool isconv;

    // 小写化
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);

    if (n == "area") {
        if (!cvr_feature_area_center(r, row, col, area)) return false;
        value = (double)area;
    } else if (n == "row") {
        if (!cvr_feature_area_center(r, row, col, area)) return false;
        value = row;
    } else if (n == "column" || n == "col") {
        if (!cvr_feature_area_center(r, row, col, area)) return false;
        value = col;
    } else if (n == "row1") {
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        value = (double)r1;
    } else if (n == "column1" || n == "col1") {
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        value = (double)c1;
    } else if (n == "row2") {
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        value = (double)r2;
    } else if (n == "column2" || n == "col2") {
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        value = (double)c2;
    } else if (n == "width") {
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        value = (double)(c2 - c1 + 1);
    } else if (n == "height") {
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        value = (double)(r2 - r1 + 1);
    } else if (n == "row_rect") {
        if (!cvr_feature_smallest_rectangle2(r, rr, rc, rphi, rl1, rl2)) return false;
        value = rr;
    } else if (n == "column_rect" || n == "col_rect") {
        if (!cvr_feature_smallest_rectangle2(r, rr, rc, rphi, rl1, rl2)) return false;
        value = rc;
    } else if (n == "phi_rect") {
        if (!cvr_feature_smallest_rectangle2(r, rr, rc, rphi, rl1, rl2)) return false;
        value = rphi;
    } else if (n == "length1") {
        if (!cvr_feature_smallest_rectangle2(r, rr, rc, rphi, rl1, rl2)) return false;
        value = rl1;
    } else if (n == "length2") {
        if (!cvr_feature_smallest_rectangle2(r, rr, rc, rphi, rl1, rl2)) return false;
        value = rl2;
    } else if (n == "row_circle") {
        if (!cvr_feature_smallest_circle(r, cr, cc, crad)) return false;
        value = cr;
    } else if (n == "column_circle" || n == "col_circle") {
        if (!cvr_feature_smallest_circle(r, cr, cc, crad)) return false;
        value = cc;
    } else if (n == "radius") {
        if (!cvr_feature_smallest_circle(r, cr, cc, crad)) return false;
        value = crad;
    } else if (n == "phi") {
        if (!cvr_feature_elliptic_axis(r, ra, rb, phi)) return false;
        value = phi;
    } else if (n == "ra") {
        if (!cvr_feature_elliptic_axis(r, ra, rb, phi)) return false;
        value = ra;
    } else if (n == "rb") {
        if (!cvr_feature_elliptic_axis(r, ra, rb, phi)) return false;
        value = rb;
    } else if (n == "contlength") {
        if (!cvr_feature_contlength(r, cl)) return false;
        value = cl;
    } else if (n == "convexity") {
        if (!cvr_feature_convexity(r, conv, isconv)) return false;
        value = conv;
    } else if (n == "compactness") {
        if (!cvr_feature_compactness(r, comp)) return false;
        value = comp;
    } else if (n == "circularity") {
        if (!cvr_feature_circularity(r, circ)) return false;
        value = circ;
    } else if (n == "rectangularity") {
        if (!cvr_feature_rectangularity(r, rect)) return false;
        value = rect;
    } else if (n == "anisometry") {
        if (!cvr_feature_elliptic_axis(r, ra, rb, phi)) return false;
        value = (rb < 1e-9) ? 0.0 : ra / rb;
    } else if (n == "bulkiness") {
        double conv2;
        if (!cvr_feature_convexity(r, conv2, isconv)) return false;
        value = 1.0 / std::max(conv2, 1e-9);
    } else if (n == "structure_factor" || n == "struct_factor") {
        // struct_factor 是 HALCON region_features 的正式名；structure_factor 为历史别名
        if (!cvr_feature_elliptic_axis(r, ra, rb, phi)) return false;
        value = (ra * ra) / std::max(rb * rb, 1e-9);
    }

    // ================= HALCON region_features 对齐补充（新） =================

    // --- 轴向矩形比 / 最小外接旋转矩形别名（HALCON 名） ---
    else if (n == "ratio") {
        // HALCON ratio = height / width，且 height/width 取像素数
        // （同 region_features 的 'height'/'width'，即 Row2-Row1+1 / Column2-Column1+1）
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        const double w = (double)(c2 - c1 + 1);
        value = (w > 0.0) ? (double)(r2 - r1 + 1) / w : 0.0;
    } else if (n == "rect2_phi") {
        if (!cvr_feature_smallest_rectangle2(r, rr, rc, rphi, rl1, rl2)) return false;
        value = rphi;
    } else if (n == "rect2_len1") {
        if (!cvr_feature_smallest_rectangle2(r, rr, rc, rphi, rl1, rl2)) return false;
        value = rl1;
    } else if (n == "rect2_len2") {
        if (!cvr_feature_smallest_rectangle2(r, rr, rc, rphi, rl1, rl2)) return false;
        value = rl2;
    } else if (n == "outer_radius") {
        // 最小外接圆半径（HALCON smallest_circle）
        if (!cvr_feature_smallest_circle(r, cr, cc, crad)) return false;
        value = crad;
    }

    // --- 轮廓距离类（HALCON roundness 算子）---
    //   Distance = Σ||p - p_i|| / F，Sigma^2 = Σ(||p - p_i|| - Distance)^2 / F
    //   Roundness = 1 - Sigma/Distance，Sides = 1.4111*(Distance/Sigma)^0.4724
    //   p = 面积重心，p_i 遍历轮廓像素，F = 轮廓像素数（8 邻域）
    else if (n == "dist_mean" || n == "dist_deviation" ||
             n == "roundness" || n == "num_sides") {
        if (!cvr_feature_area_center(r, row, col, area)) return false;
        FeatureMask fm;
        if (!build_mask(r, fm)) return false;
        std::vector<std::pair<int, int>> cp;
        mask_contour_points(fm, cp);
        if (cp.empty()) { value = 0.0; return true; }
        const double F = (double)cp.size();
        double dist = 0.0;
        std::vector<double> dv(cp.size());
        for (size_t i = 0; i < cp.size(); ++i) {
            dv[i] = std::sqrt(dist2((double)cp[i].first, (double)cp[i].second, row, col));
            dist += dv[i];
        }
        dist /= F;
        double var = 0.0;
        for (size_t i = 0; i < cp.size(); ++i) {
            const double d = dv[i] - dist;
            var += d * d;
        }
        var /= F;
        const double sigma = std::sqrt(var);
        if (n == "dist_mean")            value = dist;
        else if (n == "dist_deviation")   value = sigma;
        else if (n == "roundness")        value = (dist > 0.0) ? 1.0 - sigma / dist : 0.0;
        else                              value = (sigma > 0.0)
                                               ? 1.4111 * std::pow(dist / sigma, 0.4724)
                                               : 0.0;
    }

    // --- 最大直径（HALCON diameter_region：轮廓点间最大距离）---
    else if (n == "max_diameter") {
        FeatureMask fm;
        if (!build_mask(r, fm)) return false;
        std::vector<std::pair<int, int>> cp;
        mask_contour_points(fm, cp);
        std::vector<std::pair<double, double>> pts;
        pts.reserve(cp.size());
        for (const auto& p : cp) pts.push_back({(double)p.first, (double)p.second});
        value = point_set_diameter(pts);
    }

    // --- 连通性 / 孔洞（HALCON connect_and_holes / area_holes / euler_number）---
    else if (n == "connect_num" || n == "holes_num" ||
             n == "area_holes" || n == "euler_number") {
        FeatureMask fm;
        if (!build_mask(r, fm)) return false;
        std::vector<double> sizes;
        std::vector<unsigned char> bg_border;
        const int nconn = mask_label(fm, 1, true, sizes, nullptr);      // 前景 8 邻域
        const int nbg   = mask_label(fm, 0, false, sizes, &bg_border);  // 背景 4 邻域（连通性对偶）
        int holes = 0;
        double holes_area = 0.0;
        for (int i = 0; i < nbg; ++i) {
            if (i < (int)bg_border.size() && !bg_border[(size_t)i]) {
                ++holes;                                  // 不触边界 = 孔洞
                holes_area += sizes[(size_t)i];
            }
        }
        if (n == "connect_num")      value = (double)nconn;
        else if (n == "holes_num")   value = (double)holes;
        else if (n == "area_holes")  value = holes_area;
        else                         value = (double)(nconn - holes);
    }

    // --- 朝向（HALCON orientation_region：基于 elliptic_axis + 最远轮廓点）---
    else if (n == "orientation") {
        if (!cvr_feature_area_center(r, row, col, area)) return false;
        if (!cvr_feature_elliptic_axis(r, ra, rb, phi)) return false;
        FeatureMask fm;
        if (!build_mask(r, fm)) return false;
        std::vector<std::pair<int, int>> cp;
        mask_contour_points(fm, cp);
        double best = -1.0, bpr = row, bpc = col;
        for (const auto& p : cp) {
            const double d = dist2((double)p.first, (double)p.second, row, col);
            if (d > best) { best = d; bpr = (double)p.first; bpc = (double)p.second; }
        }
        // 旋转坐标系下的"列"分量 < 0 时加 pi（HALCON 语义）
        const double xp = (bpc - col) * std::cos(phi) + (bpr - row) * std::sin(phi);
        value = (xp < 0.0) ? phi + CVR_PI : phi;
    }

    // --- 中心矩族（HALCON moments_region_2nd / _2nd_invar / _2nd_rel_invar /
    //     _3rd / _3rd_invar）---
    else if (n.rfind("moments_", 0) == 0) {
        if (!cvr_feature_area_center(r, row, col, area)) return false;
        double m11, m20, m02, m21, m12, m03, m30;
        if (!central_moments_halcon(r, row, col, m11, m20, m02, m21, m12, m03, m30))
            return false;
        const double F = (double)area;
        const double F2 = F * F, F3 = F2 * F;
        const double m11i = (F2 > 0.0) ? m11 / F2 : 0.0;
        const double m20i = (F2 > 0.0) ? m20 / F2 : 0.0;
        const double m02i = (F2 > 0.0) ? m02 / F2 : 0.0;
        const double m21i = (F3 > 0.0) ? m21 / F3 : 0.0;
        const double m12i = (F3 > 0.0) ? m12 / F3 : 0.0;
        const double m03i = (F3 > 0.0) ? m03 / F3 : 0.0;
        const double m30i = (F3 > 0.0) ? m30 / F3 : 0.0;

        if (n == "moments_m11")             value = m11;
        else if (n == "moments_m20")        value = m20;
        else if (n == "moments_m02")        value = m02;
        else if (n == "moments_ia" || n == "moments_ib") {
            // 二阶中心矩矩阵 [[M20, M11], [M11, M02]] 的特征值
            const double t = 0.5 * (m20 + m02);
            const double dv = std::sqrt(0.25 * (m20 - m02) * (m20 - m02) + m11 * m11);
            value = (n == "moments_ia") ? t + dv : t - dv;
        }
        else if (n == "moments_m11_invar")  value = m11i;
        else if (n == "moments_m20_invar")  value = m20i;
        else if (n == "moments_m02_invar")  value = m02i;
        else if (n == "moments_phi1")       value = m20i + m02i;
        else if (n == "moments_phi2")       value = (m20i - m02i) * (m20i - m02i)
                                                  + 4.0 * m11i * m11i;
        else if (n == "moments_m21")        value = m21;
        else if (n == "moments_m12")        value = m12;
        else if (n == "moments_m03")        value = m03;
        else if (n == "moments_m30")        value = m30;
        else if (n == "moments_m21_invar")  value = m21i;
        else if (n == "moments_m12_invar")  value = m12i;
        else if (n == "moments_m03_invar")  value = m03i;
        else if (n == "moments_m30_invar")  value = m30i;
        else {
            cvr_set_last_error("cvr_get_feature: unknown feature name: " + name);
            return false;
        }
    } else {
        cvr_set_last_error("cvr_get_feature: unknown feature name: " + name);
        return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
bool cvr_region_features(const std::vector<CvrRegion>& regions,
                         const std::vector<std::string>& names,
                         std::vector<double>& values)
{
    cvr_clear_last_error();
    values.clear();
    if (regions.empty() || names.empty()) return true;

    // 并行按 region 求值，写入各自的行缓冲；顺序与串行版一致（行主序）
    const int nR = (int)regions.size();
    const int nF = (int)names.size();
    std::vector<double> buf((size_t)nR * (size_t)nF, 0.0);

    try {
        cvr_parallel_for(nR, 4, [&](int idx) {
            const CvrRegion& r = regions[(size_t)idx];
            double* row = buf.data() + (size_t)idx * (size_t)nF;
            for (int j = 0; j < nF; ++j) {
                if (!cvr_get_feature(r, names[(size_t)j], row[j]))
                    throw std::runtime_error("cvr_region_features: unknown feature: "
                                             + names[(size_t)j]);
            }
        });
    } catch (const std::exception& e) {
        cvr_set_last_error(e.what());
        return false;
    }

    values = std::move(buf);
    return true;
}

// ----------------------------------------------------------------------------
// 特征缓存掩码：名字 -> 位（与 cvr_get_feature / cv_region_features 同一套名字口径）
//   组合名额外提供：none / all / basic（O(runs) 级）/ cheap（=basic）/ hull（凸包族）
// ----------------------------------------------------------------------------
namespace {

struct FeatureMaskEntry { const char* name; uint32_t bits; };

const FeatureMaskEntry kFeatureMaskNames[] = {
    // 组合名
    { "none",  CVR_FC_NONE  },
    { "all",   CVR_FC_ALL   },
    { "basic", CVR_FC_BASIC },
    { "cheap", CVR_FC_BASIC },
    { "hull",  CVR_FC_HULL  },
    // center_area
    { "center_area", CVR_FC_CENTER_AREA },
    { "area",        CVR_FC_CENTER_AREA },
    { "center",      CVR_FC_CENTER_AREA },
    { "row",         CVR_FC_CENTER_AREA },
    { "column",      CVR_FC_CENTER_AREA },
    { "col",         CVR_FC_CENTER_AREA },
    // moments
    { "moments", CVR_FC_MOMENTS },
    // 椭圆等效轴
    { "elliptic_axis", CVR_FC_ELLIPTIC_AXIS },
    { "ra",            CVR_FC_ELLIPTIC_AXIS },
    { "rb",            CVR_FC_ELLIPTIC_AXIS },
    { "phi",           CVR_FC_ELLIPTIC_AXIS },
    // 离心率族
    { "excentricity",     CVR_FC_EXCENTRICITY },
    { "anisometry",       CVR_FC_EXCENTRICITY },
    { "bulkiness",        CVR_FC_EXCENTRICITY },
    { "structure_factor", CVR_FC_EXCENTRICITY },
    { "struct_factor",    CVR_FC_EXCENTRICITY },
    // 标量特征
    { "contlength",     CVR_FC_CONT_LENGTH },
    { "convexity",      CVR_FC_CONVEXITY },
    { "circularity",    CVR_FC_CIRCULARITY },
    { "compactness",    CVR_FC_COMPACTNESS },
    { "rectangularity", CVR_FC_RECTANGULARITY },
    // 轴对齐包围盒（rectangle1）
    { "rectangle1",          CVR_FC_RECTANGLE1 },
    { "bbox",                CVR_FC_RECTANGLE1 },
    { "smallest_rectangle1", CVR_FC_RECTANGLE1 },
    { "width",               CVR_FC_RECTANGLE1 },
    { "height",              CVR_FC_RECTANGLE1 },
    { "ratio",               CVR_FC_RECTANGLE1 },
    { "row1",                CVR_FC_RECTANGLE1 },
    { "column1",             CVR_FC_RECTANGLE1 },
    { "col1",                CVR_FC_RECTANGLE1 },
    { "row2",                CVR_FC_RECTANGLE1 },
    { "column2",             CVR_FC_RECTANGLE1 },
    { "col2",                CVR_FC_RECTANGLE1 },
    // 最小外接旋转矩形（rectangle2）
    { "rectangle2",          CVR_FC_RECTANGLE2 },
    { "rect2",               CVR_FC_RECTANGLE2 },
    { "smallest_rectangle2", CVR_FC_RECTANGLE2 },
    { "row_rect",            CVR_FC_RECTANGLE2 },
    { "column_rect",         CVR_FC_RECTANGLE2 },
    { "col_rect",            CVR_FC_RECTANGLE2 },
    { "phi_rect",            CVR_FC_RECTANGLE2 },
    { "length1",             CVR_FC_RECTANGLE2 },
    { "length2",             CVR_FC_RECTANGLE2 },
    // 最小外接圆
    { "circle",          CVR_FC_CIRCLE },
    { "smallest_circle", CVR_FC_CIRCLE },
    { "row_circle",      CVR_FC_CIRCLE },
    { "column_circle",   CVR_FC_CIRCLE },
    { "col_circle",      CVR_FC_CIRCLE },
    { "radius",          CVR_FC_CIRCLE },
};

const size_t kFeatureMaskNamesN =
    sizeof(kFeatureMaskNames) / sizeof(kFeatureMaskNames[0]);

} // namespace

bool cvr_feature_mask_name(const std::string& name, uint32_t& bits)
{
    if (name.empty()) return false;
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    const size_t b = n.find_first_not_of(" \t");
    const size_t e = n.find_last_not_of(" \t");
    n = (b == std::string::npos) ? std::string() : n.substr(b, e - b + 1);
    for (size_t i = 0; i < kFeatureMaskNamesN; ++i) {
        if (n == kFeatureMaskNames[i].name) {
            bits = kFeatureMaskNames[i].bits;
            return true;
        }
    }
    return false;
}

bool cvr_feature_mask_parse(const std::vector<std::string>& names, uint32_t& mask)
{
    cvr_clear_last_error();
    mask = 0u;
    for (const auto& raw : names) {
        /* 单个元素内允许 "a|b" 拼接写法 */
        size_t start = 0;
        for (;;) {
            const size_t sep = raw.find('|', start);
            const std::string tok = (sep == std::string::npos)
                                        ? raw.substr(start)
                                        : raw.substr(start, sep - start);
            uint32_t bits = 0u;
            if (!cvr_feature_mask_name(tok, bits)) {
                cvr_set_last_error("cvr_feature_mask_parse: unknown feature name: " + tok);
                mask = 0u;
                return false;
            }
            mask |= bits;
            if (sep == std::string::npos) break;
            start = sep + 1;
        }
    }
    return true;
}

uint32_t cvr_region_precompute_features(const CvrRegion& r, uint32_t mask)
{
    if (mask == 0u || r.runs.empty()) return 0u;
    const uint32_t before = r.feature.flags.raw();

    /* 每个分支内部已做缓存判定：已算过的组是 O(1) 直接返回 */
    if (mask & CVR_FC_CENTER_AREA) {
        double row, col;
        CvrChords area;
        cvr_feature_area_center(r, row, col, area);
    }
    if (mask & CVR_FC_RECTANGLE1) {
        CvrCoord r1, c1, r2, c2;
        cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2);
    }
    if (mask & CVR_FC_MOMENTS) {
        double a, b, c;
        cvr_feature_moments(r, a, b, c);
    }
    if (mask & CVR_FC_ELLIPTIC_AXIS) {
        double ra, rb, phi;
        cvr_feature_elliptic_axis(r, ra, rb, phi);
    }
    if (mask & CVR_FC_CONT_LENGTH) {
        double cl;
        cvr_feature_contlength(r, cl);
    }
    if (mask & CVR_FC_CONVEXITY) {
        double cv;
        bool iscv;
        cvr_feature_convexity(r, cv, iscv);
    }
    if (mask & CVR_FC_CIRCULARITY) {
        double v;
        cvr_feature_circularity(r, v);
    }
    if (mask & CVR_FC_COMPACTNESS) {
        double v;
        cvr_feature_compactness(r, v);
    }
    if (mask & CVR_FC_RECTANGULARITY) {
        double v;
        cvr_feature_rectangularity(r, v);
    }
    if (mask & CVR_FC_RECTANGLE2) {
        double a, b, c, d, e;
        cvr_feature_smallest_rectangle2(r, a, b, c, d, e);
    }
    if (mask & CVR_FC_CIRCLE) {
        double a, b, c;
        cvr_feature_smallest_circle(r, a, b, c);
    }
    if (mask & CVR_FC_EXCENTRICITY) {
        /* 与 cvr_get_feature 的 anisometry / bulkiness / structure_factor 同式 */
        double ra = 0.0, rb = 0.0, phi = 0.0;
        if (cvr_feature_elliptic_axis(r, ra, rb, phi)) {
            r.feature.anisometry       = (rb < 1e-9) ? 0.0 : ra / rb;
            r.feature.structure_factor = (ra * ra) / std::max(rb * rb, 1e-9);
        }
        double conv = 0.0;
        bool   isconv = false;
        if (cvr_feature_convexity(r, conv, isconv)) {
            r.feature.bulkiness = 1.0 / std::max(conv, 1e-9);
        }
        r.feature.flags.excentricity = 1;
    }

    /* 计算失败的组不置位；错误文本不对外泄漏（缓存命中语义） */
    cvr_clear_last_error();
    return r.feature.flags.raw() & ~before;
}

// ----------------------------------------------------------------------------
// 灰度特征（Halcon gray_features 的最小可用子集）
//   area / row / column / mean / deviation / min / max / median
namespace {

// 灰度累加器：Welford 求 mean/M2（数值稳定，避免 Σg² - Σg²/F 的抵消），
// 同时累计灰度体积、灰度加权行列矩与极值 —— 一次扫描即可派生全部 8 个特征。
struct GrayAcc {
    double  sg = 0.0, srg = 0.0, scg = 0.0;   // Σg, Σr·g, Σc·g
    double  mean = 0.0, m2 = 0.0;             // Welford: mean, Σ(g-mean)^2
    double  gmin = 0.0, gmax = 0.0;
    int64_t n = 0;
    bool    any = false;
};

inline void gray_acc_add(GrayAcc& a, double g, double r, double c)
{
    a.sg  += g;
    a.srg += r * g;
    a.scg += c * g;
    ++a.n;
    if (!a.any) { a.gmin = a.gmax = g; a.any = true; }
    else {
        if (g < a.gmin) a.gmin = g;
        if (g > a.gmax) a.gmax = g;
    }
    const double d = g - a.mean;
    a.mean += d / (double)a.n;
    a.m2   += d * (g - a.mean);
}

// 一趟扫描：region 的 runs × 灰度缓冲；行/列按图像尺寸裁剪
template <class T>
void gray_scan(const CvrRegion& r, const T* px, int width, int height, GrayAcc& a)
{
    const size_t m = r.runs.size();
    for (size_t i = 0; i < m; ++i) {
        const CvrRun& rr = r.runs[i];
        if (rr.r < 0 || rr.r >= height) continue;
        const T* line = px + (size_t)rr.r * (size_t)width;
        CvrCoord cb = rr.cb < 0 ? 0 : rr.cb;
        CvrCoord ce = rr.ce >= width ? (CvrCoord)(width - 1) : rr.ce;
        for (CvrCoord c = cb; c <= ce; ++c)
            gray_acc_add(a, (double)line[c], (double)rr.r, (double)c);
    }
}

// median：等价 min_max_gray(Percent=50) —— 直方图上累计超过一半像素处的灰度。
// 整数类型且范围可枚举时用 1:1 精确桶，否则 256 桶（同 HALCON gray_histo 默认）。
template <class T>
double gray_hist_median(const CvrRegion& r, const T* px, int width, int height,
                        const GrayAcc& a)
{
    if (a.n <= 0) return 0.0;
    const double span = a.gmax - a.gmin;
    if (!(span > 0.0)) return a.gmin;

    const bool exact = (span <= 65535.0) && (span == std::floor(span)) &&
                       (std::floor(a.gmin) == a.gmin);
    const int nbins = exact ? (int)span + 1 : 256;
    std::vector<int64_t> hist((size_t)nbins, 0);

    const size_t m = r.runs.size();
    for (size_t i = 0; i < m; ++i) {
        const CvrRun& rr = r.runs[i];
        if (rr.r < 0 || rr.r >= height) continue;
        const T* line = px + (size_t)rr.r * (size_t)width;
        CvrCoord cb = rr.cb < 0 ? 0 : rr.cb;
        CvrCoord ce = rr.ce >= width ? (CvrCoord)(width - 1) : rr.ce;
        for (CvrCoord c = cb; c <= ce; ++c) {
            const double g = (double)line[c];
            int b = exact ? (int)(g - a.gmin)
                          : (int)((g - a.gmin) * (double)(nbins - 1) / span);
            if (b < 0) b = 0;
            if (b >= nbins) b = nbins - 1;
            hist[(size_t)b]++;
        }
    }

    const int64_t half = a.n / 2;
    int64_t cum = 0;
    for (int b = 0; b < nbins; ++b) {
        cum += hist[(size_t)b];
        if (cum > half) {
            return exact ? (a.gmin + (double)b)
                         : (a.gmin + span * (double)b / (double)(nbins - 1));
        }
    }
    return a.gmax;
}

// 按运行时 depth 分派到具体像素类型
void gray_scan_dispatch(const CvrRegion& r, const void* gray, int width, int height,
                        CvrGrayDepth depth, GrayAcc& a)
{
    switch (depth) {
    case CVR_GRAY_U8:  gray_scan(r, (const uint8_t*)gray,  width, height, a); break;
    case CVR_GRAY_S8:  gray_scan(r, (const int8_t*)gray,   width, height, a); break;
    case CVR_GRAY_U16: gray_scan(r, (const uint16_t*)gray, width, height, a); break;
    case CVR_GRAY_S16: gray_scan(r, (const int16_t*)gray,  width, height, a); break;
    case CVR_GRAY_S32: gray_scan(r, (const int32_t*)gray,  width, height, a); break;
    case CVR_GRAY_S64: gray_scan(r, (const int64_t*)gray,  width, height, a); break;
    case CVR_GRAY_F32: gray_scan(r, (const float*)gray,    width, height, a); break;
    default:           gray_scan(r, (const double*)gray,   width, height, a); break;
    }
}

double gray_median_dispatch(const CvrRegion& r, const void* gray, int width, int height,
                            CvrGrayDepth depth, const GrayAcc& a)
{
    switch (depth) {
    case CVR_GRAY_U8:  return gray_hist_median(r, (const uint8_t*)gray,  width, height, a);
    case CVR_GRAY_S8:  return gray_hist_median(r, (const int8_t*)gray,   width, height, a);
    case CVR_GRAY_U16: return gray_hist_median(r, (const uint16_t*)gray, width, height, a);
    case CVR_GRAY_S16: return gray_hist_median(r, (const int16_t*)gray,  width, height, a);
    case CVR_GRAY_S32: return gray_hist_median(r, (const int32_t*)gray,  width, height, a);
    case CVR_GRAY_S64: return gray_hist_median(r, (const int64_t*)gray,  width, height, a);
    case CVR_GRAY_F32: return gray_hist_median(r, (const float*)gray,    width, height, a);
    default:           return gray_hist_median(r, (const double*)gray,   width, height, a);
    }
}

// 从累加器派生单个特征；未知名称返回 false
bool gray_derive(const GrayAcc& a, double median, const std::string& name, double& value)
{
    if (name == "area")      { value = a.sg;                        return true; }
    if (name == "row")       { value = (a.sg != 0.0) ? a.srg / a.sg : 0.0; return true; }
    if (name == "column")    { value = (a.sg != 0.0) ? a.scg / a.sg : 0.0; return true; }
    if (name == "mean")      { value = a.mean;                      return true; }
    if (name == "deviation") { value = (a.n > 0) ? std::sqrt(a.m2 / (double)a.n) : 0.0;
                               return true; }
    if (name == "min")       { value = a.any ? a.gmin : 0.0;        return true; }
    if (name == "max")       { value = a.any ? a.gmax : 0.0;        return true; }
    if (name == "median")    { value = median;                      return true; }
    return false;
}

bool gray_needs_median(const std::vector<std::string>& names)
{
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == "median") return true;
    return false;
}

} // namespace

bool cvr_gray_feature(const CvrRegion& r, const void* gray, int width, int height,
                      CvrGrayDepth depth, const std::string& name, double& value)
{
    cvr_clear_last_error();
    value = 0.0;
    if (!gray || width <= 0 || height <= 0) {
        cvr_set_last_error("cvr_gray_feature: invalid gray buffer or size");
        return false;
    }
    GrayAcc a;
    gray_scan_dispatch(r, gray, width, height, depth, a);
    const double median = (name == "median")
                              ? gray_median_dispatch(r, gray, width, height, depth, a)
                              : 0.0;
    if (!gray_derive(a, median, name, value)) {
        cvr_set_last_error("cvr_gray_feature: unknown gray feature: " + name);
        return false;
    }
    return true;
}

// ------ 图像 <-> region（对应算子 cv_bin_to_region / cv_region_to_bin，不依赖 OpenCV）
namespace {

// 图像 -> region：gray >= threshold 为前景，逐行生成 runs（天然按 (r, cb) 有序）
template <class T>
bool gray_to_region_row(const T* px, int width, int r, double threshold,
                        std::vector<CvrRun>& rowOut)
{
    const T* line = px + (size_t)r * (size_t)width;
    CvrCoord cb = -1;
    for (int c = 0; c < width; ++c) {
        if ((double)line[c] >= threshold) {
            if (cb < 0) cb = (CvrCoord)c;
        } else if (cb >= 0) {
            rowOut.push_back({(CvrCoord)r, cb, (CvrCoord)(c - 1)});
            cb = -1;
        }
    }
    if (cb >= 0) rowOut.push_back({(CvrCoord)r, cb, (CvrCoord)(width - 1)});
    return true;
}

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#define CVR_X86_SSE2 1
#include <emmintrin.h>
static inline int cvr_ctz32(unsigned v) {
#if defined(_MSC_VER)
    unsigned long k; _BitScanForward(&k, v); return (int)k;
#else
    return __builtin_ctz(v);
#endif
}
#endif

// float 特化：SSE2 成对比较。float→double 扩宽是精确的（_mm_cvtps_pd），
// cmpge 语义与 (double)f >= threshold 逐位一致 ⇒ 位级等价。
template <>
bool gray_to_region_row<float>(const float* px, int width, int r, double threshold,
                               std::vector<CvrRun>& rowOut)
{
    const float* line = px + (size_t)r * (size_t)width;
    CvrCoord cb = -1;
    int c = 0;
#ifdef CVR_X86_SSE2
    const __m128d tv = _mm_set1_pd(threshold);
    unsigned prevBit = 0;   // 上一块末像素的 fg 位（跨块 run 衔接）
    for (; c + 4 <= width; c += 4) {
        const __m128 f4 = _mm_loadu_ps(line + c);
        const __m128d lo = _mm_cvtps_pd(f4);                    // px[c], px[c+1]
        const __m128d hi = _mm_cvtps_pd(_mm_movehl_ps(f4, f4)); // px[c+2], px[c+3]
        const unsigned mlo = (unsigned)_mm_movemask_pd(_mm_cmpge_pd(lo, tv));
        const unsigned mhi = (unsigned)_mm_movemask_pd(_mm_cmpge_pd(hi, tv));
        const unsigned mask = mlo | (mhi << 2);
        // 只处理 fg 跳变沿（0→1 开 run、1→0 闭 run），稀疏前景时远快于逐位扫描。
        // trans 必须限制在块内位（mask<<1 会把末位状态泄到 bit4/bit16）
        unsigned trans = (mask ^ ((mask << 1) | prevBit)) & 0xfu;
        while (trans) {
            const int k = cvr_ctz32(trans);
            trans &= trans - 1;
            if ((mask >> k) & 1u) cb = (CvrCoord)(c + k);             // 0→1
            else { rowOut.push_back({(CvrCoord)r, cb, (CvrCoord)(c + k - 1)}); cb = -1; }
        }
        prevBit = (mask >> 3) & 1u;
    }
#endif
    for (; c < width; ++c) {
        if ((double)line[c] >= threshold) {
            if (cb < 0) cb = (CvrCoord)c;
        } else if (cb >= 0) {
            rowOut.push_back({(CvrCoord)r, cb, (CvrCoord)(c - 1)});
            cb = -1;
        }
    }
    if (cb >= 0) rowOut.push_back({(CvrCoord)r, cb, (CvrCoord)(width - 1)});
    return true;
}

// uint8 特化：(double)u >= t ⟺ u >= cmin（cmin = 满足 (double)v >= t 的最小整数，
// clamp 到 [0,256)——t<=0 时 0 恒真，t>255  时全假），比较降为无转换整数比较。
// SSE2 16 字节/轮：max_epu8 + cmpeq 实现 >=，movemask 提取位图。位级等价。
template <>
bool gray_to_region_row<uint8_t>(const uint8_t* px, int width, int r, double threshold,
                                 std::vector<CvrRun>& rowOut)
{
    const uint8_t* line = px + (size_t)r * (size_t)width;
    if (std::isnan(threshold)) return true;   // 与 (double)u >= NaN = false 一致：全背景
    long ci = (long)std::ceil(threshold);
    if (ci < 0) ci = 0;
    if (ci > 255) return true;          // 全背景
    const int cmin = (int)ci;

    CvrCoord cb = -1;
    int c = 0;
#ifdef CVR_X86_SSE2
    const __m128i tv = _mm_set1_epi8((char)cmin);
    unsigned prevBit = 0;   // 上一块末像素的 fg 位（跨块 run 衔接）
    for (; c + 16 <= width; c += 16) {
        const __m128i v = _mm_loadu_si128((const __m128i*)(line + c));
        const unsigned mask = (unsigned)_mm_movemask_epi8(
            _mm_cmpeq_epi8(_mm_max_epu8(v, tv), v)) & 0xffffu;
        // 只处理 fg 跳变沿（0→1 开、1→0 闭），跨块连续由 prevBit 衔接
        unsigned trans = (mask ^ ((mask << 1) | prevBit)) & 0xffffu;
        while (trans) {
            const int k = cvr_ctz32(trans);
            trans &= trans - 1;
            if ((mask >> k) & 1u) cb = (CvrCoord)(c + k);             // 0→1
            else { rowOut.push_back({(CvrCoord)r, cb, (CvrCoord)(c + k - 1)}); cb = -1; }
        }
        prevBit = (mask >> 15) & 1u;
    }
#endif
    for (; c < width; ++c) {
        if ((int)line[c] >= cmin) {
            if (cb < 0) cb = (CvrCoord)c;
        } else if (cb >= 0) {
            rowOut.push_back({(CvrCoord)r, cb, (CvrCoord)(c - 1)});
            cb = -1;
        }
    }
    if (cb >= 0) rowOut.push_back({(CvrCoord)r, cb, (CvrCoord)(width - 1)});
    return true;
}

// 按行并行：每行独立生成 runs，最后**按行序**合并 ⇒ 与串行逐 run 位级等价
template <class T>
bool gray_to_region_impl(const T* px, int width, int height, double threshold,
                         CvrRegion& out)
{
    if (height >= 128) {
        std::vector<std::vector<CvrRun> > perRow((size_t)height);
        try {
            cvr_parallel_for(height, 128, [&](int rr) {
                gray_to_region_row(px, width, rr, threshold, perRow[(size_t)rr]);
            });
        } catch (const std::exception& e) {
            cvr_set_last_error(e.what());
            return false;
        }
        size_t total = 0;
        for (int rr = 0; rr < height; ++rr) total += perRow[(size_t)rr].size();
        out.runs.reserve(total);
        for (int rr = 0; rr < height; ++rr)
            out.runs.insert(out.runs.end(), perRow[(size_t)rr].begin(), perRow[(size_t)rr].end());
        return true;
    }
    for (int r = 0; r < height; ++r) gray_to_region_row(px, width, r, threshold, out.runs);
    return true;
}

// region -> 图像：先铺背景，再按 runs 写前景（补集先物化，避免原地别名）
template <class T>
bool region_to_gray_impl(const CvrRegion& r, CvrCoord w, CvrCoord h, T* px,
                         double fg, double bg)
{
    const size_t n = (size_t)w * (size_t)h;
    const T bgv = (T)bg;
    const T fgv = (T)fg;
    try {
        // 铺背景：按行并行（每行写完自己的区间，互不相交 ⇒ 位级等价）
        if (h >= 128) {
            cvr_parallel_for((int)h, 128, [&](int rr) {
                T* line = px + (size_t)rr * (size_t)w;
                for (CvrCoord c = 0; c < w; ++c) line[c] = bgv;
            });
        } else {
            for (size_t i = 0; i < n; ++i) px[i] = bgv;
        }
    } catch (const std::exception& e) {
        cvr_set_last_error(e.what());
        return false;
    }
    CvrRegion tmp;
    const CvrRegion* src = &r;
    if (r.is_compl) {
        if (!cvr_region_materialize(r, w, h, tmp)) return false;
        src = &tmp;
    }
    const size_t nr = src->runs.size();
    const CvrRegion* s2 = src;
    try {
        // 写前景：按 run 并行（每个 run 写自己那一段，互不相交 ⇒ 位级等价）
        if (nr >= 64) {
            cvr_parallel_for((int)nr, 64, [&](int kk) {
                const CvrRun& rr = s2->runs[(size_t)kk];
                if (rr.r < 0 || rr.r >= h) return;
                T* line = px + (size_t)rr.r * (size_t)w;
                const CvrCoord cb = rr.cb < 0 ? 0 : rr.cb;
                const CvrCoord ce = rr.ce >= w ? (CvrCoord)(w - 1) : rr.ce;
                for (CvrCoord c = cb; c <= ce; ++c) line[c] = fgv;
            });
            return true;
        }
    } catch (const std::exception& e) {
        cvr_set_last_error(e.what());
        return false;
    }
    for (size_t k = 0; k < nr; ++k) {
        const CvrRun& rr = src->runs[k];
        if (rr.r < 0 || rr.r >= h) continue;
        T* line = px + (size_t)rr.r * (size_t)w;
        const CvrCoord cb = rr.cb < 0 ? 0 : rr.cb;
        const CvrCoord ce = rr.ce >= w ? (CvrCoord)(w - 1) : rr.ce;
        for (CvrCoord c = cb; c <= ce; ++c) line[c] = fgv;
    }
    return true;
}

} // namespace

bool cvr_bin_to_region(const void* gray, int width, int height, CvrGrayDepth depth,
                       double threshold, CvrRegion& out)
{
    cvr_clear_last_error();
    out.runs.clear();
    out.is_compl = false;
    if (!gray || width <= 0 || height <= 0) {
        cvr_set_last_error("cvr_bin_to_region: invalid gray buffer or size");
        return false;
    }
    switch (depth) {
    case CVR_GRAY_U8:  gray_to_region_impl((const uint8_t*)gray,  width, height, threshold, out); break;
    case CVR_GRAY_S8:  gray_to_region_impl((const int8_t*)gray,   width, height, threshold, out); break;
    case CVR_GRAY_U16: gray_to_region_impl((const uint16_t*)gray, width, height, threshold, out); break;
    case CVR_GRAY_S16: gray_to_region_impl((const int16_t*)gray,  width, height, threshold, out); break;
    case CVR_GRAY_S32: gray_to_region_impl((const int32_t*)gray,  width, height, threshold, out); break;
    case CVR_GRAY_S64: gray_to_region_impl((const int64_t*)gray,  width, height, threshold, out); break;
    case CVR_GRAY_F32: gray_to_region_impl((const float*)gray,    width, height, threshold, out); break;
    case CVR_GRAY_F64: gray_to_region_impl((const double*)gray,   width, height, threshold, out); break;
    default:
        cvr_set_last_error("cvr_bin_to_region: unknown depth");
        return false;
    }
    return cvr_region_normalize(out);   // 幂等：保证 (r, cb) 有序
}

bool cvr_region_to_bin(const CvrRegion& r, CvrCoord w, CvrCoord h, void* gray,
                       CvrGrayDepth depth, double foreground, double background)
{
    cvr_clear_last_error();
    if (!gray || w <= 0 || h <= 0) {
        cvr_set_last_error("cvr_region_to_bin: invalid buffer or size");
        return false;
    }
    switch (depth) {
    case CVR_GRAY_U8:  return region_to_gray_impl(r, w, h, (uint8_t*)gray,  foreground, background);
    case CVR_GRAY_S8:  return region_to_gray_impl(r, w, h, (int8_t*)gray,   foreground, background);
    case CVR_GRAY_U16: return region_to_gray_impl(r, w, h, (uint16_t*)gray, foreground, background);
    case CVR_GRAY_S16: return region_to_gray_impl(r, w, h, (int16_t*)gray,  foreground, background);
    case CVR_GRAY_S32: return region_to_gray_impl(r, w, h, (int32_t*)gray,  foreground, background);
    case CVR_GRAY_S64: return region_to_gray_impl(r, w, h, (int64_t*)gray,  foreground, background);
    case CVR_GRAY_F32: return region_to_gray_impl(r, w, h, (float*)gray,    foreground, background);
    case CVR_GRAY_F64: return region_to_gray_impl(r, w, h, (double*)gray,   foreground, background);
    default:
        cvr_set_last_error("cvr_region_to_bin: unknown depth");
        return false;
    }
}
bool cvr_gray_features(const std::vector<CvrRegion>& regions, const void* gray,
                       int width, int height, CvrGrayDepth depth,
                       const std::vector<std::string>& names,
                       std::vector<double>& values)
{
    cvr_clear_last_error();
    values.clear();
    if (!gray || width <= 0 || height <= 0) {
        cvr_set_last_error("cvr_gray_features: invalid gray buffer or size");
        return false;
    }
    if (regions.empty() || names.empty()) return true;

    const int nR = (int)regions.size();
    const int nF = (int)names.size();
    const bool wantMedian = gray_needs_median(names);

    std::vector<GrayAcc> accs((size_t)nR);
    std::vector<double>  meds((size_t)nR, 0.0);
    std::vector<double>  buf((size_t)nR * (size_t)nF, 0.0);

    // 按 region 并行（每区域单趟扫描；median 再多一趟建直方图）
    try {
        cvr_parallel_for(nR, 4, [&](int i) {
            const CvrRegion& r = regions[(size_t)i];
            GrayAcc& a = accs[(size_t)i];
            gray_scan_dispatch(r, gray, width, height, depth, a);
            if (wantMedian)
                meds[(size_t)i] = gray_median_dispatch(r, gray, width, height, depth, a);
            double* row = buf.data() + (size_t)i * (size_t)nF;
            for (int j = 0; j < nF; ++j) {
                if (!gray_derive(a, meds[(size_t)i], names[(size_t)j], row[j]))
                    throw std::runtime_error("cvr_gray_features: unknown gray feature: "
                                             + names[(size_t)j]);
            }
        });
    } catch (const std::exception& e) {
        cvr_set_last_error(e.what());
        return false;
    }

    values = std::move(buf);
    return true;
}

} // namespace cvr

/*===========================================================================
 * 形状变换 / 生成
 * （原 src/cvr_shape.cpp）
 *=========================================================================*/
namespace cvr {

static void add_ellipse_runs(CvrRegion& out, double cx, double cy,
                             double ra, double rb, double phi)
{
    // 离散生成椭圆边界并填充
    std::vector<CvrRun> runs;
    int steps = std::max(36, (int)(2.0 * CVR_PI * std::max(ra, rb) / 2.0));
    std::set<std::pair<CvrCoord, CvrCoord>> pts;

    double c = std::cos(phi), s = std::sin(phi);
    for (int i = 0; i < steps; ++i) {
        double t = 2.0 * CVR_PI * i / steps;
        double lx = ra * std::cos(t);
        double ly = rb * std::sin(t);
        double x = cy + c * lx - s * ly; // 列
        double y = cx + s * lx + c * ly; // 行
        CvrCoord r = (CvrCoord)std::round(y);
        CvrCoord ccol = (CvrCoord)std::round(x);
        pts.insert({r, ccol});
    }

    // 扫描线填充：对每一行找最小最大列
    std::map<CvrCoord, std::pair<CvrCoord, CvrCoord>> rows;
    for (auto& p : pts) {
        auto it = rows.find(p.first);
        if (it == rows.end()) rows[p.first] = {p.second, p.second};
        else {
            if (p.second < it->second.first) it->second.first = p.second;
            if (p.second > it->second.second) it->second.second = p.second;
        }
    }
    for (auto& kv : rows) {
        out.runs.push_back({kv.first, kv.second.first, kv.second.second});
    }
}

// 凸多边形 -> region（按行解析求交的扫描线填充）
//   poly 顶点为 (row, col)，顺序任意（须凸）。
//   与 HALCON shape_trans 的区域生成口径一致（像素 (r,c) = 单位方格 [r,r+1)x[c,c+1)）：
//     行取 [floor(min_row), floor(max_row)]；
//     每行在 y = row 处求多边形列跨度，列取 [floor(xmin), floor(xmax)]。
//   实测该口径可逐像素复现 HALCON：140x30 矩形 -> 141x31 = 4371，566x8 细条 -> 567x9 = 5103。
static void runs_from_convex_polygon(
    const std::vector<std::pair<double, double>>& poly, CvrRegion& out)
{
    out.runs.clear();
    if (poly.size() < 3) return;

    double min_r = poly[0].first, max_r = poly[0].first;
    for (const auto& p : poly) {
        if (p.first < min_r) min_r = p.first;
        if (p.first > max_r) max_r = p.first;
    }

    const CvrCoord r_begin = (CvrCoord)std::floor(min_r);
    const CvrCoord r_end   = (CvrCoord)std::floor(max_r);

    for (CvrCoord rr = r_begin; rr <= r_end; ++rr) {
        const double y = (double)rr;
        bool hit = false;
        double xlo = 0.0, xhi = 0.0;
        for (size_t i = 0; i < poly.size(); ++i) {
            const std::pair<double, double>& a = poly[i];
            const std::pair<double, double>& b = poly[(i + 1) % poly.size()];
            if ((a.first <= y && b.first > y) || (b.first <= y && a.first > y)) {
                const double t = (y - a.first) / (b.first - a.first);
                const double x = a.second + t * (b.second - a.second);
                if (!hit)            { xlo = xhi = x; hit = true; }
                else                 { if (x < xlo) xlo = x; if (x > xhi) xhi = x; }
            }
        }
        if (!hit) {
            // 退化：该行只与顶点/水平边相切（如矩形上下底边所在行）
            for (const auto& p : poly) {
                if (std::fabs(p.first - y) <= 1e-9) {
                    if (!hit) { xlo = xhi = p.second; hit = true; }
                    else      { if (p.second < xlo) xlo = p.second;
                                if (p.second > xhi) xhi = p.second; }
                }
            }
        }
        if (!hit) continue;
        CvrCoord cb = (CvrCoord)std::floor(xlo);
        CvrCoord ce = (CvrCoord)std::floor(xhi);
        if (ce < cb) ce = cb;
        out.runs.push_back({ rr, cb, ce });
    }
}

bool cvr_shape_trans(const CvrRegion& r, const std::string& shape, CvrRegion& out)
{
    cvr_clear_last_error();
    out.runs.clear();
    out.is_compl = false;

    std::string s = shape;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    if (s == "rectangle1") {
        CvrCoord r1, c1, r2, c2;
        if (!cvr_feature_smallest_rectangle1(r, r1, c1, r2, c2)) return false;
        for (CvrCoord rr = r1; rr <= r2; ++rr)
            out.runs.push_back({rr, c1, c2});
    }
    else if (s == "rectangle2") {
        // 最小外接旋转矩形：取 4 个角点后用凸多边形扫描线填充
        // （旧实现用 cos/sin 沿半宽/半高采样一圈 = 内切椭圆，输出成椭圆/圆）
        double row, col, phi, l1, l2;
        if (!cvr_feature_smallest_rectangle2(r, row, col, phi, l1, l2)) return false;
        if (l1 < 0.0) l1 = 0.0;
        if (l2 < 0.0) l2 = 0.0;

        const double co = std::cos(phi), si = std::sin(phi);
        // 局部坐标 u=±l1（主轴）、v=±l2（副轴）：
        //   col = col0 + co*u - si*v ;  row = row0 + si*u + co*v
        const double uu[2] = {  l1, -l1 };
        const double vv[2] = {  l2, -l2 };
        std::vector<std::pair<double, double>> quad; // (row, col)
        quad.reserve(4);
        quad.push_back({ row + si * uu[0] + co * vv[0], col + co * uu[0] - si * vv[0] });
        quad.push_back({ row + si * uu[0] + co * vv[1], col + co * uu[0] - si * vv[1] });
        quad.push_back({ row + si * uu[1] + co * vv[1], col + co * uu[1] - si * vv[1] });
        quad.push_back({ row + si * uu[1] + co * vv[0], col + co * uu[1] - si * vv[0] });
        runs_from_convex_polygon(quad, out);
    }
    else if (s == "ellipse") {
        double cx, cy, ra, rb, phi;
        CvrChords area;
        if (!cvr_feature_area_center(r, cx, cy, area)) return false;
        if (!cvr_feature_elliptic_axis(r, ra, rb, phi)) return false;
        add_ellipse_runs(out, cx, cy, ra * 0.5, rb * 0.5, phi);
    }
    else if (s == "outer_circle") {
        double cx, cy, rad;
        if (!cvr_feature_smallest_circle(r, cx, cy, rad)) return false;
        add_ellipse_runs(out, cx, cy, rad, rad, 0.0);
    }
    else if (s == "convex") {
        // 用凸包顶点生成填充多边形
        std::vector<std::pair<double, double>> pts, hull;
        for (const auto& rr : r.runs) {
            pts.push_back({(double)rr.r, (double)rr.cb});
            pts.push_back({(double)rr.r, (double)rr.ce});
        }
        // monotone chain
        std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second < b.second;
            return a.first < b.first;
        });
        pts.erase(std::unique(pts.begin(), pts.end()), pts.end());

        std::vector<std::pair<double, double>> lower, upper;
        for (const auto& p : pts) {
            while (lower.size() >= 2) {
                auto& q = lower.back();
                auto& rr = lower[lower.size() - 2];
                double cr = (q.second - rr.second) * (p.first - q.first) -
                            (q.first - rr.first) * (p.second - q.second);
                if (cr <= 0) lower.pop_back(); else break;
            }
            lower.push_back(p);
        }
        for (auto it = pts.rbegin(); it != pts.rend(); ++it) {
            const auto& p = *it;
            while (upper.size() >= 2) {
                auto& q = upper.back();
                auto& rr = upper[upper.size() - 2];
                double cr = (q.second - rr.second) * (p.first - q.first) -
                            (q.first - rr.first) * (p.second - q.second);
                if (cr <= 0) upper.pop_back(); else break;
            }
            upper.push_back(p);
        }
        lower.pop_back(); upper.pop_back();
        hull = lower;
        hull.insert(hull.end(), upper.begin(), upper.end());

        // 扫描线填充凸多边形
        if (!hull.empty()) {
            double min_r = hull[0].first, max_r = hull[0].first;
            for (auto& p : hull) { if (p.first < min_r) min_r = p.first; if (p.first > max_r) max_r = p.first; }
            for (CvrCoord row = (CvrCoord)std::floor(min_r); row <= (CvrCoord)std::ceil(max_r); ++row) {
                std::vector<double> xs;
                for (size_t i = 0; i < hull.size(); ++i) {
                    size_t j = (i + 1) % hull.size();
                    double r1 = hull[i].first, r2 = hull[j].first;
                    double c1 = hull[i].second, c2 = hull[j].second;
                    if ((r1 <= row && r2 > row) || (r2 <= row && r1 > row)) {
                        double t = (row - r1) / (r2 - r1);
                        xs.push_back(c1 + t * (c2 - c1));
                    }
                }
                if (xs.size() >= 2) {
                    std::sort(xs.begin(), xs.end());
                    out.runs.push_back({row, (CvrCoord)std::floor(xs.front()), (CvrCoord)std::ceil(xs.back())});
                }
            }
        }
    }
    else {
        cvr_set_last_error("cvr_shape_trans: unknown shape type: " + shape);
        return false;
    }

    return cvr_region_normalize(out);
}

// ----------------------------------------------------------------------------
// gen_rectangle2：生成旋转矩形
//   Halcon: gen_rectangle2(Rectangle2 : : Row, Column, Phi, Length1, Length2 : )
//   口径 = 「像素方格相交即取」：像素 (r,c) 覆盖 [r-0.5, r+0.5] x [c-0.5, c+0.5]，
//   与矩形有交集就计入 —— 实测与 HALCON gen_rectangle2 一致
//   （140x30 轴对齐 -> 141x31 = 4371；566x8 -> 567x9 = 5103；phi=0.6 -> ~4360）。
//   注意与 shape_trans "rectangle2" 的口径不同（后者按 HALCON shape_trans 的
//   多边形 rasterizer，同参数下面积可差 ~5%）。
//   另：注意 phi 的符号约定与 cv_smallest_rectangle2 / cv_elliptic_axis 返回的
//   phi 反号（cvr 内部约定），supply 层按 HALCON 语义取反后传入。
// ----------------------------------------------------------------------------
CvrRegion cvr_gen_rectangle2(double row, double col, double phi,
                             double length1, double length2)
{
    CvrRegion out;
    const double l1 = length1 < 0.0 ? 0.0 : length1;
    const double l2 = length2 < 0.0 ? 0.0 : length2;
    const double eps = 1e-9;

    const double co = std::cos(phi), si = std::sin(phi);
    // 4 个角点：局部坐标 u = ±l1（主轴）、v = ±l2
    //   col = col0 + co*u - si*v ;  row = row0 + si*u + co*v
    double cr[4], cc[4];
    const double uu[4] = { l1,  l1, -l1, -l1 };
    const double vv[4] = { l2, -l2, -l2,  l2 };
    double min_r = 0.0, max_r = 0.0, min_c = 0.0, max_c = 0.0;
    for (int i = 0; i < 4; ++i) {
        cr[i] = row + si * uu[i] + co * vv[i];
        cc[i] = col + co * uu[i] - si * vv[i];
        if (i == 0) { min_r = max_r = cr[0]; min_c = max_c = cc[0]; }
        else {
            if (cr[i] < min_r) min_r = cr[i];
            if (cr[i] > max_r) max_r = cr[i];
            if (cc[i] < min_c) min_c = cc[i];
            if (cc[i] > max_c) max_c = cc[i];
        }
    }

    // 角点行/列吸附：数学上相等但浮点相差 ~1e-14 的角点必须变成严格相等，
    // 否则"近似水平"的边会跨过扫描线产生伪交点（曾使 phi=pi/2 档丢 170 像素）
    for (int i = 0; i < 4; ++i) {
        if (std::fabs(cr[i] - min_r) <= eps)      cr[i] = min_r;
        else if (std::fabs(cr[i] - max_r) <= eps) cr[i] = max_r;
        if (std::fabs(cc[i] - min_c) <= eps)      cc[i] = min_c;
        else if (std::fabs(cc[i] - max_c) <= eps) cc[i] = max_c;
    }

    const CvrCoord r_begin = (CvrCoord)std::ceil(min_r - 0.5 - eps);
    const CvrCoord r_end   = (CvrCoord)std::floor(max_r + 0.5 + eps);

    for (CvrCoord rr = r_begin; rr <= r_end; ++rr) {
        bool hit = false;
        double xlo = 0.0, xhi = 0.0;
        // 像素行带 [rr-0.5, rr+0.5] 的两端交点决定列跨度（凸多边形取两端即可）
        for (int pass = 0; pass <= 1; ++pass) {
            const double y = (double)rr + (pass == 0 ? -0.5 : 0.5);
            for (int i = 0; i < 4; ++i) {
                const int j = (i + 1) % 4;
                const double ya = cr[i], yb = cr[j];
                if ((ya <= y && yb > y) || (yb <= y && ya > y)) {
                    const double t = (y - ya) / (yb - ya);
                    const double x = cc[i] + t * (cc[j] - cc[i]);
                    if (!hit) { xlo = xhi = x; hit = true; }
                    else      { if (x < xlo) xlo = x; if (x > xhi) xhi = x; }
                }
            }
        }
        if (!hit) {
            // 该行带内没有边穿过：用落在带内的角点（水平边/顶点相切）
            for (int i = 0; i < 4; ++i) {
                if (cr[i] >= (double)rr - 0.5 - eps &&
                    cr[i] <= (double)rr + 0.5 + eps) {
                    if (!hit) { xlo = xhi = cc[i]; hit = true; }
                    else      { if (cc[i] < xlo) xlo = cc[i];
                                if (cc[i] > xhi) xhi = cc[i]; }
                }
            }
        }
        if (!hit) continue;
        const CvrCoord cb = (CvrCoord)std::ceil(xlo - 0.5 - eps);
        const CvrCoord ce = (CvrCoord)std::floor(xhi + 0.5 + eps);
        if (ce < cb) continue;
        out.runs.push_back({ rr, cb, ce });
    }
    cvr_region_normalize(out);
    return out;
}

} // namespace cvr

/*===========================================================================
 * select_shape
 * （原 src/cvr_select.cpp）
 *=========================================================================*/
namespace cvr {

bool cvr_select_shape(const std::vector<CvrRegion>& regions,
                      const std::vector<std::string>& features,
                      const std::string& op,
                      const std::vector<double>& mins,
                      const std::vector<double>& maxs,
                      std::vector<CvrRegion>& selected)
{
    cvr_clear_last_error();
    selected.clear();

    if (features.size() != mins.size() || features.size() != maxs.size()) {
        cvr_set_last_error("cvr_select_shape: features/mins/maxs size mismatch");
        return false;
    }
    if (features.empty()) {
        selected = regions;
        return true;
    }

    std::string operation = op;
    std::transform(operation.begin(), operation.end(), operation.begin(), ::tolower);
    bool use_and = (operation == "and");
    if (!use_and && operation != "or") {
        cvr_set_last_error("cvr_select_shape: operation must be 'and' or 'or'");
        return false;
    }

    // 并行判定：worker 内只写自己的 keep[i]，错误经异常汇聚到主线程
    const int n = (int)regions.size();
    std::vector<unsigned char> keep((size_t)n, 0);

    try {
        cvr_parallel_for(n, 4, [&](int idx) {
            const CvrRegion& r = regions[(size_t)idx];
            bool pass = use_and ? true : false;
            for (size_t i = 0; i < features.size(); ++i) {
                double v = 0.0;
                if (!cvr_get_feature(r, features[i], v))
                    throw std::runtime_error("cvr_select_shape: unknown feature: "
                                             + features[i]);
                const bool in_range = (v >= mins[i] && v <= maxs[i]);
                if (use_and) {
                    pass = pass && in_range;
                    if (!pass) break;
                } else {
                    pass = pass || in_range;
                    if (pass) break;
                }
            }
            keep[(size_t)idx] = pass ? 1 : 0;
        });
    } catch (const std::exception& e) {
        cvr_set_last_error(e.what());
        return false;
    }

    // 串行收集，保持原顺序
    selected.reserve((size_t)n);
    for (int i = 0; i < n; ++i)
        if (keep[(size_t)i]) selected.push_back(regions[(size_t)i]);
    return true;
}

bool cvr_select_shape_single(const std::vector<CvrRegion>& regions,
                             const std::string& feature,
                             double min_val, double max_val,
                             std::vector<CvrRegion>& selected)
{
    return cvr_select_shape(regions, {feature}, "and", {min_val}, {max_val}, selected);
}

} // namespace cvr

/*===========================================================================
 * OpenCV 桥实现
 * （原 src/cvr_io.cpp）
 *=========================================================================*/
#ifdef CVR_WITH_OPENCV
#include <opencv2/imgproc.hpp>

namespace cvr {

bool cvr_region_to_mask(const CvrRegion& r, CvrCoord w, CvrCoord h,
                        cv::Mat& mask) {
    cvr_clear_last_error();
    if (w <= 0 || h <= 0) {
        cvr_set_last_error("cvr_region_to_mask: invalid domain size");
        return false;
    }

    mask.create(h, w, CV_8UC1);
    mask.setTo(cv::Scalar(0));

    if (r.is_compl) {
        CvrRegion mat;
        if (!cvr_region_materialize(r, w, h, mat)) {
            return false;
        }
        for (const auto& rr : mat.runs) {
            cv::line(mask, {rr.cb, rr.r}, {rr.ce, rr.r}, cv::Scalar(255));
        }
    } else {
        for (const auto& rr : r.runs) {
            if (rr.r < 0 || rr.r >= h) continue;
            CvrCoord cb = std::max<CvrCoord>(0, rr.cb);
            CvrCoord ce = std::min<CvrCoord>(w - 1, rr.ce);
            if (cb <= ce) {
                cv::line(mask, {cb, rr.r}, {ce, rr.r}, cv::Scalar(255));
            }
        }
    }
    return true;
}

bool cvr_region_from_mask(const cv::Mat& mask, CvrRegion& r,
                          CvrCoord* out_w, CvrCoord* out_h) {
    cvr_clear_last_error();
    if (mask.empty()) {
        cvr_set_last_error("cvr_region_from_mask: empty mask");
        return false;
    }
    if (mask.channels() != 1) {
        cvr_set_last_error("cvr_region_from_mask: mask must be single channel");
        return false;
    }

    CvrCoord w = static_cast<CvrCoord>(mask.cols);
    CvrCoord h = static_cast<CvrCoord>(mask.rows);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;

    r.runs.clear();
    r.is_compl = false;
    cvr_region_invalidate(r);

    cv::Mat tmp;
    if (mask.type() != CV_8UC1) {
        mask.convertTo(tmp, CV_8UC1);
    } else {
        tmp = mask;
    }

    // 按行并行：每行独立写 perRow[row]，再按行升序合并（与串行版同序）
    std::vector<std::vector<CvrRun>> perRow((size_t)h);
    cvr_parallel_for((int)h, 128, [&](int row) {
        const uchar* p = tmp.ptr<uchar>(row);
        std::vector<CvrRun>& v = perRow[(size_t)row];
        CvrCoord cb = -1;
        for (CvrCoord col = 0; col < w; ++col) {
            if (p[col] != 0) {
                if (cb < 0) cb = col;
            } else if (cb >= 0) {
                v.push_back({(CvrCoord)row, cb, static_cast<CvrCoord>(col - 1)});
                cb = -1;
            }
        }
        if (cb >= 0) v.push_back({(CvrCoord)row, cb, static_cast<CvrCoord>(w - 1)});
    });

    size_t total = 0;
    for (size_t i = 0; i < perRow.size(); ++i) total += perRow[i].size();
    r.runs.reserve(total);
    for (size_t i = 0; i < perRow.size(); ++i)
        r.runs.insert(r.runs.end(), perRow[i].begin(), perRow[i].end());

    return cvr_region_normalize(r);
}

} // namespace cvr

#endif // CVR_WITH_OPENCV

/*===========================================================================
 * 遗留 C ABI 实现
 * （原 src/cvr_c_api.cpp）
 *=========================================================================*/
/*=============================================================================
 * cvr_c_api.cpp — C ABI 外接口实现（包一层薄壳，内部走 C++ 核心）
 *===========================================================================*/


static_assert(sizeof(CvrRunC) == sizeof(cvr::CvrRun), "CvrRunC/CvrRun layout mismatch");
static_assert(offsetof(CvrRunC, r)  == offsetof(cvr::CvrRun, r),  "layout mismatch");
static_assert(offsetof(CvrRunC, cb) == offsetof(cvr::CvrRun, cb), "layout mismatch");
static_assert(offsetof(CvrRunC, ce) == offsetof(cvr::CvrRun, ce), "layout mismatch");

namespace {

inline cvr::CvrRegion* to_cpp(cvr_region_h h) {
    return reinterpret_cast<cvr::CvrRegion*>(h);
}

inline cvr_region_h to_hdl(cvr::CvrRegion* p) {
    return reinterpret_cast<cvr_region_h>(p);
}

inline const CvrRunC* to_c(const cvr::CvrRun* p) {
    return reinterpret_cast<const CvrRunC*>(p);
}

inline cvr::CvrRun* to_cpp_run(CvrRunC* p) {
    return reinterpret_cast<cvr::CvrRun*>(p);
}

} // namespace

extern "C" {

CVR_API cvr_region_h cvr_region_create(const CvrRunC* runs, int64_t num_runs,
                                       int32_t is_compl) {
    if (num_runs < 0 || (num_runs > 0 && runs == nullptr)) return nullptr;
    auto* p = new (std::nothrow) cvr::CvrRegion();
    if (!p) return nullptr;
    p->is_compl = (is_compl != 0);
    if (num_runs > 0) {
        p->runs.resize(static_cast<size_t>(num_runs));
        std::memcpy(p->runs.data(), runs,
                    static_cast<size_t>(num_runs) * sizeof(CvrRunC));
    }
    return to_hdl(p);
}

CVR_API void cvr_region_destroy(cvr_region_h h) {
    delete to_cpp(h);
}

CVR_API int64_t cvr_region_num_runs(cvr_region_h h) {
    auto* p = to_cpp(h);
    return p ? static_cast<int64_t>(p->runs.size()) : 0;
}

CVR_API const CvrRunC* cvr_region_runs(cvr_region_h h) {
    auto* p = to_cpp(h);
    return (p && !p->runs.empty()) ? to_c(p->runs.data()) : nullptr;
}

CVR_API int32_t cvr_region_is_compl(cvr_region_h h) {
    auto* p = to_cpp(h);
    return (p && p->is_compl) ? 1 : 0;
}

CVR_API int32_t cvr_union2(cvr_region_h a, cvr_region_h b,
                           int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!a || !b || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_union2(*to_cpp(a), *to_cpp(b), w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_intersection(cvr_region_h a, cvr_region_h b,
                                 int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!a || !b || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_intersection(*to_cpp(a), *to_cpp(b), w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_erosion1(cvr_region_h r, cvr_region_h se,
                             int32_t iterations, int32_t w, int32_t h,
                             cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !se || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_erosion1(*to_cpp(r), *to_cpp(se), iterations, w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_connection(cvr_region_h r, int32_t connectivity,
                               int32_t w, int32_t h,
                               cvr_region_h** out_arr, int64_t* out_count) {
    cvr::cvr_clear_last_error();
    if (!r || !out_arr || !out_count) return 0;
    std::vector<cvr::CvrRegion> comps;
    if (!cvr::cvr_connection(*to_cpp(r), connectivity, w, h, comps))
        return 0;

    const int64_t n = static_cast<int64_t>(comps.size());
    auto* arr = new (std::nothrow) cvr_region_h[static_cast<size_t>(n > 0 ? n : 1)];
    if (!arr) return 0;
    for (int64_t i = 0; i < n; ++i) {
        auto* p = new (std::nothrow) cvr::CvrRegion(std::move(comps[static_cast<size_t>(i)]));
        if (!p) {
            for (int64_t j = 0; j < i; ++j) delete to_cpp(arr[j]);
            delete[] arr;
            return 0;
        }
        arr[i] = to_hdl(p);
    }
    *out_arr = arr;
    *out_count = n;
    return 1;
}

CVR_API void cvr_region_array_destroy(cvr_region_h* arr, int64_t count) {
    if (!arr) return;
    for (int64_t i = 0; i < count; ++i) delete to_cpp(arr[i]);
    delete[] arr;
}

CVR_API int32_t cvr_select_shape(cvr_region_h const* regions, int64_t n,
                                 const char* const* features, int64_t n_features,
                                 const char* operation,
                                 const double* mins, const double* maxs,
                                 int64_t** out_indices, int64_t* out_count) {
    cvr::cvr_clear_last_error();
    if (!regions || !out_indices || !out_count || n < 0) return 0;
    if (n > 0 && (!features || n_features <= 0 || !operation || !mins || !maxs))
        return 0;

    const bool is_or =
        (std::strcmp(operation, "or") == 0 || std::strcmp(operation, "OR") == 0);

    std::vector<int64_t> idx;
    for (int64_t i = 0; i < n; ++i) {
        auto* p = to_cpp(regions[i]);
        if (!p) return 0;
        bool pass = !is_or;   // and: 全真；or: 任一真
        for (int64_t f = 0; f < n_features; ++f) {
            if (!features[f]) return 0;
            double v = 0.0;
            if (!cvr::cvr_get_feature(*p, features[f], v)) {
                cvr::cvr_set_last_error("cvr_select_shape: unknown feature");
                return 0;
            }
            const bool ok = (v >= mins[f] && v <= maxs[f]);
            if (is_or) { if (ok) { pass = true; break; } }
            else       { if (!ok) { pass = false; break; } }
        }
        if (pass) idx.push_back(i);
    }

    const int64_t cnt = static_cast<int64_t>(idx.size());
    auto* buf = new (std::nothrow) int64_t[static_cast<size_t>(cnt > 0 ? cnt : 1)];
    if (!buf) return 0;
    if (cnt > 0) std::memcpy(buf, idx.data(),
                             static_cast<size_t>(cnt) * sizeof(int64_t));
    *out_indices = buf;
    *out_count = cnt;
    return 1;
}

CVR_API void cvr_free_int64(int64_t* p) {
    delete[] p;
}

/* ================= 集合代数（补充） ================= */

CVR_API int32_t cvr_union1(cvr_region_h const* regions, int64_t n,
                           int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!regions || !out || n < 0) return 0;
    std::vector<cvr::CvrRegion> regs;
    regs.reserve(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
        auto* p = to_cpp(regions[i]);
        if (!p) return 0;
        regs.push_back(*p);
    }
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_union1(regs, w, h, *o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_difference(cvr_region_h a, cvr_region_h b,
                               int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!a || !b || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_difference(*to_cpp(a), *to_cpp(b), w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_complement(cvr_region_h r, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_complement(*to_cpp(r), *o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_symm_difference(cvr_region_h a, cvr_region_h b,
                                    int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!a || !b || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_symm_difference(*to_cpp(a), *to_cpp(b), w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

/* ================= 形态学（补充） ================= */

CVR_API int32_t cvr_dilation1(cvr_region_h r, cvr_region_h se,
                              int32_t iterations, int32_t w, int32_t h,
                              cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !se || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_dilation1(*to_cpp(r), *to_cpp(se), iterations, w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_opening(cvr_region_h r, cvr_region_h se,
                            int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !se || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_opening(*to_cpp(r), *to_cpp(se), w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_closing(cvr_region_h r, cvr_region_h se,
                            int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !se || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_closing(*to_cpp(r), *to_cpp(se), w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_fill_up(cvr_region_h r, int32_t w, int32_t h,
                            cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_fill_up(*to_cpp(r), w, h, *o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

/* ================= 预设结构元形态学 ================= */

CVR_API int32_t cvr_erosion_circle(cvr_region_h r, double radius,
                                   int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !out || radius < 0.0) return 0;
    cvr::CvrRegion se = cvr::cvr_gen_circle(0, 0, radius);
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_erosion1(*to_cpp(r), se, 1, w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_dilation_circle(cvr_region_h r, double radius,
                                    int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !out || radius < 0.0) return 0;
    cvr::CvrRegion se = cvr::cvr_gen_circle(0, 0, radius);
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_dilation1(*to_cpp(r), se, 1, w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_erosion_rectangle1(cvr_region_h r, int32_t rw, int32_t rh,
                                       int32_t w, int32_t h,
                                       cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !out || rw <= 0 || rh <= 0) return 0;
    cvr::CvrRegion se = cvr::cvr_se_rect(rw, rh);
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_erosion1(*to_cpp(r), se, 1, w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_dilation_rectangle1(cvr_region_h r, int32_t rw, int32_t rh,
                                        int32_t w, int32_t h,
                                        cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !out || rw <= 0 || rh <= 0) return 0;
    cvr::CvrRegion se = cvr::cvr_se_rect(rw, rh);
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_dilation1(*to_cpp(r), se, 1, w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

/* ================= 区域生成 ================= */

/* HALCON gen_* 约定：裁剪负坐标（region 行列 >= 0） */
static void clip_negative(cvr::CvrRegion& r) {
    auto& runs = r.runs;
    std::vector<cvr::CvrRun> kept;
    kept.reserve(runs.size());
    for (const auto& rr : runs) {
        if (rr.r < 0) continue;
        cvr::CvrCoord cb = rr.cb < 0 ? 0 : rr.cb;
        if (cb <= rr.ce) kept.push_back(cvr::CvrRun{rr.r, cb, rr.ce});
    }
    runs = std::move(kept);
}

CVR_API int32_t cvr_gen_circle(double row, double col, double radius,
                               cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!out || radius < 0.0) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion(
        cvr::cvr_gen_circle(static_cast<cvr::CvrCoord>(std::lround(row)),
                            static_cast<cvr::CvrCoord>(std::lround(col)),
                            radius));
    if (!o) return 0;
    clip_negative(*o);
    if (!cvr::cvr_region_normalize(*o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_gen_rectangle1(int32_t r1, int32_t c1, int32_t r2,
                                   int32_t c2, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!out || r2 < r1 || c2 < c1) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion(
        cvr::cvr_gen_rectangle1(r1, c1, r2, c2));
    if (!o) return 0;
    clip_negative(*o);
    if (!cvr::cvr_region_normalize(*o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

/* ================= 特征 ================= */

CVR_API int32_t cvr_area_center(cvr_region_h r, double* row, double* col,
                                double* area) {
    cvr::cvr_clear_last_error();
    if (!r || !row || !col || !area) return 0;
    double rr = 0.0, cc = 0.0;
    cvr::CvrChords a = 0;
    if (!cvr::cvr_feature_area_center(*to_cpp(r), rr, cc, a)) return 0;
    *row = rr;
    *col = cc;
    *area = static_cast<double>(a);
    return 1;
}

CVR_API int32_t cvr_smallest_rectangle1(cvr_region_h r, double* r1, double* c1,
                                        double* r2, double* c2) {
    cvr::cvr_clear_last_error();
    if (!r || !r1 || !c1 || !r2 || !c2) return 0;
    cvr::CvrCoord rr1 = 0, cc1 = 0, rr2 = 0, cc2 = 0;
    if (!cvr::cvr_feature_smallest_rectangle1(*to_cpp(r), rr1, cc1, rr2, cc2))
        return 0;
    *r1 = rr1;
    *c1 = cc1;
    *r2 = rr2;
    *c2 = cc2;
    return 1;
}

CVR_API int32_t cvr_smallest_rectangle2(cvr_region_h r, double* row, double* col,
                                        double* phi, double* length1,
                                        double* length2) {
    cvr::cvr_clear_last_error();
    if (!r || !row || !col || !phi || !length1 || !length2) return 0;
    return cvr::cvr_feature_smallest_rectangle2(*to_cpp(r), *row, *col, *phi,
                                                *length1, *length2)
               ? 1
               : 0;
}

CVR_API int32_t cvr_smallest_circle(cvr_region_h r, double* row, double* col,
                                    double* radius) {
    cvr::cvr_clear_last_error();
    if (!r || !row || !col || !radius) return 0;
    return cvr::cvr_feature_smallest_circle(*to_cpp(r), *row, *col, *radius)
               ? 1
               : 0;
}

CVR_API int32_t cvr_elliptic_axis(cvr_region_h r, double* ra, double* rb,
                                  double* phi) {
    cvr::cvr_clear_last_error();
    if (!r || !ra || !rb || !phi) return 0;
    return cvr::cvr_feature_elliptic_axis(*to_cpp(r), *ra, *rb, *phi) ? 1 : 0;
}

CVR_API int32_t cvr_get_feature(cvr_region_h r, const char* name,
                                double* value) {
    cvr::cvr_clear_last_error();
    if (!r || !name || !value) return 0;
    return cvr::cvr_get_feature(*to_cpp(r), name, *value) ? 1 : 0;
}

/* ================= 形状变换 ================= */

CVR_API int32_t cvr_shape_trans(cvr_region_h r, const char* type,
                                cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !type || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_shape_trans(*to_cpp(r), type, *o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

/* ================= region <-> 二值图 ================= */

CVR_API int32_t cvr_region_to_mask(cvr_region_h r, int32_t w, int32_t h,
                                   int32_t fg, int32_t bg,
                                   uint8_t* buf, int64_t buf_size) {
    cvr::cvr_clear_last_error();
    if (!r || !buf || w <= 0 || h <= 0) return 0;
    if (fg < 0 || fg > 255 || bg < 0 || bg > 255) return 0;
    if (buf_size < static_cast<int64_t>(w) * h) return 0;

    std::memset(buf, bg, static_cast<size_t>(w) * static_cast<size_t>(h));

    cvr::CvrRegion mat;
    if (to_cpp(r)->is_compl) {
        if (!cvr::cvr_region_materialize(*to_cpp(r), w, h, mat)) return 0;
    } else {
        mat = *to_cpp(r);
    }

    const uint8_t fgv = static_cast<uint8_t>(fg);
    for (const auto& run : mat.runs) {
        if (run.r < 0 || run.r >= h) continue;
        int32_t cb = run.cb < 0 ? 0 : run.cb;
        int32_t ce = run.ce >= w ? w - 1 : run.ce;
        if (cb > ce) continue;
        uint8_t* line = buf + static_cast<int64_t>(run.r) * w;
        for (int32_t c = cb; c <= ce; ++c) line[c] = fgv;
    }
    return 1;
}

CVR_API int32_t cvr_region_from_mask(const uint8_t* buf, int32_t w, int32_t h,
                                     int32_t threshold, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!buf || !out || w <= 0 || h <= 0 || threshold < 0 || threshold > 255)
        return 0;

    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;

    for (int32_t r = 0; r < h; ++r) {
        const uint8_t* line = buf + static_cast<int64_t>(r) * w;
        int32_t c = 0;
        while (c < w) {
            while (c < w && line[c] < threshold) ++c;
            if (c >= w) break;
            const int32_t cb = c;
            while (c < w && line[c] >= threshold) ++c;
            o->runs.push_back(cvr::CvrRun{ r, cb, c - 1 });
        }
    }
    if (!cvr::cvr_region_normalize(*o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

CVR_API const char* cvr_c_last_error(void) {
    return cvr::cvr_last_error();
}

} // extern "C"
