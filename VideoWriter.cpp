#include "VideoWriter.h"
#include "MemUtils.h"
#include <algorithm>
#include <array>

#define SAFE_RELEASE(p) if ((p)) { (p)->Release(); (p) = nullptr; }

VideoWriter::VideoWriter()
    : m_startTime(std::chrono::steady_clock::now())
    , m_copyThreadPool(std::max(1u, std::thread::hardware_concurrency()))
{}

VideoWriter::~VideoWriter() {
    stopRecording();
}

HRESULT VideoWriter::init(IDirect3DDevice9* device, const Config& config) {
    if (m_initialized) {
        m_logger.log("init: already initialized", LogLevel::ERR);
        return E_FAIL;
    }

    m_config = config;
    m_logger.setEnabled(config.enableLog);
    m_logger.setLevel(config.logLevel);

    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) {
        m_logger.log("init: CoInitialize failed", LogLevel::ERR);
        return hr;
    }

    if (m_config.width == 0 || m_config.height == 0) {
        ComPtr<IDirect3DSurface9> backBuffer;
        if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))) {
            m_logger.log("init: GetBackBuffer failed", LogLevel::ERR);
            CoUninitialize();
            return E_BACKBUFFER_FAILED;
        }
        D3DSURFACE_DESC desc;
        backBuffer->GetDesc(&desc);
        m_config.width  = static_cast<int>(desc.Width);
        m_config.height = static_cast<int>(desc.Height);
        m_logger.log("init: auto resolution " +
                     std::to_string(m_config.width) + "x" + std::to_string(m_config.height),
                     LogLevel::INFO);
    }

    CoUninitialize();

    m_limiter   = FPSLimiter(static_cast<float>(m_config.fps));
    m_startTime = std::chrono::steady_clock::now();

    hr = initFFmpeg();
    if (FAILED(hr)) {
        m_logger.log("init: initFFmpeg failed", LogLevel::ERR);
        return hr;
    }

    m_initialized = true;
    m_logger.log("init: success " + std::to_string(m_config.width) + "x" +
                 std::to_string(m_config.height) + " @ " + std::to_string(m_config.fps) + "fps",
                 LogLevel::INFO);
    return S_OK;
}

