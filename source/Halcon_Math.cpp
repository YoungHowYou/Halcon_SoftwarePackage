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

#include "cvflow/fit.hpp"
#include "Halcon_Def.h"
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




/* FitModel 句柄类型的唯一实体（Halcon_Def.h 中为 extern 声明） */
static Herror FitModelHUserHandleDestructor(Hproc_handle ph, FitModelHUserHandleData *data)
{
    if (data->ptr)
    {
        if (data->kind == cvflow::FIT_MODEL_LM)
            cvflow::LmModel::destroy(static_cast<cvflow::LmModel*>(data->ptr));
        else if (data->kind == cvflow::FIT_MODEL_LINEAR)
            cvflow::LinearModel::destroy(static_cast<cvflow::LinearModel*>(data->ptr));
        else if (data->kind == cvflow::FIT_MODEL_GEOM)
            ransac_destroy(static_cast<RansacHandle>(data->ptr));
        data->ptr = NULL;
    }
    return HFree(ph, data);
}
extern "C" const HHandleInfo FitModelHandleTypeUser =
    HANDLE_INFO_INITIALIZER_NOSER(H_FitModel_TAG, H_FitModel_SEM_TYPE,
                                  FitModelHUserHandleDestructor, NULL, NULL);

/*=============================================================================
 * 两步式拟合算子（2026-09-30，替代已删除的 eigen_lm_fit/linear_fit 一步到位族）：
 *   cv_lm_create / cv_lm_fit —— LM 非线性（M 自变量 × K 输出）
 *   cv_linear_create / cv_linear_fit —— 线性最小二乘（1~2 自变量）
 *   cv_fit_clear —— 显式释放句柄原生资源
 * 句柄：Halcon_Def.h 的 FitModel 用户对象句柄；模型本体（编译后的表达式）
 * 在 cv_flow，建模后整个生命周期只解析一次。
 *===========================================================================*/

Herror Hcv_lm_create(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 512);

    char const* const* exprArr = nullptr;   INT4_8 nExpr = 0;
    char const* const* paramArr = nullptr;  INT4_8 nParam = 0;
    char const* const* xnameArr = nullptr;  INT4_8 nXName = 0;
    HGetPElemS(proc_handle, 1, CONV_NONE, &exprArr, &nExpr);
    HGetPElemS(proc_handle, 2, CONV_NONE, &paramArr, &nParam);
    HGetPElemS(proc_handle, 3, CONV_NONE, &xnameArr, &nXName);

    std::vector<std::string> exprs, params, xnames;
    exprs.reserve((size_t)nExpr);  params.reserve((size_t)nParam);  xnames.reserve((size_t)nXName);
    for (INT4_8 k = 0; k < nExpr;  ++k) exprs.emplace_back(exprArr[k] ? exprArr[k] : "");
    for (INT4_8 i = 0; i < nParam; ++i) params.emplace_back(paramArr[i] ? paramArr[i] : "");
    for (INT4_8 j = 0; j < nXName; ++j) xnames.emplace_back(xnameArr[j] ? xnameArr[j] : "");

    int errCode = 0;
    std::string errMsg;
    cvflow::LmModel* model = cvflow::LmModel::create(exprs, params, xnames, errCode, errMsg);
    if (!model)
    {
        HSetErrText(const_cast<char*>(errMsg.c_str()));
        return 30000 + errCode;
    }

    Def_OUTFitModel(4, hOut);
    OUTFitModel(hOut)->kind = cvflow::FIT_MODEL_LM;
    OUTFitModel(hOut)->ptr  = model;
    OUTFitModel(hOut)->aux  = 0;
    return H_MSG_TRUE;
}

