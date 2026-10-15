#include "ordered_json.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace parm_ui {

namespace {

// 用 nlohmann 的 SAX 事件流建一棵保序 DOM
class DomSax : public nlohmann::json::json_sax_t {
public:
    Json        root;
    bool        has_root = false;
    bool        failed   = false;
    std::string error;

    bool null() override { return emit(Json::make_null()); }
    bool boolean(bool val) override { return emit(Json::make_bool(val)); }
    bool number_integer(number_integer_t val) override { return emit(Json::make_int(val)); }
    bool number_unsigned(number_unsigned_t val) override {
        if (val <= static_cast<number_unsigned_t>(std::numeric_limits<std::int64_t>::max()))
            return emit(Json::make_int(static_cast<std::int64_t>(val)));
        return emit(Json::make_uint(val));
    }
    bool number_float(number_float_t val, const string_t& s) override {
        Json j = Json::make_double(val);
        j.num_raw = s;  // 原样保留字面量（1.0e5 / 0.500 这类不被重新格式化）
        return emit(std::move(j));
    }
    bool string(string_t& val) override { return emit(Json::make_string(val)); }
    bool start_object(std::size_t) override { return push_frame(true); }
    bool key(string_t& val) override { pending_key_ = val; has_pending_key_ = true; return true; }
    bool end_object() override { return pop_frame(); }
    bool start_array(std::size_t) override { return push_frame(false); }
    bool end_array() override { return pop_frame(); }

    // nlohmann 在 sax_parse 里通过这个回调报语法错误（不抛异常）
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception& ex) override {
        error  = ex.what();
        failed = true;
        return false;
    }

private:
    struct Frame {
        Json        node;
        std::string key;      // 该容器在父对象里的键
        bool        has_key = false;
    };

    bool emit(Json v) {
        if (frames_.empty()) {
            root     = std::move(v);
            has_root = true;
            return true;
        }
        Frame& top = frames_.back();
        if (top.node.is_object()) {
            top.node.obj.emplace_back(has_pending_key_ ? pending_key_ : std::string(), std::move(v));
            pending_key_.clear();
            has_pending_key_ = false;
        } else {
            top.node.arr.push_back(std::move(v));
        }
        return true;
    }

    bool push_frame(bool is_obj) {
        Frame f;
        f.node = is_obj ? Json::make_object() : Json::make_array();
        if (has_pending_key_) {
            f.key            = pending_key_;
            f.has_key        = true;
            pending_key_.clear();
            has_pending_key_ = false;
        }
        frames_.push_back(std::move(f));
        return true;
    }

    bool pop_frame() {
        if (frames_.empty()) {
            error  = "内部解析状态异常";
            failed = true;
            return false;
        }
        Frame f = std::move(frames_.back());
        frames_.pop_back();
        if (frames_.empty()) {
            root     = std::move(f.node);
            has_root = true;
            return true;
        }
        Frame& top = frames_.back();
        if (top.node.is_object())
            top.node.obj.emplace_back(f.has_key ? f.key : std::string(), std::move(f.node));
        else
            top.node.arr.push_back(std::move(f.node));
        return true;
    }

    std::vector<Frame> frames_;
    std::string        pending_key_;
    bool               has_pending_key_ = false;
};

bool parse_integer_text(const std::string& s, std::int64_t& out) {
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

}  // namespace

const char* Json::type_name() const {
    switch (kind) {
        case Kind::Null:   return "null";
        case Kind::Bool:   return "bool";
        case Kind::Int:
        case Kind::UInt:
        case Kind::Double: return "number";
        case Kind::String: return "string";
        case Kind::Array:  return "array";
        case Kind::Object: return "object";
    }
    return "unknown";
}

std::size_t Json::size() const {
    if (kind == Kind::Array) return arr.size();
    if (kind == Kind::Object) return obj.size();
    return 0;
}

bool Json::as_bool(bool def) const {
    if (kind == Kind::Bool) return b;
    if (kind == Kind::Int) return i != 0;
    if (kind == Kind::UInt) return u != 0;
    if (kind == Kind::Double) return d != 0.0;
    return def;
}

