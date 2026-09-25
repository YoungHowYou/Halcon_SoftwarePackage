#pragma once
/*=============================================================================
 * ransac_interface.h — RANSAC 通用几何拟合 C ABI（v4.0.0）
 *
 * 支持显式模型 y=f(x) 与隐式模型 F(x,y)=0（圆/椭圆/圆锥曲线），
 * 几何距离残差、句柄式 API（表达式创建时预编译）、函数白名单、随机种子、
 * 权重、动态迭代更新、迭代精化、细化状态码。
 *
 * 兼容：v3.0.0 的 ransac_generic_fit / ransac_generic_fit_ex 保留为兼容层。
 * 异常不穿越 C ABI；句柄内部状态独立，可重入。
 *===========================================================================*/

#include <stddef.h>

#if defined(_WIN32) || defined(_WIN64)
#  ifdef RANSAC_BUILD_DLL
#    define RANSAC_API __declspec(dllexport)
#  else
     /* 静态链接（当前唯一用法）：不加 dllimport，否则链静态库报 __imp_ 未解析 */
#    define RANSAC_API
#  endif
#else
#  define RANSAC_API __attribute__((visibility("default")))
#endif

#define RANSAC_VERSION_MAJOR 4
#define RANSAC_VERSION_MINOR 0
#define RANSAC_VERSION_PATCH 0
#define RANSAC_VERSION_STRING "4.0.0"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 状态码 ---------- */
typedef enum {
    RANSAC_OK                     =   0,  /* 成功 */
    RANSAC_ERR_INVALID_ARG        =  -1,  /* 参数非法 */
    RANSAC_ERR_EXPR_PARSE         =  -2,  /* 表达式解析失败 */
    RANSAC_ERR_EXPR_EVAL          =  -3,  /* 表达式评估失败 */
    RANSAC_ERR_EXPR_TIMEOUT       =  -4,  /* 评估超时/预算耗尽 */
    RANSAC_ERR_NOT_ENOUGH_POINTS  =  -5,  /* 数据点不足 */
    RANSAC_ERR_SAMPLE_DEGENERATE  =  -6,  /* 采样持续退化 */
    RANSAC_ERR_SOLVE_FAILED       =  -7,  /* 求解持续失败 */
    RANSAC_ERR_NUMERIC            =  -8,  /* 数值异常 */
    RANSAC_ERR_NOT_CONVERGED      =  -9,  /* 未收敛（输出最佳候选） */
    RANSAC_ERR_MAX_ITER           = -10,  /* 达到最大迭代（输出最佳候选） */
    RANSAC_ERR_INTERNAL           = -99   /* 内部错误 */
} RansacStatus;

/* v3 兼容状态码别名 */
#define RANSAC_STATUS_OK        RANSAC_OK
#define RANSAC_STATUS_NO_CONV   RANSAC_ERR_NOT_CONVERGED
#define RANSAC_STATUS_ERROR     RANSAC_ERR_INVALID_ARG

/* ---------- 模型类型 ---------- */
typedef enum {
    RANSAC_MODEL_EXPLICIT = 0,   /* y = f(x; params) */
    RANSAC_MODEL_IMPLICIT = 1    /* F(x, y; params) = 0 */
} RansacModelType;

/* ---------- 残差类型 ---------- */
typedef enum {
    RANSAC_RESIDUAL_VERTICAL   = 0,  /* |y - f(x)|，仅显式模型 */
    RANSAC_RESIDUAL_GEOMETRIC  = 1   /* 几何距离 |F|/||grad F||，推荐 */
} RansacResidualType;

/* ---------- 参数求解策略 ---------- */
typedef enum {
    RANSAC_SOLVER_AUTO      = 0,  /* 自动判断线性/非线性 */
    RANSAC_SOLVER_LINEAR    = 1,  /* 强制线性最小二乘 */
    RANSAC_SOLVER_NONLINEAR = 2   /* 强制 LM 非线性 */
} RansacSolverType;