Herror Hcv_lm_fit(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 512);

    Def_INFitModel(1, hIn);
    if (!hIn || hIn->kind != cvflow::FIT_MODEL_LM || !hIn->ptr)
    {
        HSetErrText(const_cast<char*>("cv_lm_fit: ModelHandle 不是有效的 LM 模型句柄（或已 clear）"));
        return 30001;
    }
    const cvflow::LmModel& model = *static_cast<cvflow::LmModel*>(hIn->ptr);

    double const* px = nullptr; INT4_8 nx = 0;
    double const* py = nullptr; INT4_8 ny = 0;
    double const* pz = nullptr; INT4_8 nz = 0;
    double const* pi = nullptr; INT4_8 ni = 0;
    HGetPElemD(proc_handle, 2, CONV_CAST, &px, &nx);
    HGetPElemD(proc_handle, 3, CONV_CAST, &py, &ny);
    HGetPElemD(proc_handle, 4, CONV_CAST, &pz, &nz);
    HGetPElemD(proc_handle, 5, CONV_CAST, &pi, &ni);

    Hcpar maxIterPar, epsPar;
    HGetSPar(proc_handle, 6, LONG_PAR, &maxIterPar, 1);
    HGetSPar(proc_handle, 7, DOUBLE_PAR, &epsPar, 1);

    const std::vector<double> X(px, px + nx), Y(py, py + ny), Z(pz, pz + nz);
    const std::vector<double> init(pi, pi + ni);

    // 按 (M, K) 组装拉平数据：
    //   K=1, M=1：X=自变量, Y=观测（Z 空）
    //   K=1, M=2：X、Y=两个自变量, Z=观测
    //   K>1    ：X=自变量拉平(点数×M), Y=观测拉平(点数×K)（Z 空）
    const int M = model.varCount(), K = model.outputCount();
    std::vector<double> xFlat, yFlat;
    int shapeErr = 0;
    if (K == 1)
    {
        if (M == 1)
        {
            if (nz == 0 && ny > 0) { xFlat = X; yFlat = Y; }
            else shapeErr = 21;
        }
        else if (M == 2)
        {
            if (nx == ny && nz == nx && nz > 0)
            {
                xFlat.reserve((size_t)nx * 2);
                for (INT4_8 i = 0; i < nx; ++i) { xFlat.push_back(X[(size_t)i]); xFlat.push_back(Y[(size_t)i]); }
                yFlat = Z;
            }
            else shapeErr = 21;
        }
        else shapeErr = 22;   // M>2 暂不支持 X,Y,Z 模式（请用拉平：K>1 分支）
    }
    else
    {
        if (nz == 0) { xFlat = X; yFlat = Y; }
        else shapeErr = 21;
    }
    if (shapeErr)
    {
        HSetErrText(const_cast<char*>("cv_lm_fit: X/Y/Z 与模型维度不匹配（K=1,M=1 传 X,Y；K=1,M=2 传 X,Y,Z；K>1 传拉平 X,Y）"));
        return 30000 + shapeErr;
    }

    cvflow::LmFitResult res;
    const int rc = cvflow::lm_model_fit(model, xFlat, yFlat, init,
                                        maxIterPar.par.l, epsPar.par.d, res);
    if (rc != 0)
    {
        HSetErrText(const_cast<char*>("cv_lm_fit: 数据校验失败（1 初值数≠参数数 / 2 自变量长度非法 / 3 观测长度≠点数×K / 4 欠定）"));
        return 30000 + rc;
    }

    HPutElem(proc_handle, 1, res.params.data(), (INT4_8)res.params.size(), DOUBLE_PAR);
    HPutElem(proc_handle, 2, &res.rss, 1, DOUBLE_PAR);
    INT4_8 iters = (INT4_8)res.iterations, status = (INT4_8)res.status;
    HPutElem(proc_handle, 3, &iters, 1, LONG_PAR);
    HPutElem(proc_handle, 4, &status, 1, LONG_PAR);
    char* msgOut = NULL;
    HAllocTmp(proc_handle, &msgOut, (INT4_8)res.message.size() + 1);
    memcpy(msgOut, res.message.c_str(), res.message.size() + 1);
    HPutElem(proc_handle, 5, &msgOut, 1, STRING_PAR);
    HFreeTmp(proc_handle, msgOut, (INT4_8)res.message.size() + 1);

    return H_MSG_TRUE;
}

Herror Hcv_linear_create(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 512);

    Hcpar exprPar;
    HGetSPar(proc_handle, 1, STRING_PAR, &exprPar, 1);
    char const* const* paramArr = nullptr;  INT4_8 nParam = 0;
    char const* const* xnameArr = nullptr;  INT4_8 nXName = 0;
    HGetPElemS(proc_handle, 2, CONV_NONE, &paramArr, &nParam);
    HGetPElemS(proc_handle, 3, CONV_NONE, &xnameArr, &nXName);

    std::vector<std::string> params, xnames;
    params.reserve((size_t)nParam);  xnames.reserve((size_t)nXName);
    for (INT4_8 i = 0; i < nParam; ++i) params.emplace_back(paramArr[i] ? paramArr[i] : "");
    for (INT4_8 j = 0; j < nXName; ++j) xnames.emplace_back(xnameArr[j] ? xnameArr[j] : "");

    int errCode = 0;
    std::string errMsg;
    cvflow::LinearModel* model = cvflow::LinearModel::create(
        exprPar.par.s ? exprPar.par.s : "", params, xnames, errCode, errMsg);
    if (!model)
    {
        HSetErrText(const_cast<char*>(errMsg.c_str()));
        return 30000 + errCode;
    }

    Def_OUTFitModel(4, hOut);
    OUTFitModel(hOut)->kind = cvflow::FIT_MODEL_LINEAR;
    OUTFitModel(hOut)->ptr  = model;
    OUTFitModel(hOut)->aux  = 0;
    return H_MSG_TRUE;
}

