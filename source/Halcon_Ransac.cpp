/*=============================================================================
 * Halcon_Ransac.cpp — HALCON 几何拟合算子（两步式，2026-09-30）
 *
 *   cv_geom_create —— 构建 RANSAC 几何拟合模型：表达式白名单校验 + 预编译 +
 *                     最小采样数自动推导（muparser 整个生命周期只解析一次），
 *                     返回 FitModel 用户对象句柄（内部持有 RansacHandle）
 *   cv_geom_fit    —— 用模型句柄做 RANSAC 拟合（动态迭代 + LM 精化），
 *                     零表达式解析开销；运行参数（阈值/迭代/种子等）全在 fit 步
 *   cv_fit_clear   —— 释放句柄（实现见 Halcon_Math.cpp，三族共用）
 *
 * 约定:
 *   - supply 过程命名 Hcv_geom_create / Hcv_geom_fit，由 Halcon_SoftwarePackage.c
 *     中 CHcv_geom_create / CHcv_geom_fit 包装
 *   - 表达式模型本体在 cv_flow（ransac_create/ransac_fit 句柄式 C ABI）
 *===========================================================================*/

#include "HalconCpp.h"
#include "HDevThread.h"
#include "Halcon_SoftwarePackage.h"
#include "Halcon_Def.h"            // Def_INFitModel / Def_OUTFitModel

#include "cvflow/ransac.hpp"

#include <cstring>
#include <string>
#include <vector>

#define GEOM_ERR_PARAM   10201

/* 与 Halcon_OpenCV.cpp 同款的「字符串或整数」枚举读取（HGetSPar 是语句宏，
 * 失败时宏内 return 本函数的错误码，故可用返回值区分两种类型） */
static Herror geom_fetch_str(Hproc_handle ph, INT4_8 par, std::string& out)
{
    Hcpar p;
    HGetSPar(ph, par, STRING_PAR, &p, 1);
    out = p.par.s ? p.par.s : "";
    return H_MSG_TRUE;
}
static Herror geom_fetch_long(Hproc_handle ph, INT4_8 par, long& out)
{
    Hcpar p;
    HGetSPar(ph, par, LONG_PAR, &p, 1);
    out = p.par.l;
    return H_MSG_TRUE;
}
static bool geom_read_enum(Hproc_handle ph, INT4_8 par, int* out)
{
    std::string s;
    if (geom_fetch_str(ph, par, s) == H_MSG_OK && !s.empty())
    {
        std::string t(s);
        for (auto& c : t) c = (char)std::tolower((unsigned char)c);
        if (t == "explicit" || t == "0") { *out = 0; return true; }
        if (t == "implicit" || t == "1") { *out = 1; return true; }
        if (t == "vertical"  ) { *out = 0; return true; }
        if (t == "geometric" ) { *out = 1; return true; }
        char* end = nullptr;
        const long v = std::strtol(t.c_str(), &end, 10);
        if (end && *end == '\0') { *out = (int)v; return true; }
        return false;
    }
    long v = 0;
    if (geom_fetch_long(ph, par, v) == H_MSG_OK) { *out = (int)v; return true; }
    return false;
}

