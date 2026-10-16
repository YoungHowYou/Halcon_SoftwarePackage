#pragma once
// =============================================================================
// ordered_json.h —— 保序 JSON DOM
//
// 为什么需要它：
//   需求 2.1 要求“保存时保留原始字段顺序”。工作区根目录 HDict/json.hpp 里的
//   nlohmann/json 是 3.6.1，这个版本还没有 nlohmann::ordered_json（3.9.0 才引入），
//   它的默认对象类型是 std::map<std::string, json>，序列化后键会被按字典序重排。
//   为了不改动三方库文件，这里不改它，而是用 nlohmann 提供的 SAX 解析回调
//   自己建一棵 DOM：键顺序、未知字段、数字字面量都能原样保留。
//
// 特点：
//   * 解析仍交给 nlohmann（完整 JSON 语法 + 专业错误信息），不自己写词法分析
//   * 对象成员按出现顺序保存；重复键都保留，find() 返回最后一个（与 nlohmann 一致）
//   * 数字保真：整数走 int64/uint64 精确打印；浮点保留原文（SAX 会给出原始字面量），
//     只有被 UI 改过的浮点才会重新格式化
//   * 字符串转义复用 nlohmann 的 dump（含 \uXXXX、代理对处理），不自造转义表
// =============================================================================

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// json.hpp 由 CMake 指向工作区根目录的 HDict/（见 CMakeLists 里 parm_ui_model 的 include 路径），
// 用宏统一入口方便以后换版本
#ifndef PARM_UI_JSON_HEADER
#define PARM_UI_JSON_HEADER "json.hpp"
#endif
#include PARM_UI_JSON_HEADER

namespace parm_ui {

class Json {
public:
    enum class Kind { Null, Bool, Int, UInt, Double, String, Array, Object };

    Kind        kind = Kind::Null;
    bool        b    = false;   // Kind::Bool
    std::int64_t  i  = 0;       // Kind::Int
    std::uint64_t u  = 0;       // Kind::UInt
    double      d    = 0.0;     // Kind::Double
    std::string num_raw;        // Kind::Double 的原始字面量（有则原样输出）
    std::string str;            // Kind::String
    std::vector<Json> arr;                                    // Kind::Array
    std::vector<std::pair<std::string, Json>> obj;            // Kind::Object

    // ------------------------------------------------------------ 构造快捷方式
    static Json make_null() { return Json(); }
    static Json make_bool(bool v) { Json j; j.kind = Kind::Bool; j.b = v; return j; }
    static Json make_int(std::int64_t v) { Json j; j.kind = Kind::Int; j.i = v; return j; }
    static Json make_uint(std::uint64_t v) { Json j; j.kind = Kind::UInt; j.u = v; return j; }
    static Json make_double(double v) { Json j; j.kind = Kind::Double; j.d = v; return j; }
    static Json make_string(std::string v) { Json j; j.kind = Kind::String; j.str = std::move(v); return j; }
    static Json make_array() { Json j; j.kind = Kind::Array; return j; }
    static Json make_object() { Json j; j.kind = Kind::Object; return j; }

    // ------------------------------------------------------------ 判定
    bool is_null()   const { return kind == Kind::Null; }
    bool is_bool()   const { return kind == Kind::Bool; }
    bool is_number() const { return kind == Kind::Int || kind == Kind::UInt || kind == Kind::Double; }
    bool is_string() const { return kind == Kind::String; }
    bool is_array()  const { return kind == Kind::Array; }
    bool is_object() const { return kind == Kind::Object; }

    // 类型名（用于诊断信息）：null / bool / number / string / array / object
    const char* type_name() const;

    // 数组 / 对象元素个数；其它类型返回 0
    std::size_t size() const;

    // ------------------------------------------------------------ 取值（不抛异常）
    bool        as_bool(bool def = false) const;
    std::int64_t  as_int(std::int64_t def = 0) const;      // 数值直接取整；字符串尝试解析，失败返回 def
    double      as_double(double def = 0.0) const;
    std::string as_string(const std::string& def = std::string()) const;  // 数值 / 布尔转文本

    // 对象成员查找；不存在返回 nullptr；重复键返回最后一个
    const Json* find(const std::string& key) const;
    Json*       find(const std::string& key);

    // 数组下标（越界返回 nullptr）
    const Json* at(std::size_t idx) const;
    Json*       at(std::size_t idx);

    // ------------------------------------------------------------ 解析 / 序列化
    // 解析失败返回 false，err 填 nlohmann 的错误描述
    static bool parse(const std::string& text, Json& out, std::string& err);

    // indent > 0 缩进（默认 2 空格）；indent == 0 紧凑输出
    std::string dump(int indent = 2) const;

private:
    void dump_into(std::string& out, int indent, int depth) const;
};

// 结构相等（忽略浮点原始字面量差异，按数值比较）
bool json_equal(const Json& a, const Json& b);

// 收集所有“对象键路径”（用于校验字段顺序是否保真），按出现顺序
void collect_key_paths(const Json& j, const std::string& prefix,
                       std::vector<std::string>& out);

}  // namespace parm_ui
