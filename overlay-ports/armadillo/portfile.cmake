vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_sourceforge(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO arma
    FILENAME "armadillo-${VERSION}.tar.xz"
    # 说明：上游重新打包了 armadillo-14.4.1.tar.xz，vcpkg 官方 baseline 记录的 SHA512 已失效
    #（下载得到的文件内容为完整的 14.4.1 源码树，共 694 个文件，仅压缩包哈希不同）。
    # 此处改用本地实际下载文件的 SHA512。
    SHA512 641aa3e48cc5250042930ef2beed5ac7b7ff6b8b246a822e0c49a70634992383560b05ee2750c075d3c12dae459a5c96acdfb209486545f2873d03dd63ed5dd9
    PATCHES
        cmake-config.patch
        dependencies.patch
        pkgconfig.patch
)

set(REQUIRES_PRIVATE "")
foreach(module IN ITEMS lapack blas)
    if(EXISTS "${CURRENT_INSTALLED_DIR}/lib/pkgconfig/${module}.pc")
        string(APPEND REQUIRES_PRIVATE " ${module}")
    endif()
endforeach()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    DISABLE_PARALLEL_CONFIGURE
    OPTIONS
        -DALLOW_FLEXIBLAS_LINUX=OFF
        "-DREQUIRES_PRIVATE=${REQUIRES_PRIVATE}"
        -DBUILD_SMOKE_TEST=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(PACKAGE_NAME Armadillo CONFIG_PATH share/Armadillo/CMake)
vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/Armadillo/ArmadilloConfig.cmake"
                    [[include("${CMAKE_CURRENT_LIST_DIR}/ArmadilloLibraryDepends.cmake")]]
                    "include(CMakeFindDependencyMacro)\nfind_dependency(LAPACK)\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/ArmadilloLibraryDepends.cmake\")"
                    )
vcpkg_fixup_pkgconfig()
vcpkg_copy_pdbs()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/share/Armadillo/CMake"
)

file(GLOB SHARE_ARMADILLO_FILES "${CURRENT_PACKAGES_DIR}/share/Armadillo/*")
if(SHARE_ARMADILLO_FILES STREQUAL "")
    # On case sensitive file system there is an extra empty directory created that should be removed
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/Armadillo")
endif()

vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/armadillo_bits/config.hpp" "#define ARMA_AUX_LIBS " "#define ARMA_AUX_LIBS //")

file(COPY "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
