/*=============================================================================
 * cv_flow/src/ransac.cpp — RANSAC 全部实现（单 TU：表达式模型 + LM + 主循环 + C ABI）
 * 由 cvr.cpp 的 model_expression / optimizer / ransac_core / ransac_interface
 * 四段原样迁出，代码未动。
 *===========================================================================*/

#include "cvflow/ransac.hpp"

#include <muParser.h>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <Eigen/Dense>
#include <unsupported/Eigen/LevenbergMarquardt>
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>

/*===========================================================================
 * 表达式模型（muparser）
 * （原 src/model_expression.h）
 *=========================================================================*/
/*=============================================================================
 * model_expression.h — muparser 动态表达式封装（显式 + 隐式模型）
 *
 * 关键约束：mu::Parser 内部保存的是【变量地址】，拷贝时必须把变量重新绑定到
 * 副本自己的成员上（Eigen::NumericalDiff 会拷贝持有本类的 functor）。
 *
 * 安全：函数白名单、表达式长度限制、总评估次数预算。
 *===========================================================================*/



namespace ransac {

/* muparser 默认没有 pow，注册一个（设计文档表达式大量使用 pow(x,2)） */
inline double ransac_pow(double x, double y) { return std::pow(x, y); }

/* 注：ExprBudgetExceeded / ExprInvalid 已移到公开头 cvr.hpp（消费者按类型捕获） */

class ModelExpression
{
public:
    ModelExpression(const std::string&              expr,
                    const std::string&              xName,
                    const std::string&              yName,
                    const std::vector<std::string>& paramNames,
                    bool                            implicit,
                    int                             maxExprLen,
                    long                            maxEvals)
        : m_expr(expr), m_xName(xName), m_yName(yName),
          m_paramNames(paramNames), m_implicit(implicit),
          m_maxEvals(maxEvals)
    {
        validate_expression(expr, xName, yName, paramNames, maxExprLen);
        // m_params 之后绝不 resize（muparser 保存了元素地址）
        m_params.assign(m_paramNames.size(), 0.0);
        rebind();
        m_parser.Eval();   // 试解析：语法错误/未知变量在此提前抛出 mu::ParserError
    }

    // 拷贝构造：复制配置，重建 parser 并绑定到本副本成员
    ModelExpression(const ModelExpression& other)
        : m_expr(other.m_expr),
          m_xName(other.m_xName),
          m_yName(other.m_yName),
          m_paramNames(other.m_paramNames),
          m_implicit(other.m_implicit),
          m_maxEvals(other.m_maxEvals)
    {
        m_x = other.m_x;
        m_y = other.m_y;
        m_params.assign(other.m_params.begin(), other.m_params.end());
        rebind();
    }

    ModelExpression& operator=(const ModelExpression&) = delete;

    int nParams() const { return static_cast<int>(m_paramNames.size()); }
    bool isImplicit() const { return m_implicit; }
    const std::string& expression() const { return m_expr; }

    /* 显式模型求值 f(x; params) */
    double eval(double x, const double* params) const {
        budget();
        m_x = x;
        for (size_t i = 0; i < m_params.size(); ++i) m_params[i] = params[i];
        return m_parser.Eval();
    }
    double eval(double x, const std::vector<double>& params) const {
        return eval(x, params.data());
    }

    /* 隐式模型求值 F(x, y; params)；显式模型退化为 F(x,y) = y - f(x;params) */
    double evalF(double x, double y, const double* params) const {
        if (!m_implicit) return y - eval(x, params);
        budget();
        m_x = x;
        m_y = y;
        for (size_t i = 0; i < m_params.size(); ++i) m_params[i] = params[i];
        return m_parser.Eval();
    }
    double evalF(double x, double y, const std::vector<double>& params) const {
        return evalF(x, y, params.data());
    }

