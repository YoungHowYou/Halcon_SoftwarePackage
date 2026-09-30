/*=============================================================================
 * cvflow/fit.hpp — 表达式驱动的最小二乘拟合（两步式：建模一次，拟合多次）
 *
 * 设计：句柄类（LmModel / LinearModel）在建模时编译并预解析 muparser 表达式，
 * 拟合步直接复用编译结果——表达式整个生命周期只解析一次（旧一步到位算子每次
 * 调用都重新解析）。LM 路径的 Eigen 求解器会拷贝 functor，故 functor 只持有
 * 指向句柄共享状态的指针（多次拷贝共享同一份编译产物）。
 *
 * 依赖：muparser（表达式）+ Eigen（LM）+ OpenCV（QR 矩阵）。
 *
 * 返回值约定：create 失败返回 nullptr 并填 errCode/errMsg；
 *             fit 返回 0 成功，>0 为参数/数据校验错误码（supply 映射 30000+code）。
 *===========================================================================*/
#pragma once

#include <string>
#include <vector>

namespace cvflow {

// ---- 句柄类别（Halcon_Def.h 的 FitModel 句柄按此分发析构） ----
enum FitModelKind {
    FIT_MODEL_LM     = 0,   // LmModel（M 自变量 × K 输出，LM 非线性）
    FIT_MODEL_LINEAR = 1,   // LinearModel（1~2 自变量，对参数线性）
    FIT_MODEL_GEOM   = 2    // 几何 RANSAC（直接持有 RansacHandle，见 ransac.hpp）
};

struct LmFitResult {
    std::vector<double> params;   // 默认回吐初值；成功后为拟合值
    double rss = -1.0;            // 残差平方和（表达式出错时为 -1）
    int    iterations = 0;
    int    status = 0;            // Eigen LM 状态码
    std::string message;
};

// ----------------------------------------------------------------------------
// LmModel：M 个自变量、K 个输出表达式（共享同一组参数）的编译后模型
// ----------------------------------------------------------------------------
class LmModel {
public:
    /* 创建：校验 + 预编译全部表达式（muparser 仅此一次解析）。
     * errCode：1 无表达式 / 2 无自变量 / 3 无参数 / 4 表达式为空
     *          / 5 自变量名为空 / 6 自变量重名 / 7 参数名为空 / 8 参数重名
     *          / 9 参数与自变量冲突 / 10 表达式语法错误 */
    static LmModel* create(const std::vector<std::string>& expressions,
                           const std::vector<std::string>& paramNames,
                           const std::vector<std::string>& xNames,
                           int& errCode, std::string& errMsg);
    static void destroy(LmModel* m) { delete m; }

    int paramCount() const  { return (int)m_paramNames.size(); }
    int varCount() const    { return (int)m_xNames.size(); }
    int outputCount() const { return (int)m_exprs.size(); }
    const std::vector<std::string>& paramNames() const { return m_paramNames; }

    /* 共享状态求值接口（拟合 functor 用；同一句柄同时只能跑一个 fit） */
    void setParams(const double* p) const;      // 写参数槽
    void setX(const double* xv) const;          // 写自变量槽
    double eval(int k) const;                   // 用当前槽值求第 k 个表达式

private:
    LmModel() = default;

    std::vector<std::string> m_exprs;
    std::vector<std::string> m_paramNames;
    std::vector<std::string> m_xNames;

    mutable std::vector<double> m_paramSlots;   // muparser 绑定的变量地址，绝不 resize
    mutable std::vector<double> m_xSlots;
    std::vector<void*>          m_parsers;      // mu::Parser*（K 个），仅在 cpp 中解引用
};

/* LM 拟合。xFlat 行优先（点数×M）、yFlat 行优先（点数×K）。
 * 错误码：1 初值数≠参数数 / 2 自变量数据长度须为 M 的整数倍
 *         / 3 观测数据长度须为 点数×K / 4 数据点少于参数个数（欠定） */
int lm_model_fit(const LmModel& m,
                 const std::vector<double>& xFlat,
                 const std::vector<double>& yFlat,
                 const std::vector<double>& initial,
                 long maxIter, double eps,
                 LmFitResult& out);

struct LinearFitResult {
    std::vector<double> coefficients;
    double rss = -1.0;
    int    rank = 0;
    bool   success = false;
    std::string message;
};

// ----------------------------------------------------------------------------
// LinearModel：1~2 个自变量、对参数线性的编译后模型
// ----------------------------------------------------------------------------
class LinearModel {
public:
    /* 创建：校验 + 预编译表达式。errCode：1 表达式为空 / 2 无参数
     * / 3 自变量个数须为 1~2 / 4 参数名为空 / 5 参数重名
     * / 6 自变量名为空 / 7 自变量重名 / 8 参数与自变量冲突 / 9 表达式语法错误 */
    static LinearModel* create(const std::string& expression,
                               const std::vector<std::string>& paramNames,
                               const std::vector<std::string>& xNames,
                               int& errCode, std::string& errMsg);
    static void destroy(LinearModel* m) { delete m; }

    int paramCount() const { return (int)m_paramNames.size(); }
    int varCount() const   { return (int)m_xNames.size(); }
    const std::vector<std::string>& paramNames() const { return m_paramNames; }

    /* f(x; params)：写槽求值（拟合内部 N×(P+1) 次调用） */
    double eval(const std::vector<double>& x, const std::vector<double>& params) const;

private:
    LinearModel() = default;

    std::string              m_expr;
    std::vector<std::string> m_paramNames;
    std::vector<std::string> m_xNames;

    mutable std::vector<double> m_paramSlots;
    mutable std::vector<double> m_xSlots;
    void*                       m_parser = nullptr;   // mu::Parser*，仅在 cpp 中解引用
};

/* 线性拟合（设计矩阵 + 列主元 QR；含对参数线性的逐点校验）。
 * xData[i] 为第 i 个点的自变量向量（长度 = varCount），yData 为观测。 */
LinearFitResult linear_model_fit(const LinearModel& m,
                                 const std::vector<std::vector<double>>& xData,
                                 const std::vector<double>& yData,
                                 double linearTolerance = 1e-10);

} // namespace cvflow