Herror Hcv_geom_create(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024);

    /* 1 ModelExpression  2 XName  3 YName  4 ParamNames
     * 5 ModelType        6 ResidualType      : 7 ModelHandle */
    Hcpar exprPar, xnamePar, ynamePar;
    HGetSPar(proc_handle, 1, STRING_PAR, &exprPar, 1);
    HGetSPar(proc_handle, 2, STRING_PAR, &xnamePar, 1);
    HGetSPar(proc_handle, 3, STRING_PAR, &ynamePar, 1);

    char const* const* paramArr = nullptr;
    INT4_8 nParam = 0;
    HGetPElemS(proc_handle, 4, CONV_NONE, &paramArr, &nParam);

    int modelType = 0, residualType = 1;
    if (!geom_read_enum(proc_handle, 5, &modelType))
    {
        HSetErrText(const_cast<char*>("cv_geom_create: ModelType 必须是 'explicit'/'implicit'（或 0/1）"));
        return 30005;
    }
    if (!geom_read_enum(proc_handle, 6, &residualType))
    {
        HSetErrText(const_cast<char*>("cv_geom_create: ResidualType 必须是 'vertical'/'geometric'（或 0/1）"));
        return 30006;
    }

    std::vector<const char*> names;
    names.reserve((size_t)nParam);
    for (INT4_8 i = 0; i < nParam; ++i) names.push_back(paramArr[i] ? paramArr[i] : "");

    char errBuf[256] = {0};
    RansacHandle h = ransac_create(exprPar.par.s ? exprPar.par.s : "",
                                   xnamePar.par.s ? xnamePar.par.s : "x",
                                   ynamePar.par.s ? ynamePar.par.s : "",
                                   names.empty() ? nullptr : names.data(),
                                   (int)names.size(),
                                   (modelType == 1) ? RANSAC_MODEL_IMPLICIT
                                                    : RANSAC_MODEL_EXPLICIT,
                                   errBuf, sizeof(errBuf));
    if (!h)
    {
        HSetErrText(errBuf[0] ? errBuf : const_cast<char*>("cv_geom_create: 模型构建失败"));
        return 30007;
    }

    Def_OUTFitModel(7, hOut);
    OUTFitModel(hOut)->kind = cvflow::FIT_MODEL_GEOM;
    OUTFitModel(hOut)->ptr  = h;
    OUTFitModel(hOut)->aux  = residualType;   // fit 步作为 RansacOptions.residualType
    return H_MSG_TRUE;
}

