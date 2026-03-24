#include "AudioEncoder.h"
#include <cstring>

AudioEncoder::AudioEncoder(Logger&                  logger,
                            std::queue<AudioFrame>&  inQueue,
                            std::mutex&              inMutex,
                            std::condition_variable& inCV,
                            AVFormatContext*         fmtCtx,
                            AVCodecContext*          codecCtx,
                            AVStream*                stream,
                            SwrContext*              swrCtx,
                            std::mutex&              writeMutex,
                            int                      sampleRate,
                            int                      channels)
    : m_logger(logger)
    , m_inQueue(inQueue)
    , m_inMutex(inMutex)
    , m_inCV(inCV)
    , m_fmtCtx(fmtCtx)
    , m_codecCtx(codecCtx)
    , m_stream(stream)
    , m_swrCtx(swrCtx)
    , m_writeMutex(writeMutex)
    , m_sampleRate(sampleRate)
    , m_channels(channels)
{
    m_frame = av_frame_alloc();
    m_pkt   = av_packet_alloc();
}

AudioEncoder::~AudioEncoder() {
    stop();
    av_frame_free(&m_frame);
    av_packet_free(&m_pkt);
}

void AudioEncoder::start() {
    m_stop   = false;
    m_thread = std::thread(&AudioEncoder::encodeLoop, this);
}

void AudioEncoder::stop() {
    m_stop = true;
    m_inCV.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void AudioEncoder::encodeLoop() {
    m_logger.log("AudioEncoder: started", LogLevel::INFO);

    int     requiredSamples = m_codecCtx ? m_codecCtx->frame_size : 1024;
    int64_t lastPts         = 0;

    std::vector<std::vector<float>> audioBuffer(m_channels);
    for (auto& ch : audioBuffer)
        ch.reserve(static_cast<size_t>(requiredSamples) * 2);

    while (true) {
        std::unique_lock<std::mutex> lock(m_inMutex);
        m_inCV.wait_for(lock, std::chrono::milliseconds(100),
                        [this] { return !m_inQueue.empty() || m_stop.load(); });

        if (m_stop && m_inQueue.empty()) {
            lock.unlock();
            if (!audioBuffer[0].empty()) {
                for (auto& ch : audioBuffer)
                    ch.resize(static_cast<size_t>(requiredSamples), 0.0f);
                sendFrame(audioBuffer, lastPts, requiredSamples);
            }
            flushCodec(lastPts);
            break;
        }

        if (m_inQueue.empty()) continue;

        auto [pData, pts, frameCount] = m_inQueue.front();
        m_inQueue.pop();
        lock.unlock();

        if (!pData || frameCount == 0) { delete[] pData; continue; }

        if (pts <= lastPts)
            pts = lastPts + static_cast<int64_t>(1000000.0 * frameCount / m_sampleRate);

        int      outSamples    = static_cast<int>(av_rescale_rnd(
            swr_get_delay(m_swrCtx, m_sampleRate) + frameCount,
            m_sampleRate, m_sampleRate, AV_ROUND_UP));
        uint8_t* converted[2] = {nullptr, nullptr};

        if (av_samples_alloc(converted, nullptr, m_channels, outSamples, AV_SAMPLE_FMT_FLTP, 0) < 0) {
            delete[] pData;
            continue;
        }

        int ret = swr_convert(m_swrCtx, converted, outSamples,
                              const_cast<const uint8_t**>(reinterpret_cast<uint8_t**>(&pData)),
                              static_cast<int>(frameCount));
        delete[] pData;

        if (ret > 0) {
            for (int ch = 0; ch < m_channels; ++ch) {
                float* src = reinterpret_cast<float*>(converted[ch]);
                audioBuffer[ch].insert(audioBuffer[ch].end(), src, src + ret);
            }
        }
        av_freep(&converted[0]);

        while (audioBuffer[0].size() >= static_cast<size_t>(requiredSamples)) {
            sendFrame(audioBuffer, lastPts, requiredSamples);
            lastPts += static_cast<int64_t>(1000000.0 * requiredSamples / m_sampleRate);
        }
    }

    m_logger.log("AudioEncoder: finished", LogLevel::INFO);
}

void AudioEncoder::sendFrame(std::vector<std::vector<float>>& buffer, int64_t pts, int requiredSamples) {
    if (!m_codecCtx || !m_frame) return;

    av_frame_unref(m_frame);
    m_frame->nb_samples     = requiredSamples;
    m_frame->format         = AV_SAMPLE_FMT_FLTP;
    m_frame->channel_layout = av_get_default_channel_layout(m_channels);
    m_frame->sample_rate    = m_sampleRate;
    m_frame->pts            = av_rescale_q(pts < 0 ? 0 : pts, {1, 1000000}, m_codecCtx->time_base);

    if (av_frame_get_buffer(m_frame, 0) < 0) { av_frame_unref(m_frame); return; }

    for (int ch = 0; ch < m_channels; ++ch) {
        if (buffer[ch].size() < static_cast<size_t>(requiredSamples)) {
            av_frame_unref(m_frame);
            return;
        }
        std::memcpy(m_frame->data[ch], buffer[ch].data(), requiredSamples * sizeof(float));
        buffer[ch].erase(buffer[ch].begin(), buffer[ch].begin() + requiredSamples);
    }

    int ret = avcodec_send_frame(m_codecCtx, m_frame);
    av_frame_unref(m_frame);
    if (ret < 0) return;

    while (true) {
        av_packet_unref(m_pkt);
        ret = avcodec_receive_packet(m_codecCtx, m_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        m_pkt->stream_index = m_stream->index;
        if (m_pkt->pts == AV_NOPTS_VALUE)
            m_pkt->pts = av_rescale_q(pts, {1, 1000000}, m_stream->time_base);
        m_pkt->dts = m_pkt->pts;

        std::lock_guard<std::mutex> wlock(m_writeMutex);
        if (m_fmtCtx && m_fmtCtx->pb)
            av_interleaved_write_frame(m_fmtCtx, m_pkt);
    }
}

void AudioEncoder::flushCodec(int64_t lastPts) {
    if (!m_codecCtx) return;
    avcodec_send_frame(m_codecCtx, nullptr);

    while (true) {
        av_packet_unref(m_pkt);
        int ret = avcodec_receive_packet(m_codecCtx, m_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        m_pkt->stream_index = m_stream->index;
        if (m_pkt->pts == AV_NOPTS_VALUE)
            m_pkt->pts = av_rescale_q(lastPts, {1, 1000000}, m_stream->time_base);
        m_pkt->dts = m_pkt->pts;

        std::lock_guard<std::mutex> wlock(m_writeMutex);
        if (m_fmtCtx && m_fmtCtx->pb)
            av_interleaved_write_frame(m_fmtCtx, m_pkt);
    }
    m_logger.log("AudioEncoder: codec flushed", LogLevel::INFO);
}
