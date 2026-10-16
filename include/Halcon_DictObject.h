/*****************************************************************************
 * Halcon_DictObject.h — HDict 里“跨算子调用存活”的对象引用
 *****************************************************************************
 * HDict 的对象载荷一般是 HalconCpp 的 HObject，脚本塞进来的对象用它没问题
 * （键属于调用方上下文，直接 HCopyObj 就能交出去）。
 *
 * 但**扩展算子内部产出**的对象不行：GenImage1 / GenRectangle1 等是在当前算子
 * 调用的上下文里建键的，算子一返回，上下文销毁 →
 *   * 图像：像素数据是引用计数，拷贝还能撑住；
 *   * Region：对象本身随上下文回收，键就变成死键（下次取用报
 *     #4052 "Image data management: wrong object-ID"）。
 *
 * 因此约定：算子产出的对象先塞进句柄自带的**原生 dict**
 * （CreateDict + SetDictObject，dict 归句柄所有、跨调用存活），
 * HDict 里只放 (原生 dict 句柄, 键名) 这个引用；HALCON 侧读取时
 * （hdict_get_object）再就地 GetDictObject + HCopyObj 交给调用者，
 * 于是对象就落在调用方的上下文里，后续随便用。
 *****************************************************************************/
#pragma once

#include "HalconCpp.h"

#include <memory>
#include <string>

struct HDictNativeObjectRef
{
    HalconCpp::HTuple dict;   // 原生 dict 句柄（CreateDict 创建，由算子句柄长期持有）
    std::string       key;    // 原生 dict 里的键名
};

typedef std::shared_ptr<HDictNativeObjectRef> HDictNativeObjectRefPtr;
