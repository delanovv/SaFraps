#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <emmintrin.h>
#include "ThreadPool.h"

inline void ntMemcpy(uint8_t* dst, const uint8_t* src, size_t size) {
    const __m128i* s  = reinterpret_cast<const __m128i*>(src);
    __m128i*       d  = reinterpret_cast<__m128i*>(dst);
    size_t         blocks = size / 16;

    for (size_t i = 0; i < blocks; ++i)
        _mm_stream_si128(d + i, _mm_loadu_si128(s + i));

    _mm_sfence();

    size_t tail = size % 16;
    if (tail)
        std::memcpy(dst + blocks * 16, src + blocks * 16, tail);
}

inline void parallelNtCopy(uint8_t* dst, const uint8_t* src, size_t size, ThreadPool& pool) {
    constexpr size_t PARALLEL_THRESHOLD = 2 * 1024 * 1024;

    if (size < PARALLEL_THRESHOLD || pool.threadCount() <= 1) {
        ntMemcpy(dst, src, size);
        return;
    }

    size_t numThreads = pool.threadCount();
    size_t chunkSize  = size / numThreads;
    std::vector<std::future<void>> futures;
    futures.reserve(numThreads);

    for (size_t i = 0; i < numThreads; ++i) {
        size_t offset    = i * chunkSize;
        size_t thisChunk = (i == numThreads - 1) ? size - offset : chunkSize;
        futures.push_back(pool.enqueue([=] {
            ntMemcpy(dst + offset, src + offset, thisChunk);
        }));
    }
    for (auto& f : futures) f.get();
}
