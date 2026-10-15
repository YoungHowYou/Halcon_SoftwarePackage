// Halcon_Dict.cpp —— Dict 模块 supply 层
//   hdict_core（字典句柄，MLIQMetrics::hdict::DictPtr）
//   hparams_core（参数树句柄，MLIQMetrics::ParamTreePtr）
//
// 两个库都与 HALCON 无关（纯 STL / nlohmann），本文件只做参数搬运：
//   HDict  句柄 → DictPtr（内含 shared_mutex，读写线程安全）
//   HParam 句柄 → ParamTreePtr（内含 shared_mutex，load 写锁 / to_json 读锁）
//   ⇒ 两个句柄在多线程间共享时都是线程安全的（锁在库里，supply 不需额外加锁）

#include "Halcon_Def.h"
#include "Halcon_SoftwarePackage.h"

#include "hdict_core.h"
#include "hparams_core.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using MLIQMetrics::hdict::DictPtr;

namespace {

// ---------------------------------------------------------------- 参数读写小工具

// 说明：HGetSPar / HGetPElemS / HGetPElemD 都是**宏**（内部带 return），只能用在返回
//       Herror 的函数里；下面改用非宏的 HPGetPPar（拿到原始 Hcpar 数组，按元素类型
//       自行转换），于是 helper 可以自由返回 string / bool。

// Hcpar → 字符串（数值也转文本，只用于 Key / Type 这类字符串参数）
std::string hcparToString(const Hcpar &p)
{
    if (p.type == STRING_PAR) return std::string(p.par.s ? p.par.s : "");
    if (p.type == LONG_PAR || p.type == INT_PAR) return std::to_string((long long)p.par.l);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", p.par.d);
    return std::string(buf);
}

// Hcpar → json（字符串 / 整数 / 实数）
nlohmann::json hcparToJson(const Hcpar &p)
{
    if (p.type == STRING_PAR) return std::string(p.par.s ? p.par.s : "");
    if (p.type == LONG_PAR || p.type == INT_PAR) return (long long)p.par.l;
    return p.par.d;
}

// 读单个字符串控制参数（调用前须已 HAllocStringMem）
std::string readString(Hproc_handle ph, INT4_8 pos)
{
    Hcpar const *pars = nullptr;
    INT4_8 n = 0;
    if (HPGetPPar(ph, (int)pos, &pars, &n) != H_MSG_TRUE || n < 1) return std::string();
    return hcparToString(pars[0]);
}

// 读一个"数值或字符串"元组 → json
//   单元素存标量，多元素存数组（与 set_dict_tuple 的语义一致）
bool readJsonValue(Hproc_handle ph, INT4_8 pos, nlohmann::json &out)
{
    Hcpar const *pars = nullptr;
    INT4_8 n = 0;
    if (HPGetPPar(ph, (int)pos, &pars, &n) != H_MSG_TRUE || n < 1) return false;

    if (n == 1)
    {
        out = hcparToJson(pars[0]);
    }
    else
    {
        out = nlohmann::json::array();
        for (INT4_8 i = 0; i < n; ++i) out.push_back(hcparToJson(pars[i]));
    }
    return true;
}

// 把 json 写进输出控制参数（字符串 / 数值 / 字符串数组 / 数值数组）
Herror writeJsonValue(Hproc_handle ph, INT4_8 pos, const nlohmann::json &v)
{
    if (v.is_string())
    {
        char *s = const_cast<char *>(v.get_ref<const std::string &>().c_str());
        HPutElem(ph, pos, &s, 1, STRING_PAR);
        return H_MSG_TRUE;
    }
    if (v.is_boolean())
    {
        INT4_8 b = v.get<bool>() ? 1 : 0;
        HPutElem(ph, pos, &b, 1, LONG_PAR);
        return H_MSG_TRUE;
    }
    if (v.is_number())
    {
        double d = v.get<double>();
        HPutElem(ph, pos, &d, 1, DOUBLE_PAR);
        return H_MSG_TRUE;
    }
    if (v.is_array())
    {
        bool allString = !v.empty();
        for (const auto &e : v)
        {
            if (!e.is_string())
            {
                allString = false;
                break;
            }
        }
        if (allString)
        {
            std::vector<std::string> store;
            store.reserve(v.size());
            for (const auto &e : v) store.push_back(e.get<std::string>());
            std::vector<char *> ptrs;
            ptrs.reserve(store.size());
            for (auto &s : store) ptrs.push_back(const_cast<char *>(s.c_str()));
            if (!ptrs.empty()) HPutElem(ph, pos, ptrs.data(), (INT4_8)ptrs.size(), STRING_PAR);
        }
        else
        {
            std::vector<double> nums;
            nums.reserve(v.size());
            for (const auto &e : v) nums.push_back(e.is_number() ? e.get<double>() : 0.0);
            if (!nums.empty()) HPutElem(ph, pos, nums.data(), (INT4_8)nums.size(), DOUBLE_PAR);
        }
        return H_MSG_TRUE;
    }

    // null / object：退化为 JSON 文本
    const std::string text = v.dump();
    char *s = const_cast<char *>(text.c_str());
    HPutElem(ph, pos, &s, 1, STRING_PAR);
    return H_MSG_TRUE;
}

inline Herror failWith(const std::exception &e, int line)
{
    HSetErrText(const_cast<char *>(e.what()));
    return line * 10000;
}

}  // namespace

