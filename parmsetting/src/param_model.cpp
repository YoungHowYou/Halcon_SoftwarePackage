#include "param_model.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <system_error>

namespace parm_ui {

// ------------------------------------------------------------------ 类型名
const char* ptype_text(PType t) {
    switch (t) {
        case PType::Int:         return "int";
        case PType::Float:       return "float";
        case PType::Bool:        return "bool";
        case PType::String:      return "string";
        case PType::StringEnum:  return "string_enum";
        case PType::IntEnum:     return "int_enum";
        case PType::FloatEnum:   return "float_enum";
        case PType::IntArray:    return "int[]";
        case PType::FloatArray:  return "float[]";
        case PType::StringArray: return "string[]";
        case PType::Directory:   return "directory";
        case PType::Path:        return "path";
        case PType::Unknown:     break;
    }
    return "unknown";
}

PType ptype_from_text(const std::string& s) {
    if (s == "int")         return PType::Int;
    if (s == "float")       return PType::Float;
    if (s == "bool")        return PType::Bool;
    if (s == "string")      return PType::String;
    if (s == "string_enum") return PType::StringEnum;
    if (s == "int_enum")    return PType::IntEnum;
    if (s == "float_enum")  return PType::FloatEnum;
    if (s == "int[]")       return PType::IntArray;
    if (s == "float[]")     return PType::FloatArray;
    if (s == "string[]")    return PType::StringArray;
    if (s == "directory")   return PType::Directory;
    if (s == "path")        return PType::Path;
    return PType::Unknown;
}

bool ptype_is_array(PType t) {
    return t == PType::IntArray || t == PType::FloatArray || t == PType::StringArray;
}
bool ptype_is_enum(PType t) {
    return t == PType::StringEnum || t == PType::IntEnum || t == PType::FloatEnum;
}
bool ptype_is_numeric(PType t) {
    return t == PType::Int || t == PType::Float || t == PType::IntEnum || t == PType::FloatEnum ||
           t == PType::IntArray || t == PType::FloatArray;
}

std::string path_breadcrumb(const std::vector<std::string>& path) {
    std::string s;
    for (std::size_t k = 0; k < path.size(); ++k) {
        if (k) s += " / ";
        s += path[k];
    }
    return s;
}

int Doc::count_fatal() const {
    int n = 0;
    for (const auto& d : diags)
        if (d.fatal) ++n;
    return n;
}

int Doc::count_warning() const {
    int n = 0;
    for (const auto& d : diags)
        if (!d.fatal) ++n;
    return n;
}

// ============================================================================
namespace {

const int kMaxDepth = 64;  // 防御畸形数据的超深嵌套

void add_diag(std::vector<Diagnostic>& diags, bool fatal, const std::string& where,
              const std::string& msg) {
    diags.push_back(Diagnostic{fatal, where, msg});
}

std::string to_upper(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

bool parse_int_text(const std::string& s, std::int64_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno     = 0;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    out = static_cast<std::int64_t>(v);
    return true;
}

bool parse_double_text(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno     = 0;
    const double v = std::strtod(s.c_str(), &end);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

// 数值显示文本（枚举候选、错误信息用）
std::string num_text(const Json& j) {
    switch (j.kind) {
        case Json::Kind::Int:  return std::to_string(j.i);
        case Json::Kind::UInt: return std::to_string(j.u);
        case Json::Kind::Double: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.10g", j.d);
            return std::string(buf);
        }
        case Json::Kind::String: return j.str;
        default: return j.as_string();
    }
}

// ---------------------------------------------------------------- 归一 Value
// 成功 true；warn 非空 = 有警告（已纠正但可用）；err 非空 = 失败
bool to_int_value(const Json& in, Json& out, std::string& warn) {
    switch (in.kind) {
        case Json::Kind::Int:
            out = Json::make_int(in.i);
            return true;
        case Json::Kind::UInt:
            if (in.u <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                out = Json::make_int(static_cast<std::int64_t>(in.u));
                return true;
            }
            return false;
        case Json::Kind::Double:
            if (std::floor(in.d) == in.d && std::fabs(in.d) < 9.2e18) {
                out   = Json::make_int(static_cast<std::int64_t>(in.d));
                warn  = "Value 是浮点数，已按整数处理";
                return true;
            }
            return false;
        case Json::Kind::String: {
            std::int64_t iv = 0;
            if (parse_int_text(in.str, iv)) {
                out  = Json::make_int(iv);
                warn = "Value 是字符串，已按整数处理";
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

bool normalize_value(const Json& in, PType t, Json& out, std::string& warn, std::string& err) {
    warn.clear();
    err.clear();

    switch (t) {
        case PType::Int:
        case PType::IntEnum: {
            if (!to_int_value(in, out, warn)) {
                err = std::string("Value 应为整数，实际是 ") + in.type_name();
                return false;
            }
            return true;
        }
        case PType::Float:
        case PType::FloatEnum: {
            if (in.kind == Json::Kind::Double) {
                out = in;  // 本身就是浮点：原样保留（连原始字面量一起，如 1.0e2 / 0.500）
                return true;
            }
            if (in.is_number()) {
                out  = Json::make_double(in.as_double());
                warn = "Value 是整数，已按浮点处理";
                return true;
            }
            if (in.is_string()) {
                double dv = 0.0;
                if (parse_double_text(in.str, dv)) {
                    out  = Json::make_double(dv);
                    warn = "Value 是字符串，已按浮点处理";
                    return true;
                }
            }
            err = std::string("Value 应为浮点数，实际是 ") + in.type_name();
            return false;
        }
        case PType::Bool: {
            // 按规范：bool 的 Value 是字符串 "TRUE" / "FALSE"
            if (in.is_bool()) {
                out = Json::make_string(in.b ? "TRUE" : "FALSE");
                return true;
            }
            if (in.is_string()) {
                const std::string up = to_upper(in.str);
                if (up == "TRUE") {
                    out = Json::make_string("TRUE");
                    return true;
                }
                if (up == "FALSE") {
                    out = Json::make_string("FALSE");
                    return true;
                }
            }
            err = "Value 应为 \"TRUE\" / \"FALSE\"，实际是 " + in.as_string("<非字符串>");
            return false;
        }
        case PType::String:
        case PType::StringEnum:
        case PType::Directory:
        case PType::Path: {
            if (in.is_string()) {
                out = Json::make_string(in.str);
                return true;
            }
            if (in.is_number() || in.is_bool()) {
                out  = Json::make_string(in.as_string());
                warn = std::string("Value 应是字符串，实际是 ") + in.type_name() + "，已转成文本";
                return true;
            }
            err = std::string("Value 应为字符串，实际是 ") + in.type_name();
            return false;
        }
        case PType::IntArray: {
            if (!in.is_array()) {
                err = std::string("Value 应为数组，实际是 ") + in.type_name();
                return false;
            }
            out = Json::make_array();
            for (const Json& e : in.arr) {
                Json     o;
                std::string w;
                if (!to_int_value(e, o, w)) {
                    err = std::string("数组含非整数元素：") + e.as_string(e.type_name());
                    return false;
                }
                if (!w.empty() && warn.empty()) warn = w;
                out.arr.push_back(std::move(o));
            }
            return true;
        }
        case PType::FloatArray: {
            if (!in.is_array()) {
                err = std::string("Value 应为数组，实际是 ") + in.type_name();
                return false;
            }
            out = Json::make_array();
            for (const Json& e : in.arr) {
                if (e.kind == Json::Kind::Double) {
                    out.arr.push_back(e);  // 原样保留字面量
                    continue;
                }
                Json sub;
                std::string w, er;
                if (!normalize_value(e, PType::Float, sub, w, er)) {
                    err = "数组含非浮点元素：" + e.as_string(e.type_name());
                    return false;
                }
                if (!w.empty() && warn.empty()) warn = w;
                out.arr.push_back(std::move(sub));
            }
            return true;
        }
        case PType::StringArray: {
            if (!in.is_array()) {
                err = std::string("Value 应为数组，实际是 ") + in.type_name();
                return false;
            }
            out = Json::make_array();
            for (const Json& e : in.arr) {
                if (e.is_string()) {
                    out.arr.push_back(Json::make_string(e.str));
                } else if (e.is_number() || e.is_bool()) {
                    out.arr.push_back(Json::make_string(e.as_string()));
                    if (warn.empty()) warn = "数组含非字符串元素，已转成文本";
                } else {
                    err = std::string("数组含非法元素类型：") + e.type_name();
                    return false;
                }
            }
            return true;
        }
        case PType::Unknown:
            break;
    }
    err = "未知的数据类型";
    return false;
}

// ------------------------------------------------------------------ 归一 Limit
// 返回 false 表示致命（该节点跳过）
bool parse_limit(const Json* lim, PType t, Param& p, const std::string& where,
                 std::vector<Diagnostic>& diags) {
    const bool missing = (lim == nullptr) || lim->is_null() ||
                         (lim->is_string() && lim->str.empty());

    switch (t) {
        case PType::Int:
        case PType::Float:
        case PType::IntArray:
        case PType::FloatArray: {
            if (missing) {
                add_diag(diags, false, where, "Limit 缺失或为空，不做范围校验");
                return true;
            }
            if (lim->is_array() && lim->arr.size() == 2 && lim->arr[0].is_number() &&
                lim->arr[1].is_number()) {
                p.has_range = true;
                p.range_min = lim->arr[0].as_double();
                p.range_max = lim->arr[1].as_double();
                if (p.range_min > p.range_max) {
                    std::swap(p.range_min, p.range_max);
                    add_diag(diags, false, where, "Limit 上下限颠倒，已自动交换");
                }
                return true;
            }
            add_diag(diags, false, where, "Limit 应为 [min, max]，当前格式已忽略（不做范围校验）");
            return true;
        }
        case PType::StringEnum:
        case PType::IntEnum:
        case PType::FloatEnum: {
            if (missing || !lim->is_array() || lim->arr.empty()) {
                add_diag(diags, true, where,
                         std::string(ptype_text(t)) + " 需要非空的 Limit 候选列表");
                return false;
            }
            for (const Json& item : lim->arr) {
                if (t == PType::StringEnum) {
                    if (item.is_string()) {
                        p.enum_texts.push_back(item.str);
                        p.enum_values.push_back(Json::make_string(item.str));
                    } else if (item.is_number() || item.is_bool()) {
                        p.enum_texts.push_back(item.as_string());
                        p.enum_values.push_back(Json::make_string(item.as_string()));
                        add_diag(diags, false, where, "string_enum 的 Limit 含非字符串项，已转成文本");
                    } else {
                        add_diag(diags, true, where, "string_enum 的 Limit 元素必须是字符串");
                        return false;
                    }
                } else if (t == PType::IntEnum) {
                    Json o;
                    std::string w;
                    if (!to_int_value(item, o, w)) {
                        add_diag(diags, true, where, "int_enum 的 Limit 元素必须是整数");
                        return false;
                    }
                    p.enum_texts.push_back(num_text(o));
                    p.enum_values.push_back(std::move(o));
                } else {  // FloatEnum
                    if (!item.is_number() && !item.is_string()) {
                        add_diag(diags, true, where, "float_enum 的 Limit 元素必须是数值");
                        return false;
                    }
                    double dv = 0.0;
                    if (!item.is_number() && !parse_double_text(item.str, dv)) {
                        add_diag(diags, true, where, "float_enum 的 Limit 元素必须是数值");
                        return false;
                    }
                    Json o = Json::make_double(item.is_number() ? item.as_double() : dv);
                    o.num_raw = item.is_number() ? item.num_raw : std::string();
                    p.enum_texts.push_back(num_text(o));
                    p.enum_values.push_back(std::move(o));
                }
            }
            return true;
        }
        default: {
            // bool / string / directory / path 不需要 Limit
            if (!missing) add_diag(diags, false, where, std::string(ptype_text(t)) + " 不需要 Limit，已忽略");
            return true;
        }
    }
}

bool enum_contains(const Param& p, const Json& v) {
    for (const Json& item : p.enum_values) {
        if (p.type == PType::StringEnum) {
            if (item.as_string() == v.as_string()) return true;
        } else if (p.type == PType::IntEnum) {
            if (item.as_int() == v.as_int()) return true;
        } else {
            if (item.as_double() == v.as_double()) return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------ 单个参数
void parse_param(const std::string& key, const Json& obj, const std::vector<std::string>& path,
                 Node& out, std::vector<Diagnostic>& diags) {
    const std::string where = path_breadcrumb(path);
    out.key                 = key;
    out.is_param            = true;
    out.ok                  = false;

    const Json* tj = obj.find("Type");
    if (!tj) {
        add_diag(diags, true, where, "缺少 Type 字段");
        return;
    }
    if (!tj->is_string()) {
        add_diag(diags, true, where, std::string("Type 必须是字符串，实际是 ") + tj->type_name());
        return;
    }
    const PType pt = ptype_from_text(tj->str);
    if (pt == PType::Unknown) {
        add_diag(diags, true, where, "未知的 Type: \"" + tj->str + "\"");
        return;
    }
    out.param.type_text = tj->str;
    out.param.type      = pt;

    const Json* cj = obj.find("Competence");
    if (!cj) {
        add_diag(diags, true, where, "缺少 Competence 字段");
        return;
    }
    if (!cj->is_number()) {
        add_diag(diags, true, where,
                 std::string("Competence 必须是整数，实际是 ") + cj->type_name());
        return;
    }
    out.param.competence = static_cast<int>(cj->as_int());

    if (!parse_limit(obj.find("Limit"), pt, out.param, where, diags)) return;

    const Json* vj = obj.find("Value");
    if (!vj) {
        add_diag(diags, true, where, "缺少 Value 字段");
        return;
    }
    std::string warn, err;
    if (!normalize_value(*vj, pt, out.param.value, warn, err)) {
        add_diag(diags, true, where, err);
        return;
    }
    if (!warn.empty()) add_diag(diags, false, where, warn);

    if (ptype_is_enum(pt) && !enum_contains(out.param, out.param.value))
        add_diag(diags, false, where, "当前值不在 Limit 候选列表中，请修正");

    out.ok = true;
}

// 缺少 Type、但里面又有 Competence / Limit / Value 这种标量字段的，
// 基本可以断定是“漏写了 Type 的参数”，而不是一个分组。
// 这时只报一条精准诊断，不再把它的标量成员当子节点逐个报错（避免噪声爆炸）。
bool looks_like_param_missing_type(const Json& obj, std::string& which_key) {
    static const char* kKeys[] = {"Competence", "Limit", "Value"};
    for (const char* k : kKeys) {
        const Json* v = obj.find(k);
        if (v != nullptr && !v->is_object()) {
            which_key = k;
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------ 递归建树
void build_group(const Json& obj, std::vector<std::string> path, std::vector<Node>& out,
                 std::vector<Diagnostic>& diags, int depth) {
    if (depth > kMaxDepth) {
        add_diag(diags, true, path_breadcrumb(path), "嵌套层级过深，已跳过");
        return;
    }
    for (const auto& kv : obj.obj) {
        std::vector<std::string> child_path = path;
        child_path.push_back(kv.first);

        Node n;
        if (!kv.second.is_object()) {
            add_diag(diags, true, path_breadcrumb(child_path),
                     std::string("期望是对象（分组或参数），实际是 ") + kv.second.type_name());
            n.key = kv.first;
            n.ok  = false;  // 不渲染
            out.push_back(std::move(n));
            continue;
        }
        if (kv.second.find("Type")) {
            parse_param(kv.first, kv.second, child_path, n, diags);
        } else {
            std::string hint;
            if (looks_like_param_missing_type(kv.second, hint)) {
                add_diag(diags, true, path_breadcrumb(child_path),
                         "缺少 Type 字段（含有 " + hint + "，应该是漏写了 Type 的参数）");
                n.key = kv.first;
                n.ok  = false;
                out.push_back(std::move(n));
                continue;
            }
            n.key      = kv.first;
            n.is_param = false;
            n.ok       = true;
            build_group(kv.second, child_path, n.children, diags, depth + 1);
        }
        out.push_back(std::move(n));
    }
}

// ------------------------------------------------------------------ 校验
void validate_node(const Node& n, const std::vector<std::string>& display_path, const Doc& doc,
                   std::vector<std::string>& errors, std::vector<std::string>& warnings) {
    std::vector<std::string> cur = display_path;
    cur.push_back(n.key);

    if (!n.ok) return;

    if (!n.is_param) {
        for (const Node& c : n.children) validate_node(c, cur, doc, errors, warnings);
        return;
    }

    // 只读节点不参与校验：用户没权限改，否则会永远存不下去
    if (!can_edit(n.param.competence, doc.user_competence)) return;

    const std::string where = path_breadcrumb(cur);
    const Param&      p     = n.param;

    const auto range_error = [&](const Json& v, const std::string& extra) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s：值 %s 超出允许范围 [%.10g, %.10g]%s", where.c_str(),
                      num_text(v).c_str(), p.range_min, p.range_max, extra.c_str());
        errors.push_back(buf);
    };

    switch (p.type) {
        case PType::Int: {
            if (!p.has_range) break;
            const double dv = static_cast<double>(p.value.as_int());
            if (dv < p.range_min || dv > p.range_max) range_error(p.value, "");
            break;
        }
        case PType::Float: {
            if (!p.has_range) break;
            const double dv = p.value.as_double();
            if (dv < p.range_min || dv > p.range_max) range_error(p.value, "");
            break;
        }
        case PType::IntArray:
        case PType::FloatArray: {
            if (p.has_range) {
                for (std::size_t k = 0; k < p.value.arr.size(); ++k) {
                    const Json&  e  = p.value.arr[k];
                    const double dv = (p.type == PType::IntArray)
                                          ? static_cast<double>(e.as_int())
                                          : e.as_double();
                    if (dv < p.range_min || dv > p.range_max)
                        range_error(e, "（第 " + std::to_string(k + 1) + " 个元素）");
                }
            }
            break;
        }
        case PType::StringEnum:
        case PType::IntEnum:
        case PType::FloatEnum: {
            if (!enum_contains(p, p.value)) {
                std::string list;
                for (std::size_t k = 0; k < p.enum_texts.size(); ++k) {
                    if (k) list += ", ";
                    list += p.enum_texts[k];
                }
                errors.push_back(where + "：值 " + num_text(p.value) + " 不在候选列表中 [" + list + "]");
            }
            break;
        }
        case PType::Directory:
        case PType::Path: {
            const std::string path_text = p.value.as_string();
            if (!path_text.empty()) {
                std::error_code ec;
                if (!std::filesystem::exists(std::filesystem::u8path(path_text), ec))
                    warnings.push_back(where + "：路径不存在：" + path_text + "（仅提示，不阻止保存）");
            }
            break;
        }
        default:
            break;  // bool / string / string[] 无内容约束
    }
}

// ------------------------------------------------------------------ 回写
void apply_nodes(Json& container, std::vector<Node>& nodes, int user_competence) {
    for (Node& n : nodes) {
        if (!n.ok) continue;
        Json* sub = container.find(n.key);
        if (!sub || !sub->is_object()) continue;
        if (!n.is_param) {
            apply_nodes(*sub, n.children, user_competence);
            continue;
        }
        if (!can_edit(n.param.competence, user_competence)) continue;  // 越权不写（需求 2.4）
        Json* v = sub->find("Value");
        if (v) *v = n.param.value;
    }
}

}  // namespace

// ============================================================================
bool doc_load(Doc& doc, const std::string& text, std::string& err) {
    doc.raw = Json();
    doc.roots.clear();
    doc.diags.clear();

    if (!Json::parse(text, doc.raw, err)) return false;
    if (!doc.raw.is_object()) {
        err = std::string("根节点必须是对象，实际是 ") + doc.raw.type_name();
        return false;
    }
    build_group(doc.raw, {}, doc.roots, doc.diags, 0);
    return true;
}

void doc_validate(const Doc& doc, std::vector<std::string>& errors,
                  std::vector<std::string>& warnings) {
    errors.clear();
    warnings.clear();
    for (const Node& n : doc.roots) validate_node(n, {}, doc, errors, warnings);
}

std::string doc_save(Doc& doc, int indent) {
    apply_nodes(doc.raw, doc.roots, doc.user_competence);
    return doc.raw.dump(indent);
}

}  // namespace parm_ui
