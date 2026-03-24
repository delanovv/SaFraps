#include "FPSLimiter.h"

FPSLimiter::FPSLimiter(float fps)
    : m_targetFrameTime(1.0f / fps)
    , m_lastFrameTime(std::chrono::high_resolution_clock::now())
{}

bool FPSLimiter::tick() {
    auto now     = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastFrameTime);
    auto target  = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::duration<float>(m_targetFrameTime));

    if (elapsed < target)
        return false;

    m_lastFrameTime = now;
    return true;
}
