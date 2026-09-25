/*=============================================================================
 * ransac_core.cpp — RANSAC 主循环实现
 *===========================================================================*/
#include "ransac_core.h"
#include "cvr/ransac_interface.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace ransac {

namespace {

/* 理论迭代次数：N = log(1-p) / log(1-(1-e)^s)
 * 边界：e<=0 或公式退化 -> maxIterations */
long estimate_iterations(int minSample, double outlierRatio, double confidence,
                         int maxIterations) {
    if (outlierRatio <= 0.0) return maxIterations;
    const double p = (confidence > 0.0 && confidence < 1.0) ? confidence : 0.99;
    const double s = static_cast<double>(minSample);
    const double oneMinusE = 1.0 - outlierRatio;
    const double base = std::pow(oneMinusE, s);
    if (base <= 0.0) return maxIterations;
    const double denom = std::log(1.0 - base);
    if (denom >= 0.0 || !std::isfinite(denom)) return maxIterations;
    const double n = std::log(1.0 - p) / denom;
    if (!std::isfinite(n) || n < 1.0) return 1;
    const long long est = static_cast<long long>(std::ceil(n));
    if (est <= 0) return 1;
    return est;
}

/* 内点统计：r_i = 几何距离；内点判定 r_i <= threshold；加权残差平方和 */
int compute_inliers(ModelExpression& model,
                    const std::vector<double>& params,
                    const std::vector<double>& xData,
                    const std::vector<double>& yData,
                    const std::vector<double>& weights,
                    bool useVertical,
                    double threshold,
                    std::vector<unsigned char>& mask,
                    double& residualSum)
{
    const int n = static_cast<int>(xData.size());
    mask.assign(static_cast<size_t>(n), 0);
    residualSum = 0.0;
    int inliers = 0;
    for (int i = 0; i < n; ++i) {
        const double r = model.geometric_residual(
            xData[static_cast<size_t>(i)], yData[static_cast<size_t>(i)],
            params.data(), useVertical);
        if (std::isfinite(r) && r <= threshold) {
            mask[static_cast<size_t>(i)] = 1;
            const double w = (i < (int)weights.size())
                                 ? weights[static_cast<size_t>(i)]
                                 : 1.0;
            residualSum += w * r * r;
            ++inliers;
        }
    }
    return inliers;
}

/* 采样退化检查：采样点间最小距离过小则视为退化 */
bool is_sample_degenerate(const std::vector<int>& idx,
                          const std::vector<double>& xData,
                          const std::vector<double>& yData) {
    const int s = static_cast<int>(idx.size());
    for (int a = 0; a < s; ++a) {
        for (int b = a + 1; b < s; ++b) {
            const double dx = xData[static_cast<size_t>(idx[a])] -
                              xData[static_cast<size_t>(idx[b])];
            const double dy = yData[static_cast<size_t>(idx[a])] -
                              yData[static_cast<size_t>(idx[b])];
            if (dx * dx + dy * dy > 1e-24) return false;   // 存在不同点
        }
    }
    return true;   // 全部点重合
}

} // namespace

