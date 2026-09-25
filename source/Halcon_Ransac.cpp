/*=============================================================================
 * Halcon_Ransac.cpp — HALCON RANSAC 拟合算子（直接调用 cv_region 的 C++ API）
 *
 *   cv_ransac_fit —— RANSAC 通用几何拟合（显式 y=f(x) / 隐式 F(x,y)=0，
 *   muparser 表达式模型、几何距离残差、动态迭代、LM 精化、随机种子）
 *
 * 约定:
 *   - supply 过程命名 Hcv_ransac_fit，由 Halcon_SoftwarePackage.c 中
 *     CHcv_ransac_fit 包装
 *   - ransac 核心已并入 cvr_core 静态库（ransac::ransac_run），本文件只做
 *     HALCON 参数读取与结果写出；异常不穿越 supply，统一映射为状态码
 *===========================================================================*/

#include "HalconCpp.h"
#include "HDevThread.h"
#include "Halcon_SoftwarePackage.h"

#include "ransac_core.h"   // ransac::CoreOptions/CoreResult/ransac_run

#include <muParser.h>          // mu::ParserError 异常映射

#include <cstring>
#include <string>
#include <vector>

#define RANSAC_ERR_PARAM   10201
#define RANSAC_ERR_OP      10202

Herror Hcv_ransac_fit(Hproc_handle proc_handle)
{
    /* 读取字符串参数前必须分配字符串内存（手册 5.5.10） */
    HAllocStringMem(proc_handle, 1024);

    /* ---- 输入控制参数 ----
     * 1 ModelExpression(string)   2 ParamNames(string tuple)
     * 3 InitialValues(real tuple) 4 XData(real tuple)  5 YData(real tuple)
     * 6 XName(string)             7 Threshold(real)    8 MaxIter(integer)
     * 9 OutlierRatio(real)        10 ModelType(int)    11 YName(string)
     * 12 MinSampleSize(int)       13 Confidence(real)  14 Seed(int) */
    Hcpar expr_par, xname_par, yname_par;
    HGetSPar(proc_handle, 1, STRING_PAR, &expr_par, 1);
    HGetSPar(proc_handle, 6, STRING_PAR, &xname_par, 1);
    HGetSPar(proc_handle, 11, STRING_PAR, &yname_par, 1);

    char const* const* param_name_arr = nullptr;
    INT4_8 n_param_names = 0;
    HGetPElemS(proc_handle, 2, CONV_NONE, &param_name_arr, &n_param_names);

    double const* init_vals = nullptr;
    INT4_8 n_init = 0;
    HGetPElemD(proc_handle, 3, CONV_CAST, &init_vals, &n_init);

    double const* x_data = nullptr;
    INT4_8 n_x = 0;
    HGetPElemD(proc_handle, 4, CONV_CAST, &x_data, &n_x);

    double const* y_data = nullptr;
    INT4_8 n_y = 0;
    HGetPElemD(proc_handle, 5, CONV_CAST, &y_data, &n_y);

    Hcpar thr_par, iter_par, outl_par, mtype_par, minsmp_par, conf_par, seed_par;
    HGetSPar(proc_handle, 7, DOUBLE_PAR, &thr_par, 1);
    HGetSPar(proc_handle, 8, LONG_PAR, &iter_par, 1);
    HGetSPar(proc_handle, 9, DOUBLE_PAR, &outl_par, 1);
    HGetSPar(proc_handle, 10, LONG_PAR, &mtype_par, 1);
    HGetSPar(proc_handle, 12, LONG_PAR, &minsmp_par, 1);
    HGetSPar(proc_handle, 13, DOUBLE_PAR, &conf_par, 1);
    HGetSPar(proc_handle, 14, LONG_PAR, &seed_par, 1);

    const char* expr_str  = expr_par.par.s ? expr_par.par.s : "";
    const char* xname_str = xname_par.par.s ? xname_par.par.s : "x";
    const char* yname_str = yname_par.par.s ? yname_par.par.s : "y";

    /* XData/YData 长度不匹配属于真·误用（无法确定 nPoints），返回算子错误 */
    if (n_x != n_y) {
        HSetErrText(const_cast<char*>("cv_ransac_fit: XData/YData length mismatch"));
        return RANSAC_ERR_PARAM;
    }

    const int n_params = static_cast<int>(n_param_names);
    const int n_points = static_cast<int>(n_x);

    /* ModelType / MinSampleSize 解析（0 = 自动） */
    const int model_type_raw = static_cast<int>(mtype_par.par.l);
    int min_sample = static_cast<int>(minsmp_par.par.l);
    if (min_sample <= 0) min_sample = n_params > 0 ? n_params : 1;

    /* ---- 等价于原 ransac_fit_once + ransac_fit 的选项组装 ---- */
    ransac::CoreOptions opt;
    opt.modelExpression = expr_str;
    opt.xName           = xname_str;
    opt.yName           = yname_str;
    opt.paramNames.assign(param_name_arr, param_name_arr + n_param_names);
    opt.implicit        = (model_type_raw == 1);
    opt.useVertical     = false;   // 固定几何残差（与原 RANSAC_RESIDUAL_GEOMETRIC 一致）
    if (n_init == n_param_names)
        opt.initialValues.assign(init_vals, init_vals + n_init);
    opt.minSampleSize   = min_sample;
    opt.threshold       = thr_par.par.d;
    opt.maxIterations   = static_cast<int>(iter_par.par.l);
    opt.outlierRatio    = outl_par.par.d;
    opt.confidence      = (conf_par.par.d > 0.0) ? conf_par.par.d : 0.99;
    opt.seed            = static_cast<unsigned int>(seed_par.par.l);
    opt.solverType      = RANSAC_SOLVER_AUTO;
    opt.maxSolverIterations = 0;   // 0 = 核心默认（50 + 20*nParams）
    opt.solverTolerance     = 0.0;
    opt.enableRefinement    = true;
    opt.maxRefineIterations = 0;   // 0 = 核心默认（5）
    opt.maxExpressionLength = 0;   // 0 = 核心默认
    opt.maxExpressionEvals  = 0;

    std::vector<double> param_vals(static_cast<size_t>(n_params), 0.0);
    std::vector<unsigned char> inlier_mask(static_cast<size_t>(n_points), 0);
    double rss = 0.0;
    int    iters = 0;
    RansacStatus status = RANSAC_OK;
    std::string  msg;

    /* 与原 ransac_fit 相同的前置校验（状态码一致，不返回算子错误） */
    if (min_sample < 1 || n_points < min_sample || n_points < n_params) {
        status = RANSAC_ERR_NOT_ENOUGH_POINTS;
        msg = "not enough points";
    } else if (!(opt.threshold > 0.0) || opt.maxIterations <= 0 ||
               !(opt.outlierRatio >= 0.0 && opt.outlierRatio < 1.0) ||
               !(opt.confidence >= 0.0 && opt.confidence < 1.0)) {
        status = RANSAC_ERR_INVALID_ARG;
        msg = "invalid ransac parameters";
    } else {
        try {
            std::vector<double> xs(x_data, x_data + n_points);
            std::vector<double> ys(y_data, y_data + n_points);

            const ransac::CoreResult res = ransac::ransac_run(opt, xs, ys);

            for (size_t j = 0; j < res.paramValues.size() && j < param_vals.size(); ++j)
                param_vals[j] = res.paramValues[j];
            for (size_t i = 0; i < res.inlierMask.size() && i < inlier_mask.size(); ++i)
                inlier_mask[i] = res.inlierMask[i];
            rss    = res.residualSum;
            iters  = res.iterations;
            status = res.status;
            msg    = res.statusMessage;
        }
        catch (const mu::ParserError& e) {
            status = RANSAC_ERR_EXPR_PARSE;
            msg    = std::string("expression parse error: ") + e.GetMsg();
        }
        catch (const ransac::ExprInvalid& e) {
            status = RANSAC_ERR_EXPR_PARSE;
            msg    = e.what();
        }
        catch (const std::bad_alloc&) {
            status = RANSAC_ERR_INTERNAL;
            msg    = "out of memory";
        }
        catch (const std::exception& e) {
            status = RANSAC_ERR_INTERNAL;
            msg    = e.what();
        }
        catch (...) {
            status = RANSAC_ERR_INTERNAL;
            msg    = "unknown error";
        }
        if (status == RANSAC_OK && n_params > 0 && param_vals.empty())
            { status = RANSAC_ERR_INTERNAL; msg = "no result"; }
    }

    /* ---- 输出控制参数 ----
     * 1 ParamValues  2 InlierMask  3 ResidualSum  4 Iterations  5 Status
     * 6 StatusMessage  7 InlierRatio */
    HPutElem(proc_handle, 1, param_vals.data(),
             static_cast<INT4_8>(n_params), DOUBLE_PAR);
    {
        std::vector<INT4_8> mask_i(static_cast<size_t>(n_points), 0);
        for (int i = 0; i < n_points; ++i)
            mask_i[static_cast<size_t>(i)] =
                inlier_mask[static_cast<size_t>(i)] ? 1 : 0;
        HPutElem(proc_handle, 2, mask_i.data(),
                 static_cast<INT4_8>(n_points), LONG_PAR);
    }
    HPutElem(proc_handle, 3, &rss, 1, DOUBLE_PAR);
    {
        INT4_8 iters_out = iters, status_out = (INT4_8)status;
        HPutElem(proc_handle, 4, &iters_out, 1, LONG_PAR);
        HPutElem(proc_handle, 5, &status_out, 1, LONG_PAR);
    }
    {
        /* HPutElem STRING_PAR 需要 char**（见 Halcon_Math.cpp 的 msgOut 模式） */
        char* msgOut = const_cast<char*>(msg.c_str());
        HPutElem(proc_handle, 6, &msgOut, 1, STRING_PAR);
    }
    {
        const double ratio =
            (n_points > 0)
                ? [&] {
                      int c = 0;
                      for (int i = 0; i < n_points; ++i)
                          if (inlier_mask[static_cast<size_t>(i)]) ++c;
                      return (double)c / n_points;
                  }()
                : 0.0;
        HPutElem(proc_handle, 7, &ratio, 1, DOUBLE_PAR);
    }

    return H_MSG_TRUE;
}
