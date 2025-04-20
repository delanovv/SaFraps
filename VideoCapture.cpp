#include "VideoCapture.h"
#include <iostream>
#include "libyuv.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <combaseapi.h>

std::mutex g_ffmpegWriteMutex;
#define SAFE_RELEASE(p) if ((p)) { (p)->Release(); (p) = nullptr; }
void FPSLimiter::limitFPS() {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - lastFrameTime);
    auto targetDuration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::duration<float>(targetFrameTime));
    auto sleepTime = targetDuration - elapsed;

    if (sleepTime > std::chrono::microseconds(0)) {
        std::this_thread::sleep_for(sleepTime);
    }
    lastFrameTime = now;
}
void VideoWriter::logMessage(const std::string& message, LogLevel level) {
    static bool isFirstLog = true;
    if (!m_bEnableLogging || static_cast<int>(level) > static_cast<int>(m_eCurrentLogLevel)) {
        return;
    }

    std::ofstream logFile;
    if (isFirstLog) {
        logFile.open("GrandFraps/log.txt", std::ios::out);
        isFirstLog = false;
    }
    else {
        logFile.open("GrandFraps/log.txt", std::ios::app);
    }

    if (logFile.is_open()) {
        std::time_t currentTime = std::time(nullptr);
        char timeStr[100];
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", std::localtime(&currentTime));
        std::string levelStr = (level == LogLevel::ERR) ? "ERROR" : (level == LogLevel::DEBUG) ? "DEBUG" : "INFO";
        logFile << "[" << timeStr << "] [" << levelStr << "] " << message << std::endl;
    }
}

VideoWriter::VideoWriter() : m_pStartTime(std::chrono::steady_clock::now()) {
    logMessage("VideoWriter constructed", LogLevel::INFO);
}

VideoWriter::~VideoWriter() {
    stopRecording();
    if (m_pSwrCtx) {
        swr_free(&m_pSwrCtx);
    }
    logMessage("VideoWriter destroyed", LogLevel::INFO);
}

void VideoWriter::startRecording() {
    if (!m_bInitialized) {
        logMessage("StartRecording failed: Not initialized", LogLevel::ERR);
        return;
    }

    m_bStopEncoding = false;
    m_bStopAudioEncoding = false;
    m_pEncodeThread = std::thread(&VideoWriter::encodeFrame, this);
    m_bStopRawProcessing = false;

    m_pRawProcessThread = std::thread(&VideoWriter::processRawFrames, this);
    if (m_bEnableAudio) {
        m_pAudioCaptureThread = std::thread(&VideoWriter::captureAudio, this);
        m_pAudioEncodeThread = std::thread(&VideoWriter::encodeAudioFrame, this);
    }
    logMessage("Recording started with audio", LogLevel::INFO);
}

HRESULT VideoWriter::init(IDirect3DDevice9* pDevice) {
    if (m_bInitialized) {
        logMessage("Initialization failed: Already initialized", LogLevel::ERR);
        return E_FAIL;
    }
    else {
        logMessage("Оно заработало?", LogLevel::INFO);
    }
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) {
        logMessage("Failed to initialize COM", LogLevel::ERR);
        return hr;
    }

    IDirect3DSurface9* pSurface = nullptr;
    if (FAILED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pSurface))) {
        logMessage("Failed to get back buffer", LogLevel::ERR);
        return E_BACKBUFFER_FAILED;
    }

    D3DSURFACE_DESC surfaceDesc;
    pSurface->GetDesc(&surfaceDesc);
    m_eFormat = surfaceDesc.Format;

    if (m_nWidth == 0 || m_nHeight == 0) {
        m_nWidth = surfaceDesc.Width;
        m_nHeight = surfaceDesc.Height;
        logMessage("Width and height not specified, using back buffer resolution: " +
            std::to_string(m_nWidth) + "x" + std::to_string(m_nHeight), LogLevel::INFO);
    }
    else {
        logMessage("Using specified resolution: " + std::to_string(m_nWidth) + "x" + std::to_string(m_nHeight), LogLevel::INFO);
    }

    pSurface->Release();

    m_oLimiter = FPSLimiter(static_cast<float>(m_nFps));
    m_pStartTime = std::chrono::steady_clock::now();
    hr = initFFmpeg();
    if (FAILED(hr)) {
        logMessage("FFmpeg initialization failed", LogLevel::ERR);
        return hr;
    }

    m_bInitialized = true;
    logMessage("Initialization successful: width=" + std::to_string(m_nWidth) + ", height=" + std::to_string(m_nHeight) +
        ", fps=" + std::to_string(m_nFps), LogLevel::INFO);
    return S_OK;
}

