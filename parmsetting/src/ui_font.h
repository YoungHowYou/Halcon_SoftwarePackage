#pragma once
// =============================================================================
// ui_font.h —— 跨平台中文字体探测
//
//   ImGui 内置字体只有 ASCII，而参数键名基本是中文（隐裂 / 面积筛选 ...），
//   不加载中文字体就会整片显示成 '?'。
//
//   字体查找顺序：先相对目录（fonts/、resources/fonts/、assets/fonts/、./），
//   再退到系统字体（系统字体只能写绝对路径）。
//
//   字形：用的是 ImGui 1.92 的“按需加载”机制——不需要预置字形范围，
//   任意 UTF-8 字符（中文 / 生僻字 / 补充平面）用到时才从字体文件里取。
//
//   环境变量：PARM_UI_FONT 强制指定字体文件（相对或绝对路径都可以）。
// =============================================================================

#include <string>

namespace parm_ui {
namespace ui {

// 成功返回 true，并把实际使用的字体路径写进 used_path；失败返回 false（界面仍可用）
bool load_ui_font(float size_px, std::string& used_path);

}  // namespace ui
}  // namespace parm_ui
