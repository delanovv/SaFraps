#pragma once
#include <string>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <d3d9.h>
#include <wrl/client.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

#include "ErrorCodes.h"
#include "Logger.h"
#include "FPSLimiter.h"
#include "FrameData.h"
#include "SurfacePool.h"
#include "AudioCapture.h"
#include "AudioEncoder.h"
#include "VideoEncoder.h"

using Microsoft::WRL::ComPtr;

class VideoWriter {
public:
    struct Config {
        std::string outputPath;
        int         width       = 0;
        int         height      = 0;
        int         fps         = 30;
        int         crf         = 23;
        std::string preset;
        bool        enableAudio = true;
        int         sampleRate  = 44100;
        int         channels    = 2;
        bool        enableLog   = true;
        LogLevel    logLevel    = LogLevel::INFO;
    };

    VideoWriter();
    ~VideoWriter();
    VideoWriter(const VideoWriter&) = delete;
    VideoWriter& operator=(const VideoWriter&) = delete;

    HRESULT init(IDirect3DDevice9* device, const Config& config);
    void    startRecording();
    void    stopRecording();
    void    captureFrame(IDirect3DDevice9* device);

private:
    HRESULT initFFmpeg();
    void    freeFFmpeg();

    static void parallelCopy(uint8_t* dst, const uint8_t* src, size_t size);

    Config  m_config;
    Logger  m_logger;
    bool    m_initialized = false;

    FPSLimiter   m_limiter;
    SurfacePool  m_surfacePool{m_logger};

    ComPtr<IDirect3DSurface9> m_intermediateSurface;

    std::chrono::steady_clock::time_point m_startTime;

    std::queue<RawFrame>    m_rawQueue;
    std::mutex              m_rawMutex;
    std::condition_variable m_rawCV;

    std::queue<AudioFrame>  m_audioQueue;
    std::mutex              m_audioMutex;
    std::condition_variable m_audioCV;

    AVFormatContext* m_fmtCtx       = nullptr;
    AVCodecContext*  m_videoCodecCtx = nullptr;
    AVCodecContext*  m_audioCodecCtx = nullptr;
    AVStream*        m_videoStream   = nullptr;
    AVStream*        m_audioStream   = nullptr;
    SwrContext*      m_swrCtx        = nullptr;
    std::mutex       m_writeMutex;

    std::unique_ptr<AudioCapture> m_audioCapture;
    std::unique_ptr<AudioEncoder> m_audioEncoder;
    std::unique_ptr<VideoEncoder> m_videoEncoder;

    static constexpr size_t MAX_QUEUE_SIZE = 100;
};