HRESULT VideoWriter::initFFmpeg() {
    avformat_network_init();

    if (avformat_alloc_output_context2(&m_fmtCtx, nullptr, nullptr,
                                        m_config.outputPath.c_str()) < 0) {
        m_logger.log("initFFmpeg: alloc output context failed", LogLevel::ERR);
        return E_FFMPEG_CONTEXT_FAILED;
    }

    m_videoStream = avformat_new_stream(m_fmtCtx, nullptr);
    if (!m_videoStream) {
        m_logger.log("initFFmpeg: new video stream failed", LogLevel::ERR);
        return E_FFMPEG_STREAM_FAILED;
    }

    struct CodecOption { const char* name; };
    constexpr std::array<CodecOption, 3> codecs{{
        {"h264_amf"}, {"h264_nvenc"}, {"libx264"}
    }};

    const AVCodec*  selectedCodec    = nullptr;
    AVCodecContext* selectedCodecCtx = nullptr;

    for (const auto& opt : codecs) {
        const AVCodec* codec = avcodec_find_encoder_by_name(opt.name);
        if (!codec) {
            m_logger.log(std::string(opt.name) + ": not found", LogLevel::DEBUG);
            continue;
        }

        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        if (!ctx) continue;

        ctx->width        = m_config.width;
        ctx->height       = m_config.height;
        ctx->time_base    = {1, m_config.fps};
        ctx->framerate    = {m_config.fps, 1};
        ctx->pix_fmt      = AV_PIX_FMT_YUV420P;
        ctx->gop_size     = m_config.fps;
        ctx->max_b_frames = 0;
        ctx->bit_rate     = 0;

        std::string  preset = m_config.preset;
        AVDictionary* opts  = nullptr;

        if (std::string(opt.name) == "h264_amf") {
            static const std::string amfPresets[] = {
                "transcoding", "lowlatency", "ultralowlatency", "webcam"};
            if (std::find(std::begin(amfPresets), std::end(amfPresets), preset) == std::end(amfPresets))
                preset = "ultralowlatency";
            av_dict_set    (&opts, "usage",       preset.c_str(), 0);
            av_dict_set    (&opts, "profile",     "main",         0);
            av_dict_set    (&opts, "quality",     "speed",        0);
            av_dict_set    (&opts, "preanalysis", "false",        0);
            av_dict_set    (&opts, "vbaq",        "false",        0);
            av_dict_set_int(&opts, "qp_i",        m_config.crf,   0);
            av_dict_set_int(&opts, "qp_p",        m_config.crf,   0);

        } else if (std::string(opt.name) == "h264_nvenc") {
            static const std::string nvencPresets[] = {"p1","p2","p3","p4","p5","p6","p7"};
            if (std::find(std::begin(nvencPresets), std::end(nvencPresets), preset) == std::end(nvencPresets))
                preset = "p1";
            av_dict_set    (&opts, "preset",      preset.c_str(), 0);
            av_dict_set    (&opts, "rc",          "constqp",      0);
            av_dict_set_int(&opts, "qp",          m_config.crf,   0);
            av_dict_set    (&opts, "delay",       "0",            0);
            av_dict_set    (&opts, "zerolatency", "1",            0);
            av_dict_set_int(&opts, "async_depth", 4,              0);
            av_dict_set_int(&opts, "surfaces",    8,              0);

        } else {
            static const std::string x264Presets[] = {
                "ultrafast","superfast","veryfast","faster","fast",
                "medium","slow","slower","veryslow"};
            if (std::find(std::begin(x264Presets), std::end(x264Presets), preset) == std::end(x264Presets))
                preset = "ultrafast";
            av_dict_set    (&opts, "preset", preset.c_str(), 0);
            av_dict_set    (&opts, "tune",   "zerolatency",  0);
            av_dict_set_int(&opts, "crf",    m_config.crf,   0);
        }

        bool opened = (avcodec_open2(ctx, codec, &opts) == 0);
        av_dict_free(&opts);

        if (!opened) {
            m_logger.log(std::string(opt.name) + ": open failed", LogLevel::DEBUG);
            avcodec_free_context(&ctx);
            continue;
        }

        selectedCodec    = codec;
        selectedCodecCtx = ctx;
        m_logger.log("initFFmpeg: using " + std::string(opt.name), LogLevel::INFO);
        break;
    }

    if (!selectedCodec || !selectedCodecCtx) {
        m_logger.log("initFFmpeg: no usable codec found", LogLevel::ERR);
        return E_FFMPEG_CODEC_NOT_FOUND;
    }

    m_videoCodecCtx = selectedCodecCtx;
    avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCodecCtx);
    m_videoStream->time_base = m_videoCodecCtx->time_base;

    if (m_config.enableAudio) {
        m_audioStream = avformat_new_stream(m_fmtCtx, nullptr);
        if (!m_audioStream) {
            m_logger.log("initFFmpeg: new audio stream failed", LogLevel::ERR);
            return E_FFMPEG_STREAM_FAILED;
        }

        const AVCodec* audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!audioCodec) {
            m_logger.log("initFFmpeg: AAC codec not found", LogLevel::ERR);
            return E_FFMPEG_CODEC_NOT_FOUND;
        }

        m_audioCodecCtx = avcodec_alloc_context3(audioCodec);
        if (!m_audioCodecCtx) return E_FFMPEG_CODEC_ALLOC_FAILED;

        m_audioCodecCtx->sample_rate    = m_config.sampleRate;
        m_audioCodecCtx->channels       = m_config.channels;
        m_audioCodecCtx->channel_layout = av_get_default_channel_layout(m_config.channels);
        m_audioCodecCtx->sample_fmt     = AV_SAMPLE_FMT_FLTP;
        m_audioCodecCtx->bit_rate       = 128000;
        m_audioCodecCtx->time_base      = {1, m_config.sampleRate};

        if (avcodec_open2(m_audioCodecCtx, audioCodec, nullptr) < 0) {
            m_logger.log("initFFmpeg: audio codec open failed", LogLevel::ERR);
            return E_FFMPEG_CODEC_OPEN_FAILED;
        }

        avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCodecCtx);
        m_audioStream->time_base = {1, m_config.sampleRate};

        m_swrCtx = swr_alloc();
        av_opt_set_int       (m_swrCtx, "in_channel_layout",  av_get_default_channel_layout(m_config.channels), 0);
        av_opt_set_int       (m_swrCtx, "out_channel_layout", av_get_default_channel_layout(m_config.channels), 0);
        av_opt_set_int       (m_swrCtx, "in_sample_rate",     m_config.sampleRate, 0);
        av_opt_set_int       (m_swrCtx, "out_sample_rate",    m_config.sampleRate, 0);
        av_opt_set_sample_fmt(m_swrCtx, "in_sample_fmt",      AV_SAMPLE_FMT_FLT,  0);
        av_opt_set_sample_fmt(m_swrCtx, "out_sample_fmt",     AV_SAMPLE_FMT_FLTP, 0);

        if (swr_init(m_swrCtx) < 0) {
            m_logger.log("initFFmpeg: swr_init failed", LogLevel::ERR);
            return E_FFMPEG_INIT_FAILED;
        }
    }

    if (avio_open(&m_fmtCtx->pb, m_config.outputPath.c_str(), AVIO_FLAG_WRITE) < 0) {
        m_logger.log("initFFmpeg: avio_open failed: " + m_config.outputPath, LogLevel::ERR);
        return E_FFMPEG_FILE_OPEN_FAILED;
    }

    AVDictionary* muxOpts = nullptr;
    av_dict_set(&muxOpts, "movflags", "faststart", 0);
    int ret = avformat_write_header(m_fmtCtx, &muxOpts);
    av_dict_free(&muxOpts);

    if (ret < 0) {
        m_logger.log("initFFmpeg: write_header failed", LogLevel::ERR);
        return E_FFMPEG_HEADER_FAILED;
    }

    m_logger.log("initFFmpeg: success", LogLevel::INFO);
    return S_OK;
}

