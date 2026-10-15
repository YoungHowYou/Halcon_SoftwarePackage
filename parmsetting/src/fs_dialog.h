#pragma once
// =============================================================================
// fs_dialog.h —— 内置文件 / 目录选择对话框（ImGui 实现，跨平台零额外依赖）
//
//   为什么不用系统对话框：
//     Win 的 IFileDialog 和 Linux 的 zenity/kdialog 各写一套，Linux 上还不一定装了；
//     自己用 std::filesystem + ImGui 画一个，Windows / Linux 行为完全一致。
//   同时保留“手输路径”，所以对话框只是辅助（需求 2.2 的 directory / path 控件）。
// =============================================================================

#include <string>
#include <vector>

namespace parm_ui {
namespace ui {

struct FileDialogState {
    bool        open      = false;
    bool        dir_mode  = false;   // true = 选目录，false = 选文件
    bool        just_open = false;
    std::string title;

    std::string current;      // 当前目录（空 = Windows 驱动器列表 / POSIX 根）
    std::string filename;     // 文件模式下的文件名输入框
    int         selected = -1;  // 列表选中项

    std::string result;       // 确定后的路径
    bool        has_result = false;

    // 内部缓存
    std::vector<std::string> names;
    std::vector<bool>        is_dir;
    bool                     dirty = true;   // 需要重新列目录
    std::string              error;
};

// 打开对话框（dir_mode = true 选目录；initial 为初始路径，可为空）
void open_file_dialog(FileDialogState& st, bool dir_mode, const std::string& title,
                      const std::string& initial);

// 每帧调用；本次产生了结果（用户点了确定）返回 true，结果在 st.result
bool draw_file_dialog(FileDialogState& st);

}  // namespace ui
}  // namespace parm_ui
