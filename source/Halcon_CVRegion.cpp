/*=============================================================================
 * Halcon_CVRegion.cpp — HALCON Region 算子（直接调用 cv_region 的 C++ API）
 *
 * 涵盖算子:
 *   集合运算：union1/union2/intersection/difference/complement/symm_difference
 *   形态学：erosion1/dilation1/opening/closing + 预设结构元 circle/rectangle1
 *   连通域：connection；生成：gen_circle/gen_rectangle1；填充：fill_up
 *   特征：area_center/smallest_rectangle1/2/smallest_circle/elliptic_axis/
 *         contlength/circularity/compactness/convexity/rectangularity/
 *         anisometry/bulkiness/structure_factor
 *   形状变换：shape_trans；筛选：select_shape
 *   互转：region_to_bin / bin_to_region
 *
 * 约定:
 *   - supply 过程命名 Hcv_xxx，由 Halcon_SoftwarePackage.c 中 CHcv_xxx 包装
 *   - cv_region 以静态库（cvr_core）直接链入本包，CvrRegion 全程值/引用传递，
 *     无跨 DLL 的句柄创建/销毁与额外 run 数组拷贝
 *   - 自定义错误码 > 10000（R6），出错前 HSetErrText
 *   - 注意：HGetXxx/HNewRegion 等宏是"语句"（内部 return Herror），只能用在
 *     返回 Herror 的函数里；返回指针/bool 的 helper 一律直接调 HP* 函数
 *===========================================================================*/

#include "HalconCpp.h"
#include "HDevThread.h"
#include "Halcon_SoftwarePackage.h"

#include "cvr/cvr_region.hpp"
#include "cvr/cvr_ops.hpp"
#include "cvr/cvr_conn.hpp"
#include "cvr/cvr_morph.hpp"
#include "cvr/cvr_feat.hpp"
#include "cvr/cvr_shape.hpp"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

/* cv_region 自定义错误码段（避免与包内其它模块冲突，使用 10100 起） */
#define CVR_ERR_DOMAIN   10101
#define CVR_ERR_OP       10102
#define CVR_ERR_PARAM    10103

using cvr::CvrRegion;
using cvr::CvrCoord;
using cvr::CvrRun;
using cvr::CvrChords;

namespace {

/* 读取对象 key 的 region 组件到 CvrRegion（失败返回 false） */
inline bool read_region(Hproc_handle proc_handle, Hkey obj_key, CvrRegion& out) {
    Hkey region_key;
    if (HPGetComp(proc_handle, obj_key, REGION, &region_key) != H_MSG_OK)
        return false;
    Hrlregion* hrl = nullptr;
    if (HPGetFRL(proc_handle, region_key, &hrl) != H_MSG_OK || !hrl)
        return false;
    out.runs.clear();
    out.is_compl = (hrl->is_compl != 0);
    out.runs.reserve(static_cast<size_t>(hrl->num));
    for (HITEMCNT i = 0; i < hrl->num; ++i) {
        const Hrun& hr = hrl->rl[i];
        out.runs.push_back(CvrRun{ static_cast<CvrCoord>(hr.l),
                                   static_cast<CvrCoord>(hr.cb),
                                   static_cast<CvrCoord>(hr.ce) });
    }
    cvr::cvr_region_invalidate(out);
    return true;
}

/* 定义域：优先 HGWidth/HGHeight 全局变量（读不到则 has_domain=false）
   注意：这些全局存的是整型，必须用整型缓冲接，不能用 double */
inline void get_domain(Hproc_handle proc_handle, int32_t* w, int32_t* h,
                       bool* has_domain) {
    INT4_8 gw = 0, gh = 0;
    Herror e1 = HAccessGlVar(proc_handle, HGWidth, GV_READ_INFO, &gw,
                             0.0, nullptr, 0, 0);
    Herror e2 = HAccessGlVar(proc_handle, HGHeight, GV_READ_INFO, &gh,
                             0.0, nullptr, 0, 0);
    *w = static_cast<int32_t>(gw);
    *h = static_cast<int32_t>(gh);
    *has_domain = (e1 == H_MSG_OK && e2 == H_MSG_OK && *w > 0 && *h > 0);
    if (!*has_domain) { *w = 0; *h = 0; }
}

/* 无全局定义域时：按输入 region + 结构元 bbox 推导一个足够大的定义域 */
inline void fallback_domain(const CvrRun* runs, size_t n,
                            const CvrRun* se, size_t nse,
                            int32_t* w, int32_t* h) {
    int32_t r2 = 0, c2 = 0;
    for (size_t i = 0; i < n; ++i) {
        if (runs[i].r > r2) r2 = runs[i].r;
        if (runs[i].ce > c2) c2 = runs[i].ce;
    }
    int32_t mr = 0, mc = 0;
    for (size_t i = 0; i < nse; ++i) {
        int32_t ar = se[i].r < 0 ? -se[i].r : se[i].r;
        int32_t ac1 = se[i].cb < 0 ? -se[i].cb : se[i].cb;
        int32_t ac2 = se[i].ce < 0 ? -se[i].ce : se[i].ce;
        if (ar > mr) mr = ar;
        if (ac1 > mc) mc = ac1;
        if (ac2 > mc) mc = ac2;
    }
    *h = r2 + mr + 2;
    *w = c2 + mc + 2;
    if (*h <= 0) *h = 1;
    if (*w <= 0) *w = 1;
}

/* gen_* 的负坐标裁剪（HALCON 行为，与原 C API 的 clip_negative 相同） */
inline void clip_negative(CvrRegion& r) {
    std::vector<CvrRun> kept;
    kept.reserve(r.runs.size());
    for (const auto& rr : r.runs) {
        if (rr.r < 0) continue;
        CvrCoord cb = rr.cb < 0 ? 0 : rr.cb;
        if (cb <= rr.ce) kept.push_back(CvrRun{ rr.r, cb, rr.ce });
    }
    r.runs = std::move(kept);
}

/* region -> byte 掩码图（fg/bg 填充；补集先物化） */
inline bool region_to_buf(const CvrRegion& r, int32_t w, int32_t h,
                          int32_t fg, int32_t bg, uint8_t* buf) {
    std::memset(buf, bg, static_cast<size_t>(w) * static_cast<size_t>(h));
    CvrRegion mat;
    const CvrRegion* pr = &r;
    if (r.is_compl) {
        if (!cvr::cvr_region_materialize(r, w, h, mat)) return false;
        pr = &mat;
    }
    const uint8_t fgv = static_cast<uint8_t>(fg);
    for (const auto& run : pr->runs) {
        if (run.r < 0 || run.r >= h) continue;
        int32_t cb = run.cb < 0 ? 0 : run.cb;
        int32_t ce = run.ce >= w ? w - 1 : run.ce;
        if (cb > ce) continue;
        uint8_t* line = buf + static_cast<size_t>(run.r) * static_cast<size_t>(w);
        for (int32_t c = cb; c <= ce; ++c) line[c] = fgv;
    }
    return true;
}

/* byte 掩码图 -> region（gray >= Threshold 为前景） */
inline bool buf_to_region(const uint8_t* buf, int32_t w, int32_t h,
                          int32_t threshold, CvrRegion& out) {
    out.runs.clear();
    out.is_compl = false;
    for (int32_t r = 0; r < h; ++r) {
        const uint8_t* line = buf + static_cast<size_t>(r) * static_cast<size_t>(w);
        int32_t c = 0;
        while (c < w) {
            while (c < w && line[c] < threshold) ++c;
            if (c >= w) break;
            const int32_t cb = c;
            while (c < w && line[c] >= threshold) ++c;
            out.runs.push_back(CvrRun{ r, cb, c - 1 });
        }
    }
    return cvr::cvr_region_normalize(out);
}

} // namespace