void VideoWriter::freeFFmpeg() {
    if (m_fmtCtx && m_fmtCtx->pb) {
        int ret = av_write_trailer(m_fmtCtx);
        if (ret < 0) {
            char err[128]; av_strerror(ret, err, sizeof(err));
            m_logger.log("freeFFmpeg: write_trailer failed: " + std::string(err), LogLevel::ERR);
        }
        avio_close(m_fmtCtx->pb);
        m_fmtCtx->pb = nullptr;
    }
    if (m_swrCtx)        { swr_free(&m_swrCtx); }
    if (m_audioCodecCtx) { avcodec_free_context(&m_audioCodecCtx); }
    if (m_videoCodecCtx) { avcodec_free_context(&m_videoCodecCtx); }
    if (m_fmtCtx)        { avformat_free_context(m_fmtCtx); m_fmtCtx = nullptr; }
}

void VideoWriter::setThreadPriorities() {
    if (m_videoEncoder) {
        SetThreadPriority(m_videoEncoder->rawThreadHandle(),    THREAD_PRIORITY_ABOVE_NORMAL);
        SetThreadPriority(m_videoEncoder->encodeThreadHandle(), THREAD_PRIORITY_ABOVE_NORMAL);

        DWORD_PTR numCores = std::thread::hardware_concurrency();
        if (numCores >= 4) {
            SetThreadAffinityMask(m_videoEncoder->rawThreadHandle(),    DWORD_PTR(1) << 1);
            SetThreadAffinityMask(m_videoEncoder->encodeThreadHandle(), DWORD_PTR(1) << 2);
        }
    }

    if (m_audioCapture) {
        SetThreadPriority(m_audioCapture->nativeHandle(), THREAD_PRIORITY_NORMAL);
    }
    if (m_audioEncoder) {
        SetThreadPriority(m_audioEncoder->nativeHandle(), THREAD_PRIORITY_NORMAL);
    }
}