HRESULT VideoWriter::initFFmpeg() {
    avformat_network_init();

    if (avformat_alloc_output_context2(&m_pFmtCtx, nullptr, nullptr, m_sOutputPath.c_str()) < 0) {
        logMessage("Failed to allocate output context", LogLevel::ERR);
        return E_FFMPEG_CONTEXT_FAILED;
    }

    m_pVideoStream = avformat_new_stream(m_pFmtCtx, nullptr);
    if (!m_pVideoStream) {
        logMessage("Failed to create new video stream", LogLevel::ERR);
        return E_FFMPEG_STREAM_FAILED;
    }

    const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc");
    bool useNVENC = true;
    if (!codec) {
        logMessage("h264_nvenc not found, falling back to libx264", LogLevel::DEBUG);
        codec = avcodec_find_encoder_by_name("libx264");
        useNVENC = false;
        if (!codec) {
            logMessage("libx264 not found", LogLevel::ERR);
            return E_FFMPEG_CODEC_NOT_FOUND;
        }
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        logMessage("Failed to allocate codec context", LogLevel::ERR);
        return E_FFMPEG_CODEC_ALLOC_FAILED;
    }

    codecCtx->width = m_nWidth;
    codecCtx->height = m_nHeight;
    codecCtx->time_base = { 1, m_nFps };
    codecCtx->framerate = { m_nFps, 1 };
    codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    codecCtx->gop_size = m_nFps;
    codecCtx->max_b_frames = 0;
    codecCtx->bit_rate = 0;

    AVDictionary* opts = nullptr;
    std::string preset = m_sPreset.empty() ? "p1" : m_sPreset;
    if (useNVENC) {
        if (preset != "p1" && preset != "p2" && preset != "p3" &&
            preset != "p4" && preset != "p5" && preset != "p6" && preset != "p7") {
            logMessage("Invalid NVENC preset '" + preset + "', using default 'p4'", LogLevel::ERR);
            preset = "p4";
        }
        av_dict_set(&opts, "preset", preset.c_str(), 0);
        av_dict_set(&opts, "rc", "constqp", 0); 
        av_dict_set_int(&opts, "qp", m_nCrf, 0);
        av_dict_set(&opts, "delay", "0", 0);
    }
    else {
        static const std::string x264_presets[] = {
            "ultrafast", "superfast", "veryfast", "faster", "fast",
            "medium", "slow", "slower", "veryslow"
        };
        if (std::find(std::begin(x264_presets), std::end(x264_presets), preset) == std::end(x264_presets)) {
            logMessage("Invalid x264 preset '" + preset + "', using default 'medium'", LogLevel::ERR);
            preset = "medium";
        }
        av_dict_set(&opts, "preset", preset.c_str(), 0);
        av_dict_set(&opts, "tune", "zerolatency", 0);
        av_dict_set_int(&opts, "crf", m_nCrf, 0);
    }
    av_dict_set_int(&opts, "crf", m_nCrf, 0);

    if (avcodec_open2(codecCtx, codec, &opts) < 0) {
        logMessage("Failed to open codec", LogLevel::ERR);
        avcodec_free_context(&codecCtx);
        av_dict_free(&opts);
        return E_FFMPEG_CODEC_OPEN_FAILED;
    }
    av_dict_free(&opts);

    avcodec_parameters_from_context(m_pVideoStream->codecpar, codecCtx);
    m_pVideoStream->time_base = codecCtx->time_base;
    m_pCodecCtx = codecCtx; 

    if (m_bEnableAudio) {
        m_pAudioStream = avformat_new_stream(m_pFmtCtx, nullptr);
        if (!m_pAudioStream) {
            logMessage("Failed to create new audio stream", LogLevel::ERR);
            return E_FFMPEG_STREAM_FAILED;
        }

        const AVCodec* audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!audioCodec) {
            logMessage("AAC codec not found", LogLevel::ERR);
            return E_FFMPEG_CODEC_NOT_FOUND;
        }

        m_pAudioCodecCtx = avcodec_alloc_context3(audioCodec);
        if (!m_pAudioCodecCtx) {
            logMessage("Failed to allocate audio codec context", LogLevel::ERR);
            return E_FFMPEG_CODEC_ALLOC_FAILED;
        }

        m_pAudioCodecCtx->sample_rate = m_nSampleRate;
        m_pAudioCodecCtx->channels = m_nChannels;
        m_pAudioCodecCtx->channel_layout = av_get_default_channel_layout(m_nChannels);
        m_pAudioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
        m_pAudioCodecCtx->bit_rate = 128000;
        m_pAudioCodecCtx->time_base = { 1, m_nSampleRate };

        if (avcodec_open2(m_pAudioCodecCtx, audioCodec, nullptr) < 0) {
            logMessage("Failed to open audio codec", LogLevel::ERR);
            return E_FFMPEG_CODEC_OPEN_FAILED;
        }

        avcodec_parameters_from_context(m_pAudioStream->codecpar, m_pAudioCodecCtx);
        m_pAudioStream->time_base = { 1, m_nSampleRate };
    }

    if (avio_open(&m_pFmtCtx->pb, m_sOutputPath.c_str(), AVIO_FLAG_WRITE) < 0) {
        logMessage("Failed to open output file: " + m_sOutputPath, LogLevel::ERR);
        return E_FFMPEG_FILE_OPEN_FAILED;
    }

    AVDictionary* options = nullptr;
    av_dict_set(&options, "movflags", "faststart", 0);
    if (avformat_write_header(m_pFmtCtx, &options) < 0) {
        logMessage("Failed to write header", LogLevel::ERR);
        return E_FFMPEG_HEADER_WRITE_FAILED;
    }
    av_dict_free(&options);

    m_pFrame = av_frame_alloc();
    if (!m_pFrame) {
        logMessage("Failed to allocate AVFrame", LogLevel::ERR);
        return E_FFMPEG_FRAME_ALLOC_FAILED;
    }
    m_pFrame->format = AV_PIX_FMT_YUV420P;
    m_pFrame->width = m_nWidth;
    m_pFrame->height = m_nHeight;

    if (av_frame_get_buffer(m_pFrame, 0) < 0) {
        logMessage("Failed to allocate frame buffer", LogLevel::ERR);
        return E_FFMPEG_FRAME_ALLOC_FAILED;
    }

    m_pPkt = av_packet_alloc();
    if (!m_pPkt) {
        logMessage("Failed to allocate AVPacket", LogLevel::ERR);
        return E_FFMPEG_CODEC_ALLOC_FAILED;
    }

    if (m_bEnableAudio) {
        m_pAudioFrame = av_frame_alloc();
        if (!m_pAudioFrame) {
            logMessage("Failed to allocate audio AVFrame", LogLevel::ERR);
            return E_FFMPEG_FRAME_ALLOC_FAILED;
        }

        m_pAudioPkt = av_packet_alloc();
        if (!m_pAudioPkt) {
            logMessage("Failed to allocate audio AVPacket", LogLevel::ERR);
            return E_FFMPEG_CODEC_ALLOC_FAILED;
        }

        m_pSwrCtx = swr_alloc();
        av_opt_set_int(m_pSwrCtx, "in_channel_layout", av_get_default_channel_layout(m_nChannels), 0);
        av_opt_set_int(m_pSwrCtx, "out_channel_layout", av_get_default_channel_layout(m_nChannels), 0);
        av_opt_set_int(m_pSwrCtx, "in_sample_rate", m_nSampleRate, 0);
        av_opt_set_int(m_pSwrCtx, "out_sample_rate", m_nSampleRate, 0);
        av_opt_set_sample_fmt(m_pSwrCtx, "in_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
        av_opt_set_sample_fmt(m_pSwrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);

        if (swr_init(m_pSwrCtx) < 0) {
            logMessage("Failed to initialize swresample", LogLevel::ERR);
            return E_FFMPEG_INIT_FAILED;
        }
    }

    m_pFrameBuffer = (uint8_t*)_aligned_malloc(m_nWidth * m_nHeight * 4, 32);
    if (!m_pFrameBuffer) {
        logMessage("Failed to allocate frame buffer", LogLevel::ERR);
        return E_FFMPEG_INIT_FAILED;
    }

    logMessage("FFmpeg initialized successfully for file: " + m_sOutputPath + (useNVENC ? " with NVENC" : " with x264"), LogLevel::INFO);
    return S_OK;
}

bool VideoWriter::ensureInitialized() {
    if (!m_bInitialized) {
        logMessage("Operation failed: Not initialized", LogLevel::ERR);
        return false;
    }
    return true;
}

