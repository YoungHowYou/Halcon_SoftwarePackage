#=============================================================================
#  cv_region/CMakeLists.txt — C++17 region + ransac library（单头 + 单实现）
#=============================================================================
# 形态：静态库 cvr_core，直接链入 Halcon_SoftwarePackage 扩展包，supply 层直接调 C++ API。
#   源码只有  src/cvr.cpp  ，声明只有  include/cvr/cvr.hpp  。
#   —— 新增算子/函数直接加进这两个文件；**非必要不要新建文件**。
#      （原 model_expression.h / optimizer.h / ransac_core.h 等内部头已折叠进 cvr.cpp，
#        原 cvr_io（OpenCV 桥）、cvr_c_api（遗留 C ABI）已折成文件末尾的段落。）
#   依赖：STL + Eigen3::Eigen + muparser（PUBLIC 链出）。
#
# Build options:
#    CVR_BUILD_OPENCV   ON/OFF  (default: ON 仅独立构建时；作为子项目默认 OFF)
#    CVR_BUILD_TESTS    ON/OFF  (default: ON 仅独立构建时；作为子项目默认 OFF)
#=============================================================================

cmake_minimum_required(VERSION 3.21)
project(cv_region
  VERSION     0.2.0
  DESCRIPTION "Open-source region library compatible with HALCON semantics"
  LANGUAGES   CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 作为子项目被 Halcon_SoftwarePackage 引入时，默认不构建测试 / OpenCV 桥
if(PROJECT_IS_TOP_LEVEL)
  set(_cvr_default ON)
else()
  set(_cvr_default OFF)
endif()

# ---- options ----
option(CVR_BUILD_TESTS  "Build cv_region core tests"  ${_cvr_default})
option(CVR_BUILD_OPENCV "Build OpenCV I/O bridge"    ${_cvr_default})
unset(_cvr_default)

# ---- 依赖：拆分后 cv_region 只依赖 STL（ransac/measure 已迁至 cv_flow） ----

# ---- 静态库：单头 + 单实现（直接链入扩展包，免跨 DLL 拷贝） ----
add_library(cvr_core STATIC
  src/cvr.cpp
)

target_include_directories(cvr_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 无三方链接依赖（纯 STL）

#=============================================================================
#  Install & export — 外部 CMake 工程免 HALCON 复用：
#    find_package(cvr_core CONFIG REQUIRED)
#    target_link_libraries(app PRIVATE cvr::cvr_core)
#=============================================================================
add_library(cvr::cvr_core ALIAS cvr_core)
include(GNUInstallDirs)
install(TARGETS cvr_core
  EXPORT cvr_core-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# cvr_core 纯 STL、无链接依赖，config 直接引入 targets 文件
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cvr_core-config.cmake"
  "include(\"\${CMAKE_CURRENT_LIST_DIR}/cvr_core-targets.cmake\")\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cvr_core-config.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cvr_core")
install(EXPORT cvr_core-targets
  FILE cvr_core-targets.cmake
  NAMESPACE cvr::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cvr_core")

# 注：单 TU 失去了「按 obj 粒度丢弃未引用代码」的粒度，但**不需要** /Gy ——
# 实测（2026-09）：加 /Gy 与不加，cvr.obj 同为 10,352 KB、cvr_core.lib 15,612 vs 15,246 KB、
# 最终 DLL 同为 1,085,952 字节且死代码（cvr_c_api）都探不到 —— 链接期 /OPT:REF 已足够剔除。

# ---- 优化 flags（性能整改；只影响本库这一个 TU，不传染 consumer） ----
# 显式钉死 /O2：防默认空 build type 落入无优化。
# **严禁 /fp:fast**：会改变 moments / area_center / gray 特征的浮点累加结果。
# **不加 /arch:AVX2**（实测 2026-09-28）：该 TU 含 Eigen LM + muparser 段，AVX2 下
# cv_ransac_fit 触发 0xC0000374 堆损坏（对齐相关），收益/风险不成比例，放弃。
if(MSVC)
  target_compile_options(cvr_core PRIVATE
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:RelWithDebInfo>:/O2>)
endif()

# ---- OpenCV 桥（折在 cvr.hpp/cvr.cpp 末尾的 #ifdef CVR_WITH_OPENCV 段） ----
if(CVR_BUILD_OPENCV)
  find_package(OpenCV QUIET COMPONENTS core imgproc)
  if(OpenCV_FOUND)
    target_include_directories(cvr_core PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(cvr_core PUBLIC CVR_WITH_OPENCV=1)
    target_link_libraries(cvr_core PRIVATE ${OpenCV_LIBS})
    message(STATUS "cv_region: OpenCV bridge enabled (${OpenCV_VERSION})")
  else()
    message(STATUS "cv_region: OpenCV not found, bridge disabled")
    set(CVR_BUILD_OPENCV OFF CACHE BOOL "Build OpenCV I/O bridge" FORCE)
  endif()
endif()

# ---- tests ----
if(CVR_BUILD_TESTS)
  enable_testing()

  # m8_ransac / m9_measure 已随 ransac/measure 迁至 cv_flow/tests
  foreach(_t IN ITEMS
      test_m1_region test_m2_ops test_m3_conn test_m4_morph
      test_m5_features test_m6_shape test_m7_select test_m10_cache)
    add_executable(${_t} tests/${_t}.cpp)
    target_link_libraries(${_t} PRIVATE cvr_core)
    add_test(NAME ${_t} COMMAND ${_t})
  endforeach()
  unset(_t)
endif()
