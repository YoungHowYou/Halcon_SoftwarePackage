#include "cvr/ransac_interface.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("CHECK failed line %d: %s\n", __LINE__, #cond); \
            return 1;                                                   \
        }                                                               \
    } while (0)

static bool near(double a, double b, double tol) {
    return std::fabs(a - b) <= tol;
}

/* 1) 显式二次多项式（v4 句柄 API）：y = x^2 + x + 1 */
static int test_quadratic_v4() {
    const double x[] = {0, 1, 2, 3, 4};
    const double y[] = {1, 3, 7, 13, 21};
    const int n = 5;
    const char* params[] = {"a", "b", "c"};
    double init[] = {0.5, 0.5, 0.5};

    char cmsg[256];
    RansacHandle h = ransac_create("a*pow(x,2)+b*x+c", "x", nullptr, params, 3,
                                   RANSAC_MODEL_EXPLICIT, cmsg, sizeof(cmsg));
    CHECK(h != nullptr);
    CHECK(ransac_get_param_count(h) == 3);
    CHECK(ransac_get_required_min_sample(h) == 3);

    RansacOptions opt = {};
    opt.ModelExpression = "a*pow(x,2)+b*x+c";
    opt.XName = "x";
    opt.ParamNames = params;
    opt.ParamCount = 3;
    opt.InitialValues = init;
    opt.ModelType = RANSAC_MODEL_EXPLICIT;
    opt.ResidualType = RANSAC_RESIDUAL_GEOMETRIC;
    opt.MinSampleSize = 3;
    opt.Threshold = 1e-3;
    opt.MaxIterations = 1000;
    opt.OutlierRatio = 0.2;
    opt.Confidence = 0.99;
    opt.Seed = 12345;
    opt.SolverType = RANSAC_SOLVER_AUTO;
    opt.EnableRefinement = 1;

    double fitted[3];
    unsigned char mask[5];
    char msg[256];
    RansacResult res = {};
    res.ParamValues = fitted;      res.ParamValuesLen = 3;
    res.InlierMask = mask;         res.InlierMaskLen = 5;
    res.StatusMessage = msg;       res.StatusMessageLen = sizeof(msg);

    int ret = ransac_fit(h, &opt, x, y, n, &res);
    CHECK(ret == 0);
    CHECK(res.Status == RANSAC_OK);
    CHECK(near(fitted[0], 1.0, 1e-4));
    CHECK(near(fitted[1], 1.0, 1e-4));
    CHECK(near(fitted[2], 1.0, 1e-4));
    CHECK(res.InlierRatio > 0.99);
    CHECK(res.ResidualSum < 1e-6);
    std::printf("[OK] v4 quadratic a=%.6f b=%.6f c=%.6f ratio=%.3f\n",
                fitted[0], fitted[1], fitted[2], res.InlierRatio);
    ransac_destroy(h);
    return 0;
}

/* 2) 隐式圆拟合：(x-2)^2+(y-3)^2 = 25，含离群点 */
static int test_circle_implicit() {
    const int nIn = 24, nOut = 6, n = nIn + nOut;
    double x[n], y[n];
    for (int i = 0; i < nIn; ++i) {
        const double t = 6.28318530718 * i / nIn;
        x[i] = 2.0 + 5.0 * std::cos(t);
        y[i] = 3.0 + 5.0 * std::sin(t);
    }
    for (int i = 0; i < nOut; ++i) {
        x[nIn + i] = 2.0 + 12.0 * std::cos((double)i);
        y[nIn + i] = 3.0 + 12.0 * std::sin((double)i);
    }

    const char* params[] = {"cx", "cy", "r"};
    double init[] = {0.0, 0.0, 1.0};
    double fitted[3];
    unsigned char mask[30];
    double rss;
    int iters;
    RansacStatus st;
    char msg[256];

    int ret = ransac_fit_once(
        "pow(x-cx,2)+pow(y-cy,2)-r*r", "x", "y", params, 3,
        RANSAC_MODEL_IMPLICIT, RANSAC_RESIDUAL_GEOMETRIC,
        3, init, x, y, n, 0.5, 3000, 0.35, 42,
        fitted, 3, mask, 30, &rss, &iters, &st, msg, sizeof(msg));
    CHECK(ret == 0);
    CHECK(st == RANSAC_OK);
    CHECK(near(fitted[0], 2.0, 0.05));
    CHECK(near(fitted[1], 3.0, 0.05));
    CHECK(near(fitted[2], 5.0, 0.05));
    int outMasked = 0;
    for (int i = 0; i < nOut; ++i) if (!mask[nIn + i]) ++outMasked;
    CHECK(outMasked == nOut);
    std::printf("[OK] implicit circle cx=%.4f cy=%.4f r=%.4f outliers=%d/%d\n",
                fitted[0], fitted[1], fitted[2], outMasked, nOut);
    return 0;
}