class SurfaceRAII {
    IDirect3DSurface9* ptr = nullptr;
public:
    SurfaceRAII() = default;
    explicit SurfaceRAII(IDirect3DSurface9* p) : ptr(p) {}
    ~SurfaceRAII() { if (ptr) ptr->Release(); }
    IDirect3DSurface9* get() const { return ptr; }
    SurfaceRAII(const SurfaceRAII&) = delete;
    SurfaceRAII& operator=(const SurfaceRAII&) = delete;
};

uint8_t* VideoWriter::getReusableFrameBuffer() {
    if (!m_frameBufferPool.empty()) {
        uint8_t* buffer = m_frameBufferPool.back();
        m_frameBufferPool.pop_back();
        return buffer;
    }
    return new uint8_t[m_nWidth * m_nHeight * 3 / 2];
}

#include <immintrin.h>

void VideoWriter::captureFrame(IDirect3DDevice9* pDevice) {
    if (!ensureInitialized()) return;

    m_oLimiter.limitFPS();
    if (!pDevice) {
        logMessage("Invalid Direct3D device pointer", LogLevel::ERR);
        return;
    }

    IDirect3DSurface9* backBuffer = nullptr;
    if (FAILED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))) {
        logMessage("Failed to get back buffer", LogLevel::ERR);
        return;
    }
    SurfaceRAII backSurface(backBuffer);

    D3DSURFACE_DESC desc;
    if (FAILED(backSurface.get()->GetDesc(&desc))) {
        logMessage("Failed to get surface description", LogLevel::ERR);
        return;
    }

    if (!m_pCachedTempSurface || desc.Width != m_cachedWidth || desc.Height != m_cachedHeight || desc.Format != m_cachedFormat) {
        SAFE_RELEASE(m_pCachedTempSurface);
        if (FAILED(pDevice->CreateOffscreenPlainSurface(
            desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &m_pCachedTempSurface, nullptr))) {
            logMessage("Failed to create offscreen plain surface", LogLevel::ERR);
            return;
        }
        m_cachedWidth = desc.Width;
        m_cachedHeight = desc.Height;
        m_cachedFormat = desc.Format;
        logMessage("Created new offscreen surface: " + std::to_string(m_cachedWidth) + "x" + std::to_string(m_cachedHeight), LogLevel::INFO);
    }
    if (desc.MultiSampleType == D3DMULTISAMPLE_NONE) {
        if (FAILED(pDevice->GetRenderTargetData(backSurface.get(), m_pCachedTempSurface))) {
            logMessage("Failed to get render target data (direct)", LogLevel::ERR);
            return;
        }
    }
    else {
        IDirect3DSurface9* intermediate = nullptr;
        if (FAILED(pDevice->CreateRenderTarget(desc.Width, desc.Height, desc.Format,
            D3DMULTISAMPLE_NONE, 0, FALSE, &intermediate, nullptr))) {
            logMessage("Failed to create intermediate render target", LogLevel::ERR);
            return;
        }
        SurfaceRAII surfaceIntermediate(intermediate);
        if (FAILED(pDevice->StretchRect(backSurface.get(), nullptr, surfaceIntermediate.get(), nullptr, D3DTEXF_NONE))) {
            logMessage("Failed to stretch rect", LogLevel::ERR);
            return;
        }
        if (FAILED(pDevice->GetRenderTargetData(surfaceIntermediate.get(), m_pCachedTempSurface))) {
            logMessage("Failed to get render target data (via intermediate)", LogLevel::ERR);
            return;
        }
    }

    D3DLOCKED_RECT locked;
    if (FAILED(m_pCachedTempSurface->LockRect(&locked, nullptr, D3DLOCK_READONLY | D3DLOCK_DONOTWAIT))) {
        logMessage("Failed to lock surface", LogLevel::ERR);
        return;
    }

    uint8_t* src = static_cast<uint8_t*>(locked.pBits);
    int srcStride = locked.Pitch;
    if (!src || srcStride <= 0) {
        m_pCachedTempSurface->UnlockRect();
        logMessage("Invalid surface data or stride", LogLevel::ERR);
        return;
    }

    size_t bufferSize = srcStride * desc.Height;
    uint8_t* rawBuffer = getReusableRawBuffer(bufferSize);
    for (size_t y = 0; y < desc.Height; ++y) {
        uint8_t* srcLine = src + y * srcStride;
        uint8_t* dstLine = rawBuffer + y * srcStride;
        for (size_t x = 0; x < srcStride; x += 16) {
            __m128i data = _mm_load_si128((__m128i*)(srcLine + x));
            _mm_stream_si128((__m128i*)(dstLine + x), data);
        }
    }
    _mm_sfence();
    m_pCachedTempSurface->UnlockRect();

    int64_t pts = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - m_pStartTime).count();

    {
        std::lock_guard<std::mutex> lock(m_pRawQueueMutex);
        m_pRawFrameQueue.emplace(rawBuffer, bufferSize, desc.Width, desc.Height, srcStride, pts);
    }
    m_pRawQueueCondition.notify_one();
}

uint8_t* VideoWriter::getReusableRawBuffer(size_t size) {
    for (auto* buffer : m_rawBufferPool) {
        m_rawBufferPool.erase(std::remove(m_rawBufferPool.begin(), m_rawBufferPool.end(), buffer), m_rawBufferPool.end());
        return buffer;
    }
    return new uint8_t[size];
}

