#pragma once
/*=============================================================================
 * optimizer.h — 候选参数求解（显式/隐式、线性/非线性、权重）
 *
 * 策略：
 *   1. 数值检测表达式对参数是否线性（显式/隐式通用）；
 *   2. 线性：构建设计矩阵，Eigen 最小二乘（加权）精确求解；
 *   3. 非线性：Eigen LevenbergMarquardt + NumericalDiff；隐式模型用
 *      Taubin 几何归一化残差 F/||grad F||；
 *   4. 失败（奇异/NaN/Inf/不收敛）返回 false，调用方继续下一次 RANSAC 迭代。
 *===========================================================================*/

#include "model_expression.h"
#include "cvr/ransac_interface.h"

#include <Eigen/Dense>
#include <unsupported/Eigen/LevenbergMarquardt>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ransac {

/* LM functor：持有拷贝安全的 ModelExpression（见 model_expression.h 注释） */
struct LmExprFunctor : public Eigen::DenseFunctor<double>
{
    LmExprFunctor(const ModelExpression& model,
                  std::vector<double>    xs,
                  std::vector<double>    ys,
                  std::vector<double>    ws)
        : Eigen::DenseFunctor<double>(model.nParams(),
                                      static_cast<int>(xs.size())),
          m_model(model),
          m_xs(std::move(xs)), m_ys(std::move(ys)), m_ws(std::move(ws))
    {}

    int operator()(const InputType& params, ValueType& fvec) const
    {
        const bool implicit = m_model.isImplicit();
        for (int i = 0; i < m_inputs; ++i) {
            const double x = m_xs[static_cast<size_t>(i)];
            const double y = m_ys[static_cast<size_t>(i)];
            double r;
            if (!implicit) {
                r = y - m_model.eval(x, params.data());
            } else {
                // Taubin 几何归一化：F / ||grad F||
                const double f0 = m_model.evalF(x, y, params.data());
                if (!std::isfinite(f0)) { fvec(i) = 1e12; continue; }
                const double hx = 1e-6 * (std::max)(1.0, std::fabs(x));
                const double hy = 1e-6 * (std::max)(1.0, std::fabs(y));
                const double gx = (m_model.evalF(x + hx, y, params.data()) -
                                   m_model.evalF(x - hx, y, params.data())) /
                                  (2.0 * hx);
                const double gy = (m_model.evalF(x, y + hy, params.data()) -
                                   m_model.evalF(x, y - hy, params.data())) /
                                  (2.0 * hy);
                const double norm = std::sqrt(gx * gx + gy * gy);
                r = (norm < 1e-12) ? f0 * 1e12 : f0 / norm;
            }
            const double w = (i < (int)m_ws.size()) ? m_ws[static_cast<size_t>(i)]
                                                    : 1.0;
            fvec(i) = r * (w > 0.0 ? std::sqrt(w) : 0.0);
        }
        return 0;
    }

    ModelExpression     m_model;
    std::vector<double> m_xs;
    std::vector<double> m_ys;
    std::vector<double> m_ws;
};

/* 数值线性检测：f(2p) ≈ 2 f(p) - f(0)，多点抽查（显式/隐式通用） */
inline bool is_linear_in_params(ModelExpression&       model,
                                const std::vector<double>& xs,
                                const std::vector<double>& ys,
                                const std::vector<double>& init)
{
    const int nParams = model.nParams();
    std::vector<double> p0(static_cast<size_t>(nParams), 0.0);
    std::vector<double> p1(static_cast<size_t>(nParams), 0.0);
    std::vector<double> p2(static_cast<size_t>(nParams), 0.0);
    for (int j = 0; j < nParams; ++j) {
        const double base = (j < (int)init.size() && init[j] != 0.0)
                                ? init[j]
                                : 1.0 + 0.37 * j;
        p1[static_cast<size_t>(j)] = base;
        p2[static_cast<size_t>(j)] = 2.0 * base;
    }

    const int checks = xs.size() < 3 ? (int)xs.size() : 3;
    for (int k = 0; k < checks; ++k) {
        const double x = xs[static_cast<size_t>(k)];
        const double y = ys[static_cast<size_t>(k)];
        double f0, f1, f2;
        if (model.isImplicit()) {
            f0 = model.evalF(x, y, p0);
            f1 = model.evalF(x, y, p1);
            f2 = model.evalF(x, y, p2);
        } else {
            f0 = model.eval(x, p0);
            f1 = model.eval(x, p1);
            f2 = model.eval(x, p2);
        }
        const double expect = 2.0 * f1 - f0;
        const double scale  = (std::max)(1.0, std::fabs(expect));
        if (!std::isfinite(f0) || !std::isfinite(f1) || !std::isfinite(f2))
            return false;
        if (std::fabs(f2 - expect) > 1e-6 * scale) return false;
    }
    return true;
}

