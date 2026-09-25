#pragma once
#include "cvr/cvr_types.hpp"
#include <vector>

namespace cvr {

// ----------------------------------------------------------------------------
// 单条 chord/run：与 HALCON Hrun 语义一致
//   r  = 行号
//   cb = 起始列（包含）
//   ce = 结束列（包含）
// 不变量：cb <= ce；同一行多个 run 不重叠、不相接（相接必须合并）
// ----------------------------------------------------------------------------
struct CvrRun {
    CvrCoord r;
    CvrCoord cb;
    CvrCoord ce;
};

// ----------------------------------------------------------------------------
// 特征缓存（惰性计算）
// flags == 0 表示没有任何特征被计算过
// ----------------------------------------------------------------------------
struct CvrFeature {
    CvrFeatureFlags flags;

    // shape
    uint8_t  shape = 0;
    bool     is_convex = false;
    bool     is_filled = false;
    bool     is_connected4 = false;
    bool     is_connected8 = false;
    bool     is_thin = false;

    // scalar features
    double circularity  = 0.0;
    double compactness  = 0.0;
    double contlength   = 0.0;
    double convexity    = 0.0;
    double phi          = 0.0;
    double ra = 0.0, rb = 0.0;
    double ra_ = 0.0, rb_ = 0.0;
    double anisometry = 0.0, bulkiness = 0.0, structure_factor = 0.0;

    // moments
    double m11 = 0.0, m20 = 0.0, m02 = 0.0, ia = 0.0, ib = 0.0;

    // center / area
    double   row = 0.0, col = 0.0;
    CvrChords area = 0;

    // smallest rectangle1 (axis-aligned bbox)
    CvrCoord row1 = 0, col1 = 0, row2 = -1, col2 = -1;

    // smallest rectangle2 (oriented bbox)
    double row_rect = 0.0, col_rect = 0.0, phi_rect = 0.0;
    double length1 = 0.0, length2 = 0.0;

    // smallest outer circle
    double row_circle = 0.0, col_circle = 0.0, radius = 0.0;

    // chord statistics
    CvrCoord min_chord = 0, max_chord = 0;
    CvrCoord min_chord_gap = 0, max_chord_gap = 0;

    double rectangularity = 0.0;
};

// ----------------------------------------------------------------------------
// Region 主结构
// ----------------------------------------------------------------------------
struct CvrRegion {
    std::vector<CvrRun> runs;
    bool                is_compl = false;
    CvrFeature          feature;
};

// ----------------------------------------------------------------------------
// 比较与排序
// ----------------------------------------------------------------------------
inline bool cvr_run_less(const CvrRun& a, const CvrRun& b) noexcept {
    if (a.r != b.r) return a.r < b.r;
    if (a.cb != b.cb) return a.cb < b.cb;
    return a.ce < b.ce;
}

// ----------------------------------------------------------------------------
// 不变量维护
// ----------------------------------------------------------------------------

// 重置特征缓存（修改 runs 后必须调用）
void cvr_region_invalidate(CvrRegion& r) noexcept;

// 排序 + 合并相邻/重叠 run；完成后 region 满足 chord 三条件
bool cvr_region_normalize(CvrRegion& r);

// 判断 region 是否为空（考虑 is_compl）
bool cvr_region_is_empty(const CvrRegion& r, CvrCoord w, CvrCoord h) noexcept;

// 物化补集：将 is_compl 展开为普通 runs（结果 is_compl=false）
// 需要定义域 w,h；失败返回 false
bool cvr_region_materialize(const CvrRegion& r, CvrCoord w, CvrCoord h,
                            CvrRegion& out);

// 计算并返回轴对齐包围盒；结果写入 out 参数
bool cvr_region_bbox(const CvrRegion& r, CvrCoord& row1, CvrCoord& col1,
                     CvrCoord& row2, CvrCoord& col2);

// 行视图：访问某一行在 [0, w-1] 区间内的前景段
// 返回该行所有 run 的 [begin, end) 索引对
bool cvr_region_row_ranges(const CvrRegion& r, CvrCoord row,
                           size_t& begin, size_t& end);

// 调试辅助：检查 region 是否满足所有不变量
bool cvr_region_check_invariants(const CvrRegion& r) noexcept;

} // namespace cvr
