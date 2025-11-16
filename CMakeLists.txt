# CMakeLists.txt for HALCON extension package 'halconuser' example
# Copyright (c) 2019-2023 by MVTec Software GmbH

# Option for using HALCON XL.
option(HALCON_XL "Use HALCON XL" OFF)
# Option for enabling .NET support. This will require a more recent version
# of CMake to work.
option(HALCON_DOTNET "Build .NET support" ON)
# If the HALCON_DOTNET option is selected, check if this version of CMake
# has the required CSharp support. Note that the Visual Studio 2010 or later
# generator has to be used.
if(HALCON_DOTNET)
  cmake_minimum_required(VERSION 4.1.1)
  set(_CSHARP CSharp)
else()
  cmake_minimum_required(VERSION 4.1.1)
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
# 禁止生成 Debug/Release 子目录，直接输出到 bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin")
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


HALCON_AddExtensionPackage(Halcon_SoftwarePackage
  DEF_FILES
    def/Halcon_Sqlite.def
  SOURCES
    source/Halcon_SoftwarePackage.c
    source/Halcon_modbus.cpp
    source/sqlite3.c
    source/shell.c
    source/Halcon_sqlite.cpp
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
# Halcon 的 lib 目录（根据编译器自动选 x64-win64 vs x86-win32）
set(HALCON_LIB_DIR ${HALCON_ROOT}/lib/x64-win64)#

# --------------  三方库目录（无空格/括号安全版） --------------

target_include_directories(${ProjectName} 
    PUBLIC
        $<BUILD_INTERFACE:${HALCON_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        #往后增加3方库的头文件目录
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/3rd/libmodbus>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/3rd/iup/include>

)
set(IUP_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/3rd/iup/lib/")
target_link_libraries(${ProjectName} 
    ${HALCON_LIB_DIR}/halconcpp.lib
    #windowsAPI静态库
    ws2_32.lib
    comctl32.lib
    gdi32.lib
    user32.lib
    winmm.lib
    ole32.lib
    #往后增加3方库的lib文件
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/libmodbus/modbus.lib
    #IUP核心静态库（按需选择）
    ${IUP_LIB_PATH}/iup.lib              # 基础库，必需
    ${IUP_LIB_PATH}/iupcontrols.lib      # 控件库（如使用IupButton等）
    ${IUP_LIB_PATH}/iupimglib.lib        # 图像库（如使用内置图标）
)

