#pragma once

#ifdef CVR_WITH_OPENCV
#include "cvr/cvr_region.hpp"
#include <opencv2/core.hpp>

namespace cvr {

// 将 CvrRegion 渲染为 CV_8U 二值 mask（前景=255，背景=0）
// 若 r.is_compl=true，则按定义域 [w,h] 物化后渲染
bool cvr_region_to_mask(const CvrRegion& r, CvrCoord w, CvrCoord h,
                        cv::Mat& mask);

// 从 CV_8U mask 提取 region；mask 中非零视为前景
// 可选返回 mask 的宽高
bool cvr_region_from_mask(const cv::Mat& mask, CvrRegion& r,
                          CvrCoord* out_w = nullptr,
                          CvrCoord* out_h = nullptr);

} // namespace cvr

#endif // CVR_WITH_OPENCV