/* 线性模型精确求解（可对超定方程组，加权） */
inline bool solve_linear(ModelExpression&            model,
                         const std::vector<double>&  xs,
                         const std::vector<double>&  ys,
                         const std::vector<double>&  ws,
                         std::vector<double>&        outParams)
{
    const int nParams = model.nParams();
    const int n       = static_cast<int>(xs.size());
    const bool implicit = model.isImplicit();
    // 隐式齐次模型（直线/圆锥，差一缩放）只需 nParams-1 个点即可定解；
    // 显式/非齐次需要 nParams 个点
    const int needPts = implicit ? nParams - 1 : nParams;
    if (n < needPts) return false;

    std::vector<double> p0(static_cast<size_t>(nParams), 0.0);
    Eigen::MatrixXd     G(n, nParams);
    Eigen::VectorXd     b(n);

    for (int i = 0; i < n; ++i) {
        const double x = xs[static_cast<size_t>(i)];
        const double y = ys[static_cast<size_t>(i)];
        double g0;
        if (implicit) {
            g0   = model.evalF(x, y, p0);
            b(i) = -g0;
        } else {
            g0   = model.eval(x, p0);
            b(i) = y - g0;
        }
        for (int j = 0; j < nParams; ++j) {
            std::vector<double> ej(static_cast<size_t>(nParams), 0.0);
            ej[static_cast<size_t>(j)] = 1.0;
            G(i, j) = (implicit ? model.evalF(x, y, ej) : model.eval(x, ej)) - g0;
        }
        // 加权：sqrt(w) 乘到该行
        const double w = (i < (int)ws.size()) ? ws[static_cast<size_t>(i)] : 1.0;
        const double sw = (w > 0.0) ? std::sqrt(w) : 0.0;
        b(i) *= sw;
        for (int j = 0; j < nParams; ++j) G(i, j) *= sw;
    }

    Eigen::VectorXd p;
    if (implicit) {
        // 隐式线性模型：G p = b。若 b≈0（齐次，如直线/圆锥一般式），
        // 最小二乘给零解，必须取最小奇异值对应的右奇异向量（零空间方向）。
        if (b.norm() < 1e-10 * (std::max)(1.0, (double)n)) {
            Eigen::JacobiSVD<Eigen::MatrixXd> svd(G, Eigen::ComputeFullV);
            if (svd.matrixV().cols() < nParams || svd.singularValues().size() < 1)
                return false;
            p = svd.matrixV().col(nParams - 1);
        } else {
            p = G.colPivHouseholderQr().solve(b);
        }
    } else {
        p = G.colPivHouseholderQr().solve(b);
    }
    if (p.size() != nParams) return false;
    for (int j = 0; j < nParams; ++j) {
        if (!std::isfinite(p(j))) return false;
        outParams[static_cast<size_t>(j)] = p(j);
    }
    return true;
}

/* 非线性模型 LM 求解；maxfev 限制每次 RANSAC 迭代内的优化开销 */
inline bool solve_nonlinear(ModelExpression&            model,
                            const std::vector<double>&  xs,
                            const std::vector<double>&  ys,
                            const std::vector<double>&  ws,
                            const std::vector<double>&  init,
                            int                         maxfev,
                            double                      tolerance,
                            std::vector<double>&        outParams)
{
    const int nParams = model.nParams();
    if ((int)xs.size() < nParams) return false;

    LmExprFunctor functor(model, xs, ys, ws);
    Eigen::NumericalDiff<LmExprFunctor> numDiff(functor, 0.0);
    Eigen::LevenbergMarquardt<Eigen::NumericalDiff<LmExprFunctor> > lm(numDiff);
    if (maxfev > 0) lm.setMaxfev((Eigen::Index)maxfev);   // Eigen 5.x setter
    const double tol = (tolerance > 0.0) ? tolerance : 1e-8;
    lm.setFtol(tol);
    lm.setXtol(tol);
    lm.setGtol(0.0);

    Eigen::VectorXd p(nParams);
    for (int j = 0; j < nParams; ++j)
        p(j) = (j < (int)init.size()) ? init[j] : 0.0;

    lm.minimize(p);

    for (int j = 0; j < nParams; ++j) {
        if (!std::isfinite(p(j))) return false;
        outParams[static_cast<size_t>(j)] = p(j);
    }
    return true;
}

/* 统一入口：按 SolverType 分派；AUTO 用线性检测结果 */
inline bool solve_params(ModelExpression&            model,
                         const std::vector<double>&  xs,
                         const std::vector<double>&  ys,
                         const std::vector<double>&  ws,
                         const std::vector<double>&  init,
                         RansacSolverType            solverType,
                         int                         maxfev,
                         double                      tolerance,
                         std::vector<double>&        outParams)
{
    bool linear = (solverType == RANSAC_SOLVER_LINEAR);
    if (solverType == RANSAC_SOLVER_AUTO)
        linear = is_linear_in_params(model, xs, ys, init);

    if (linear) return solve_linear(model, xs, ys, ws, outParams);
    return solve_nonlinear(model, xs, ys, ws, init, maxfev, tolerance,
                           outParams);
}

} // namespace ransac
