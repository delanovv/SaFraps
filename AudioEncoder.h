#pragma once
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <Windows.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

#include "FrameData.h"
#include "Logger.h"

class AudioEncoder {
public:
    AudioEncoder(Logger&                  logger,
                 std::queue<AudioFrame>&  inQueue,
                 std::mutex&              inMutex,
                 std::condition_variable& inCV,
                 AVFormatContext*         fmtCtx,
                 AVCodecContext*          codecCtx,
                 AVStream*                stream,
                 SwrContext*              swrCtx,
                 std::mutex&              writeMutex,
                 int                      sampleRate,
                 int                      channels);
    ~AudioEncoder();
    AudioEncoder(const AudioEncoder&) = delete;
    AudioEncoder& operator=(const AudioEncoder&) = delete;

    void   start();
    void   stop();
    HANDLE nativeHandle() { return m_thread.native_handle(); }

private:
    void encodeLoop();
    void sendFrame(std::vector<std::vector<float>>& buffer, int64_t pts, int requiredSamples);
    void flushCodec(int64_t lastPts);

    Logger&                  m_logger;
    std::queue<AudioFrame>&  m_inQueue;
    std::mutex&              m_inMutex;
    std::condition_variable& m_inCV;
    AVFormatContext*         m_fmtCtx;
    AVCodecContext*          m_codecCtx;
    AVStream*                m_stream;
    SwrContext*              m_swrCtx;
    std::mutex&              m_writeMutex;
    int                      m_sampleRate;
    int                      m_channels;

    std::atomic<bool> m_stop{false};
    std::thread       m_thread;
    AVFrame*          m_frame = nullptr;
    AVPacket*         m_pkt   = nullptr;

    static constexpr size_t MAX_QUEUE_SIZE = 100;
};