    /* 几何距离残差（内点判定与精化用）
     * 显式 GEOMETRIC: |y - f(x)| / sqrt(1 + f'(x)^2)
     * 显式 VERTICAL : |y - f(x)|
     * 隐式          : |F(x,y)| / ||grad F(x,y)||，梯度退化 -> +inf */
    double geometric_residual(double x, double y, const double* params,
                              bool useVertical) const {
        if (!m_implicit && useVertical)
            return std::fabs(y - eval(x, params));

        const double fx  = x, fy = y;
        const double f0  = evalF(fx, fy, params);
        if (!std::isfinite(f0)) return std::numeric_limits<double>::infinity();

        if (!m_implicit) {
            // F(x,y) = y - f(x)：dF/dy = 1，dF/dx = -f'(x)
            const double h  = 1e-6 * (std::max)(1.0, std::fabs(fx));
            const double df = (eval(fx + h, params) - eval(fx - h, params)) /
                              (2.0 * h);
            if (!std::isfinite(df)) return std::numeric_limits<double>::infinity();
            const double norm = std::sqrt(1.0 + df * df);
            return std::fabs(f0) / norm;
        }

        // 隐式：数值梯度 (dF/dx, dF/dy)
        const double hx = 1e-6 * (std::max)(1.0, std::fabs(fx));
        const double hy = 1e-6 * (std::max)(1.0, std::fabs(fy));
        const double gx = (evalF(fx + hx, fy, params) -
                           evalF(fx - hx, fy, params)) / (2.0 * hx);
        const double gy = (evalF(fx, fy + hy, params) -
                           evalF(fx, fy - hy, params)) / (2.0 * hy);
        if (!std::isfinite(gx) || !std::isfinite(gy))
            return std::numeric_limits<double>::infinity();
        const double norm = std::sqrt(gx * gx + gy * gy);
        if (norm < 1e-12) return std::numeric_limits<double>::infinity();
        return std::fabs(f0) / norm;
    }

private:
    void rebind() {
        m_parser.SetExpr(m_expr);
        m_parser.DefineFun("pow", &ransac_pow);
        m_parser.DefineVar(m_xName, &m_x);
        if (m_implicit) m_parser.DefineVar(m_yName, &m_y);
        for (size_t i = 0; i < m_paramNames.size(); ++i)
            m_parser.DefineVar(m_paramNames[i], &m_params[i]);
    }

    void budget() const {
        if (m_maxEvals > 0 && ++m_evalCount > m_maxEvals)
            throw ExprBudgetExceeded();
    }

    /* 白名单校验：长度 + 标识符（函数名必须在白名单内，变量名必须是
     * xName/yName/参数名之一；muparser 关键字/常量放行） */
    static void validate_expression(const std::string& expr,
                                    const std::string& xName,
                                    const std::string& yName,
                                    const std::vector<std::string>& paramNames,
                                    int maxExprLen) {
        static const std::set<std::string> kFuncWhitelist = {
            "sin","cos","tan","asin","acos","atan","atan2","sqrt","abs",
            "exp","log","log10","log2","pow","min","max","floor","ceil",
            "sinh","cosh","tanh","sign","rint"
        };
        static const std::set<std::string> kConstWords = { "pi", "e" };

        if (expr.empty())
            throw ExprInvalid("empty model expression");
        if (maxExprLen > 0 && (int)expr.size() > maxExprLen)
            throw ExprInvalid("model expression too long");

        std::set<std::string> vars;
        vars.insert(xName);
        if (!yName.empty()) vars.insert(yName);
        for (const auto& p : paramNames) vars.insert(p);

        size_t i = 0, n = expr.size();
        while (i < n) {
            const char c = expr[i];
            if (std::isalpha((unsigned char)c) || c == '_') {
                size_t j = i + 1;
                while (j < n && (std::isalnum((unsigned char)expr[j]) ||
                                 expr[j] == '_'))
                    ++j;
                const std::string ident = expr.substr(i, j - i);
                // 跳过空白后看是否为函数调用
                size_t k = j;
                while (k < n && std::isspace((unsigned char)expr[k])) ++k;
                if (k < n && expr[k] == '(') {
                    if (kFuncWhitelist.find(ident) == kFuncWhitelist.end())
                        throw ExprInvalid("function not in whitelist: " + ident);
                } else {
                    if (vars.find(ident) == vars.end() &&
                        kConstWords.find(ident) == kConstWords.end())
                        throw ExprInvalid("unknown identifier: " + ident);
                }
                i = j;
            } else {
                ++i;
            }
        }
    }

