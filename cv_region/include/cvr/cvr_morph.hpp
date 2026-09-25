#pragma once
#include "cvr/cvr_region.hpp"

namespace cvr {

// ====== 结构元（Halcon 里用 gen_circle / gen_rectangle1 生成） ======
// 约定：结构元的 runs 坐标是相对参考点 (0,0) 的偏移，可以是负数

// Halcon: gen_circle(StructElement, Row, Column, Radius)
CvrRegion cvr_gen_circle(CvrCoord row, CvrCoord col, double radius);

// Halcon: gen_rectangle1(StructElement, Row1, Column1, Row2, Column2)
CvrRegion cvr_gen_rectangle1(CvrCoord r1, CvrCoord c1, CvrCoord r2, CvrCoord c2);

// 便捷：以 (0,0) 为中心的圆/矩形/十字
CvrRegion cvr_se_disk(int radius);
CvrRegion cvr_se_rect(int w, int h);
CvrRegion cvr_se_cross(int radius);

// 结构元转置（开运算的膨胀要用转置）
bool cvr_se_transpose(const CvrRegion& se, CvrRegion& out);

// ====== 形态学 ======
// Halcon: dilation1(Region : RegionDilation : StructElement, Iterations : )
bool cvr_dilation1(const CvrRegion& r, const CvrRegion& se, int iterations,
                   CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: erosion1(Region : RegionErosion : StructElement, Iterations : )
bool cvr_erosion1(const CvrRegion& r, const CvrRegion& se, int iterations,
                  CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: opening(Region : RegionOpening : StructElement : )
bool cvr_opening(const CvrRegion& r, const CvrRegion& se,
                 CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: closing(Region : RegionClosing : StructElement : )
bool cvr_closing(const CvrRegion& r, const CvrRegion& se,
                 CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: fill_up(Region : RegionFill : : )
bool cvr_fill_up(const CvrRegion& r, CvrCoord w, CvrCoord h, CvrRegion& out);

} // namespace cvr
