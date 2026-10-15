#=============================================================================
#  cv_region/python/CMakeLists.txt — pybind11 扩展模块（pip / scikit-build-core）
#=============================================================================
#  为什么**不**用 add_subdirectory(..) 复用父工程：
#    父工程 cv_region/CMakeLists.txt 里带 install(TARGETS cvr_core ...) 和
#    install(DIRECTORY include/ ...)，那些 install 规则会被 wheel 构建一并执行，
#    把 cvr_core.lib（十几 MB）+ 头文件塞进 wheel —— 只有扩展模块才是要装的东西。
#  所以这里直接把「单头 include/cvr/cvr.hpp + 单实现 src/cvr.cpp」编进模块，
#  优化/浮点口径与父工程保持一致（见下方注释）。
#
#  构建（推荐走 pip，见 pyproject.toml）：
#      pip install ../            # 在 cv_region/python 下
#      cmake -S . -B build -Dpybind11_DIR=<pybind11 的 cmake 目录>
#=============================================================================

cmake_minimum_required(VERSION 3.21)
project(cv_region_python LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# cv_region 根目录（C++ 源码所在）
set(CVR_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

# ---- 依赖：pybind11 ----
# pip 装的 pybind11 自带 CMake 配置；scikit-build-core 通常已把它放进搜索路径，
# 找不到时再从当前 Python 环境里问出 pybind11.get_cmake_dir() 兜底。
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

# ---- 扩展模块：绑定 + C++ 核心（单头 + 单实现） ----
pybind11_add_module(_cvr
  src/bindings.cpp
  "${CVR_ROOT}/src/cvr.cpp"
)

target_include_directories(_cvr PRIVATE "${CVR_ROOT}/include")

# cvr.cpp 现在 include <Eigen/Dense>（cv_shape_trans_ex 段）→ 需要 Eigen 头。
# Eigen 是 header-only：只需 include 路径，无链接产物；本机可直接从 vcpkg 树拿到
#（构建时用 -DCMAKE_PREFIX_PATH=<repo>/build/vcpkg_installed/x64-windows 或 vcpkg 工具链）。
find_package(Eigen3 CONFIG REQUIRED)
target_link_libraries(_cvr PRIVATE Eigen3::Eigen)

# 源文件含中文注释与 docstring：MSVC 必须按 UTF-8 解析（source）并输出 UTF-8（execution），
# 否则 docstring 会按系统代码页(GBK)落到字节码里，Python 侧读到乱码。
target_compile_options(_cvr PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 与 cv_region/CMakeLists.txt 相同的口径（改动前请先读那里的注释）：
#   * 显式 /O2：防止空 CMAKE_BUILD_TYPE 掉进无优化
#   * 严禁 /fp:fast：会改变 moments / area_center / 灰度特征的浮点累加结果
#   * 不加 /arch:AVX2：该 TU 在 AVX2 下做过 cv_ransac_fit 堆损坏（0xC0000374）
if(MSVC)
  target_compile_options(_cvr PRIVATE
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:RelWithDebInfo>:/O2>)
endif()

# ---- 安装：扩展模块装进 Python 包 cv_region/ ----
# Windows 上扩展是 .pyd（RUNTIME），类 Unix 上是 .so（LIBRARY），两个都要写。
install(TARGETS _cvr
  LIBRARY DESTINATION cv_region
  RUNTIME DESTINATION cv_region)
