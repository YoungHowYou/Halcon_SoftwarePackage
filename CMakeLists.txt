# CMakeLists.txt for HALCON extension package 'halconuser' example
# Copyright (c) 2019-2023 by MVTec Software GmbH

# Option for using HALCON XL.
option(HALCON_XL "Use HALCON XL" OFF)
# Option for enabling .NET support. This will require a more recent version
# of CMake to work. Linux 默认关闭（系统 cmake 通常较旧、且很少在 Linux 上发布 .NET 扩展）。
if(WIN32)
  option(HALCON_DOTNET "Build .NET support" ON)
else()
  option(HALCON_DOTNET "Build .NET support" OFF)
endif()
# If the HALCON_DOTNET option is selected, check if this version of CMake
# has the required CSharp support. Note that the Visual Studio 2010 or later
# generator has to be used.
if(HALCON_DOTNET)
  cmake_minimum_required(VERSION 4.1.1)
  set(_CSHARP CSharp)
else()
  cmake_minimum_required(VERSION 3.16)
endif()
# When using CMake 3.9 or newer, do not let RPATH affect the install_name on
# macOS.
if(POLICY CMP0068)
  cmake_policy(SET CMP0068 NEW)
endif()
# If policy CMP0091 is available, set it to NEW to enable use of the
# CMAKE_MSVC_RUNTIME_LIBRARY variable.
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
# Start the project here





project(Halcon_SoftwarePackage C CXX )

