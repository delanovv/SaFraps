#include "BufferPool.h"

BufferPool::BufferPool(size_t maxPoolSize) : m_maxSize(maxPoolSize) {}

BufferPool::~BufferPool() {
    {
        std::lock_guard<std::mutex> lock(m_rawMutex);
        drainPool(m_rawPool);
    }
    {
        std::lock_guard<std::mutex> lock(m_yuvMutex);
        drainPool(m_yuvPool);
    }
}

void BufferPool::drainPool(std::vector<uint8_t*>& pool) {
    for (auto* p : pool)
        delete[] p;
    pool.clear();
}

uint8_t* BufferPool::acquireRaw(size_t size) {
    std::lock_guard<std::mutex> lock(m_rawMutex);
    if (!m_rawPool.empty()) {
        uint8_t* buf = m_rawPool.back();
        m_rawPool.pop_back();
        return buf;
    }
    return new uint8_t[size];
}

void BufferPool::releaseRaw(uint8_t* buf) {
    std::lock_guard<std::mutex> lock(m_rawMutex);
    if (m_rawPool.size() < m_maxSize)
        m_rawPool.push_back(buf);
    else
        delete[] buf;
}

uint8_t* BufferPool::acquireYuv(size_t size) {
    std::lock_guard<std::mutex> lock(m_yuvMutex);
    if (!m_yuvPool.empty()) {
        uint8_t* buf = m_yuvPool.back();
        m_yuvPool.pop_back();
        return buf;
    }
    return new uint8_t[size];
}

void BufferPool::releaseYuv(uint8_t* buf) {
    std::lock_guard<std::mutex> lock(m_yuvMutex);
    if (m_yuvPool.size() < m_maxSize)
        m_yuvPool.push_back(buf);
    else
        delete[] buf;
}