CoreResult ransac_run(const CoreOptions& opt,
                      const std::vector<double>& xData,
                      const std::vector<double>& yData)
{
    CoreResult res;
    res.residualSum = 0.0;
    res.inlierRatio = 0.0;
    res.iterations  = 0;
    res.status      = RANSAC_ERR_NOT_CONVERGED;

    const int nParams = static_cast<int>(opt.paramNames.size());
    const int n       = static_cast<int>(xData.size());

    ModelExpression model(opt.modelExpression, opt.xName, opt.yName,
                          opt.paramNames, opt.implicit,
                          opt.maxExpressionLength, opt.maxExpressionEvals);

    std::vector<double> init(static_cast<size_t>(nParams), 0.0);
    for (int j = 0; j < nParams && j < (int)opt.initialValues.size(); ++j)
        init[static_cast<size_t>(j)] = opt.initialValues[static_cast<size_t>(j)];

    const bool useVertical = (!opt.implicit) && opt.useVertical;

    // 随机数发生器（局部对象，可重入；seed>0 时可复现）
    std::mt19937 rng;
    if (opt.seed != 0) {
        rng.seed(opt.seed);
    } else {
        std::random_device rd;
        rng.seed(rd());
    }

    std::vector<int> indices(static_cast<size_t>(n));
    std::iota(indices.begin(), indices.end(), 0);

    long nUse = estimate_iterations(opt.minSampleSize, opt.outlierRatio,
                                    opt.confidence, opt.maxIterations);
    if (nUse > opt.maxIterations) nUse = opt.maxIterations;
    if (nUse < 1) nUse = 1;

    const int needInl = (std::max)(
        opt.minSampleSize,
        static_cast<int>(std::ceil(n * (1.0 - opt.outlierRatio))));

    std::vector<double> bestParams = init;
    int                 bestInliers = -1;

    int consecutiveDegenerate = 0;
    int totalSolveFails       = 0;
    bool hitMaxIter           = false;

    int iter = 0;
    try {
        for (iter = 1; iter <= nUse; ++iter) {
            // 随机不重复采样 minSampleSize 个点，退化则重试
            std::vector<double> sx, sy, sw;
            bool sampled = false;
            for (int retry = 0; retry < 3 && !sampled; ++retry) {
                std::shuffle(indices.begin(), indices.end(), rng);
                std::vector<int> sampleIdx(
                    indices.begin(), indices.begin() + opt.minSampleSize);
                if (is_sample_degenerate(sampleIdx, xData, yData)) {
                    ++consecutiveDegenerate;
                    continue;
                }
                sx.clear();
                sy.clear();
                sw.clear();
                for (int j = 0; j < opt.minSampleSize; ++j) {
                    const int idx = sampleIdx[static_cast<size_t>(j)];
                    sx.push_back(xData[static_cast<size_t>(idx)]);
                    sy.push_back(yData[static_cast<size_t>(idx)]);
                    if (idx < (int)opt.weights.size())
                        sw.push_back(opt.weights[static_cast<size_t>(idx)]);
                }
                sampled = true;
            }
            if (!sampled) continue;

            // 候选参数求解；失败则丢弃该候选继续下一次
            std::vector<double> cand(static_cast<size_t>(nParams), 0.0);
            const int maxfev =
                (opt.maxSolverIterations > 0) ? opt.maxSolverIterations
                                              : 50 + 20 * nParams;
            if (!solve_params(model, sx, sy, sw, init, opt.solverType, maxfev,
                              opt.solverTolerance, cand)) {
                ++totalSolveFails;
                continue;
            }

            // 全量残差统计
            std::vector<unsigned char> mask;
            double rss = 0.0;
            const int inliers =
                compute_inliers(model, cand, xData, yData, opt.weights,
                                useVertical, opt.threshold, mask, rss);

            if (inliers > bestInliers) {
                bestInliers = inliers;
                bestParams  = cand;

                // 动态迭代更新：用当前最佳内点比例重估剩余预算
                const double eCur = 1.0 - (double)inliers / n;
                const long nDyn = estimate_iterations(
                    opt.minSampleSize, eCur, opt.confidence, opt.maxIterations);
                if (iter + nDyn < nUse) nUse = iter + nDyn;
            }

            // 达到期望内点数，提前终止
            if (inliers >= needInl) break;
        }
        hitMaxIter = (iter > nUse);
    }
    catch (const ExprBudgetExceeded&) {
        res.status = RANSAC_ERR_EXPR_TIMEOUT;
        res.statusMessage = "Expression eval budget exceeded";
    }

    // 最终精化（若启用且已有可用最佳模型）
    if (opt.enableRefinement && bestInliers >= nParams) {
        std::vector<unsigned char> mask;
        double rss = 0.0;
        compute_inliers(model, bestParams, xData, yData, opt.weights,
                        useVertical, opt.threshold, mask, rss);

        const int maxRefine =
            (opt.maxRefineIterations > 0) ? opt.maxRefineIterations : 5;
        for (int k = 0; k < maxRefine; ++k) {
            std::vector<double> ix, iy, iw;
            for (int i = 0; i < n; ++i) {
                if (mask[static_cast<size_t>(i)]) {
                    ix.push_back(xData[static_cast<size_t>(i)]);
                    iy.push_back(yData[static_cast<size_t>(i)]);
                    if (i < (int)opt.weights.size())
                        iw.push_back(opt.weights[static_cast<size_t>(i)]);
                }
            }
            std::vector<double> refined(static_cast<size_t>(nParams), 0.0);
            const int maxfev = 200 + 40 * nParams;
            if (!solve_params(model, ix, iy, iw, bestParams, opt.solverType,
                              maxfev, opt.solverTolerance, refined))
                break;   // 精化失败，回退到当前 bestParams

            std::vector<unsigned char> newMask;
            double newRss = 0.0;
            compute_inliers(model, refined, xData, yData, opt.weights,
                            useVertical, opt.threshold, newMask, newRss);
            bestParams = std::move(refined);
            if (newMask == mask) { mask = std::move(newMask); break; }
            mask = std::move(newMask);
        }
    }

    // 最终内点掩码与残差
    double rss = 0.0;
    const int finalInliers =
        compute_inliers(model, bestParams, xData, yData, opt.weights,
                        useVertical, opt.threshold, res.inlierMask, rss);

    res.paramValues = std::move(bestParams);
    res.residualSum = rss;
    res.inlierRatio = (n > 0) ? (double)finalInliers / n : 0.0;
    res.iterations  = iter > nUse ? (int)nUse : iter;

    // 状态码判定（ERR_EXPR_TIMEOUT 已在 catch 中设置则不覆盖）
    if (res.status == RANSAC_ERR_EXPR_TIMEOUT) {
        // 保持
    } else if (finalInliers >= needInl) {
        res.status        = RANSAC_OK;
        res.statusMessage = "Success";
    } else if (consecutiveDegenerate > 0 && bestInliers < nParams) {
        res.status        = RANSAC_ERR_SAMPLE_DEGENERATE;
        res.statusMessage = "Persistent degenerate samples";
    } else if (bestInliers < nParams && totalSolveFails > 0) {
        res.status        = RANSAC_ERR_SOLVE_FAILED;
        res.statusMessage = "Candidate solve failed repeatedly";
    } else if (hitMaxIter && nUse >= opt.maxIterations) {
        res.status        = RANSAC_ERR_MAX_ITER;
        res.statusMessage = "Max iterations reached (best candidate returned)";
    } else {
        res.status        = RANSAC_ERR_NOT_CONVERGED;
        res.statusMessage =
            "Convergence failed: not enough inliers (best candidate returned)";
    }
    return res;
}

} // namespace ransac
