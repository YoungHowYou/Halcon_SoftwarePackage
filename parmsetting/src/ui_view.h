#pragma once
// =============================================================================
// ui_view.h —— 参数树界面（按 Type 自动选控件 + 校验 + 权限）
// =============================================================================

#include <string>
#include <vector>

#include "param_model.h"

namespace parm_ui {
namespace ui {

struct ViewResult {
    bool save   = false;   // 用户点了“保存并关闭”且校验通过
    bool cancel = false;   // 用户点了“取消”或关窗
};

// 画整个界面（顶部工具条 + 参数树 + 诊断/校验弹窗 + 文件对话框）
// font_ok = false 时会在顶部提示“没找到中文字体”（正常时不占位置）
ViewResult draw_main_window(Doc& doc, bool font_ok);

// 测试钩子：让下一帧的“保存并关闭”按钮视为被按下（只有 smoke 脚本会用到）
void smoke_click_save(bool v);

}  // namespace ui
}  // namespace parm_ui
