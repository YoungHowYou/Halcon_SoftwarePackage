/*=============================================================================
 * cv_region — cvr.hpp：全部公开声明（单文件约定）
 *=============================================================================
 * 说明：cv_region 现在是「1 个头 + 1 个实现」的静态库（cvr_core），本文件由原
 *       12 个 .hpp / 12 个 .cpp 机械合并而成，段落顺序与下表一致（原文件 → 段落）：
 *   include/cvr/ransac_interface.h     RANSAC 枚举 / 结构 / 句柄式 C 接口（test_m8_ransac 与配套实现）
 *   include/cvr/cvr_types.hpp          基础类型：RLE chord 编码、坐标与游程类型
 *   include/cvr/cvr_region.hpp         region 本体与基本属性
 *   include/cvr/cvr_ops.hpp            集合运算 / ROI / 生成
 *   include/cvr/cvr_conn.hpp           连通域与孔洞
 *   include/cvr/cvr_morph.hpp          形态学
 *   include/cvr/cvr_feat.hpp           形状特征
 *   include/cvr/cvr_shape.hpp          形状变换 / 矩形圆生成
 *   include/cvr/cvr_select.hpp         select_shape
 *   include/cvr/cvr_measure.hpp        1D 边缘测量
 *   src/ransac_core.h                  ransac::CoreOptions / CoreResult / ransac_run（原内部头，扩展包 supply 需要）
 *   include/cvr/cvr_io.hpp             OpenCV 桥（可选；需定义 CVR_WITH_OPENCV）
 *   include/cvr/cvr_c_api.h            遗留 C ABI（当前无人调用，保留兼容）
 *
 * 外部工程集成（把本文件 + cvr.cpp 两个文件拷走即可，无需本仓库其它东西）：
 *   - 语言/标准：C++17；
 *   - 编译期**必须**有 Eigen3（<Eigen/Dense> + <unsupported/Eigen/LevenbergMarquardt>）
 *     与 muparser 头（<muParser.h>），否则 cvr.cpp 直接 C1083 编不过（ransac 段无开关）；
 *   - 链接期需要 muparser 库；运行期需要 muparser.dll（muparser 是 SHARED）；
 *   - **不需要 OpenCV**：<opencv2/core.hpp> 在文件末尾 #ifdef CVR_WITH_OPENCV 段内，
 *     要用 region<->mask 互转才定义该宏并补 OpenCV 头/库；
 *   - 本文件与 cvr.cpp 是 UTF-8 **with BOM**（含中文注释），拷到别处请保持；
 *     调用方自己的含中文源文件也要 BOM 或 /utf-8，否则 MSVC 按 936 解码会吞行（C4819）。
 *
 * 约定（务必遵守）：
 *   1. 新增算子/函数直接加进本文件对应段落，**非必要不要新建文件**；
 *   2. 不在这里写内部 include：内部依赖已折叠（model_expression.h / optimizer.h /
 *      ransac_core.h 的声明在 本文件）；
 *   3. OpenCV 桥（cvr_io）与遗留 C ABI 在文件末尾，分别由 CVR_WITH_OPENCV 宏控制；
 *   4. 该头用 `#pragma once`，禁止再拆分子头。
 *===========================================================================*/

#include <stddef.h>
#include <cstdint>
#include <string>
#include <vector>
#include <stdint.h>
#include <stdexcept>   // ransac::ExprInvalid / ExprBudgetExceeded（std::runtime_error）

/*===========================================================================
 * RANSAC 枚举 / 结构 / 句柄式 C 接口（test_m8_ransac 与配套实现）
 * （原 include/cvr/ransac_interface.h）
 *=========================================================================*/
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

/*===========================================================================
 * 基础类型：RLE chord 编码、坐标与游程类型
 * （原 include/cvr/cvr_types.hpp）
 *=========================================================================*/
