#pragma once
// =============================================================================
// hparams_core.h —— 复刻 BackLightCracks.hdev 的递归 JSON 参数解析器（LoadParams）
//
//   LoadParam(Parmsetting, level, DefaultValue, Competence, Type, Limit) -> Value
//
//   * level = [一级目录, 二级目录, 标签]，例如 {"Check parameters","Back scratch front","Area filter"}
//   * 语义  = “查不到就写默认值并自注册”：
//       节点不存在 -> 写入 Competence / Type / Limit / Value，返回默认值
//       节点已存在 -> 直接返回它的 "Value"
//     （所以 Detection_init / Detection_process 传入的参数字典会被就地补全）
//   * Type = 'int' / 'float' / 'bool' / 'string' / 'int[]' / 'float[]' / 'string[]' / ...
//     bool 类型会把非法默认值纠成 'TRUE'；数组类型会做字符串切分 / dict 展平（照抄 hdev）
//
//   与 hdev 的差异（有意为之）：
//     * hdev 的 LoadParams 带 n 递归下标，这里收进 .cpp 内部；对外只暴露一次到位的
//       LoadParam（level 传整条路径），行为一致。
//
//   线程安全：
//     * LoadParam(json&, ...) 是**无状态纯函数**（只用入参 + 调用方传入的 json）：
//       各线程操作各自的 json 对象时天然安全；多线程共享**同一个** json 对象则需调用方自己加锁。
//     * 要在多线程间共享**同一棵**参数树，用带锁的 ParamTree（内嵌 shared_mutex：
//       load 取写锁、snapshot/to_json 取读锁），句柄语义用 std::shared_ptr<ParamTree>。
//     * 依赖只有 nlohmann/json.hpp（header-only）+ STL。
// =============================================================================

#include <memory>
#include <shared_mutex>  // std::shared_mutex（C++17）
#include <string>
#include <vector>

#include "json.hpp"

namespace MLIQMetrics {

// 查不到就注册默认值并返回；已存在则返回其 "Value"（不覆盖已存内容）。
// 无状态纯函数：不碰全局/静态数据，多线程各用各的 json 对象时天然安全。
nlohmann::json LoadParam(nlohmann::json& parmSetting, const std::vector<std::string>& level,
                         const nlohmann::json& defaultValue, int competence,
                         const std::string& type, const nlohmann::json& limit);

// ---------------------------------------------------------------- 线程安全的参数树
// 多线程共享**同一棵**参数树时用这个（LoadParam 本身不加锁）：
//   * load()     —— 就地装载（语义与 LoadParam 完全一致），内部取写锁
//   * assign()   —— 整体替换，取写锁
//   * snapshot() —— 取整棵树的拷贝，取读锁
//   * to_json()  —— 导出紧凑 JSON（UTF-8 无 BOM、键按字典序），取读锁
// 因含 std::shared_mutex 而**不可拷贝/移动**；句柄语义请用 std::shared_ptr<ParamTree>。
class ParamTree {
public:
    ParamTree();
    explicit ParamTree(nlohmann::json tree);

    ParamTree(const ParamTree&)            = delete;
    ParamTree& operator=(const ParamTree&) = delete;
    ParamTree(ParamTree&&)                 = delete;
    ParamTree& operator=(ParamTree&&)      = delete;

    nlohmann::json load(const std::vector<std::string>& level,
                        const nlohmann::json& defaultValue, int competence,
                        const std::string& type, const nlohmann::json& limit);

    void assign(nlohmann::json tree);

    nlohmann::json snapshot() const;
    std::string    to_json() const;

private:
    mutable std::shared_mutex mtx_;
    nlohmann::json            data_;
};

using ParamTreePtr = std::shared_ptr<ParamTree>;

}  // namespace MLIQMetrics
