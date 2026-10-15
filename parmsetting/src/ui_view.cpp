#include "ui_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "fs_dialog.h"
#include "imgui.h"
#include "imgui_stdlib.h"

namespace parm_ui {
namespace ui {

namespace {

// 界面级共享状态（同一时刻只允许一个配置窗口，所以用静态变量是安全的）
FileDialogState g_dialog;
Json*           g_dialog_target = nullptr;

std::vector<std::string> g_errors;
std::vector<std::string> g_warnings;
bool                     g_show_errors   = false;
bool                     g_show_warnings = false;
bool                     g_show_diags    = false;
bool                     g_smoke_click_save = false;   // 测试钩子，见 smoke_click_save()

const float kInputWidth = 150.0f;

// ---------------------------------------------------------------- 小工具
const char* competence_role(int c) {
    switch (c) {
        case 0: return "操作员";
        case 1: return "产线工程师";
        case 2: return "系统工程师";
        case 3: return "管理员/开发";
        default: return "未知";
    }
}

// UTF-8 安全的截断（按码点边界，尾部补 "..."）
std::string ellipsize(const std::string& s, float max_w) {
    if (ImGui::CalcTextSize(s.c_str()).x <= max_w) return s;
    const float ell_w = ImGui::CalcTextSize("...").x;
    std::string out;
    std::size_t k = 0;
    while (k < s.size()) {
        std::size_t next = k + 1;
        while (next < s.size() && (static_cast<unsigned char>(s[next]) & 0xC0) == 0x80) ++next;
        std::string cand = s.substr(0, next);
        if (ImGui::CalcTextSize(cand.c_str()).x + ell_w > max_w) break;
        out = cand;
        k   = next;
    }
    return out + "...";
}

std::string range_text(const Param& p) {
    char buf[96];
    if (p.type == PType::Int || p.type == PType::IntArray)
        std::snprintf(buf, sizeof(buf), "[%.0f, %.0f]", p.range_min, p.range_max);
    else
        std::snprintf(buf, sizeof(buf), "[%.10g, %.10g]", p.range_min, p.range_max);
    return std::string(buf);
}

std::string value_display(const Param& p) {
    switch (p.type) {
        case PType::StringEnum:
        case PType::Bool:
        case PType::String:
        case PType::Directory:
        case PType::Path:
            return p.value.as_string();
        case PType::IntEnum:
            return std::to_string(p.value.as_int());
        case PType::FloatEnum: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.10g", p.value.as_double());
            return std::string(buf);
        }
        default:
            return p.value.dump(0);
    }
}

bool enum_matches(const Param& p, const Json& candidate, const Json& current) {
    if (p.type == PType::StringEnum) return candidate.as_string() == current.as_string();
    if (p.type == PType::IntEnum) return candidate.as_int() == current.as_int();
    return candidate.as_double() == current.as_double();
}

Json default_array_element(const Param& p) {
    switch (p.type) {
        case PType::IntArray:
            return Json::make_int(static_cast<std::int64_t>(std::llround(p.has_range ? p.range_min : 0.0)));
        case PType::FloatArray:
            return Json::make_double(p.has_range ? p.range_min : 0.0);
        default:
            return Json::make_string("");
    }
}

// 元素范围提示（数组用）
void draw_label_tooltip(const Node& n, const Doc& doc) {
    if (!ImGui::IsItemHovered()) return;
    const Param& p = n.param;
    ImGui::BeginTooltip();
    ImGui::Text("Type: %s", p.type_text.c_str());
    ImGui::Text("权限: %d (%s)%s", p.competence, competence_role(p.competence),
                can_edit(p.competence, doc.user_competence) ? "" : " —— 当前只读");
    switch (p.type) {
        case PType::Int:
        case PType::Float:
            ImGui::Text("取值范围: %s", p.has_range ? range_text(p).c_str() : "不限");
            break;
        case PType::IntArray:
        case PType::FloatArray:
            ImGui::Text("每个元素取值范围: %s", p.has_range ? range_text(p).c_str() : "不限");
            break;
        case PType::StringEnum:
        case PType::IntEnum:
        case PType::FloatEnum: {
            std::string list;
            for (std::size_t k = 0; k < p.enum_texts.size(); ++k) {
                if (k) list += ", ";
                list += p.enum_texts[k];
            }
            ImGui::TextWrapped("候选值: %s", list.c_str());
            break;
        }
        default:
            break;
    }
    ImGui::EndTooltip();
}

// ---------------------------------------------------------------- 各类型控件
// control_x：控件列的起始 x（保证数组多行对齐）
void draw_control(Node& n, const Doc& doc, float control_x) {
    Param& p = n.param;

    switch (p.type) {
        case PType::Int: {
            std::int64_t v = p.value.as_int();
            ImGui::SetNextItemWidth(kInputWidth);
            if (ImGui::InputScalar("##v", ImGuiDataType_S64, &v)) p.value = Json::make_int(v);
            if (p.has_range) {
                ImGui::SameLine();
                const std::string rt = range_text(p);
                const float       tw = ImGui::CalcTextSize(rt.c_str()).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
                std::int64_t      lo = static_cast<std::int64_t>(std::llround(p.range_min));
                std::int64_t      hi = static_cast<std::int64_t>(std::llround(p.range_max));
                ImGui::SetNextItemWidth(-tw);
                if (ImGui::SliderScalar("##s", ImGuiDataType_S64, &v, &lo, &hi))
                    p.value = Json::make_int(v);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", rt.c_str());
            }
            break;
        }
        case PType::Float: {
            double v = p.value.as_double();
            ImGui::SetNextItemWidth(kInputWidth);
            if (ImGui::InputScalar("##v", ImGuiDataType_Double, &v, nullptr, nullptr, "%.10g"))
                p.value = Json::make_double(v);
            if (p.has_range) {
                ImGui::SameLine();
                const std::string rt = range_text(p);
                const float       tw = ImGui::CalcTextSize(rt.c_str()).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
                ImGui::SetNextItemWidth(-tw);
                if (ImGui::SliderScalar("##s", ImGuiDataType_Double, &v, &p.range_min, &p.range_max, "%.6g"))
                    p.value = Json::make_double(v);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", rt.c_str());
            }
            break;
        }
        case PType::Bool: {
            bool b = (p.value.as_string() == "TRUE");
            if (ImGui::Checkbox("##v", &b)) p.value = Json::make_string(b ? "TRUE" : "FALSE");
            break;
        }
        case PType::String: {
            std::string s = p.value.as_string();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##v", &s)) p.value = Json::make_string(s);
            break;
        }
        case PType::StringEnum:
        case PType::IntEnum:
        case PType::FloatEnum: {
            std::vector<std::string> items = p.enum_texts;
            int                      cur   = -1;
            for (std::size_t k = 0; k < p.enum_values.size(); ++k)
                if (enum_matches(p, p.enum_values[k], p.value)) cur = static_cast<int>(k);
            bool extra = false;
            if (cur < 0) {  // 当前值不在候选里：补一项，避免静默改值
                items.push_back(value_display(p) + "   <- 不在候选列表中");
                cur   = static_cast<int>(items.size()) - 1;
                extra = true;
            }
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##v", items[static_cast<std::size_t>(cur)].c_str())) {
                for (std::size_t k = 0; k < items.size(); ++k) {
                    const bool selected = (static_cast<int>(k) == cur);
                    if (extra && static_cast<int>(k) == cur)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.35f, 1.0f));
                    if (ImGui::Selectable(items[k].c_str(), selected) && !(extra && static_cast<int>(k) == cur))
                        p.value = p.enum_values[k];
                    if (selected) ImGui::SetItemDefaultFocus();
                    if (extra && static_cast<int>(k) == cur) ImGui::PopStyleColor();
                }
                ImGui::EndCombo();
            }
            break;
        }
        case PType::IntArray:
        case PType::FloatArray:
        case PType::StringArray: {
            Json& a = p.value;
            if (!a.is_array()) a = Json::make_array();

            int erase_index = -1;
            for (std::size_t k = 0; k < a.arr.size(); ++k) {
                if (k > 0) ImGui::SetCursorPosX(control_x);
                ImGui::PushID(static_cast<int>(k));
                if (p.type == PType::StringArray) {
                    std::string s = a.arr[k].as_string();
                    ImGui::SetNextItemWidth(kInputWidth);
                    if (ImGui::InputText("##e", &s)) a.arr[k] = Json::make_string(s);
                } else if (p.type == PType::IntArray) {
                    std::int64_t v = a.arr[k].as_int();
                    ImGui::SetNextItemWidth(kInputWidth);
                    if (ImGui::InputScalar("##e", ImGuiDataType_S64, &v)) a.arr[k] = Json::make_int(v);
                } else {
                    double v = a.arr[k].as_double();
                    ImGui::SetNextItemWidth(kInputWidth);
                    if (ImGui::InputScalar("##e", ImGuiDataType_Double, &v, nullptr, nullptr, "%.10g"))
                        a.arr[k] = Json::make_double(v);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("-")) erase_index = static_cast<int>(k);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("删除第 %d 项", static_cast<int>(k) + 1);
                ImGui::PopID();
            }
            if (erase_index >= 0) {
                a.arr.erase(a.arr.begin() + erase_index);
                return;  // 本帧不再画追加按钮，避免索引错乱
            }

            ImGui::SetCursorPosX(control_x);
            if (ImGui::SmallButton("+ 添加")) a.arr.push_back(default_array_element(p));
            if (p.has_range) {
                ImGui::SameLine();
                ImGui::TextDisabled("每项范围 %s", range_text(p).c_str());
            }
            break;
        }
        case PType::Directory:
        case PType::Path: {
            std::string s = p.value.as_string();
            ImGui::SetNextItemWidth(-120.0f);
            if (ImGui::InputText("##v", &s)) p.value = Json::make_string(s);
            ImGui::SameLine();
            const char* label = (p.type == PType::Directory) ? "浏览目录..." : "浏览文件...";
            if (ImGui::Button(label, ImVec2(110.0f, 0.0f))) {
                open_file_dialog(g_dialog, p.type == PType::Directory, n.key, s);
                g_dialog_target = &p.value;
            }
            break;
        }
        case PType::Unknown:
            break;
    }
}

