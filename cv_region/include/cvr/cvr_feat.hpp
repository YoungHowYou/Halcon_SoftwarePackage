#pragma once
#include "cvr/cvr_region.hpp"
#include <string>
#include <vector>

namespace cvr {

// ----------------------------------------------------------------------------
// 形状特征计算（惰性缓存由调用方通过 CvrRegion.feature 维护）
// 所有函数：输入 region 必须已 normalize；输出 is_compl=false
// ----------------------------------------------------------------------------

// 面积与重心
bool cvr_feature_area_center(const CvrRegion& r,
                             double& row, double& col, CvrChords& area);

// 二阶中心矩
bool cvr_feature_moments(const CvrRegion& r,
                         double& m11, double& m20, double& m02);

// 椭圆等效轴：ra（长半轴）、rb（短半轴）、phi（主轴与列轴夹角，弧度）
bool cvr_feature_elliptic_axis(const CvrRegion& r,
                               double& ra, double& rb, double& phi);

// 轮廓长度（含 sqrt(2) 对角修正）
bool cvr_feature_contlength(const CvrRegion& r, double& contlength);

// 凸包 + 凸度
bool cvr_feature_convexity(const CvrRegion& r, double& convexity, bool& is_convex);

// 轴对齐包围盒（rectangle1）
bool cvr_feature_smallest_rectangle1(const CvrRegion& r,
                                     CvrCoord& row1, CvrCoord& col1,
                                     CvrCoord& row2, CvrCoord& col2);

// 最小外接旋转矩形（rectangle2）
bool cvr_feature_smallest_rectangle2(const CvrRegion& r,
                                     double& row, double& col,
                                     double& phi, double& length1, double& length2);

// 最小外接圆
bool cvr_feature_smallest_circle(const CvrRegion& r,
                                 double& row, double& col, double& radius);

// 紧凑度 / 圆度（Halcon compactness = contlength^2 / (4*pi*area)）
bool cvr_feature_compactness(const CvrRegion& r, double& compactness);
bool cvr_feature_circularity(const CvrRegion& r, double& circularity);

// 矩形度
bool cvr_feature_rectangularity(const CvrRegion& r, double& rectangularity);

// 按名称查特征（Halcon 风格小写）
// 支持的名称：
//   "area", "row", "column", "width", "height",
//   "row1","column1","row2","column2",
//   "row_rect","column_rect","phi_rect","length1","length2",
//   "row_circle","column_circle","radius",
//   "contlength", "convexity", "compactness", "circularity", "rectangularity",
//   "anisometry", "bulkiness", "structure_factor",
//   "phi", "ra", "rb"
bool cvr_get_feature(const CvrRegion& r, const std::string& name, double& value);

// 批量按名称取特征，结果按行输出 [N_regions x N_features]
bool cvr_region_features(const std::vector<CvrRegion>& regions,
                         const std::vector<std::string>& names,
                         std::vector<double>& values);

} // namespace cvr
