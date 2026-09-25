/*=============================================================================
 * cvr_measure.cpp — 一维边缘测量实现（cvMeasurePos 自 supply 下沉，行为不变）
 *===========================================================================*/

#include "cvr/cvr_measure.hpp"

#include <algorithm>
#include <cmath>

namespace cvr {

namespace {
const double kPi = 3.14159265358979323846;
}

bool cvr_measure_pos(const std::uint8_t* gray, int width, int height,
                     double column, double row, double phi,
                     double length1, double length2, double sigma,
                     double threshold, int transition,
                     CvrMeasureResult& out)
{
    out.row.clear();
    out.col.clear();
    out.amplitude.clear();

    if (!gray || width < 2 || height < 2) return false;
    if (sigma <= 0.0 || length1 < 1.0) return false;

    // 1) 沿测量矩形抽取一维剖面（双线性采样 + 垂直方向平均）
    int n = 2 * (int)std::round(length1) + 1;
    if (n < 3) return true;   // 参数合法但无可测长度：空结果

    std::vector<double> profile(n, 0.0);

    const double cosA = std::cos(phi);
    const double sinA = std::sin(phi);
    const double ux = cosA,  uy = sinA;   // 主轴单位向量
    const double vx = -sinA, vy = cosA;   // 垂直主轴单位向量
    const int half_w = std::max(0, (int)std::round(length2));

    for (int i = 0; i < n; ++i) {
        const double t = i - (n - 1) * 0.5;
        double sum = 0.0;
        int cnt = 0;
        for (int j = -half_w; j <= half_w; ++j) {
            const double x = column + t * ux + j * vx;
            const double y = row + t * uy + j * vy;

            if (x >= 0.0 && y >= 0.0 &&
                x <= width - 1.0 && y <= height - 1.0)
            {
                int x0 = std::min((int)x, width - 2);
                int y0 = std::min((int)y, height - 2);
                if (x0 < 0) x0 = 0;
                if (y0 < 0) y0 = 0;
                const double dx = x - x0;
                const double dy = y - y0;
                const std::uint8_t* p0 = gray + (size_t)y0 * width;
                const std::uint8_t* p1 = gray + (size_t)(y0 + 1) * width;
                const double v00 = p0[x0];
                const double v01 = p0[x0 + 1];
                const double v10 = p1[x0];
                const double v11 = p1[x0 + 1];
                sum += (1.0 - dy) * ((1.0 - dx) * v00 + dx * v01)
                     +        dy  * ((1.0 - dx) * v10 + dx * v11);
            }
            ++cnt;
        }
        profile[i] = (cnt > 0) ? (sum / cnt) : 0.0;
    }

    // 2) 构造高斯一阶导数核 G'(x)
    const int half_k = std::max(1, (int)std::ceil(3.0 * sigma));
    const int klen   = 2 * half_k + 1;
    std::vector<double> kernel(klen);
    {
        const double s2    = sigma * sigma;
        const double knorm = 1.0 / (std::sqrt(2.0 * kPi) * sigma * s2);
        for (int i = 0; i < klen; ++i) {
            const double x = i - half_k;
            kernel[i] = -x * std::exp(-x * x / (2.0 * s2)) * knorm;
        }
    }

    // 3) 卷积 d = profile * kernel（mode='same'）
    std::vector<double> d(n, 0.0);
    for (int i = 0; i < n; ++i) {
        double sum = 0.0;
        for (int j = 0; j < klen; ++j) {
            const int idx = i - half_k + j;
            if (idx >= 0 && idx < n) sum += profile[idx] * kernel[j];
        }
        d[i] = sum;
    }

    // 4) 找 |d| 的局部极大值 + 振幅阈值过滤
    std::vector<double> ag(n);
    for (int i = 0; i < n; ++i) ag[i] = std::abs(d[i]);

    const double amp_scale = std::sqrt(2.0 * kPi) * sigma;
    std::vector<int>    cand;
    std::vector<double> cand_amp;

    for (int i = 0; i < n; ++i) {
        const bool ge_l = (i > 0)     ? (ag[i] >= ag[i - 1]) : false;
        const bool ge_r = (i < n - 1) ? (ag[i] >= ag[i + 1]) : false;
        const bool gt_l = (i > 0)     ? (ag[i] >  ag[i - 1]) : false;
        const bool gt_r = (i < n - 1) ? (ag[i] >  ag[i + 1]) : false;
        if (ge_l && ge_r && (gt_l || gt_r)) {
            const double amp = d[i] * amp_scale;
            if (std::abs(amp) >= threshold) {
                cand.push_back(i);
                cand_amp.push_back(amp);
            }
        }
    }
    if (cand.empty()) return true;   // 无边缘：空结果

    // 5) 抛物线亚像素插值
    const int m = (int)cand.size();
    std::vector<double> pos(m), amps(m);
    for (int j = 0; j < m; ++j) {
        const int i = cand[j];
        amps[j] = cand_amp[j];
        if (i > 0 && i < n - 1) {
            const double a = d[i - 1];
            const double b = d[i];
            const double c = d[i + 1];
            const double denom = a - 2.0 * b + c;
            double delta = 0.0;
            if (std::abs(denom) > 1e-9) delta = 0.5 * (a - c) / denom;
            pos[j] = (double)i + delta;
        } else {
            pos[j] = (double)i;
        }
    }

    // 6) 按位置排序
    std::vector<int> order(m);
    for (int j = 0; j < m; ++j) order[j] = j;
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return pos[a] < pos[b]; });

    // 7) 去重 + 方向过滤 + 一维位置映射回二维坐标
    const double t0 = -(n - 1) * 0.5;
    double last_pos = -1e18;

    for (int j = 0; j < m; ++j) {
        const int idx = order[j];

        if (pos[idx] - last_pos <= 0.5) continue;
        last_pos = pos[idx];

        const double amp = amps[idx];
        if (transition == 1  && amp <= 0.0) continue;
        if (transition == -1 && amp >= 0.0) continue;

        const double t = t0 + pos[idx];
        out.col.push_back(column + t * ux);
        out.row.push_back(row + t * uy);
        out.amplitude.push_back(amp);
    }

    return true;
}

} // namespace cvr