void VideoWriter::processRawFrames() {
    logMessage("Raw frame processing thread started", LogLevel::INFO);
    while (!m_bStopRawProcessing) {
        std::unique_lock<std::mutex> lock(m_pRawQueueMutex);
        m_pRawQueueCondition.wait(lock, [this] { return !m_pRawFrameQueue.empty() || m_bStopRawProcessing; });

        if (m_bStopRawProcessing && m_pRawFrameQueue.empty()) {
            logMessage("Raw frame processing thread stopping", LogLevel::INFO);
            break;
        }

        auto frame = std::move(m_pRawFrameQueue.front());
        m_pRawFrameQueue.pop();
        lock.unlock();
        
        std::vector<uint8_t> tempY(frame.width * frame.height);
        std::vector<uint8_t> tempU(frame.width * frame.height / 4);
        std::vector<uint8_t> tempV(frame.width * frame.height / 4);

        int ret = libyuv::ConvertToI420(
            frame.buffer, frame.size,
            tempY.data(), frame.width,
            tempU.data(), frame.width / 2,
            tempV.data(), frame.width / 2,
            0, 0, frame.width, frame.height,
            m_nWidth, m_nHeight,
            libyuv::kRotate0, libyuv::FOURCC_ARGB
        );

        if (ret != 0) {
            logMessage("Failed to convert raw frame to I420, ret=" + std::to_string(ret), LogLevel::ERR);
            delete[] frame.buffer;
            continue;
        }

        uint8_t* buffer = getReusableFrameBuffer();
        uint8_t* dstY = buffer;
        uint8_t* dstU = dstY + m_nWidth * m_nHeight;
        uint8_t* dstV = dstU + m_nWidth * m_nHeight / 4;

        memcpy(dstY, tempY.data(), m_nWidth * m_nHeight);
        memcpy(dstU, tempU.data(), m_nWidth * m_nHeight / 4);
        memcpy(dstV, tempV.data(), m_nWidth * m_nHeight / 4);

        m_rawBufferPool.push_back(frame.buffer);

        {
            std::lock_guard<std::mutex> lock(m_pQueueMutex);
            m_pFrameQueue.emplace(buffer, frame.pts);
        }
        m_pQueueCondition.notify_one();
    }
    logMessage("Raw frame processing thread finished", LogLevel::INFO);
}

void VideoWriter::captureAudio() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        logMessage("Failed to initialize COM for audio capture, HRESULT: " + std::to_string(hr), LogLevel::ERR);
        return;
    }

    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDevice* pDevice = nullptr;
    IAudioClient* pAudioClient = nullptr;
    IAudioCaptureClient* pCaptureClient = nullptr;
    WAVEFORMATEX* pwfx = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr)) {
        logMessage("Failed to create device enumerator, HRESULT: " + std::to_string(hr), LogLevel::ERR);
        goto cleanup;
    }

    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr)) {
        logMessage("Failed to get default audio endpoint, HRESULT: " + std::to_string(hr), LogLevel::ERR);
        goto cleanup;
    }

    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pAudioClient);
    if (FAILED(hr)) {
        logMessage("Failed to activate audio client, HRESULT: " + std::to_string(hr), LogLevel::ERR);
        goto cleanup;
    }

    hr = pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) {
        logMessage("Failed to get mix format, HRESULT: " + std::to_string(hr), LogLevel::ERR);
        goto cleanup;
    }

    if (!(pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && pwfx->wBitsPerSample == 32) &&
        !(pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT &&
            pwfx->wBitsPerSample == 32)) {
        logMessage("Unsupported audio format: not IEEE_FLOAT 32-bit", LogLevel::ERR);
        CoTaskMemFree(pwfx);
        goto cleanup;
    }

    if (m_nSampleRate == 0 || m_nChannels == 0) {
        m_nSampleRate = pwfx->nSamplesPerSec;
        m_nChannels = pwfx->nChannels;
    }
    else if (m_nSampleRate != pwfx->nSamplesPerSec || m_nChannels != pwfx->nChannels) {
        logMessage("Audio device format mismatch: expected " + std::to_string(m_nSampleRate) + "Hz, " +
            std::to_string(m_nChannels) + "ch, got " + std::to_string(pwfx->nSamplesPerSec) + "Hz, " +
            std::to_string(pwfx->nChannels) + "ch", LogLevel::ERR);
        CoTaskMemFree(pwfx);
        goto cleanup;
    }

    logMessage("Audio format OK: " +
        std::to_string(pwfx->nSamplesPerSec) + "Hz, " +
        std::to_string(pwfx->nChannels) + "ch, " +
        std::to_string(pwfx->wBitsPerSample) + "bit", LogLevel::INFO);

    REFERENCE_TIME bufferDuration = 10000000;
    hr = pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        bufferDuration,
        0,
        pwfx,
        nullptr
    );

    if (FAILED(hr)) {
        logMessage("AudioClient->Initialize failed, HRESULT: " + std::to_string(hr), LogLevel::ERR);
        CoTaskMemFree(pwfx);
        goto cleanup;
    }

    hr = pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient);
    if (FAILED(hr)) {
        logMessage("Failed to get audio capture client, HRESULT: " + std::to_string(hr), LogLevel::ERR);
        goto cleanup;
    }

    hr = pAudioClient->Start();
    if (FAILED(hr)) {
        logMessage("Failed to start audio client, HRESULT: " + std::to_string(hr), LogLevel::ERR);
        goto cleanup;
    }

    while (!m_bStopAudioEncoding) {
        UINT32 packetLength = 0;
        hr = pCaptureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) {
            logMessage("Failed to get next packet size, HRESULT: " + std::to_string(hr), LogLevel::ERR);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        while (packetLength > 0) {
            BYTE* pData;
            UINT32 numFrames;
            DWORD flags;

            hr = pCaptureClient->GetBuffer(&pData, &numFrames, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                logMessage("Failed to get audio buffer, HRESULT: " + std::to_string(hr), LogLevel::ERR);
                break;
            }

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && pData != nullptr && numFrames > 0) {
                size_t frameSize = numFrames * m_nChannels * sizeof(float);
                uint8_t* pFrameData = new uint8_t[frameSize];
                memcpy(pFrameData, pData, frameSize);

                auto now = std::chrono::steady_clock::now();
                int64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now - m_pStartTime).count();

                {
                    std::lock_guard<std::mutex> lock(m_pAudioQueueMutex);
                    m_pAudioQueue.push({ pFrameData, timestamp, numFrames });
                    logMessage("Captured audio frame: timestamp=" + std::to_string(timestamp) +
                        ", frames=" + std::to_string(numFrames), LogLevel::DEBUG);
                }
                m_pAudioQueueCondition.notify_one();
            }
            else {
                logMessage("Silent or invalid audio buffer skipped, frames=" + std::to_string(numFrames), LogLevel::DEBUG);
            }

            hr = pCaptureClient->ReleaseBuffer(numFrames);
            if (FAILED(hr)) {
                logMessage("Failed to release audio buffer, HRESULT: " + std::to_string(hr), LogLevel::ERR);
                break;
            }

            hr = pCaptureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) {
                logMessage("Failed to get next packet size after release, HRESULT: " + std::to_string(hr), LogLevel::ERR);
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    pAudioClient->Stop();
    logMessage("Audio capture stopped", LogLevel::INFO);

    CoTaskMemFree(pwfx);

cleanup:
    SAFE_RELEASE(pCaptureClient);
    SAFE_RELEASE(pAudioClient);
    SAFE_RELEASE(pDevice);
    SAFE_RELEASE(pEnumerator);
    CoUninitialize();
}

