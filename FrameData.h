#pragma once
#include <cstdint>
#include <cstddef>

struct RawFrame {
    uint8_t* buffer    = nullptr;
    size_t   size      = 0;
    uint32_t width     = 0;
    uint32_t height    = 0;
    int      stride    = 0;
    int64_t  pts       = 0;
};

struct YuvFrame {
    uint8_t* buffer = nullptr;
    int64_t  pts    = 0;
};

struct AudioFrame {
    uint8_t* data       = nullptr;
    int64_t  pts        = 0;
    uint32_t frameCount = 0;
};