/* 把 CvrRegion 写出为第 1 个输出对象参数（在返回 Herror 的函数里调用） */
static Herror output_region(Hproc_handle proc_handle, const CvrRegion& r) {
    const size_t n = r.runs.size();

    Hrlregion* hrl = nullptr;
    Herror err = HAllocRLNumTmp(proc_handle, &hrl, n);
    if (err != H_MSG_OK) return err;

    hrl->is_compl = r.is_compl ? 1 : 0;
    hrl->num      = static_cast<HITEMCNT>(n);
    hrl->num_max  = static_cast<HITEMCNT>(n);
    for (size_t i = 0; i < n; ++i) {
        hrl->rl[i].l  = static_cast<HIMGCOOR>(r.runs[i].r);
        hrl->rl[i].cb = static_cast<HIMGCOOR>(r.runs[i].cb);
        hrl->rl[i].ce = static_cast<HIMGCOOR>(r.runs[i].ce);
    }

    err = HPNewRegion(proc_handle, hrl);
    HFreeRLTmp(proc_handle, hrl);
    return err;
}

/*=============================================================================
 * 通用 helper：pairwise 二元 region 算子（union2/intersection/difference/...）
 *===========================================================================*/
typedef bool (*CvrBinaryOp)(const CvrRegion&, const CvrRegion&, CvrCoord,
                            CvrCoord, CvrRegion&);

static Herror cvr_binary_op_impl(Hproc_handle proc_handle, CvrBinaryOp op,
                                 const char* op_name) {
    HCkNoObj(proc_handle);

    int32_t w = 0, h = 0;
    bool has_domain = false;
    get_domain(proc_handle, &w, &h, &has_domain);

    INT4_8 n1 = 0, n2 = 0;
    HGetObjNum(proc_handle, 1, &n1);
    HGetObjNum(proc_handle, 2, &n2);

    for (INT4_8 i = 1; i <= n1; ++i) {
        Hkey k1, k2;
        HGetObj(proc_handle, 1, i, &k1);
        HGetObj(proc_handle, 2, (n2 == 1) ? 1 : i, &k2);

        CvrRegion a, b, r;
        if (!read_region(proc_handle, k1, a) ||
            !read_region(proc_handle, k2, b) ||
            !op(a, b, w, h, r)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "%s failed: %s", op_name,
                     cvr::cvr_last_error());
            HSetErrText(msg);
            return CVR_ERR_OP;
        }
        Herror err = output_region(proc_handle, r);
        if (err != H_MSG_OK) return err;
    }
    return H_MSG_TRUE;
}

/*=============================================================================
 * 通用 helper：带结构元的形态学算子（erosion1/dilation1/opening/closing）
 *   op 统一带 iterations 槽；opening/closing 通过适配器忽略之
 *===========================================================================*/
typedef bool (*CvrMorphOp)(const CvrRegion&, const CvrRegion&, int, CvrCoord,
                           CvrCoord, CvrRegion&);