void VideoWriter::encodeAudioFrame() {
    logMessage("Audio encode thread started", LogLevel::INFO);
    int64_t nLastPts = 0;
    std::vector<float> audioBuffer[2];
    int requiredSamples = m_pAudioCodecCtx ? m_pAudioCodecCtx->frame_size : 1024;

    for (int i = 0; i < m_nChannels; ++i) {
        audioBuffer[i].reserve(requiredSamples * 2);
    }

    while (true) {
        std::unique_lock<std::mutex> lock(m_pAudioQueueMutex);
        m_pAudioQueueCondition.wait(lock, [this] {
            return !m_pAudioQueue.empty() || m_bStopAudioEncoding;
            });

        if (m_bStopAudioEncoding && m_pAudioQueue.empty()) {
            if (!audioBuffer[0].empty()) {
                logMessage("Processing remaining " + std::to_string(audioBuffer[0].size()) + " samples", LogLevel::DEBUG);
                if (audioBuffer[0].size() < static_cast<size_t>(requiredSamples)) {
                    for (int ch = 0; ch < m_nChannels; ++ch) {
                        audioBuffer[ch].resize(requiredSamples, 0.0f);
                    }
                }
                sendAudioFrame(audioBuffer, nLastPts, requiredSamples);
                nLastPts += (int64_t)(1000000.0 * requiredSamples / m_nSampleRate);
            }

            logMessage("Flushing audio codec", LogLevel::DEBUG);
            int ret = avcodec_send_frame(m_pAudioCodecCtx, nullptr);
            if (ret < 0 && ret != AVERROR_EOF) {
                char errBuf[128];
                av_strerror(ret, errBuf, sizeof(errBuf));
            }
            break;
        }

        auto [pFrameData, nPts, frameCount] = m_pAudioQueue.front();
        m_pAudioQueue.pop();
        lock.unlock();

        if (frameCount <= 0 || !pFrameData) {
            delete[] pFrameData;
            continue;
        }

        if (nPts <= nLastPts) {
            nPts = nLastPts + (int64_t)(1000000.0 * frameCount / m_nSampleRate);
        }
        logMessage("Audio frame from queue: raw_pts=" + std::to_string(nPts) +
            ", frameCount=" + std::to_string(frameCount), LogLevel::DEBUG);

        int out_samples = av_rescale_rnd(swr_get_delay(m_pSwrCtx, m_nSampleRate) + frameCount,
            m_nSampleRate, m_nSampleRate, AV_ROUND_UP);
        uint8_t* converted_data[2] = { nullptr, nullptr };
        int ret = av_samples_alloc(converted_data, nullptr, m_nChannels, out_samples, AV_SAMPLE_FMT_FLTP, 0);
        if (ret < 0) {
            char errBuf[128];
            av_strerror(ret, errBuf, sizeof(errBuf));
            logMessage("Failed to allocate converted audio buffer: " + std::string(errBuf), LogLevel::ERR);
            delete[] pFrameData;
            continue;
        }

        ret = swr_convert(m_pSwrCtx, converted_data, out_samples,
            (const uint8_t**)&pFrameData, frameCount);
        if (ret < 0) {
            char errBuf[128];
            av_strerror(ret, errBuf, sizeof(errBuf));
            logMessage("swr_convert failed: " + std::string(errBuf), LogLevel::ERR);
            av_freep(&converted_data[0]);
            delete[] pFrameData;
            continue;
        }

        for (int ch = 0; ch < m_nChannels; ++ch) {
            float* data = (float*)converted_data[ch];
            audioBuffer[ch].insert(audioBuffer[ch].end(), data, data + ret);
        }
        av_freep(&converted_data[0]);
        delete[] pFrameData;

        logMessage("Added " + std::to_string(ret) + " samples to buffer, total: " +
            std::to_string(audioBuffer[0].size()), LogLevel::DEBUG);

        while (audioBuffer[0].size() >= static_cast<size_t>(requiredSamples)) {
            sendAudioFrame(audioBuffer, nLastPts, requiredSamples);
            nLastPts += (int64_t)(1000000.0 * requiredSamples / m_nSampleRate);
            logMessage("Updated nLastPts to: " + std::to_string(nLastPts), LogLevel::DEBUG);
        }
    }

    while (true) {
        int ret = avcodec_receive_packet(m_pAudioCodecCtx, m_pAudioPkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            logMessage("Audio packet flush completed", LogLevel::DEBUG);
            break;
        }
        if (ret < 0) {
            char errBuf[128];
            av_strerror(ret, errBuf, sizeof(errBuf));
            logMessage("Failed to receive audio packet during flush: " + std::string(errBuf), LogLevel::ERR);
            break;
        }

        av_packet_make_writable(m_pAudioPkt);

        if (m_pAudioPkt->duration <= 0) {
            m_pAudioPkt->duration = av_rescale_q(m_pAudioFrame->nb_samples,
                { 1, m_nSampleRate }, m_pAudioStream->time_base);
        }

        if (m_pAudioPkt->pts == AV_NOPTS_VALUE || m_pAudioPkt->pts < 0) {
            if (nLastPts <= 0) nLastPts = 0;
            m_pAudioPkt->pts = av_rescale_q(nLastPts, { 1, 1000000 }, m_pAudioStream->time_base);
        }
        m_pAudioPkt->dts = m_pAudioPkt->pts;


        logMessage("Audio packet want to write during flush, pts=" + std::to_string(m_pAudioPkt->pts), LogLevel::INFO);
        m_pAudioPkt->stream_index = m_pAudioStream->index;

        {
            std::lock_guard<std::mutex> lock(g_ffmpegWriteMutex);
            if (!m_pFmtCtx || !m_pFmtCtx->pb) {
                logMessage("Cannot write audio packet during flush: AVIO context is null or closed", LogLevel::ERR);
                av_packet_unref(m_pAudioPkt);
                continue;
            }
            if (ret < 0) {
                char errBuf[128];
                av_strerror(ret, errBuf, sizeof(errBuf));
                logMessage("Failed to write audio packet during flush: " + std::string(errBuf), LogLevel::ERR);
            }
            else {
                logMessage("Audio packet written during flush, pts=" + std::to_string(m_pAudioPkt->pts), LogLevel::INFO);
                nLastPts += (int64_t)(1000000.0 * m_pAudioPkt->duration * m_nSampleRate / m_pAudioStream->time_base.den);
            }
        }
    }

    logMessage("Audio encode thread finished", LogLevel::INFO);
}

