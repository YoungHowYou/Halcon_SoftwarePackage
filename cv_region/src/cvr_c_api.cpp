/*=============================================================================
 * cvr_c_api.cpp — C ABI 外接口实现（包一层薄壳，内部走 C++ 核心）
 *===========================================================================*/
#include "cvr/cvr_c_api.h"
#include "cvr/cvr_region.hpp"
#include "cvr/cvr_ops.hpp"
#include "cvr/cvr_morph.hpp"
#include "cvr/cvr_conn.hpp"
#include "cvr/cvr_feat.hpp"
#include "cvr/cvr_shape.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

static_assert(sizeof(CvrRunC) == sizeof(cvr::CvrRun), "CvrRunC/CvrRun layout mismatch");
static_assert(offsetof(CvrRunC, r)  == offsetof(cvr::CvrRun, r),  "layout mismatch");
static_assert(offsetof(CvrRunC, cb) == offsetof(cvr::CvrRun, cb), "layout mismatch");
static_assert(offsetof(CvrRunC, ce) == offsetof(cvr::CvrRun, ce), "layout mismatch");

namespace {

inline cvr::CvrRegion* to_cpp(cvr_region_h h) {
    return reinterpret_cast<cvr::CvrRegion*>(h);
}

inline cvr_region_h to_hdl(cvr::CvrRegion* p) {
    return reinterpret_cast<cvr_region_h>(p);
}

inline const CvrRunC* to_c(const cvr::CvrRun* p) {
    return reinterpret_cast<const CvrRunC*>(p);
}

inline cvr::CvrRun* to_cpp_run(CvrRunC* p) {
    return reinterpret_cast<cvr::CvrRun*>(p);
}

} // namespace

