/*=============================================================================
 * cv_flow/src/fit.cpp — 两步式拟合实现（建模一次解析，拟合零解析）
 *
 * 旧一步到位入口（lm_fit / lm_fit_2d / linear_fit_solve）已随算子重构删除；
 * qrColPivSolve（列主元 Householder QR）为线性拟合共用件，原样保留。
 *===========================================================================*/
#include "cvflow/fit.hpp"

#include <muParser.h>
#include <Eigen/Dense>
#include <unsupported/Eigen/LevenbergMarquardt>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <opencv2/core.hpp>

namespace cvflow {
namespace {

/* muparser 默认没有 pow，注册一个（与 ransac 的 ModelExpression 同款） */
inline double cvflow_pow(double x, double y) { return std::pow(x, y); }

// ----------------------------------------------------------------------------
// Eigen LM 状态码 → 文本（原样保留）
// ----------------------------------------------------------------------------
std::string LmStatusMessage(int status)
{
    switch (status)
    {
    case -2: return "not started";
    case -1: return "running";
    case  0: return "improper input parameters";
    case  1: return "converged: relative reduction of sum of squares <= ftol";
    case  2: return "converged: relative error between x and solution <= xtol";
    case  3: return "converged: conditions 1 and 2 both hold";
    case  4: return "converged: cosine of angle between fvec and jacobian columns <= gtol";
    case  5: return "max function evaluations reached (increase MaxIter)";
    case  6: return "ftol too small, no further reduction possible";
    case  7: return "xtol too small, no further improvement possible";
    case  8: return "gtol too small, no further improvement possible";
    case  9: return "user asked to stop";
    default: return "unknown status";
    }
}

// ----------------------------------------------------------------------------
// LM 共享状态 functor：只持指针，Eigen 的多次拷贝共享同一份编译产物
//（muparser 表达式在 LmModel::create 时已解析，此处绝不复制 parser）
// ----------------------------------------------------------------------------
class LmSharedFunctor : public Eigen::DenseFunctor<double>
{
public:
    LmSharedFunctor(const LmModel* model,
                    const std::vector<double>* xFlat,
                    const std::vector<double>* yFlat)
        : Eigen::DenseFunctor<double>(model->paramCount(),
                                      (int)(yFlat->size())),
          m_model(model), m_x(xFlat), m_y(yFlat)
    {}

