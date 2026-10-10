#=============================================================================
#  Params/CMakeLists.txt — 复刻 hdev 的 LoadParams（递归 JSON 参数解析）静态库
#=============================================================================
#  形态：静态库 hparams_core，单头 hparams_core.h + 单实现 hparams_core.cpp（+ nlohmann json.hpp）。
#    依赖：纯 STL + nlohmann/json.hpp（header-only），零链接依赖。
#    对外只暴露 LoadParam（hdev 的 n 递归已收进 .cpp 内部）。
#
#  两种用法：
#    ① 作为 Halcon_SoftwarePackage 的子项目（根 CMakeLists 已 add_subdirectory）：
#       target_link_libraries(<target> PRIVATE hparams_core)
#       产物输出到仓库 bin/（继承根工程的输出目录设置）。
#    ② 独立构建 + install：
#       cmake -S Params -B <build> -DHPARAMS_BUILD_TESTS=ON
#       find_package(hparams_core CONFIG REQUIRED)
#       target_link_libraries(app PRIVATE hparams::hparams_core)
#=============================================================================

cmake_minimum_required(VERSION 3.21)

project(Params
  VERSION     1.0.0
  DESCRIPTION "hdev LoadParams replica — recursive JSON parameter loader"
  LANGUAGES   CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 作为子项目被 Halcon_SoftwarePackage 引入时，默认不构建测试
if(PROJECT_IS_TOP_LEVEL)
  set(_hparams_tests_default ON)
else()
  set(_hparams_tests_default OFF)
endif()
option(HPARAMS_BUILD_TESTS "Build Params tests" ${_hparams_tests_default})
unset(_hparams_tests_default)

#=============================================================================
#  Library  静态库（单头 + 单实现）
#=============================================================================
add_library(hparams_core STATIC
  hparams_core.cpp
)
add_library(hparams::hparams_core ALIAS hparams_core)

target_include_directories(hparams_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>   # 安装后： #include <hparams/hparams_core.h>
)

target_compile_features(hparams_core PUBLIC cxx_std_17)

if(MSVC)
  # 源码含中文注释/字符串：必须 /utf-8，否则 MSVC 按 GBK 误读
  target_compile_options(hparams_core PRIVATE
    /utf-8
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:RelWithDebInfo>:/O2>)
endif()

#=============================================================================
#  Install & export
#=============================================================================
include(GNUInstallDirs)

install(TARGETS hparams_core
  EXPORT hparams_core-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# 头文件一起装（hparams_core.h 依赖同目录的 json.hpp）
install(FILES hparams_core.h json.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hparams)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/hparams_core-config.cmake"
  "include(\"\${CMAKE_CURRENT_LIST_DIR}/hparams_core-targets.cmake\")\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hparams_core-config.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hparams_core")
install(EXPORT hparams_core-targets
  FILE hparams_core-targets.cmake
  NAMESPACE hparams::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hparams_core")

#=============================================================================
#  Tests
#=============================================================================
if(HPARAMS_BUILD_TESTS)
  enable_testing()
  find_package(Threads REQUIRED)   # 测试里有 ParamTree 并发用例

  add_executable(test_params tests/test_params.cpp)
  target_link_libraries(test_params PRIVATE hparams_core Threads::Threads)
  if(MSVC)
    target_compile_options(test_params PRIVATE /utf-8)
  endif()
  add_test(NAME test_params COMMAND test_params)
endif()
