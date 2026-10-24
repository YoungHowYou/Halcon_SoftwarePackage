#=============================================================================
#  yolo_core/python/CMakeLists.txt — pybind11 扩展模块（pip / scikit-build-core）
#=============================================================================
#  和 cv_region/python 同样的取舍：**不**用 add_subdirectory(..) 复用父工程，
#  因为父工程 yolo_core/CMakeLists.txt 里有 install(TARGETS yolo_core yolo_capi ...)
#  和 install(DIRECTORY include/) —— 那些 install 规则会被 wheel 构建一并执行，
#  把静态库 + 头文件塞进 wheel。这里只把「include/yolo/yolo.hpp + src/yolo.cpp」
#  直接编进扩展模块。
#
#  依赖（OpenCV / OpenVINO）走 vcpkg 的 CMake 配置：
#      pip install ./yolo_core/python --config-settings=cmake.define.CMAKE_PREFIX_PATH=<repo>/build/vcpkg_installed/x64-windows
#  wheel 里只含 .pyd + Python 代码，运行时需要 opencv_core4.dll / openvino*.dll
#  能被找到（见 yolo_core/__init__.py 的 DLL 搜索与 README）。
#=============================================================================

cmake_minimum_required(VERSION 3.21)
project(yolo_core_python LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# yolo_core 根目录（C++ 源码所在）
set(YOLO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

# pybind11 的 non-classic 模式要用 CMake 自带的 python_add_library，
# 它来自 Python 的 Development.Module 组件 —— 必须先找到（scikit-build-core 会把
# 当前解释器通过 Python_EXECUTABLE 传进来）。
find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)

# ---- 依赖：pybind11 ----
find_package(pybind11 CONFIG QUIET)
if(NOT pybind11_FOUND)
  find_package(Python COMPONENTS Interpreter REQUIRED)
  execute_process(
    COMMAND "${Python_EXECUTABLE}" -c "import pybind11; print(pybind11.get_cmake_dir())"
    OUTPUT_VARIABLE _pybind11_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _pybind11_rc)
  if(NOT _pybind11_rc EQUAL 0)
    message(FATAL_ERROR "找不到 pybind11（先 pip install pybind11）")
  endif()
  list(APPEND CMAKE_PREFIX_PATH "${_pybind11_dir}")
  find_package(pybind11 CONFIG REQUIRED)
endif()

# ---- 依赖：OpenCV + OpenVINO（yolo.cpp 自己用；与扩展包一致） ----
find_package(OpenCV REQUIRED)
find_package(OpenVINO REQUIRED)

# ---- 扩展模块：绑定 + C++ 核心 ----
pybind11_add_module(_yolo
  src/bindings.cpp
  "${YOLO_ROOT}/src/yolo.cpp"
)

target_include_directories(_yolo PRIVATE
  "${YOLO_ROOT}/include"
  ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(_yolo PRIVATE ${OpenCV_LIBS} openvino::runtime)

# 源文件含中文注释与 docstring：MSVC 必须按 UTF-8 解析并输出 UTF-8，
# 否则 docstring / 异常文本会按系统代码页(GBK)落到字节码里，Python 侧读到乱码。
target_compile_options(_yolo PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 优化口径与扩展包一致：显式 /O2，避免空 CMAKE_BUILD_TYPE 掉进无优化
if(MSVC)
  target_compile_options(_yolo PRIVATE
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:RelWithDebInfo>:/O2>)
endif()

# ---- 安装：扩展模块装进 Python 包 yolo_core/ ----
install(TARGETS _yolo
  LIBRARY DESTINATION yolo_core
  RUNTIME DESTINATION yolo_core)