/* 3) 隐式直线拟合（圆锥特例）：线性求解器应自动识别 */
static int test_line_implicit_linear() {
    const int n = 12;
    double x[n], y[n];
    // 2x - y + 1 = 0 -> y = 2x + 1
    for (int i = 0; i < n; ++i) { x[i] = (double)i; y[i] = 2.0 * i + 1.0; }

    const char* params[] = {"A", "B", "C"};
    double init[] = {0.0, 0.0, 0.0};
    double fitted[3];
    unsigned char mask[12];
    double rss;
    int iters;
    RansacStatus st;
    char msg[256];

    int ret = ransac_fit_once(
        "A*x+B*y+C", "x", "y", params, 3,
        RANSAC_MODEL_IMPLICIT, RANSAC_RESIDUAL_GEOMETRIC,
        2, init, x, y, n, 1e-3, 500, 0.0, 7,
        fitted, 3, mask, 12, &rss, &iters, &st, msg, sizeof(msg));
    CHECK(ret == 0);
    CHECK(st == RANSAC_OK);
    // 参数只差一个整体缩放：A/B ≈ -2
    CHECK(fabs(fitted[1]) > 1e-9);
    const double ratio = fitted[0] / fitted[1];
    CHECK(near(ratio, -2.0, 1e-3));
    std::printf("[OK] implicit line A=%.4f B=%.4f C=%.4f (A/B=%.3f)\n",
                fitted[0], fitted[1], fitted[2], ratio);
    return 0;
}

