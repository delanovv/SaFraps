#pragma once
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

#include "FrameData.h"
#include "BufferPool.h"
#include "ThreadPool.h"
#include "Logger.h"

class VideoEncoder {
public:
    VideoEncoder(Logger&                  logger,
                 std::queue<RawFrame>&    rawInQueue,
                 std::mutex&              rawInMutex,
                 std::condition_variable& rawInCV,
                 AVFormatContext*         fmtCtx,
                 AVCodecContext*          codecCtx,
                 AVStream*                stream,
                 std::mutex&              writeMutex,
                 uint32_t                 width,
                 uint32_t                 height);
    ~VideoEncoder();
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    void   start();
    void   stop();
    std::thread::native_handle_type rawThreadHandle() { return m_rawThread.native_handle();    }
    std::thread::native_handle_type encodeThreadHandle() { return m_encodeThread.native_handle(); }

private:
    void rawProcessLoop();
    void encodeLoop();
    void flushCodec();

    Logger&                  m_logger;
    std::queue<RawFrame>&    m_rawInQueue;
    std::mutex&              m_rawInMutex;
    std::condition_variable& m_rawInCV;
    AVFormatContext*         m_fmtCtx;
    AVCodecContext*          m_codecCtx;
    AVStream*                m_stream;
    std::mutex&              m_writeMutex;
    uint32_t                 m_width;
    uint32_t                 m_height;

    BufferPool  m_bufferPool;
    ThreadPool  m_threadPool;

    std::vector<uint8_t> m_tempY;
    std::vector<uint8_t> m_tempU;
    std::vector<uint8_t> m_tempV;

    std::queue<YuvFrame>    m_yuvQueue;
    std::mutex              m_yuvMutex;
    std::condition_variable m_yuvCV;

    std::atomic<bool> m_stopRaw{false};
    std::atomic<bool> m_stopEncode{false};

    std::thread m_rawThread;
    std::thread m_encodeThread;

    AVFrame*  m_frame        = nullptr;
    AVPacket* m_pkt          = nullptr;
    int64_t   m_frameCounter = 0;

    static constexpr size_t MAX_QUEUE_SIZE = 100;
};
