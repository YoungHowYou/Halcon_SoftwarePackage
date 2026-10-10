// hparams_core.cpp —— 逐句对齐 BackLightCracks.hdev 的 LoadParams
//   （hdev 用 n 做递归下标，这里等价地收进本文件内部的 load_params()，对外只给 LoadParam）

#include "hparams_core.h"

#include <cstddef>
#include <mutex>      // std::unique_lock
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace MLIQMetrics {

namespace {

// 复刻 hdev 的 tuple_split(Value, '[,]', Parts)：hdev 只用逗号这一种分隔符
std::vector<std::string> split_by_comma(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

// 复刻 hdev 的 tuple_number：字符串 -> 实数
//   注意：hdev 里 int[] 也走 tuple_number，得到的是实数（不取整）—— 与 hdev 保持一致
std::vector<double> to_numbers(const std::vector<std::string>& parts) {
    std::vector<double> out;
    out.reserve(parts.size());
    for (const auto& s : parts) {
        try {
            out.push_back(std::stod(s));
        } catch (...) {
            out.push_back(0.0);
        }
    }
    return out;
}

// 复刻 hdev：if(Type='int[]' or Type='float[]' or Type='string[]') 那段
//   字符串 -> 按 ',' 切分（int[] / float[] 再转数值）
//   数组   -> 本来就是数组，原样返回
//   dict   -> 把所有值按顺序展平成一个 tuple
nlohmann::json HandleArrayType(const nlohmann::json& value, const std::string& type) {
    if (value.is_string()) {
        const std::vector<std::string> parts = split_by_comma(value.get<std::string>());
        if (type == "int[]" || type == "float[]") {
            return nlohmann::json(to_numbers(parts));
        }
        return nlohmann::json(parts);
    }

    if (value.is_array()) {
        return value;
    }

    if (value.is_object()) {
        nlohmann::json tuple = nlohmann::json::array();
        for (auto it = value.begin(); it != value.end(); ++it) tuple.push_back(it.value());
        return tuple;
    }

    return value;
}

// hdev 的 LoadParams(..., n, ...) 本体（n = 递归下标，仅内部使用）
nlohmann::json load_params(nlohmann::json& node, const std::vector<std::string>& level,
                           std::size_t n, const nlohmann::json& defaultValue, int competence,
                           const std::string& type, const nlohmann::json& limit) {
    if (n < level.size()) {
        // 逐级下钻；中间层不存在就建 dict（复刻 hdev 的 create_dict + set_dict_tuple）
        if (!node.is_object()) {
            throw std::runtime_error("LoadParam: \"" + level[n] + "\" 不是字典节点");
        }
        auto it = node.find(level[n]);
        if (it == node.end()) {
            node[level[n]] = nlohmann::json::object();
            it = node.find(level[n]);
        }
        if (!it->is_object()) {
            throw std::runtime_error("LoadParam: \"" + level[n] + "\" 不是字典节点");
        }
        return load_params(*it, level, n + 1, defaultValue, competence, type, limit);
    }

    nlohmann::json value;
    if (!node.is_object() || node.find("Value") == node.end()) {
        node["Competence"] = competence;
        node["Type"] = type;
        node["Limit"] = limit;

        // 复刻 bool 类型默认值纠偏
        nlohmann::json def = defaultValue;
        if (type == "bool") {
            const bool legal = def.is_string() && (def.get<std::string>() == "FALSE" ||
                                                   def.get<std::string>() == "TRUE");
            if (!legal) def = "TRUE";
        }

        node["Value"] = def;
        value = def;
    } else {
        value = node["Value"];
    }

    if (type == "int[]" || type == "float[]" || type == "string[]") {
        value = HandleArrayType(value, type);
    }

    return value;
}

}  // namespace

nlohmann::json LoadParam(nlohmann::json& parmSetting, const std::vector<std::string>& level,
                         const nlohmann::json& defaultValue, int competence,
                         const std::string& type, const nlohmann::json& limit) {
    return load_params(parmSetting, level, 0, defaultValue, competence, type, limit);
}

// ---------------------------------------------------------------- ParamTree

ParamTree::ParamTree() : data_(nlohmann::json::object()) {}

ParamTree::ParamTree(nlohmann::json tree) : data_(std::move(tree)) {}

nlohmann::json ParamTree::load(const std::vector<std::string>& level,
                               const nlohmann::json& defaultValue, int competence,
                               const std::string& type, const nlohmann::json& limit) {
    std::unique_lock<std::shared_mutex> lk(mtx_);  // 就地补全整棵树 → 独占
    return LoadParam(data_, level, defaultValue, competence, type, limit);
}

void ParamTree::assign(nlohmann::json tree) {
    std::unique_lock<std::shared_mutex> lk(mtx_);
    data_ = std::move(tree);
}

nlohmann::json ParamTree::snapshot() const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    return data_;
}

std::string ParamTree::to_json() const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    return data_.dump();
}

}  // namespace MLIQMetrics
