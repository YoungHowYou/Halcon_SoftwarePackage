/*=============================================================================
 * test_m9_measure.cpp — cvr_measure_pos 一维边缘测量单元测试
 *   合成阶梯/脉冲边缘图，验证：边缘位置、振幅、极性过滤、参数校验
 *===========================================================================*/

#include "cvr/cvr_measure.hpp"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;   // MSVC <cmath> 默认不定义 M_PI

int g_failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    } else {
        std::printf("ok:   %s\n", what);
    }
}

bool near(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

/* 垂直阶梯边缘图：x < edgeX 为 dark(30)，>= edgeX 为 bright(220) */
std::vector<std::uint8_t> makeStepImage(int w, int h, int edgeX,
                                        std::uint8_t dark = 30,
                                        std::uint8_t bright = 220)
{
    std::vector<std::uint8_t> img((size_t)w * h, dark);
    for (int y = 0; y < h; ++y)
        for (int x = edgeX; x < w; ++x)
            img[(size_t)y * w + x] = bright;
    return img;
}

/* 双边缘（亮脉冲）图：pulseLo..pulseHi 之间为 bright，其余 dark */
std::vector<std::uint8_t> makePulseImage(int w, int h, int pulseLo, int pulseHi,
                                         std::uint8_t dark = 20,
                                         std::uint8_t bright = 240)
{
    std::vector<std::uint8_t> img((size_t)w * h, dark);
    for (int y = 0; y < h; ++y)
        for (int x = pulseLo; x <= pulseHi && x < w; ++x)
            img[(size_t)y * w + x] = bright;
    return img;
}

} // namespace

int main()
{
    using cvr::cvr_measure_pos;
    using cvr::CvrMeasureResult;

    // 1) 垂直正边缘（暗->亮，沿水平线测量）
    //    注意：本实现对暗->亮（上升沿）输出负振幅（kernel 为互相关而非卷积），
    //    transition=1 保留亮->暗（amp>0），transition=-1 保留暗->亮（amp<0）。
    //    这是原 supply 实现的固有语义，下沉保持原样。
    {
        const int W = 200, H = 100, EDGE = 100;
        auto img = makeStepImage(W, H, EDGE);

        CvrMeasureResult r0;
        check(cvr_measure_pos(img.data(), W, H,
                              100.0, 50.0, 0.0,   // 中心在边缘上，phi=0 沿水平
                              60.0, 8.0,          // length1=60（覆盖边缘）, length2=8
                              1.0, 20.0, 0, r0),
              "step edge: call ok");
        check(r0.row.size() == 1, "step edge: exactly 1 edge (transition=0)");
        if (r0.row.size() == 1) {
            check(near(r0.col[0], EDGE, 0.6), "step edge: col near edge (subpixel)");
            check(near(r0.row[0], 50.0, 0.6), "step edge: row near center line");
            check(std::fabs(r0.amplitude[0]) > 50.0,
                  "step edge: |amplitude| sizable (~190)");
        }

        CvrMeasureResult rp;
        cvr_measure_pos(img.data(), W, H, 100.0, 50.0, 0.0,
                        60.0, 8.0, 1.0, 20.0, 1, rp);
        check(rp.row.empty(), "transition=1 keeps bright->dark only (filters this edge)");

        CvrMeasureResult rn;
        cvr_measure_pos(img.data(), W, H, 100.0, 50.0, 0.0,
                        60.0, 8.0, 1.0, 20.0, -1, rn);
        check(rn.row.size() == 1, "transition=-1 keeps dark->bright edge");
    }

    // 2) 双边缘（亮脉冲）：应检出 2 条边缘，排序后左负右正
    {
        const int W = 200, H = 100, LO = 60, HI = 140;
        auto img = makePulseImage(W, H, LO, HI);

        CvrMeasureResult r;
        cvr_measure_pos(img.data(), W, H, 100.0, 50.0, 0.0,
                        80.0, 8.0, 1.0, 20.0, 0, r);
        check(r.row.size() == 2, "pulse: exactly 2 edges");
        if (r.row.size() == 2) {
            check(r.col[0] < r.col[1], "pulse: edges sorted by position");
            check(near(r.col[0], LO, 1.0), "pulse: left edge near LO");
            check(near(r.col[1], HI + 1, 1.0), "pulse: right edge near HI");
            check(r.amplitude[0] < 0.0, "pulse: left edge negative (dark->bright)");
            check(r.amplitude[1] > 0.0, "pulse: right edge positive (bright->dark)");
        }
    }

    // 3) phi=pi/2 垂直方向测量（沿列方向）
    {
        const int W = 100, H = 200, EDGE = 120;
        // 水平边缘图：y < EDGE 为 dark，>= 为 bright
        std::vector<std::uint8_t> img((size_t)W * H, 30);
        for (int y = EDGE; y < H; ++y)
            for (int x = 0; x < W; ++x)
                img[(size_t)y * W + x] = 220;

        CvrMeasureResult r;
        cvr_measure_pos(img.data(), W, H, 50.0, 120.0, kPi / 2.0,
                        60.0, 8.0, 1.0, 20.0, 0, r);
        check(r.row.size() == 1, "horizontal edge via phi=pi/2: 1 edge");
        if (r.row.size() == 1)
            check(near(r.row[0], EDGE, 0.8), "horizontal edge: row near EDGE");
    }

    // 4) 参数校验：sigma<=0 / length1<1 / 空图 -> false 且输出为空
    {
        const int W = 50, H = 50;
        std::vector<std::uint8_t> img((size_t)W * H, 128);
        CvrMeasureResult r;
        check(!cvr_measure_pos(img.data(), W, H, 25, 25, 0.0,
                               10, 4, 0.0, 10, 0, r) && r.row.empty(),
              "invalid sigma rejected");
        check(!cvr_measure_pos(img.data(), W, H, 25, 25, 0.0,
                               0.5, 4, 1.0, 10, 0, r) && r.row.empty(),
              "invalid length1 rejected");
        check(!cvr_measure_pos(nullptr, W, H, 25, 25, 0.0,
                               10, 4, 1.0, 10, 0, r),
              "null image rejected");
    }

    // 5) 高阈值下无边缘 -> 空结果但返回 true
    {
        const int W = 100, H = 50, EDGE = 50;
        auto img = makeStepImage(W, H, EDGE);
        CvrMeasureResult r;
        check(cvr_measure_pos(img.data(), W, H, 50.0, 25.0, 0.0,
                              30.0, 6.0, 1.0, 1e9, 0, r) && r.row.empty(),
              "huge threshold -> empty result, ok=true");
    }

    if (g_failures == 0) std::printf("ALL PASSED\n");
    return g_failures == 0 ? 0 : 1;
}