static Herror cvr_morph_se_impl(Hproc_handle proc_handle, CvrMorphOp op,
                                int32_t with_iterations) {
    HCkNoObj(proc_handle);

    int32_t iterations = 1;
    if (with_iterations) {
        Hcpar it_par;
        HGetSPar(proc_handle, 1, LONG_PAR, &it_par, 1);
        iterations = static_cast<int32_t>(it_par.par.l);
        if (iterations < 1) iterations = 1;
    }

    int32_t w = 0, h = 0;
    bool has_domain = false;
    get_domain(proc_handle, &w, &h, &has_domain);

    INT4_8 n1 = 0, n2 = 0;
    HGetObjNum(proc_handle, 1, &n1);
    HGetObjNum(proc_handle, 2, &n2);

    for (INT4_8 i = 1; i <= n1; ++i) {
        Hkey k1, k2;
        HGetObj(proc_handle, 1, i, &k1);
        HGetObj(proc_handle, 2, (n2 == 1) ? 1 : i, &k2);

        CvrRegion r, se;
        if (!read_region(proc_handle, k1, r) ||
            !read_region(proc_handle, k2, se)) {
            HSetErrText(const_cast<char*>("cv morph: failed to read region"));
            return CVR_ERR_PARAM;
        }

        int32_t dw = w, dh = h;
        if (!has_domain) {
            fallback_domain(r.runs.data(), r.runs.size(),
                            se.runs.data(), se.runs.size(),
                            &dw, &dh);
        }

        CvrRegion out;
        if (!op(r, se, iterations, dw, dh, out)) {
            HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
            return CVR_ERR_OP;
        }
        Herror err = output_region(proc_handle, out);
        if (err != H_MSG_OK) return err;
    }
    return H_MSG_TRUE;
}

/*=============================================================================
 * 通用 helper：单值特征 tuple 输出（contlength/circularity/...）
 *===========================================================================*/
typedef bool (*CvrFeatFn)(const CvrRegion&, double*);

static Herror cvr_feature1_impl(Hproc_handle proc_handle, CvrFeatFn fn,
                                const char* feat_name) {
    HCkNoObj(proc_handle);

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    std::vector<double> vals(static_cast<size_t>(n), 0.0);
    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);
        CvrRegion r;
        if (!read_region(proc_handle, k, r) ||
            !fn(r, &vals[static_cast<size_t>(i - 1)])) {
            char msg[256];
            snprintf(msg, sizeof(msg), "%s failed: %s", feat_name,
                     cvr::cvr_last_error());
            HSetErrText(msg);
            return CVR_ERR_OP;
        }
    }

    HPutElem(proc_handle, 1, vals.data(), n, DOUBLE_PAR);
    return H_MSG_TRUE;
}

/* cvr_get_feature 适配器生成 */
#define CVR_FEAT_ADAPTER(fn_name, feat_str)                                   \
    static bool fn_name(const CvrRegion& r, double* v) {                      \
        return cvr::cvr_get_feature(r, feat_str, *v);                         \
    }

CVR_FEAT_ADAPTER(feat_contlength,       "contlength")
CVR_FEAT_ADAPTER(feat_circularity,      "circularity")
CVR_FEAT_ADAPTER(feat_compactness,      "compactness")
CVR_FEAT_ADAPTER(feat_convexity,        "convexity")
CVR_FEAT_ADAPTER(feat_rectangularity,   "rectangularity")
CVR_FEAT_ADAPTER(feat_anisometry,       "anisometry")
CVR_FEAT_ADAPTER(feat_bulkiness,        "bulkiness")
CVR_FEAT_ADAPTER(feat_structure_factor, "structure_factor")

/* opening/closing 无 iterations 参数，适配到统一签名 */
static bool morph_opening(const CvrRegion& r, const CvrRegion& se, int,
                          CvrCoord w, CvrCoord h, CvrRegion& out) {
    return cvr::cvr_opening(r, se, w, h, out);
}
static bool morph_closing(const CvrRegion& r, const CvrRegion& se, int,
                          CvrCoord w, CvrCoord h, CvrRegion& out) {
    return cvr::cvr_closing(r, se, w, h, out);
}

/*=============================================================================
 * cv_union2
 *===========================================================================*/