    int operator()(const InputType& params, ValueType& fvec) const
    {
        m_model->setParams(params.data());
        const int M = m_model->varCount();
        const int K = m_model->outputCount();
        const int nPts = (int)m_y->size() / K;
        for (int h = 0; h < nPts; ++h)
        {
            m_model->setX(m_x->data() + (size_t)h * M);
            for (int k = 0; k < K; ++k)
                fvec(h * K + k) = (*m_y)[(size_t)(h * K + k)] - m_model->eval(k);
        }
        return 0;
    }

private:
    const LmModel*             m_model;
    const std::vector<double>* m_x;
    const std::vector<double>* m_y;
};

} // anonymous namespace

// ----------------------------------------------------------------------------
// LmModel
// ----------------------------------------------------------------------------
LmModel* LmModel::create(const std::vector<std::string>& expressions,
                         const std::vector<std::string>& paramNames,
                         const std::vector<std::string>& xNames,
                         int& errCode, std::string& errMsg)
{
    errCode = 0; errMsg.clear();

    const size_t nExpr  = expressions.size();
    const size_t nParam = paramNames.size();
    const size_t nXName = xNames.size();

    if (nExpr < 1)  { errCode = 1;  errMsg = "at least one model expression required"; return nullptr; }
    if (nXName < 1) { errCode = 2;  errMsg = "at least one independent variable required"; return nullptr; }
    if (nParam < 1) { errCode = 3;  errMsg = "at least one parameter required"; return nullptr; }

    std::set<std::string> seen;
    for (size_t k = 0; k < nExpr; ++k)
        if (expressions[k].empty()) { errCode = 4; errMsg = "expression is empty"; return nullptr; }
    for (size_t j = 0; j < nXName; ++j)
    {
        if (xNames[j].empty())            { errCode = 5; errMsg = "independent variable name is empty"; return nullptr; }
        if (!seen.insert(xNames[j]).second){ errCode = 6; errMsg = "duplicate independent variable name: " + xNames[j]; return nullptr; }
    }
    seen.clear();
    for (size_t i = 0; i < nParam; ++i)
    {
        if (paramNames[i].empty())            { errCode = 7; errMsg = "parameter name is empty"; return nullptr; }
        if (!seen.insert(paramNames[i]).second){ errCode = 8; errMsg = "duplicate parameter name: " + paramNames[i]; return nullptr; }
    }
    for (size_t i = 0; i < nParam; ++i)
        for (size_t j = 0; j < nXName; ++j)
            if (paramNames[i] == xNames[j])
                { errCode = 9; errMsg = "parameter name conflicts with variable: " + paramNames[i]; return nullptr; }

    LmModel* m = new (std::nothrow) LmModel();
    if (!m) { errCode = 10; errMsg = "out of memory"; return nullptr; }

    m->m_exprs      = expressions;
    m->m_paramNames = paramNames;
    m->m_xNames     = xNames;
    m->m_paramSlots.assign(nParam, 0.0);
    m->m_xSlots.assign(nXName, 0.0);

    try
    {
        m->m_parsers.resize(nExpr, nullptr);
        for (size_t k = 0; k < nExpr; ++k)
        {
            mu::Parser* p = new mu::Parser();
            m->m_parsers[k] = p;
            p->DefineFun("pow", &cvflow_pow);
            p->SetExpr(m->m_exprs[k]);
            for (size_t j = 0; j < nXName; ++j)
                p->DefineVar(m->m_xNames[j], &m->m_xSlots[j]);
            for (size_t i = 0; i < nParam; ++i)
                p->DefineVar(m->m_paramNames[i], &m->m_paramSlots[i]);
            p->Eval();   // 预解析：语法错误/未知变量在此暴露（唯一一次解析）
        }
    }
    catch (mu::ParserError& e)
    {
        errCode = 10;
        errMsg  = std::string("expression parse error: ") + e.GetMsg();
        LmModel::destroy(m);
        return nullptr;
    }
    catch (...)
    {
        errCode = 10;
        errMsg  = "unknown error while compiling expressions";
        LmModel::destroy(m);
        return nullptr;
    }
    return m;
}

void LmModel::setParams(const double* p) const
{
    for (size_t i = 0; i < m_paramSlots.size(); ++i)
        m_paramSlots[i] = p[i];
}

void LmModel::setX(const double* xv) const
{
    for (size_t j = 0; j < m_xSlots.size(); ++j)
        m_xSlots[j] = xv[j];
}

double LmModel::eval(int k) const
{
    return static_cast<mu::Parser*>(m_parsers[(size_t)k])->Eval();
}

int lm_model_fit(const LmModel& m,
                 const std::vector<double>& xFlat,
                 const std::vector<double>& yFlat,
                 const std::vector<double>& initial,
                 long maxIter, double eps,
                 LmFitResult& out)
{
    out = LmFitResult();

    const size_t nParam = (size_t)m.paramCount();
    const size_t nXName = (size_t)m.varCount();
    const size_t nExpr  = (size_t)m.outputCount();

    if (initial.size() != nParam) { return 1; }                 // 初值数≠参数数
    if (xFlat.empty() || (xFlat.size() % nXName) != 0) { return 2; } // 自变量长度须为 M 整数倍

    const size_t nPoints = xFlat.size() / nXName;
    if (yFlat.size() != nPoints * nExpr) { return 3; }          // 观测长度须为 点数×K
    if (nPoints < nParam)                { return 4; }          // 欠定
    if (maxIter <= 0) maxIter = 400 * ((long)nParam + 1);       // Eigen LM 默认量级

    out.params = initial;                                       // 默认回吐初值

    try
    {
        LmSharedFunctor functor(&m, &xFlat, &yFlat);

        // eps 只能经 NumericalDiff 构造函数传入（Eigen 5.x 中该成员为 private，
        // 且 LevenbergMarquardt::setEpsilon 并不参与数值微分）
        Eigen::NumericalDiff<LmSharedFunctor> numDiff(functor, eps > 0.0 ? eps : 0.0);

        Eigen::LevenbergMarquardt<Eigen::NumericalDiff<LmSharedFunctor> > lm(numDiff);
        lm.setMaxfev((Eigen::Index)maxIter);
        lm.setFtol(1e-12);          // 相对残差下降容差
        lm.setXtol(1e-12);          // 相对参数变化容差
        lm.setGtol(0.0);            // 0 = 关闭梯度余弦判据

        Eigen::VectorXd params((Eigen::Index)nParam);
        for (size_t i = 0; i < nParam; ++i)
            params((Eigen::Index)i) = initial[i];

        out.status     = (int)lm.minimize(params);
        out.iterations = (int)lm.iterations();

        Eigen::VectorXd residual((Eigen::Index)(nPoints * nExpr));
        functor(params, residual);
        out.rss = residual.squaredNorm();

        out.message = LmStatusMessage(out.status);
        if (!std::isfinite(out.rss))
            out.message += " | warning: residual sum of squares is not finite";

        out.params.assign(params.data(), params.data() + params.size());
    }
    catch (mu::ParserError& e)
    {
        // muparser 的 ParserError 不派生自 std::exception，必须单独捕获
        out.message = std::string("expression error: ") + e.GetMsg();
    }
    catch (const std::exception& e)
    {
        out.message = std::string("error: ") + e.what();
    }
    catch (...)
    {
        out.message = "error: unknown exception";
    }
    return 0;
}

// ----------------------------------------------------------------------------
// LinearModel
// ----------------------------------------------------------------------------
LinearModel* LinearModel::create(const std::string& expression,
                                 const std::vector<std::string>& paramNames,
                                 const std::vector<std::string>& xNames,
                                 int& errCode, std::string& errMsg)
{
    errCode = 0; errMsg.clear();

    const size_t nParam = paramNames.size();
    const size_t nXName = xNames.size();

    if (expression.empty()) { errCode = 1; errMsg = "expression is empty"; return nullptr; }
    if (nParam < 1)         { errCode = 2; errMsg = "at least one parameter required"; return nullptr; }
    if (nXName < 1 || nXName > 2)
                            { errCode = 3; errMsg = "independent variable count must be 1 or 2"; return nullptr; }

    std::set<std::string> seen;
    for (size_t i = 0; i < nParam; ++i)
    {
        if (paramNames[i].empty())            { errCode = 4; errMsg = "parameter name is empty"; return nullptr; }
        if (!seen.insert(paramNames[i]).second){ errCode = 5; errMsg = "duplicate parameter name: " + paramNames[i]; return nullptr; }
    }
    seen.clear();
    for (size_t j = 0; j < nXName; ++j)
    {
        if (xNames[j].empty())                { errCode = 6; errMsg = "independent variable name is empty"; return nullptr; }
        if (!seen.insert(xNames[j]).second)   { errCode = 7; errMsg = "duplicate independent variable name: " + xNames[j]; return nullptr; }
    }
    for (size_t i = 0; i < nParam; ++i)
        for (size_t j = 0; j < nXName; ++j)
            if (paramNames[i] == xNames[j])
                { errCode = 8; errMsg = "parameter name conflicts with variable: " + paramNames[i]; return nullptr; }

    LinearModel* m = new (std::nothrow) LinearModel();
    if (!m) { errCode = 9; errMsg = "out of memory"; return nullptr; }

    m->m_expr      = expression;
    m->m_paramNames = paramNames;
    m->m_xNames    = xNames;
    m->m_paramSlots.assign(nParam, 0.0);
    m->m_xSlots.assign(nXName, 0.0);

    try
    {
        mu::Parser* p = new mu::Parser();
        m->m_parser = p;
        p->DefineFun("pow", &cvflow_pow);
        for (size_t j = 0; j < nXName; ++j)
            p->DefineVar(m->m_xNames[j], &m->m_xSlots[j]);
        for (size_t i = 0; i < nParam; ++i)
            p->DefineVar(m->m_paramNames[i], &m->m_paramSlots[i]);
        p->SetExpr(m->m_expr);
        p->Eval();   // 预解析（唯一一次）
    }
    catch (mu::ParserError& e)
    {
        errCode = 9;
        errMsg  = std::string("expression parse error: ") + e.GetMsg();
        LinearModel::destroy(m);
        return nullptr;
    }
    catch (...)
    {
        errCode = 9;
        errMsg  = "unknown error while compiling expression";
        LinearModel::destroy(m);
        return nullptr;
    }
    return m;
}

double LinearModel::eval(const std::vector<double>& x,
                         const std::vector<double>& params) const
{
    for (size_t j = 0; j < m_xSlots.size(); ++j)
        m_xSlots[j] = x[j];
    for (size_t i = 0; i < m_paramSlots.size(); ++i)
        m_paramSlots[i] = params[i];
    return static_cast<mu::Parser*>(m_parser)->Eval();
}

// ----------------------------------------------------------------------------
// 列主元 Householder QR 求解最小二乘（cv::Mat 版，原样保留）
// ----------------------------------------------------------------------------
static cv::Mat qrColPivSolve(cv::Mat Z, cv::Mat F, int* outRank = nullptr)
{
    const int m = Z.rows, n = Z.cols;
    std::vector<int> perm(n);
    std::iota(perm.begin(), perm.end(), 0);

    std::vector<double> colNorms(n);
    for (int j = 0; j < n; ++j) {
        double s = 0.0;
        for (int i = 0; i < m; ++i) s += Z.at<double>(i, j) * Z.at<double>(i, j);
        colNorms[j] = s;
    }

    const int kmax = (std::min)(m, n);
    std::vector<double> diagR(kmax, 0.0);

    for (int k = 0; k < kmax; ++k) {
        int piv = k;
        double best = colNorms[k];
        for (int j = k + 1; j < n; ++j) if (colNorms[j] > best) { best = colNorms[j]; piv = j; }
        if (piv != k) {
            for (int i = 0; i < m; ++i) std::swap(Z.at<double>(i, k), Z.at<double>(i, piv));
            std::swap(colNorms[k], colNorms[piv]);
            std::swap(perm[k], perm[piv]);
        }

        double normx = 0.0;
        for (int i = k; i < m; ++i) normx += Z.at<double>(i, k) * Z.at<double>(i, k);
        normx = std::sqrt(normx);
        if (normx < 1e-300) { diagR[k] = 0.0; continue; }

        double alpha = (Z.at<double>(k, k) >= 0) ? -normx : normx;
        std::vector<double> v(m - k);
        for (int i = k; i < m; ++i) v[i - k] = Z.at<double>(i, k);
        v[0] -= alpha;
        double vnorm2 = 0.0;
        for (double vi : v) vnorm2 += vi * vi;
        if (vnorm2 < 1e-300) { diagR[k] = Z.at<double>(k, k); continue; }

        for (int j = k; j < n; ++j) {
            double dot = 0.0;
            for (int i = k; i < m; ++i) dot += v[i - k] * Z.at<double>(i, j);
            double factor = 2.0 * dot / vnorm2;
            for (int i = k; i < m; ++i) Z.at<double>(i, j) -= factor * v[i - k];
        }
        {
            double dot = 0.0;
            for (int i = k; i < m; ++i) dot += v[i - k] * F.at<double>(i, 0);
            double factor = 2.0 * dot / vnorm2;
            for (int i = k; i < m; ++i) F.at<double>(i, 0) -= factor * v[i - k];
        }
        diagR[k] = Z.at<double>(k, k);

        for (int j = k + 1; j < n; ++j) {
            double s = 0.0;
            for (int i = k + 1; i < m; ++i) s += Z.at<double>(i, j) * Z.at<double>(i, j);
            colNorms[j] = s;
        }
    }

    double maxDiag = 0.0;
    for (int i = 0; i < kmax; ++i) maxDiag = (std::max)(maxDiag, std::abs(diagR[i]));
    const double tol = (std::max)(m, n) * std::numeric_limits<double>::epsilon() * maxDiag;
    int rank = 0;
    for (int i = 0; i < kmax; ++i) {
        if (std::abs(diagR[i]) > tol) rank = i + 1;
        else break;
    }
    if (outRank) *outRank = rank;

    std::vector<double> xPiv(n, 0.0);   // 超出秩的列保持 0
    for (int i = rank - 1; i >= 0; --i) {
        double s = F.at<double>(i, 0);
        for (int j = i + 1; j < rank; ++j) s -= Z.at<double>(i, j) * xPiv[j];
        xPiv[i] = s / Z.at<double>(i, i);
    }

    cv::Mat result(n, 1, CV_64F);
    for (int i = 0; i < n; ++i) result.at<double>(perm[i], 0) = xPiv[i];
    return result;
}

LinearFitResult linear_model_fit(const LinearModel& m,
                                 const std::vector<std::vector<double>>& xData,
                                 const std::vector<double>& yData,
                                 double linearTolerance)
{
    LinearFitResult result;

    const size_t sampleCount = xData.size();
    const size_t paramCount  = (size_t)m.paramCount();
    const size_t xCount      = (size_t)m.varCount();

    if (sampleCount == 0)
    {
        result.message = "No data.";
        return result;
    }
    if (yData.size() != sampleCount)
    {
        result.message = "XData and YData size mismatch.";
        return result;
    }
    for (size_t i = 0; i < sampleCount; ++i)
    {
        if (xData[i].size() != xCount)
        {
            result.message = "Invalid XData dimension.";
            return result;
        }
    }

    std::vector<double> zeros(paramCount, 0.0);

    // 构造设计矩阵 Z / 观测向量 F：
    //   f(x,p) = f(x,0) + Σ p_j * basis_j(x)
    //   Z(i,j) = f(x_i, e_j) - f(x_i, 0)
    //   F(i)   = y_i - f(x_i, 0)
    cv::Mat Z((int)sampleCount, (int)paramCount, CV_64F);
    cv::Mat F((int)sampleCount, 1, CV_64F);

    try
    {
        for (size_t i = 0; i < sampleCount; ++i)
        {
            const double f0 = m.eval(xData[i], zeros);
            if (!std::isfinite(f0))
            {
                result.message = "Model evaluation returned non-finite value.";
                return result;
            }
            F.at<double>((int)i, 0) = yData[i] - f0;

            for (size_t j = 0; j < paramCount; ++j)
            {
                std::vector<double> oneHot(paramCount, 0.0);
                oneHot[j] = 1.0;
                const double fj = m.eval(xData[i], oneHot);
                if (!std::isfinite(fj))
                {
                    result.message = "Model evaluation returned non-finite value.";
                    return result;
                }
                Z.at<double>((int)i, (int)j) = fj - f0;
            }
        }
    }
    catch (const mu::Parser::exception_type& e)
    {
        result.message = std::string("Model evaluation error: ") + e.GetMsg();
        return result;
    }

    // 验证模型确实对参数线性：
    //   单参数：f(2e_j) ≈ f0 + 2*basis_j
    //   交叉项：f(e_i+e_j) ≈ f0 + basis_i + basis_j
    try
    {
        for (size_t i = 0; i < sampleCount; ++i)
        {
            const double f0 = m.eval(xData[i], zeros);

            for (size_t j = 0; j < paramCount; ++j)
            {
                std::vector<double> two(paramCount, 0.0);
                two[j] = 2.0;
                const double f2 = m.eval(xData[i], two);
                const double basis = Z.at<double>((int)i, (int)j);
                const double expected = f0 + 2.0 * basis;
                const double error = std::abs(f2 - expected);
                const double scale = (std::max)({1.0, std::abs(f2), std::abs(expected)});
                if (error > linearTolerance * scale)
                {
                    std::ostringstream oss;
                    oss << "Model is nonlinear in parameter '" << m.paramNames()[j] << "'.";
                    result.message = oss.str();
                    return result;
                }
            }

            for (size_t j = 0; j < paramCount; ++j)
            {
                for (size_t k = j + 1; k < paramCount; ++k)
                {
                    std::vector<double> both(paramCount, 0.0);
                    both[j] = 1.0;
                    both[k] = 1.0;
                    const double fij = m.eval(xData[i], both);
                    const double basisJ = Z.at<double>((int)i, (int)j);
                    const double basisK = Z.at<double>((int)i, (int)k);
                    const double expected = f0 + basisJ + basisK;
                    const double error = std::abs(fij - expected);
                    const double scale = (std::max)({1.0, std::abs(fij), std::abs(expected)});
                    if (error > linearTolerance * scale)
                    {
                        std::ostringstream oss;
                        oss << "Model contains nonlinear parameter interaction between '"
                            << m.paramNames()[j] << "' and '" << m.paramNames()[k] << "'.";
                        result.message = oss.str();
                        return result;
                    }
                }
            }
        }
    }
    catch (const mu::Parser::exception_type& e)
    {
        result.message = std::string("Linearity check error: ") + e.GetMsg();
        return result;
    }

    // 列主元 Householder QR 求解（传入副本，保留原始 Z/F 用于残差）
    int rank = 0;
    cv::Mat solution = qrColPivSolve(Z.clone(), F.clone(), &rank);
    result.rank = rank;

    result.coefficients.assign(paramCount, 0.0);
    for (size_t j = 0; j < paramCount; ++j)
        result.coefficients[j] = solution.at<double>((int)j, 0);

    // RSS = || Z * coeff - F ||^2
    {
        cv::Mat residual = Z * solution - F;
        double nrm = cv::norm(residual);
        result.rss = nrm * nrm;
    }
    if (!std::isfinite(result.rss))
    {
        result.message = "RSS is not finite.";
        return result;
    }

    if (rank < (int)paramCount)
    {
        result.success = true;
        std::ostringstream oss;
        oss << "Fit succeeded, but matrix is rank deficient. Rank = "
            << rank << "/" << paramCount << ".";
        result.message = oss.str();
        return result;
    }

    result.success = true;
    result.message = "Fit succeeded.";
    return result;
}

} // namespace cvflow