void VideoWriter::startRecording() {
    if (!m_initialized) {
        m_logger.log("startRecording: not initialized", LogLevel::ERR);
        return;
    }

    m_videoEncoder = std::make_unique<VideoEncoder>(
        m_logger,
        m_rawQueue, m_rawMutex, m_rawCV,
        m_fmtCtx, m_videoCodecCtx, m_videoStream, m_writeMutex,
        static_cast<uint32_t>(m_config.width),
        static_cast<uint32_t>(m_config.height));

    m_videoEncoder->start();

    if (m_config.enableAudio) {
        m_audioCapture = std::make_unique<AudioCapture>(
            m_logger,
            m_audioQueue, m_audioMutex, m_audioCV,
            m_startTime,
            m_config.sampleRate,
            m_config.channels);

        m_audioEncoder = std::make_unique<AudioEncoder>(
            m_logger,
            m_audioQueue, m_audioMutex, m_audioCV,
            m_fmtCtx, m_audioCodecCtx, m_audioStream, m_swrCtx, m_writeMutex,
            m_config.sampleRate,
            m_config.channels);

        m_audioCapture->start();
        m_audioEncoder->start();
    }

    setThreadPriorities();

    m_logger.log("startRecording: started", LogLevel::INFO);
}

void VideoWriter::stopRecording() {
    if (!m_initialized) return;

    m_logger.log("stopRecording: stopping", LogLevel::INFO);

    m_pendingFrames.clear();

    if (m_audioCapture) { m_audioCapture->stop(); m_audioCapture.reset(); }
    if (m_audioEncoder) { m_audioEncoder->stop(); m_audioEncoder.reset(); }
    if (m_videoEncoder) { m_videoEncoder->stop(); m_videoEncoder.reset(); }

    freeFFmpeg();

    m_surfacePool.reset();
    m_intermediateSurface.Reset();

    {
        std::lock_guard<std::mutex> lock(m_rawMutex);
        while (!m_rawQueue.empty()) {
            delete[] m_rawQueue.front().buffer;
            m_rawQueue.pop();
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_audioMutex);
        while (!m_audioQueue.empty()) {
            delete[] m_audioQueue.front().data;
            m_audioQueue.pop();
        }
    }

    m_initialized = false;
    m_logger.log("stopRecording: done", LogLevel::INFO);
}

bool VideoWriter::reinitSurfaces(IDirect3DDevice9* device, const D3DSURFACE_DESC& desc) {
    m_intermediateSurface.Reset();
    m_surfacePool.reset();

    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        if (FAILED(device->CreateRenderTarget(
                desc.Width, desc.Height, desc.Format,
                D3DMULTISAMPLE_NONE, 0, FALSE,
                &m_intermediateSurface, nullptr))) {
            m_logger.log("captureFrame: CreateRenderTarget failed", LogLevel::ERR);
            return false;
        }
    }

    if (!m_surfacePool.initialize(device, desc.Width, desc.Height, desc.Format,
                                   m_config.surfaceCount)) {
        m_logger.log("captureFrame: SurfacePool init failed", LogLevel::ERR);
        return false;
    }

    m_logger.log("captureFrame: surfaces reinitialized " +
                 std::to_string(desc.Width) + "x" + std::to_string(desc.Height),
                 LogLevel::INFO);
    return true;
}

