#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

// ═══════════════════════════════════════════════════════════════════════
// ThreadPool — Generic, reusable thread pool
//
// OS Concept: Thread Pool Pattern
// ─────────────────────────────────────────────────────────────────────
// A thread pool pre-creates a fixed number of worker threads that sit
// idle until work is enqueued. This avoids the cost of thread creation/
// destruction per task (pthread_create is ~20–50µs on Linux; for
// thousands of chunks, that overhead adds up).
//
// Why condition_variable instead of busy-waiting (spin-lock)?
// ─────────────────────────────────────────────────────────────────────
// Busy-waiting (while(!hasWork) {}) burns CPU cycles continuously,
// wasting power and starving other threads of CPU time. A condition
// variable puts the thread to sleep via the kernel's futex mechanism
// (on Linux), consuming zero CPU until explicitly woken by notify_one.
// This is critical for a compression tool: while one thread compresses
// a chunk, the other workers should sleep — not spin.
//
// Spurious Wakeups
// ─────────────────────────────────────────────────────────────────────
// POSIX and C++ standards allow condition_variable::wait() to return
// even when no notify was called (due to kernel signal handling,
// scheduler internals, or hardware interrupts). We guard against this
// by passing a predicate to wait(): the lambda checks if the queue
// is non-empty or shutdown is signaled. If the wakeup is spurious
// (queue empty, not shutting down), wait() re-sleeps automatically.
// ═══════════════════════════════════════════════════════════════════════

#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <stdexcept>

class ThreadPool {
public:
    // Creates `numThreads` worker threads that block on the task queue.
    explicit ThreadPool(size_t numThreads);

    // Enqueue a callable task. Returns a future to wait on completion.
    // The future carries void; use it to detect exceptions thrown by the task.
    template<class F>
    std::future<void> enqueue(F&& task);

    // Signal all workers to finish their current task and exit.
    // Blocks until all threads have joined.
    // Safe to call multiple times (idempotent).
    void shutdown();

    // RAII: calls shutdown() if not already called.
    ~ThreadPool();

    // Deleted copy/move — thread ownership is non-transferable.
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Query the number of worker threads.
    size_t numThreads() const noexcept { return workers_.size(); }

private:
    // Worker threads — created in constructor, joined in shutdown()
    std::vector<std::thread> workers_;

    // Task queue — protected by mutex_, signaled by cv_
    std::deque<std::function<void()>> tasks_;

    // Synchronization primitives
    std::mutex              mutex_;
    std::condition_variable cv_;

    // Shutdown flag — set to true by shutdown(), checked by workers
    bool stopped_ = false;
};

// ─────────────────────────────────────────────────────────────────────
// Template implementation must be in the header (or an included .tpp)
// because the compiler needs to see the full definition at each
// instantiation site.
// ─────────────────────────────────────────────────────────────────────
template<class F>
std::future<void> ThreadPool::enqueue(F&& task) {
    // Wrap the task in a shared_ptr<packaged_task> so it's copyable
    // (std::function requires copyable callables, but packaged_task
    // is move-only). The shared_ptr adds a small allocation but avoids
    // type-erasure gymnastics.
    auto packaged = std::make_shared<std::packaged_task<void()>>(
        std::forward<F>(task)
    );
    std::future<void> future = packaged->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            throw std::runtime_error(
                "ThreadPool::enqueue: Cannot enqueue after shutdown");
        }
        tasks_.emplace_back([packaged]() { (*packaged)(); });
    }

    // Wake ONE sleeping worker to pick up the new task.
    // notify_one is preferred over notify_all here because we added
    // exactly one task — waking all workers would cause a thundering
    // herd where N-1 workers wake up, find nothing, and re-sleep.
    cv_.notify_one();

    return future;
}

#endif // THREAD_POOL_HPP
