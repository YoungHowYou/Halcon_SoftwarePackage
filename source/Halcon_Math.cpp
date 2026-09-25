/*=============================================================================
 * Halcon_Math.cpp — HALCON 数学/矩阵扩展算子
 *
 * 涵盖:
 *   Eigen : eigen_svd (JacobiSVD), eigen_ldlt, eigen_llt
 *           eigen_lm_fit    (LevenbergMarquardt + muparser, 1D 单输出曲线拟合)
 *           eigen_lm_fit_2d (LevenbergMarquardt + muparser, 多维自变量 / 多输出共享参数)
 *   Armadillo : arma_interp1
 *   C++ std : std_nth_element, std_sort, std_lower_bound
 *
 * 约定:
 *   - 矩阵用 real 单通道图像承载（height=行数, width=列数）
 *   - 向量用 1×N 或 N×1 的 real 图像承载
 *   - 标量/索引用 tuple 输出
 *===========================================================================*/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <functional>
#include <vector>
#include <string>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <numeric>

#include <Eigen/Dense>
#include <unsupported/Eigen/LevenbergMarquardt>
#include <opencv2/opencv.hpp>
#include <armadillo>
#include <muParser.h>

#include "HalconCpp.h"
#include "Halcon_SoftwarePackage.h"

using namespace std;
using namespace HalconCpp;

/*=============================================================================
 * std_nth_element — 找第 n 小元素（展平后，0-based）
 *===========================================================================*/
Herror HCstd_nth_element(Hproc_handle proc_handle)
{
    Hkey   in_obj_key;
    Himage inimage;
    Hcpar  n;

    HGetSPar(proc_handle, 1, LONG_PAR, &n, 1);
    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    if (inimage.kind != FLOAT_IMAGE)
        return 30001;   // 仅支持 real 单通道

    int total = (int)inimage.width * (int)inimage.height;
    INT4_8 idx = n.par.l;
    if (idx < 0 || idx >= total)
        return 30002;   // n 越界

    const float* src = inimage.pixel.f;
    std::vector<double> v(src, src + total);
    std::nth_element(v.begin(), v.begin() + (size_t)idx, v.end());

    double result = v[(size_t)idx];
    HPutElem(proc_handle, 1, &result, 1, DOUBLE_PAR);

    return H_MSG_TRUE;
}

/*=============================================================================
 * std_sort — 展平排序（升/降序），结果 reshape 回原尺寸
 *===========================================================================*/
Herror HCstd_sort(Hproc_handle proc_handle)
{
    Hkey   in_obj_key, out_obj_key, out_image_key;
    Himage inimage, outimage;
    Hcpar  descending;

    HGetSPar(proc_handle, 1, LONG_PAR, &descending, 1);
    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    if (inimage.kind != FLOAT_IMAGE)
        return 30001;   // 仅支持 real 单通道

    int total = (int)inimage.width * (int)inimage.height;
    const float* src = inimage.pixel.f;
    std::vector<double> v(src, src + total);

    if (descending.par.l != 0)
        std::sort(v.begin(), v.end(), std::greater<double>());
    else
        std::sort(v.begin(), v.end());

    HCkP(HNewImage(proc_handle, &outimage, FLOAT_IMAGE, inimage.width, inimage.height));
    for (int i = 0; i < total; ++i)
        outimage.pixel.f[i] = (float)v[i];

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * std_lower_bound — 二分查找下界（要求输入已升序，返回第一个 >= Value 的位置）
 *===========================================================================*/
Herror HCstd_lower_bound(Hproc_handle proc_handle)
{
    Hkey   in_obj_key;
    Himage inimage;
    Hcpar  value;

    HGetSPar(proc_handle, 1, DOUBLE_PAR, &value, 1);
    HGetObj(proc_handle, 1, 1, &in_obj_key);
    HGetDImage(proc_handle, in_obj_key, 1, &inimage);

    if (inimage.kind != FLOAT_IMAGE)
        return 30001;   // 仅支持 real 单通道

    int total = (int)inimage.width * (int)inimage.height;
    const float* src = inimage.pixel.f;
    std::vector<double> v(src, src + total);

    auto it = std::lower_bound(v.begin(), v.end(), value.par.d);
    INT4_8 index = (INT4_8)(it - v.begin());
    HPutElem(proc_handle, 1, &index, 1, LONG_PAR);

    return H_MSG_TRUE;
}

/*=============================================================================
 * eigen_svd — 奇异值分解（Eigen::JacobiSVD，满 U/V）
 *   A: m×n → U: m×m, S: 1×min(m,n), V: n×n
 *===========================================================================*/
Herror HCeigen_svd(Hproc_handle proc_handle)
{
    Hkey   inA_obj_key, u_obj_key, s_obj_key, v_obj_key, out_image_key;
    Himage inA, uimage, simage, vimage;

    HGetObj(proc_handle, 1, 1, &inA_obj_key);
    HGetDImage(proc_handle, inA_obj_key, 1, &inA);

    if (inA.kind != FLOAT_IMAGE)
        return 30001;   // 仅支持 real 单通道矩阵

    int m = (int)inA.height, n = (int)inA.width;
    Eigen::MatrixXd A(m, n);
    {
        const float* p = inA.pixel.f;
        for (int r = 0; r < m; ++r)
            for (int c = 0; c < n; ++c)
                A(r, c) = p[(size_t)r * n + c];
    }

    Eigen::JacobiSVD<Eigen::MatrixXd> svd(A, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Eigen::MatrixXd U = svd.matrixU();
    Eigen::VectorXd S = svd.singularValues();
    Eigen::MatrixXd V = svd.matrixV();

    // 输出 U: m×m
    HCkP(HNewImage(proc_handle, &uimage, FLOAT_IMAGE, m, m));
    for (int r = 0; r < m; ++r)
        for (int c = 0; c < m; ++c)
            uimage.pixel.f[(size_t)r * m + c] = (float)U(r, c);
    HCrObj(proc_handle, 1, &u_obj_key);
    HPutDImage(proc_handle, u_obj_key, 1, &uimage, FALSE, &out_image_key);
    HPutRect(proc_handle, u_obj_key, uimage.width, uimage.height);

    // 输出 S: 1×min(m,n)
    int k = (int)S.size();
    HCkP(HNewImage(proc_handle, &simage, FLOAT_IMAGE, k, 1));
    for (int i = 0; i < k; ++i)
        simage.pixel.f[i] = (float)S(i);
    HCrObj(proc_handle, 2, &s_obj_key);
    HPutDImage(proc_handle, s_obj_key, 1, &simage, FALSE, &out_image_key);
    HPutRect(proc_handle, s_obj_key, simage.width, simage.height);

    // 输出 V: n×n
    HCkP(HNewImage(proc_handle, &vimage, FLOAT_IMAGE, n, n));
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c)
            vimage.pixel.f[(size_t)r * n + c] = (float)V(r, c);
    HCrObj(proc_handle, 3, &v_obj_key);
    HPutDImage(proc_handle, v_obj_key, 1, &vimage, FALSE, &out_image_key);
    HPutRect(proc_handle, v_obj_key, vimage.width, vimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * eigen 解线性方程组辅助函数（LDLT / LLT）
 *===========================================================================*/
static Herror eigenSolveLinear(Hproc_handle proc_handle, bool useLDLT)
{
    Hkey   inA_obj_key, inB_obj_key, out_obj_key, out_image_key;
    Himage inA, inB, outimage;

    HGetObj(proc_handle, 1, 1, &inA_obj_key);
    HGetDImage(proc_handle, inA_obj_key, 1, &inA);
    HGetObj(proc_handle, 2, 1, &inB_obj_key);
    HGetDImage(proc_handle, inB_obj_key, 1, &inB);

    if (inA.kind != FLOAT_IMAGE || inB.kind != FLOAT_IMAGE)
        return 30001;   // 仅支持 real 单通道矩阵

    int n = (int)inA.height;
    if ((int)inA.width != n)
        return 30002;   // A 必须为方阵
    int k = (int)inB.width;
    if ((int)inB.height != n)
        return 30003;   // B 的行数须等于 A 的维数

    Eigen::MatrixXd A(n, n), b(n, k);
    {
        const float* pa = inA.pixel.f;
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c)
                A(r, c) = pa[(size_t)r * n + c];
    }
    {
        const float* pb = inB.pixel.f;
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < k; ++c)
                b(r, c) = pb[(size_t)r * k + c];
    }

    Eigen::MatrixXd x;
    try
    {
        if (useLDLT)
            x = A.ldlt().solve(b);
        else
            x = A.llt().solve(b);
    }
    catch (...)
    {
        return 30004;   // 求解失败
    }

    HCkP(HNewImage(proc_handle, &outimage, FLOAT_IMAGE, k, n));
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < k; ++c)
            outimage.pixel.f[(size_t)r * k + c] = (float)x(r, c);

    HCrObj(proc_handle, 1, &out_obj_key);
    HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);
    HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * eigen_ldlt — 解对称线性方程组（Eigen::LDLT）
 *===========================================================================*/
