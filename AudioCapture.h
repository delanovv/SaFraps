#pragma once
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "FrameData.h"
#include "Logger.h"
#include <Windows.h>
class AudioCapture {
public:
    AudioCapture(Logger&                               logger,
                 std::queue<AudioFrame>&               outQueue,
                 std::mutex&                           outMutex,
                 std::condition_variable&              outCV,
                 std::chrono::steady_clock::time_point startTime,
                 int                                   sampleRate,
                 int                                   channels);
    ~AudioCapture();
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    void start();
    void stop();

    std::thread::native_handle_type nativeHandle() { return m_thread.native_handle(); }

private:
    void captureLoop();

    Logger&                               m_logger;
    std::queue<AudioFrame>&               m_outQueue;
    std::mutex&                           m_outMutex;
    std::condition_variable&              m_outCV;
    std::chrono::steady_clock::time_point m_startTime;
    int                                   m_sampleRate;
    int                                   m_channels;

    std::atomic<bool> m_stop{false};
    std::thread       m_thread;

    static constexpr size_t MAX_QUEUE_SIZE = 100;
};
