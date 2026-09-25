#pragma once
#include "cvr/cvr_region.hpp"
#include <vector>

namespace cvr {

// ====== 集合代数 ======
// 所有涉及补集的算子都需要定义域 (w,h) 以在必要时物化补集。
// 输出 is_compl 恒为 false（complement 除外，它只翻转标记）。

// Halcon: union1(Regions : RegionUnion : : )
bool cvr_union1(const std::vector<CvrRegion>& regions,
                CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: union2(Region1, Region2 : RegionUnion : : )
bool cvr_union2(const CvrRegion& r1, const CvrRegion& r2,
                CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: intersection(Region1, Region2 : RegionIntersection : : )
bool cvr_intersection(const CvrRegion& r1, const CvrRegion& r2,
                      CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: difference(Region1, Region2 : RegionDifference : : )
// out = r1 - r2
bool cvr_difference(const CvrRegion& r1, const CvrRegion& r2,
                    CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: complement(Region : RegionComplement : : )
bool cvr_complement(const CvrRegion& r, CvrRegion& out);

// Halcon: symm_difference(Region1, Region2 : RegionDifference : : )
bool cvr_symm_difference(const CvrRegion& r1, const CvrRegion& r2,
                         CvrCoord w, CvrCoord h, CvrRegion& out);

} // namespace cvr
