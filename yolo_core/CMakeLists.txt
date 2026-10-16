#=============================================================================
#  yolo_core/CMakeLists.txt — YOLO26-seg OpenVINO 推理核心库
#=============================================================================
# 形态：两个静态库，都可 install 后给任意工程复用（无需 HALCON）
#   yolo_core  C++ API：yolo::Model（include/yolo/yolo.hpp），依赖 OpenCV + OpenVINO
#   yolo_capi  C   API：yolo_openvino_load_model / yolo_openvino_infer / yolo_seg_detect
#              （include/yolo/yolo_capi.h），接口形式与 HALCON 扩展包算子 1:1 一致，
#              字典用 Halcon_SoftwarePackage 的 hdict_core（DictPtr），实现已内嵌，
#              消费方不需要额外链接 hdict_core。
#
# Build options:
#    YOLO_HDICT_DIR     hdict_core 源码目录（默认从 HALCONEXTENSIONS 里找 HDict/）
# 本库不含单元测试：它的验证方式是扩展包里的 examples/yolo/*.hdev 例程
# （以及任意工程直接调 yolo::Model / yolo_capi 的端到端跑图）。
#=============================================================================

cmake_minimum_required(VERSION 3.21)
project(yolo_core
  VERSION     1.0.0
  DESCRIPTION "YOLO26 instance segmentation inference core (OpenVINO, HALCON-free)"
  LANGUAGES   CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)
find_package(OpenVINO REQUIRED)

add_library(yolo_core STATIC
  src/yolo.cpp
)

