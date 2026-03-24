#pragma once
#include <vector>
#include <mutex>
#include <cstdint>
#include <cstddef>

class BufferPool {
public:
    explicit BufferPool(size_t maxPoolSize = 100);
    ~BufferPool();
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    uint8_t* acquireRaw(size_t size);
    void     releaseRaw(uint8_t* buf);

    uint8_t* acquireYuv(size_t size);
    void     releaseYuv(uint8_t* buf);

private:
    size_t m_maxSize;

    std::mutex             m_rawMutex;
    std::vector<uint8_t*> m_rawPool;

    std::mutex             m_yuvMutex;
    std::vector<uint8_t*> m_yuvPool;

    static void drainPool(std::vector<uint8_t*>& pool);
};