    std::string              m_expr;
    std::string              m_xName;
    std::string              m_yName;
    std::vector<std::string> m_paramNames;
    bool                     m_implicit;
    long                     m_maxEvals;

    mutable double              m_x = 0.0;
    mutable double              m_y = 0.0;
    mutable std::vector<double> m_params;
    mutable mu::Parser          m_parser;
    mutable long                m_evalCount = 0;
};

} // namespace ransac


/*===========================================================================
 * LM 全局优化（Eigen）
 * （原 src/optimizer.h）
 *=========================================================================*/
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


/*===========================================================================
 * ransac 主循环
 * （原 src/ransac_core.cpp）
 *=========================================================================*/
/*=============================================================================
 * ransac_core.cpp — RANSAC 主循环实现
 *===========================================================================*/


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


/*===========================================================================
 * ransac C ABI 实现
 * （原 src/ransac_interface.cpp）
 *=========================================================================*/
/*=============================================================================
 * ransac_interface.cpp — C ABI 入口：句柄生命周期、参数校验、异常捕获、
 *                        v3.0.0 兼容层
 *===========================================================================*/


namespace {

void set_msg(char* msg, size_t len, const std::string& text) {
    if (!msg || len == 0) return;
    const size_t n = (text.size() < len - 1) ? text.size() : len - 1;
    std::memcpy(msg, text.c_str(), n);
    msg[n] = '\0';
}

bool blank(const char* s) { return !s || s[0] == '\0'; }

} // namespace

/* ---------- 句柄 ---------- */
struct RansacHandleOpaque {
    std::string              expr;
    std::string              xName;
    std::string              yName;
    std::vector<std::string> paramNames;
    RansacModelType          modelType;
};

extern "C" RANSAC_API RansacHandle ransac_create(
    const char*        modelExpression,
    const char*        xName,
    const char*        yName,
    const char* const  paramNames[],
    int                paramCount,
    RansacModelType    modelType,
    char*              statusMessage,
    size_t             statusMessageLen)
{
    set_msg(statusMessage, statusMessageLen, "");

    // ---- 基本校验 ----
    if (blank(modelExpression)) {
        set_msg(statusMessage, statusMessageLen, "empty model expression");
        return nullptr;
    }
    if (blank(xName)) {
        set_msg(statusMessage, statusMessageLen, "empty XName");
        return nullptr;
    }
    if (modelType == RANSAC_MODEL_IMPLICIT && blank(yName)) {
        set_msg(statusMessage, statusMessageLen,
                "implicit model requires YName");
        return nullptr;
    }
    if (!paramNames || paramCount <= 0) {
        set_msg(statusMessage, statusMessageLen, "no params");
        return nullptr;
    }
    for (int j = 0; j < paramCount; ++j) {
        if (blank(paramNames[j])) {
            set_msg(statusMessage, statusMessageLen, "empty param name");
            return nullptr;
        }
        if (std::strcmp(paramNames[j], xName) == 0 ||
            (yName && std::strcmp(paramNames[j], yName) == 0)) {
            set_msg(statusMessage, statusMessageLen,
                    "param name conflicts with variable name");
            return nullptr;
        }
    }

    try {
        std::vector<std::string> names(paramNames, paramNames + paramCount);
        // 试编译：白名单 + 解析错误在此暴露
        ransac::ModelExpression probe(
            modelExpression, xName, yName ? yName : "", names,
            modelType == RANSAC_MODEL_IMPLICIT, 4096, 0);

        auto* h = new (std::nothrow) RansacHandleOpaque();
        if (!h) {
            set_msg(statusMessage, statusMessageLen, "out of memory");
            return nullptr;
        }
        h->expr      = modelExpression;
        h->xName     = xName;
        h->yName     = yName ? yName : "";
        h->paramNames = std::move(names);
        h->modelType = modelType;
        set_msg(statusMessage, statusMessageLen, "OK");
        return h;
    }
    catch (const ransac::ExprInvalid& e) {
        set_msg(statusMessage, statusMessageLen, e.what());
        return nullptr;
    }
    catch (const mu::ParserError& e) {
        set_msg(statusMessage, statusMessageLen,
                std::string("expression parse error: ") + e.GetMsg());
        return nullptr;
    }
    catch (const std::exception& e) {
        set_msg(statusMessage, statusMessageLen, e.what());
        return nullptr;
    }
    catch (...) {
        set_msg(statusMessage, statusMessageLen, "unknown error");
        return nullptr;
    }
}

