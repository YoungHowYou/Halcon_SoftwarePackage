#=============================================================================
#  cv_flow/CMakeLists.txt — 流程算法库（单头集合 + 单/少实现）
#=============================================================================
# 定位：对三方库做「流程/组合」的算法层（RANSAC 拟合、1D 测量、特征检测配准、
#       ROI 运算、图像序列化、LM 拟合），供扩展包 supply 与其他 C++ 项目复用。
#   公开头：include/cvflow/*.hpp（只暴露 STL + 自有类型）
#   依赖：Eigen3 + muparser（PUBLIC 链出）、OpenCV（PUBLIC，图像类流程用）
#
# Build options:
#    CVF_BUILD_TESTS  ON/OFF  (default: ON 仅独立构建时；作为子项目默认 OFF)
#=============================================================================

cmake_minimum_required(VERSION 3.21)
project(cv_flow
  VERSION     0.1.0
  DESCRIPTION "Flow/composition algorithms on top of third-party libraries (ransac, measure, features)"
  LANGUAGES   CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(PROJECT_IS_TOP_LEVEL)
  set(_cvf_default ON)
else()
  set(_cvf_default OFF)
endif()
option(CVF_BUILD_TESTS "Build cv_flow tests" ${_cvf_default})
unset(_cvf_default)

# ---- 依赖（PUBLIC：头文件/模板泄漏到使用者） ----
find_package(Eigen3 CONFIG REQUIRED)
find_package(muparser CONFIG REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc features2d calib3d)

file(GLOB _cvflow_headers CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/include/cvflow/*.hpp")

add_library(cv_flow STATIC
  src/ransac.cpp
  src/measure.cpp
  src/features.cpp
  src/roi_arith.cpp
  src/imgstr.cpp
  src/fit.cpp
  ${_cvflow_headers}
)

target_include_directories(cv_flow
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(cv_flow
  PUBLIC
    Eigen3::Eigen
    muparser::muparser
    ${OpenCV_LIBS}
)
# 仅构建期需要显式 include（OpenCV 的导入目标自带接口头路径，安装消费方经由
# find_dependency(OpenCV) 获得）；不加 BUILD_INTERFACE 会导致 install(EXPORT) 报错
target_include_directories(cv_flow PUBLIC
  $<BUILD_INTERFACE:${OpenCV_INCLUDE_DIRS}>)

# 与主仓库一致：钉死 /O2；严禁 /fp:fast、/arch:AVX2（Eigen LM 段对齐问题）
if(MSVC)
  target_compile_options(cv_flow PRIVATE
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:RelWithDebInfo>:/O2>)
endif()

# ---- tests ----
if(CVF_BUILD_TESTS)
  enable_testing()
  foreach(_t IN ITEMS test_m8_ransac test_m9_measure)
    add_executable(${_t} tests/${_t}.cpp)
    target_link_libraries(${_t} PRIVATE cv_flow)
    add_test(NAME ${_t} COMMAND ${_t})
  endforeach()
  unset(_t)
endif()

#=============================================================================
#  Install & export — 外部 CMake 工程免 HALCON 复用：
#    find_package(cv_flow CONFIG REQUIRED)   # 会自动 find_dependency Eigen3/muparser/OpenCV
#    target_link_libraries(app PRIVATE cvf::cv_flow)
#=============================================================================
add_library(cvf::cv_flow ALIAS cv_flow)
include(GNUInstallDirs)
install(TARGETS cv_flow
  EXPORT cv_flow-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# config：先解析 PUBLIC 依赖（Eigen3 / muparser / OpenCV），再引入 targets 文件
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cv_flow-config.cmake"
[=[include(CMakeFindDependencyMacro)
find_dependency(Eigen3 CONFIG)
find_dependency(muparser CONFIG)
find_dependency(OpenCV COMPONENTS core imgproc features2d calib3d)
include("${CMAKE_CURRENT_LIST_DIR}/cv_flow-targets.cmake")
]=])
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cv_flow-config.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cv_flow")
install(EXPORT cv_flow-targets
  FILE cv_flow-targets.cmake
  NAMESPACE cvf::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cv_flow")