Herror HCeigen_ldlt(Hproc_handle proc_handle)
{
    return eigenSolveLinear(proc_handle, true);
}

/*=============================================================================
 * eigen_llt — 解正定线性方程组（Eigen::LLT）
 *===========================================================================*/
Herror HCeigen_llt(Hproc_handle proc_handle)
{
    return eigenSolveLinear(proc_handle, false);
}

/*=============================================================================
 * arma_interp1 — 一维插值（Armadillo::interp1）
 *   X: 1×N, Y: 1×N, XI: 1×M → YI: 1×M
 *   method: linear / nearest / previous / next / pchip / *linear
 *===========================================================================*/
Herror HCarma_interp1(Hproc_handle proc_handle)
{
    Hkey   x_obj_key, y_obj_key, xi_obj_key, yi_obj_key, out_image_key;
    Himage ximage, yimage, xiimage, yiimage;
    Hcpar  method;

    HAllocStringMem(proc_handle, 64);
    HGetSPar(proc_handle, 1, STRING_PAR, &method, 1);
    HGetObj(proc_handle, 1, 1, &x_obj_key);
    HGetDImage(proc_handle, x_obj_key, 1, &ximage);
    HGetObj(proc_handle, 2, 1, &y_obj_key);
    HGetDImage(proc_handle, y_obj_key, 1, &yimage);
    HGetObj(proc_handle, 3, 1, &xi_obj_key);
    HGetDImage(proc_handle, xi_obj_key, 1, &xiimage);

    if (ximage.kind != FLOAT_IMAGE || yimage.kind != FLOAT_IMAGE || xiimage.kind != FLOAT_IMAGE)
        return 30001;   // 仅支持 real 单通道

    int N = (int)ximage.width * (int)ximage.height;
    if ((int)yimage.width * (int)yimage.height != N)
        return 30002;   // X 与 Y 长度不一致
    int M = (int)xiimage.width * (int)xiimage.height;

    arma::vec x(N), y(N), xi(M);
    for (int i = 0; i < N; ++i) { x(i) = ximage.pixel.f[i]; y(i) = yimage.pixel.f[i]; }
    for (int i = 0; i < M; ++i) xi(i) = xiimage.pixel.f[i];

    arma::vec yi;
    try
    {
        arma::interp1(x, y, xi, yi, method.par.s);
    }
    catch (...)
    {
        return 30003;   // 插值失败
    }

    HCkP(HNewImage(proc_handle, &yiimage, FLOAT_IMAGE, M, 1));
    for (int i = 0; i < M; ++i)
        yiimage.pixel.f[i] = (float)yi(i);

    HCrObj(proc_handle, 1, &yi_obj_key);
    HPutDImage(proc_handle, yi_obj_key, 1, &yiimage, FALSE, &out_image_key);
    HPutRect(proc_handle, yi_obj_key, yiimage.width, yiimage.height);

    return H_MSG_TRUE;
}