// 一行 = 标签列 + 控件列
void draw_param_row(Node& n, const Doc& doc, float label_w) {
    const bool editable = can_edit(n.param.competence, doc.user_competence);
    if (!editable) ImGui::BeginDisabled();

    ImGui::AlignTextToFramePadding();
    const std::string label = ellipsize(n.key, label_w - 80.0f);
    ImGui::TextUnformatted(label.c_str());
    draw_label_tooltip(n, doc);

    if (!editable) {
        // 标记放在标签后面：整行宽度的控件（下拉框 / 文本框）之后会没有位置显示它
        ImGui::SameLine();
        ImGui::TextDisabled("[只读:%d]", n.param.competence);
    }

    ImGui::SameLine(label_w);
    draw_control(n, doc, label_w);

    if (!editable) ImGui::EndDisabled();
}

// 该节点（含子孙）是否有东西可画：解析失败的节点以及只含失败子节点的分组都不画
bool renderable(const Node& n) {
    if (!n.ok) return false;
    if (n.is_param) return true;
    for (const Node& c : n.children)
        if (renderable(c)) return true;
    return false;
}

void draw_node(Node& n, const Doc& doc, float label_w, int depth) {
    if (!renderable(n)) return;

    if (!n.is_param) {
        ImGui::PushID(n.key.c_str());
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (depth < 2) flags |= ImGuiTreeNodeFlags_DefaultOpen;  // 默认展开前两层
        const bool open = ImGui::TreeNodeEx("##g", flags, "%s", n.key.c_str());
        ImGui::PopID();
        if (open) {
            for (Node& c : n.children) draw_node(c, doc, label_w, depth + 1);
            ImGui::TreePop();
        }
        return;
    }

    ImGui::PushID(n.key.c_str());
    draw_param_row(n, doc, label_w);
    ImGui::PopID();
}

