#pragma once
/*=============================================================================
 * model_expression.h — muparser 动态表达式封装（显式 + 隐式模型）
 *
 * 关键约束：mu::Parser 内部保存的是【变量地址】，拷贝时必须把变量重新绑定到
 * 副本自己的成员上（Eigen::NumericalDiff 会拷贝持有本类的 functor）。
 *
 * 安全：函数白名单、表达式长度限制、总评估次数预算。
 *===========================================================================*/

#include <muParser.h>

#include <cmath>
#include <cctype>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace ransac {

/* muparser 默认没有 pow，注册一个（设计文档表达式大量使用 pow(x,2)） */
inline double ransac_pow(double x, double y) { return std::pow(x, y); }

/* 表达式预算耗尽（映射到 ERR_EXPR_TIMEOUT/NOT_CONVERGED） */
struct ExprBudgetExceeded : public std::runtime_error {
    ExprBudgetExceeded() : std::runtime_error("expression eval budget exceeded") {}
};

/* 表达式非法（白名单外函数/未知标识符/超长），映射到 ERR_EXPR_PARSE */
struct ExprInvalid : public std::runtime_error {
    explicit ExprInvalid(const std::string& m) : std::runtime_error(m) {}
};

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