void VideoWriter::processSurface(IDirect3DDevice9* device, PendingFrame& pending) {
    D3DLOCKED_RECT locked;
    if (FAILED(pending.surface->LockRect(&locked, nullptr, D3DLOCK_READONLY | D3DLOCK_DONOTWAIT))) {
        m_logger.log("captureFrame: LockRect failed", LogLevel::ERR);
        m_surfacePool.release(pending.surface);
        return;
    }

    uint8_t* src      = static_cast<uint8_t*>(locked.pBits);
    int      srcStride = locked.Pitch;

    if (!src || srcStride <= 0) {
        pending.surface->UnlockRect();
        m_surfacePool.release(pending.surface);
        return;
    }

    size_t   bufSize = static_cast<size_t>(srcStride) * m_surfacePool.cachedHeight();
    uint8_t* raw     = m_captureBufferPool.acquireRaw(bufSize);

    parallelNtCopy(raw, src, bufSize, m_copyThreadPool);

    pending.surface->UnlockRect();
    m_surfacePool.release(pending.surface);

    {
        std::lock_guard<std::mutex> lock(m_rawMutex);
        if (m_rawQueue.size() >= MAX_QUEUE_SIZE) {
            m_logger.log("captureFrame: raw queue full, dropping oldest", LogLevel::ERR);
            m_captureBufferPool.releaseRaw(m_rawQueue.front().buffer);
            m_rawQueue.pop();
        }
        m_rawQueue.push({
            raw,
            bufSize,
            m_surfacePool.cachedWidth(),
            m_surfacePool.cachedHeight(),
            srcStride,
            pending.pts
        });
    }
    m_rawCV.notify_one();
}

void VideoWriter::pollPendingFrames(IDirect3DDevice9* device) {
    auto it = m_pendingFrames.begin();
    while (it != m_pendingFrames.end()) {
        HRESULT hr = it->query->GetData(nullptr, 0, D3DGETDATA_FLUSH);

        if (hr == S_OK) {
            processSurface(device, *it);
            it = m_pendingFrames.erase(it);
        } else if (hr == S_FALSE) {
            ++it;
        } else {
            m_logger.log("pollPendingFrames: query GetData failed", LogLevel::ERR);
            m_surfacePool.release(it->surface);
            it = m_pendingFrames.erase(it);
        }
    }
}

void VideoWriter::captureFrame(IDirect3DDevice9* device) {
    if (!m_initialized || !device) return;

    pollPendingFrames(device);

    if (!m_limiter.tick()) return;

    ComPtr<IDirect3DSurface9> backBuffer;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))) {
        m_logger.log("captureFrame: GetBackBuffer failed", LogLevel::ERR);
        return;
    }

    D3DSURFACE_DESC desc;
    if (FAILED(backBuffer->GetDesc(&desc))) {
        m_logger.log("captureFrame: GetDesc failed", LogLevel::ERR);
        return;
    }

    if (m_surfacePool.needsReinit(desc.Width, desc.Height, desc.Format)) {
        m_pendingFrames.clear();
        if (!reinitSurfaces(device, desc)) return;
    }

    IDirect3DSurface9* source = backBuffer.Get();
    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        if (!m_intermediateSurface) return;
        if (FAILED(device->StretchRect(source, nullptr,
                                        m_intermediateSurface.Get(), nullptr, D3DTEXF_POINT))) {
            m_logger.log("captureFrame: StretchRect failed", LogLevel::ERR);
            return;
        }
        source = m_intermediateSurface.Get();
    }

    if (m_pendingFrames.size() >= MAX_PENDING_FRAMES) {
        m_logger.log("captureFrame: pending queue full, forcing oldest", LogLevel::DEBUG);
        processSurface(device, m_pendingFrames.front());
        m_pendingFrames.erase(m_pendingFrames.begin());
    }

    auto tempSurface = m_surfacePool.acquire();
    if (!tempSurface) {
        m_logger.log("captureFrame: no free surfaces", LogLevel::ERR);
        return;
    }

    if (FAILED(device->GetRenderTargetData(source, tempSurface.Get()))) {
        m_logger.log("captureFrame: GetRenderTargetData failed", LogLevel::ERR);
        m_surfacePool.release(tempSurface);
        return;
    }

    ComPtr<IDirect3DQuery9> query;
    if (FAILED(device->CreateQuery(D3DQUERYTYPE_EVENT, &query))) {
        m_logger.log("captureFrame: CreateQuery failed, falling back to sync", LogLevel::DEBUG);
        int64_t pts = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - m_startTime).count();
        PendingFrame pf;
        pf.surface = tempSurface;
        pf.pts     = pts;
        processSurface(device, pf);
        return;
    }

    query->Issue(D3DISSUE_END);

    int64_t pts = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - m_startTime).count();

    m_pendingFrames.push_back({tempSurface, query, pts});
}
