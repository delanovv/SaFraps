#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <future>
#include <functional>
#include <atomic>

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template<typename F>
    std::future<void> enqueue(F&& task) {
        auto promise = std::make_shared<std::promise<void>>();
        auto future  = promise->get_future();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tasks.push([p = std::move(promise), t = std::forward<F>(task)]() mutable {
                t();
                p->set_value();
            });
        }
        m_cv.notify_one();
        return future;
    }

    size_t threadCount() const { return m_workers.size(); }

private:
    void workerLoop();

    std::vector<std::thread>          m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex                        m_mutex;
    std::condition_variable           m_cv;
    std::atomic<bool>                 m_stop{false};
};