Herror Hcv_union2(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    int32_t w = 0, h = 0;
    bool has_domain = false;
    get_domain(proc_handle, &w, &h, &has_domain);

    INT4_8 n1 = 0, n2 = 0;
    HGetObjNum(proc_handle, 1, &n1);
    HGetObjNum(proc_handle, 2, &n2);

    for (INT4_8 i = 1; i <= n1; ++i) {
        Hkey k1, k2;
        HGetObj(proc_handle, 1, i, &k1);
        HGetObj(proc_handle, 2, (n2 == 1) ? 1 : i, &k2);

        CvrRegion a, b, r;
        if (!read_region(proc_handle, k1, a) ||
            !read_region(proc_handle, k2, b) ||
            !cvr::cvr_union2(a, b, w, h, r)) {
            HSetErrText(const_cast<char*>("cv_union2 failed"));
            return CVR_ERR_OP;
        }
        Herror err = output_region(proc_handle, r);
        if (err != H_MSG_OK) return err;
    }
    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_intersection
 *===========================================================================*/
Herror Hcv_intersection(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    int32_t w = 0, h = 0;
    bool has_domain = false;
    get_domain(proc_handle, &w, &h, &has_domain);

    INT4_8 n1 = 0, n2 = 0;
    HGetObjNum(proc_handle, 1, &n1);
    HGetObjNum(proc_handle, 2, &n2);

    for (INT4_8 i = 1; i <= n1; ++i) {
        Hkey k1, k2;
        HGetObj(proc_handle, 1, i, &k1);
        HGetObj(proc_handle, 2, (n2 == 1) ? 1 : i, &k2);

        CvrRegion a, b, r;
        if (!read_region(proc_handle, k1, a) ||
            !read_region(proc_handle, k2, b) ||
            !cvr::cvr_intersection(a, b, w, h, r)) {
            HSetErrText(const_cast<char*>("cv_intersection failed"));
            return CVR_ERR_OP;
        }
        Herror err = output_region(proc_handle, r);
        if (err != H_MSG_OK) return err;
    }
    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_erosion1
 *===========================================================================*/
Herror Hcv_erosion1(Hproc_handle proc_handle)
{
    return cvr_morph_se_impl(proc_handle, &cvr::cvr_erosion1, 1);
}

/*=============================================================================
 * cv_connection
 *===========================================================================*/
Herror Hcv_connection(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    int32_t w = 0, h = 0;
    bool has_domain = false;
    get_domain(proc_handle, &w, &h, &has_domain);

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);

        CvrRegion r;
        if (!read_region(proc_handle, k, r)) {
            HSetErrText(const_cast<char*>("cv_connection: failed to read region"));
            return CVR_ERR_PARAM;
        }

        std::vector<CvrRegion> comps;
        if (!cvr::cvr_connection(r, 8, w, h, comps)) {
            HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
            return CVR_ERR_OP;
        }

        for (size_t c = 0; c < comps.size(); ++c) {
            Herror err = output_region(proc_handle, comps[c]);
            if (err != H_MSG_OK) return err;
        }
    }
    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_select_shape —— 输出为输入子集（HCopyObj 引用语义）
 *===========================================================================*/
Herror Hcv_select_shape(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    /* 读取字符串参数前必须分配字符串内存（手册 5.5.10），否则访问违例 */
    HAllocStringMem(proc_handle, 1024);

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    /* 控制参数: 1=Features(string tuple) 2=Operation(string) 3=Min 4=Max */
    char const* const* feat_ptr = nullptr;
    INT4_8 n_feat = 0;
    HGetPElemS(proc_handle, 1, CONV_NONE, &feat_ptr, &n_feat);
    if (n_feat <= 0) {
        HSetErrText(const_cast<char*>("cv_select_shape: Features is empty"));
        return CVR_ERR_PARAM;
    }

    Hcpar op_par;
    HGetSPar(proc_handle, 2, STRING_PAR, &op_par, 1);

    std::vector<double> vmin(static_cast<size_t>(n_feat), 0.0);
    std::vector<double> vmax(static_cast<size_t>(n_feat), 1e30);
    {
        INT4_8 num_min = 0, num_max = 0;
        HGetCParNum(proc_handle, 3, &num_min);
        HGetCParNum(proc_handle, 4, &num_max);
        if (num_min > 0) {
            std::vector<Hcpar> vals(static_cast<size_t>(num_min));
            INT4_8 got = 0;
            HGetCPar(proc_handle, 3, DOUBLE_PAR, vals.data(), num_min, num_min,
                     &got);
            for (INT4_8 j = 0; j < n_feat && got > 0; ++j)
                vmin[static_cast<size_t>(j)] =
                    vals[static_cast<size_t>(j < got ? j : got - 1)].par.d;
        }
        if (num_max > 0) {
            std::vector<Hcpar> vals(static_cast<size_t>(num_max));
            INT4_8 got = 0;
            HGetCPar(proc_handle, 4, DOUBLE_PAR, vals.data(), num_max, num_max,
                     &got);
            for (INT4_8 j = 0; j < n_feat && got > 0; ++j)
                vmax[static_cast<size_t>(j)] =
                    vals[static_cast<size_t>(j < got ? j : got - 1)].par.d;
        }
    }

    /* 全部输入 region -> CvrRegion 数组（保持原 C API 的索引+引用语义） */
    std::vector<CvrRegion> regs(static_cast<size_t>(n));
    std::vector<Hkey>      obj_keys(static_cast<size_t>(n));
    for (INT4_8 i = 1; i <= n; ++i) {
        HGetObj(proc_handle, 1, i, &obj_keys[static_cast<size_t>(i - 1)]);
        if (!read_region(proc_handle, obj_keys[static_cast<size_t>(i - 1)],
                         regs[static_cast<size_t>(i - 1)])) {
            HSetErrText(const_cast<char*>("cv_select_shape: failed to read region"));
            return CVR_ERR_PARAM;
        }
    }

    std::vector<std::string> feats(static_cast<size_t>(n_feat));
    for (INT4_8 f = 0; f < n_feat; ++f)
        feats[static_cast<size_t>(f)] = feat_ptr[f] ? feat_ptr[f] : "";

    const char* op_str = op_par.par.s ? op_par.par.s : "and";
    const bool is_or = (std::strcmp(op_str, "or") == 0);

    for (INT4_8 i = 1; i <= n; ++i) {
        bool pass = !is_or;
        for (INT4_8 f = 0; f < n_feat; ++f) {
            double v = 0.0;
            if (!cvr::cvr_get_feature(regs[static_cast<size_t>(i - 1)],
                                      feats[static_cast<size_t>(f)], v)) {
                HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
                return CVR_ERR_OP;
            }
            const bool ok = (v >= vmin[static_cast<size_t>(f)] &&
                             v <= vmax[static_cast<size_t>(f)]);
            if (is_or) { if (ok) { pass = true; break; } }
            else       { if (!ok) { pass = false; break; } }
        }
        if (pass) {
            Hkey out_key;
            HCopyObj(proc_handle, obj_keys[static_cast<size_t>(i - 1)], 1,
                     &out_key);
        }
    }
    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_union1 —— 所有输入区域求并
 *===========================================================================*/
Herror Hcv_union1(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    int32_t w = 0, h = 0;
    bool has_domain = false;
    get_domain(proc_handle, &w, &h, &has_domain);

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    std::vector<CvrRegion> regs(static_cast<size_t>(n));
    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);
        if (!read_region(proc_handle, k, regs[static_cast<size_t>(i - 1)])) {
            HSetErrText(const_cast<char*>("cv_union1: failed to read region"));
            return CVR_ERR_PARAM;
        }
    }

    CvrRegion out;
    if (!cvr::cvr_union1(regs, w, h, out)) {
        HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
        return CVR_ERR_OP;
    }

    return output_region(proc_handle, out);
}

