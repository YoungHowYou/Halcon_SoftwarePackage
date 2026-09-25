/*=============================================================================
 * ransac_interface.cpp — C ABI 入口：句柄生命周期、参数校验、异常捕获、
 *                        v3.0.0 兼容层
 *===========================================================================*/
#include "cvr/ransac_interface.h"
#include "model_expression.h"
#include "ransac_core.h"

#include <cstring>
#include <string>
#include <vector>

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
