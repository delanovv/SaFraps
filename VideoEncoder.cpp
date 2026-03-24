#include "VideoEncoder.h"
#include "MemUtils.h"
#include <libyuv.h>
#include <algorithm>

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
    , m_threadPool(std::max(1u, std::thread::hardware_concurrency()))
{
    size_t ySize  = static_cast<size_t>(width) * height;
    size_t uvSize = ySize / 4;
    m_tempY.resize(ySize);
    m_tempU.resize(uvSize);
    m_tempV.resize(uvSize);

    m_frame = av_frame_alloc();
    if (m_frame) {
        m_frame->format = AV_PIX_FMT_YUV420P;
        m_frame->width  = static_cast<int>(width);
        m_frame->height = static_cast<int>(height);
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
    if (m_rawThread.joinable()) m_rawThread.join();

    {
        std::lock_guard<std::mutex> lock(m_yuvMutex);
        m_stopEncode = true;
    }
    m_yuvCV.notify_all();
    if (m_encodeThread.joinable()) m_encodeThread.join();
}

void VideoEncoder::rawProcessLoop() {
    m_logger.log("VideoEncoder: raw thread started", LogLevel::INFO);

    while (true) {
        std::unique_lock<std::mutex> lock(m_rawInMutex);
        m_rawInCV.wait_for(lock, std::chrono::milliseconds(100),
                           [this] { return !m_rawInQueue.empty() || m_stopRaw.load(); });

        if (m_stopRaw && m_rawInQueue.empty()) break;
        if (m_rawInQueue.empty()) continue;

        RawFrame frame = m_rawInQueue.front();
        m_rawInQueue.pop();
        lock.unlock();

        int ret = libyuv::ConvertToI420(
            frame.buffer, frame.size,
            m_tempY.data(), static_cast<int>(m_width),
            m_tempU.data(), static_cast<int>(m_width / 2),
            m_tempV.data(), static_cast<int>(m_width / 2),
            0, 0,
            static_cast<int>(frame.width),  static_cast<int>(frame.height),
            static_cast<int>(m_width),      static_cast<int>(m_height),
            libyuv::kRotate0, libyuv::FOURCC_ARGB);

        m_bufferPool.releaseRaw(frame.buffer);
        frame.buffer = nullptr;

        if (ret != 0) {
            m_logger.log("VideoEncoder: ConvertToI420 failed ret=" + std::to_string(ret), LogLevel::ERR);
            continue;
        }

        size_t   ySize  = static_cast<size_t>(m_width) * m_height;
        size_t   uvSize = ySize / 4;
        uint8_t* yuvBuf = m_bufferPool.acquireYuv(ySize + uvSize * 2);

        parallelNtCopy(yuvBuf,                m_tempY.data(), ySize,  m_threadPool);
        parallelNtCopy(yuvBuf + ySize,        m_tempU.data(), uvSize, m_threadPool);
        parallelNtCopy(yuvBuf + ySize + uvSize, m_tempV.data(), uvSize, m_threadPool);

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

    m_logger.log("VideoEncoder: raw thread finished", LogLevel::INFO);
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

        size_t ySize  = static_cast<size_t>(m_width) * m_height;
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
            if (m_fmtCtx && m_fmtCtx->pb)
                av_interleaved_write_frame(m_fmtCtx, m_pkt);
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