namespace cvr {

// ----------------------------------------------------------------------------
// 基本类型（与 HALCON 类型无关，内部统一 32/64 位）
// ----------------------------------------------------------------------------
using CvrCoord  = int32_t;   // 行/列坐标，内部统一 32 位
using CvrChords = int64_t;   // run 计数，对齐 HALCON 的 HITEMCNT

constexpr double CVR_INF_VAL = 1e30;
constexpr double CVR_PI      = 3.14159265358979323846;

// ----------------------------------------------------------------------------
// 错误处理：L1 纯函数返回 bool，错误信息通过 thread_local 字符串传递
// ----------------------------------------------------------------------------
const char* cvr_last_error() noexcept;
void        cvr_set_last_error(const std::string& msg) noexcept;
void        cvr_clear_last_error() noexcept;

// ----------------------------------------------------------------------------
// 特征缓存标志（与 HALCON HFeatureFlags 语义对应，但位布局独立）
// ----------------------------------------------------------------------------
struct CvrFeatureFlags {
    uint32_t shape              : 1;
    uint32_t is_convex          : 1;
    uint32_t is_filled          : 1;
    uint32_t is_connected4      : 1;
    uint32_t is_connected8      : 1;
    uint32_t is_thin            : 1;
    uint32_t circularity        : 1;
    uint32_t compactness        : 1;
    uint32_t contlength         : 1;
    uint32_t convexity          : 1;
    uint32_t phi                : 1;
    uint32_t elliptic_axis      : 1;  // ra, rb
    uint32_t elliptic_shape     : 1;  // ra_, rb_
    uint32_t excentricity       : 1;  // anisometry, bulkiness, structure_factor
    uint32_t moments            : 1;  // m11, m20, m02, ia, ib
    uint32_t center_area        : 1;  // row, col, area
    uint32_t smallest_rectangle1: 1;  // row1, col1, row2, col2
    uint32_t smallest_rectangle2: 1;  // row_rect, col_rect, phi_rect, length1, length2
    uint32_t smallest_circle    : 1;  // row_circle, col_circle, radius
    uint32_t min_max_chord      : 1;
    uint32_t min_max_chord_gap  : 1;
    uint32_t rectangularity     : 1;
    uint32_t reserved           : 10;

    CvrFeatureFlags() noexcept { *reinterpret_cast<uint32_t*>(this) = 0; }

    void reset() noexcept { *reinterpret_cast<uint32_t*>(this) = 0; }
    bool any() const noexcept { return *reinterpret_cast<const uint32_t*>(this) != 0; }
};

static_assert(sizeof(CvrFeatureFlags) == sizeof(uint32_t),
              "CvrFeatureFlags must pack into 32 bits");

} // namespace cvr

/*===========================================================================
 * region 本体与基本属性
 * （原 include/cvr/cvr_region.hpp）
 *=========================================================================*/
namespace cvr {

// ----------------------------------------------------------------------------
// 单条 chord/run：与 HALCON Hrun 语义一致
//   r  = 行号
//   cb = 起始列（包含）
//   ce = 结束列（包含）
// 不变量：cb <= ce；同一行多个 run 不重叠、不相接（相接必须合并）
// ----------------------------------------------------------------------------
struct CvrRun {
    CvrCoord r;
    CvrCoord cb;
    CvrCoord ce;
};

// ----------------------------------------------------------------------------
// 特征缓存（惰性计算）
// flags == 0 表示没有任何特征被计算过
// ----------------------------------------------------------------------------
struct CvrFeature {
    CvrFeatureFlags flags;

    // shape
    uint8_t  shape = 0;
    bool     is_convex = false;
    bool     is_filled = false;
    bool     is_connected4 = false;
    bool     is_connected8 = false;
    bool     is_thin = false;

    // scalar features
    double circularity  = 0.0;
    double compactness  = 0.0;
    double contlength   = 0.0;
    double convexity    = 0.0;
    double phi          = 0.0;
    double ra = 0.0, rb = 0.0;
    double ra_ = 0.0, rb_ = 0.0;
    double anisometry = 0.0, bulkiness = 0.0, structure_factor = 0.0;

    // moments
    double m11 = 0.0, m20 = 0.0, m02 = 0.0, ia = 0.0, ib = 0.0;

    // center / area
    double   row = 0.0, col = 0.0;
    CvrChords area = 0;

    // smallest rectangle1 (axis-aligned bbox)
    CvrCoord row1 = 0, col1 = 0, row2 = -1, col2 = -1;

    // smallest rectangle2 (oriented bbox)
    double row_rect = 0.0, col_rect = 0.0, phi_rect = 0.0;
    double length1 = 0.0, length2 = 0.0;

    // smallest outer circle
    double row_circle = 0.0, col_circle = 0.0, radius = 0.0;

    // chord statistics
    CvrCoord min_chord = 0, max_chord = 0;
    CvrCoord min_chord_gap = 0, max_chord_gap = 0;