extern "C" {

CVR_API cvr_region_h cvr_region_create(const CvrRunC* runs, int64_t num_runs,
                                       int32_t is_compl) {
    if (num_runs < 0 || (num_runs > 0 && runs == nullptr)) return nullptr;
    auto* p = new (std::nothrow) cvr::CvrRegion();
    if (!p) return nullptr;
    p->is_compl = (is_compl != 0);
    if (num_runs > 0) {
        p->runs.resize(static_cast<size_t>(num_runs));
        std::memcpy(p->runs.data(), runs,
                    static_cast<size_t>(num_runs) * sizeof(CvrRunC));
    }
    return to_hdl(p);
}

CVR_API void cvr_region_destroy(cvr_region_h h) {
    delete to_cpp(h);
}

CVR_API int64_t cvr_region_num_runs(cvr_region_h h) {
    auto* p = to_cpp(h);
    return p ? static_cast<int64_t>(p->runs.size()) : 0;
}

CVR_API const CvrRunC* cvr_region_runs(cvr_region_h h) {
    auto* p = to_cpp(h);
    return (p && !p->runs.empty()) ? to_c(p->runs.data()) : nullptr;
}

CVR_API int32_t cvr_region_is_compl(cvr_region_h h) {
    auto* p = to_cpp(h);
    return (p && p->is_compl) ? 1 : 0;
}

CVR_API int32_t cvr_union2(cvr_region_h a, cvr_region_h b,
                           int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!a || !b || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_union2(*to_cpp(a), *to_cpp(b), w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_intersection(cvr_region_h a, cvr_region_h b,
                                 int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!a || !b || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_intersection(*to_cpp(a), *to_cpp(b), w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_erosion1(cvr_region_h r, cvr_region_h se,
                             int32_t iterations, int32_t w, int32_t h,
                             cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !se || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_erosion1(*to_cpp(r), *to_cpp(se), iterations, w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_connection(cvr_region_h r, int32_t connectivity,
                               int32_t w, int32_t h,
                               cvr_region_h** out_arr, int64_t* out_count) {
    cvr::cvr_clear_last_error();
    if (!r || !out_arr || !out_count) return 0;
    std::vector<cvr::CvrRegion> comps;
    if (!cvr::cvr_connection(*to_cpp(r), connectivity, w, h, comps))
        return 0;

    const int64_t n = static_cast<int64_t>(comps.size());
    auto* arr = new (std::nothrow) cvr_region_h[static_cast<size_t>(n > 0 ? n : 1)];
    if (!arr) return 0;
    for (int64_t i = 0; i < n; ++i) {
        auto* p = new (std::nothrow) cvr::CvrRegion(std::move(comps[static_cast<size_t>(i)]));
        if (!p) {
            for (int64_t j = 0; j < i; ++j) delete to_cpp(arr[j]);
            delete[] arr;
            return 0;
        }
        arr[i] = to_hdl(p);
    }
    *out_arr = arr;
    *out_count = n;
    return 1;
}

CVR_API void cvr_region_array_destroy(cvr_region_h* arr, int64_t count) {
    if (!arr) return;
    for (int64_t i = 0; i < count; ++i) delete to_cpp(arr[i]);
    delete[] arr;
}

CVR_API int32_t cvr_select_shape(cvr_region_h const* regions, int64_t n,
                                 const char* const* features, int64_t n_features,
                                 const char* operation,
                                 const double* mins, const double* maxs,
                                 int64_t** out_indices, int64_t* out_count) {
    cvr::cvr_clear_last_error();
    if (!regions || !out_indices || !out_count || n < 0) return 0;
    if (n > 0 && (!features || n_features <= 0 || !operation || !mins || !maxs))
        return 0;

    const bool is_or =
        (std::strcmp(operation, "or") == 0 || std::strcmp(operation, "OR") == 0);

    std::vector<int64_t> idx;
    for (int64_t i = 0; i < n; ++i) {
        auto* p = to_cpp(regions[i]);
        if (!p) return 0;
        bool pass = !is_or;   // and: 全真；or: 任一真
        for (int64_t f = 0; f < n_features; ++f) {
            if (!features[f]) return 0;
            double v = 0.0;
            if (!cvr::cvr_get_feature(*p, features[f], v)) {
                cvr::cvr_set_last_error("cvr_select_shape: unknown feature");
                return 0;
            }
            const bool ok = (v >= mins[f] && v <= maxs[f]);
            if (is_or) { if (ok) { pass = true; break; } }
            else       { if (!ok) { pass = false; break; } }
        }
        if (pass) idx.push_back(i);
    }

    const int64_t cnt = static_cast<int64_t>(idx.size());
    auto* buf = new (std::nothrow) int64_t[static_cast<size_t>(cnt > 0 ? cnt : 1)];
    if (!buf) return 0;
    if (cnt > 0) std::memcpy(buf, idx.data(),
                             static_cast<size_t>(cnt) * sizeof(int64_t));
    *out_indices = buf;
    *out_count = cnt;
    return 1;
}

CVR_API void cvr_free_int64(int64_t* p) {
    delete[] p;
}

/* ================= 集合代数（补充） ================= */

CVR_API int32_t cvr_union1(cvr_region_h const* regions, int64_t n,
                           int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!regions || !out || n < 0) return 0;
    std::vector<cvr::CvrRegion> regs;
    regs.reserve(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
        auto* p = to_cpp(regions[i]);
        if (!p) return 0;
        regs.push_back(*p);
    }
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_union1(regs, w, h, *o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_difference(cvr_region_h a, cvr_region_h b,
                               int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!a || !b || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_difference(*to_cpp(a), *to_cpp(b), w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_complement(cvr_region_h r, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_complement(*to_cpp(r), *o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_symm_difference(cvr_region_h a, cvr_region_h b,
                                    int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!a || !b || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_symm_difference(*to_cpp(a), *to_cpp(b), w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

/* ================= 形态学（补充） ================= */

CVR_API int32_t cvr_dilation1(cvr_region_h r, cvr_region_h se,
                              int32_t iterations, int32_t w, int32_t h,
                              cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !se || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_dilation1(*to_cpp(r), *to_cpp(se), iterations, w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_opening(cvr_region_h r, cvr_region_h se,
                            int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !se || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_opening(*to_cpp(r), *to_cpp(se), w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_closing(cvr_region_h r, cvr_region_h se,
                            int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !se || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_closing(*to_cpp(r), *to_cpp(se), w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_fill_up(cvr_region_h r, int32_t w, int32_t h,
                            cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_fill_up(*to_cpp(r), w, h, *o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

/* ================= 预设结构元形态学 ================= */

CVR_API int32_t cvr_erosion_circle(cvr_region_h r, double radius,
                                   int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !out || radius < 0.0) return 0;
    cvr::CvrRegion se = cvr::cvr_gen_circle(0, 0, radius);
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_erosion1(*to_cpp(r), se, 1, w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_dilation_circle(cvr_region_h r, double radius,
                                    int32_t w, int32_t h, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !out || radius < 0.0) return 0;
    cvr::CvrRegion se = cvr::cvr_gen_circle(0, 0, radius);
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_dilation1(*to_cpp(r), se, 1, w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_erosion_rectangle1(cvr_region_h r, int32_t rw, int32_t rh,
                                       int32_t w, int32_t h,
                                       cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !out || rw <= 0 || rh <= 0) return 0;
    cvr::CvrRegion se = cvr::cvr_se_rect(rw, rh);
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_erosion1(*to_cpp(r), se, 1, w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_dilation_rectangle1(cvr_region_h r, int32_t rw, int32_t rh,
                                        int32_t w, int32_t h,
                                        cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !out || rw <= 0 || rh <= 0) return 0;
    cvr::CvrRegion se = cvr::cvr_se_rect(rw, rh);
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_dilation1(*to_cpp(r), se, 1, w, h, *o)) {
        delete o;
        return 0;
    }
    *out = to_hdl(o);
    return 1;
}

/* ================= 区域生成 ================= */

/* HALCON gen_* 约定：裁剪负坐标（region 行列 >= 0） */
static void clip_negative(cvr::CvrRegion& r) {
    auto& runs = r.runs;
    std::vector<cvr::CvrRun> kept;
    kept.reserve(runs.size());
    for (const auto& rr : runs) {
        if (rr.r < 0) continue;
        cvr::CvrCoord cb = rr.cb < 0 ? 0 : rr.cb;
        if (cb <= rr.ce) kept.push_back(cvr::CvrRun{rr.r, cb, rr.ce});
    }
    runs = std::move(kept);
}

CVR_API int32_t cvr_gen_circle(double row, double col, double radius,
                               cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!out || radius < 0.0) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion(
        cvr::cvr_gen_circle(static_cast<cvr::CvrCoord>(std::lround(row)),
                            static_cast<cvr::CvrCoord>(std::lround(col)),
                            radius));
    if (!o) return 0;
    clip_negative(*o);
    if (!cvr::cvr_region_normalize(*o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

CVR_API int32_t cvr_gen_rectangle1(int32_t r1, int32_t c1, int32_t r2,
                                   int32_t c2, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!out || r2 < r1 || c2 < c1) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion(
        cvr::cvr_gen_rectangle1(r1, c1, r2, c2));
    if (!o) return 0;
    clip_negative(*o);
    if (!cvr::cvr_region_normalize(*o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

/* ================= 特征 ================= */

CVR_API int32_t cvr_area_center(cvr_region_h r, double* row, double* col,
                                double* area) {
    cvr::cvr_clear_last_error();
    if (!r || !row || !col || !area) return 0;
    double rr = 0.0, cc = 0.0;
    cvr::CvrChords a = 0;
    if (!cvr::cvr_feature_area_center(*to_cpp(r), rr, cc, a)) return 0;
    *row = rr;
    *col = cc;
    *area = static_cast<double>(a);
    return 1;
}

CVR_API int32_t cvr_smallest_rectangle1(cvr_region_h r, double* r1, double* c1,
                                        double* r2, double* c2) {
    cvr::cvr_clear_last_error();
    if (!r || !r1 || !c1 || !r2 || !c2) return 0;
    cvr::CvrCoord rr1 = 0, cc1 = 0, rr2 = 0, cc2 = 0;
    if (!cvr::cvr_feature_smallest_rectangle1(*to_cpp(r), rr1, cc1, rr2, cc2))
        return 0;
    *r1 = rr1;
    *c1 = cc1;
    *r2 = rr2;
    *c2 = cc2;
    return 1;
}

CVR_API int32_t cvr_smallest_rectangle2(cvr_region_h r, double* row, double* col,
                                        double* phi, double* length1,
                                        double* length2) {
    cvr::cvr_clear_last_error();
    if (!r || !row || !col || !phi || !length1 || !length2) return 0;
    return cvr::cvr_feature_smallest_rectangle2(*to_cpp(r), *row, *col, *phi,
                                                *length1, *length2)
               ? 1
               : 0;
}

CVR_API int32_t cvr_smallest_circle(cvr_region_h r, double* row, double* col,
                                    double* radius) {
    cvr::cvr_clear_last_error();
    if (!r || !row || !col || !radius) return 0;
    return cvr::cvr_feature_smallest_circle(*to_cpp(r), *row, *col, *radius)
               ? 1
               : 0;
}

CVR_API int32_t cvr_elliptic_axis(cvr_region_h r, double* ra, double* rb,
                                  double* phi) {
    cvr::cvr_clear_last_error();
    if (!r || !ra || !rb || !phi) return 0;
    return cvr::cvr_feature_elliptic_axis(*to_cpp(r), *ra, *rb, *phi) ? 1 : 0;
}

CVR_API int32_t cvr_get_feature(cvr_region_h r, const char* name,
                                double* value) {
    cvr::cvr_clear_last_error();
    if (!r || !name || !value) return 0;
    return cvr::cvr_get_feature(*to_cpp(r), name, *value) ? 1 : 0;
}

/* ================= 形状变换 ================= */

CVR_API int32_t cvr_shape_trans(cvr_region_h r, const char* type,
                                cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!r || !type || !out) return 0;
    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;
    if (!cvr::cvr_shape_trans(*to_cpp(r), type, *o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

/* ================= region <-> 二值图 ================= */

CVR_API int32_t cvr_region_to_mask(cvr_region_h r, int32_t w, int32_t h,
                                   int32_t fg, int32_t bg,
                                   uint8_t* buf, int64_t buf_size) {
    cvr::cvr_clear_last_error();
    if (!r || !buf || w <= 0 || h <= 0) return 0;
    if (fg < 0 || fg > 255 || bg < 0 || bg > 255) return 0;
    if (buf_size < static_cast<int64_t>(w) * h) return 0;

    std::memset(buf, bg, static_cast<size_t>(w) * static_cast<size_t>(h));

    cvr::CvrRegion mat;
    if (to_cpp(r)->is_compl) {
        if (!cvr::cvr_region_materialize(*to_cpp(r), w, h, mat)) return 0;
    } else {
        mat = *to_cpp(r);
    }

    const uint8_t fgv = static_cast<uint8_t>(fg);
    for (const auto& run : mat.runs) {
        if (run.r < 0 || run.r >= h) continue;
        int32_t cb = run.cb < 0 ? 0 : run.cb;
        int32_t ce = run.ce >= w ? w - 1 : run.ce;
        if (cb > ce) continue;
        uint8_t* line = buf + static_cast<int64_t>(run.r) * w;
        for (int32_t c = cb; c <= ce; ++c) line[c] = fgv;
    }
    return 1;
}

CVR_API int32_t cvr_region_from_mask(const uint8_t* buf, int32_t w, int32_t h,
                                     int32_t threshold, cvr_region_h* out) {
    cvr::cvr_clear_last_error();
    if (!buf || !out || w <= 0 || h <= 0 || threshold < 0 || threshold > 255)
        return 0;

    auto* o = new (std::nothrow) cvr::CvrRegion();
    if (!o) return 0;

    for (int32_t r = 0; r < h; ++r) {
        const uint8_t* line = buf + static_cast<int64_t>(r) * w;
        int32_t c = 0;
        while (c < w) {
            while (c < w && line[c] < threshold) ++c;
            if (c >= w) break;
            const int32_t cb = c;
            while (c < w && line[c] >= threshold) ++c;
            o->runs.push_back(cvr::CvrRun{ r, cb, c - 1 });
        }
    }
    if (!cvr::cvr_region_normalize(*o)) { delete o; return 0; }
    *out = to_hdl(o);
    return 1;
}

CVR_API const char* cvr_c_last_error(void) {
    return cvr::cvr_last_error();
}

} // extern "C"
