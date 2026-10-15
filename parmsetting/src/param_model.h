#pragma once
// =============================================================================
// param_model.h —— 参数模型（纯数据层，不依赖 GUI，可单独单测）
//
//   解析规则（需求 1.1）：
//     节点含 "Type"  -> 末端参数（必须同时含 Competence / Limit / Value）
//     节点不含 Type  -> 分组，继续递归
//   健壮性（需求 3）：
//     单个节点解析失败只记 Diagnostic 并跳过，不影响整棵树；只有 JSON 语法错误
//     或根不是对象才算整体失败。
//   保真（需求 2.1）：
//     原始树 Json raw 原样保留（键顺序 + 未知字段），保存时只把编辑过的 Value
//     写回去，不做任何结构重排。
// =============================================================================

#include <string>
#include <vector>

#include "ordered_json.h"

namespace parm_ui {

// ------------------------------------------------------------------ 数据类型
enum class PType {
    Unknown,
    Int,
    Float,
    Bool,
    String,
    StringEnum,
    IntEnum,
    FloatEnum,
    IntArray,
    FloatArray,
    StringArray,
    Directory,
    Path
};

const char* ptype_text(PType t);                      // 回写 JSON 用的 Type 文本
PType       ptype_from_text(const std::string& s);    // 未知返回 Unknown
bool        ptype_is_array(PType t);
bool        ptype_is_enum(PType t);
bool        ptype_is_numeric(PType t);

// ------------------------------------------------------------------ 诊断
struct Diagnostic {
    bool        fatal = false;   // true = 该节点被跳过（整棵树仍会加载）
    std::string where;           // 面包屑路径，如 "Check parameters / backscratch / 面积筛选"
    std::string message;
};

// ------------------------------------------------------------------ 末端参数
struct Param {
    std::string type_text;
    PType       type       = PType::Unknown;
    int         competence = 0;

    // int / float / int[] / float[] 的 [min, max]；has_range=false 表示不限
    bool   has_range = false;
    double range_min = 0.0;
    double range_max = 0.0;

    // 三种枚举的候选列表：显示文本 + 原值
    std::vector<std::string> enum_texts;
    std::vector<Json>        enum_values;

    Json value;   // 当前编辑值（已按 Type 归一）
};

// ------------------------------------------------------------------ 树节点
struct Node {
    std::string key;
    bool        is_param = false;
    bool        ok       = true;   // 解析成功才渲染 / 才回写
    Param       param;
    std::vector<Node> children;    // 分组
};

// ------------------------------------------------------------------ 文档
struct Doc {
    Json                     raw;             // 原始树（保序 + 未知字段原样）
    std::vector<Node>        roots;
    std::vector<Diagnostic>  diags;
    int                      user_competence = 0;

    int count_fatal() const;
    int count_warning() const;
};

// 解析 + 建树。返回 false 仅当 JSON 语法错误 / 根不是对象（err 填原因）
bool doc_load(Doc& doc, const std::string& text, std::string& err);

// 保存前校验：errors 阻断保存，warnings 只提示（需求 2.3）
void doc_validate(const Doc& doc,
                  std::vector<std::string>& errors,
                  std::vector<std::string>& warnings);

// 把编辑值写回 raw 并序列化（越权节点不写回，需求 2.4）
std::string doc_save(Doc& doc, int indent = 2);

// 权限判定：节点等级 <= 用户等级才可编辑
inline bool can_edit(int node_competence, int user_competence) {
    return node_competence <= user_competence;
}

// 路径面包屑（"A / B / C"）
std::string path_breadcrumb(const std::vector<std::string>& path);

}  // namespace parm_ui