/* ---------- 拟合选项 ---------- */
typedef struct {
    /* 模型定义 */
    const char*        ModelExpression;
    const char*        XName;             /* 自变量/横坐标名 */
    const char*        YName;             /* 纵坐标名（隐式必填，显式可 NULL） */
    const char* const* ParamNames;
    int                ParamCount;
    const double*      InitialValues;     /* 长度 = ParamCount，可为 NULL */
    RansacModelType    ModelType;
    RansacResidualType ResidualType;

    /* RANSAC 参数 */
    int                MinSampleSize;     /* 最小采样点数，必须 >= 1 */
    double             Threshold;         /* 内点几何距离阈值，> 0 */
    int                MaxIterations;     /* > 0 */
    double             OutlierRatio;      /* [0,1) */
    double             Confidence;        /* (0,1)，如 0.99；0 = 取 0.99 */
    unsigned int       Seed;              /* 0 = 非确定性 */

    /* 求解器参数 */
    RansacSolverType   SolverType;
    int                MaxSolverIterations; /* 每次候选求解最大迭代，<=0 默认 50 */
    double             SolverTolerance;     /* <=0 默认 1e-8 */

    /* 精化参数 */
    int                EnableRefinement;    /* 默认 1 */
    int                MaxRefineIterations; /* <=0 默认 5 */

    /* 安全限制 */
    int                MaxExpressionLength; /* <=0 默认 4096 */
    long               MaxExpressionEvals;  /* <=0 不限 */
    int                EvalTimeoutMs;       /* <=0 不限（当前实现按评估预算代替） */

    /* 权重（可选） */
    const double*      Weights;             /* 长度 = PointCount，可为 NULL */
} RansacOptions;

/* ---------- 拟合结果 ---------- */
typedef struct {
    double*        ParamValues;       /* 长度 = ParamCount，调用方分配 */
    size_t         ParamValuesLen;

    unsigned char* InlierMask;        /* 长度 = PointCount，调用方分配 */
    size_t         InlierMaskLen;

    double         ResidualSum;       /* 内点（加权）残差平方和 */
    double         InlierRatio;       /* 内点比例 */
    int            Iterations;
    RansacStatus   Status;
    char*          StatusMessage;     /* 调用方分配 */
    size_t         StatusMessageLen;

    /* 可选输出（当前版本预留，未实现时为 0/NULL） */
    double*        Covariance;
    size_t         CovarianceLen;
} RansacResult;

/* ---------- 句柄 ---------- */
typedef struct RansacHandleOpaque* RansacHandle;

/* 创建句柄：预编译表达式；失败返回 NULL 并写 statusMessage */
RANSAC_API RansacHandle ransac_create(
    const char*        modelExpression,
    const char*        xName,
    const char*        yName,
    const char* const  paramNames[],
    int                paramCount,
    RansacModelType    modelType,
    char*              statusMessage,
    size_t             statusMessageLen);

RANSAC_API void ransac_destroy(RansacHandle handle);

RANSAC_API int ransac_get_param_count(RansacHandle handle);
RANSAC_API int ransac_get_required_min_sample(RansacHandle handle);

/* 执行拟合。返回 0 = 函数成功执行（结果看 result->Status）；-1 = 函数级异常 */
RANSAC_API int ransac_fit(
    RansacHandle       handle,
    const RansacOptions* options,
    const double*      xData,
    const double*      yData,
    int                pointCount,
    RansacResult*      result);

/* ---------- 便捷单次调用 ---------- */
RANSAC_API int ransac_fit_once(
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
    char*              statusMessage,     size_t statusMessageLen);

/* ================= v3.0.0 兼容层（deprecated） ================= */
RANSAC_API int ransac_generic_fit(
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
    char*         StatusMessage);

RANSAC_API int ransac_generic_fit_ex(
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
    char*         StatusMessage);

#ifdef __cplusplus
}
#endif
