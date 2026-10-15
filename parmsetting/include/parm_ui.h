/* ============================================================================
 * parm_ui.h —— 参数配置模块 C 接口（唯一对外头文件）
 *
 * 用法（一行）：
 *     int cancelled = 0;
 *     char* out = parm_ui_json(in_json, 当前用户权限等级, &cancelled);
 *     if (out) { ...用结果...; parm_ui_free(out); }
 *     else    { printf("%s\n", parm_ui_last_error()); }
 *
 * 语义：
 *   * 传入一份参数 JSON（UTF-8，无 BOM），弹出一个【模态窗口】，窗口按 Type 字段
 *     自动渲染控件；用户点“保存并关闭”或“取消 / 右上角 ×”关窗后函数才返回。
 *   * 窗口跑在一条【本函数自己创建的 UI 线程】上：调用方的其它线程完全不受影响，
 *     只有当前这个调用线程会阻塞到窗口关闭（这就是“模态”的语义）。
 *     不想阻塞当前线程就用 parm_ui_json_async()。
 *   * 返回：堆上的 UTF-8 JSON 串，必须用 parm_ui_free() 释放（不要用 free/delete）。
 *       - 保存关闭：返回修改后的 JSON
 *       - 取消 / ×：返回【原始 JSON 的副本】，*cancelled = 1
 *       - 失败：返回 NULL，用 parm_ui_last_error() 取原因（thread-local）
 *
 * 线程约定：
 *   * parm_ui_json() / parm_ui_json_async()：可以在任意线程调用。
 *   * parm_ui_json_inline()：不另起线程，就在当前线程里跑整个 UI 循环。
 *     调用方必须保证“同一个线程从头跑到尾”，且该线程不能同时还在跑别的
 *     UI 事件循环（否则会互相卡住）。
 *   * 同一时刻只允许一个配置窗口：并发调用会排队（串行执行）。
 *   * parm_ui_last_error() 返回的是 thread-local 缓冲，仅对本线程有效。
 *
 * 权限：competence 就是当前登录用户的等级（0 操作员 / 1 产线 / 2 系统 / 3 管理员）。
 *       节点 Competence <= competence 可编辑，否则置灰只读；保存时二次校验，越权值不写回。
 * ==========================================================================*/
#ifndef PARM_UI_H
#define PARM_UI_H

#if defined(_WIN32) && !defined(PARM_UI_STATIC)
#  ifdef PARM_UI_BUILD_DLL
#    define PARM_UI_API __declspec(dllexport)
#  else
#    define PARM_UI_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define PARM_UI_API __attribute__((visibility("default")))
#else
#  define PARM_UI_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ 返回码 */
#define PARM_UI_OK           0   /* 成功（含用户取消，看 cancelled） */
#define PARM_UI_E_INVALID   -1   /* 入参非法（in_json / cb 为空等） */
#define PARM_UI_E_PARSE     -2   /* JSON 语法错误 */
#define PARM_UI_E_NO_DISPLAY -3  /* 无图形环境（Linux 无 DISPLAY / WAYLAND_DISPLAY） */
#define PARM_UI_E_UI_INIT   -4   /* 窗口 / 渲染初始化失败 */
#define PARM_UI_E_INTERNAL  -5   /* 内部异常 */

/* ------------------------------------------------------------------ 入口 1：
 * 阻塞式（内部 UI 线程）。任意线程可调，其它线程不受影响。
 * in_json       : UTF-8 JSON 串（调用返回前需要保持有效，内部会立即拷贝）
 * competence    : 当前用户权限等级
 * cancelled     : 可为 NULL；窗口被“取消/×”关闭时置 1，保存关闭置 0
 * 返回          : 见文件头说明；失败返回 NULL 并设置 thread-local 错误
 */
PARM_UI_API char* parm_ui_json(const char* in_json, int competence, int* cancelled);

/* ------------------------------------------------------------------ 入口 2：
 * 不另起线程，就在调用线程里跑 UI 循环（给“宿主自己掌控主线程”的场景用）
 */
PARM_UI_API char* parm_ui_json_inline(const char* in_json, int competence, int* cancelled);

/* ------------------------------------------------------------------ 入口 3：
 * 异步：函数立刻返回，窗口在内部线程里跑到关闭为止，然后回调。
 *   out_json : 结果串，**仅在回调期间有效**（要留就自己拷一份）；失败为 NULL
 *   cancelled: 1 = 用户取消
 *   error    : 出错时的原因文本（可为 NULL），同样仅在回调期间有效
 * 注意：回调运行在【UI 线程】上，不要在回调里做重活或再调 parm_ui_json。
 */
typedef void (*parm_ui_done_cb)(const char* out_json, int cancelled, const char* error, void* user);
PARM_UI_API int parm_ui_json_async(const char* in_json, int competence,
                                   parm_ui_done_cb cb, void* user);

/* 释放 parm_ui_json* 返回的字符串（只能释放本库返回的指针） */
PARM_UI_API void parm_ui_free(char* p);

/* 本线程最近一次错误原因（UTF-8，无错误返回空串）；指针在下次调用前有效 */
PARM_UI_API const char* parm_ui_last_error(void);

/* 版本号：任意参数可为 NULL */
PARM_UI_API void parm_ui_version(int* major, int* minor, int* patch);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* PARM_UI_H */