/*=============================================================================
 * eigen_lm_fit — 通用非线性最小二乘拟合
 *   Eigen LevenbergMarquardt + muparser 运行时表达式解析。
 *   模型表达式在调用时以字符串给出（如 "a*exp(b*x)+c"），自变量名与
 *   参数名均由用户指定，无需编译期 functor，可拟合任意表达式模型。
 *
 *   输入控制:
 *     1  expression      模型表达式字符串（自变量名 + 参数名组成）
 *     2  paramNames      参数名元组，如 ['a','b','c']
 *     3  initialValues   参数初值元组（长度须与 paramNames 相同）
 *     4  xData           自变量观测值元组
 *     5  yData           因变量观测值元组（长度须与 xData 相同）
 *     6  xName           自变量名（空串时取 'x'）
 *     7  maxIter         最大函数求值次数（<=0 时取 400*(n+1)）
 *     8  eps             数值微分步长（<=0 时由 Eigen 自动选取）
 *   输出控制:
 *     1  paramValues     拟合后的参数值元组（与 paramNames 顺序一致）
 *     2  rss             残差平方和（表达式出错时为 -1）
 *     3  iterations      迭代次数
 *     4  status          Eigen LM 状态码（见 README.md「数学 / 矩阵扩展算子」章节）
 *     5  message         状态描述文本
 *===========================================================================*/

namespace {

// ---- LM 残差函子：fvec[i] = yData[i] - f(xData[i]; params) ----
// 必须派生自 Eigen::DenseFunctor<double>：Eigen 5.x 的 LevenbergMarquardt
// 要求 FunctorType 提供 QRSolver / InputType / ValueType / JacobianType。
//
// 注意：Eigen::NumericalDiff 会【拷贝】该 functor，而 mu::Parser 内部保存的是
// 变量地址，因此拷贝构造必须重新绑定到副本自己的成员，否则副本求值时写入副本
// 的 m_paramValues，parser 却仍读取源对象的旧值，导致残差恒定、Jacobian 全零。
class LmExprFunctor : public Eigen::DenseFunctor<double>
{
public:
    LmExprFunctor(const std::string& expression,
                  const std::vector<std::string>& paramNames,
                  std::vector<double> xData,
                  std::vector<double> yData,
                  const std::string& xName)
        : Eigen::DenseFunctor<double>((int)paramNames.size(), (int)xData.size()),
          m_expression(expression),
          m_xName(xName),
          m_paramNames(paramNames),
          m_xData(xData),
          m_yData(yData)
    {
        // 注意：此 vector 之后绝不 resize，muparser 内部保存了元素地址
        m_paramValues.assign(m_paramNames.size(), 0.0);

        RebindVariables();
        m_parser.Eval();    // 试解析：语法错误 / 未知变量在此提前暴露
    }

    // 拷贝构造：重新绑定 parser 变量到本副本的成员
    LmExprFunctor(const LmExprFunctor& other)
        : Eigen::DenseFunctor<double>(other),
          m_expression(other.m_expression),
          m_xName(other.m_xName),
          m_paramNames(other.m_paramNames),
          m_xData(other.m_xData),
          m_yData(other.m_yData)
    {
        m_x = other.m_x;
        m_paramValues.assign(other.m_paramValues.begin(), other.m_paramValues.end());
        RebindVariables();
    }

    int operator()(const InputType& params, ValueType& fvec) const
    {
        for (int i = 0; i < m_inputs; ++i)
            m_paramValues[(size_t)i] = params(i);

        int n = (int)m_xData.size();
        for (int i = 0; i < n; ++i)
        {
            m_x = m_xData[(size_t)i];
            fvec(i) = m_yData[(size_t)i] - m_parser.Eval();
        }
        return 0;
    }

private:
    void RebindVariables()
    {
        m_parser.SetExpr(m_expression);
        m_parser.DefineVar(m_xName, &m_x);
        for (size_t i = 0; i < m_paramNames.size(); ++i)
            m_parser.DefineVar(m_paramNames[i], &m_paramValues[i]);
    }

    std::string              m_expression;
    std::string              m_xName;
    std::vector<std::string> m_paramNames;
    std::vector<double>      m_xData;
    std::vector<double>      m_yData;

    mutable double              m_x = 0.0;
    mutable std::vector<double> m_paramValues;
    mutable mu::Parser          m_parser;
};

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

}   // anonymous namespace


