#pragma once
#include "cvr/cvr_region.hpp"
#include <string>

namespace cvr {

// Halcon shape_trans 支持的形状
constexpr const char* CVR_SHAPE_RECTANGLE1 = "rectangle1";
constexpr const char* CVR_SHAPE_RECTANGLE2 = "rectangle2";
constexpr const char* CVR_SHAPE_ELLIPSE    = "ellipse";
constexpr const char* CVR_SHAPE_OUTER_CIRCLE = "outer_circle";
constexpr const char* CVR_SHAPE_CONVEX     = "convex";

// Halcon: shape_trans(Region : RegionTrans : Shape : )
// Shape: "rectangle1" / "rectangle2" / "ellipse" / "outer_circle" / "convex"
bool cvr_shape_trans(const CvrRegion& r, const std::string& shape, CvrRegion& out);

} // namespace cvr
