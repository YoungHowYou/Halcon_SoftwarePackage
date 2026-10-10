// hdict_core.cpp —— 实现见 hdict_core.h

#include "hdict_core.h"

#include <mutex>      // std::unique_lock
#include <stdexcept>
#include <utility>

namespace MLIQMetrics {
namespace hdict {

namespace {

[[noreturn]] void throw_missing_key(const std::string& key) {
    throw std::runtime_error("dict: 键 \"" + key + "\" 不存在");
}

void ensure_handle(const DictPtr& dict, const char* what) {
    if (!dict) throw std::runtime_error(std::string(what) + ": 句柄为空");
}

// 只读查找（调用方必须已持锁）
const Dict::Value* find_value_locked(const Dict& dict, const std::string& key) {
    auto it = dict.items.find(key);
    return it == dict.items.end() ? nullptr : &it->second;
}

}  // namespace

// ---------------------------------------------------------------- 创建 / 复制
DictPtr create_dict() { return std::make_shared<Dict>(); }

DictPtr copy_dict(const DictPtr& dict) {
    ensure_handle(dict, "copy_dict");
    // Dict 含 std::shared_mutex ⇒ 不可拷贝构造，手工拷键值；
    // out 尚未发布（其它线程不可见），故 out 侧无需加锁。
    auto out = std::make_shared<Dict>();
    std::shared_lock<std::shared_mutex> lk(dict->mtx);
    out->items = dict->items;  // 值语义拷贝（子 dict 共享同一份，与 HALCON 行为一致）
    return out;
}

// ---------------------------------------------------------------- tuple 存取
void set_dict_tuple(const DictPtr& dict, const std::string& key, const nlohmann::json& tuple) {
    set_dict_tuple(dict, key, nlohmann::json(tuple));  // 锁外拷贝一份，再 move 进 map
}

void set_dict_tuple(const DictPtr& dict, const std::string& key, nlohmann::json&& tuple) {
    ensure_handle(dict, "set_dict_tuple");
    Dict::Value v;  // 锁外备好值（拷贝/移动都发生在这里）
    v.kind  = Dict::Kind::Tuple;
    v.tuple = std::move(tuple);
    std::unique_lock<std::shared_mutex> lk(dict->mtx);
    dict->items[key] = std::move(v);  // 锁内只有 map 操作
}

void set_dict_tuple(const DictPtr& dict, const std::string& key, const DictPtr& subDict) {
    ensure_handle(dict, "set_dict_tuple");
    Dict::Value v;
    v.kind = Dict::Kind::SubDict;
    v.sub  = subDict;
    std::unique_lock<std::shared_mutex> lk(dict->mtx);
    dict->items[key] = std::move(v);
}

void set_dict_tuple(const DictPtr& dict, const std::string& key, const char* value) {
    set_dict_tuple(dict, key, nlohmann::json(std::string(value == nullptr ? "" : value)));
}
void set_dict_tuple(const DictPtr& dict, const std::string& key, const std::string& value) {
    set_dict_tuple(dict, key, nlohmann::json(value));
}
void set_dict_tuple(const DictPtr& dict, const std::string& key, bool value) {
    set_dict_tuple(dict, key, nlohmann::json(value));
}
void set_dict_tuple(const DictPtr& dict, const std::string& key, int value) {
    set_dict_tuple(dict, key, nlohmann::json(value));
}
void set_dict_tuple(const DictPtr& dict, const std::string& key, double value) {
    set_dict_tuple(dict, key, nlohmann::json(value));
}

nlohmann::json get_dict_tuple(const DictPtr& dict, const std::string& key) {
    ensure_handle(dict, "get_dict_tuple");
    std::shared_lock<std::shared_mutex> lk(dict->mtx);
    const Dict::Value* v = find_value_locked(*dict, key);
    if (v == nullptr) throw_missing_key(key);
    switch (v->kind) {
        case Dict::Kind::Tuple:
            return v->tuple;  // 共享锁下拷贝：多读并行，写者短暂等待
        case Dict::Kind::SubDict:
            throw std::runtime_error("get_dict_tuple: 键 \"" + key + "\" 是子 dict，不是 tuple");
        default:
            throw std::runtime_error("get_dict_tuple: 键 \"" + key + "\" 不是 tuple");
    }
}

// ---------------------------------------------------------------- 对象存取
void set_dict_object(const DictPtr& dict, const std::string& key, const std::any& object) {
    set_dict_object(dict, key, std::any(object));  // 锁外拷贝一份，再 move 进 map
}

void set_dict_object(const DictPtr& dict, const std::string& key, std::any&& object) {
    ensure_handle(dict, "set_dict_object");
    Dict::Value v;
    v.kind   = Dict::Kind::Object;
    v.object = std::move(object);
    std::unique_lock<std::shared_mutex> lk(dict->mtx);
    dict->items[key] = std::move(v);
}

std::any get_dict_object(const DictPtr& dict, const std::string& key) {
    ensure_handle(dict, "get_dict_object");
    std::shared_lock<std::shared_mutex> lk(dict->mtx);
    const Dict::Value* v = find_value_locked(*dict, key);
    if (v == nullptr) throw_missing_key(key);
    if (v->kind != Dict::Kind::Object) {
        throw std::runtime_error("get_dict_object: 键 \"" + key + "\" 不是对象");
    }
    return v->object;
}

std::any take_dict_object(const DictPtr& dict, const std::string& key) {
    ensure_handle(dict, "take_dict_object");
    std::unique_lock<std::shared_mutex> lk(dict->mtx);
    auto it = dict->items.find(key);
    if (it == dict->items.end()) throw_missing_key(key);
    if (it->second.kind != Dict::Kind::Object) {
        throw std::runtime_error("take_dict_object: 键 \"" + key + "\" 不是对象");
    }
    std::any out = std::move(it->second.object);  // 零拷贝移出
    dict->items.erase(it);
    return out;
}

// ------------------------------------------------------------ get_dict_param
std::vector<std::string> get_dict_param_keys(const DictPtr& dict) {
    ensure_handle(dict, "get_dict_param");
    std::vector<std::string> keys;
    std::shared_lock<std::shared_mutex> lk(dict->mtx);
    keys.reserve(dict->items.size());
    for (const auto& kv : dict->items) keys.push_back(kv.first);
    return keys;
}

std::vector<bool> get_dict_param_key_exists(const DictPtr& dict,
                                            const std::vector<std::string>& keys) {
    std::vector<bool> out;
    out.reserve(keys.size());
    for (const auto& k : keys) out.push_back(key_exists(dict, k));
    return out;
}

bool key_exists(const DictPtr& dict, const std::string& key) {
    if (!dict) return false;
    std::shared_lock<std::shared_mutex> lk(dict->mtx);
    return dict->items.find(key) != dict->items.end();
}

std::string key_kind_name(const DictPtr& dict, const std::string& key) {
    ensure_handle(dict, "key_kind_name");
    std::shared_lock<std::shared_mutex> lk(dict->mtx);
    const Dict::Value* v = find_value_locked(*dict, key);
    if (v == nullptr) return "empty";
    switch (v->kind) {
        case Dict::Kind::Tuple:   return "tuple";
        case Dict::Kind::Object:  return "object";
        case Dict::Kind::SubDict: return "dict";
        default:                  return "empty";
    }
}

bool remove_dict_key(const DictPtr& dict, const std::string& key) {
    if (!dict) return false;
    std::unique_lock<std::shared_mutex> lk(dict->mtx);
    return dict->items.erase(key) > 0;
}

}  // namespace hdict
}  // namespace MLIQMetrics
