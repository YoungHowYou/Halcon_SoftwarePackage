#pragma once
// =============================================================================
// HDict —— 复刻 HALCON dict 的“键值对容器 / 传句柄”用途
//
//   * 值可以是：tuple(JSON) / 任意对象(std::any) / 子 dict
//   * 句柄语义：DictPtr = shared_ptr<Dict>，就是检测接口文档里的 ResourceDictPtr
//   * 纯数据 + 自由函数：
//       - 不做 dllexport（Dict 全是标准库类型，避免 C4251，静态库/动态库都好用）
//       - 不依赖 OpenCV / cvr：对象一律走 std::any，谁用谁 any_cast
//
//   ---------------------------------------------------------------------------
//   线程安全（B 档：读写分离锁）
//     Dict 内嵌 std::shared_mutex：
//       - 写（set_dict_* / take_dict_object / remove_dict_key）：unique_lock（互斥）
//       - 读（get_dict_* / key_exists / get_dict_param_* / key_kind_name）：shared_lock
//     每个单独调用都是线程安全的；但**多次调用的组合不是原子的**（例如
//     “先 key_exists 再 set”仍有 TOCTOU 窗口，需要调用方在外面再加锁）。
//     取出的 std::any / json 是**拷贝**，调用方持有期间完全不受 dict 被改写的影响。
//
//   ---------------------------------------------------------------------------
//   零拷贝（能省一次拷贝就省一次）
//     * 对象优先装 std::shared_ptr<T>：set/get 只做原子引用计数，不深拷贝。
//       set_dict_object_shared(d, "img", imgPtr);                      // 零深拷贝
//       auto img = get_dict_object_as<std::shared_ptr<Image>>(d, "img");
//     * 手上有右值（大 json / 大 any）就直接 move 进来，避免整份拷贝：
//       set_dict_tuple(d, "pts", std::move(bigJson));
//       set_dict_object(d, "blob", std::move(bigAny));
//     * take_dict_object()：把对象**移出**并删除该键，零拷贝拿走所有权。
//     * get_dict_tuple / get_dict_object 返回的是拷贝（共享锁下拷贝，保证线程安全）；
//       要零拷贝取共享句柄走 get_dict_object_as<std::shared_ptr<T>>。
//   ---------------------------------------------------------------------------
// =============================================================================

#include <any>
#include <map>
#include <memory>
#include <shared_mutex>  // std::shared_mutex（C++17）
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"

namespace MLIQMetrics {
namespace hdict {

struct Dict {
    enum class Kind { Empty, Tuple, Object, SubDict };

    struct Value {
        Kind                  kind = Kind::Empty;
        nlohmann::json        tuple;   // Kind::Tuple
        std::any              object;  // Kind::Object
        std::shared_ptr<Dict> sub;     // Kind::SubDict
    };

    Dict() = default;

    // 含 std::shared_mutex ⇒ 不可拷贝/移动（需要值拷贝请用 copy_dict()）
    Dict(const Dict&)            = delete;
    Dict& operator=(const Dict&) = delete;
    Dict(Dict&&)                 = delete;
    Dict& operator=(Dict&&)      = delete;

    mutable std::shared_mutex    mtx;    // 写 = unique_lock，读 = shared_lock
    std::map<std::string, Value> items;
};

using DictPtr = std::shared_ptr<Dict>;

// ---------------------------------------------------------------- 创建 / 复制
DictPtr create_dict();
DictPtr copy_dict(const DictPtr& dict);  // 深拷贝键值（子 dict 共享，同 HALCON）

// ---------------------------------------------------------------- tuple 存取
void           set_dict_tuple(const DictPtr& dict, const std::string& key, const nlohmann::json& tuple);
void           set_dict_tuple(const DictPtr& dict, const std::string& key, nlohmann::json&& tuple);  // 零拷贝（move）
void           set_dict_tuple(const DictPtr& dict, const std::string& key, const DictPtr& subDict);
void           set_dict_tuple(const DictPtr& dict, const std::string& key, const char* value);
void           set_dict_tuple(const DictPtr& dict, const std::string& key, const std::string& value);
void           set_dict_tuple(const DictPtr& dict, const std::string& key, bool value);
void           set_dict_tuple(const DictPtr& dict, const std::string& key, int value);
void           set_dict_tuple(const DictPtr& dict, const std::string& key, double value);

// 缺键抛 std::runtime_error；键是子 dict / 对象时同样抛错（不再静默返回空 json）
nlohmann::json get_dict_tuple(const DictPtr& dict, const std::string& key);

// ---------------------------------------------------------------- 对象存取
// HALCON 里 set_dict_object 放的是“图 / 区域 / 句柄”这类对象；这里统一走 std::any。
// 建议装 std::shared_ptr<T>：set/get 只动引用计数，零深拷贝。
void     set_dict_object(const DictPtr& dict, const std::string& key, const std::any& object);
void     set_dict_object(const DictPtr& dict, const std::string& key, std::any&& object);  // 零拷贝（move）
std::any get_dict_object(const DictPtr& dict, const std::string& key);  // 缺键抛 / 非对象抛（拷贝）
std::any take_dict_object(const DictPtr& dict, const std::string& key); // 移出并删除该键（零拷贝）

template <class T>
T get_dict_object_as(const DictPtr& dict, const std::string& key) {
    return std::any_cast<T>(get_dict_object(dict, key));
}

// 便捷：装共享句柄（零深拷贝，只 +1 引用计数）
template <class T>
void set_dict_object_shared(const DictPtr& dict, const std::string& key, std::shared_ptr<T> object) {
    set_dict_object(dict, key, std::any(std::move(object)));
}

// ------------------------------------------------------------ get_dict_param
std::vector<std::string> get_dict_param_keys(const DictPtr& dict);
std::vector<bool>        get_dict_param_key_exists(const DictPtr& dict,
                                                   const std::vector<std::string>& keys);
bool                     key_exists(const DictPtr& dict, const std::string& key);
std::string              key_kind_name(const DictPtr& dict, const std::string& key);

bool remove_dict_key(const DictPtr& dict, const std::string& key);

}  // namespace hdict
}  // namespace MLIQMetrics
