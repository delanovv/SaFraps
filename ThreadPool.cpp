#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t numThreads) {
    for (size_t i = 0; i < numThreads; ++i)
        m_workers.emplace_back(&ThreadPool::workerLoop, this);
}

ThreadPool::~ThreadPool() {
    m_stop = true;
    m_cv.notify_all();
    for (auto& t : m_workers)
        if (t.joinable()) t.join();
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop.load() || !m_tasks.empty(); });
            if (m_stop && m_tasks.empty()) return;
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        task();
    }
}