std::int64_t Json::as_int(std::int64_t def) const {
    switch (kind) {
        case Kind::Int:    return i;
        case Kind::UInt:   return (u > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                                      ? def
                                      : static_cast<std::int64_t>(u);
        case Kind::Double: return static_cast<std::int64_t>(d);
        case Kind::String: { std::int64_t v = 0; return parse_integer_text(str, v) ? v : def; }
        case Kind::Bool:   return b ? 1 : 0;
        default:           return def;
    }
}

double Json::as_double(double def) const {
    switch (kind) {
        case Kind::Int:    return static_cast<double>(i);
        case Kind::UInt:   return static_cast<double>(u);
        case Kind::Double: return d;
        case Kind::String: { double v = 0.0; return parse_double_text(str, v) ? v : def; }
        case Kind::Bool:   return b ? 1.0 : 0.0;
        default:           return def;
    }
}

std::string Json::as_string(const std::string& def) const {
    switch (kind) {
        case Kind::String: return str;
        case Kind::Bool:   return b ? "true" : "false";
        case Kind::Int:    return std::to_string(i);
        case Kind::UInt:   return std::to_string(u);
        case Kind::Double:
            if (!num_raw.empty()) return num_raw;
            return nlohmann::json(d).dump();
        default: return def;
    }
}

const Json* Json::find(const std::string& key) const {
    const Json* found = nullptr;
    for (const auto& kv : obj)
        if (kv.first == key) found = &kv.second;  // 重复键取最后一个（与 nlohmann 一致）
    return found;
}

Json* Json::find(const std::string& key) {
    Json* found = nullptr;
    for (auto& kv : obj)
        if (kv.first == key) found = &kv.second;
    return found;
}

const Json* Json::at(std::size_t idx) const { return idx < arr.size() ? &arr[idx] : nullptr; }
Json*       Json::at(std::size_t idx) { return idx < arr.size() ? &arr[idx] : nullptr; }

bool Json::parse(const std::string& text, Json& out, std::string& err) {
    std::string s = text;
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF)
        s.erase(0, 3);  // 去 UTF-8 BOM

    if (s.empty()) {
        err = "输入为空";
        return false;
    }

    DomSax sax;
    bool   ok = false;
    try {
        ok = nlohmann::json::sax_parse(s.begin(), s.end(), &sax);
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
    if (!ok || sax.failed || !sax.has_root) {
        err = sax.error.empty() ? std::string("JSON 解析失败") : sax.error;
        return false;
    }
    out = std::move(sax.root);
    return true;
}

void Json::dump_into(std::string& out, int indent, int depth) const {
    const auto newline_indent = [&out, indent](int d) {
        if (indent > 0) {
            out += '\n';
            out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(d), ' ');
        }
    };

    switch (kind) {
        case Kind::Null: out += "null"; break;
        case Kind::Bool: out += (b ? "true" : "false"); break;
        case Kind::Int:  out += std::to_string(i); break;
        case Kind::UInt: out += std::to_string(u); break;
        case Kind::Double:
            out += num_raw.empty() ? nlohmann::json(d).dump() : num_raw;
            break;
        case Kind::String:
            out += nlohmann::json(str).dump();  // 复用 nlohmann 的转义（含 \uXXXX）
            break;
        case Kind::Array: {
            if (arr.empty()) { out += "[]"; break; }
            out += '[';
            for (std::size_t k = 0; k < arr.size(); ++k) {
                if (k) out += ',';
                newline_indent(depth + 1);
                arr[k].dump_into(out, indent, depth + 1);
            }
            newline_indent(depth);
            out += ']';
            break;
        }
        case Kind::Object: {
            if (obj.empty()) { out += "{}"; break; }
            out += '{';
            for (std::size_t k = 0; k < obj.size(); ++k) {
                if (k) out += ',';
                newline_indent(depth + 1);
                out += nlohmann::json(obj[k].first).dump();
                out += indent > 0 ? ": " : ":";
                obj[k].second.dump_into(out, indent, depth + 1);
            }
            newline_indent(depth);
            out += '}';
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    out.reserve(256);
    dump_into(out, indent, 0);
    return out;
}

bool json_equal(const Json& a, const Json& b) {
    // 数字：整数之间按整数精确比，其它情况按 double 比
    if (a.is_number() && b.is_number()) {
        const bool a_int = (a.kind == Json::Kind::Int || a.kind == Json::Kind::UInt);
        const bool b_int = (b.kind == Json::Kind::Int || b.kind == Json::Kind::UInt);
        if (a_int && b_int) return a.as_int() == b.as_int();
        return a.as_double() == b.as_double();
    }
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case Json::Kind::Null:   return true;
        case Json::Kind::Bool:   return a.b == b.b;
        case Json::Kind::Int:    return a.i == b.i;
        case Json::Kind::UInt:   return a.u == b.u;
        case Json::Kind::Double: return a.d == b.d;
        case Json::Kind::String: return a.str == b.str;
        case Json::Kind::Array: {
            if (a.arr.size() != b.arr.size()) return false;
            for (std::size_t k = 0; k < a.arr.size(); ++k)
                if (!json_equal(a.arr[k], b.arr[k])) return false;
            return true;
        }
        case Json::Kind::Object: {
            if (a.obj.size() != b.obj.size()) return false;
            for (std::size_t k = 0; k < a.obj.size(); ++k) {
                if (a.obj[k].first != b.obj[k].first) return false;  // 顺序也算
                if (!json_equal(a.obj[k].second, b.obj[k].second)) return false;
            }
            return true;
        }
    }
    return false;
}

void collect_key_paths(const Json& j, const std::string& prefix, std::vector<std::string>& out) {
    if (j.is_object()) {
        for (const auto& kv : j.obj) {
            const std::string p = prefix + "/" + kv.first;
            out.push_back(p);
            collect_key_paths(kv.second, p, out);
        }
    } else if (j.is_array()) {
        for (std::size_t k = 0; k < j.arr.size(); ++k)
            collect_key_paths(j.arr[k], prefix + "/[" + std::to_string(k) + "]", out);
    }
}

}  // namespace parm_ui
