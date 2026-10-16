# 拷贝 OpenVINO 插件/前端 DLL 到扩展包输出目录。
# 这些 DLL 由 openvino.dll 在运行时按目录加载，不属于链接期依赖，
# 因此 $<TARGET_RUNTIME_DLLS> 不会包含它们，需要单独拷贝。
#
# 期望变量：
#   CONFIG         当前构建配置（Debug / Release / ...）
#   VCPKG_BIN_RELEASE  vcpkg installed/<triplet>/bin
#   VCPKG_BIN_DEBUG    vcpkg installed/<triplet>/debug/bin
#   OUT_DIR        扩展包输出目录（bin/）

if(CONFIG STREQUAL "Debug")
    set(_src "${VCPKG_BIN_DEBUG}")
else()
    set(_src "${VCPKG_BIN_RELEASE}")
endif()

if(EXISTS "${_src}")
    file(GLOB _dlls "${_src}/openvino*.dll")
    foreach(_dll ${_dlls})
        file(COPY "${_dll}" DESTINATION "${OUT_DIR}")
    endforeach()
    message(STATUS "OpenVINO runtime DLLs copied from ${_src}")
else()
    message(WARNING "vcpkg bin dir not found: ${_src}")
endif()
