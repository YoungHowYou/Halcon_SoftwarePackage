/*=============================================================================
 * cv_flow/src/imgstr.cpp — 字符串打包实现（由 Halcon_SoftwarePackage.c 迁入，格式未动）
 *===========================================================================*/
#include "cvflow/imgstr.hpp"

#include <cmath>
#include <cstring>

namespace cvflow {

void str_image_size(int strLen, int& width, int& height)
{
    height = 1;
    width  = 1024;
    if ((strLen + 4) > 1024)
        height = (int)std::ceil((strLen + 4) / 1024.0);
    else
        width = (strLen + 4) + 1;
}

void pack_string_to_image(const char* s, int strLen, unsigned char* buf)
{
    std::memcpy(buf, &strLen, 4);
    std::memcpy(buf + 4, s, (size_t)strLen);
}

std::string unpack_string_from_image(const unsigned char* buf)
{
    int len = 0;
    std::memcpy(&len, buf, 4);
    return std::string((const char*)buf + 4, (size_t)len);
}

} // namespace cvflow

/* ---- C ABI 包装 ---- */
extern "C" {

void cvflow_str_image_size(int strLen, int* width, int* height)
{
    cvflow::str_image_size(strLen, *width, *height);
}

void cvflow_pack_string_to_image(const char* s, int strLen, unsigned char* buf)
{
    cvflow::pack_string_to_image(s, strLen, buf);
}

int cvflow_unpack_string_len(const unsigned char* buf)
{
    int len = 0;
    std::memcpy(&len, buf, 4);
    return len;
}

void cvflow_unpack_string_copy(const unsigned char* buf, char* dst)
{
    const int len = cvflow_unpack_string_len(buf);
    std::memcpy(dst, buf + 4, (size_t)len);
    dst[len] = '\0';
}

} // extern "C"
