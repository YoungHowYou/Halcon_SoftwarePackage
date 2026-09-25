#pragma once
#include "cvr/cvr_region.hpp"
#include <string>
#include <vector>

namespace cvr {

// Halcon: select_shape(Regions : SelectedRegions : Features, Operation, Min, Max : )
// op: "and" / "or"
bool cvr_select_shape(const std::vector<CvrRegion>& regions,
                      const std::vector<std::string>& features,
                      const std::string& op,
                      const std::vector<double>& mins,
                      const std::vector<double>& maxs,
                      std::vector<CvrRegion>& selected);

// 便捷接口：按单个特征筛选
bool cvr_select_shape_single(const std::vector<CvrRegion>& regions,
                             const std::string& feature,
                             double min_val, double max_val,
                             std::vector<CvrRegion>& selected);

} // namespace cvr
