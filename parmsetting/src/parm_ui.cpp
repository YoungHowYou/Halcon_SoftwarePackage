// =============================================================================
// parm_ui.cpp —— C 接口实现
//
//   线程模型（这是本模块的关键约定，见 include/parm_ui.h 的说明）：
//     * parm_ui_json()        —— 起一条【本库自己的 UI 线程】跑完整个窗口生命周期，
//                                调用线程阻塞等待结果。宿主其它线程完全不受影响，
//                                也不要求调用方是主线程（GLFW 只要求“同一条线程跑完全程”）。
//     * parm_ui_json_inline() —— 不另起线程，就在当前线程里跑（调用方自己保证线程约束）。
//     * parm_ui_json_async()  —— 立刻返回，窗口在内部线程里跑，关闭时回调。
//   并发：同一时刻只允许一个配置窗口（全局会话锁），后到的调用排队等待。
// =============================================================================

#include <parm_ui.h>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <thread>

#include "ui_app.h"

namespace {

// 同一时刻只允许一个配置窗口：GLFW/ImGui 的全局状态不是为多窗口并发设计的，
// 而且配置界面本身就是“一次改一份”的语义。
std::mutex g_session_mutex;

// 每个线程各自的错误信息（thread-local，互不干扰）
thread_local std::string t_last_error;

struct SessionResult {
    std::string out;
    std::string err;
    bool        cancelled = false;
    int         code      = PARM_UI_OK;
};

// 真正跑一次会话（调用者必须已持有 g_session_mutex）
SessionResult run_session_locked(const std::string& input, int competence) {
    SessionResult r;
    try {
        r.code = parm_ui::run_ui_session(input, competence, r.out, r.cancelled, r.err);
    } catch (const std::exception& e) {
        r.code = PARM_UI_E_INTERNAL;
        r.err  = std::string("内部异常：") + e.what();
    } catch (...) {
        r.code = PARM_UI_E_INTERNAL;
        r.err  = "内部未知异常";
    }
    return r;
}

// 用 malloc 分配，保证调用方既可以用 parm_ui_free 也可以用 free
char* dup_c_string(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (p == nullptr) return nullptr;
    std::memcpy(p, s.data(), s.size());
    p[s.size()] = '\0';
    return p;
}

char* finish(const SessionResult& r, int* cancelled) {
    if (cancelled != nullptr) *cancelled = r.cancelled ? 1 : 0;
    if (r.code != PARM_UI_OK) {
        t_last_error = r.err.empty() ? "未知错误" : r.err;
        return nullptr;
    }
    return dup_c_string(r.out);
}

}  // namespace

extern "C" {

char* parm_ui_json(const char* in_json, int competence, int* cancelled) {
    if (cancelled != nullptr) *cancelled = 0;
    t_last_error.clear();

    if (in_json == nullptr) {
        t_last_error = "in_json 为空指针";
        return nullptr;
    }

    const std::string input = in_json;
    SessionResult     result;

    try {
        std::thread ui_thread([&result, &input, competence]() {
            std::lock_guard<std::mutex> lock(g_session_mutex);
            result = run_session_locked(input, competence);
        });
        ui_thread.join();  // 只等这条 UI 线程，别的线程照跑
    } catch (const std::exception& e) {
        t_last_error = std::string("创建 UI 线程失败：") + e.what();
        return nullptr;
    }

    return finish(result, cancelled);
}

char* parm_ui_json_inline(const char* in_json, int competence, int* cancelled) {
    if (cancelled != nullptr) *cancelled = 0;
    t_last_error.clear();

    if (in_json == nullptr) {
        t_last_error = "in_json 为空指针";
        return nullptr;
    }

    const std::string input = in_json;
    SessionResult     result;
    try {
        std::lock_guard<std::mutex> lock(g_session_mutex);
        result = run_session_locked(input, competence);
    } catch (const std::exception& e) {
        t_last_error = std::string("内部异常：") + e.what();
        return nullptr;
    }
    return finish(result, cancelled);
}

int parm_ui_json_async(const char* in_json, int competence, parm_ui_done_cb cb, void* user) {
    t_last_error.clear();
    if (in_json == nullptr || cb == nullptr) {
        t_last_error = "in_json 或回调为空";
        return PARM_UI_E_INVALID;
    }

    const std::string input = in_json;  // 拷一份，调用方返回后无需再保持有效
    try {
        std::thread([input, competence, cb, user]() {
            SessionResult result;
            {
                std::lock_guard<std::mutex> lock(g_session_mutex);
                result = run_session_locked(input, competence);
            }
            const char* out = (result.code == PARM_UI_OK) ? result.out.c_str() : nullptr;
            const char* err = (result.code == PARM_UI_OK) ? nullptr : result.err.c_str();
            try {
                cb(out, result.cancelled ? 1 : 0, err, user);
            } catch (...) {
                // 回调里抛异常不允许越过 C 边界，这里吞掉
            }
        }).detach();
    } catch (const std::exception& e) {
        t_last_error = std::string("创建 UI 线程失败：") + e.what();
        return PARM_UI_E_INTERNAL;
    }
    return PARM_UI_OK;
}

void parm_ui_free(char* p) { std::free(p); }

const char* parm_ui_last_error(void) { return t_last_error.c_str(); }

void parm_ui_version(int* major, int* minor, int* patch) {
    if (major != nullptr) *major = 1;
    if (minor != nullptr) *minor = 0;
    if (patch != nullptr) *patch = 0;
}

}  // extern "C"
