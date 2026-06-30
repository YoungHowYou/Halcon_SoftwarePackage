#=============================================================================
#  CMakeLists.txt — HALCON Extension Package "Halcon_SoftwarePackage"
#=============================================================================
#  Copyright (c) 2019-2023 by MVTec Software GmbH
#
#  依赖管理：全部通过 vcpkg
#    spdlog, libmodbus, sqlite3, libmysql, opencv4, exiv2  →  vcpkg
#    HALCON                                  →  HALCONROOT 环境变量
#
#  用法:
#    cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake
#    cmake --build build
#=============================================================================

cmake_minimum_required(VERSION 3.21)

#=============================================================================
#  Options  选项
#=============================================================================
option(HALCON_XL "Use HALCON XL" OFF)

# .NET 支持：Windows 默认 ON，Linux/macOS 默认 OFF
if(WIN32)
  option(HALCON_DOTNET "Build .NET support" ON)
else()
  option(HALCON_DOTNET "Build .NET support" OFF)
endif()

# ---- vcpkg toolchain 自动拾取 ----
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(_vcpkg_tc "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  if(EXISTS "${_vcpkg_tc}")
    set(CMAKE_TOOLCHAIN_FILE "${_vcpkg_tc}" CACHE STRING "vcpkg toolchain")
  endif()
  unset(_vcpkg_tc)
endif()

#=============================================================================
#  Policies  策略
#=============================================================================
if(POLICY CMP0068)
  cmake_policy(SET CMP0068 NEW)   # macOS install_name 不受 RPATH 影响
endif()
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)   # 启用 CMAKE_MSVC_RUNTIME_LIBRARY
endif()

#=============================================================================
#  Project  项目
#=============================================================================
project(Halcon_SoftwarePackage
  VERSION     1.0.0
  DESCRIPTION "HALCON SoftwarePackage Extension — Modbus / MySQL / SQLite / spdlog / OpenCV / exiv2"
  LANGUAGES   C CXX
)

# .NET / CSharp 支持（必须在 project() 之后调用 enable_language）
if(HALCON_DOTNET)
  enable_language(CSharp OPTIONAL)
  if(NOT CMAKE_CSharp_COMPILER)
    message(WARNING "CSharp compiler not found — .NET extension disabled. "
                    "Install .NET SDK or pass -DHALCON_DOTNET=OFF")
    set(HALCON_DOTNET OFF)
  endif()
endif()

# ---- 自动修复 vcpkg_make_install 的 usr/* 前缀问题 ----
set(_VCPKG_INSTALLED "${CMAKE_BINARY_DIR}/vcpkg_installed")
if(EXISTS "${_VCPKG_INSTALLED}")
  file(GLOB_RECURSE _usr_dirs "${_VCPKG_INSTALLED}/*/usr/local" "${_VCPKG_INSTALLED}/*/usr/lib" "${_VCPKG_INSTALLED}/*/usr/share")
  foreach(_d ${_usr_dirs})
    get_filename_component(_pkg_root "${_d}" DIRECTORY)
    get_filename_component(_sub_dir "${_d}" NAME)
    file(COPY "${_d}/" DESTINATION "${_pkg_root}/${_sub_dir}")
    message(STATUS "Fixed usr/${_sub_dir}: ${_pkg_root}")
  endforeach()
  # Clean up entire usr dir after fixing
  file(GLOB_RECURSE _usr_roots "${_VCPKG_INSTALLED}/*/usr")
  foreach(_d ${_usr_roots})
    if(IS_DIRECTORY "${_d}")
      file(REMOVE_RECURSE "${_d}")
    endif()
  endforeach()
endif()

set(ProjectName Halcon_SoftwarePackage)

#=============================================================================
#  Platform detection & output directories  平台检测 & 输出目录
#=============================================================================
if(WIN32)
  set(HALCON_PLATFORM     "x64-win64")
  set(HALCON_SHARED_SUFFIX ".dll")
  set(_PKG_OUTPUT_DIR     "${CMAKE_SOURCE_DIR}/bin")
elseif(APPLE)
  set(HALCON_PLATFORM     "x64-macos")
  set(HALCON_SHARED_SUFFIX ".dylib")
  set(_PKG_OUTPUT_DIR     "${CMAKE_SOURCE_DIR}/lib/${HALCON_PLATFORM}")
