#include "ui_font.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

#include "imgui.h"

namespace parm_ui {
namespace ui {

namespace {

// 字体候选：**相对路径优先**（相对进程当前工作目录），找不到再退到系统字体。
// 系统字体本身只能用绝对路径，这是没办法的部分。
std::vector<std::string> candidate_fonts() {
    std::vector<std::string> v;

    if (const char* env = std::getenv("PARM_UI_FONT"))
        if (*env) v.emplace_back(env);

    static const char* kDirs[]  = {"fonts/", "resources/fonts/", "assets/fonts/", "./"};
    static const char* kNames[] = {"msyh.ttc",
                                   "msyh.ttf",
                                   "simhei.ttf",
                                   "simsun.ttc",
                                   "NotoSansCJK-Regular.ttc",
                                   "NotoSansCJKsc-Regular.otf",
                                   "wqy-microhei.ttc",
                                   "wqy-zenhei.ttc",
                                   "DroidSansFallbackFull.ttf"};
    for (const char* dir : kDirs)
        for (const char* name : kNames) v.emplace_back(std::string(dir) + name);

#if defined(_WIN32)
    v.emplace_back("C:/Windows/Fonts/msyh.ttc");     // 微软雅黑
    v.emplace_back("C:/Windows/Fonts/msyh.ttf");
    v.emplace_back("C:/Windows/Fonts/msyhl.ttc");
    v.emplace_back("C:/Windows/Fonts/simhei.ttf");   // 黑体
    v.emplace_back("C:/Windows/Fonts/simsun.ttc");   // 宋体
    v.emplace_back("C:/Windows/Fonts/Deng.ttf");     // 等线
#elif defined(__APPLE__)
    v.emplace_back("/System/Library/Fonts/PingFang.ttc");
    v.emplace_back("/System/Library/Fonts/Hiragino Sans GB.ttc");
#endif

    // Linux / 通用
    v.emplace_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
    v.emplace_back("/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf");
    v.emplace_back("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc");
    v.emplace_back("/usr/share/fonts/truetype/noto/NotoSansCJKsc-Regular.otf");
    v.emplace_back("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc");
    v.emplace_back("/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc");
    v.emplace_back("/usr/share/fonts/truetype/arphic/uming.ttc");
    v.emplace_back("/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf");
    v.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    return v;
}

}  // namespace

bool load_ui_font(float size_px, std::string& used_path) {
    used_path.clear();

    ImGuiIO& io = ImGui::GetIO();

    // 1.92 的字体是“字形按需加载”：不需要再预置字形范围，
    // 任意 UTF-8 字符（中文 / 生僻字 / 补充平面）用到时才从字体文件里取。
    ImGui::GetStyle().FontSizeBase = size_px;

    for (const std::string& p : candidate_fonts()) {
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::u8path(p), ec)) continue;

        ImFont* f = io.Fonts->AddFontFromFileTTF(p.c_str());
        if (f != nullptr) {
            used_path = p;
            return true;
        }
    }
    return false;
}

}  // namespace ui
}  // namespace parm_ui
