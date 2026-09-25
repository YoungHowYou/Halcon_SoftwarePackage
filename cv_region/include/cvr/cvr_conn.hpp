#pragma once
#include "cvr/cvr_region.hpp"
#include <vector>

namespace cvr {

// Halcon: connection(Region : ConnectedRegions : : )
// connectivity: 4 或 8（默认 8，和 Halcon 一致）
bool cvr_connection(const CvrRegion& r, int connectivity,
                    CvrCoord w, CvrCoord h,
                    std::vector<CvrRegion>& out);

} // namespace cvr