Herror Hcv_geom_fit(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024);

    /* 1 ModelHandle  2 XData  3 YData  4 InitialValues
     * 5 Threshold  6 MaxIter  7 OutlierRatio  8 Confidence  9 Seed
     * : 1 ParamValues 2 InlierMask 3 ResidualSum 4 Iterations 5 Status 6 StatusMessage 7 InlierRatio */
    Def_INFitModel(1, hIn);
    if (!hIn || hIn->kind != cvflow::FIT_MODEL_GEOM || !hIn->ptr)
    {
        HSetErrText(const_cast<char*>("cv_geom_fit: ModelHandle 不是有效的几何模型句柄（或已 clear）"));
        return 30001;
    }

    double const* xData = nullptr; INT4_8 nX = 0;
    double const* yData = nullptr; INT4_8 nY = 0;
    double const* initV = nullptr; INT4_8 nInit = 0;
    HGetPElemD(proc_handle, 2, CONV_CAST, &xData, &nX);
    HGetPElemD(proc_handle, 3, CONV_CAST, &yData, &nY);
    HGetPElemD(proc_handle, 4, CONV_CAST, &initV, &nInit);

    Hcpar thrPar, iterPar, outlPar, confPar, seedPar;
    HGetSPar(proc_handle, 5, DOUBLE_PAR, &thrPar, 1);
    HGetSPar(proc_handle, 6, LONG_PAR,   &iterPar, 1);
    HGetSPar(proc_handle, 7, DOUBLE_PAR, &outlPar, 1);
    HGetSPar(proc_handle, 8, DOUBLE_PAR, &confPar, 1);
    HGetSPar(proc_handle, 9, LONG_PAR,   &seedPar, 1);

    if (nX != nY)
    {
        HSetErrText(const_cast<char*>("cv_geom_fit: XData/YData 长度不一致"));
        return GEOM_ERR_PARAM;
    }

    RansacOptions opt;
    std::memset(&opt, 0, sizeof(opt));
    opt.MinSampleSize   = ransac_get_required_min_sample((RansacHandle)hIn->ptr);
    opt.Threshold       = thrPar.par.d;
    opt.MaxIterations   = (int)iterPar.par.l;
    opt.OutlierRatio    = outlPar.par.d;
    opt.Confidence      = (confPar.par.d > 0.0) ? confPar.par.d : 0.99;
    opt.Seed            = (unsigned int)seedPar.par.l;
    opt.ModelType       = RANSAC_MODEL_EXPLICIT;   // 句柄创建时已编译模型类型
    opt.ResidualType    = (hIn->aux == 0) ? RANSAC_RESIDUAL_VERTICAL
                                          : RANSAC_RESIDUAL_GEOMETRIC;
    opt.SolverType      = RANSAC_SOLVER_AUTO;
    opt.MaxSolverIterations = 0;   // 0 = 核心默认（50 + 20*nParams）
    opt.SolverTolerance     = 0.0;
    opt.EnableRefinement    = 1;
    opt.MaxRefineIterations = 0;   // 0 = 核心默认（5）
    opt.MaxExpressionLength = 0;
    opt.MaxExpressionEvals  = 0;
    opt.InitialValues       = (nInit > 0) ? initV : nullptr;

    const int nParams = ransac_get_param_count((RansacHandle)hIn->ptr);
    std::vector<double> paramVals((size_t)(nParams > 0 ? nParams : 1), 0.0);
    std::vector<unsigned char> inlierMask((size_t)nX, 0);

    RansacResult result;
    std::memset(&result, 0, sizeof(result));
    result.ParamValues    = paramVals.data();
    result.ParamValuesLen = paramVals.size();
    result.InlierMask     = inlierMask.data();
    result.InlierMaskLen  = inlierMask.size();
    char msgBuf[512] = {0};
    result.StatusMessage    = msgBuf;
    result.StatusMessageLen = sizeof(msgBuf);

    /* 前置校验与旧一步到位算子同口径：不返回算子错误，而是 Status 状态码 */
    if (nX < opt.MinSampleSize) {
        result.Status = RANSAC_ERR_NOT_ENOUGH_POINTS;
        std::snprintf(msgBuf, sizeof(msgBuf), "not enough points");
    } else if (!(opt.Threshold > 0.0) || opt.MaxIterations <= 0 ||
               !(opt.OutlierRatio >= 0.0 && opt.OutlierRatio < 1.0) ||
               !(opt.Confidence > 0.0 && opt.Confidence < 1.0)) {
        result.Status = RANSAC_ERR_INVALID_ARG;
        std::snprintf(msgBuf, sizeof(msgBuf), "invalid ransac parameters");
    } else if (ransac_fit((RansacHandle)hIn->ptr, &opt, xData, yData, (int)nX, &result) != 0) {
        HSetErrText(const_cast<char*>("cv_geom_fit: 拟合执行异常"));
        return 30002;
    }

    HPutElem(proc_handle, 1, paramVals.data(), (INT4_8)nParams, DOUBLE_PAR);
    {
        std::vector<INT4_8> mask_i((size_t)nX, 0);
        for (INT4_8 i = 0; i < nX; ++i)
            mask_i[(size_t)i] = inlierMask[(size_t)i] ? 1 : 0;
        HPutElem(proc_handle, 2, mask_i.data(), nX, LONG_PAR);
    }
    HPutElem(proc_handle, 3, &result.ResidualSum, 1, DOUBLE_PAR);
    {
        INT4_8 iters = (INT4_8)result.Iterations, status = (INT4_8)result.Status;
        HPutElem(proc_handle, 4, &iters, 1, LONG_PAR);
        HPutElem(proc_handle, 5, &status, 1, LONG_PAR);
    }
    {
        char* msgOut = msgBuf;
        HPutElem(proc_handle, 6, &msgOut, 1, STRING_PAR);
    }
    HPutElem(proc_handle, 7, &result.InlierRatio, 1, DOUBLE_PAR);

    return H_MSG_TRUE;
}
