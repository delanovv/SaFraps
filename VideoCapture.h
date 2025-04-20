#pragma once
#ifndef VIDEO_CAPTURE_H
#define VIDEO_CAPTURE_H

#include "d3d9.h"
extern "C"
{
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/time.h"
#include "libswresample/swresample.h"
#include "libavutil/opt.h"
#include "x264.h"
}
#include "fstream"
#include "iostream"
#include "chrono"
#include "thread"
#include "string"
#include "queue"
#include "mutex"
#include "condition_variable"
#include "vector"
#include "Audioclient.h"
#include "mmdeviceapi.h"
#include "avrt.h"

// Пользовательские коды ошибок
#define CUSTOM_HRESULT_BASE 0x80040000
#define E_LOGFILE_OPEN_FAILED (CUSTOM_HRESULT_BASE + 1)
#define E_BACKBUFFER_FAILED (CUSTOM_HRESULT_BASE + 2)
#define E_FFMPEG_INIT_FAILED (CUSTOM_HRESULT_BASE + 3)
#define E_FFMPEG_CONTEXT_FAILED (CUSTOM_HRESULT_BASE + 4)
#define E_FFMPEG_STREAM_FAILED (CUSTOM_HRESULT_BASE + 5)
#define E_FFMPEG_CODEC_NOT_FOUND (CUSTOM_HRESULT_BASE + 6)
#define E_FFMPEG_CODEC_ALLOC_FAILED (CUSTOM_HRESULT_BASE + 7)
#define E_FFMPEG_CODEC_OPEN_FAILED (CUSTOM_HRESULT_BASE + 8)
#define E_FFMPEG_FILE_OPEN_FAILED (CUSTOM_HRESULT_BASE + 9)
#define E_FFMPEG_HEADER_WRITE_FAILED (CUSTOM_HRESULT_BASE + 10)
#define E_FFMPEG_FRAME_ALLOC_FAILED (CUSTOM_HRESULT_BASE + 11)

struct AudioSample
{
    uint8_t *data;
    size_t size;
    int64_t pts; // В микросекундах
    AudioSample(uint8_t *d, size_t s, int64_t p) : data(d), size(s), pts(p) {}
    ~AudioSample() { delete[] data; }
};

class FPSLimiter
{
public:
    FPSLimiter() : targetFrameTime(1.0f / 200.0f), lastFrameTime(std::chrono::steady_clock::now()) {}
    FPSLimiter(float fps) : targetFrameTime(1.0f / fps), lastFrameTime(std::chrono::steady_clock::now()) {}
    void limitFPS();

private:
    float targetFrameTime;
    std::chrono::steady_clock::time_point lastFrameTime;
};
enum class LogLevel
{
    NONE = 0,
    ERR = 1,
    INFO = 2,
    DEBUG = 3
};

class VideoWriter
{
public:
    VideoWriter();
    ~VideoWriter();
    HRESULT init(IDirect3DDevice9 *pDevice);
    void startRecording();
    void stopRecording();
    void captureFrame(IDirect3DDevice9 *pDevice);
    void captureAudio();
    void encodeFrame();
    void encodeAudioFrame();
    void processRawFrames();
    void sendAudioFrame(std::vector<float> audioBuffer[2], int64_t nPts, int requiredSamples);
    bool ensureInitialized();
    HRESULT initFFmpeg();
    void logMessage(const std::string &message, LogLevel level);
    uint8_t *getReusableRawBuffer(size_t size);
    uint8_t *getReusableFrameBuffer();
    void freeResources();

    // DirectX 9 ресурсы
    IDirect3DSurface9 *m_pCachedTempSurface = nullptr;
    IDirect3DTexture9 *m_pCachedRenderTexture = nullptr;       // Восстановлено
    IDirect3DSurface9 *m_pCachedIntermediateSurface = nullptr; // Восстановлено
    uint32_t m_cachedWidth = 0;
    uint32_t m_cachedHeight = 0;
    D3DFORMAT m_cachedFormat = D3DFMT_UNKNOWN;
    D3DFORMAT m_eFormat = D3DFMT_UNKNOWN;

    // FFmpeg ресурсы
    AVFormatContext *m_pFmtCtx = nullptr;
    AVStream *m_pVideoStream = nullptr;
    AVStream *m_pAudioStream = nullptr;
    AVCodecContext *m_pCodecCtx = nullptr;
    AVCodecContext *m_pAudioCodecCtx = nullptr;
    AVFrame *m_pFrame = nullptr;
    AVFrame *m_pAudioFrame = nullptr;
    AVPacket *m_pPkt = nullptr;
    AVPacket *m_pAudioPkt = nullptr;
    SwrContext *m_pSwrCtx = nullptr;
    x264_t *m_pEncoder = nullptr;
    x264_picture_t *m_pPicIn = nullptr;

    // Буферы
    uint8_t *m_pFrameBuffer = nullptr;
    std::vector<uint8_t *> m_frameBufferPool;
    std::vector<uint8_t *> m_rawBufferPool;

    // Очереди
    struct RawFrame
    {
        uint8_t *buffer;
        size_t size;
        uint32_t width, height;
        int stride;
        int64_t pts;
        RawFrame(uint8_t *b, size_t s, uint32_t w, uint32_t h, int str, int64_t p)
            : buffer(b), size(s), width(w), height(h), stride(str), pts(p) {}
    };
    std::queue<RawFrame> m_pRawFrameQueue;
    std::mutex m_pRawQueueMutex;
    std::condition_variable m_pRawQueueCondition;

    struct Frame
    {
        uint8_t *buffer;
        int64_t pts;
        Frame(uint8_t *b, int64_t p) : buffer(b), pts(p) {}
    };
    std::queue<Frame> m_pFrameQueue;
    std::mutex m_pQueueMutex;
    std::condition_variable m_pQueueCondition;

    struct AudioFrame
    {
        uint8_t *data;
        int64_t pts;
        size_t frameCount;
        AudioFrame(uint8_t *d, int64_t p, size_t fc) : data(d), pts(p), frameCount(fc) {}
    };
    std::queue<AudioFrame> m_pAudioQueue;
    std::mutex m_pAudioQueueMutex;
    std::condition_variable m_pAudioQueueCondition;

    // Потоки
    std::thread m_pEncodeThread;
    std::thread m_pRawProcessThread;
    std::thread m_pAudioCaptureThread;
    std::thread m_pAudioEncodeThread;

    // Флаги
    bool m_bInitialized = false;
    bool m_bStopEncoding = false;
    bool m_bStopRawProcessing = false;
    bool m_bStopAudioEncoding = false;
    bool m_bEnableAudio = true;
    bool m_bEnableLogging = true;

    // Параметры записи
    std::string m_sOutputPath = "output.mp4";
    std::string m_sPreset = "medium";
    int m_nWidth = 0;
    int m_nHeight = 0;
    int m_nFps = 60;
    int m_nCrf = 23;
    int m_nSampleRate = 44100;
    int m_nChannels = 2;
    LogLevel m_eCurrentLogLevel = LogLevel::INFO;

    // Время
    std::chrono::steady_clock::time_point m_pStartTime;
    FPSLimiter m_oLimiter;
};

#endif // VIDEO_CAPTURE_H