Herror HCeigen_lm_fit(Hproc_handle proc_handle)
{
    Hcpar  exprPar, xNamePar, maxIterPar, epsPar;
    char const* const* paramNameArr;
    INT4_8 nParamNames;
    double const* initialValues;
    INT4_8 nInitial;
    double const* xData;
    INT4_8 nX;
    double const* yData;
    INT4_8 nY;

    HAllocStringMem(proc_handle, 256);

    HGetSPar(proc_handle, 1, STRING_PAR, &exprPar, 1);
    HGetPElemS(proc_handle, 2, CONV_NONE, &paramNameArr, &nParamNames);
    HGetPElemD(proc_handle, 3, CONV_NONE, &initialValues, &nInitial);
    HGetPElemD(proc_handle, 4, CONV_NONE, &xData, &nX);
    HGetPElemD(proc_handle, 5, CONV_NONE, &yData, &nY);
    HGetSPar(proc_handle, 6, STRING_PAR, &xNamePar, 1);
    HGetSPar(proc_handle, 7, LONG_PAR, &maxIterPar, 1);
    HGetSPar(proc_handle, 8, DOUBLE_PAR, &epsPar, 1);

    const char* exprStr  = exprPar.par.s ? exprPar.par.s : "";
    const char* xNameStr = xNamePar.par.s ? xNamePar.par.s : "";
    INT4_8 maxIter = maxIterPar.par.l;
    double eps     = epsPar.par.d;

    // ---- 基本合法性校验 ----
    if (exprStr[0] == '\0')
        return 30001;                               // 表达式为空
    if (xNameStr[0] == '\0')
        xNameStr = "x";
    if (nParamNames < 1)
        return 30002;                               // 至少需要一个参数
    if (nInitial != nParamNames)
        return 30003;                               // 初值数量与参数名数量不符
    if (nX != nY)
        return 30004;                               // X / Y 观测数据长度不符
    if (nX < nParamNames)
        return 30005;                               // 数据点少于参数个数（欠定）
    if (maxIter <= 0)
        maxIter = 400 * (nParamNames + 1);          // Eigen LM 默认值

    std::vector<std::string> paramNames;
    std::set<std::string>    uniqueCheck;
    paramNames.reserve((size_t)nParamNames);
    for (INT4_8 i = 0; i < nParamNames; ++i)
    {
        const char* pn = paramNameArr[i] ? paramNameArr[i] : "";
        if (pn[0] == '\0')
            return 30006;                           // 参数名不能为空
        if (strcmp(pn, xNameStr) == 0)
            return 30007;                           // 参数名与自变量名冲突
        if (!uniqueCheck.insert(pn).second)
            return 30008;                           // 参数名重复
        paramNames.push_back(pn);
    }

    std::vector<double> initVals(initialValues, initialValues + nInitial);
    std::vector<double> xv(xData, xData + nX);
    std::vector<double> yv(yData, yData + nY);

    // ---- 拟合（结果先给默认值，异常时保持可控输出） ----
    INT4_8 status     = 0;
    INT4_8 iterations = 0;
    double rss        = -1.0;
    std::string message;
    std::vector<double> outParams(initVals);        // 默认回吐初值

    try
    {
        LmExprFunctor functor(exprStr, paramNames, xv, yv, xNameStr);

        // eps 只能经 NumericalDiff 构造函数传入（Eigen 5.x 中该成员为 private，
        // 且 LevenbergMarquardt::setEpsilon 并不参与数值微分）
        Eigen::NumericalDiff<LmExprFunctor> numDiff(functor, eps > 0.0 ? eps : 0.0);

        Eigen::LevenbergMarquardt<Eigen::NumericalDiff<LmExprFunctor> > lm(numDiff);
        lm.setMaxfev((Eigen::Index)maxIter);        // Eigen 5.x 改用 setter
        lm.setFtol(1e-12);                          // 相对残差下降容差
        lm.setXtol(1e-12);                          // 相对参数变化容差
        lm.setGtol(0.0);                            // 0 = 关闭梯度余弦判据

        Eigen::VectorXd params((Eigen::Index)nParamNames);
        for (INT4_8 i = 0; i < nParamNames; ++i)
            params((Eigen::Index)i) = initVals[(size_t)i];

        status     = (INT4_8)lm.minimize(params);
        iterations = (INT4_8)lm.iterations();

        Eigen::VectorXd residual((Eigen::Index)nX);
        functor(params, residual);
        rss = residual.squaredNorm();

        message = LmStatusMessage((int)status);
        if (!std::isfinite(rss))
            message += " | warning: residual sum of squares is not finite";

        outParams.assign(params.data(), params.data() + params.size());
    }
    catch (mu::ParserError& e)
    {
        // muparser 的 ParserError 不派生自 std::exception，必须单独捕获
        status     = 0;
        iterations = 0;
        rss        = -1.0;
        message    = std::string("expression error: ") + e.GetMsg();
    }
    catch (const std::exception& e)
    {
        // 其它标准异常（内存不足、vector 长度错误等）
        status     = 0;
        iterations = 0;
        rss        = -1.0;
        message    = std::string("error: ") + e.what();
    }
    catch (...)
    {
        status     = 0;
        iterations = 0;
        rss        = -1.0;
        message    = "error: unknown exception";
    }

    // ---- 输出 ----
    HPutElem(proc_handle, 1, outParams.data(), (INT4_8)outParams.size(), DOUBLE_PAR);
    HPutElem(proc_handle, 2, &rss, 1, DOUBLE_PAR);
    HPutElem(proc_handle, 3, &iterations, 1, LONG_PAR);
    HPutElem(proc_handle, 4, &status, 1, LONG_PAR);

    char* msgOut = NULL;
    HAllocTmp(proc_handle, &msgOut, (INT4_8)message.size() + 1);
    memcpy(msgOut, message.c_str(), message.size() + 1);
    HPutElem(proc_handle, 5, &msgOut, 1, STRING_PAR);
    HFreeTmp(proc_handle, msgOut, (INT4_8)message.size() + 1);

    return H_MSG_TRUE;
}

/*=============================================================================
 * eigen_lm_fit_2d — 多维自变量 / 多输出非线性最小二乘拟合
 *
 *   与 eigen_lm_fit 的区别：
 *     - 支持 M 个自变量（典型 2D 用法：u, v，也可再传入预计算的派生量）
 *     - 支持 K 个输出表达式（典型：dx 与 dy），
 *       所有表达式【共享同一组参数】——这正是畸变场这类模型的形态
 *     - 残差向量长度 = 点数 × 输出个数，交由同一个 LM 联合优化
 *
 *   输入控制:
 *     1  expressions     模型表达式元组，一个或多个（如 ['dx模型','dy模型']）
 *     2  paramNames      共享参数名元组
 *     3  initialValues   参数初值元组（长度须与 paramNames 相同）
 *     4  xNames          自变量名元组，一个或多个（如 ['u','v','r2']）
 *     5  xData           自变量数据元组，长度 = 点数 × M（每点 M 个值连续存放）
 *     6  yData           观测数据元组，长度 = 点数 × K（每点 K 个值连续存放）
 *     7  maxIter         最大函数求值次数（<=0 时取 400*(n+1)）
 *     8  eps             数值微分步长（<=0 时由 Eigen 自动选取）
 *   输出控制:
 *     1  paramValues     拟合后的参数值元组
 *     2  rss             残差平方和（表达式出错时为 -1）
 *     3  iterations      迭代次数
 *     4  status          Eigen LM 状态码
 *     5  statusMessage   状态描述文本
 *===========================================================================*/