    double rectangularity = 0.0;
};

// ----------------------------------------------------------------------------
// Region 主结构
// ----------------------------------------------------------------------------
struct CvrRegion {
    std::vector<CvrRun> runs;
    bool                is_compl = false;
    CvrFeature          feature;
};

// ----------------------------------------------------------------------------
// 比较与排序
// ----------------------------------------------------------------------------
inline bool cvr_run_less(const CvrRun& a, const CvrRun& b) noexcept {
    if (a.r != b.r) return a.r < b.r;
    if (a.cb != b.cb) return a.cb < b.cb;
    return a.ce < b.ce;
}

// ----------------------------------------------------------------------------
// 不变量维护
// ----------------------------------------------------------------------------

// 重置特征缓存（修改 runs 后必须调用）
void cvr_region_invalidate(CvrRegion& r) noexcept;

// 排序 + 合并相邻/重叠 run；完成后 region 满足 chord 三条件
bool cvr_region_normalize(CvrRegion& r);

// 判断 region 是否为空（考虑 is_compl）
bool cvr_region_is_empty(const CvrRegion& r, CvrCoord w, CvrCoord h) noexcept;

// 物化补集：将 is_compl 展开为普通 runs（结果 is_compl=false）
// 需要定义域 w,h；失败返回 false
bool cvr_region_materialize(const CvrRegion& r, CvrCoord w, CvrCoord h,
                            CvrRegion& out);

// 计算并返回轴对齐包围盒；结果写入 out 参数
bool cvr_region_bbox(const CvrRegion& r, CvrCoord& row1, CvrCoord& col1,
                     CvrCoord& row2, CvrCoord& col2);

// 行视图：访问某一行在 [0, w-1] 区间内的前景段
// 返回该行所有 run 的 [begin, end) 索引对
bool cvr_region_row_ranges(const CvrRegion& r, CvrCoord row,
                           size_t& begin, size_t& end);

// 调试辅助：检查 region 是否满足所有不变量
bool cvr_region_check_invariants(const CvrRegion& r) noexcept;

} // namespace cvr

/*===========================================================================
 * 集合运算 / ROI / 生成
 * （原 include/cvr/cvr_ops.hpp）
 *=========================================================================*/