else()
  # Linux / 其他 Unix
  set(HALCON_PLATFORM     "x64-linux")
  set(HALCON_SHARED_SUFFIX ".so")
  set(_PKG_OUTPUT_DIR     "${CMAKE_SOURCE_DIR}/lib/${HALCON_PLATFORM}")
endif()

message(STATUS "HALCON platform : ${HALCON_PLATFORM}")
message(STATUS "Output directory: ${_PKG_OUTPUT_DIR}")

# 所有构建产物统一输出到 _PKG_OUTPUT_DIR（不分 Debug/Release 子目录）
foreach(_cfg "" _DEBUG _RELEASE _MINSIZEREL _RELWITHDEBINFO)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${_cfg} "${_PKG_OUTPUT_DIR}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY${_cfg} "${_PKG_OUTPUT_DIR}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY${_cfg} "${_PKG_OUTPUT_DIR}")
endforeach()
unset(_cfg)

#=============================================================================
#  HALCON setup
#=============================================================================
file(TO_CMAKE_PATH "$ENV{HALCONEXAMPLES}" HALCONEXAMPLES)
list(APPEND CMAKE_MODULE_PATH "${HALCONEXAMPLES}/cmake")
include(UseHALCON)

set(HALCON_ROOT        "$ENV{HALCONROOT}")
set(HALCON_INCLUDE_DIR "${HALCON_ROOT}/include/halconcpp")
set(HALCON_LIB_DIR     "${HALCON_ROOT}/lib/${HALCON_PLATFORM}")

#----------------------------------------------------------------------
#  HALCON extension 源文件（SQLite3 由 vcpkg 提供，不再编译 amalgamation）
#----------------------------------------------------------------------
set(EXT_SOURCES
  source/Halcon_SoftwarePackage.c
  source/Halcon_modbus.cpp
  source/Halcon_sqlite.cpp
  source/Halcon_spdlog.cpp
  source/Halcon_mysql.cpp
  source/Halcon_OpenCV.cpp
)

#----------------------------------------------------------------------
#  核心：创建 HALCON 扩展包目标
#----------------------------------------------------------------------
HALCON_AddExtensionPackage(${ProjectName}
  DEF_FILES
    def/Halcon_Sqlite.def
    def/Halcon_Modbus.def
    def/Halcon_Spdlog.def
    def/Halcon_Mysql.def
    def/Halcon_OpenCV.def
  SOURCES
    ${EXT_SOURCES}
  CHAPTERS
    userextensions
  CLASSES
    SoftwarePackage
  OPERATORS
)

#=============================================================================
#  Dependencies — 全部通过 vcpkg
#=============================================================================

#==================================================================
#  OpenCV (from Halcon_YouloBe migration)
#==================================================================
find_package(OpenCV REQUIRED
  COMPONENTS core imgproc imgcodecs features2d calib3d flann
)
message(STATUS "OpenCV ${OpenCV_VERSION} — ${OpenCV_DIR}")

#==================================================================
#  exiv2 (from Halcon_YouloBe migration)
#==================================================================
find_package(exiv2 REQUIRED)
message(STATUS "exiv2 ${exiv2_VERSION}")

#==================================================================
#  spdlog
#==================================================================
find_package(spdlog CONFIG REQUIRED)
message(STATUS "spdlog    → vcpkg  (spdlog::spdlog)")
set(SPDLOG_LIBS spdlog::spdlog)

#==================================================================
#  libmodbus
#==================================================================
find_package(libmodbus CONFIG REQUIRED)
message(STATUS "libmodbus → vcpkg  (modbus)")
set(MODBUS_LIBS modbus)

#==================================================================
#  SQLite3
#==================================================================
find_package(unofficial-sqlite3 CONFIG REQUIRED)
message(STATUS "SQLite3   → vcpkg  (unofficial::sqlite3::sqlite3)")
set(SQLITE3_LIBS unofficial::sqlite3::sqlite3)