namespace {

// ---- 多维 / 多输出 LM 残差函子 ----
// 残差按行优先排列：fvec[h*K + k] = yData[h*K + k] - expr_k(x_h; params)
class LmExprFunctor2D : public Eigen::DenseFunctor<double>
{
public:
    LmExprFunctor2D(const std::vector<std::string>& expressions,
                    const std::vector<std::string>& paramNames,
                    const std::vector<std::string>& xNames,
                    std::vector<double> xData,
                    std::vector<double> yData,
                    int nPoints)
        : Eigen::DenseFunctor<double>((int)paramNames.size(),
                                      nPoints * (int)expressions.size()),
          m_expressions(expressions),
          m_paramNames(paramNames),
          m_xNames(xNames),
          m_xData(xData),
          m_yData(yData),
          m_nPoints(nPoints),
          m_nVars((int)xNames.size()),
          m_nOutputs((int)expressions.size())
    {
        // 注意：这两个 vector 之后绝不 resize，muparser 内部保存了元素地址
        m_paramValues.assign(m_paramNames.size(), 0.0);
        m_xvals.assign((size_t)m_nVars, 0.0);

        m_parsers.resize((size_t)m_nOutputs);   // 一次到位，避免后续扩容搬移
        RebindVariables();

        for (int k = 0; k < m_nOutputs; ++k)
            m_parsers[(size_t)k].Eval();        // 试解析，语法错误提前暴露
    }

    // 拷贝构造：Eigen::NumericalDiff 会拷贝 functor，必须把每个 parser 的变量
    // 指针重新绑定到本副本的成员；否则副本写入自己的 vector，parser 却仍读源
    // 对象的旧值，导致残差恒定、Jacobian 全零（见 LmExprFunctor 处的说明）。
    LmExprFunctor2D(const LmExprFunctor2D& other)
        : Eigen::DenseFunctor<double>(other),
          m_expressions(other.m_expressions),
          m_paramNames(other.m_paramNames),
          m_xNames(other.m_xNames),
          m_xData(other.m_xData),
          m_yData(other.m_yData),
          m_nPoints(other.m_nPoints),
          m_nVars(other.m_nVars),
          m_nOutputs(other.m_nOutputs)
    {
        m_paramValues.assign(other.m_paramValues.begin(), other.m_paramValues.end());
        m_xvals.assign(other.m_xvals.begin(), other.m_xvals.end());
        m_parsers.resize((size_t)m_nOutputs);
        RebindVariables();
    }

    int operator()(const InputType& params, ValueType& fvec) const
    {
        for (int i = 0; i < m_inputs; ++i)
            m_paramValues[(size_t)i] = params(i);

        for (int h = 0; h < m_nPoints; ++h)
        {
            // 取第 h 个点的 M 个自变量
            for (int j = 0; j < m_nVars; ++j)
                m_xvals[(size_t)j] = m_xData[(size_t)(h * m_nVars + j)];

            // 该点的 K 个输出残差
            for (int k = 0; k < m_nOutputs; ++k)
                fvec(h * m_nOutputs + k) =
                    m_yData[(size_t)(h * m_nOutputs + k)] - m_parsers[(size_t)k].Eval();
        }
        return 0;
    }

private:
    void RebindVariables()
    {
        for (int k = 0; k < m_nOutputs; ++k)
        {
            mu::Parser& p = m_parsers[(size_t)k];
            p.SetExpr(m_expressions[(size_t)k]);
            for (int j = 0; j < m_nVars; ++j)
                p.DefineVar(m_xNames[(size_t)j], &m_xvals[(size_t)j]);
            for (int i = 0; i < m_inputs; ++i)
                p.DefineVar(m_paramNames[(size_t)i], &m_paramValues[(size_t)i]);
        }
    }

    std::vector<std::string> m_expressions;
    std::vector<std::string> m_paramNames;
    std::vector<std::string> m_xNames;
    std::vector<double>      m_xData;       // 点数 × M，行优先
    std::vector<double>      m_yData;       // 点数 × K，行优先
    int                      m_nPoints;
    int                      m_nVars;
    int                      m_nOutputs;

    mutable std::vector<double>     m_paramValues;
    mutable std::vector<double>     m_xvals;
    mutable std::vector<mu::Parser> m_parsers;
};

}   // anonymous namespace


