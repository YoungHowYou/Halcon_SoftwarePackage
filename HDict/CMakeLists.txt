#=============================================================================
#  HDict/CMakeLists.txt — HALCON dict 复刻（tuple / 对象句柄 / 子 dict）静态库
#=============================================================================
#  形态：静态库 hdict_core，单头 hdict_core.h + 单实现 hdict_core.cpp（+ 第三方 json.hpp）。
#    依赖：纯 STL（C++17，std::shared_mutex），零三方链接依赖。
#
#  两种用法：
#    ① 作为 Halcon_SoftwarePackage 的子项目（根 CMakeLists 已 add_subdirectory）：
#       直接  target_link_libraries(<target> PRIVATE hdict_core)
#       产物输出到仓库 bin/（继承根工程的输出目录设置）。
#    ② 独立构建 + install，外部 CMake 工程免 HALCON 复用：
#       cmake -S HDict -B <build> -DHDICT_BUILD_TESTS=ON
#       find_package(hdict_core CONFIG REQUIRED)
#       target_link_libraries(app PRIVATE hdict::hdict_core)
#=============================================================================

cmake_minimum_required(VERSION 3.21)

project(HDict
  VERSION     1.0.0
  DESCRIPTION "HALCON dict replica — tuple / object handle / sub-dict, thread-safe"
  LANGUAGES   CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 作为子项目被 Halcon_SoftwarePackage 引入时，默认不构建测试
if(PROJECT_IS_TOP_LEVEL)
  set(_hdict_tests_default ON)
else()
  set(_hdict_tests_default OFF)
endif()
option(HDICT_BUILD_TESTS "Build HDict smoke / concurrency tests" ${_hdict_tests_default})
unset(_hdict_tests_default)

#=============================================================================
#  Library  静态库（单头 + 单实现）
#=============================================================================
add_library(hdict_core STATIC
  hdict_core.cpp
)
add_library(hdict::hdict_core ALIAS hdict_core)

target_include_directories(hdict_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>   # 安装后： #include <hdict/hdict_core.h>
)

# std::shared_mutex 需要 C++17
target_compile_features(hdict_core PUBLIC cxx_std_17)

if(MSVC)
  # 源码含中文注释：必须 /utf-8，否则 MSVC 按 GBK 误读（且 /O2 只作用于本库一个 TU）
  target_compile_options(hdict_core PRIVATE
    /utf-8
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:RelWithDebInfo>:/O2>)
endif()

#=============================================================================
#  Install & export — 外部 CMake 工程免 HALCON 复用
#=============================================================================
include(GNUInstallDirs)

install(TARGETS hdict_core
  EXPORT hdict_core-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# 头文件一起装（hdict_core.h 依赖同目录的 json.hpp）
install(FILES hdict_core.h json.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hdict)

# hdict_core 纯 STL、无链接依赖，config 直接引入 targets 文件
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/hdict_core-config.cmake"
  "include(\"\${CMAKE_CURRENT_LIST_DIR}/hdict_core-targets.cmake\")\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hdict_core-config.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hdict_core")
install(EXPORT hdict_core-targets
  FILE hdict_core-targets.cmake
  NAMESPACE hdict::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hdict_core")

#=============================================================================
#  Tests
#=============================================================================
if(HDICT_BUILD_TESTS)
  enable_testing()
  find_package(Threads REQUIRED)

  add_executable(test_hdict tests/test_hdict.cpp)
  target_link_libraries(test_hdict PRIVATE hdict_core Threads::Threads)
  if(MSVC)
    target_compile_options(test_hdict PRIVATE /utf-8)  # 测试与头文件同含中文注释
  endif()
  add_test(NAME test_hdict COMMAND test_hdict)
endif()
