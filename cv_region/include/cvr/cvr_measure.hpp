#pragma once
/*=============================================================================
 * cvr_measure.hpp — 一维边缘测量（HALCON measure_pos 语义子集）
 *
 * 算法流程（与 HALCON 一致的核心步骤）：
 *   1) 沿测量矩形主轴抽取一维灰度剖面（双线性采样 + 垂直方向平均）
 *   2) 构造高斯一阶导数核 G'(x; sigma)
 *    3) 剖面与核卷积（mode='same'）
 *   4) |d| 局部极大值 + 振幅阈值过滤
 *   5) 抛物线亚像素插值
 *   6) 按位置排序、去重（间距 <= 0.5 像素合并）
 *   7) 方向过滤（transition: 1 仅正边缘 / -1 仅负边缘 / 0 全部），映射回 2D 坐标
 *
 * 输入为 8 位单通道灰度图（raw 字节缓冲，行优先），不依赖 OpenCV。
 *===========================================================================*/

#include <cstdint>
#include <vector>

namespace cvr {

/* 测量结果：边缘点的行/列（亚像素）与振幅（一阶梯度幅值） */
struct CvrMeasureResult {
    std::vector<double> row;
    std::vector<double> col;
    std::vector<double> amplitude;
};

/* 参数语义与 HALCON measure_pos 对应：
 *   (column, row) 矩形中心；phi 主轴方向（弧度）；
 *   length1/length2 矩形半长/半宽（像素）；sigma 高斯平滑；
 *   threshold 振幅阈值；transition 极性（1 正 / -1 负 / 0 全部）
 * 返回 false 表示参数非法（sigma<=0、length1<1 等），结果为空向量。 */
bool cvr_measure_pos(const std::uint8_t* gray, int width, int height,
                     double column, double row, double phi,
                     double length1, double length2, double sigma,
                     double threshold, int transition,
                     CvrMeasureResult& out);

} // namespace cvr
