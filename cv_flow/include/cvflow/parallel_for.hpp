/*=============================================================================
 * cv_flow/src/parallel_for.hpp — 内部小工具：常驻线程池 + 并行 for
 * 从 cvr.cpp 的同名实现原样复制（cv_flow 独立后自带一份，不反向依赖 cvr）。
 * 语义不变：嵌套并行自动降级串行；fn 内异常汇聚到主线程重抛；
 *          fn 只写自己的本地/独享缓冲。
 *===========================================================================*/
#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace cvflow {
namespace par {

inline int hardware_threads() {
    int hw = (int)std::thread::hardware_concurrency();
    return hw <= 1 ? 1 : hw;
}

// 当前线程是否已在并行区内（嵌套并行 → 串行，避免线程池自锁 + 过度订阅）
inline int& depth_slot() { static thread_local int d = 0; return d; }

class Pool {
public:
    static Pool& get() {
        static Pool inst;
        return inst;
    }
    void submit(const std::function<void()>& job) {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_tasks.push(job);
            ++m_pending;
        }
        m_cv.notify_one();
    }
    void waitAll() {
        std::unique_lock<std::mutex> lk(m_mtx);
        m_done.wait(lk, [this] { return m_pending == 0; });
    }
    int executors() const { return (int)m_threads.size() + 1; }
private:
    Pool() {
        int hw = hardware_threads();
        for (int i = 0; i < hw - 1; ++i)
            m_threads.emplace_back([this] { workerLoop(); });
    }
    ~Pool() {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_stop = true;
        }
        m_cv.notify_all();
        for (size_t i = 0; i < m_threads.size(); ++i)
            if (m_threads[i].joinable()) m_threads[i].join();
    }
    void workerLoop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(m_mtx);
                m_cv.wait(lk, [this] { return m_stop || !m_tasks.empty(); });
                if (m_tasks.empty()) {
                    if (m_stop) return;
                    continue;
                }
                job = std::move(m_tasks.front());
                m_tasks.pop();
            }
            depth_slot() = 1;   // worker 内视为"已在并行区"：嵌套调用走串行
            job();
            depth_slot() = 0;
            {
                std::lock_guard<std::mutex> lk(m_mtx);
                if (--m_pending == 0) m_done.notify_all();
            }
        }
    }
    std::vector<std::thread>          m_threads;
    std::queue<std::function<void()>> m_tasks;
    std::mutex                        m_mtx;
    std::condition_variable           m_cv, m_done;
    int                               m_pending = 0;
    bool                              m_stop = false;
};

// 并行 for：fn(i) 对 i ∈ [0, n) 各调用一次；min_grain 以下或嵌套时走串行
template <class Fn>
void parallel_for(int n, int min_grain, Fn&& fn) {
    if (n <= 0) return;
    if (depth_slot() > 0) {
        for (int i = 0; i < n; ++i) fn(i);
        return;
    }
    Pool& pool = Pool::get();
    const int nThreads = std::min(pool.executors(), n);
    if (nThreads <= 1 || n < min_grain) {
        for (int i = 0; i < n; ++i) fn(i);
        return;
    }
    std::atomic<int> next{0};
    std::exception_ptr eptr = nullptr;
    std::mutex mtx;
    auto worker = [&]() {
        try {
            for (;;) {
                int i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= n) break;
                fn(i);
            }
        } catch (...) {
            std::lock_guard<std::mutex> lk(mtx);
            if (!eptr) eptr = std::current_exception();
        }
    };
    depth_slot() = 1;
    for (int t = 0; t < nThreads - 1; ++t) pool.submit(worker);
    worker();
    pool.waitAll();
    depth_slot() = 0;
    if (eptr) std::rethrow_exception(eptr);
}

} // namespace par
} // namespace cvflow