// ---------------------------------------------------------------- 弹窗
void draw_diag_popup(const Doc& doc) {
    if (g_show_diags) {
        ImGui::OpenPopup("解析诊断##diag");
        g_show_diags = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720, 420), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("解析诊断##diag", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("共 %d 条：%d 个节点已跳过，%d 条提示", static_cast<int>(doc.diags.size()),
                    doc.count_fatal(), doc.count_warning());
        ImGui::Separator();
        ImGui::BeginChild("##list", ImVec2(0, -32.0f), true);
        for (const Diagnostic& d : doc.diags) {
            if (d.fatal)
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "[跳过] %s", d.where.c_str());
            else
                ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.35f, 1.0f), "[提示] %s", d.where.c_str());
            ImGui::TextWrapped("        %s", d.message.c_str());
        }
        ImGui::EndChild();
        if (ImGui::Button("关闭", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void draw_errors_popup(bool& save_out) {
    if (g_show_errors) {
        ImGui::OpenPopup("无法保存##err");
        g_show_errors = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720, 420), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("无法保存##err", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "有 %d 处校验不通过，请修正后再保存：",
                           static_cast<int>(g_errors.size()));
        ImGui::Separator();
        ImGui::BeginChild("##errlist", ImVec2(0, -32.0f), true);
        for (const std::string& e : g_errors) ImGui::TextWrapped("- %s", e.c_str());
        ImGui::EndChild();
        if (ImGui::Button("返回修改", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    (void)save_out;
}

void draw_warnings_popup(bool& save_out) {
    if (g_show_warnings) {
        ImGui::OpenPopup("保存确认##warn");
        g_show_warnings = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720, 380), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("保存确认##warn", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.35f, 1.0f), "有 %d 条提示（不阻止保存）：",
                           static_cast<int>(g_warnings.size()));
        ImGui::Separator();
        ImGui::BeginChild("##warnlist", ImVec2(0, -32.0f), true);
        for (const std::string& w : g_warnings) ImGui::TextWrapped("- %s", w.c_str());
        ImGui::EndChild();
        if (ImGui::Button("仍然保存", ImVec2(120, 0))) {
            save_out = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("返回修改", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace

void smoke_click_save(bool v) { g_smoke_click_save = v; }

// ============================================================================
ViewResult draw_main_window(Doc& doc, bool font_ok) {
    ViewResult r;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("##parm_ui_main", nullptr, flags);

    // ---------------------------------------------------------- 顶部工具条
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("参数配置");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "当前权限等级 %d (%s)",
                       doc.user_competence, competence_role(doc.user_competence));

    {
        const float bw      = 130.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(ImGui::GetWindowWidth() - 2.0f * bw - spacing - 20.0f);
        if (ImGui::Button("保存并关闭", ImVec2(bw, 0)) || g_smoke_click_save) {
            g_smoke_click_save = false;  // 模拟点击只生效一次
            doc_validate(doc, g_errors, g_warnings);
            if (!g_errors.empty())
                g_show_errors = true;
            else if (!g_warnings.empty())
                g_show_warnings = true;
            else
                r.save = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(bw, 0))) r.cancel = true;
    }

    // ---------------------------------------------------------- 状态提示
    const int n_fatal = doc.count_fatal();
    if (n_fatal > 0 || doc.count_warning() > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, n_fatal > 0 ? ImVec4(1.0f, 0.6f, 0.4f, 1.0f)
                                                         : ImVec4(0.95f, 0.8f, 0.35f, 1.0f));
        ImGui::Text("[!] 解析诊断：%d 个节点已跳过，%d 条提示", n_fatal, doc.count_warning());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("查看详情")) g_show_diags = true;
    }
    if (!font_ok) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.4f, 1.0f));
        ImGui::TextWrapped("[!] 未找到可用的中文字体，中文可能显示为 '?'。"
                           "可设置环境变量 PARM_UI_FONT 指向字体文件（如 msyh.ttc / NotoSansCJK-Regular.ttc）。");
        ImGui::PopStyleColor();
    }
    ImGui::Separator();

    // ---------------------------------------------------------- 参数树
    const float label_w = std::max(220.0f, ImGui::GetWindowWidth() * 0.34f);
    ImGui::BeginChild("##tree", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (Node& n : doc.roots) draw_node(n, doc, label_w, 0);
    ImGui::EndChild();

    // ---------------------------------------------------------- 文件对话框
    if (draw_file_dialog(g_dialog) && g_dialog_target != nullptr) {
        *g_dialog_target = Json::make_string(g_dialog.result);
        g_dialog_target  = nullptr;
    }

    // ---------------------------------------------------------- 弹窗
    bool save_from_popup = false;
    draw_errors_popup(save_from_popup);
    draw_warnings_popup(save_from_popup);
    if (save_from_popup) r.save = true;
    draw_diag_popup(doc);

    ImGui::End();
    return r;
}

}  // namespace ui
}  // namespace parm_ui