Herror HCeigen_lm_fit_2d(Hproc_handle proc_handle)
{
    char const* const* exprArr;
    INT4_8 nExpr;
    char const* const* paramNameArr;
    INT4_8 nParam;
    double const* initVals;
    INT4_8 nInit;
    char const* const* xNameArr;
    INT4_8 nXName;
    double const* xData;
    INT4_8 nXData;
    double const* yDataArr;
    INT4_8 nYData;
    Hcpar  maxIterPar, epsPar;

    HAllocStringMem(proc_handle, 512);

    HGetPElemS(proc_handle, 1, CONV_NONE, &exprArr, &nExpr);
    HGetPElemS(proc_handle, 2, CONV_NONE, &paramNameArr, &nParam);
    HGetPElemD(proc_handle, 3, CONV_NONE, &initVals, &nInit);
    HGetPElemS(proc_handle, 4, CONV_NONE, &xNameArr, &nXName);
    HGetPElemD(proc_handle, 5, CONV_NONE, &xData, &nXData);
    HGetPElemD(proc_handle, 6, CONV_NONE, &yDataArr, &nYData);
    HGetSPar(proc_handle, 7, LONG_PAR, &maxIterPar, 1);
    HGetSPar(proc_handle, 8, DOUBLE_PAR, &epsPar, 1);

    INT4_8 maxIter = maxIterPar.par.l;
    double eps     = epsPar.par.d;

    // ---- 维度校验 ----
    // 名称里的 "2d" 指典型用途（二维定位 / 位移场），实现上自变量与输出
    // 个数均不设上限，因此像 r2 = u^2+v^2 这类派生量可以预计算后当自变量传入。
    if (nExpr < 1)
        return 30001;                               // 至少需要一个模型表达式
    if (nXName < 1)
        return 30002;                               // 至少需要一个自变量
    if (nParam < 1)
        return 30003;                               // 至少需要一个参数
    if (nInit != nParam)
        return 30004;                               // 初值数量与参数名数量不符
    if (nXData <= 0 || (nXData % nXName) != 0)
        return 30005;                               // 自变量数据长度须为自变量个数的整数倍

    INT4_8 nPoints = nXData / nXName;
    if (nYData != nPoints * nExpr)
        return 30006;                               // 观测数据长度须为 点数 × 输出个数
    if (nPoints < nParam)
        return 30007;                               // 数据点少于参数个数（欠定）
    if (maxIter <= 0)
        maxIter = 400 * (nParam + 1);               // Eigen LM 默认量级

    // ---- 名称校验 ----
    std::vector<std::string> expressions, paramNames, xNames;
    std::set<std::string>    seen;

    for (INT4_8 k = 0; k < nExpr; ++k)
    {
        const char* e = exprArr[k] ? exprArr[k] : "";
        if (e[0] == '\0')
            return 30008;                           // 表达式为空
        expressions.push_back(e);
    }

    for (INT4_8 j = 0; j < nXName; ++j)
    {
        const char* xn = xNameArr[j] ? xNameArr[j] : "";
        if (xn[0] == '\0')
            return 30009;                           // 自变量名为空
        if (!seen.insert(xn).second)
            return 30010;                           // 自变量名重复
        xNames.push_back(xn);
    }

    seen.clear();
    for (INT4_8 i = 0; i < nParam; ++i)
    {
        const char* pn = paramNameArr[i] ? paramNameArr[i] : "";
        if (pn[0] == '\0')
            return 30011;                           // 参数名为空
        if (!seen.insert(pn).second)
            return 30012;                           // 参数名重复
        paramNames.push_back(pn);
    }

    for (size_t i = 0; i < paramNames.size(); ++i)
        for (size_t j = 0; j < xNames.size(); ++j)
            if (paramNames[i] == xNames[j])
                return 30013;                       // 参数名与自变量名冲突

    // ---- 数据整理 ----
    std::vector<double> initV(initVals, initVals + nInit);
    std::vector<double> xv(xData, xData + nXData);
    std::vector<double> yv(yDataArr, yDataArr + nYData);

    // ---- 拟合（结果先给默认值，异常时保持可控输出） ----
    INT4_8 status     = 0;
    INT4_8 iterations = 0;
    double rss        = -1.0;
    std::string message;
    std::vector<double> outParams(initV);           // 默认回吐初值

    try
    {
        LmExprFunctor2D functor(expressions, paramNames, xNames, xv, yv, (int)nPoints);

        Eigen::NumericalDiff<LmExprFunctor2D> numDiff(functor, eps > 0.0 ? eps : 0.0);

        Eigen::LevenbergMarquardt<Eigen::NumericalDiff<LmExprFunctor2D> > lm(numDiff);
        lm.setMaxfev((Eigen::Index)maxIter);
        lm.setFtol(1e-12);
        lm.setXtol(1e-12);
        lm.setGtol(0.0);

        Eigen::VectorXd params((Eigen::Index)nParam);
        for (INT4_8 i = 0; i < nParam; ++i)
            params((Eigen::Index)i) = initV[(size_t)i];

        status     = (INT4_8)lm.minimize(params);
        iterations = (INT4_8)lm.iterations();

        Eigen::VectorXd residual((Eigen::Index)(nPoints * nExpr));
        functor(params, residual);
        rss = residual.squaredNorm();

        message = LmStatusMessage((int)status);
        if (!std::isfinite(rss))
            message += " | warning: residual sum of squares is not finite";

        outParams.assign(params.data(), params.data() + params.size());
    }
    catch (mu::ParserError& e)
    {
        status     = 0;
        iterations = 0;
        rss        = -1.0;
        message    = std::string("expression error: ") + e.GetMsg();
    }
    catch (const std::exception& e)
    {
        status     = 0;
        iterations = 0;
        rss        = -1.0;
        message    = std::string("error: ") + e.what();
    }
    catch (...)
    {
        status     = 0;
        iterations = 0;
        rss        = -1.0;
        message    = "error: unknown exception";
    }

    // ---- 输出 ----
    HPutElem(proc_handle, 1, outParams.data(), (INT4_8)outParams.size(), DOUBLE_PAR);
    HPutElem(proc_handle, 2, &rss, 1, DOUBLE_PAR);
    HPutElem(proc_handle, 3, &iterations, 1, LONG_PAR);
    HPutElem(proc_handle, 4, &status, 1, LONG_PAR);

    char* msgOut = NULL;
    HAllocTmp(proc_handle, &msgOut, (INT4_8)message.size() + 1);
    memcpy(msgOut, message.c_str(), message.size() + 1);
    HPutElem(proc_handle, 5, &msgOut, 1, STRING_PAR);
    HFreeTmp(proc_handle, msgOut, (INT4_8)message.size() + 1);

    return H_MSG_TRUE;
}

/*=============================================================================
 * linear_fit / linear_fit_2d — 通用线性最小二乘拟合
 *   Eigen ColPivHouseholderQR + muparser 运行时表达式解析。
 *   模型对参数必须线性（如 a+b*x+c*x^2），自变量可以非线性（如 exp(-x)）。
 *   不需要初值、不需要迭代；linear_fit 单自变量，linear_fit_2d 多自变量。
 *===========================================================================*/