#==================================================================
#  MySQL
#==================================================================
find_package(unofficial-libmysql CONFIG REQUIRED)
message(STATUS "MySQL     → vcpkg  (unofficial::libmysql::libmysql)")
set(MYSQL_LIBS unofficial::libmysql::libmysql)
# vcpkg libmysql 的头文件在 include/mysql/ 子目录，需额外添加
get_target_property(_mysql_inc ${MYSQL_LIBS} INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(${ProjectName} PRIVATE "${_mysql_inc}/mysql")

#=============================================================================
#  Include directories — 公共头文件路径
#=============================================================================
target_include_directories(${ProjectName}
  PUBLIC
    $<BUILD_INTERFACE:${HALCON_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${OpenCV_INCLUDE_DIRS}>
)

#=============================================================================
#  IDE 辅助：头文件 & .def 文件在 IDE 中可见
#=============================================================================
file(GLOB_RECURSE PROJECT_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
target_sources(${ProjectName} PRIVATE ${PROJECT_HEADERS})

file(GLOB DEF_FILES "${CMAKE_CURRENT_SOURCE_DIR}/def/*.def")
target_sources(${ProjectName} PRIVATE ${DEF_FILES})
set_source_files_properties(${DEF_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
source_group("Module Definition Files" FILES ${DEF_FILES})

#=============================================================================
#  Link libraries  链接
#=============================================================================
if(WIN32)
  target_link_libraries(${ProjectName}
    ${HALCON_LIB_DIR}/halconcpp.lib
    ws2_32.lib
    ${OpenCV_LIBS}
    exiv2lib
    ${SPDLOG_LIBS}
    ${MODBUS_LIBS}
    ${SQLITE3_LIBS}
    ${MYSQL_LIBS}
  )
else()
  find_package(Threads REQUIRED)
  target_link_libraries(${ProjectName}
    ${HALCON_LIB_DIR}/libhalconcpp${HALCON_SHARED_SUFFIX}
    ${OpenCV_LIBS}
    exiv2lib
    ${SPDLOG_LIBS}
    ${MODBUS_LIBS}
    ${SQLITE3_LIBS}
    ${MYSQL_LIBS}
    Threads::Threads
    ${CMAKE_DL_LIBS}
  )
endif()

#=============================================================================
#  Post-build  构建后复制 vcpkg 共享库到输出目录
#=============================================================================
if(WIN32)
  add_custom_command(TARGET ${ProjectName} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE_DIR:${MYSQL_LIBS}>/../bin/libmysql.dll"
      "$<TARGET_FILE_DIR:${ProjectName}>/libmysql.dll"
    COMMENT "Copying libmysql.dll → output"
  )
else()
  # Linux / macOS：从 vcpkg installed 目录复制 .so/.dylib（含符号链接）到输出目录
  set(_VCPKG_LIBDIR "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib")
  foreach(_lib ${SPDLOG_LIBS} ${MODBUS_LIBS} ${SQLITE3_LIBS} ${MYSQL_LIBS})
    # 从 IMPORTED_LOCATION 提取库的基名（去掉版本后缀），如 libmodbus.so.5.1.0 → libmodbus
    get_target_property(_loc ${_lib} IMPORTED_LOCATION)
    if(NOT _loc)
      get_target_property(_loc ${_lib} IMPORTED_LOCATION_RELEASE)
    endif()
    if(_loc)
      get_filename_component(_name "${_loc}" NAME_WE)
      string(REGEX REPLACE "\\.[0-9].*" "" _base "${_name}")
      # 复制该库的所有相关文件（本体 + SONAME 符号链接 + linker-name 符号链接）
      file(GLOB _files "${_VCPKG_LIBDIR}/${_base}${HALCON_SHARED_SUFFIX}*")
      foreach(_f ${_files})
        get_filename_component(_fname "${_f}" NAME)
        add_custom_command(TARGET ${ProjectName} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_f}"
                  "$<TARGET_FILE_DIR:${ProjectName}>/${_fname}"
          COMMENT "Copying ${_fname} → output"
        )
      endforeach()
      unset(_files)
    endif()
  endforeach()

  # RPATH：优先在自身目录查找依赖库
  set(CMAKE_SKIP_RPATH FALSE)
  set(CMAKE_SKIP_INSTALL_RPATH FALSE)
  set_target_properties(${ProjectName} PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN;${HALCON_LIB_DIR}"
  )
endif()
