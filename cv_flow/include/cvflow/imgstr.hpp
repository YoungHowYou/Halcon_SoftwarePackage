/*=============================================================================
 * cvflow/imgstr.hpp — 字符串 ↔ 字节图像 打包流程
 * 由 Halcon_SoftwarePackage.c 的 StringByImage region 迁入，格式未动：
 *   图像缓冲 = [4 字节小端长度][字符串字节...]，图像尺寸按原规则取。
 * 同时提供 extern "C" 包装，供 C 编译单元（Halcon_SoftwarePackage.c）调用。
 *===========================================================================*/
#pragma once

#include <string>

namespace cvflow {

// 原 HSetStringByImageIn 的尺寸规则：len<=1020 时宽=len+5 高=1，否则 1024 宽、行数向上取整
void str_image_size(int strLen, int& width, int& height);

// 打包：buf 至少 str_image_size 字节；写 [4B 长度][字节]
void pack_string_to_image(const char* s, int strLen, unsigned char* buf);

// 解包：从图像首行缓冲读出字符串（长度取自前 4 字节）
std::string unpack_string_from_image(const unsigned char* buf);

} // namespace cvflow

/* ---- C ABI（供 C 编译的 supply 文件调用） ---- */
#ifdef __cplusplus
extern "C" {
#endif

void cvflow_str_image_size(int strLen, int* width, int* height);
void cvflow_pack_string_to_image(const char* s, int strLen, unsigned char* buf);
int  cvflow_unpack_string_len(const unsigned char* buf);   /* 返回字符串长度 */
void cvflow_unpack_string_copy(const unsigned char* buf, char* dst); /* strcpy 语义 */

#ifdef __cplusplus
}
#endif