extern "C" RANSAC_API void ransac_destroy(RansacHandle handle) {
    delete handle;
}

extern "C" RANSAC_API int ransac_get_param_count(RansacHandle handle) {
    return handle ? (int)handle->paramNames.size() : 0;
}

extern "C" RANSAC_API int ransac_get_required_min_sample(RansacHandle handle) {
    return handle ? (int)handle->paramNames.size() : 0;
}

/* ---------- 执行拟合 ---------- */
extern "C" RANSAC_API int ransac_fit(
    RansacHandle         handle,
    const RansacOptions* options,
    const double*        xData,
    const double*        yData,
    int                  pointCount,
    RansacResult*        result)
{
    if (!result) return -1;
    /* 只初始化计算输出字段；不得清空调用方设置的缓冲区指针 */
    result->ResidualSum = 0.0;
    result->InlierRatio = 0.0;
    result->Iterations  = 0;
    result->Status      = RANSAC_ERR_INVALID_ARG;
    if (result->Covariance && result->CovarianceLen > 0)
        std::memset(result->Covariance, 0,
                    result->CovarianceLen * sizeof(double));
    if (result->StatusMessage && result->StatusMessageLen > 0)
        result->StatusMessage[0] = '\0';

    if (!handle) {
        return -1;
    }
    if (!options || !xData || !yData) {
        return -1;
    }
    if (!result->ParamValues ||
        result->ParamValuesLen < handle->paramNames.size() ||
        !result->InlierMask || result->InlierMaskLen < (size_t)pointCount ||
        !result->StatusMessage || result->StatusMessageLen == 0) {
        set_msg(result->StatusMessage, result->StatusMessageLen,
                "output buffer too small or NULL");
        return -1;
    }
    if (options->MinSampleSize < 1 ||
        pointCount < options->MinSampleSize ||
        pointCount < (int)handle->paramNames.size()) {
        set_msg(result->StatusMessage, result->StatusMessageLen,
                "not enough points");
        result->Status = RANSAC_ERR_NOT_ENOUGH_POINTS;
        return -1;
    }
    if (!(options->Threshold > 0.0) || options->MaxIterations <= 0 ||
        !(options->OutlierRatio >= 0.0 && options->OutlierRatio < 1.0) ||
        !(options->Confidence >= 0.0 && options->Confidence < 1.0)) {
        set_msg(result->StatusMessage, result->StatusMessageLen,
                "invalid ransac parameters");
        return -1;
    }

    try {
        ransac::CoreOptions opt;
        opt.modelExpression   = handle->expr;
        opt.xName             = handle->xName;
        opt.yName             = handle->yName;
        opt.paramNames        = handle->paramNames;
        opt.implicit          = (handle->modelType == RANSAC_MODEL_IMPLICIT);
        opt.useVertical       =
            (handle->modelType == RANSAC_MODEL_EXPLICIT &&
             options->ResidualType == RANSAC_RESIDUAL_VERTICAL);
        if (options->InitialValues)
            opt.initialValues.assign(options->InitialValues,
                                     options->InitialValues +
                                         handle->paramNames.size());
        opt.minSampleSize        = options->MinSampleSize;
        opt.threshold            = options->Threshold;
        opt.maxIterations        = options->MaxIterations;
        opt.outlierRatio         = options->OutlierRatio;
        opt.confidence           = (options->Confidence > 0.0)
                                       ? options->Confidence
                                       : 0.99;
        opt.seed                 = options->Seed;
        opt.solverType           = options->SolverType;
        opt.maxSolverIterations  = options->MaxSolverIterations;
        opt.solverTolerance      = options->SolverTolerance;
        opt.enableRefinement     = (options->EnableRefinement != 0);
        opt.maxRefineIterations  = options->MaxRefineIterations;
        opt.maxExpressionLength  = options->MaxExpressionLength;
        opt.maxExpressionEvals   = options->MaxExpressionEvals;
        if (options->Weights)
            opt.weights.assign(options->Weights, options->Weights + pointCount);

        std::vector<double> xs(xData, xData + pointCount);
        std::vector<double> ys(yData, yData + pointCount);

        ransac::CoreResult r = ransac::ransac_run(opt, xs, ys);

        for (size_t j = 0; j < handle->paramNames.size(); ++j)
            result->ParamValues[j] = r.paramValues[j];
        for (int i = 0; i < pointCount; ++i)
            result->InlierMask[i] = r.inlierMask[static_cast<size_t>(i)];
        result->ResidualSum = r.residualSum;
        result->InlierRatio = r.inlierRatio;
        result->Iterations  = r.iterations;
        result->Status      = r.status;
        set_msg(result->StatusMessage, result->StatusMessageLen,
                r.statusMessage);
        return 0;
    }
    catch (const mu::ParserError& e) {
        result->Status = RANSAC_ERR_EXPR_PARSE;
        set_msg(result->StatusMessage, result->StatusMessageLen,
                std::string("expression parse error: ") + e.GetMsg());
        return -1;
    }
    catch (const ransac::ExprInvalid& e) {
        result->Status = RANSAC_ERR_EXPR_PARSE;
        set_msg(result->StatusMessage, result->StatusMessageLen, e.what());
        return -1;
    }
    catch (const std::bad_alloc&) {
        result->Status = RANSAC_ERR_INTERNAL;
        set_msg(result->StatusMessage, result->StatusMessageLen,
                "out of memory");
        return -1;
    }
    catch (const std::exception& e) {
        result->Status = RANSAC_ERR_INTERNAL;
        set_msg(result->StatusMessage, result->StatusMessageLen, e.what());
        return -1;
    }
    catch (...) {
        result->Status = RANSAC_ERR_INTERNAL;
        set_msg(result->StatusMessage, result->StatusMessageLen,
                "unknown error");
        return -1;
    }
}