namespace {

// ---- 列主元 Householder QR 求解最小二乘（cv::Mat 版）----
//   输入 Z: N×ncols 设计矩阵, F: N×1 观测向量（均会被原地修改）
//   返回 ncols×1 系数向量；outRank 返回数值秩。
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

// ---- 对参数线性的通用最小二乘拟合器 ----
class LinearFit
{
public:
    struct Result
    {
        std::vector<double> coefficients;
        double rss = -1.0;
        int rank = 0;
        bool success = false;
        std::string message;
    };

    static Result fit(
        const std::string& expression,
        const std::vector<std::string>& paramNames,
        const std::vector<std::string>& xNames,
        const std::vector<std::vector<double>>& xData,
        const std::vector<double>& yData,
        double linearTolerance = 1e-10)
    {
        Result result;

        const size_t sampleCount = xData.size();
        const size_t paramCount  = paramNames.size();
        const size_t xCount      = xNames.size();

        if (expression.empty())
        {
            result.message = "Expression is empty.";
            return result;
        }
        if (paramCount == 0)
        {
            result.message = "No parameters.";
            return result;
        }
        if (xCount == 0)
        {
            result.message = "No independent variables.";
            return result;
        }
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

        // 参数名不能重复
        {
            std::vector<std::string> names = paramNames;
            std::sort(names.begin(), names.end());
            if (std::adjacent_find(names.begin(), names.end()) != names.end())
            {
                result.message = "Duplicate parameter names.";
                return result;
            }
        }

        // 自变量名称不能重复
        {
            std::vector<std::string> names = xNames;
            std::sort(names.begin(), names.end());
            if (std::adjacent_find(names.begin(), names.end()) != names.end())
            {
                result.message = "Duplicate X variable names.";
                return result;
            }
        }

        // 创建 muParser
        mu::Parser parser;
        std::vector<double> params(paramCount, 0.0);
        std::vector<double> xValues(xCount, 0.0);

        try
        {
            for (size_t j = 0; j < paramCount; ++j)
                parser.DefineVar(paramNames[j], &params[j]);
            for (size_t j = 0; j < xCount; ++j)
                parser.DefineVar(xNames[j], &xValues[j]);
            parser.SetExpr(expression);
        }
        catch (const mu::Parser::exception_type& e)
        {
            result.message = std::string("Expression parse error: ") + e.GetMsg();
            return result;
        }

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
                for (size_t k = 0; k < xCount; ++k)
                    xValues[k] = xData[i][k];

                std::fill(params.begin(), params.end(), 0.0);
                double f0 = parser.Eval();
                if (!std::isfinite(f0))
                {
                    result.message = "Model evaluation returned non-finite value.";
                    return result;
                }
                F.at<double>((int)i, 0) = yData[i] - f0;

                for (size_t j = 0; j < paramCount; ++j)
                {
                    std::fill(params.begin(), params.end(), 0.0);
                    params[j] = 1.0;
                    double fj = parser.Eval();
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
                for (size_t k = 0; k < xCount; ++k)
                    xValues[k] = xData[i][k];

                std::fill(params.begin(), params.end(), 0.0);
                const double f0 = parser.Eval();

                for (size_t j = 0; j < paramCount; ++j)
                {
                    std::fill(params.begin(), params.end(), 0.0);
                    params[j] = 2.0;
                    const double f2 = parser.Eval();
                    const double basis = Z.at<double>((int)i, (int)j);
                    const double expected = f0 + 2.0 * basis;
                    const double error = std::abs(f2 - expected);
                    const double scale = (std::max)({1.0, std::abs(f2), std::abs(expected)});
                    if (error > linearTolerance * scale)
                    {
                        std::ostringstream oss;
                        oss << "Model is nonlinear in parameter '" << paramNames[j] << "'.";
                        result.message = oss.str();
                        return result;
                    }
                }

                for (size_t j = 0; j < paramCount; ++j)
                {
                    for (size_t k = j + 1; k < paramCount; ++k)
                    {
                        std::fill(params.begin(), params.end(), 0.0);
                        params[j] = 1.0;
                        params[k] = 1.0;
                        const double fij = parser.Eval();
                        const double basisJ = Z.at<double>((int)i, (int)j);
                        const double basisK = Z.at<double>((int)i, (int)k);
                        const double expected = f0 + basisJ + basisK;
                        const double error = std::abs(fij - expected);
                        const double scale = (std::max)({1.0, std::abs(fij), std::abs(expected)});
                        if (error > linearTolerance * scale)
                        {
                            std::ostringstream oss;
                            oss << "Model contains nonlinear parameter interaction between '"
                                << paramNames[j] << "' and '" << paramNames[k] << "'.";
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
};

}   // anonymous namespace


Herror HLinear_fit(Hproc_handle proc_handle)
{
    Hcpar  exprPar, xNamePar, tolPar;
    char const* const* paramNameArr;
    INT4_8 nParamNames;
    double const* xData;
    INT4_8 nX;
    double const* yData;
    INT4_8 nY;

    HAllocStringMem(proc_handle, 256);

    HGetSPar(proc_handle, 1, STRING_PAR, &exprPar, 1);
    HGetPElemS(proc_handle, 2, CONV_NONE, &paramNameArr, &nParamNames);
    HGetPElemD(proc_handle, 3, CONV_NONE, &xData, &nX);
    HGetPElemD(proc_handle, 4, CONV_NONE, &yData, &nY);
    HGetSPar(proc_handle, 5, STRING_PAR, &xNamePar, 1);
    HGetSPar(proc_handle, 6, DOUBLE_PAR, &tolPar, 1);

    const char* exprStr  = exprPar.par.s ? exprPar.par.s : "";
    const char* xNameStr = xNamePar.par.s ? xNamePar.par.s : "";
    double tol = tolPar.par.d;

    if (exprStr[0] == '\0')
        return 30001;                               // 表达式为空
    if (xNameStr[0] == '\0')
        xNameStr = "x";
    if (nParamNames < 1)
        return 30002;                               // 至少需要一个参数
    if (nX != nY)
        return 30003;                               // X / Y 观测数据长度不符
    if (nX < nParamNames)
        return 30004;                               // 数据点少于参数个数（欠定）
    if (tol <= 0.0)
        tol = 1e-10;

    std::vector<std::string> paramNames;
    std::set<std::string>    uniqueCheck;
    paramNames.reserve((size_t)nParamNames);
    for (INT4_8 i = 0; i < nParamNames; ++i)
    {
        const char* pn = paramNameArr[i] ? paramNameArr[i] : "";
        if (pn[0] == '\0')
            return 30005;                           // 参数名不能为空
        if (strcmp(pn, xNameStr) == 0)
            return 30006;                           // 参数名与自变量名冲突
        if (!uniqueCheck.insert(pn).second)
            return 30007;                           // 参数名重复
        paramNames.push_back(pn);
    }

    std::vector<std::string> xNames(1, xNameStr);
    std::vector<std::vector<double>> xd((size_t)nX);
    for (INT4_8 i = 0; i < nX; ++i)
        xd[(size_t)i] = { xData[i] };
    std::vector<double> yd(yData, yData + nY);

    LinearFit::Result res = LinearFit::fit(exprStr, paramNames, xNames, xd, yd, tol);

    std::vector<double> outParams = res.coefficients;

    double rss = res.rss;
    if (!std::isfinite(rss))
        rss = -1.0;
    INT4_8 rank    = (INT4_8)res.rank;
    INT4_8 success = res.success ? 1 : 0;

    HPutElem(proc_handle, 1, outParams.data(), (INT4_8)outParams.size(), DOUBLE_PAR);
    HPutElem(proc_handle, 2, &rss, 1, DOUBLE_PAR);
    HPutElem(proc_handle, 3, &rank, 1, LONG_PAR);
    HPutElem(proc_handle, 4, &success, 1, LONG_PAR);

    char* msgOut = NULL;
    HAllocTmp(proc_handle, &msgOut, (INT4_8)res.message.size() + 1);
    memcpy(msgOut, res.message.c_str(), res.message.size() + 1);
    HPutElem(proc_handle, 5, &msgOut, 1, STRING_PAR);
    HFreeTmp(proc_handle, msgOut, (INT4_8)res.message.size() + 1);

    return H_MSG_TRUE;
}


Herror HLinear_fit_2d(Hproc_handle proc_handle)
{
    Hcpar  exprPar, tolPar;
    char const* const* paramNameArr;
    INT4_8 nParamNames;
    double const* xData;
    INT4_8 nX;
    double const* yData;
    INT4_8 nY;
    double const* zData;
    INT4_8 nZ;

    HAllocStringMem(proc_handle, 256);

    HGetSPar(proc_handle, 1, STRING_PAR, &exprPar, 1);
    HGetPElemS(proc_handle, 2, CONV_NONE, &paramNameArr, &nParamNames);
    HGetPElemD(proc_handle, 3, CONV_NONE, &xData, &nX);
    HGetPElemD(proc_handle, 4, CONV_NONE, &yData, &nY);
    HGetPElemD(proc_handle, 5, CONV_NONE, &zData, &nZ);
    HGetSPar(proc_handle, 6, DOUBLE_PAR, &tolPar, 1);

    const char* exprStr = exprPar.par.s ? exprPar.par.s : "";
    double tol = tolPar.par.d;

    if (exprStr[0] == '\0')
        return 30001;                               // 表达式为空
    if (nParamNames < 1)
        return 30002;                               // 至少需要一个参数
    if (nX != nY || nX != nZ)
        return 30003;                               // X / Y / Z 数据长度须一致
    if (nX < nParamNames)
        return 30004;                               // 数据点少于参数个数（欠定）
    if (tol <= 0.0)
        tol = 1e-10;

    std::vector<std::string> paramNames;
    std::set<std::string>    uniqueP;
    paramNames.reserve((size_t)nParamNames);
    for (INT4_8 i = 0; i < nParamNames; ++i)
    {
        const char* pn = paramNameArr[i] ? paramNameArr[i] : "";
        if (pn[0] == '\0')
            return 30005;                           // 参数名不能为空
        if (strcmp(pn, "x") == 0 || strcmp(pn, "y") == 0)
            return 30006;                           // 参数名与自变量名 x/y 冲突
        if (!uniqueP.insert(pn).second)
            return 30007;                           // 参数名重复
        paramNames.push_back(pn);
    }

    std::vector<std::string> xNames = { "x", "y" };
    std::vector<std::vector<double>> xd((size_t)nX);
    for (INT4_8 i = 0; i < nX; ++i)
        xd[(size_t)i] = { xData[i], yData[i] };
    std::vector<double> zd(zData, zData + nZ);

    LinearFit::Result res = LinearFit::fit(exprStr, paramNames, xNames, xd, zd, tol);

    std::vector<double> outParams = res.coefficients;

    double rss = res.rss;
    if (!std::isfinite(rss))
        rss = -1.0;
    INT4_8 rank    = (INT4_8)res.rank;
    INT4_8 success = res.success ? 1 : 0;

    HPutElem(proc_handle, 1, outParams.data(), (INT4_8)outParams.size(), DOUBLE_PAR);
    HPutElem(proc_handle, 2, &rss, 1, DOUBLE_PAR);
    HPutElem(proc_handle, 3, &rank, 1, LONG_PAR);
    HPutElem(proc_handle, 4, &success, 1, LONG_PAR);

    char* msgOut = NULL;
    HAllocTmp(proc_handle, &msgOut, (INT4_8)res.message.size() + 1);
    memcpy(msgOut, res.message.c_str(), res.message.size() + 1);
    HPutElem(proc_handle, 5, &msgOut, 1, STRING_PAR);
    HFreeTmp(proc_handle, msgOut, (INT4_8)res.message.size() + 1);

    return H_MSG_TRUE;
}