void VideoWriter::sendAudioFrame(std::vector<float> audioBuffer[2], int64_t nPts, int requiredSamples) {
    if (!m_pAudioCodecCtx) {
        logMessage("Audio codec context is null, skipping frame", LogLevel::ERR);
        return;
    }

    m_pAudioFrame->nb_samples = requiredSamples;
    m_pAudioFrame->format = AV_SAMPLE_FMT_FLTP;
    m_pAudioFrame->channel_layout = av_get_default_channel_layout(m_nChannels);
    m_pAudioFrame->sample_rate = m_nSampleRate;

    if (nPts < 0 || nPts == AV_NOPTS_VALUE) {
        logMessage("Invalid nPts passed (" + std::to_string(nPts) + "), correcting to 0", LogLevel::ERR);
        nPts = 0;
    }

    m_pAudioFrame->pts = av_rescale_q(nPts, { 1, 1000000 }, m_pAudioCodecCtx->time_base);
    logMessage("Audio Frame Details: nb_samples=" + std::to_string(m_pAudioFrame->nb_samples) +
        ", format=" + std::to_string(m_pAudioFrame->format) +
        ", channel_layout=" + std::to_string(m_pAudioFrame->channel_layout) +
        ", sample_rate=" + std::to_string(m_pAudioFrame->sample_rate) +
        ", pts=" + std::to_string(m_pAudioFrame->pts), LogLevel::DEBUG);

    if (av_frame_get_buffer(m_pAudioFrame, 0) < 0) {
        logMessage("Failed to allocate audio frame buffer", LogLevel::ERR);
        return;
    }

    for (int ch = 0; ch < m_nChannels; ++ch) {
        if (audioBuffer[ch].size() < static_cast<size_t>(requiredSamples)) {
            logMessage("Insufficient audio data for channel " + std::to_string(ch) +
                ": have " + std::to_string(audioBuffer[ch].size()) +
                ", need " + std::to_string(requiredSamples), LogLevel::ERR);
            av_frame_unref(m_pAudioFrame);
            return;
        }
        memcpy(m_pAudioFrame->data[ch], audioBuffer[ch].data(), requiredSamples * sizeof(float));
        audioBuffer[ch].erase(audioBuffer[ch].begin(), audioBuffer[ch].begin() + requiredSamples);
    }

    int ret = avcodec_send_frame(m_pAudioCodecCtx, m_pAudioFrame);
    if (ret < 0) {
        char errBuf[128];
        av_strerror(ret, errBuf, sizeof(errBuf));
        logMessage("Failed to send audio frame: " + std::string(errBuf), LogLevel::ERR);
        av_frame_unref(m_pAudioFrame);
        return;
    }

    while (true) {
        av_packet_unref(m_pAudioPkt);
        ret = avcodec_receive_packet(m_pAudioCodecCtx, m_pAudioPkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            char errBuf[128];
            av_strerror(ret, errBuf, sizeof(errBuf));
            logMessage("Failed to receive audio packet: " + std::string(errBuf), LogLevel::ERR);
            break;
        }

        m_pAudioPkt->stream_index = m_pAudioStream->index;
        if (m_pAudioPkt->pts == AV_NOPTS_VALUE) {
            m_pAudioPkt->pts = av_rescale_q(nPts, { 1, 1000000 }, m_pAudioStream->time_base);
            logMessage("Corrected invalid audio packet PTS to: " + std::to_string(m_pAudioPkt->pts), LogLevel::DEBUG);
        }
        m_pAudioPkt->dts = m_pAudioPkt->pts;

        {
            std::lock_guard<std::mutex> lock(g_ffmpegWriteMutex);
            if (!m_pFmtCtx || !m_pFmtCtx->pb) {
                logMessage("Cannot write audio packet: AVIO context is null or closed", LogLevel::ERR);
                av_packet_unref(m_pAudioPkt);
                break;
            }

            ret = av_interleaved_write_frame(m_pFmtCtx, m_pAudioPkt);
            if (ret < 0) {
                char errBuf[128];
                av_strerror(ret, errBuf, sizeof(errBuf));
                logMessage("Failed to write audio packet: " + std::string(errBuf), LogLevel::ERR);
            }
            else {
                logMessage("Audio packet written, pts=" + std::to_string(m_pAudioPkt->pts), LogLevel::INFO);
            }
        }
    }

    av_frame_unref(m_pAudioFrame);
}