set(ProjectName Halcon_SoftwarePackage)
set(CMAKE_BUILD_TYPE Debug)
# 禁止生成 Debug/Release 子目录
# Windows: 输出到 bin/
# Linux:   直接输出到 lib/x64-linux/
if(WIN32)
    set(_PKG_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin")
else()
    set(_PKG_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/lib/x64-linux")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_PKG_OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${_PKG_OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_PKG_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${_PKG_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${_PKG_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${_PKG_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${_PKG_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${_PKG_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${_PKG_OUTPUT_DIR}")
# Append HALCON's ikcmake directory to the module path. This requires the
# HALCONEXAMPLES environment variable to be set.
file(TO_CMAKE_PATH "$ENV{HALCONEXAMPLES}" HALCONEXAMPLES)
list(APPEND CMAKE_MODULE_PATH "${HALCONEXAMPLES}/cmake")
# Include common settings for HALCON projects
include(UseHALCON)
# Use the HALCON_AddExtensionPackage function defined in UseHalcon to create
# a HALCON extension package. See the description of HALCON_AddExtensionPackage
# <HALCONEXAMPLES>/cmake/UseHALCON.cmake for details on the allowed parameters.
##这以上都能不能动
#add_definitions(-DIUP_NO_STUB_LIB)  # 告诉IUP不使用stub

HALCON_AddExtensionPackage(Halcon_SoftwarePackage
  DEF_FILES
    def/Halcon_Sqlite.def
    def/Halcon_Modbus.def
    def/Halcon_Spdlog.def
    def/Halcon_Mysql.def
  SOURCES
    source/Halcon_SoftwarePackage.c
    source/Halcon_modbus.cpp
    source/sqlite3.c
    source/shell.c
    source/Halcon_sqlite.cpp
    source/Halcon_spdlog.cpp
    source/Halcon_mysql.cpp
    #source/iupstub.c
  CHAPTERS
    userextensions
  CLASSES
    SoftwarePackage
  OPERATORS


)
##三方库包含
set(HALCON_ROOT $ENV{HALCONROOT})   # 转成 CMake 变量，后面用着方便
# Halcon 头文件目录
set(HALCON_INCLUDE_DIR ${HALCON_ROOT}/include/halconcpp)#HALCON_INCLUDE_DIR变量是${HALCON_ROOT}/include的路径
# Halcon 的 lib 目录：Windows 用 x64-win64，Linux 用 x64-linux
if(WIN32)
    set(HALCON_LIB_DIR ${HALCON_ROOT}/lib/x64-win64)
else()
    set(HALCON_LIB_DIR ${HALCON_ROOT}/lib/x64-linux)
endif()
file(GLOB_RECURSE PROJECT_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
target_sources(${ProjectName} PRIVATE ${PROJECT_HEADERS})
# 1. 收集 .def 文件
file(GLOB DEF_FILES "${CMAKE_CURRENT_SOURCE_DIR}/def/*.def")
# 2. 添加到目标，但不参与编译（通过 HEADER_FILE_ONLY 属性）
target_sources(${ProjectName} PRIVATE ${DEF_FILES})
# 3. 设置属性：只显示，不编译、不链接
set_source_files_properties(${DEF_FILES} PROPERTIES
    HEADER_FILE_ONLY TRUE   # 关键：不让它参与构建
)
# 4. （可选）在 IDE 中分组显示
source_group("Module Definition Files" FILES ${DEF_FILES})
# --------------  三方库目录（无空格/括号安全版） --------------

target_include_directories(${ProjectName}
    PUBLIC
        $<BUILD_INTERFACE:${HALCON_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # spdlog 是 header-only，两端都用打包内的头
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/3rd/spdlog/include>
)

if(WIN32)
    # Windows 用打包好的 modbus / mysql 头（来自 3rd/）
    target_include_directories(${ProjectName} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/3rd/libmodbus>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/3rd/mysql/include>
    )
else()
    # Linux 用 3rd/<lib>/linux/include 下的头（与 Windows 分平台隔离）
    target_include_directories(${ProjectName} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/3rd/libmodbus/linux/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/3rd/libmodbus/linux/include/modbus>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/3rd/mysql/linux/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/3rd/mysql/linux/include/mysql>
    )
endif()

if(WIN32)
    # ---------- Windows：保持原有依赖路径 ----------
    target_link_libraries(${ProjectName}
        ${HALCON_LIB_DIR}/halconcpp.lib
        #往后增加3方库的lib文件
        ws2_32.lib
        ${CMAKE_CURRENT_SOURCE_DIR}/3rd/libmodbus/modbus.lib
        ${CMAKE_CURRENT_SOURCE_DIR}/3rd/spdlog/spdlog.lib
        ${CMAKE_CURRENT_SOURCE_DIR}/3rd/mysql/lib/libmysql.lib
    )

    # 把 libmysql.dll 拷到输出目录，运行时 HALCON 加载扩展时能找到它
    add_custom_command(TARGET ${ProjectName} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/3rd/mysql/lib/libmysql.dll"
            "$<TARGET_FILE_DIR:${ProjectName}>/libmysql.dll"
    )
else()
    # ---------- Linux：使用 3rd/ 内自带的 modbus / mysql ----------
    # 头文件已通过上面的 target_include_directories 注入；
    # spdlog 仍走 header-only 模式，避免依赖 Windows 上预编译的 spdlog.lib。
    target_compile_definitions(${ProjectName} PRIVATE SPDLOG_HEADER_ONLY)

    set(MODBUS_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rd/libmodbus/linux/lib/x64-linux")
    set(MYSQL_LIB_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/3rd/mysql/linux/lib/x64-linux")

    find_package(Threads REQUIRED)

    target_link_libraries(${ProjectName}
        ${HALCON_LIB_DIR}/libhalconcpp.so
        ${MODBUS_LIB_DIR}/libmodbus.so
        ${MYSQL_LIB_DIR}/libmysqlclient.so
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )

    # 把 3rd/ 里的 .so 复制到输出目录，运行时 HALCON 能就近加载
    add_custom_command(TARGET ${ProjectName} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MODBUS_LIB_DIR}/libmodbus.so.5.1.0"
            "$<TARGET_FILE_DIR:${ProjectName}>/libmodbus.so.5.1.0"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "libmodbus.so.5.1.0"
            "$<TARGET_FILE_DIR:${ProjectName}>/libmodbus.so.5"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "libmodbus.so.5.1.0"
            "$<TARGET_FILE_DIR:${ProjectName}>/libmodbus.so"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MYSQL_LIB_DIR}/libmysqlclient.so.21.2.45"
            "$<TARGET_FILE_DIR:${ProjectName}>/libmysqlclient.so.21.2.45"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "libmysqlclient.so.21.2.45"
            "$<TARGET_FILE_DIR:${ProjectName}>/libmysqlclient.so.21"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "libmysqlclient.so.21.2.45"
            "$<TARGET_FILE_DIR:${ProjectName}>/libmysqlclient.so"
    )

    # UseHALCON.cmake 默认把 CMAKE_SKIP_RPATH/INSTALL_RPATH 设为 TRUE，
    # 这里在本目录范围内放开，让 INSTALL_RPATH 生效。
    set(CMAKE_SKIP_RPATH FALSE)
    set(CMAKE_SKIP_INSTALL_RPATH FALSE)
    # 让生成的 .so 优先在自身所在目录查找 modbus / mysql，
    # 同时仍然能找到 HALCON 的 libhalconcpp。
    set_target_properties(${ProjectName} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN;${HALCON_LIB_DIR}"
    )
endif()