Herror Hcv_linear_fit(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 512);

    Def_INFitModel(1, hIn);
    if (!hIn || hIn->kind != cvflow::FIT_MODEL_LINEAR || !hIn->ptr)
    {
        HSetErrText(const_cast<char*>("cv_linear_fit: ModelHandle 不是有效的线性模型句柄（或已 clear）"));
        return 30001;
    }
    const cvflow::LinearModel& model = *static_cast<cvflow::LinearModel*>(hIn->ptr);

    double const* px = nullptr; INT4_8 nx = 0;
    double const* py = nullptr; INT4_8 ny = 0;
    double const* pz = nullptr; INT4_8 nz = 0;
    HGetPElemD(proc_handle, 2, CONV_CAST, &px, &nx);
    HGetPElemD(proc_handle, 3, CONV_CAST, &py, &ny);
    HGetPElemD(proc_handle, 4, CONV_CAST, &pz, &nz);
    Hcpar tolPar;
    HGetSPar(proc_handle, 5, DOUBLE_PAR, &tolPar, 1);

    // 一维：X=自变量, Y=观测（Z 空）；二维：X、Y=两个自变量, Z=观测
    std::vector<std::vector<double>> rows;
    std::vector<double> y;
    int shapeErr = 0;
    if (model.varCount() == 1)
    {
        if (nz != 0) shapeErr = 21;
        else if (nx != ny || nx == 0) shapeErr = 22;
        else
        {
            rows.resize((size_t)nx);
            for (INT4_8 i = 0; i < nx; ++i) rows[(size_t)i] = { px[i] };
            y.assign(py, py + ny);
        }
    }
    else
    {
        if (!(nx == ny && ny == nz) || nx == 0) shapeErr = 22;
        else
        {
            rows.resize((size_t)nx);
            for (INT4_8 i = 0; i < nx; ++i) rows[(size_t)i] = { px[i], py[i] };
            y.assign(pz, pz + nz);
        }
    }
    if (shapeErr)
    {
        HSetErrText(const_cast<char*>("cv_linear_fit: X/Y/Z 与模型维度不匹配（一维传 X,Y；二维传 X,Y,Z）"));
        return 30000 + shapeErr;
    }

    cvflow::LinearFitResult res = cvflow::linear_model_fit(model, rows, y, tolPar.par.d);

    HPutElem(proc_handle, 1, res.coefficients.data(), (INT4_8)res.coefficients.size(), DOUBLE_PAR);
    HPutElem(proc_handle, 2, &res.rss, 1, DOUBLE_PAR);
    INT4_8 rank = (INT4_8)res.rank, success = res.success ? 1 : 0;
    HPutElem(proc_handle, 3, &rank, 1, LONG_PAR);
    HPutElem(proc_handle, 4, &success, 1, LONG_PAR);
    char* msgOut = NULL;
    HAllocTmp(proc_handle, &msgOut, (INT4_8)res.message.size() + 1);
    memcpy(msgOut, res.message.c_str(), res.message.size() + 1);
    HPutElem(proc_handle, 5, &msgOut, 1, STRING_PAR);
    HFreeTmp(proc_handle, msgOut, (INT4_8)res.message.size() + 1);

    return H_MSG_TRUE;
}

Herror Hcv_fit_clear(Hproc_handle proc_handle)
{
    Def_INFitModel(1, hIn);
    if (hIn && hIn->ptr)
    {
        if (hIn->kind == cvflow::FIT_MODEL_LM)
            cvflow::LmModel::destroy(static_cast<cvflow::LmModel*>(hIn->ptr));
        else if (hIn->kind == cvflow::FIT_MODEL_LINEAR)
            cvflow::LinearModel::destroy(static_cast<cvflow::LinearModel*>(hIn->ptr));
        else if (hIn->kind == cvflow::FIT_MODEL_GEOM)
            ransac_destroy(static_cast<RansacHandle>(hIn->ptr));
        hIn->ptr = NULL;   // 句柄壳由 HALCON 句柄 GC 回收，析构见 ptr=NULL 不再重复释放
    }
    return H_MSG_TRUE;
}