void VideoWriter::encodeFrame() {
    logMessage("EncodeFrame thread started", LogLevel::INFO);
    static int64_t frameCounter = 0;
    while (true) {
        std::unique_lock<std::mutex> lock(m_pQueueMutex);
        m_pQueueCondition.wait(lock, [this] { return !m_pFrameQueue.empty() || m_bStopEncoding; });
        if (m_bStopEncoding && m_pFrameQueue.empty()) {
            logMessage("Received stop signal, exiting encode thread", LogLevel::INFO);
            break;
        }

        auto [pFrameDataRaw, nPts] = m_pFrameQueue.front();
        std::unique_ptr<uint8_t[]> pFrameData(pFrameDataRaw);
        m_pFrameQueue.pop();
        logMessage("Frame queue size after pop: " + std::to_string(m_pFrameQueue.size()), LogLevel::DEBUG);
        lock.unlock();

        if (m_pFrameQueue.size() > 500) {
            logMessage("Frame queue too large, dropping frame", LogLevel::ERR);
            continue;
        }

        logMessage("Processing video frame with raw pts=" + std::to_string(nPts), LogLevel::DEBUG);

        m_pFrame->pts = av_rescale_q(nPts, { 1, 1000000 }, m_pCodecCtx->time_base);

        m_pFrame->data[0] = pFrameData.get();
        m_pFrame->data[1] = pFrameData.get() + m_nWidth * m_nHeight;
        m_pFrame->data[2] = pFrameData.get() + m_nWidth * m_nHeight * 5 / 4;
        m_pFrame->linesize[0] = m_nWidth;
        m_pFrame->linesize[1] = m_nWidth / 2;
        m_pFrame->linesize[2] = m_nWidth / 2;

        logMessage("Frame PTS before encoding: " + std::to_string(m_pFrame->pts) +
            " (codec time_base=" + std::to_string(m_pCodecCtx->time_base.num) + "/" +
            std::to_string(m_pCodecCtx->time_base.den) + ")", LogLevel::DEBUG);

        int ret = avcodec_send_frame(m_pCodecCtx, m_pFrame);
        if (ret < 0) {
            char errBuf[128];
            av_strerror(ret, errBuf, sizeof(errBuf));
            logMessage("Failed to send frame: " + std::string(errBuf), LogLevel::ERR);
            continue;
        }

        bool packetWritten = false;
        while (true) {
            av_packet_unref(m_pPkt);
            ret = avcodec_receive_packet(m_pCodecCtx, m_pPkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                char errBuf[128];
                av_strerror(ret, errBuf, sizeof(errBuf));
                logMessage("Failed to receive packet: " + std::string(errBuf), LogLevel::ERR);
                break;
            }

            m_pPkt->stream_index = m_pVideoStream->index;

            m_pPkt->pts = av_rescale_q_rnd(nPts, { 1, 1000000 }, m_pVideoStream->time_base, AV_ROUND_NEAR_INF);
            m_pPkt->dts = m_pPkt->pts;

            logMessage("Video packet prepared: pts=" + std::to_string(m_pPkt->pts) +
                ", dts=" + std::to_string(m_pPkt->dts) +
                " (stream time_base=" + std::to_string(m_pVideoStream->time_base.num) + "/" +
                std::to_string(m_pVideoStream->time_base.den) + ")", LogLevel::DEBUG);

            {
                std::lock_guard<std::mutex> lock(g_ffmpegWriteMutex);
                if (!m_pFmtCtx || !m_pFmtCtx->pb) {
                    logMessage("Cannot write video packet: AVIO context is null or closed", LogLevel::ERR);
                    av_packet_unref(m_pPkt);
                    continue;
                }
                ret = av_interleaved_write_frame(m_pFmtCtx, m_pPkt);
                if (ret < 0) {
                    char errBuf[128];
                    av_strerror(ret, errBuf, sizeof(errBuf));
                    logMessage("Failed to write video packet: " + std::string(errBuf), LogLevel::ERR);
                }
                else {
                    logMessage("Video packet written successfully", LogLevel::INFO);
                    packetWritten = true;
                }
            }
        }

        if (packetWritten) {
            std::lock_guard<std::mutex> lock(m_pQueueMutex);
            m_frameBufferPool.push_back(pFrameData.release());
            logMessage("Returned buffer to frameBufferPool, pool size: " + std::to_string(m_frameBufferPool.size()), LogLevel::DEBUG);
        }
    }

    if (m_pCodecCtx) {
        logMessage("Flushing video codec", LogLevel::INFO);
        avcodec_send_frame(m_pCodecCtx, nullptr);
        while (true) {
            av_packet_unref(m_pPkt);
            int ret = avcodec_receive_packet(m_pCodecCtx, m_pPkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                logMessage("Video codec flush completed", LogLevel::INFO);
                break;
            }
            if (ret < 0) {
                char errBuf[128];
                av_strerror(ret, errBuf, sizeof(errBuf));
                logMessage("Failed to receive packet during flush: " + std::string(errBuf), LogLevel::ERR);
                break;
            }

            m_pPkt->stream_index = m_pVideoStream->index;
            m_pPkt->pts = av_rescale_q_rnd(frameCounter, m_pCodecCtx->time_base,
                m_pVideoStream->time_base, AV_ROUND_NEAR_INF);
            m_pPkt->dts = m_pPkt->pts;

            logMessage("Flushed packet prepared: pts=" + std::to_string(m_pPkt->pts) +
                ", dts=" + std::to_string(m_pPkt->dts), LogLevel::DEBUG);

            {
                std::lock_guard<std::mutex> lock(g_ffmpegWriteMutex);
                if (!m_pFmtCtx || !m_pFmtCtx->pb) {
                    logMessage("Cannot write video packet during flush: AVIO context is null or closed", LogLevel::ERR);
                    av_packet_unref(m_pPkt);
                    continue;
                }
                ret = av_interleaved_write_frame(m_pFmtCtx, m_pPkt);
                if (ret < 0) {
                    char errBuf[128];
                    av_strerror(ret, errBuf, sizeof(errBuf));
                    logMessage("Failed to write flushed video packet: " + std::string(errBuf), LogLevel::ERR);
                }
                else {
                    logMessage("Flushed video packet written successfully", LogLevel::INFO);
                }
            }
            frameCounter++;
        }
    }

    logMessage("Video encode thread finished", LogLevel::INFO);
}
void VideoWriter::freeResources() {
    logMessage("Starting resource cleanup", LogLevel::INFO);

    if (m_pFrame) {
        logMessage("Freeing AVFrame", LogLevel::INFO);
        av_frame_free(&m_pFrame);
        m_pFrame = nullptr;
    }

    if (m_pAudioFrame) {
        logMessage("Freeing audio AVFrame", LogLevel::INFO);
        av_frame_free(&m_pAudioFrame);
        m_pAudioFrame = nullptr;
    }

    if (m_pPkt) {
        logMessage("Freeing AVPacket", LogLevel::INFO);
        av_packet_free(&m_pPkt);
        m_pPkt = nullptr;
    }

    if (m_pAudioPkt) {
        logMessage("Freeing audio AVPacket", LogLevel::INFO);
        av_packet_free(&m_pAudioPkt);
        m_pAudioPkt = nullptr;
    }

    if (m_pAudioCodecCtx) {
        logMessage("Freeing audio codec context", LogLevel::INFO);
        avcodec_free_context(&m_pAudioCodecCtx);
        m_pAudioCodecCtx = nullptr;
    }

    for (auto* ptr : m_frameBufferPool) delete[] ptr;
    m_frameBufferPool.clear();

    if (m_pFmtCtx) {
        if (m_pFmtCtx->pb) {
            logMessage("Closing AVIO context", LogLevel::INFO);
            int nRet = avio_close(m_pFmtCtx->pb);
            if (nRet < 0) {
                char errBuf[128];
                av_strerror(nRet, errBuf, sizeof(errBuf));
                logMessage("Failed to close AVIO context, ret=" + std::to_string(nRet) + ", error: " + errBuf, LogLevel::ERR);
            }
            m_pFmtCtx->pb = nullptr;
        }
        logMessage("Freeing AVFormatContext", LogLevel::INFO);
        avformat_free_context(m_pFmtCtx);
        m_pFmtCtx = nullptr;
    }
    m_frameBufferPool.clear();
    if (m_pFrameBuffer) {
        logMessage("Freeing frameBuffer", LogLevel::INFO);
        _aligned_free(m_pFrameBuffer);
        m_pFrameBuffer = nullptr;
    }

    if (m_pPicIn) {
        logMessage("Cleaning pic_in", LogLevel::INFO);
        if (m_pPicIn->img.plane) {
            for (int i = 0; i < 4; i++) {
                m_pPicIn->img.plane[i] = nullptr;
            }
            m_pPicIn->img.i_plane = 0;
        }
        x264_picture_clean(m_pPicIn);
        delete m_pPicIn;
        m_pPicIn = nullptr;
    }

    if (m_pCodecCtx) {
        logMessage("Freeing codec context", LogLevel::INFO);
        avcodec_free_context(&m_pCodecCtx);
        m_pCodecCtx = nullptr;
    }

    if (m_pEncoder) {
        logMessage("Closing x264 encoder", LogLevel::INFO);
        x264_encoder_close(m_pEncoder);
        m_pEncoder = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(m_pQueueMutex);
        logMessage("Clearing video frame queue, initial size=" + std::to_string(m_pFrameQueue.size()), LogLevel::INFO);
        while (!m_pFrameQueue.empty()) {
            delete[] m_pFrameQueue.front().buffer;
            m_pFrameQueue.pop();
        }
        logMessage("Video frame queue cleared", LogLevel::INFO);
    }

    {
        std::lock_guard<std::mutex> lock(m_pAudioQueueMutex);
        logMessage("Clearing audio frame queue, initial size=" + std::to_string(m_pAudioQueue.size()), LogLevel::INFO);
        while (!m_pAudioQueue.empty()) {
            delete[] m_pAudioQueue.front().data;
            m_pAudioQueue.pop();
        }
        logMessage("Audio frame queue cleared", LogLevel::INFO);
    }

    if (m_pCachedTempSurface) {
        m_pCachedTempSurface->Release();
        m_pCachedTempSurface = nullptr;
    }

    if (m_pSwrCtx) {
        swr_free(&m_pSwrCtx);
        m_pSwrCtx = nullptr;
    }
    for (auto* ptr : m_rawBufferPool) delete[] ptr;
    m_rawBufferPool.clear();

    {
        std::lock_guard<std::mutex> lock(m_pRawQueueMutex);
        logMessage("Clearing raw frame queue, initial size=" + std::to_string(m_pRawFrameQueue.size()), LogLevel::INFO);
        while (!m_pRawFrameQueue.empty()) {
            delete[] m_pRawFrameQueue.front().buffer;
            m_pRawFrameQueue.pop();
        }
        logMessage("Raw frame queue cleared", LogLevel::INFO);
    }
    logMessage("Resources freed successfully", LogLevel::INFO);
}
void VideoWriter::stopRecording() {
    if (!m_bInitialized) {
        logMessage("StopRecording failed: Not initialized", LogLevel::ERR);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_pQueueMutex);
        m_bStopEncoding = true;
    }
    {
        std::lock_guard<std::mutex> lock(m_pAudioQueueMutex);
        m_bStopAudioEncoding = true;
    }
    {
        std::lock_guard<std::mutex> lock(m_pRawQueueMutex);
        m_bStopRawProcessing = true;
    }
    m_pQueueCondition.notify_all();
    m_pAudioQueueCondition.notify_all();
    m_pRawQueueCondition.notify_all();
    if (m_pRawProcessThread.joinable()) {
        logMessage("Joining raw frame processing thread", LogLevel::INFO);
        m_pRawProcessThread.join();
    }
    if (m_pEncodeThread.joinable()) {
        logMessage("Joining video encoding thread", LogLevel::INFO);
        m_pEncodeThread.join();
    }
    if (m_pAudioEncodeThread.joinable()) {
        logMessage("Joining audio encoding thread", LogLevel::INFO);
        m_pAudioEncodeThread.join();
    }
    if (m_pAudioCaptureThread.joinable()) {
        logMessage("Joining audio capture thread", LogLevel::INFO);
        m_pAudioCaptureThread.join();
    }

    if (m_pFmtCtx) {
        int nRet = av_write_trailer(m_pFmtCtx);
        if (nRet < 0) {
            char errBuf[128];
            av_strerror(nRet, errBuf, sizeof(errBuf));
            logMessage("Failed to write trailer, ret=" + std::to_string(nRet) + ", error: " + errBuf, LogLevel::ERR);
        }
        else {
            logMessage("Trailer written successfully", LogLevel::INFO);
        }
    }


    logMessage("Starting queue cleanup", LogLevel::INFO);
    auto start = std::chrono::steady_clock::now();
    while (true) {
        {
            std::lock_guard<std::mutex> lock(m_pRawQueueMutex);
            if (m_pRawFrameQueue.empty()) break;
            logMessage("Clearing raw frame queue, size=" + std::to_string(m_pRawFrameQueue.size()), LogLevel::INFO);
            while (!m_pRawFrameQueue.empty()) {
                delete[] m_pRawFrameQueue.front().buffer;
                m_pRawFrameQueue.pop();
            }
        }
        {
            std::lock_guard<std::mutex> lock(m_pQueueMutex);
            if (m_pFrameQueue.empty()) break;
            logMessage("Clearing video frame queue, size=" + std::to_string(m_pFrameQueue.size()), LogLevel::INFO);
            while (!m_pFrameQueue.empty()) {
                auto [pFrameData, nPts] = m_pFrameQueue.front();
                m_pFrameQueue.pop();
                delete[] pFrameData;
            }
        }
        {
            std::lock_guard<std::mutex> lock(m_pAudioQueueMutex);
            if (m_pAudioQueue.empty()) break;
            logMessage("Clearing audio frame queue, size=" + std::to_string(m_pAudioQueue.size()), LogLevel::INFO);
            while (!m_pAudioQueue.empty()) {
                auto [pFrameData, nPts, frameCount] = m_pAudioQueue.front();
                m_pAudioQueue.pop();
                delete[] pFrameData;
            }
        }
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() > 5) {
            logMessage("Timeout waiting for queues to clear, proceeding with cleanup", LogLevel::INFO);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!m_pEncodeThread.joinable() && !m_pAudioEncodeThread.joinable() && !m_pAudioCaptureThread.joinable()) {
        logMessage("Starting full cleanup", LogLevel::INFO);

        if (m_pFmtCtx && m_pFmtCtx->pb) {
            int nRet = avio_close(m_pFmtCtx->pb);
            if (nRet < 0) {
                char errBuf[128];
                av_strerror(nRet, errBuf, sizeof(errBuf));
                logMessage("Failed to close AVIO context, ret=" + std::to_string(nRet) + ", error: " + errBuf, LogLevel::ERR);
            }
            else {
                logMessage("AVIO context closed successfully", LogLevel::INFO);
            }
            m_pFmtCtx->pb = nullptr;
        }

        freeResources();
    }
    else {
        logMessage("Threads still active, skipping full cleanup to avoid race conditions", LogLevel::INFO);
    }

    m_bInitialized = false;
    logMessage("Recording stopped", LogLevel::INFO);
}