/* ---------- 便捷单次调用 ---------- */
extern "C" RANSAC_API int ransac_fit_once(
    const char*        modelExpression,
    const char*        xName,
    const char*        yName,
    const char* const  paramNames[],
    int                paramCount,
    RansacModelType    modelType,
    RansacResidualType residualType,
    int                minSampleSize,
    const double*      initialValues,
    const double*      xData,
    const double*      yData,
    int                pointCount,
    double             threshold,
    int                maxIterations,
    double             outlierRatio,
    unsigned int       seed,
    double*            paramValues,       size_t paramValuesLen,
    unsigned char*     inlierMask,        size_t inlierMaskLen,
    double*            residualSum,
    int*               iterations,
    RansacStatus*      status,
    char*              statusMessage,     size_t statusMessageLen)
{
    if (status) *status = RANSAC_ERR_INVALID_ARG;
    if (residualSum) *residualSum = 0.0;
    if (iterations) *iterations = 0;
    set_msg(statusMessage, statusMessageLen, "");

    RansacHandle h = ransac_create(modelExpression, xName, yName, paramNames,
                                   paramCount, modelType, statusMessage,
                                   statusMessageLen);
    if (!h) {
        if (status) *status = RANSAC_ERR_EXPR_PARSE;
        return -1;
    }

    RansacOptions opt = {};
    opt.ModelExpression = modelExpression;
    opt.XName           = xName;
    opt.YName           = yName;
    opt.ParamNames      = paramNames;
    opt.ParamCount      = paramCount;
    opt.InitialValues   = initialValues;
    opt.ModelType       = modelType;
    opt.ResidualType    = residualType;
    opt.MinSampleSize   = minSampleSize;
    opt.Threshold       = threshold;
    opt.MaxIterations   = maxIterations;
    opt.OutlierRatio    = outlierRatio;
    opt.Confidence      = 0.99;
    opt.Seed            = seed;
    opt.SolverType      = RANSAC_SOLVER_AUTO;
    opt.EnableRefinement = 1;

    RansacResult res = {};
    res.ParamValues      = paramValues;
    res.ParamValuesLen   = paramValuesLen;
    res.InlierMask       = inlierMask;
    res.InlierMaskLen    = inlierMaskLen;
    res.StatusMessage    = statusMessage;
    res.StatusMessageLen = statusMessageLen;

    const int ret = ransac_fit(h, &opt, xData, yData, pointCount, &res);
    if (residualSum) *residualSum = res.ResidualSum;
    if (iterations)  *iterations  = res.Iterations;
    if (status)      *status      = res.Status;

    ransac_destroy(h);
    return ret;
}

