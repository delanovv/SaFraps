#include "VideoEncoder.h"
#include <libyuv.h>
#include <future>
#include <algorithm>
#include <thread>

VideoEncoder::VideoEncoder(Logger&                  logger,
                            std::queue<RawFrame>&    rawInQueue,
                            std::mutex&              rawInMutex,
                            std::condition_variable& rawInCV,
                            AVFormatContext*         fmtCtx,
                            AVCodecContext*          codecCtx,
                            AVStream*                stream,
                            std::mutex&              writeMutex,
                            uint32_t                 width,
                            uint32_t                 height)
    : m_logger(logger)
    , m_rawInQueue(rawInQueue)
    , m_rawInMutex(rawInMutex)
    , m_rawInCV(rawInCV)
    , m_fmtCtx(fmtCtx)
    , m_codecCtx(codecCtx)
    , m_stream(stream)
    , m_writeMutex(writeMutex)
    , m_width(width)
    , m_height(height)
{
    m_frame = av_frame_alloc();
    if (m_frame) {
        m_frame->format = AV_PIX_FMT_YUV420P;
        m_frame->width  = static_cast<int>(width);
        m_frame->height = static_cast<int>(height);
        av_frame_get_buffer(m_frame, 0);
    }
    m_pkt = av_packet_alloc();
}

VideoEncoder::~VideoEncoder() {
    stop();
    av_frame_free(&m_frame);
    av_packet_free(&m_pkt);
}

void VideoEncoder::start() {
    m_stopRaw    = false;
    m_stopEncode = false;
    m_rawThread    = std::thread(&VideoEncoder::rawProcessLoop, this);
    m_encodeThread = std::thread(&VideoEncoder::encodeLoop, this);
}

void VideoEncoder::stop() {
    {
        std::lock_guard<std::mutex> lock(m_rawInMutex);
        m_stopRaw = true;
    }
    m_rawInCV.notify_all();

    if (m_rawThread.joinable())
        m_rawThread.join();

    {
        std::lock_guard<std::mutex> lock(m_yuvMutex);
        m_stopEncode = true;
    }
    m_yuvCV.notify_all();

    if (m_encodeThread.joinable())
        m_encodeThread.join();
}

void VideoEncoder::parallelCopy(uint8_t* dst, const uint8_t* src, size_t size) {
    constexpr size_t PARALLEL_THRESHOLD = 4 * 1024 * 1024;
    int numThreads = static_cast<int>(std::thread::hardware_concurrency());

    if (numThreads <= 1 || size < PARALLEL_THRESHOLD) {
        std::memcpy(dst, src, size);
        return;
    }

    size_t chunkSize = size / numThreads;
    std::vector<std::future<void>> futures;

    for (int i = 0; i < numThreads; ++i) {
        size_t offset    = static_cast<size_t>(i) * chunkSize;
        size_t thisChunk = (i == numThreads - 1) ? size - offset : chunkSize;
        futures.push_back(std::async(std::launch::async, [=] {
            std::memcpy(dst + offset, src + offset, thisChunk);
        }));
    }
    for (auto& f : futures) f.get();
}

void VideoEncoder::rawProcessLoop() {
    m_logger.log("VideoEncoder: raw process thread started", LogLevel::INFO);

    while (true) {
        std::unique_lock<std::mutex> lock(m_rawInMutex);
        m_rawInCV.wait_for(lock, std::chrono::milliseconds(100),
                           [this] { return !m_rawInQueue.empty() || m_stopRaw.load(); });

        if (m_stopRaw && m_rawInQueue.empty()) break;
        if (m_rawInQueue.empty()) continue;

        RawFrame frame = m_rawInQueue.front();
        m_rawInQueue.pop();
        lock.unlock();

        size_t ySize  = m_width * m_height;
        size_t uvSize = ySize / 4;

        std::vector<uint8_t> tempY(ySize);
        std::vector<uint8_t> tempU(uvSize);
        std::vector<uint8_t> tempV(uvSize);

        int ret = libyuv::ConvertToI420(
            frame.buffer, frame.size,
            tempY.data(), static_cast<int>(m_width),
            tempU.data(), static_cast<int>(m_width / 2),
            tempV.data(), static_cast<int>(m_width / 2),
            0, 0,
            static_cast<int>(frame.width), static_cast<int>(frame.height),
            static_cast<int>(m_width), static_cast<int>(m_height),
            libyuv::kRotate0, libyuv::FOURCC_ARGB);

        m_bufferPool.releaseRaw(frame.buffer);
        frame.buffer = nullptr;

        if (ret != 0) {
            m_logger.log("VideoEncoder: ConvertToI420 failed, ret=" + std::to_string(ret), LogLevel::ERR);
            continue;
        }

        uint8_t* yuvBuf = m_bufferPool.acquireYuv(ySize + uvSize * 2);
        std::memcpy(yuvBuf,                tempY.data(), ySize);
        std::memcpy(yuvBuf + ySize,        tempU.data(), uvSize);
        std::memcpy(yuvBuf + ySize + uvSize, tempV.data(), uvSize);

        {
            std::lock_guard<std::mutex> qlock(m_yuvMutex);
            if (m_yuvQueue.size() >= MAX_QUEUE_SIZE) {
                m_bufferPool.releaseYuv(m_yuvQueue.front().buffer);
                m_yuvQueue.pop();
            }
            m_yuvQueue.push({yuvBuf, frame.pts});
        }
        m_yuvCV.notify_one();
    }

    m_logger.log("VideoEncoder: raw process thread finished", LogLevel::INFO);
}

