#pragma once
#include <chrono>

class FPSLimiter {
public:
    FPSLimiter() = default;
    explicit FPSLimiter(float fps);

    bool tick();

private:
    float m_targetFrameTime = 1.0f / 30.0f;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime{};
};
