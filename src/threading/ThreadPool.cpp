#include "threading/ThreadPool.hpp"

#include <iostream>  // std::cerr for diagnostics

// ─────────────────────────────────────────────────────────────────────
// Constructor: spawn worker threads
//
// Each worker runs an infinite loop:
//   1. Lock the mutex
//   2. Wait on the condition variable (with predicate guard)
//   3. Pop a task from the front of the deque
//   4. Unlock the mutex
//   5. Execute the task
//   6. Repeat
//
// The predicate in cv_.wait() guards against spurious wakeups:
//   - If the queue is empty AND we're not shutting down, go back to sleep
//   - If the queue is non-empty, pop and execute
//   - If shutdown is signaled AND queue is empty, exit the loop
// ─────────────────────────────────────────────────────────────────────
ThreadPool::ThreadPool(size_t numThreads) {
    workers_.reserve(numThreads);

    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back([this]() {
            // Worker thread main loop
            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(mutex_);

                    // Wait until there's a task or shutdown is requested.
                    //
                    // SPURIOUS WAKEUP GUARD: The lambda predicate is
                    // re-evaluated after every wakeup. If neither
                    // condition is true (spurious wakeup), wait()
                    // automatically re-blocks the thread. Without this
                    // predicate, a spurious wakeup would cause the worker
                    // to try popping from an empty queue → undefined behavior.
                    cv_.wait(lock, [this]() {
                        return stopped_ || !tasks_.empty();
                    });

                    // If shutdown was requested and no tasks remain, exit.
                    // We drain remaining tasks before exiting to ensure
                    // all enqueued work completes — this is important for
                    // correctness: callers hold futures that expect results.
                    if (stopped_ && tasks_.empty()) {
                        return;
                    }

                    // Pop the next task (FIFO order from deque front)
                    task = std::move(tasks_.front());
                    tasks_.pop_front();
                }
                // Mutex is released here — the task executes WITHOUT
                // holding the lock. This is critical: if tasks held the
                // mutex, only one task could run at a time, defeating
                // the purpose of multithreading.

                // Execute the task. Any exception is captured by the
                // packaged_task and delivered through the future.
                task();
            }
        });
    }
}

// ─────────────────────────────────────────────────────────────────────
// shutdown(): Signal workers and join
//
// Order of operations:
//   1. Set stopped_ = true (under lock)
//   2. notify_all — wake ALL sleeping workers so they see stopped_
//   3. Join each thread — blocks until the worker's loop exits
//
// Idempotent: safe to call multiple times (second call finds workers_
// empty after the first call moved/joined all threads).
// ─────────────────────────────────────────────────────────────────────
void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) return;  // Already shut down
        stopped_ = true;
    }

    // Wake ALL workers — they need to see stopped_ == true.
    // notify_one would only wake one; the rest would sleep forever.
    cv_.notify_all();

    // Join each worker thread.
    // std::thread::join() blocks until the thread exits.
    // After join, the thread object is no longer joinable.
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────
// Destructor: RAII guarantee — always join threads.
//
// If the caller forgot to call shutdown(), the destructor does it.
// This prevents std::terminate() from being called when a joinable
// std::thread is destroyed (C++ terminates the program if you destroy
// a joinable thread without joining or detaching it).
// ─────────────────────────────────────────────────────────────────────
ThreadPool::~ThreadPool() {
    shutdown();
}