namespace cvr {

// ====== 集合代数 ======
// 所有涉及补集的算子都需要定义域 (w,h) 以在必要时物化补集。
// 输出 is_compl 恒为 false（complement 除外，它只翻转标记）。

// Halcon: union1(Regions : RegionUnion : : )
bool cvr_union1(const std::vector<CvrRegion>& regions,
                CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: union2(Region1, Region2 : RegionUnion : : )
bool cvr_union2(const CvrRegion& r1, const CvrRegion& r2,
                CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: intersection(Region1, Region2 : RegionIntersection : : )
bool cvr_intersection(const CvrRegion& r1, const CvrRegion& r2,
                      CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: difference(Region1, Region2 : RegionDifference : : )
// out = r1 - r2
bool cvr_difference(const CvrRegion& r1, const CvrRegion& r2,
                    CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: complement(Region : RegionComplement : : )
bool cvr_complement(const CvrRegion& r, CvrRegion& out);

// Halcon: symm_difference(Region1, Region2 : RegionDifference : : )
bool cvr_symm_difference(const CvrRegion& r1, const CvrRegion& r2,
                         CvrCoord w, CvrCoord h, CvrRegion& out);

} // namespace cvr

/*===========================================================================
 * 连通域与孔洞
 * （原 include/cvr/cvr_conn.hpp）
 *=========================================================================*/
namespace cvr {

// Halcon: connection(Region : ConnectedRegions : : )
// connectivity: 4 或 8（默认 8，和 Halcon 一致）
bool cvr_connection(const CvrRegion& r, int connectivity,
                    CvrCoord w, CvrCoord h,
                    std::vector<CvrRegion>& out);

} // namespace cvr

/*===========================================================================
 * 形态学
 * （原 include/cvr/cvr_morph.hpp）
 *=========================================================================*/
namespace cvr {

// ====== 结构元（Halcon 里用 gen_circle / gen_rectangle1 生成） ======
// 约定：结构元的 runs 坐标是相对参考点 (0,0) 的偏移，可以是负数

// Halcon: gen_circle(StructElement, Row, Column, Radius)
CvrRegion cvr_gen_circle(CvrCoord row, CvrCoord col, double radius);

// Halcon: gen_rectangle1(StructElement, Row1, Column1, Row2, Column2)
CvrRegion cvr_gen_rectangle1(CvrCoord r1, CvrCoord c1, CvrCoord r2, CvrCoord c2);

// 便捷：以 (0,0) 为中心的圆/矩形/十字
CvrRegion cvr_se_disk(int radius);
CvrRegion cvr_se_rect(int w, int h);
CvrRegion cvr_se_cross(int radius);

// 结构元转置（开运算的膨胀要用转置）
bool cvr_se_transpose(const CvrRegion& se, CvrRegion& out);

// ====== 形态学 ======
// Halcon: dilation1(Region : RegionDilation : StructElement, Iterations : )
bool cvr_dilation1(const CvrRegion& r, const CvrRegion& se, int iterations,
                   CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: erosion1(Region : RegionErosion : StructElement, Iterations : )
bool cvr_erosion1(const CvrRegion& r, const CvrRegion& se, int iterations,
                  CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: opening(Region : RegionOpening : StructElement : )
bool cvr_opening(const CvrRegion& r, const CvrRegion& se,
                 CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: closing(Region : RegionClosing : StructElement : )
bool cvr_closing(const CvrRegion& r, const CvrRegion& se,
                 CvrCoord w, CvrCoord h, CvrRegion& out);

// Halcon: fill_up(Region : RegionFill : : )
bool cvr_fill_up(const CvrRegion& r, CvrCoord w, CvrCoord h, CvrRegion& out);

} // namespace cvr

/*===========================================================================
 * 形状特征
 * （原 include/cvr/cvr_feat.hpp）
 *=========================================================================*/
namespace cvr {

// ----------------------------------------------------------------------------
// 形状特征计算（惰性缓存由调用方通过 CvrRegion.feature 维护）
// 所有函数：输入 region 必须已 normalize；输出 is_compl=false
// ----------------------------------------------------------------------------

// 面积与重心
bool cvr_feature_area_center(const CvrRegion& r,
                             double& row, double& col, CvrChords& area);

// 二阶中心矩
bool cvr_feature_moments(const CvrRegion& r,
                         double& m11, double& m20, double& m02);

// 椭圆等效轴：ra（长半轴）、rb（短半轴）、phi（主轴与列轴夹角，弧度）
bool cvr_feature_elliptic_axis(const CvrRegion& r,
                               double& ra, double& rb, double& phi);

// 轮廓长度（含 sqrt(2) 对角修正）
bool cvr_feature_contlength(const CvrRegion& r, double& contlength);

// 凸包 + 凸度
bool cvr_feature_convexity(const CvrRegion& r, double& convexity, bool& is_convex);

// 轴对齐包围盒（rectangle1）
bool cvr_feature_smallest_rectangle1(const CvrRegion& r,
                                     CvrCoord& row1, CvrCoord& col1,
                                     CvrCoord& row2, CvrCoord& col2);

// 最小外接旋转矩形（rectangle2）
bool cvr_feature_smallest_rectangle2(const CvrRegion& r,
                                     double& row, double& col,
                                     double& phi, double& length1, double& length2);

// 最小外接圆
bool cvr_feature_smallest_circle(const CvrRegion& r,
                                 double& row, double& col, double& radius);

// 紧凑度 / 圆度（Halcon compactness = contlength^2 / (4*pi*area)）
bool cvr_feature_compactness(const CvrRegion& r, double& compactness);
bool cvr_feature_circularity(const CvrRegion& r, double& circularity);

// 矩形度
bool cvr_feature_rectangularity(const CvrRegion& r, double& rectangularity);

// 按名称查特征（Halcon 风格小写）
// 支持的名称：
//   "area", "row", "column", "width", "height",
//   "row1","column1","row2","column2",
//   "row_rect","column_rect","phi_rect","length1","length2",
//   "row_circle","column_circle","radius",
//   "contlength", "convexity", "compactness", "circularity", "rectangularity",
//   "anisometry", "bulkiness", "structure_factor",
//   "phi", "ra", "rb"
bool cvr_get_feature(const CvrRegion& r, const std::string& name, double& value);

// 批量按名称取特征，结果按行输出 [N_regions x N_features]
bool cvr_region_features(const std::vector<CvrRegion>& regions,
                         const std::vector<std::string>& names,
                         std::vector<double>& values);

// ----------------------------------------------------------------------------
// Halcon: gray_features(Regions, Image : : Features : Value) —— 灰度特征
// 像素类型（depth 决定 gray 指向的实际类型）：
//   CVR_GRAY_U8  -> uint8_t*   (byte / direction / cyclic)
//   CVR_GRAY_S8  -> int8_t*    (int1，signed)
//   CVR_GRAY_U16 -> uint16_t*  (uint2)
//   CVR_GRAY_S16 -> int16_t*   (int2)
//   CVR_GRAY_S32 -> int32_t*   (int4)
//   CVR_GRAY_S64 -> int64_t*   (int8)
//   CVR_GRAY_F32 -> float*     (real)
//   CVR_GRAY_F64 -> double*    (real, double 缓冲)
enum CvrGrayDepth {
    CVR_GRAY_U8  = 0, CVR_GRAY_S8  = 1, CVR_GRAY_U16 = 2, CVR_GRAY_S16 = 3,
    CVR_GRAY_S32 = 4, CVR_GRAY_S64 = 5, CVR_GRAY_F32 = 6, CVR_GRAY_F64 = 7
};

// 单区域灰度特征。name ∈ {"area","row","column","mean","deviation","min","max","median"}
//   area      = Σ g                    （灰度体积，同 HALCON area_center_gray）
//   row/column= Σ r·g / Σ g, Σ c·g / Σ g（灰度重心）
//   mean      = Σ g / F；deviation = sqrt(Σ (g-mean)^2 / F)   （F = 区域像素数）
//   min/max   = 区域内灰度极值；median ↔ min_max_gray(Percent=50)
// 区域按图像尺寸裁剪；runs 需按 (r, cb) 有序（构造时保证）。
bool cvr_gray_feature(const CvrRegion& r, const void* gray, int width, int height,
                      CvrGrayDepth depth, const std::string& name, double& value);

// 批量：结果按行输出 [N_regions x N_features]，顺序同 cvr_region_features
bool cvr_gray_features(const std::vector<CvrRegion>& regions, const void* gray,
                       int width, int height, CvrGrayDepth depth,
                       const std::vector<std::string>& names,
                       std::vector<double>& values);

// ----------------------------------------------------------------------------
// 图像 <-> region 互转（对应算子 cv_bin_to_region / cv_region_to_bin）
//   * 纯 C++，不依赖 OpenCV：调用方自备灰度缓冲 + 尺寸 + CvrGrayDepth
//   * 与 OpenCV 门控的 cvr_region_to_mask / cvr_region_from_mask 的区别：
//     那两个只处理 0/1 mask；这里是「任意数值类型 + 阈值」/「写前景背景值」。
//   图像 -> region：gray >= threshold 的像素为前景（等价 cv_bin_to_region）；
//                   结果 runs 保证按 (r, cb) 有序，按 [0,w)x[0,h) 裁剪。
//    region -> 图像：区域内写 foreground、区域外写 background（等价 cv_region_to_bin）；
//                   is_compl 先物化，区域按 w x h 裁剪。
bool cvr_bin_to_region(const void* gray, int width, int height, CvrGrayDepth depth,
                       double threshold, CvrRegion& out);

bool cvr_region_to_bin(const CvrRegion& r, CvrCoord w, CvrCoord h,
                       void* gray, CvrGrayDepth depth,
                       double foreground, double background);

} // namespace cvr

/*===========================================================================
 * 形状变换 / 矩形圆生成
 * （原 include/cvr/cvr_shape.hpp）
 *=========================================================================*/
namespace cvr {

// Halcon shape_trans 支持的形状
constexpr const char* CVR_SHAPE_RECTANGLE1 = "rectangle1";
constexpr const char* CVR_SHAPE_RECTANGLE2 = "rectangle2";
constexpr const char* CVR_SHAPE_ELLIPSE    = "ellipse";
constexpr const char* CVR_SHAPE_OUTER_CIRCLE = "outer_circle";
constexpr const char* CVR_SHAPE_CONVEX     = "convex";

// Halcon: shape_trans(Region : RegionTrans : Shape : )
// Shape: "rectangle1" / "rectangle2" / "ellipse" / "outer_circle" / "convex"
bool cvr_shape_trans(const CvrRegion& r, const std::string& shape, CvrRegion& out);

// Halcon: gen_rectangle2(Rectangle2 : : Row, Column, Phi, Length1, Length2 : )
// 生成旋转矩形 Region：中心 (row, col)，主轴与列轴夹角 phi（弧度），
// length1/length2 为两个方向的半边长（length1 沿主轴）。
// 栅格化口径 = 像素中心包含判定，与 HALCON gen_rectangle2 一致
// （注意与 shape_trans "rectangle2" 的口径不同：后者按 HALCON shape_trans
//  的多边形 rasterizer，同一参数下两者面积可差 ~5%，例如 140x30、phi=0.6：
//  gen_rectangle2 = 4359，shape_trans = 4599）。
CvrRegion cvr_gen_rectangle2(double row, double col, double phi,
                             double length1, double length2);

} // namespace cvr

/*===========================================================================
 * select_shape
 * （原 include/cvr/cvr_select.hpp）
 *=========================================================================*/
namespace cvr {

// Halcon: select_shape(Regions : SelectedRegions : Features, Operation, Min, Max : )
// op: "and" / "or"
bool cvr_select_shape(const std::vector<CvrRegion>& regions,
                      const std::vector<std::string>& features,
                      const std::string& op,
                      const std::vector<double>& mins,
                      const std::vector<double>& maxs,
                      std::vector<CvrRegion>& selected);

// 便捷接口：按单个特征筛选
bool cvr_select_shape_single(const std::vector<CvrRegion>& regions,
                             const std::string& feature,
                             double min_val, double max_val,
                             std::vector<CvrRegion>& selected);

} // namespace cvr

/*===========================================================================
 * 1D 边缘测量
 * （原 include/cvr/cvr_measure.hpp）
 *=========================================================================*/
/*=============================================================================
 * cvr_measure.hpp — 一维边缘测量（HALCON measure_pos 语义子集）
 *
 * 算法流程（与 HALCON 一致的核心步骤）：
 *   1) 沿测量矩形主轴抽取一维灰度剖面（双线性采样 + 垂直方向平均）
 *   2) 构造高斯一阶导数核 G'(x; sigma)
 *    3) 剖面与核卷积（mode='same'）
 *   4) |d| 局部极大值 + 振幅阈值过滤
 *   5) 抛物线亚像素插值
 *   6) 按位置排序、去重（间距 <= 0.5 像素合并）
 *   7) 方向过滤（transition: 1 仅正边缘 / -1 仅负边缘 / 0 全部），映射回 2D 坐标
 *
 * 输入为 8 位单通道灰度图（raw 字节缓冲，行优先），不依赖 OpenCV。
 *===========================================================================*/


namespace cvr {

/* 测量结果：边缘点的行/列（亚像素）与振幅（一阶梯度幅值） */
struct CvrMeasureResult {
    std::vector<double> row;
    std::vector<double> col;
    std::vector<double> amplitude;
};

/* 参数语义与 HALCON measure_pos 对应：
 *   (column, row) 矩形中心；phi 主轴方向（弧度）；
 *   length1/length2 矩形半长/半宽（像素）；sigma 高斯平滑；
 *   threshold 振幅阈值；transition 极性（1 正 / -1 负 / 0 全部）
 * 返回 false 表示参数非法（sigma<=0、length1<1 等），结果为空向量。 */
bool cvr_measure_pos(const std::uint8_t* gray, int width, int height,
                     double column, double row, double phi,
                     double length1, double length2, double sigma,
                     double threshold, int transition,
                     CvrMeasureResult& out);

} // namespace cvr

/*===========================================================================
 * ransac 表达式异常（原 src/model_expression.h 的公开部分）
 *   扩展包 supply 按异常类型映射错误码（ExprInvalid → ERR_EXPR_PARSE、
 *   ExprBudgetExceeded → ERR_EXPR_TIMEOUT/NOT_CONVERGED），故必须放在公开头；
 *   表达式求值本体（class ModelExpression，依赖 muparser）在 cvr.cpp 内部段落。
 *=========================================================================*/
namespace ransac {

/* 表达式预算耗尽（映射到 ERR_EXPR_TIMEOUT/NOT_CONVERGED） */
struct ExprBudgetExceeded : public std::runtime_error {
    ExprBudgetExceeded() : std::runtime_error("expression eval budget exceeded") {}
};

/* 表达式非法（白名单外函数/未知标识符/超长），映射到 ERR_EXPR_PARSE */
struct ExprInvalid : public std::runtime_error {
    explicit ExprInvalid(const std::string& m) : std::runtime_error(m) {}
};

} // namespace ransac

/*===========================================================================
 * ransac::CoreOptions / CoreResult / ransac_run（原内部头，扩展包 supply 需要）
 * （原 src/ransac_core.h）
 *=========================================================================*/
/*=============================================================================
 * ransac_core.h — RANSAC 主循环（显式/隐式模型、动态迭代、精化、权重）
 *===========================================================================*/



namespace ransac {

struct CoreOptions {
    // 模型
    std::string              modelExpression;
    std::string              xName;
    std::string              yName;
    std::vector<std::string> paramNames;
    std::vector<double>      initialValues;   // 可为空 -> 全 0
    bool                     implicit;
    bool                     useVertical;     // 仅显式模型有效

    // RANSAC
    int                      minSampleSize;
    double                   threshold;
    int                      maxIterations;
    double                   outlierRatio;
    double                   confidence;      // (0,1)
    unsigned int             seed;            // 0 = 非确定

    // 求解器
    RansacSolverType         solverType;
    int                      maxSolverIterations;
    double                   solverTolerance;

    // 精化
    bool                     enableRefinement;
    int                      maxRefineIterations;

    // 安全
    int                      maxExpressionLength;
    long                     maxExpressionEvals;

    // 权重（可为空）
    std::vector<double>      weights;
};

struct CoreResult {
    std::vector<double>        paramValues;
    std::vector<unsigned char> inlierMask;
    double                     residualSum;
    double                     inlierRatio;
    int                        iterations;
    RansacStatus               status;
    std::string                statusMessage;
};

CoreResult ransac_run(const CoreOptions& opt,
                      const std::vector<double>& xData,
                      const std::vector<double>& yData);

} // namespace ransac

/*===========================================================================
 * OpenCV 桥（可选；需定义 CVR_WITH_OPENCV）
 * （原 include/cvr/cvr_io.hpp）
 *=========================================================================*/
#ifdef CVR_WITH_OPENCV
#include <opencv2/core.hpp>

namespace cvr {

// 将 CvrRegion 渲染为 CV_8U 二值 mask（前景=255，背景=0）
// 若 r.is_compl=true，则按定义域 [w,h] 物化后渲染
bool cvr_region_to_mask(const CvrRegion& r, CvrCoord w, CvrCoord h,
                        cv::Mat& mask);

// 从 CV_8U mask 提取 region；mask 中非零视为前景
// 可选返回 mask 的宽高
bool cvr_region_from_mask(const cv::Mat& mask, CvrRegion& r,
                          CvrCoord* out_w = nullptr,
                          CvrCoord* out_h = nullptr);

} // namespace cvr

#endif // CVR_WITH_OPENCV

/*===========================================================================
 * 遗留 C ABI（当前无人调用，保留兼容）
 * （原 include/cvr/cvr_c_api.h）
 *=========================================================================*/
/*=============================================================================
 * cvr_c_api.h — cv_region DLL 的 C 外接口（ABI 稳定，可被 HALCON 扩展包调用）
 *
 * 设计要点:
 *   - 不透明句柄 cvr_region_h，隐藏 C++ std::vector 内部实现
 *   - 所有函数返回 int32_t: 非 0 成功，0 失败（失败原因用 cvr_c_last_error()）
 *   - 内存所有权: create 的句柄用 cvr_region_destroy 释放；
 *     connection 的句柄数组用 cvr_region_array_destroy 释放；
 *     select_shape 的索引数组用 cvr_free_int64 释放
 *===========================================================================*/


#if defined(_WIN32) || defined(_WIN64)
#  ifdef CVR_BUILD_DLL
#    define CVR_API __declspec(dllexport)
#  else
     /* 静态链接（当前唯一用法）：不加 dllimport，否则链静态库报 __imp_ 未解析 */
#    define CVR_API
#  endif
#else
#  define CVR_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 【返回值约定】本段 C ABI：1 = 成功，0 = 失败（出错信息用 cvr_last_error 取）。
   注意与文件上方 ransac_* C 接口不同（那是 0 = 成功 / 负错误码）。 */

/* 单条 chord/run：闭区间 [cb, ce]，行号 r。与 HALCON Hrun 字段语义一致 */
typedef struct CvrRunC {
    int32_t r;
    int32_t cb;
    int32_t ce;
} CvrRunC;

typedef void* cvr_region_h;

/* ---- 句柄生命周期 ---- */
CVR_API cvr_region_h   cvr_region_create(const CvrRunC* runs, int64_t num_runs,
                                         int32_t is_compl);
CVR_API void           cvr_region_destroy(cvr_region_h h);
CVR_API int64_t        cvr_region_num_runs(cvr_region_h h);
CVR_API const CvrRunC* cvr_region_runs(cvr_region_h h);   /* 指针随 destroy 失效 */
CVR_API int32_t        cvr_region_is_compl(cvr_region_h h);

/* ---- 集合代数（w,h 为定义域；非补集输入可传 0） ---- */
CVR_API int32_t cvr_union2(cvr_region_h a, cvr_region_h b,
                           int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_intersection(cvr_region_h a, cvr_region_h b,
                                 int32_t w, int32_t h, cvr_region_h* out);

/* ---- 形态学 ---- */
CVR_API int32_t cvr_erosion1(cvr_region_h r, cvr_region_h se,
                             int32_t iterations, int32_t w, int32_t h,
                             cvr_region_h* out);

/* ---- 连通域拆分：输出句柄数组（DLL 分配） ---- */
CVR_API int32_t cvr_connection(cvr_region_h r, int32_t connectivity,
                               int32_t w, int32_t h,
                               cvr_region_h** out_arr, int64_t* out_count);
CVR_API void    cvr_region_array_destroy(cvr_region_h* arr, int64_t count);

/* ---- 形状筛选：返回被选中输入区域的 0 基索引数组（DLL 分配） ---- */
CVR_API int32_t cvr_select_shape(cvr_region_h const* regions, int64_t n,
                                 const char* const* features, int64_t n_features,
                                 const char* operation,
                                 const double* mins, const double* maxs,
                                 int64_t** out_indices, int64_t* out_count);
CVR_API void    cvr_free_int64(int64_t* p);

/* ---- 集合代数（补充） ---- */
CVR_API int32_t cvr_union1(cvr_region_h const* regions, int64_t n,
                           int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_difference(cvr_region_h a, cvr_region_h b,
                               int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_complement(cvr_region_h r, cvr_region_h* out);
CVR_API int32_t cvr_symm_difference(cvr_region_h a, cvr_region_h b,
                                    int32_t w, int32_t h, cvr_region_h* out);

/* ---- 形态学（补充） ---- */
CVR_API int32_t cvr_dilation1(cvr_region_h r, cvr_region_h se,
                              int32_t iterations, int32_t w, int32_t h,
                              cvr_region_h* out);
CVR_API int32_t cvr_opening(cvr_region_h r, cvr_region_h se,
                            int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_closing(cvr_region_h r, cvr_region_h se,
                            int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_fill_up(cvr_region_h r, int32_t w, int32_t h,
                            cvr_region_h* out);

/* ---- 预设结构元形态学（HALCON erosion_circle/rectangle1 同形，无 Iterations） ---- */
CVR_API int32_t cvr_erosion_circle(cvr_region_h r, double radius,
                                   int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_dilation_circle(cvr_region_h r, double radius,
                                    int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_erosion_rectangle1(cvr_region_h r, int32_t rw, int32_t rh,
                                       int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_dilation_rectangle1(cvr_region_h r, int32_t rw, int32_t rh,
                                        int32_t w, int32_t h,
                                        cvr_region_h* out);

/* ---- 区域生成 ---- */
CVR_API int32_t cvr_gen_circle(double row, double col, double radius,
                               cvr_region_h* out);
CVR_API int32_t cvr_gen_rectangle1(int32_t r1, int32_t c1, int32_t r2, int32_t c2,
                                   cvr_region_h* out);

/* ---- 特征（单区域；tuple 由调用方循环） ---- */
CVR_API int32_t cvr_area_center(cvr_region_h r, double* row, double* col,
                                double* area);
CVR_API int32_t cvr_smallest_rectangle1(cvr_region_h r, double* r1, double* c1,
                                        double* r2, double* c2);
CVR_API int32_t cvr_smallest_rectangle2(cvr_region_h r, double* row, double* col,
                                        double* phi, double* length1,
                                        double* length2);
CVR_API int32_t cvr_smallest_circle(cvr_region_h r, double* row, double* col,
                                    double* radius);
CVR_API int32_t cvr_elliptic_axis(cvr_region_h r, double* ra, double* rb,
                                  double* phi);
CVR_API int32_t cvr_get_feature(cvr_region_h r, const char* name, double* value);

/* ---- 形状变换（type: rectangle1/rectangle2/ellipse/outer_circle/convex） ---- */
CVR_API int32_t cvr_shape_trans(cvr_region_h r, const char* type,
                                cvr_region_h* out);

/* ---- region <-> 二值图（byte；fg/bg 可自定义，from_mask 按阈值 >=） ---- */
CVR_API int32_t cvr_region_to_mask(cvr_region_h r, int32_t w, int32_t h,
                                   int32_t fg, int32_t bg,
                                   uint8_t* buf, int64_t buf_size);
CVR_API int32_t cvr_region_from_mask(const uint8_t* buf, int32_t w, int32_t h,
                                     int32_t threshold, cvr_region_h* out);

/* ---- 错误 ---- */
CVR_API const char* cvr_c_last_error(void);

#ifdef __cplusplus
}
#endif
