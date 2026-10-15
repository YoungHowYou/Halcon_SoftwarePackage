#pragma once
// =============================================================================
// ui_app.h —— 窗口会话（GLFW + ImGui 模态循环）
//
//   一次会话 = 建窗口 -> 循环渲染 -> 关窗销毁。
//   返回值：0 成功（可能是用户取消），否则 PARM_UI_E_*，原因写进 err。
//   整个会话必须跑在同一条线程里（GLFW/OpenGL 的硬要求），
//   由 parm_ui.cpp 负责把这条线程安排好。
// =============================================================================

#include <string>

namespace parm_ui {

int run_ui_session(const std::string& in_json, int competence, std::string& out_json,
                   bool& cancelled, std::string& err);

}  // namespace parm_ui