target_include_directories(yolo_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(yolo_core
  PUBLIC
    ${OpenCV_LIBS}
    openvino::runtime
)
# OpenCV 的导入目标自带接口头路径；仅构建期需要显式 include（安装消费方经由
# find_dependency(OpenCV) 获得）。不加 BUILD_INTERFACE 会导致 install(EXPORT) 报错。
target_include_directories(yolo_core PUBLIC
  $<BUILD_INTERFACE:${OpenCV_INCLUDE_DIRS}>)

#=============================================================================
#  hdict_core：HALCON-free 字典容器（C 接口的字典句柄 = hdict 的 DictPtr）
#    ① 宿主包（Halcon_SoftwarePackage）构建：直接用包内的 hdict_core 目标（同一份实现，
#       符号不重复；该目标不进导出集，故用 BUILD_INTERFACE 包住链接）
#    ② 独立构建 yolo_core：自己编一份（优先相邻的 ../HDict，或 -DYOLO_HDICT_DIR=）
#=============================================================================
set(YOLO_HDICT_DIR "" CACHE PATH "hdict_core 源码目录（含 hdict_core.h/.cpp/json.hpp）")
if(NOT YOLO_HDICT_DIR)
  set(_yolo_hdict_cand "${CMAKE_CURRENT_SOURCE_DIR}/../HDict")   # 宿主仓库内的 HDict/
  if(EXISTS "${_yolo_hdict_cand}/hdict_core.cpp")
    set(YOLO_HDICT_DIR "${_yolo_hdict_cand}")
  endif()
  unset(_yolo_hdict_cand)
endif()
if(NOT YOLO_HDICT_DIR AND DEFINED ENV{HALCONEXTENSIONS})
  set(_halcon_ext_list "$ENV{HALCONEXTENSIONS}")
  foreach(_ext IN LISTS _halcon_ext_list)
    if(EXISTS "${_ext}/HDict/hdict_core.cpp")
      set(YOLO_HDICT_DIR "${_ext}/HDict")
      break()
    endif()
  endforeach()
  unset(_ext)
  unset(_halcon_ext_list)
endif()
if(NOT EXISTS "${YOLO_HDICT_DIR}/hdict_core.cpp")
  message(FATAL_ERROR
    "找不到 hdict_core 源码（yolo_capi 用它当字典容器）。\n"
    "  请用 -DYOLO_HDICT_DIR=<Halcon_SoftwarePackage>/HDict 指定。")
endif()
# 统一成正斜杠：该绝对路径会被写进 install 脚本，反斜杠会被当成转义字符（\d 非法）
file(TO_CMAKE_PATH "${YOLO_HDICT_DIR}" YOLO_HDICT_DIR)
message(STATUS "hdict_core source  : ${YOLO_HDICT_DIR}")

if(TARGET hdict_core)
  set(_yolo_hdict_tgt hdict_core)
elseif(TARGET hdict::hdict_core)
  set(_yolo_hdict_tgt hdict::hdict_core)
else()
  set(_yolo_hdict_tgt "")
endif()

if(_yolo_hdict_tgt)
  # 宿主包构建：复用包内实现（外部构建无需内嵌）
  message(STATUS "hdict_core target  : ${_yolo_hdict_tgt} (reuse)")
  set(_yolo_hdict_link "$<BUILD_INTERFACE:${_yolo_hdict_tgt}>")
  set(_yolo_hdict_objs "")
else()
  # 独立构建：把实现编成 OBJECT 库内嵌进 yolo_capi（消费方无需再链 hdict_core）
  add_library(yolo_hdict OBJECT "${YOLO_HDICT_DIR}/hdict_core.cpp")
  target_include_directories(yolo_hdict PUBLIC "${YOLO_HDICT_DIR}")
  target_compile_features(yolo_hdict PUBLIC cxx_std_17)
  set_target_properties(yolo_hdict PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    # hdict_core.cpp 含中文注释：必须 /utf-8，否则 MSVC 按 GBK 误读
    target_compile_options(yolo_hdict PRIVATE /utf-8)
  endif()
  set(_yolo_hdict_link "")
  set(_yolo_hdict_objs "$<TARGET_OBJECTS:yolo_hdict>")
endif()

#=============================================================================
#  yolo_capi：纯 C 接口（字典传参，与 HALCON 算子 1:1）
#=============================================================================
add_library(yolo_capi STATIC
  src/yolo_capi.cpp
  ${_yolo_hdict_objs}
)
add_library(yolo::yolo_capi ALIAS yolo_capi)

target_include_directories(yolo_capi
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${YOLO_HDICT_DIR}>   # 构建期：hdict_core.h（填字典要用）
    $<INSTALL_INTERFACE:include>           # 安装后：hdict_core.h 与 yolo/ 同在前缀 include/
)
target_compile_features(yolo_capi PUBLIC cxx_std_17)
set_target_properties(yolo_capi PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(yolo_capi PRIVATE /utf-8)
endif()
target_link_libraries(yolo_capi PUBLIC yolo_core ${_yolo_hdict_link})

#=============================================================================
#  Install & export — 外部 CMake 工程免 HALCON 复用：
#    find_package(yolo_core CONFIG REQUIRED)  # 自动 find_dependency OpenCV/OpenVINO
#    target_link_libraries(app PRIVATE yolo::yolo_core)   # 只要 C++ API
#    target_link_libraries(app PRIVATE yolo::yolo_capi)   # 要字典式 C 接口
#=============================================================================
add_library(yolo::yolo_core ALIAS yolo_core)
include(GNUInstallDirs)
install(TARGETS yolo_core yolo_capi
  EXPORT yolo_core-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# C 接口的字典是 hdict_core 的对象，消费方需要它的头（json.hpp 与它同目录，一并装）
install(FILES "${YOLO_HDICT_DIR}/hdict_core.h" "${YOLO_HDICT_DIR}/json.hpp"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/yolo_core-config.cmake"
[=[include(CMakeFindDependencyMacro)
find_dependency(OpenCV)
find_dependency(OpenVINO)
include("${CMAKE_CURRENT_LIST_DIR}/yolo_core-targets.cmake")
]=])
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/yolo_core-config.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/yolo_core")
install(EXPORT yolo_core-targets
  FILE yolo_core-targets.cmake
  NAMESPACE yolo::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/yolo_core")