// ============================================================
// 字典句柄（hdict_core）
// ============================================================

Herror Hhdict_create(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    Def_OUTHDictObject(1, pUserData);
    HDictSetDict(OUTHDictObject(pUserData), MLIQMetrics::hdict::create_dict());
    return H_MSG_TRUE;
}

Herror Hhdict_copy(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    Def_INHDictObject(1, pIn);
    Def_OUTHDictObject(2, pOut);

    DictPtr src = HDictGetDict(pIn);
    if (!src) return H__LINE__ * 10000;
    try
    {
        HDictSetDict(OUTHDictObject(pOut), MLIQMetrics::hdict::copy_dict(src));
    }
    catch (const std::exception &e)
    {
        return failWith(e, H__LINE__);
    }
    return H_MSG_TRUE;
}

Herror Hhdict_set_tuple(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    HAllocStringMem(proc_handle, 1024);
    Def_INHDictObject(1, pData);

    const std::string key = readString(proc_handle, 2);
    nlohmann::json value;
    if (!readJsonValue(proc_handle, 3, value)) return H__LINE__ * 10000;

    DictPtr d = HDictGetDict(pData);
    if (!d) return H__LINE__ * 10000;
    try
    {
        MLIQMetrics::hdict::set_dict_tuple(d, key, value);
    }
    catch (const std::exception &e)
    {
        return failWith(e, H__LINE__);
    }
    return H_MSG_TRUE;
}

Herror Hhdict_get_tuple(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    HAllocStringMem(proc_handle, 1024);
    Def_INHDictObject(1, pData);

    const std::string key = readString(proc_handle, 2);
    DictPtr d = HDictGetDict(pData);
    if (!d) return H__LINE__ * 10000;
    try
    {
        // 注：HALCON 的**输出控制参数独立编号**（从 1 起），本算子唯一输出控制 = 1
        return writeJsonValue(proc_handle, 1, MLIQMetrics::hdict::get_dict_tuple(d, key));
    }
    catch (const std::exception &e)
    {
        return failWith(e, H__LINE__);
    }
}

Herror Hhdict_set_subdict(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    HAllocStringMem(proc_handle, 1024);
    Def_INHDictObject(1, pData);
    const std::string key = readString(proc_handle, 2);
    Def_INHDictObject(3, pSub);

    DictPtr d = HDictGetDict(pData);
    DictPtr s = HDictGetDict(pSub);
    if (!d || !s) return H__LINE__ * 10000;
    try
    {
        MLIQMetrics::hdict::set_dict_tuple(d, key, s);
    }
    catch (const std::exception &e)
    {
        return failWith(e, H__LINE__);
    }
    return H_MSG_TRUE;
}

Herror Hhdict_get_keys(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    Def_INHDictObject(1, pData);

    DictPtr d = HDictGetDict(pData);
    if (!d) return H__LINE__ * 10000;
    try
    {
        const std::vector<std::string> keys = MLIQMetrics::hdict::get_dict_param_keys(d);
        std::vector<char *> ptrs;
        ptrs.reserve(keys.size());
        for (const auto &k : keys) ptrs.push_back(const_cast<char *>(k.c_str()));
        if (!ptrs.empty()) HPutElem(proc_handle, 1, ptrs.data(), (INT4_8)ptrs.size(), STRING_PAR);
    }
    catch (const std::exception &e)
    {
        return failWith(e, H__LINE__);
    }
    return H_MSG_TRUE;
}

Herror Hhdict_key_exists(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    HAllocStringMem(proc_handle, 1024);
    Def_INHDictObject(1, pData);

    const std::string key = readString(proc_handle, 2);
    DictPtr d = HDictGetDict(pData);
    INT4_8 exists = (d && MLIQMetrics::hdict::key_exists(d, key)) ? 1 : 0;
    HPutElem(proc_handle, 1, &exists, 1, LONG_PAR);
    return H_MSG_TRUE;
}

