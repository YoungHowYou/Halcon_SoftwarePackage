// test_hdict.cpp —— HDict 冒烟 + 并发测试（不依赖任何测试框架）
//
//   覆盖：
//     * tuple / object / sub-dict 三类值的存取与类型校验（缺键抛、取错类型抛）
//     * copy_dict 的独立性、keys / key_exists / key_kind_name / remove_dict_key
//     * 零拷贝路径：move 传入（json / any）、shared_ptr 引用计数、take_dict_object
//     * shared_mutex（B 档）下的「多读 + 单写」并发正确性（不崩、值自洽）
//
//   用法：ctest，或直接运行；退出码 = 失败断言数（0 = 全通过）。

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "hdict_core.h"

using MLIQMetrics::hdict::DictPtr;
using namespace MLIQMetrics::hdict;

namespace {

std::atomic<int> g_fail{0};

// 不受 NDEBUG 影响的断言（裸 assert 在 Release 会被编译掉 → 假通过）
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            g_fail.fetch_add(1);                                                 \
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

// ---------------------------------------------------------------- 基础存取
void test_tuple() {
    DictPtr d = create_dict();
    set_dict_tuple(d, "i", 42);
    set_dict_tuple(d, "x", 3.5);
    set_dict_tuple(d, "s", std::string("hi"));
    set_dict_tuple(d, "b", true);
    set_dict_tuple(d, "arr", nlohmann::json::array({1, 2, 3}));

    CHECK(get_dict_tuple(d, "i").get<int>() == 42);
    CHECK(get_dict_tuple(d, "x").get<double>() == 3.5);
    CHECK(get_dict_tuple(d, "s").get<std::string>() == "hi");
    CHECK(get_dict_tuple(d, "b").get<bool>() == true);
    CHECK(get_dict_tuple(d, "arr").size() == 3u);

    // 缺键 → 抛
    CHECK(throws_any([&] { get_dict_tuple(d, "nope"); }));

    // 取错类型 → 抛（object 键当 tuple 取、tuple 键当 object 取）
    set_dict_object(d, "obj", std::any(123));
    CHECK(throws_any([&] { get_dict_tuple(d, "obj"); }));
    CHECK(throws_any([&] { get_dict_object(d, "i"); }));
    CHECK(throws_any([&] { get_dict_object(d, "nope"); }));

    // 空句柄
    DictPtr null;
    CHECK(throws_any([&] { get_dict_tuple(null, "i"); }));
    CHECK(throws_any([&] { set_dict_tuple(null, "i", 1); }));
    CHECK(!key_exists(null, "i"));
    CHECK(!remove_dict_key(null, "i"));
}

// ---------------------------------------------------------------- 子 dict / 复制
void test_subdict_and_copy() {
    DictPtr child = create_dict();
    set_dict_tuple(child, "v", 7);

    DictPtr d = create_dict();
    set_dict_tuple(d, "child", child);
    CHECK(key_kind_name(d, "child") == "dict");
    // 子 dict 当 tuple 取 → 抛（旧实现会静默返回空 json）
    CHECK(throws_any([&] { get_dict_tuple(d, "child"); }));

    // 复制：改副本不影响源；副本里 child 仍以子 dict 形式存在
    DictPtr c = copy_dict(d);
    CHECK(key_exists(c, "child"));
    CHECK(key_kind_name(c, "child") == "dict");
    set_dict_tuple(c, "extra", 1);
    CHECK(!key_exists(d, "extra"));

    // 子 dict 共享同一份（与 HALCON 行为一致）：改 child 两边都看得到
    set_dict_tuple(child, "v2", 8);
    CHECK(get_dict_tuple(child, "v2").get<int>() == 8);
}

// ---------------------------------------------------------------- keys / kind / remove
void test_remove_keys_kind() {
    DictPtr d = create_dict();
    set_dict_tuple(d, "a", 1);
    set_dict_tuple(d, "b", 2.0);
    set_dict_object(d, "c", std::any(1));

    CHECK(key_kind_name(d, "a") == "tuple");
    CHECK(key_kind_name(d, "c") == "object");
    CHECK(key_kind_name(d, "zzz") == "empty");

    const auto keys = get_dict_param_keys(d);
    CHECK(keys.size() == 3u && keys[0] == "a" && keys[1] == "b" && keys[2] == "c");  // map 有序

    const auto ex = get_dict_param_key_exists(d, {"a", "zzz"});
    CHECK(ex.size() == 2u && ex[0] && !ex[1]);

    CHECK(remove_dict_key(d, "a"));
    CHECK(!remove_dict_key(d, "a"));
    CHECK(!key_exists(d, "a"));
    CHECK(get_dict_param_keys(d).size() == 2u);
}

// ---------------------------------------------------------------- 零拷贝路径
void test_zero_copy() {
    DictPtr d = create_dict();

    // ① move 传入大 json：源被搬空
    nlohmann::json big = nlohmann::json::array();
    for (int i = 0; i < 1000; ++i) big.push_back(i);
    set_dict_tuple(d, "big", std::move(big));
    CHECK(big.is_null());
    CHECK(get_dict_tuple(d, "big").size() == 1000u);

    // ② move 传入 any（源状态是“有效但未指定”，不做断言）
    std::any a = std::string("payload");
    set_dict_object(d, "any", std::move(a));
    CHECK(get_dict_object_as<std::string>(d, "any") == "payload");
    CHECK(get_dict_object(d, "any").has_value());

    // ③ shared_ptr：只动引用计数，零深拷贝
    auto sp = std::make_shared<std::string>(1000, 'x');
    CHECK(sp.use_count() == 1);
    set_dict_object_shared(d, "str", sp);
    CHECK(sp.use_count() == 2);  // dict 持有一份
    {
        auto got = get_dict_object_as<std::shared_ptr<std::string>>(d, "str");
        CHECK(got == sp);
        CHECK(got->size() == 1000u);
        CHECK(sp.use_count() == 3);  // 取出时多一份（临时）
    }
    CHECK(sp.use_count() == 2);  // 临时销毁后回到 dict

    // ④ take：移出并删除键，零拷贝拿走所有权
    std::any taken = take_dict_object(d, "str");
    CHECK(!key_exists(d, "str"));
    CHECK(std::any_cast<std::shared_ptr<std::string>>(taken) == sp);
    CHECK(sp.use_count() == 2);  // taken 仍持有一份
    CHECK(throws_any([&] { take_dict_object(d, "str"); }));  // 已删 → 缺键抛
}

// ---------------------------------------------------------------- 并发（多读 + 单写）
void test_concurrency() {
    DictPtr d = create_dict();
    set_dict_tuple(d, "a", 0);
    set_dict_tuple(d, "s", std::string("hello"));
    set_dict_object_shared(d, "p", std::make_shared<int>(7));

    std::atomic<bool> stop{false};
    std::atomic<long long> reads{0};

    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                auto j = get_dict_tuple(d, "a");
                CHECK(j.is_number());
                auto s = get_dict_tuple(d, "s");
                CHECK(s.is_string());
                auto p = get_dict_object_as<std::shared_ptr<int>>(d, "p");
                CHECK(p && *p == 7);
                CHECK(get_dict_param_keys(d).size() >= 3u);
                reads.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::thread writer([&] {
        for (int i = 0; i < 20000; ++i) {
            set_dict_tuple(d, "a", i);
            set_dict_tuple(d, "k" + std::to_string(i % 7), i);           // 增
            if (i % 3 == 0) remove_dict_key(d, "k" + std::to_string((i + 3) % 7));  // 删
            set_dict_object_shared(d, "p", std::make_shared<int>(7));    // 换共享句柄
        }
        stop.store(true);
    });

    writer.join();
    for (auto& th : readers) th.join();

    CHECK(reads.load() > 0);
    std::printf("concurrency: %lld reads, ok\n", reads.load());
}

}  // namespace

int main() {
    test_tuple();
    test_subdict_and_copy();
    test_remove_keys_kind();
    test_zero_copy();
    test_concurrency();

    if (g_fail.load() == 0) {
        std::printf("ALL PASS\n");
        return 0;
    }
    std::fprintf(stderr, "%d CHECK(s) FAILED\n", g_fail.load());
    return 1;
}
