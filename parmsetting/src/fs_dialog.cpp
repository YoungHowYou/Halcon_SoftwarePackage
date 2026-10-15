#include "fs_dialog.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "imgui.h"
#include "imgui_stdlib.h"

namespace parm_ui {
namespace ui {

namespace {

const char* kPopupId = "##parm_ui_fs_dialog";

// 拼路径：current 为空表示“驱动器/根列表”
std::string join_path(const std::string& dir, const std::string& name) {
    if (dir.empty()) {
        // 驱动器列表里选中 "C:" -> "C:/"
        if (name.size() >= 1 && name.back() == ':') return name + "/";
        if (name == "/") return "/";
        return name;
    }
    std::string d = dir;
    if (d.back() != '/' && d.back() != '\\') d += '/';
    return d + name;
}

// 上级目录；返回空表示到顶（驱动器列表）
std::string parent_of(const std::string& dir) {
    if (dir.empty()) return std::string();
    std::filesystem::path p = std::filesystem::u8path(dir);
    std::filesystem::path parent = p.parent_path();
    if (parent.empty()) return std::string();
    return parent.generic_u8string();
}

// 结果尽量给**相对路径**（相对进程当前工作目录）；
// 本来就算相对路径就原样保留，跑到工作目录外面才用绝对路径。
std::string prefer_relative(const std::string& path) {
    std::error_code           ec;
    const std::filesystem::path p = std::filesystem::u8path(path);
    if (!p.is_absolute()) return path;

    const std::filesystem::path base = std::filesystem::current_path(ec);
    if (ec) return path;

    const std::filesystem::path rel = std::filesystem::relative(p, base, ec);
    if (ec || rel.empty()) return path;

    const std::string s = rel.generic_u8string();
    if (s.rfind("..", 0) == 0) return path;  // 在工作目录之外，绝对路径更清楚
    return s;
}

void list_dir(FileDialogState& st) {
    st.names.clear();
    st.is_dir.clear();
    st.error.clear();
    st.selected = -1;

    std::error_code ec;

    if (st.current.empty()) {
#if defined(_WIN32)
        for (char c = 'A'; c <= 'Z'; ++c) {
            const std::string d = std::string(1, c) + ":/";
            if (std::filesystem::exists(std::filesystem::u8path(d), ec)) {
                st.names.push_back(std::string(1, c) + ":");
                st.is_dir.push_back(true);
            }
        }
#else
        st.names.push_back("/");
        st.is_dir.push_back(true);
#endif
        st.dirty = false;
        return;
    }

    std::filesystem::directory_iterator it(std::filesystem::u8path(st.current), ec);
    if (ec) {
        st.error = "无法读取目录：" + st.current;
        st.dirty  = false;
        return;
    }
    for (const auto& entry : it) {
        std::error_code ec2;
        const bool      dir  = entry.is_directory(ec2);
        std::string     name = entry.path().filename().u8string();
        if (name.empty() || name[0] == '.') continue;  // 跳过隐藏项
        st.names.push_back(std::move(name));
        st.is_dir.push_back(dir);
    }

    // 目录在前，同类按名字排序
    std::vector<std::size_t> idx(st.names.size());
    for (std::size_t k = 0; k < idx.size(); ++k) idx[k] = k;
    std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
        const bool da = static_cast<bool>(st.is_dir[a]);
        const bool db = static_cast<bool>(st.is_dir[b]);
        if (da != db) return da;   // 目录排在文件前面
        return st.names[a] < st.names[b];
    });
    std::vector<std::string> n2;
    std::vector<bool>        d2;
    n2.reserve(idx.size());
    d2.reserve(idx.size());
    for (std::size_t k : idx) {
        n2.push_back(st.names[k]);
        d2.push_back(st.is_dir[k]);
    }
    st.names.swap(n2);
    st.is_dir.swap(d2);

    st.dirty = false;
}

}  // namespace

void open_file_dialog(FileDialogState& st, bool dir_mode, const std::string& title,
                      const std::string& initial) {
    st.open      = true;
    st.just_open = true;
    st.dir_mode  = dir_mode;
    st.title     = title;
    st.result.clear();
    st.has_result = false;
    st.filename.clear();
    st.selected = -1;

    std::error_code ec;
    std::filesystem::path p = std::filesystem::u8path(initial);
    if (!initial.empty()) {
        if (std::filesystem::is_directory(p, ec)) {
            st.current = p.generic_u8string();
        } else {
            st.current  = p.parent_path().generic_u8string();
            st.filename = p.filename().u8string();
        }
    }
    if (st.current.empty()) {
        std::error_code ec2;
        st.current = std::filesystem::current_path(ec2).generic_u8string();
    }
    st.dirty = true;
}

bool draw_file_dialog(FileDialogState& st) {
    if (!st.open) return false;

    if (st.just_open) {
        ImGui::OpenPopup(kPopupId);
        st.just_open = false;
    }

    bool produced = false;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(680, 460), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted(st.title.empty()
                                   ? (st.dir_mode ? "选择目录" : "选择文件")
                                   : st.title.c_str());
        ImGui::Separator();

        // ---- 路径栏
        ImGui::SetNextItemWidth(-260.0f);
        if (ImGui::InputText("##dir", &st.current, ImGuiInputTextFlags_EnterReturnsTrue))
            st.dirty = true;
        ImGui::SameLine();
        if (ImGui::Button("上级目录")) {
            st.current = parent_of(st.current);
            st.dirty   = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("刷新")) st.dirty = true;

        if (st.dirty) list_dir(st);

        // ---- 目录/文件列表
        ImGui::BeginChild("##list", ImVec2(0, -60.0f), true);
        for (std::size_t k = 0; k < st.names.size(); ++k) {
            const bool        is_dir = st.is_dir[k];
            const std::string label  = (is_dir ? "[目录] " : "       ") + st.names[k];
            const bool        sel    = (static_cast<int>(k) == st.selected);
            if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                st.selected = static_cast<int>(k);
                if (!is_dir) st.filename = st.names[k];
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (is_dir) {
                        st.current = join_path(st.current, st.names[k]);
                        st.dirty   = true;
                    }
                }
            }
        }
        ImGui::EndChild();

        if (!st.error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", st.error.c_str());

        // ---- 文件模式：文件名可手输
        if (!st.dir_mode) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##file", &st.filename);
        } else {
            ImGui::TextDisabled("双击列表进入子目录，或直接点“选择当前目录”");
        }

        // ---- 按钮
        const char* ok_label = st.dir_mode ? "选择当前目录" : "确定";
        if (ImGui::Button(ok_label, ImVec2(130, 0))) {
            if (st.dir_mode) {
                if (!st.current.empty()) {
                    st.result     = prefer_relative(st.current);
                    st.has_result = true;
                    produced      = true;
                    ImGui::CloseCurrentPopup();
                    st.open = false;
                }
            } else {
                if (!st.filename.empty()) {
                    const std::string full = join_path(st.current, st.filename);
                    st.result              = prefer_relative(full);
                    st.has_result          = true;
                    produced               = true;
                    ImGui::CloseCurrentPopup();
                    st.open = false;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
            st.open = false;
        }

        ImGui::EndPopup();
    }

    return produced;
}

}  // namespace ui
}  // namespace parm_ui