/*=============================================================================
 * cv_difference / cv_complement / cv_symm_difference
 *===========================================================================*/
Herror Hcv_difference(Hproc_handle proc_handle)
{
    return cvr_binary_op_impl(proc_handle, &cvr::cvr_difference, "cv_difference");
}

Herror Hcv_symm_difference(Hproc_handle proc_handle)
{
    return cvr_binary_op_impl(proc_handle, &cvr::cvr_symm_difference,
                              "cv_symm_difference");
}

Herror Hcv_complement(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);

        CvrRegion r, out;
        if (!read_region(proc_handle, k, r) ||
            !cvr::cvr_complement(r, out)) {
            HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
            return CVR_ERR_OP;
        }
        Herror err = output_region(proc_handle, out);
        if (err != H_MSG_OK) return err;
    }
    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_dilation1 / cv_opening / cv_closing
 *===========================================================================*/
Herror Hcv_dilation1(Hproc_handle proc_handle)
{
    return cvr_morph_se_impl(proc_handle, &cvr::cvr_dilation1, 1);
}

Herror Hcv_opening(Hproc_handle proc_handle)
{
    return cvr_morph_se_impl(proc_handle, &morph_opening, 0);
}

Herror Hcv_closing(Hproc_handle proc_handle)
{
    return cvr_morph_se_impl(proc_handle, &morph_closing, 0);
}

/*=============================================================================
 * cv_fill_up —— 填充孔洞
 *===========================================================================*/
Herror Hcv_fill_up(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    int32_t w = 0, h = 0;
    bool has_domain = false;
    get_domain(proc_handle, &w, &h, &has_domain);

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);

        CvrRegion r;
        if (!read_region(proc_handle, k, r)) {
            HSetErrText(const_cast<char*>("cv_fill_up: failed to read region"));
            return CVR_ERR_PARAM;
        }

        int32_t dw = w, dh = h;
        if (!has_domain) {
            /* 无全局定义域：用输入 bbox（孔洞由定义保证不触边） */
            for (size_t j = 0; j < r.runs.size(); ++j) {
                if (r.runs[j].r + 1 > dh) dh = r.runs[j].r + 1;
                if (r.runs[j].ce + 1 > dw) dw = r.runs[j].ce + 1;
            }
            if (dw <= 0) dw = 1;
            if (dh <= 0) dh = 1;
        }

        CvrRegion out;
        if (!cvr::cvr_fill_up(r, dw, dh, out)) {
            HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
            return CVR_ERR_OP;
        }
        Herror err = output_region(proc_handle, out);
        if (err != H_MSG_OK) return err;
    }
    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_gen_circle / cv_gen_rectangle1 —— 无输入对象，不调 HCkNoObj
 *===========================================================================*/
Herror Hcv_gen_circle(Hproc_handle proc_handle)
{
    Hcpar row_par, col_par, rad_par;
    HGetSPar(proc_handle, 1, DOUBLE_PAR, &row_par, 1);
    HGetSPar(proc_handle, 2, DOUBLE_PAR, &col_par, 1);
    HGetSPar(proc_handle, 3, DOUBLE_PAR, &rad_par, 1);

    if (rad_par.par.d < 0.0) {
        HSetErrText(const_cast<char*>("cv_gen_circle: invalid parameters"));
        return CVR_ERR_PARAM;
    }
    CvrRegion out = cvr::cvr_gen_circle(
        static_cast<CvrCoord>(std::lround(row_par.par.d)),
        static_cast<CvrCoord>(std::lround(col_par.par.d)),
        rad_par.par.d);
    clip_negative(out);
    if (!cvr::cvr_region_normalize(out)) {
        HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
        return CVR_ERR_OP;
    }
    return output_region(proc_handle, out);
}

Herror Hcv_gen_rectangle1(Hproc_handle proc_handle)
{
    Hcpar r1, c1, r2, c2;
    HGetSPar(proc_handle, 1, LONG_PAR, &r1, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &c1, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &r2, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &c2, 1);

    CvrRegion out = cvr::cvr_gen_rectangle1(
        static_cast<CvrCoord>(r1.par.l), static_cast<CvrCoord>(c1.par.l),
        static_cast<CvrCoord>(r2.par.l), static_cast<CvrCoord>(c2.par.l));
    clip_negative(out);
    if (!cvr::cvr_region_normalize(out)) {
        HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
        return CVR_ERR_OP;
    }
    return output_region(proc_handle, out);
}

/*=============================================================================
 * 特征算子（tuple 输出）
 *===========================================================================*/