/* ================= v3.0.0 兼容层 ================= */
namespace {

int generic_fit_impl(
    const char*   ModelExpression,
    const char*   ParamNames[],
    int           nParams,
    const double  InitialValues[],
    const double  XData[],
    const double  YData[],
    int           nPoints,
    const char*   XName,
    double        Threshold,
    int           MaxIter,
    double        OutlierRatio,
    long          Seed,
    double        ParamValues[],
    unsigned char InlierMask[],
    double*       ResidualSum,
    int*          Iterations,
    int*          Status,
    char*         StatusMessage)
{
    if (ParamValues && nParams > 0)
        for (int j = 0; j < nParams; ++j) ParamValues[j] = 0.0;
    if (InlierMask && nPoints > 0)
        std::memset(InlierMask, 0, static_cast<size_t>(nPoints));
    if (ResidualSum) *ResidualSum = 0.0;
    if (Iterations)  *Iterations  = 0;
    if (Status)      *Status      = RANSAC_STATUS_ERROR;
    set_msg(StatusMessage, 256, "");

    RansacStatus st = RANSAC_ERR_INVALID_ARG;
    const int ret = ransac_fit_once(
        ModelExpression, XName, nullptr,
        const_cast<const char**>(ParamNames), nParams,
        RANSAC_MODEL_EXPLICIT, RANSAC_RESIDUAL_VERTICAL,
        nParams /*MinSampleSize = nParams，与 v3 行为一致*/,
        InitialValues, XData, YData, nPoints, Threshold, MaxIter,
        OutlierRatio, (unsigned int)Seed,
        ParamValues, (size_t)nParams, InlierMask, (size_t)nPoints,
        ResidualSum, Iterations, &st, StatusMessage, 256);

    if (Status) {
        // v3 语义：0=成功, 1=未收敛, -1=异常
        if (st == RANSAC_OK) *Status = 0;
        else if (st == RANSAC_ERR_NOT_CONVERGED || st == RANSAC_ERR_MAX_ITER ||
                 st == RANSAC_ERR_SOLVE_FAILED ||
                 st == RANSAC_ERR_SAMPLE_DEGENERATE ||
                 st == RANSAC_ERR_EXPR_TIMEOUT)
            *Status = 1;
        else
            *Status = -1;
    }
    return ret;
}

} // namespace

extern "C" RANSAC_API int ransac_generic_fit(
    const char*   ModelExpression,
    const char*   ParamNames[],
    int           nParams,
    const double  InitialValues[],
    const double  XData[],
    const double  YData[],
    int           nPoints,
    const char*   XName,
    double        Threshold,
    int           MaxIter,
    double        OutlierRatio,
    double        ParamValues[],
    unsigned char InlierMask[],
    double*       ResidualSum,
    int*          Iterations,
    int*          Status,
    char*         StatusMessage)
{
    return generic_fit_impl(ModelExpression, ParamNames, nParams,
                            InitialValues, XData, YData, nPoints, XName,
                            Threshold, MaxIter, OutlierRatio, 0,
                            ParamValues, InlierMask, ResidualSum, Iterations,
                            Status, StatusMessage);
}

extern "C" RANSAC_API int ransac_generic_fit_ex(
    const char*   ModelExpression,
    const char*   ParamNames[],
    int           nParams,
    const double  InitialValues[],
    const double  XData[],
    const double  YData[],
    int           nPoints,
    const char*   XName,
    double        Threshold,
    int           MaxIter,
    double        OutlierRatio,
    long          Seed,
    double        ParamValues[],
    unsigned char InlierMask[],
    double*       ResidualSum,
    int*          Iterations,
    int*          Status,
    char*         StatusMessage)
{
    return generic_fit_impl(ModelExpression, ParamNames, nParams,
                            InitialValues, XData, YData, nPoints, XName,
                            Threshold, MaxIter, OutlierRatio, Seed,
                            ParamValues, InlierMask, ResidualSum, Iterations,
                            Status, StatusMessage);
}

