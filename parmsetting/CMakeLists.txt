# =============================================================================
# parm_ui —— 参数配置模块（C 接口 + 跨平台 GUI）
#
#   * 纯 CMake 工程，不产出 / 不依赖任何 IDE 工程文件
#   * Windows : MSVC (cl.exe) —— Ninja 或 Visual Studio 生成器都能用
#   * Linux   : GCC
#   * 对外产物：parm_ui.dll / libparm_ui.so （只导出 include/parm_ui.h 里的 C 函数）
#               + parm_ui_demo（命令行 demo）
#
#   三方库（全部开源非商业，构建时自动拉取）：
#     Dear ImGui  https://github.com/ocornut/imgui   MIT
#     GLFW        https://github.com/glfw/glfw       zlib/libpng
#     nlohmann/json  用工作区自带的 json/json.hpp（3.6.1，只用它的 SAX 解析/转义）
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(parm_ui VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# GLFW/ImGui 会静态链进 .so，Linux 上必须是 PIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PARM_UI_BUILD_DEMO  "构建命令行 demo (parm_ui_demo)" ON)
option(PARM_UI_BUILD_TESTS "构建无头自测程序 (parm_ui_selftest)" ON)
option(PARM_UI_WAYLAND     "Linux: 同时编译 GLFW 的 Wayland 后端（需要 wayland 开发包）" OFF)
option(PARM_UI_FETCH_DEPS  "构建时联网下载 GLFW / ImGui" ON)
set(PARM_UI_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE PATH
    "本地三方库源码目录（离线构建时把 glfw/ imgui/ 放这里）")

if(MSVC)
    # /utf-8 必加：源码含中文字面量，否则 MSVC 按 GBK 解析会乱码/报错
    # /wd4819：/utf-8 下 MSVC 对无 BOM 的中文源码仍会误报“字符不在当前代码页”
    add_compile_options(/utf-8 /W3 /EHsc /wd4819)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# ------------------------------------------------------------------ 三方库
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
    set(GLFW_BUILD_X11     ON  CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND ${PARM_UI_WAYLAND} CACHE BOOL "" FORCE)
endif()

if(PARM_UI_FETCH_DEPS)
    include(FetchContent)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE)
    # ImGui 没有 CMakeLists.txt，FetchContent 只负责把源码拉下来，目标自己建
    # 1.92 起字形是“按需加载”的：不再需要预置字形范围，任意 UTF-8 字符
    # （中文 / 生僻字 / 表情）用到时才从字体文件里取，字体图集也小很多。
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.92.9b
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glfw imgui)
else()
    if(NOT EXISTS "${PARM_UI_DEPS_DIR}/glfw/CMakeLists.txt")
        message(FATAL_ERROR "PARM_UI_FETCH_DEPS=OFF 时需要在 ${PARM_UI_DEPS_DIR} 下放 glfw/ 与 imgui/ 源码")
    endif()
    add_subdirectory("${PARM_UI_DEPS_DIR}/glfw" "${CMAKE_BINARY_DIR}/_deps/glfw-build")
    set(imgui_SOURCE_DIR "${PARM_UI_DEPS_DIR}/imgui")
endif()

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

add_library(parm_ui_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp)
target_include_directories(parm_ui_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp)
target_link_libraries(parm_ui_imgui PUBLIC glfw OpenGL::GL)
# IMGUI_USE_WCHAR32：字形码点用 32 位，UTF-8 里的补充平面字符（生僻汉字 / 表情）
# 也能正常查表显示；配合 1.92 的按需加载基本没有额外开销。
target_compile_definitions(parm_ui_imgui PUBLIC IMGUI_USE_WCHAR32)
# 隐藏 ImGui/GLFW 符号：宿主进程里就算也有 Qt/GLFW/ImGui 也不会互相踩
set_target_properties(parm_ui_imgui PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# --------------------------------------------------- 纯模型层（无 GUI，可单测）
add_library(parm_ui_model STATIC
    src/ordered_json.cpp
    src/param_model.cpp)
# json/ 必须 PUBLIC：ordered_json.h 里 #include "json.hpp"
target_include_directories(parm_ui_model PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/json)
target_compile_definitions(parm_ui_model PUBLIC PARM_UI_JSON_HEADER="json.hpp")

# ------------------------------------------------------------- 对外动态库
add_library(parm_ui SHARED
    src/parm_ui.cpp
    src/ui_app.cpp
    src/ui_font.cpp
    src/ui_view.cpp
    src/fs_dialog.cpp)
target_include_directories(parm_ui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(parm_ui PRIVATE parm_ui_model parm_ui_imgui Threads::Threads)
target_compile_definitions(parm_ui PRIVATE PARM_UI_BUILD_DLL)
set_target_properties(parm_ui PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    WINDOWS_EXPORT_ALL_SYMBOLS OFF)

# --------------------------------------------------------------- demo 程序
if(PARM_UI_BUILD_DEMO)
    add_executable(parm_ui_demo demo/main.cpp)
    target_link_libraries(parm_ui_demo PRIVATE parm_ui)
    if(WIN32)
        # demo 用 CommandLineToArgvW 拿宽字符命令行（为了支持中文路径）
        target_link_libraries(parm_ui_demo PRIVATE shell32)
    endif()
endif()

# ------------------------------------------------------------ 无头自测程序
if(PARM_UI_BUILD_TESTS)
    enable_testing()
    add_executable(parm_ui_selftest tests/selftest.cpp)
    target_link_libraries(parm_ui_selftest PRIVATE parm_ui_model)
    add_test(NAME selftest
             COMMAND parm_ui_selftest "${CMAKE_CURRENT_SOURCE_DIR}/test/sample_all_types.json")
endif()