Herror Hcv_area_center(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    std::vector<double> area(static_cast<size_t>(n), 0.0);
    std::vector<double> row(static_cast<size_t>(n), 0.0);
    std::vector<double> col(static_cast<size_t>(n), 0.0);

    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);
        CvrRegion r;
        const size_t j = static_cast<size_t>(i - 1);
        CvrChords a = 0;
        if (!read_region(proc_handle, k, r) ||
            !cvr::cvr_feature_area_center(r, row[j], col[j], a)) {
            HSetErrText(const_cast<char*>("cv_area_center failed"));
            return CVR_ERR_OP;
        }
        area[j] = static_cast<double>(a);
    }

    HPutElem(proc_handle, 1, area.data(), n, DOUBLE_PAR);
    HPutElem(proc_handle, 2, row.data(),  n, DOUBLE_PAR);
    HPutElem(proc_handle, 3, col.data(),  n, DOUBLE_PAR);
    return H_MSG_TRUE;
}

Herror Hcv_smallest_rectangle1(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    std::vector<double> r1(static_cast<size_t>(n)), c1(static_cast<size_t>(n));
    std::vector<double> r2(static_cast<size_t>(n)), c2(static_cast<size_t>(n));

    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);
        CvrRegion r;
        const size_t j = static_cast<size_t>(i - 1);
        CvrCoord vr1 = 0, vc1 = 0, vr2 = 0, vc2 = 0;
        if (!read_region(proc_handle, k, r) ||
            !cvr::cvr_feature_smallest_rectangle1(r, vr1, vc1, vr2, vc2)) {
            HSetErrText(const_cast<char*>("cv_smallest_rectangle1 failed"));
            return CVR_ERR_OP;
        }
        r1[j] = static_cast<double>(vr1);
        c1[j] = static_cast<double>(vc1);
        r2[j] = static_cast<double>(vr2);
        c2[j] = static_cast<double>(vc2);
    }

    HPutElem(proc_handle, 1, r1.data(), n, DOUBLE_PAR);
    HPutElem(proc_handle, 2, c1.data(), n, DOUBLE_PAR);
    HPutElem(proc_handle, 3, r2.data(), n, DOUBLE_PAR);
    HPutElem(proc_handle, 4, c2.data(), n, DOUBLE_PAR);
    return H_MSG_TRUE;
}

Herror Hcv_smallest_rectangle2(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    std::vector<double> row(static_cast<size_t>(n)), col(static_cast<size_t>(n));
    std::vector<double> phi(static_cast<size_t>(n));
    std::vector<double> l1(static_cast<size_t>(n)),  l2(static_cast<size_t>(n));

    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);
        CvrRegion r;
        const size_t j = static_cast<size_t>(i - 1);
        if (!read_region(proc_handle, k, r) ||
            !cvr::cvr_feature_smallest_rectangle2(r, row[j], col[j], phi[j],
                                                  l1[j], l2[j])) {
            HSetErrText(const_cast<char*>("cv_smallest_rectangle2 failed"));
            return CVR_ERR_OP;
        }
    }

    HPutElem(proc_handle, 1, row.data(), n, DOUBLE_PAR);
    HPutElem(proc_handle, 2, col.data(), n, DOUBLE_PAR);
    HPutElem(proc_handle, 3, phi.data(), n, DOUBLE_PAR);
    HPutElem(proc_handle, 4, l1.data(),  n, DOUBLE_PAR);
    HPutElem(proc_handle, 5, l2.data(),  n, DOUBLE_PAR);
    return H_MSG_TRUE;
}

Herror Hcv_smallest_circle(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    std::vector<double> row(static_cast<size_t>(n)), col(static_cast<size_t>(n));
    std::vector<double> radius(static_cast<size_t>(n));

    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);
        CvrRegion r;
        const size_t j = static_cast<size_t>(i - 1);
        if (!read_region(proc_handle, k, r) ||
            !cvr::cvr_feature_smallest_circle(r, row[j], col[j], radius[j])) {
            HSetErrText(const_cast<char*>("cv_smallest_circle failed"));
            return CVR_ERR_OP;
        }
    }

    HPutElem(proc_handle, 1, row.data(),    n, DOUBLE_PAR);
    HPutElem(proc_handle, 2, col.data(),    n, DOUBLE_PAR);
    HPutElem(proc_handle, 3, radius.data(), n, DOUBLE_PAR);
    return H_MSG_TRUE;
}

Herror Hcv_elliptic_axis(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    std::vector<double> ra(static_cast<size_t>(n)), rb(static_cast<size_t>(n));
    std::vector<double> phi(static_cast<size_t>(n));

    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);
        CvrRegion r;
        const size_t j = static_cast<size_t>(i - 1);
        if (!read_region(proc_handle, k, r) ||
            !cvr::cvr_feature_elliptic_axis(r, ra[j], rb[j], phi[j])) {
            HSetErrText(const_cast<char*>("cv_elliptic_axis failed"));
            return CVR_ERR_OP;
        }
    }

    HPutElem(proc_handle, 1, ra.data(),  n, DOUBLE_PAR);
    HPutElem(proc_handle, 2, rb.data(),  n, DOUBLE_PAR);
    HPutElem(proc_handle, 3, phi.data(), n, DOUBLE_PAR);
    return H_MSG_TRUE;
}

/* 单值特征：统一走 cvr_feature1_impl */
Herror Hcv_contlength(Hproc_handle proc_handle)
{ return cvr_feature1_impl(proc_handle, &feat_contlength, "cv_contlength"); }

