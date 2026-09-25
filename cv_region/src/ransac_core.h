#pragma once
/*=============================================================================
 * ransac_core.h — RANSAC 主循环（显式/隐式模型、动态迭代、精化、权重）
 *===========================================================================*/

#include "model_expression.h"
#include "optimizer.h"

#include <string>
#include <vector>

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
