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
#include "hdict_core.h"

#include <memory>
#include <string>
#include <vector>

struct HDictNativeObjectRef
{
    HalconCpp::HTuple dict;   // 原生 dict 句柄（CreateDict 创建，由算子句柄长期持有）
    std::string       key;    // 原生 dict 里的键名
};

typedef std::shared_ptr<HDictNativeObjectRef> HDictNativeObjectRefPtr;

/*****************************************************************************
 * 原生对象仓库
 *****************************************************************************
 * HDict 句柄自己持有一个原生 dict（懒创建，存在内部键 __hdict_native_store__
 * 下），扩展算子产出的对象先搬进去保活，HDict 里只留 (dict, 键) 这个引用。
 * 这样算子返回、上下文销毁后对象仍然可读（图像 / 区域都一样）。
 *****************************************************************************/
struct HDictNativeStore
{
    HalconCpp::HTuple dict;   // CreateDict 创建，随 HDict 一起存活
};

typedef std::shared_ptr<HDictNativeStore> HDictNativeStorePtr;

/*****************************************************************************
 * HDictObj —— 供 supply 层 / 扩展算子共用的对象与元组小工具
 *****************************************************************************/
namespace HDictObj {

using MLIQMetrics::hdict::DictPtr;

// 内部键：`__` 开头的键不参与 hdict_get_keys 输出（过滤在 supply 层做）
constexpr const char* kInternalPrefix = "__";
constexpr const char* kNativeStoreKey = "__hdict_native_store__";

// ------------------------------------------------- 原生对象仓库（懒创建）
inline HDictNativeStorePtr GetNativeStore(const DictPtr& dict, bool create)
{
    if (!dict) return nullptr;

    try
    {
        HDictNativeStorePtr store =
            MLIQMetrics::hdict::get_dict_object_as<HDictNativeStorePtr>(dict, kNativeStoreKey);
        if (store) return store;
    }
    catch (...)
    {
    }
    if (!create) return nullptr;

    try
    {
        HDictNativeStorePtr store = std::make_shared<HDictNativeStore>();
        HalconCpp::CreateDict(&store->dict);
        MLIQMetrics::hdict::set_dict_object_shared(dict, kNativeStoreKey, store);
        return store;
    }
    catch (...)
    {
        return nullptr;
    }
}

// ------------------------------------------------- 读对象（两种载荷都认）
//   ① shared_ptr<HObject>   —— hdict_set_object 存的调用方上下文对象
//   ② HDictNativeObjectRef  —— 扩展算子产出、寄存在原生 dict 里的对象
inline bool ReadObject(const DictPtr& dict, const std::string& key, HalconCpp::HObject& out)
{
    if (!dict) return false;

    try
    {
        std::shared_ptr<HalconCpp::HObject> p =
            MLIQMetrics::hdict::get_dict_object_as<std::shared_ptr<HalconCpp::HObject>>(dict, key);
        if (p && p->IsInitialized())
        {
            out = *p;
            if (out.IsInitialized()) return true;
        }
    }
    catch (...)
    {
    }

    try
    {
        HDictNativeObjectRefPtr ref =
            MLIQMetrics::hdict::get_dict_object_as<HDictNativeObjectRefPtr>(dict, key);
        if (ref)
        {
            HalconCpp::GetDictObject(&out, ref->dict, HalconCpp::HTuple(ref->key.c_str()));
            return out.IsInitialized();
        }
    }
    catch (...)
    {
    }
    return false;
}

// ------------------------------------------------- 写对象（搬进原生 dict 保活）
inline bool WriteObject(const DictPtr& dict, const std::string& key, const HalconCpp::HObject& obj)
{
    if (!dict || !obj.IsInitialized()) return false;

    HDictNativeStorePtr store = GetNativeStore(dict, true);
    if (!store) return false;

    try
    {
        HalconCpp::SetDictObject(obj, store->dict, HalconCpp::HTuple(key.c_str()));

        HDictNativeObjectRefPtr ref = std::make_shared<HDictNativeObjectRef>();
        ref->dict = store->dict;
        ref->key  = key;
        MLIQMetrics::hdict::set_dict_object_shared(dict, key, ref);
    }
    catch (...)
    {
        return false;
    }
    return true;
}

// ------------------------------------------------- 元组（json）小工具
// 数值容错：json 可能是 int / uint / double / string / bool / 单元素数组
inline double JsonNum(const nlohmann::json& v, double def)
{
    try
    {
        if (v.is_number())  return v.get<double>();
        if (v.is_boolean()) return v.get<bool>() ? 1.0 : 0.0;
        if (v.is_string())  return std::stod(v.get<std::string>());
        if (v.is_array())   return v.empty() ? def : JsonNum(v[0], def);
    }
    catch (...)
    {
    }
    return def;
}

inline int JsonInt(const nlohmann::json& v, int def)
{
    return (int)JsonNum(v, (double)def);
}

inline std::vector<double> JsonToVec(const nlohmann::json& v)
{
    std::vector<double> out;
    if (v.is_array())
    {
        out.reserve(v.size());
        for (const auto& e : v) out.push_back(JsonNum(e, 0.0));
    }
    else if (!v.is_null())
    {
        out.push_back(JsonNum(v, 0.0));
    }
    return out;
}

inline nlohmann::json VecToJson(const std::vector<double>& v)
{
    nlohmann::json a = nlohmann::json::array();
    for (double x : v) a.push_back(x);
    return a;
}

// 整数元组：必须保持 JSON 整数（脚本侧 tuple_select 等算子要求整数索引）
inline nlohmann::json IntVecToJson(const std::vector<long long>& v)
{
    nlohmann::json a = nlohmann::json::array();
    for (long long x : v) a.push_back(x);
    return a;
}

// 读键，缺键 / 不是元组时返回默认值（等价于原生 GetDictTuple 的 try/catch 兜底）
inline nlohmann::json GetTupleOr(const DictPtr& dict, const std::string& key,
                                 const nlohmann::json& def)
{
    if (!dict) return def;
    try
    {
        return MLIQMetrics::hdict::get_dict_tuple(dict, key);
    }
    catch (...)
    {
        return def;
    }
}

}  // namespace HDictObj