Herror Hcv_circularity(Hproc_handle proc_handle)
{ return cvr_feature1_impl(proc_handle, &feat_circularity, "cv_circularity"); }

Herror Hcv_compactness(Hproc_handle proc_handle)
{ return cvr_feature1_impl(proc_handle, &feat_compactness, "cv_compactness"); }

Herror Hcv_convexity(Hproc_handle proc_handle)
{ return cvr_feature1_impl(proc_handle, &feat_convexity, "cv_convexity"); }

Herror Hcv_rectangularity(Hproc_handle proc_handle)
{ return cvr_feature1_impl(proc_handle, &feat_rectangularity, "cv_rectangularity"); }

Herror Hcv_anisometry(Hproc_handle proc_handle)
{ return cvr_feature1_impl(proc_handle, &feat_anisometry, "cv_anisometry"); }

Herror Hcv_bulkiness(Hproc_handle proc_handle)
{ return cvr_feature1_impl(proc_handle, &feat_bulkiness, "cv_bulkiness"); }

Herror Hcv_structure_factor(Hproc_handle proc_handle)
{ return cvr_feature1_impl(proc_handle, &feat_structure_factor, "cv_structure_factor"); }

/*=============================================================================
 * cv_shape_trans
 *===========================================================================*/
Herror Hcv_shape_trans(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    HAllocStringMem(proc_handle, 256);

    Hcpar type_par;
    HGetSPar(proc_handle, 1, STRING_PAR, &type_par, 1);
    const char* type = type_par.par.s ? type_par.par.s : "convex";

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);

        CvrRegion r, out;
        if (!read_region(proc_handle, k, r) ||
            !cvr::cvr_shape_trans(r, type, out)) {
            HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
            return CVR_ERR_OP;
        }
        Herror err = output_region(proc_handle, out);
        if (err != H_MSG_OK) return err;
    }
    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_region_to_bin —— region -> byte 二值图（0/255 或自定义 fg/bg）
 *===========================================================================*/
Herror Hcv_region_to_bin(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    Hcpar fg_par, bg_par, w_par, h_par;
    HGetSPar(proc_handle, 1, LONG_PAR, &fg_par, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &bg_par, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &w_par, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &h_par, 1);

    const int32_t fg = static_cast<int32_t>(fg_par.par.l);
    const int32_t bg = static_cast<int32_t>(bg_par.par.l);
    int32_t w = static_cast<int32_t>(w_par.par.l);
    int32_t h = static_cast<int32_t>(h_par.par.l);

    if (fg < 0 || fg > 255 || bg < 0 || bg > 255) {
        HSetErrText(const_cast<char*>("cv_region_to_bin: gray value out of range"));
        return CVR_ERR_PARAM;
    }

    /* Width/Height = 0 时自动推导：全局定义域，否则输入 bbox */
    if (w <= 0 || h <= 0) {
        int32_t gw = 0, gh = 0;
        bool has_domain = false;
        get_domain(proc_handle, &gw, &gh, &has_domain);
        if (has_domain) {
            if (w <= 0) w = gw;
            if (h <= 0) h = gh;
        }
    }

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);

        CvrRegion r;
        if (!read_region(proc_handle, k, r)) {
            HSetErrText(const_cast<char*>("cv_region_to_bin: failed to read region"));
            return CVR_ERR_PARAM;
        }

        int32_t dw = w, dh = h;
        if (dw <= 0 || dh <= 0) {
            for (size_t j = 0; j < r.runs.size(); ++j) {
                if (r.runs[j].r + 1 > dh) dh = r.runs[j].r + 1;
                if (r.runs[j].ce + 1 > dw) dw = r.runs[j].ce + 1;
            }
            if (dw <= 0) dw = 1;
            if (dh <= 0) dh = 1;
        }

        std::vector<uint8_t> buf(static_cast<size_t>(dw) * static_cast<size_t>(dh), 0);
        if (!region_to_buf(r, dw, dh, fg, bg, buf.data())) {
            HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
            return CVR_ERR_OP;
        }

        Himage outimage;
        HCkP(HNewImage(proc_handle, &outimage, BYTE_IMAGE,
                       static_cast<HIMGDIM>(dw), static_cast<HIMGDIM>(dh)));
        memcpy(outimage.pixel.b, buf.data(), buf.size());

        Hkey out_obj_key, out_image_key;
        HCrObj(proc_handle, 1, &out_obj_key);
        HPutDImage(proc_handle, out_obj_key, 1, &outimage, TRUE, &out_image_key);
        HPutRect(proc_handle, out_obj_key, dw, dh);
    }
    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_bin_to_region —— byte 二值图 -> region（gray >= Threshold）
 *===========================================================================*/
Herror Hcv_bin_to_region(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    Hcpar th_par;
    HGetSPar(proc_handle, 1, LONG_PAR, &th_par, 1);
    const int32_t threshold = static_cast<int32_t>(th_par.par.l);
    if (threshold < 0 || threshold > 255) {
        HSetErrText(const_cast<char*>("cv_bin_to_region: Threshold out of range"));
        return CVR_ERR_PARAM;
    }

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey obj_key;
        HGetObj(proc_handle, 1, i, &obj_key);

        Himage img;
        HGetDImage(proc_handle, obj_key, 1, &img);
        if (img.kind != BYTE_IMAGE) {
            HSetErrText(const_cast<char*>(
                "cv_bin_to_region: only byte images supported"));
            return CVR_ERR_PARAM;
        }

        CvrRegion out;
        if (!buf_to_region(img.pixel.b, static_cast<int32_t>(img.width),
                           static_cast<int32_t>(img.height), threshold, out)) {
            HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
            return CVR_ERR_OP;
        }
        Herror err = output_region(proc_handle, out);
        if (err != H_MSG_OK) return err;
    }
    return H_MSG_TRUE;
}

