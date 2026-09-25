#=============================================================================
#  cv_region/CMakeLists.txt — C++17 region library (HALCON-compatible RLE)
#=============================================================================
# 形态：静态库，直接链入 Halcon_SoftwarePackage 扩展包，supply 层直接调 C++ API。
# 独立的 C ABI（cvr_c_api.cpp / ransac_interface.cpp）仍编进库内，供 cv_region 自身测试使用。
#  ransac 拟合核心（原独立 ransac 项目）已并入本库：muparser 表达式模型 + Eigen LM 求解。
#
# Build options:
#    CVR_BUILD_OPENCV   ON/OFF  (default: ON if OpenCV found, else OFF)
#    CVR_BUILD_TESTS    ON/OFF  (default: ON 仅独立构建时；作为子项目默认 OFF)
#=============================================================================

cmake_minimum_required(VERSION 3.21)
project(cv_region
  VERSION     0.1.0
  DESCRIPTION "Open-source region library compatible with HALCON semantics"
  LANGUAGES   CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 作为子项目被 Halcon_SoftwarePackage 引入时，默认不构建测试/OpenCV 桥
if(PROJECT_IS_TOP_LEVEL)
  set(_cvr_default ON)
else()
  set(_cvr_default OFF)
endif()

# ---- options ----
option(CVR_BUILD_TESTS "Build cv_region core tests" ${_cvr_default})
option(CVR_BUILD_OPENCV "Build OpenCV I/O bridge" ${_cvr_default})
unset(_cvr_default)

# ---- ransac 依赖（表达式模型 + LM 求解；头文件泄漏到使用者，需 PUBLIC） ----
find_package(Eigen3 CONFIG REQUIRED)
find_package(muparser CONFIG REQUIRED)

# ---- core library: static（直接链入扩展包，免跨 DLL 拷贝） ----
add_library(cvr_core STATIC
  src/cvr_region.cpp
  src/cvr_ops.cpp
  src/cvr_conn.cpp
  src/cvr_morph.cpp
  src/cvr_feat.cpp
  src/cvr_shape.cpp
  src/cvr_select.cpp
  src/cvr_c_api.cpp
  src/cvr_measure.cpp
  # ransac 拟合核心（并入）
  src/ransac_interface.cpp
  src/ransac_core.cpp
)

target_include_directories(cvr_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(cvr_core
  PUBLIC
    Eigen3::Eigen
    muparser::muparser
)

# ---- OpenCV bridge (optional) ----
if(CVR_BUILD_OPENCV)
  find_package(OpenCV QUIET COMPONENTS core imgproc)
  if(OpenCV_FOUND)
    add_library(cvr_io STATIC
      src/cvr_io.cpp
    )
    target_include_directories(cvr_io
      PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${OpenCV_INCLUDE_DIRS}
    )
    target_link_libraries(cvr_io
      PUBLIC cvr_core
      PRIVATE ${OpenCV_LIBS}
    )
    target_compile_definitions(cvr_io PUBLIC CVR_WITH_OPENCV=1)
    message(STATUS "cv_region: OpenCV bridge enabled (${OpenCV_VERSION})")
  else()
    message(STATUS "cv_region: OpenCV not found, bridge disabled")
    set(CVR_BUILD_OPENCV OFF CACHE BOOL "Build OpenCV I/O bridge" FORCE)
  endif()
endif()

# ---- tests ----
if(CVR_BUILD_TESTS)
  enable_testing()

  foreach(_t IN ITEMS
      test_m1_region test_m2_ops test_m3_conn test_m4_morph
      test_m5_features test_m6_shape test_m7_select test_m8_ransac
      test_m9_measure)
    add_executable(${_t} tests/${_t}.cpp)
    target_link_libraries(${_t} PRIVATE cvr_core)
    if(CVR_BUILD_OPENCV AND TARGET cvr_io)
      target_link_libraries(${_t} PRIVATE cvr_io)
    endif()
    add_test(NAME ${_t} COMMAND ${_t})
  endforeach()
  unset(_t)
endif()
