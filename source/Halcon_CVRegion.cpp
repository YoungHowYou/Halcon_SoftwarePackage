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

#include "cvr/cvr.hpp"
#include "cvflow/parallel_for.hpp"

#include <thread>

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <deque>
#include <mutex>
#include <unordered_map>

/* cv_region 自定义错误码段（避免与包内其它模块冲突，使用 10100 起） */
#define CVR_ERR_DOMAIN   10101
#define CVR_ERR_OP       10102
#define CVR_ERR_PARAM    10103

using cvr::CvrRegion;
using cvr::CvrCoord;
using cvr::CvrRun;
using cvr::CvrChords;

namespace {

/*=============================================================================
 * 跨算子特征缓存（内容指纹）
 *
 *   HALCON 的 region 对象无法携带 CvrRegion 里的惰性特征缓存：每个算子调用都从
 *   Hrlregion 重建 CvrRegion 并 invalidate（避开陈旧值）。为把"算过的特征"跨算子
 *   复用（如 connection → select_shape → region_features），这里按内容指纹缓存：
 *   指纹只覆盖影响特征值的量（runs + is_compl）；所有 cvr_feature_* 都不依赖
 *   定义域 w/h，因此不存在"同 key 不同值"的风险，最差情况只是未命中。
 *
 *   用法：特征类算子读 region 时传 &key（read_region 顺带算指纹，该循环本就
 *   遍历全部 run 做 int16→int32 扩宽），命中即把缓存拷回 out.feature；算完后
 *   feature_cache_store 回写。集合/形态学等算子不参与，零额外开销。
 *===========================================================================*/
namespace {

struct FeatureCacheKey {
    uint64_t h1 = 0, h2 = 0;
    bool operator==(const FeatureCacheKey& o) const noexcept {
        return h1 == o.h1 && h2 == o.h2;
    }
};

struct FeatureCacheKeyHash {
    size_t operator()(const FeatureCacheKey& k) const noexcept {
        return static_cast<size_t>(k.h1 ^ (k.h2 + 0x9e3779b97f4a7c15ull +
                                           (k.h1 << 6) + (k.h1 >> 2)));
    }
};

/* 双累加器（FNV-1a + LCG 混合）=> 128 位指纹，碰撞概率可忽略 */
inline void feature_hash_mix(uint64_t& h1, uint64_t& h2, uint64_t v) noexcept {
    h1 ^= v;
    h1 *= 1099511628211ull;
    h2 = h2 * 6364136223846793005ull + v + 1442695040888963407ull;
}

inline FeatureCacheKey feature_key_of(const cvr::CvrRun* runs, size_t n,
                                      bool is_compl) noexcept {
    FeatureCacheKey k;
    k.h1 = 1469598103934665603ull;
    k.h2 = 0x9e3779b97f4a7c15ull;
    feature_hash_mix(k.h1, k.h2, static_cast<uint64_t>(n));
    for (size_t i = 0; i < n; ++i) {
        const uint64_t r  = static_cast<uint64_t>(static_cast<uint32_t>(runs[i].r));
        const uint64_t cb = static_cast<uint64_t>(static_cast<uint32_t>(runs[i].cb));
        const uint64_t ce = static_cast<uint64_t>(static_cast<uint32_t>(runs[i].ce));
        feature_hash_mix(k.h1, k.h2, r);
        feature_hash_mix(k.h1, k.h2, cb);
        feature_hash_mix(k.h1, k.h2, ce);
    }
    feature_hash_mix(k.h1, k.h2, is_compl ? 1ull : 0ull);
    return k;
}

/* 分片 + 每片 FIFO 上限（防长跑会话无限增长）；条目仅特征值（~300B/region）。
 * 注意上限的选取：实测 connection→select_shape→region_features 流水线会在一次
 * 调用内写入 20 万+ 唯一 key，512 的 per-shard 上限会让同一次调用把 97% 的条目
 * 挤掉、下一次调用基本全 miss（缓存形同虚设）。改 65536/分片（合计 ~100 万条，
 * 极端峰值 ~350MB 随 FIFO 回收），覆盖该量级并保留防泄漏上限。 */
struct FeatureCacheShard {
    static constexpr size_t kCap = 65536;
    std::mutex                                     mtx;
    std::unordered_map<FeatureCacheKey, cvr::CvrFeature, FeatureCacheKeyHash> map;
    std::deque<FeatureCacheKey>                    fifo;
};

constexpr size_t kFeatureCacheShards = 16;

inline std::array<FeatureCacheShard, kFeatureCacheShards>& feature_cache() noexcept {
    /* 函数内 static：进程内单例，首次使用时构造 */
    static std::array<FeatureCacheShard, kFeatureCacheShards> s;
    return s;
}

inline bool feature_cache_lookup(const FeatureCacheKey& k, cvr::CvrFeature& out) {
    FeatureCacheShard& sh = feature_cache()[static_cast<size_t>(k.h1 % kFeatureCacheShards)];
    std::lock_guard<std::mutex> lk(sh.mtx);
    const auto it = sh.map.find(k);
    if (it == sh.map.end()) return false;
    out = it->second;
    return true;
}

/* 只缓存"计算代价高"的特征：实测（28 万唯一内容 region）全量回写时 store 占
 * 算子耗时 ~40%（~70ms），远超重新计算便宜特征的成本。判据：只要含任一非
 * 平凡特征（hull/Welzl/卡壳/矩/轮廓类）就回写；只含 center_area（面积/重心）
 * 或 bbox（rectangle1）这类 O(runs) 秒算的则跳过——它们重算比回写还快。 */
inline bool feature_worth_caching(const cvr::CvrFeature& f) noexcept {
    return f.flags.circularity        || f.flags.compactness       ||
           f.flags.contlength         || f.flags.convexity         ||
           f.flags.phi                || f.flags.elliptic_axis     ||
           f.flags.elliptic_shape     || f.flags.excentricity      ||
           f.flags.moments            || f.flags.smallest_rectangle2 ||
           f.flags.smallest_circle    || f.flags.min_max_chord     ||
           f.flags.min_max_chord_gap  || f.flags.rectangularity    ||
           f.flags.is_convex;
}

inline void feature_cache_store(const FeatureCacheKey& k, const cvr::CvrFeature& f) {
    if (!f.flags.any() || !feature_worth_caching(f)) return;
    FeatureCacheShard& sh = feature_cache()[static_cast<size_t>(k.h1 % kFeatureCacheShards)];
    std::lock_guard<std::mutex> lk(sh.mtx);
    const auto r = sh.map.emplace(k, f);
    if (r.second) {
        sh.fifo.push_back(k);
        while (sh.fifo.size() > FeatureCacheShard::kCap) {
            sh.map.erase(sh.fifo.front());
            sh.fifo.pop_front();
        }
    } else {
        r.first->second = f;   /* 已存在：用后算的（信息更全）覆盖 */
    }
}

} // namespace

/* 读取对象 key 的 region 组件到 CvrRegion（失败返回 false）。
   注意 Hrun 的坐标是 int16（HC_LARGE_IMAGES 未定义时）而 CvrRun 是 int32，
   必须逐字段扩宽；用 resize + 下标直写替代 push_back（免每次容量/大小检查）。
   key != nullptr 时顺带算内容指纹并在命中时水合特征缓存（见上文）。 */
inline bool read_region(Hproc_handle proc_handle, Hkey obj_key, CvrRegion& out,
                        FeatureCacheKey* key = nullptr) {
    Hkey region_key;
    if (HPGetComp(proc_handle, obj_key, REGION, &region_key) != H_MSG_OK)
        return false;
    Hrlregion* hrl = nullptr;
    if (HPGetFRL(proc_handle, region_key, &hrl) != H_MSG_OK || !hrl)
        return false;
    const size_t n = static_cast<size_t>(hrl->num);
    out.runs.resize(n);
    const Hrun* src = hrl->rl;
    cvr::CvrRun* dst = out.runs.data();
    for (size_t i = 0; i < n; ++i) {
        dst[i].r  = static_cast<cvr::CvrCoord>(src[i].l);
        dst[i].cb = static_cast<cvr::CvrCoord>(src[i].cb);
        dst[i].ce = static_cast<cvr::CvrCoord>(src[i].ce);
    }
    out.is_compl = (hrl->is_compl != 0);
    cvr::cvr_region_invalidate(out);
    if (key) {
        *key = feature_key_of(out.runs.data(), out.runs.size(), out.is_compl);
        /* 命中：把之前算过的特征挂回本次打开的对象（后续 cvr_feature_* 直接查表） */
        feature_cache_lookup(*key, out.feature);
    }
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
    cvr::cvr_region_invalidate(r);   /* runs 已变：惰性特征缓存必须失效 */
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

/* 掩码图 -> region（gray >= Threshold 为前景）；T 为像素类型 */
template <typename T>
inline bool buf_to_region_impl(const T* buf, int32_t w, int32_t h,
                               double threshold, CvrRegion& out) {
    out.runs.clear();
    out.is_compl = false;
    for (int32_t r = 0; r < h; ++r) {
        const T* line = buf + static_cast<size_t>(r) * static_cast<size_t>(w);
        int32_t c = 0;
        while (c < w) {
            while (c < w && static_cast<double>(line[c]) < threshold) ++c;
            if (c >= w) break;
            const int32_t cb = c;
            while (c < w && static_cast<double>(line[c]) >= threshold) ++c;
            out.runs.push_back(CvrRun{ r, cb, c - 1 });
        }
    }
    return cvr::cvr_region_normalize(out);
}

/* image_to_region 的分派结果 */
enum CvrImgToRegion
{
    CVR_I2R_OK = 0,          /* 成功 */
    CVR_I2R_UNSUPPORTED = 1, /* 非数值类型图像 */
    CVR_I2R_FAIL = 2         /* 核心库失败 */
};

/* 任意数值类型单通道图像 -> region（直接比较灰度与阈值，语义同 HALCON threshold 的下界包含） */
inline int image_to_region(const Himage& img, double threshold, CvrRegion& out) {
    const int32_t w = static_cast<int32_t>(img.width);
    const int32_t h = static_cast<int32_t>(img.height);
    if (w <= 0 || h <= 0) return CVR_I2R_FAIL;

    switch (img.kind) {
    case BYTE_IMAGE:
        return buf_to_region_impl(img.pixel.b, w, h, threshold, out)
                   ? CVR_I2R_OK : CVR_I2R_FAIL;
    case INT1_IMAGE:
        return buf_to_region_impl(img.pixel.i, w, h, threshold, out)
                   ? CVR_I2R_OK : CVR_I2R_FAIL;
    case INT2_IMAGE:
        if (!img.pixel.s.p) return CVR_I2R_FAIL;
        return buf_to_region_impl(img.pixel.s.p, w, h, threshold, out)
                   ? CVR_I2R_OK : CVR_I2R_FAIL;
    case UINT2_IMAGE:
        if (!img.pixel.u.p) return CVR_I2R_FAIL;
        return buf_to_region_impl(img.pixel.u.p, w, h, threshold, out)
                   ? CVR_I2R_OK : CVR_I2R_FAIL;
    case INT4_IMAGE:
        return buf_to_region_impl(img.pixel.l, w, h, threshold, out)
                   ? CVR_I2R_OK : CVR_I2R_FAIL;
    case INT8_IMAGE:
        return buf_to_region_impl(img.pixel.i8, w, h, threshold, out)
                   ? CVR_I2R_OK : CVR_I2R_FAIL;
    case FLOAT_IMAGE:
        return buf_to_region_impl(img.pixel.f, w, h, threshold, out)
                   ? CVR_I2R_OK : CVR_I2R_FAIL;
    default:
        /* direction / cyclic / complex / vector_field 等非数值灰度类型 */
        return CVR_I2R_UNSUPPORTED;
    }
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
    Hrun* dst = hrl->rl;
    const cvr::CvrRun* src = r.runs.data();
    for (size_t i = 0; i < n; ++i) {
        dst[i].l  = static_cast<HIMGCOOR>(src[i].r);
        dst[i].cb = static_cast<HIMGCOOR>(src[i].cb);
        dst[i].ce = static_cast<HIMGCOOR>(src[i].ce);
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
        FeatureCacheKey ck;
        if (!read_region(proc_handle, k, r, &ck) ||
            !fn(r, &vals[static_cast<size_t>(i - 1)])) {
            char msg[256];
            snprintf(msg, sizeof(msg), "%s failed: %s", feat_name,
                     cvr::cvr_last_error());
            HSetErrText(msg);
            return CVR_ERR_OP;
        }
        feature_cache_store(ck, r.feature);
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
 * cv_connection_ex —— 同 cv_connection，另支持按 CacheFeatures 预计算形状特征
 *
 *   预计算结果写入"内容指纹"缓存（见文件开头 feature_cache_*）：后续算子
 *   （cv_select_shape / cv_region_features / 单值特征算子）对同一 region 内容
 *   直接查表，不再重复计算。CacheFeatures 为字符串元组（元素内可用 '|' 拼接，
 *   支持 none/basic/cheap/hull/all 组合名）；传 'none' / 不传时行为与
 *   cv_connection 完全一致。
 *===========================================================================*/
Herror Hcv_connection_ex(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    /* 读字符串参数前必须分配字符串内存（手册 5.5.10），每算子只调一次 */
    HAllocStringMem(proc_handle, 1024);

    /* 输入控制 1 = CacheFeatures（字符串元组） */
    uint32_t mask = 0u;
    {
        char const* const* feat_ptr = nullptr;
        INT4_8 n_feat = 0;
        HGetPElemS(proc_handle, 1, CONV_NONE, &feat_ptr, &n_feat);
        if (n_feat > 0) {
            std::vector<std::string> names;
            names.reserve(static_cast<size_t>(n_feat));
            for (INT4_8 j = 0; j < n_feat; ++j)
                names.emplace_back(feat_ptr[j] ? feat_ptr[j] : "");
            if (!cvr::cvr_feature_mask_parse(names, mask)) {
                HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
                return CVR_ERR_PARAM;
            }
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
            HSetErrText(const_cast<char*>("cv_connection_ex: failed to read region"));
            return CVR_ERR_PARAM;
        }

        std::vector<CvrRegion> comps;
        if (!cvr::cvr_connection(r, 8, w, h, comps)) {
            HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
            return CVR_ERR_OP;
        }

        for (size_t c = 0; c < comps.size(); ++c) {
            if (mask != 0u) {
                const FeatureCacheKey ck =
                    feature_key_of(comps[c].runs.data(), comps[c].runs.size(),
                                   comps[c].is_compl);
                /* 先水合：命中时保留既有（可能更全）的字段，
                   precompute 只补 mask 里还缺的位，回写时不会降级已有条目 */
                feature_cache_lookup(ck, comps[c].feature);
                cvr::cvr_region_precompute_features(comps[c], mask);
                if (comps[c].feature.flags.any())
                    feature_cache_store(ck, comps[c].feature);
            }
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
    static const bool sel_prof = std::getenv("CVR_SEL_PROFILE") != nullptr;  // DIAG
    auto ptick = [&]() { return std::chrono::steady_clock::now(); };          // DIAG
    double ms_r = 0, ms_c = 0;                                                // DIAG

    std::vector<CvrRegion> regs(static_cast<size_t>(n));
    std::vector<Hkey>      obj_keys(static_cast<size_t>(n));
    std::vector<FeatureCacheKey> keys(static_cast<size_t>(n));
    auto _t0 = ptick();                                                      // DIAG
    for (INT4_8 i = 1; i <= n; ++i) {
        HGetObj(proc_handle, 1, i, &obj_keys[static_cast<size_t>(i - 1)]);
        /* select_shape 是缓存的"生产者"：实测大批量唯一 key 时，lookup+水合
         * （~300B 拷贝/region）远超跳过它重算的成本——水合收益属于后续算子
         * （region_features 自己的 read_region 会水合）。故此处只算 key 存着，
         * 不做 lookup；store 仍照常回写。 */
        FeatureCacheKey& k = keys[static_cast<size_t>(i - 1)];
        if (!read_region(proc_handle, obj_keys[static_cast<size_t>(i - 1)],
                         regs[static_cast<size_t>(i - 1)], nullptr)) {
            HSetErrText(const_cast<char*>("cv_select_shape: failed to read region"));
            return CVR_ERR_PARAM;
        }
        k = feature_key_of(regs[static_cast<size_t>(i - 1)].runs.data(),
                           regs[static_cast<size_t>(i - 1)].runs.size(),
                           regs[static_cast<size_t>(i - 1)].is_compl);
    }
    ms_r = std::chrono::duration<double, std::milli>(ptick() - _t0).count(); // DIAG

    std::vector<std::string> feats(static_cast<size_t>(n_feat));
    for (INT4_8 f = 0; f < n_feat; ++f)
        feats[static_cast<size_t>(f)] = feat_ptr[f] ? feat_ptr[f] : "";

    const char* op_str = op_par.par.s ? op_par.par.s : "and";
    const bool is_or = (std::strcmp(op_str, "or") == 0);

    /* 预校验特征名（并行区里拿不到主线程错误串；这里顺便让首个 region 的水合生效） */
    if (n > 0) {
        for (INT4_8 f = 0; f < n_feat; ++f) {
            double v = 0.0;
            if (!cvr::cvr_get_feature(regs[0], feats[static_cast<size_t>(f)], v)) {
                HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
                return CVR_ERR_OP;
            }
        }
    }

    /* feat+store 按 region 并行（cvr_get_feature 纯 per-region 无副作用；
     * feature_cache_store 用分片互斥锁，线程安全；passed[] 每 region 独立槽位）。
     * HCopyObj 必须按输入序且串行（HALCON API）。小 n 保持串行。 */
    std::vector<char> passed(static_cast<size_t>(n), 0);

    auto feat_range = [&](INT4_8 beg, INT4_8 end) {
        for (INT4_8 i = beg; i < end; ++i) {
            const CvrRegion& r = regs[static_cast<size_t>(i - 1)];
            bool pass = !is_or;
            for (INT4_8 f = 0; f < n_feat; ++f) {
                double v = 0.0;
                cvr::cvr_get_feature(r, feats[static_cast<size_t>(f)], v);  // 已预校验，不会失败
                const bool ok = (v >= vmin[static_cast<size_t>(f)] &&
                                 v <= vmax[static_cast<size_t>(f)]);
                if (is_or) { if (ok) { pass = true; break; } }
                else       { if (!ok) { pass = false; break; } }
            }
            feature_cache_store(keys[static_cast<size_t>(i - 1)], r.feature);
            passed[static_cast<size_t>(i - 1)] = pass ? 1 : 0;
        }
    };

    if (n >= 2048) {
        const INT4_8 nT = (INT4_8)cvflow::par::Pool::get().executors();
        std::vector<std::thread> ths;
        ths.reserve((size_t)(nT > 0 ? nT - 1 : 0));
        for (INT4_8 t = 1; t < nT; ++t) {
            const INT4_8 beg = 1 + (INT4_8)(((long long)(t - 1) * n) / nT);
            const INT4_8 end = 1 + (INT4_8)(((long long)t * n) / nT);
            ths.emplace_back([&, beg, end] { feat_range(beg, end); });
        }
        feat_range(1, 1 + (INT4_8)((long long)n / nT));
        for (auto& t : ths) if (t.joinable()) t.join();
    } else {
        feat_range(1, n + 1);
    }

    for (INT4_8 i = 1; i <= n; ++i) {
        if (passed[static_cast<size_t>(i - 1)]) {
            auto _tc = ptick();                                              // DIAG
            Hkey out_key;
            HCopyObj(proc_handle, obj_keys[static_cast<size_t>(i - 1)], 1,
                     &out_key);
            ms_c += std::chrono::duration<double, std::milli>(ptick() - _tc).count(); // DIAG
        }
    }
    if (sel_prof)                                                            // DIAG
        std::fprintf(stderr, "[sel_prof] n=%lld read=%.1f feat+store(par) copy=%.1f ms\n",
                     n, ms_r, ms_c);
    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_region_features —— 按名称计算 region 形状特征，输出平铺 tuple
 *   输入 Regions 可为元组；输出 Values 布局 [N_regions x N_features]（区域优先，
 *   同一区域的 N 个特征值相邻），与 HALCON 原生 region_features 一致。
 *   特征名口径与 cv_select_shape / cvr_get_feature 完全一致。
 *===========================================================================*/
Herror Hcv_region_features(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    /* 读取字符串参数前必须分配字符串内存（手册 5.5.10），每算子只调一次 */
    HAllocStringMem(proc_handle, 1024);

    /* 控制参数 1 = Features（字符串元组） */
    char const* const* feat_ptr = nullptr;
    INT4_8 n_feat = 0;
    HGetPElemS(proc_handle, 1, CONV_NONE, &feat_ptr, &n_feat);
    if (n_feat <= 0) {
        HSetErrText(const_cast<char*>("cv_region_features: Features is empty"));
        return CVR_ERR_PARAM;
    }

    std::vector<std::string> names;
    names.reserve(static_cast<size_t>(n_feat));
    for (INT4_8 j = 0; j < n_feat; ++j) {
        names.emplace_back(feat_ptr[j] ? feat_ptr[j] : "");
    }

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    std::vector<CvrRegion> regs(static_cast<size_t>(n));
    std::vector<FeatureCacheKey> keys(static_cast<size_t>(n));
    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);
        if (!read_region(proc_handle, k, regs[static_cast<size_t>(i - 1)],
                         &keys[static_cast<size_t>(i - 1)])) {
            HSetErrText(const_cast<char*>(
                "cv_region_features: failed to read region"));
            return CVR_ERR_PARAM;
        }
    }

    std::vector<double> values;
    if (!cvr::cvr_region_features(regs, names, values)) {
        /* 未知名 / 计算失败，cvr_last_error 带具体原因 */
        HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
        return CVR_ERR_OP;
    }
    /* 本次算出的特征按内容指纹回写（后续算子可直接查表） */
    for (INT4_8 i = 1; i <= n; ++i) {
        feature_cache_store(keys[static_cast<size_t>(i - 1)],
                            regs[static_cast<size_t>(i - 1)].feature);
    }

    if (!values.empty()) {
        HPutElem(proc_handle, 1, values.data(),
                 static_cast<INT4_8>(values.size()), DOUBLE_PAR);
    }
    return H_MSG_TRUE;
}

/*=============================================================================
 * cv_gray_features —— 灰度特征（Halcon gray_features 的最小可用子集）
 *   area / row / column / mean / deviation / min / max / median
 * 输入对象 1 = Regions（元组），输入对象 2 = Image（单张），
 * 输入控制 1 = Features（字符串元组，缺省 mean），输出控制 1 = Value
 *===========================================================================*/
Herror Hcv_gray_features(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    /* 读取字符串参数前必须分配字符串内存（手册 5.5.10），每算子只调一次 */
    HAllocStringMem(proc_handle, 1024);

    /* 输入控制 1 = Features（字符串元组）；DEF multivalue=optional，缺省 mean */
    char const* const* feat_ptr = nullptr;
    INT4_8 n_feat = 0;
    HGetPElemS(proc_handle, 1, CONV_NONE, &feat_ptr, &n_feat);

    std::vector<std::string> names;
    if (n_feat <= 0) {
        names.emplace_back("mean");
    } else {
        names.reserve(static_cast<size_t>(n_feat));
        for (INT4_8 j = 0; j < n_feat; ++j) {
            names.emplace_back(feat_ptr[j] ? feat_ptr[j] : "");
        }
    }

    /* 输入对象 1 = Regions（元组） */
    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);
    if (n <= 0) return H_MSG_TRUE;

    std::vector<CvrRegion> regs(static_cast<size_t>(n));
    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey k;
        HGetObj(proc_handle, 1, i, &k);
        if (!read_region(proc_handle, k, regs[static_cast<size_t>(i - 1)])) {
            HSetErrText(const_cast<char*>(
                "cv_gray_features: failed to read region"));
            return CVR_ERR_PARAM;
        }
    }

    /* 输入对象 2 = Image（单张灰度图；忽略其已设 domain，与 HALCON 语义一致） */
    INT4_8 n_img = 0;
    HGetObjNum(proc_handle, 2, &n_img);
    if (n_img < 1) {
        HSetErrText(const_cast<char*>("cv_gray_features: no input image"));
        return CVR_ERR_PARAM;
    }
    Hkey ik;
    HGetObj(proc_handle, 2, 1, &ik);
    Himage img;
    HGetDImage(proc_handle, ik, 1, &img);

    const int32_t w = static_cast<int32_t>(img.width);
    const int32_t h = static_cast<int32_t>(img.height);
    if (w <= 0 || h <= 0) {
        HSetErrText(const_cast<char*>("cv_gray_features: empty image"));
        return CVR_ERR_PARAM;
    }

    const void* px = nullptr;
    cvr::CvrGrayDepth depth = cvr::CVR_GRAY_U8;
    switch (img.kind) {
    case BYTE_IMAGE:  px = img.pixel.b;    depth = cvr::CVR_GRAY_U8;  break;
    case INT1_IMAGE:  px = img.pixel.i;    depth = cvr::CVR_GRAY_S8;  break;
    case INT2_IMAGE:  px = img.pixel.s.p;  depth = cvr::CVR_GRAY_S16; break;
    case UINT2_IMAGE: px = img.pixel.u.p;  depth = cvr::CVR_GRAY_U16; break;
    case INT4_IMAGE:  px = img.pixel.l;    depth = cvr::CVR_GRAY_S32; break;
    case INT8_IMAGE:  px = img.pixel.i8;   depth = cvr::CVR_GRAY_S64; break;
    case FLOAT_IMAGE: px = img.pixel.f;    depth = cvr::CVR_GRAY_F32; break;
    default:
        HSetErrText(const_cast<char*>(
            "cv_gray_features: unsupported image type "
            "(byte/int1/int2/uint2/int4/int8/real only)"));
        return CVR_ERR_PARAM;
    }
    if (!px) {
        HSetErrText(const_cast<char*>("cv_gray_features: null image pointer"));
        return CVR_ERR_PARAM;
    }

    std::vector<double> values;
    if (!cvr::cvr_gray_features(regs, px, w, h, depth, names, values)) {
        HSetErrText(const_cast<char*>(cvr::cvr_last_error()));
        return CVR_ERR_OP;
    }
    if (!values.empty()) {
        HPutElem(proc_handle, 1, values.data(),
                 static_cast<INT4_8>(values.size()), DOUBLE_PAR);
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

/*---------------------------------------------------------------------------
 * cv_gen_rectangle2 —— 生成旋转矩形（无输入对象，不调 HCkNoObj）
 *   参数 1..5 = Row, Column, Phi, Length1, Length2（real/integer 标量）
 *-------------------------------------------------------------------------*/
Herror Hcv_gen_rectangle2(Hproc_handle proc_handle)
{
    Hcpar row_par, col_par, phi_par, l1_par, l2_par;
    HGetSPar(proc_handle, 1, DOUBLE_PAR, &row_par, 1);
    HGetSPar(proc_handle, 2, DOUBLE_PAR, &col_par, 1);
    HGetSPar(proc_handle, 3, DOUBLE_PAR, &phi_par, 1);
    HGetSPar(proc_handle, 4, DOUBLE_PAR, &l1_par, 1);
    HGetSPar(proc_handle, 5, DOUBLE_PAR, &l2_par, 1);

    if (l1_par.par.d < 0.0 || l2_par.par.d < 0.0) {
        HSetErrText(const_cast<char*>(
            "cv_gen_rectangle2: Length1/Length2 must be >= 0"));
        return CVR_ERR_PARAM;
    }

      /* Phi 口径：本包 cv_* 层一律用 cvr/OpenCV 口径（不取反）。
         注意与 HALCON gen_rectangle2 的 phi 反号：同参数生成的是绕列轴镜像的矩形
         （phi=0 与 +-pi/2 因矩形自身对称不受影响）。*/
      CvrRegion out = cvr::cvr_gen_rectangle2(
        row_par.par.d, col_par.par.d, phi_par.par.d,
        l1_par.par.d, l2_par.par.d);
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
        FeatureCacheKey ck;
        if (!read_region(proc_handle, k, r, &ck) ||
            !cvr::cvr_feature_area_center(r, row[j], col[j], a)) {
            HSetErrText(const_cast<char*>("cv_area_center failed"));
            return CVR_ERR_OP;
        }
        area[j] = static_cast<double>(a);
        feature_cache_store(ck, r.feature);
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
        FeatureCacheKey ck;
        if (!read_region(proc_handle, k, r, &ck) ||
            !cvr::cvr_feature_smallest_rectangle1(r, vr1, vc1, vr2, vc2)) {
            HSetErrText(const_cast<char*>("cv_smallest_rectangle1 failed"));
            return CVR_ERR_OP;
        }
        r1[j] = static_cast<double>(vr1);
        c1[j] = static_cast<double>(vc1);
        r2[j] = static_cast<double>(vr2);
        c2[j] = static_cast<double>(vc2);
        feature_cache_store(ck, r.feature);
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
        FeatureCacheKey ck;
        if (!read_region(proc_handle, k, r, &ck) ||
            !cvr::cvr_feature_smallest_rectangle2(r, row[j], col[j], phi[j],
                                                  l1[j], l2[j])) {
            HSetErrText(const_cast<char*>("cv_smallest_rectangle2 failed"));
            return CVR_ERR_OP;
        }
        feature_cache_store(ck, r.feature);
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
        FeatureCacheKey ck;
        if (!read_region(proc_handle, k, r, &ck) ||
            !cvr::cvr_feature_smallest_circle(r, row[j], col[j], radius[j])) {
            HSetErrText(const_cast<char*>("cv_smallest_circle failed"));
            return CVR_ERR_OP;
        }
        feature_cache_store(ck, r.feature);
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
        FeatureCacheKey ck;
        if (!read_region(proc_handle, k, r, &ck) ||
            !cvr::cvr_feature_elliptic_axis(r, ra[j], rb[j], phi[j])) {
            HSetErrText(const_cast<char*>("cv_elliptic_axis failed"));
            return CVR_ERR_OP;
        }
        feature_cache_store(ck, r.feature);
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
 * cv_bin_to_region —— 数值类型图像 -> region（gray >= Threshold）
 *   支持 byte / int1 / int2 / uint2 / int4 / int8 / real 单通道图像；
 *   Threshold 为 real 或 integer（小数阈值可用于 real 图像）。
 *===========================================================================*/
Herror Hcv_bin_to_region(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);

    /* Threshold：DEF type_list = real,integer，CONV_CAST 兼容整数字面量 */
    double const* th_ptr = nullptr;
    INT4_8 th_num = 0;
    HGetPElemD(proc_handle, 1, CONV_CAST, &th_ptr, &th_num);
    if (!th_ptr || th_num < 1) {
        HSetErrText(const_cast<char*>("cv_bin_to_region: Threshold missing"));
        return CVR_ERR_PARAM;
    }
    const double threshold = th_ptr[0];
    if (!(threshold == threshold)) { /* NaN 检查 */
        HSetErrText(const_cast<char*>("cv_bin_to_region: Threshold is NaN"));
        return CVR_ERR_PARAM;
    }

    INT4_8 n = 0;
    HGetObjNum(proc_handle, 1, &n);

    for (INT4_8 i = 1; i <= n; ++i) {
        Hkey obj_key;
        HGetObj(proc_handle, 1, i, &obj_key);

        Himage img;
        HGetDImage(proc_handle, obj_key, 1, &img);

        CvrRegion out;
        const int st = image_to_region(img, threshold, out);
        if (st == CVR_I2R_UNSUPPORTED) {
            HSetErrText(const_cast<char*>(
                "cv_bin_to_region: unsupported image type "
                "(byte/int1/int2/uint2/int4/int8/real only)"));
            return CVR_ERR_PARAM;
        }
        if (st != CVR_I2R_OK) {
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

static bool preset_opening_circle(const CvrRegion& r, double p1, int32_t,
                                  CvrCoord w, CvrCoord h, CvrRegion& out) {
    if (p1 < 0.0) return false;
    CvrRegion se = cvr::cvr_gen_circle(0, 0, p1);
    return cvr::cvr_opening(r, se, w, h, out);
}

static bool preset_closing_circle(const CvrRegion& r, double p1, int32_t,
                                  CvrCoord w, CvrCoord h, CvrRegion& out) {
    if (p1 < 0.0) return false;
    CvrRegion se = cvr::cvr_gen_circle(0, 0, p1);
    return cvr::cvr_closing(r, se, w, h, out);
}

static bool preset_opening_rect(const CvrRegion& r, double p1, int32_t p2,
                                CvrCoord w, CvrCoord h, CvrRegion& out) {
    CvrRegion se = cvr::cvr_se_rect(static_cast<int32_t>(p1), p2);
    return cvr::cvr_opening(r, se, w, h, out);
}

static bool preset_closing_rect(const CvrRegion& r, double p1, int32_t p2,
                                CvrCoord w, CvrCoord h, CvrRegion& out) {
    CvrRegion se = cvr::cvr_se_rect(static_cast<int32_t>(p1), p2);
    return cvr::cvr_closing(r, se, w, h, out);
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

Herror Hcv_opening_rectangle1(Hproc_handle proc_handle)
{ return cvr_preset_morph_impl(proc_handle, &preset_opening_rect, true); }

Herror Hcv_closing_rectangle1(Hproc_handle proc_handle)
{ return cvr_preset_morph_impl(proc_handle, &preset_closing_rect, true); }

Herror Hcv_opening_circle(Hproc_handle proc_handle)
{ return cvr_preset_morph_impl(proc_handle, &preset_opening_circle, false); }

Herror Hcv_closing_circle(Hproc_handle proc_handle)
{ return cvr_preset_morph_impl(proc_handle, &preset_closing_circle, false); }
Herror Hcv_erosion_circle(Hproc_handle proc_handle)
{ return cvr_preset_morph_impl(proc_handle, &preset_erosion_circle, false); }

Herror Hcv_dilation_circle(Hproc_handle proc_handle)
{ return cvr_preset_morph_impl(proc_handle, &preset_dilation_circle, false); }

Herror Hcv_erosion_rectangle1(Hproc_handle proc_handle)
{ return cvr_preset_morph_impl(proc_handle, &preset_erosion_rect, true); }

Herror Hcv_dilation_rectangle1(Hproc_handle proc_handle)
{ return cvr_preset_morph_impl(proc_handle, &preset_dilation_rect, true); }