/*=============================================================================
 * 预设结构元形态学通用实现：cv_erosion_circle / cv_dilation_circle /
 *   cv_erosion_rectangle1 / cv_dilation_rectangle1
 *   op 签名：(r, p1, p2, w, h, out)；circle 用 p1=Radius(double)，
 *   rectangle 用 p1=Width, p2=Height(int32)
 *===========================================================================*/
typedef bool (*CvrPresetMorphOp)(const CvrRegion&, double, int32_t,
                                 CvrCoord, CvrCoord, CvrRegion&);

static bool preset_erosion_circle(const CvrRegion& r, double p1, int32_t,
                                  CvrCoord w, CvrCoord h, CvrRegion& out) {
    if (p1 < 0.0) return false;
    CvrRegion se = cvr::cvr_gen_circle(0, 0, p1);
    return cvr::cvr_erosion1(r, se, 1, w, h, out);
}

static bool preset_dilation_circle(const CvrRegion& r, double p1, int32_t,
                                   CvrCoord w, CvrCoord h, CvrRegion& out) {
    if (p1 < 0.0) return false;
    CvrRegion se = cvr::cvr_gen_circle(0, 0, p1);
    return cvr::cvr_dilation1(r, se, 1, w, h, out);
}

static bool preset_erosion_rect(const CvrRegion& r, double p1, int32_t p2,
                                CvrCoord w, CvrCoord h, CvrRegion& out) {
    CvrRegion se = cvr::cvr_se_rect(static_cast<int32_t>(p1), p2);
    return cvr::cvr_erosion1(r, se, 1, w, h, out);
}

static bool preset_dilation_rect(const CvrRegion& r, double p1, int32_t p2,
                                 CvrCoord w, CvrCoord h, CvrRegion& out) {
    CvrRegion se = cvr::cvr_se_rect(static_cast<int32_t>(p1), p2);
    return cvr::cvr_dilation1(r, se, 1, w, h, out);
}

static Herror cvr_preset_morph_impl(Hproc_handle proc_handle,
                                    CvrPresetMorphOp op, bool two_int_params) {
    HCkNoObj(proc_handle);

    double p1 = 0.0;
    int32_t p2 = 0;
    if (two_int_params) {
        Hcpar w_par, h_par;
        HGetSPar(proc_handle, 1, LONG_PAR, &w_par, 1);
        HGetSPar(proc_handle, 2, LONG_PAR, &h_par, 1);
        p1 = static_cast<double>(static_cast<int32_t>(w_par.par.l));
        p2 = static_cast<int32_t>(h_par.par.l);
        if (p1 <= 0 || p2 <= 0) {
            HSetErrText(const_cast<char*>("Width/Height must be > 0"));
            return CVR_ERR_PARAM;
        }
    } else {
        Hcpar rad_par;
        HGetSPar(proc_handle, 1, DOUBLE_PAR, &rad_par, 1);
        p1 = rad_par.par.d;
        if (p1 < 0.0) {
            HSetErrText(const_cast<char*>("Radius must be >= 0"));
            return CVR_ERR_PARAM;
        }
    }

    int32_t w = 0, h = 0;
    bool has_domain = false;
    get_domain(proc_handle, &w, &h, &has_domain);

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);

        CvrRegion r;
        if (!read_region(proc_handle, k, r)) {
            HSetErrText(const_cast<char*>("preset morph: failed to read region"));
            return CVR_ERR_PARAM;
        }

        int32_t dw = w, dh = h;
        if (!has_domain) {
            /* 无全局定义域：bbox + 结构元尺度外扩 */
            const int32_t margin_r = two_int_params ? p2 : (int32_t)std::ceil(p1);
            const int32_t margin_c = two_int_params ? (int32_t)p1 : (int32_t)std::ceil(p1);
            int32_t r2 = 0, c2 = 0;
            for (size_t j = 0; j < r.runs.size(); ++j) {
                if (r.runs[j].r > r2)  r2 = r.runs[j].r;
                if (r.runs[j].ce > c2) c2 = r.runs[j].ce;
            }
            dh = r2 + margin_r + 2;
            dw = c2 + margin_c + 2;
            if (dw <= 0) dw = 1;
            if (dh <= 0) dh = 1;
        }

        CvrRegion out;
        if (!op(r, p1, p2, dw, dh, out)) {
            HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
            return CVR_ERR_OP;
        }
        Herror err = output_region(proc_handle, out);
        if (err != H_MSG_OK) return err;
    }
    return H_MSG_TRUE;
}

Herror Hcv_erosion_circle(Hproc_handle proc_handle)
{ return cvr_preset_morph_impl(proc_handle, &preset_erosion_circle, false); }

Herror Hcv_dilation_circle(Hproc_handle proc_handle)
{ return cvr_preset_morph_impl(proc_handle, &preset_dilation_circle, false); }

Herror Hcv_erosion_rectangle1(Hproc_handle proc_handle)
{ return cvr_preset_morph_impl(proc_handle, &preset_erosion_rect, true); }

Herror Hcv_dilation_rectangle1(Hproc_handle proc_handle)
{ return cvr_preset_morph_impl(proc_handle, &preset_dilation_rect, true); }