void VideoEncoder::encodeLoop() {
    m_logger.log("VideoEncoder: encode thread started", LogLevel::INFO);

    while (true) {
        std::unique_lock<std::mutex> lock(m_yuvMutex);
        m_yuvCV.wait_for(lock, std::chrono::milliseconds(100),
                         [this] { return !m_yuvQueue.empty() || m_stopEncode.load(); });

        if (m_stopEncode && m_yuvQueue.empty()) break;
        if (m_yuvQueue.empty()) continue;

        YuvFrame yuvFrame = m_yuvQueue.front();
        m_yuvQueue.pop();
        lock.unlock();

        size_t ySize  = m_width * m_height;
        size_t uvSize = ySize / 4;

        m_frame->data[0]     = yuvFrame.buffer;
        m_frame->data[1]     = yuvFrame.buffer + ySize;
        m_frame->data[2]     = yuvFrame.buffer + ySize + uvSize;
        m_frame->linesize[0] = static_cast<int>(m_width);
        m_frame->linesize[1] = static_cast<int>(m_width / 2);
        m_frame->linesize[2] = static_cast<int>(m_width / 2);
        m_frame->pts         = av_rescale_q(yuvFrame.pts, {1, 1000000}, m_codecCtx->time_base);

        int ret = avcodec_send_frame(m_codecCtx, m_frame);
        m_bufferPool.releaseYuv(yuvFrame.buffer);

        if (ret < 0) {
            char err[128]; av_strerror(ret, err, sizeof(err));
            m_logger.log("VideoEncoder: send_frame failed: " + std::string(err), LogLevel::ERR);
            continue;
        }

        while (true) {
            av_packet_unref(m_pkt);
            ret = avcodec_receive_packet(m_codecCtx, m_pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            m_pkt->stream_index = m_stream->index;
            av_packet_rescale_ts(m_pkt, m_codecCtx->time_base, m_stream->time_base);

            std::lock_guard<std::mutex> wlock(m_writeMutex);
            if (m_fmtCtx && m_fmtCtx->pb) {
                ret = av_interleaved_write_frame(m_fmtCtx, m_pkt);
                if (ret < 0) {
                    char err[128]; av_strerror(ret, err, sizeof(err));
                    m_logger.log("VideoEncoder: write_frame failed: " + std::string(err), LogLevel::ERR);
                }
            }
        }
    }

    flushCodec();
    m_logger.log("VideoEncoder: encode thread finished", LogLevel::INFO);
}

void VideoEncoder::flushCodec() {
    if (!m_codecCtx) return;

    m_logger.log("VideoEncoder: flushing codec", LogLevel::INFO);
    avcodec_send_frame(m_codecCtx, nullptr);

    while (true) {
        av_packet_unref(m_pkt);
        int ret = avcodec_receive_packet(m_codecCtx, m_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        m_pkt->stream_index = m_stream->index;
        m_pkt->pts = av_rescale_q(m_frameCounter, m_codecCtx->time_base, m_stream->time_base);
        m_pkt->dts = m_pkt->pts;
        ++m_frameCounter;

        std::lock_guard<std::mutex> wlock(m_writeMutex);
        if (m_fmtCtx && m_fmtCtx->pb)
            av_interleaved_write_frame(m_fmtCtx, m_pkt);
    }
}