/* 4) 种子可复现：相同 seed 两次结果一致（8 内点 + 2 离群点，应收敛） */
static int test_seed_reproducible() {
    const double x[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    double y[10];
    for (int i = 0; i < 10; ++i) y[i] = 2.0 * x[i] + 1.0;
    y[3] += 50.0;   // 离群
    y[7] += 50.0;   // 离群

    const char* params[] = {"a", "b"};
    double p1[2], p2[2];
    unsigned char m1[10], m2[10];
    double r1, r2;
    int it1, it2;
    RansacStatus s1, s2;
    char msg[256];

    ransac_fit_once("a*x+b", "x", nullptr, params, 2,
                    RANSAC_MODEL_EXPLICIT, RANSAC_RESIDUAL_VERTICAL,
                    2, nullptr, x, y, 10, 1.0, 500, 0.3, 99,
                    p1, 2, m1, 10, &r1, &it1, &s1, msg, sizeof(msg));
    ransac_fit_once("a*x+b", "x", nullptr, params, 2,
                    RANSAC_MODEL_EXPLICIT, RANSAC_RESIDUAL_VERTICAL,
                    2, nullptr, x, y, 10, 1.0, 500, 0.3, 99,
                    p2, 2, m2, 10, &r2, &it2, &s2, msg, sizeof(msg));
    CHECK(s1 == s2 && s1 == RANSAC_OK);
    CHECK(near(p1[0], p2[0], 1e-12));
    CHECK(near(p1[1], p2[1], 1e-12));
    CHECK(near(r1, r2, 1e-12));
    std::printf("[OK] seed reproducible a=%.6f b=%.6f\n", p1[0], p1[1]);
    return 0;
}

/* 5) 输出缓冲区长度不足 -> ERR_INVALID_ARG */
static int test_buffer_len() {
    const double x[] = {0, 1, 2, 3};
    const double y[] = {1, 3, 5, 7};
    const char* params[] = {"a", "b"};
    double fitted[1];   // 太小
    unsigned char mask[4];
    char msg[256];

    RansacHandle h = ransac_create("a*x+b", "x", nullptr, params, 2,
                                   RANSAC_MODEL_EXPLICIT, msg, sizeof(msg));
    CHECK(h != nullptr);

    RansacOptions opt = {};
    opt.ModelExpression = "a*x+b";
    opt.XName = "x";
    opt.ParamNames = params;
    opt.ParamCount = 2;
    opt.ModelType = RANSAC_MODEL_EXPLICIT;
    opt.MinSampleSize = 2;
    opt.Threshold = 1.0;
    opt.MaxIterations = 100;
    opt.OutlierRatio = 0.0;

    RansacResult res = {};
    res.ParamValues = fitted;    res.ParamValuesLen = 1;   // 不足
    res.InlierMask = mask;       res.InlierMaskLen = 4;
    res.StatusMessage = msg;     res.StatusMessageLen = sizeof(msg);

    int ret = ransac_fit(h, &opt, x, y, 4, &res);
    CHECK(ret == -1);
    CHECK(res.Status == RANSAC_ERR_INVALID_ARG);
    std::printf("[OK] buffer length rejected\n");
    ransac_destroy(h);
    return 0;
}

/* 6) 表达式白名单与解析错误 */
static int test_expr_safety() {
    const char* params[] = {"a"};
    char msg[256];
    // 白名单外函数
    RansacHandle h = ransac_create("eval(x)+a", "x", nullptr, params, 1,
                                   RANSAC_MODEL_EXPLICIT, msg, sizeof(msg));
    CHECK(h == nullptr);
    // 未知标识符
    h = ransac_create("a*z+1", "x", nullptr, params, 1,
                      RANSAC_MODEL_EXPLICIT, msg, sizeof(msg));
    CHECK(h == nullptr);
    // 合法表达式
    h = ransac_create("a*x+1", "x", nullptr, params, 1,
                      RANSAC_MODEL_EXPLICIT, msg, sizeof(msg));
    CHECK(h != nullptr);
    ransac_destroy(h);
    std::printf("[OK] expression whitelist enforced\n");
    return 0;
}

/* 7) v3 兼容层行为保持 */
static int test_v3_compat() {
    const double x[] = {0, 1, 2, 3, 4};
    const double y[] = {1, 3, 7, 13, 21};
    const char* params[] = {"a", "b", "c"};
    double init[] = {0.5, 0.5, 0.5};
    double fitted[3];
    unsigned char mask[5];
    double rss;
    int iters, status;
    char msg[256];

    int ret = ransac_generic_fit("a*pow(x,2)+b*x+c", params, 3, init, x, y, 5,
                                 "x", 1.0, 1000, 0.2, fitted, mask, &rss,
                                 &iters, &status, msg);
    CHECK(ret == 0);
    CHECK(status == 0);
    CHECK(near(fitted[0], 1.0, 1e-4));
    CHECK(near(fitted[1], 1.0, 1e-4));
    CHECK(near(fitted[2], 1.0, 1e-4));
    std::printf("[OK] v3 compat a=%.6f b=%.6f c=%.6f\n",
                fitted[0], fitted[1], fitted[2]);
    return 0;
}

int main() {
    if (test_quadratic_v4()) return 1;
    if (test_circle_implicit()) return 1;
    if (test_line_implicit_linear()) return 1;
    if (test_seed_reproducible()) return 1;
    if (test_buffer_len()) return 1;
    if (test_expr_safety()) return 1;
    if (test_v3_compat()) return 1;
    std::printf("RANSAC v4 ALL PASS\n");
    return 0;
}