Herror Hhdict_key_kind(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    HAllocStringMem(proc_handle, 1024);
    Def_INHDictObject(1, pData);

    const std::string key = readString(proc_handle, 2);
    DictPtr d = HDictGetDict(pData);
    const std::string kind =
        d ? MLIQMetrics::hdict::key_kind_name(d, key) : std::string("empty");
    char *s = const_cast<char *>(kind.c_str());
    HPutElem(proc_handle, 1, &s, 1, STRING_PAR);
    return H_MSG_TRUE;
}

Herror Hhdict_remove_key(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    HAllocStringMem(proc_handle, 1024);
    Def_INHDictObject(1, pData);

    const std::string key = readString(proc_handle, 2);
    DictPtr d = HDictGetDict(pData);
    INT4_8 removed = (d && MLIQMetrics::hdict::remove_dict_key(d, key)) ? 1 : 0;
    HPutElem(proc_handle, 1, &removed, 1, LONG_PAR);
    return H_MSG_TRUE;
}

// ============================================================
// json 参数树（hparams_core）
// ============================================================

Herror Hparam_create(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    Def_OUTHParamObject(1, pUserData);
    HParamSetTree(OUTHParamObject(pUserData), std::make_shared<MLIQMetrics::ParamTree>());
    return H_MSG_TRUE;
}

Herror Hload_param(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    HAllocStringMem(proc_handle, 1024);
    Def_INHParamObject(1, pData);

    // 输入控制 2 = Level（字符串元组：一级目录、二级目录、标签……）
    std::vector<std::string> level;
    {
        Hcpar const *pars = nullptr;
        INT4_8 n = 0;
        if (HPGetPPar(proc_handle, 2, &pars, &n) != H_MSG_TRUE) return H__LINE__ * 10000;
        for (INT4_8 i = 0; i < n; ++i) level.push_back(hcparToString(pars[i]));
    }

    // 输入控制 3 = DefaultValue（数值或字符串元组）
    nlohmann::json defaultValue;
    if (!readJsonValue(proc_handle, 3, defaultValue)) return H__LINE__ * 10000;

    // 输入控制 4 = Competence（整数）
    Hcpar competence;
    HGetSPar(proc_handle, 4, LONG_PAR, &competence, 1);

    // 输入控制 5 = Type（字符串）
    const std::string type = readString(proc_handle, 5);

    // 输入控制 6 = Limit（数值或字符串元组，可空）
    nlohmann::json limit;
    if (!readJsonValue(proc_handle, 6, limit)) limit = nlohmann::json::array();

    MLIQMetrics::ParamTreePtr tree = HParamGetTree(pData);
    if (!tree) return H__LINE__ * 10000;

    try
    {
        // ParamTree::load 内部持写锁 ⇒ 多线程共享同一句柄也安全
        const nlohmann::json value =
            tree->load(level, defaultValue, (int)competence.par.l, type, limit);
        return writeJsonValue(proc_handle, 1, value);
    }
    catch (const std::exception &e)
    {
        return failWith(e, H__LINE__);
    }
}

// ---------------------------------------------------------------------------
// json_to_param —— param_to_json 的逆操作（hparams_core 的 ParamTree(json) 构造函数）
//   输入：JSON 串；输出：新建的 HParam 句柄（可继续 load_param 就地补全）
// ---------------------------------------------------------------------------
Herror Hjson_to_param(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    HAllocStringMem(proc_handle, 1024);

    // 输入控制 1 = Json（参数树可能较长：零拷贝取 HALCON 内部字符串指针）
    char const *const *strs = nullptr;
    INT4_8              n    = 0;
    HGetPElemS(proc_handle, 1, CONV_NONE, &strs, &n);
    if (n != 1 || strs == nullptr || strs[0] == nullptr)
    {
        HSetErrText(const_cast<char *>("json_to_param: input Json must be exactly one string"));
        return H__LINE__ * 10000;
    }

    // 先解析：语法错误直接报错（此时还没分配输出句柄）
    nlohmann::json parsed;
    try
    {
        parsed = nlohmann::json::parse(std::string(strs[0]));
    }
    catch (const std::exception &e)
    {
        return failWith(e, H__LINE__);
    }

    Def_OUTHParamObject(1, pUserData);
    HParamSetTree(OUTHParamObject(pUserData),
                  std::make_shared<MLIQMetrics::ParamTree>(std::move(parsed)));
    return H_MSG_TRUE;
}

Herror Hparam_to_json(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    Def_INHParamObject(1, pData);

    MLIQMetrics::ParamTreePtr tree = HParamGetTree(pData);
    if (!tree) return H__LINE__ * 10000;
    const std::string text = tree->to_json();  // 内部持读锁
    char *s = const_cast<char *>(text.c_str());
    HPutElem(proc_handle, 1, &s, 1, STRING_PAR);
    return H_MSG_TRUE;
}
