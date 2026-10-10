// test_params.cpp —— LoadParam 冒烟测试（不依赖测试框架，自带 CHECK）
//
//   覆盖：
//     * 缺键注册（Competence / Type / Limit / Value）与已存在时只读 Value
//     * bool 默认值纠偏（非法 -> 'TRUE'，合法 -> 保留）
//     * int[] / float[] / string[] 的字符串切分
//     * 数组形式的值（回归：以前会抛 type_error，现在原样返回）
//     * dict 展平、多级 level 自动建 dict、中间层类型错误抛异常
//     * ParamTree 并发：4 个线程对**同一棵树**并发 load/snapshot/to_json + 单线程整体 assign
//
//   用法：ctest 或直接运行；退出码 = 失败断言数（0 = 全通过）。

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "hparams_core.h"

using MLIQMetrics::LoadParam;

namespace {

std::atomic<int> g_fail{0};

// 不受 NDEBUG 影响的断言
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            ++g_fail;                                                            \
        }                                                                        \
    } while (0)

template <class F>
bool throws_any(F&& fn) {
    try {
        fn();
    } catch (...) {
        return true;
    }
    return false;
}

nlohmann::json no_limit() { return nlohmann::json::array(); }

// ------------------------------------------------------- 注册 / 读回
void test_register_and_readback() {
    nlohmann::json cfg = nlohmann::json::object();

    // 缺键 → 写入默认值并返回
    const auto v1 = LoadParam(cfg, {"检测参数", "背面", "面积筛选"}, 100, 1, "int", no_limit());
    CHECK(v1 == 100);

    const nlohmann::json& node = cfg["检测参数"]["背面"]["面积筛选"];
    CHECK(node.is_object());
    CHECK(node["Value"] == 100);
    CHECK(node["Competence"] == 1);
    CHECK(node["Type"] == "int");
    CHECK(node["Limit"] == no_limit());

    // 已存在 → 返回原 Value，且不改写 Competence
    const auto v2 = LoadParam(cfg, {"检测参数", "背面", "面积筛选"}, 999, 7, "int", no_limit());
    CHECK(v2 == 100);
    CHECK(cfg["检测参数"]["背面"]["面积筛选"]["Competence"] == 1);
}

// ------------------------------------------------------- bool 纠偏
void test_bool_fixup() {
    nlohmann::json cfg = nlohmann::json::object();

    const auto b1 = LoadParam(cfg, {"b1"}, true, 1, "bool", no_limit());  // 非字符串 → 'TRUE'
    CHECK(b1.is_string() && b1.get<std::string>() == "TRUE");

    const auto b2 = LoadParam(cfg, {"b2"}, "FALSE", 1, "bool", no_limit());  // 合法 → 保留
    CHECK(b2.get<std::string>() == "FALSE");

    const auto b3 = LoadParam(cfg, {"b3"}, "TRUE", 1, "bool", no_limit());
    CHECK(b3.get<std::string>() == "TRUE");
}

// ------------------------------------------------------- 数组类型
void test_array_types() {
    nlohmann::json cfg = nlohmann::json::object();

    // int[]：字符串切分（hdev 不取整 → 数值数组）
    const auto vi = LoadParam(cfg, {"i"}, "1,2,3", 1, "int[]", no_limit());
    CHECK(vi.is_array() && vi.size() == 3u && vi[0] == 1.0 && vi[2] == 3.0);

    // float[]
    const auto vf = LoadParam(cfg, {"f"}, "0.5,1.5", 1, "float[]", no_limit());
    CHECK(vf.size() == 2u && vf[1] == 1.5);

    // string[]
    const auto vs = LoadParam(cfg, {"s"}, "a,b,c", 1, "string[]", no_limit());
    CHECK(vs.size() == 3u && vs[0] == "a" && vs[2] == "c");

    // 回归：value 本身就是数组时不再抛 type_error
    const auto va =
        LoadParam(cfg, {"arr"}, nlohmann::json::array({1, 2, 3}), 1, "int[]", no_limit());
    CHECK(va.is_array() && va.size() == 3u && va[2] == 3);

    // dict → 按值顺序展平
    nlohmann::json d = nlohmann::json::object();
    d["k1"] = 1;
    d["k2"] = 2;
    const auto vd = LoadParam(cfg, {"d"}, d, 1, "float[]", no_limit());
    CHECK(vd.is_array() && vd.size() == 2u && vd[0] == 1 && vd[1] == 2);
}

// ------------------------------------------------------- 多级 level / 错误
void test_nested_and_errors() {
    nlohmann::json cfg = nlohmann::json::object();
    const auto v = LoadParam(cfg, {"a", "b", "c"}, 5, 1, "int", no_limit());
    CHECK(v == 5);
    CHECK(cfg["a"].is_object() && cfg["a"]["b"].is_object());
    CHECK(cfg["a"]["b"]["c"]["Value"] == 5);

    // 中间层不是 dict → 抛
    nlohmann::json bad = nlohmann::json::object();
    bad["x"] = 42;
    CHECK(throws_any([&] { LoadParam(bad, {"x", "y"}, 1, 1, "int", no_limit()); }));
}

// ------------------------------------------------------- ParamTree 并发（多线程共享同一棵树）
void test_paramtree_concurrency() {
    auto tree = std::make_shared<MLIQMetrics::ParamTree>();

    std::atomic<bool> stop{false};
    std::atomic<long long> ops{0};

    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&, t] {
            const std::string lvl = "w" + std::to_string(t);
            while (!stop.load(std::memory_order_relaxed)) {
                // 各自独立的 level（并发写同一棵树）；值恒为 1，便于断言
                const auto v = tree->load({"g", lvl, "p"}, 1, 1, "int", no_limit());
                CHECK(v == 1);
                CHECK(tree->snapshot().is_object());
                CHECK(!tree->to_json().empty());
                ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::thread writer([&] {
        for (int i = 0; i < 4000; ++i) {
            tree->load({"shared", "k"}, i, 1, "int", no_limit());
            tree->load({"shared", "s"}, std::string("v"), 1, "string", no_limit());
            if (i % 200 == 0) tree->assign(nlohmann::json::object());  // 整体替换（写锁）
        }
        stop.store(true);
    });

    writer.join();
    for (auto& th : readers) th.join();

    CHECK(ops.load() > 0);
    std::printf("paramtree concurrency: %lld ops, ok\n", ops.load());
}

}  // namespace

int main() {
    test_register_and_readback();
    test_bool_fixup();
    test_array_types();
    test_nested_and_errors();
    test_paramtree_concurrency();

    if (g_fail.load() == 0) {
        std::printf("ALL PASS\n");
        return 0;
    }
    std::fprintf(stderr, "%d CHECK(s) FAILED\n", g_fail.load());
    return 1;
}
