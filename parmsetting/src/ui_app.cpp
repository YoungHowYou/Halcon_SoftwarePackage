#include "ui_app.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <parm_ui.h>

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"

#include "param_model.h"
#include "ui_font.h"
#include "ui_view.h"

namespace parm_ui {

namespace {

void glfw_error_callback(int code, const char* desc) {
    std::fprintf(stderr, "[parm_ui] GLFW error %d: %s\n", code, desc ? desc : "");
}

int env_int(const char* name, int def) {
    if (const char* v = std::getenv(name))
        if (*v) return std::atoi(v);
    return def;
}

bool env_flag(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' && std::strcmp(v, "0") != 0;
}

// 测试钩子用：把当前帧存成 24 位 BMP（不依赖任何图片库）
bool write_bmp(const char* path, int w, int h) {
    if (w <= 0 || h <= 0) return false;

    std::vector<unsigned char> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());

    const int row             = ((w * 3 + 3) / 4) * 4;
    const std::uint32_t data  = static_cast<std::uint32_t>(row) * static_cast<std::uint32_t>(h);
    const std::uint32_t total = 54u + data;

    unsigned char hdr[54] = {0};
    hdr[0] = 'B';
    hdr[1] = 'M';
    std::memcpy(&hdr[2], &total, 4);
    const std::uint32_t off = 54;
    std::memcpy(&hdr[10], &off, 4);
    const std::uint32_t dib = 40;
    std::memcpy(&hdr[14], &dib, 4);
    const std::int32_t ww = w;
    std::memcpy(&hdr[18], &ww, 4);
    const std::int32_t hh = h;
    std::memcpy(&hdr[22], &hh, 4);
    const std::uint16_t planes = 1;
    std::memcpy(&hdr[26], &planes, 2);
    const std::uint16_t bpp = 24;
    std::memcpy(&hdr[28], &bpp, 2);
    std::memcpy(&hdr[34], &data, 4);
    const std::int32_t dpi = 3780;  // 96 dpi
    std::memcpy(&hdr[38], &dpi, 4);
    std::memcpy(&hdr[42], &dpi, 4);

    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    std::fwrite(hdr, 1, 54, f);

    std::vector<unsigned char> line(static_cast<std::size_t>(row), 0);
    for (int y = 0; y < h; ++y) {
        // glReadPixels 给出的是 bottom-up，BMP 也是 bottom-up，行序不用翻
        const unsigned char* src = px.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 3u;
        for (int x = 0; x < w; ++x) {
            line[static_cast<std::size_t>(x) * 3u + 0u] = src[static_cast<std::size_t>(x) * 3u + 2u];
            line[static_cast<std::size_t>(x) * 3u + 1u] = src[static_cast<std::size_t>(x) * 3u + 1u];
            line[static_cast<std::size_t>(x) * 3u + 2u] = src[static_cast<std::size_t>(x) * 3u + 0u];
        }
        std::fwrite(line.data(), 1, static_cast<std::size_t>(row), f);
    }
    std::fclose(f);
    return true;
}

// 有没有可用的图形环境（Linux 上没有 DISPLAY/WAYLAND_DISPLAY 时直接报错，
// 不做离屏渲染）
bool has_display() {
#if defined(_WIN32) || defined(__APPLE__)
    return true;
#else
    const char* d  = std::getenv("DISPLAY");
    const char* wl = std::getenv("WAYLAND_DISPLAY");
    return (d != nullptr && *d != '\0') || (wl != nullptr && *wl != '\0');
#endif
}

// 输出缩进跟随输入风格：
//   * 输入是机器写的单行紧凑 JSON（没有换行）-> 仍旧紧凑输出
//   * 输入本身就是多行可读的                              -> 用 2 空格缩进
// 这样不会因为“打开看一眼”就把对方流水线生成的配置文件整体重排。
int detect_indent(const std::string& in_json) {
    return in_json.find('\n') == std::string::npos ? 0 : 2;
}

}  // namespace

int run_ui_session(const std::string& in_json, int competence, std::string& out_json,
                   bool& cancelled, std::string& err) {
    out_json.clear();
    cancelled = false;
    err.clear();

    // ---------------------------------------------------------------- 1) 解析
    Doc         doc;
    std::string parse_err;
    if (!doc_load(doc, in_json, parse_err)) {
        err = "JSON 解析失败：" + parse_err;
        return PARM_UI_E_PARSE;
    }
    doc.user_competence = competence;

    // ---------------------------------------------------------------- 2) 环境
    if (!has_display()) {
        err = "无图形环境：DISPLAY / WAYLAND_DISPLAY 均未设置";
        return PARM_UI_E_NO_DISPLAY;
    }
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        err = "glfwInit 失败（无法连接窗口系统）";
        return PARM_UI_E_NO_DISPLAY;
    }

    GLFWwindow* window = glfwCreateWindow(1000, 780, "参数配置", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        err = "创建窗口失败（缺少可用的 OpenGL 上下文）";
        return PARM_UI_E_UI_INIT;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetWindowSizeLimits(window, 760, 520, GLFW_DONT_CARE, GLFW_DONT_CARE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.IniFilename = nullptr;  // 不往磁盘写 imgui.ini
    ImGui::StyleColorsDark();

    std::string font_path;
    const bool  font_ok = ui::load_ui_font(17.0f, font_path);
    if (!font_ok) io.Fonts->AddFontDefault();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    // ---------------------------------------------------------------- 3) 循环
    bool save   = false;
    bool cancel = false;   // 仅用于语义清晰：窗口被关/取消

    // 测试钩子（只在设置了环境变量时生效，正常运行完全不受影响）
    const int   smoke_ms   = env_int("PARM_UI_SMOKE_MS", 0);
    const bool  smoke_save = env_flag("PARM_UI_SMOKE_SAVE");
    const int   click_frame = env_int("PARM_UI_SMOKE_CLICK_SAVE", 0);  // 第 N 帧模拟点“保存并关闭”
    const char* shot_path  = std::getenv("PARM_UI_SMOKE_SHOT");
    const char* shot2_path = std::getenv("PARM_UI_SMOKE_SHOT2");
    const double t0        = glfwGetTime();
    int          frame     = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (click_frame > 0 && frame == click_frame) ui::smoke_click_save(true);

        const ui::ViewResult vr = ui::draw_main_window(doc, font_ok);
        if (vr.save) {
            save = true;
            break;
        }
        if (vr.cancel) {
            cancel = true;
            break;
        }
        ImGui::Render();
        int fb_w = 0, fb_h = 0;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        ++frame;
        if (shot_path != nullptr && frame == 3) write_bmp(shot_path, fb_w, fb_h);
        if (shot2_path != nullptr && click_frame > 0 && frame == click_frame + 3)
            write_bmp(shot2_path, fb_w, fb_h);

        glfwSwapBuffers(window);

        if (smoke_ms > 0 && (glfwGetTime() - t0) * 1000.0 >= static_cast<double>(smoke_ms)) {
            if (smoke_save) {
                save = true;
            } else {
                cancel = true;
            }
            break;
        }
    }

    // ---------------------------------------------------------------- 4) 收尾
    if (save) {
        out_json = doc_save(doc, detect_indent(in_json));
    } else {
        out_json  = in_json;  // 取消 / 关窗：原样返回输入
        cancelled = true;
    }
    (void)cancel;

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return PARM_UI_OK;
}

}  // namespace parm_ui
