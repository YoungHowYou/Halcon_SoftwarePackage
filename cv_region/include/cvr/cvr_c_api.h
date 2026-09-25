#pragma once
/*=============================================================================
 * cvr_c_api.h — cv_region DLL 的 C 外接口（ABI 稳定，可被 HALCON 扩展包调用）
 *
 * 设计要点:
 *   - 不透明句柄 cvr_region_h，隐藏 C++ std::vector 内部实现
 *   - 所有函数返回 int32_t: 非 0 成功，0 失败（失败原因用 cvr_c_last_error()）
 *   - 内存所有权: create 的句柄用 cvr_region_destroy 释放；
 *     connection 的句柄数组用 cvr_region_array_destroy 释放；
 *     select_shape 的索引数组用 cvr_free_int64 释放
 *===========================================================================*/

#include <stdint.h>

#if defined(_WIN32) || defined(_WIN64)
#  ifdef CVR_BUILD_DLL
#    define CVR_API __declspec(dllexport)
#  else
     /* 静态链接（当前唯一用法）：不加 dllimport，否则链静态库报 __imp_ 未解析 */
#    define CVR_API
#  endif
#else
#  define CVR_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 单条 chord/run：闭区间 [cb, ce]，行号 r。与 HALCON Hrun 字段语义一致 */
typedef struct CvrRunC {
    int32_t r;
    int32_t cb;
    int32_t ce;
} CvrRunC;

typedef void* cvr_region_h;

/* ---- 句柄生命周期 ---- */
CVR_API cvr_region_h   cvr_region_create(const CvrRunC* runs, int64_t num_runs,
                                         int32_t is_compl);
CVR_API void           cvr_region_destroy(cvr_region_h h);
CVR_API int64_t        cvr_region_num_runs(cvr_region_h h);
CVR_API const CvrRunC* cvr_region_runs(cvr_region_h h);   /* 指针随 destroy 失效 */
CVR_API int32_t        cvr_region_is_compl(cvr_region_h h);

/* ---- 集合代数（w,h 为定义域；非补集输入可传 0） ---- */
CVR_API int32_t cvr_union2(cvr_region_h a, cvr_region_h b,
                           int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_intersection(cvr_region_h a, cvr_region_h b,
                                 int32_t w, int32_t h, cvr_region_h* out);

/* ---- 形态学 ---- */
CVR_API int32_t cvr_erosion1(cvr_region_h r, cvr_region_h se,
                             int32_t iterations, int32_t w, int32_t h,
                             cvr_region_h* out);

/* ---- 连通域拆分：输出句柄数组（DLL 分配） ---- */
CVR_API int32_t cvr_connection(cvr_region_h r, int32_t connectivity,
                               int32_t w, int32_t h,
                               cvr_region_h** out_arr, int64_t* out_count);
CVR_API void    cvr_region_array_destroy(cvr_region_h* arr, int64_t count);

/* ---- 形状筛选：返回被选中输入区域的 0 基索引数组（DLL 分配） ---- */
CVR_API int32_t cvr_select_shape(cvr_region_h const* regions, int64_t n,
                                 const char* const* features, int64_t n_features,
                                 const char* operation,
                                 const double* mins, const double* maxs,
                                 int64_t** out_indices, int64_t* out_count);
CVR_API void    cvr_free_int64(int64_t* p);

/* ---- 集合代数（补充） ---- */
CVR_API int32_t cvr_union1(cvr_region_h const* regions, int64_t n,
                           int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_difference(cvr_region_h a, cvr_region_h b,
                               int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_complement(cvr_region_h r, cvr_region_h* out);
CVR_API int32_t cvr_symm_difference(cvr_region_h a, cvr_region_h b,
                                    int32_t w, int32_t h, cvr_region_h* out);

/* ---- 形态学（补充） ---- */
CVR_API int32_t cvr_dilation1(cvr_region_h r, cvr_region_h se,
                              int32_t iterations, int32_t w, int32_t h,
                              cvr_region_h* out);
CVR_API int32_t cvr_opening(cvr_region_h r, cvr_region_h se,
                            int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_closing(cvr_region_h r, cvr_region_h se,
                            int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_fill_up(cvr_region_h r, int32_t w, int32_t h,
                            cvr_region_h* out);

/* ---- 预设结构元形态学（HALCON erosion_circle/rectangle1 同形，无 Iterations） ---- */
CVR_API int32_t cvr_erosion_circle(cvr_region_h r, double radius,
                                   int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_dilation_circle(cvr_region_h r, double radius,
                                    int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_erosion_rectangle1(cvr_region_h r, int32_t rw, int32_t rh,
                                       int32_t w, int32_t h, cvr_region_h* out);
CVR_API int32_t cvr_dilation_rectangle1(cvr_region_h r, int32_t rw, int32_t rh,
                                        int32_t w, int32_t h,
                                        cvr_region_h* out);

/* ---- 区域生成 ---- */
CVR_API int32_t cvr_gen_circle(double row, double col, double radius,
                               cvr_region_h* out);
CVR_API int32_t cvr_gen_rectangle1(int32_t r1, int32_t c1, int32_t r2, int32_t c2,
                                   cvr_region_h* out);

/* ---- 特征（单区域；tuple 由调用方循环） ---- */
CVR_API int32_t cvr_area_center(cvr_region_h r, double* row, double* col,
                                double* area);
CVR_API int32_t cvr_smallest_rectangle1(cvr_region_h r, double* r1, double* c1,
                                        double* r2, double* c2);
CVR_API int32_t cvr_smallest_rectangle2(cvr_region_h r, double* row, double* col,
                                        double* phi, double* length1,
                                        double* length2);
CVR_API int32_t cvr_smallest_circle(cvr_region_h r, double* row, double* col,
                                    double* radius);
CVR_API int32_t cvr_elliptic_axis(cvr_region_h r, double* ra, double* rb,
                                  double* phi);
CVR_API int32_t cvr_get_feature(cvr_region_h r, const char* name, double* value);

/* ---- 形状变换（type: rectangle1/rectangle2/ellipse/outer_circle/convex） ---- */
CVR_API int32_t cvr_shape_trans(cvr_region_h r, const char* type,
                                cvr_region_h* out);

/* ---- region <-> 二值图（byte；fg/bg 可自定义，from_mask 按阈值 >=） ---- */
CVR_API int32_t cvr_region_to_mask(cvr_region_h r, int32_t w, int32_t h,
                                   int32_t fg, int32_t bg,
                                   uint8_t* buf, int64_t buf_size);
CVR_API int32_t cvr_region_from_mask(const uint8_t* buf, int32_t w, int32_t h,
                                     int32_t threshold, cvr_region_h* out);

/* ---- 错误 ---- */
CVR_API const char* cvr_c_last_error(void);

#ifdef __cplusplus
}
#endif
