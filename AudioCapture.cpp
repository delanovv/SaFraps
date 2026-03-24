#include "AudioCapture.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <combaseapi.h>

#define SAFE_RELEASE(p) if ((p)) { (p)->Release(); (p) = nullptr; }

AudioCapture::AudioCapture(Logger&                           logger,
                            std::queue<AudioFrame>&           outQueue,
                            std::mutex&                       outMutex,
                            std::condition_variable&          outCV,
                            std::chrono::steady_clock::time_point startTime,
                            int                               sampleRate,
                            int                               channels)
    : m_logger(logger)
    , m_outQueue(outQueue)
    , m_outMutex(outMutex)
    , m_outCV(outCV)
    , m_startTime(startTime)
    , m_sampleRate(sampleRate)
    , m_channels(channels)
{}

AudioCapture::~AudioCapture() {
    stop();
}

void AudioCapture::start() {
    m_stop = false;
    m_thread = std::thread(&AudioCapture::captureLoop, this);
}

void AudioCapture::stop() {
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
}

void AudioCapture::captureLoop() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        m_logger.log("AudioCapture: CoInitializeEx failed, hr=" + std::to_string(hr), LogLevel::ERR);
        return;
    }

    IMMDeviceEnumerator* pEnumerator  = nullptr;
    IMMDevice*           pDevice      = nullptr;
    IAudioClient*        pAudioClient = nullptr;
    IAudioCaptureClient* pCapture     = nullptr;
    WAVEFORMATEX*        pwfx         = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&pEnumerator));
    if (FAILED(hr)) {
        m_logger.log("AudioCapture: CoCreateInstance failed, hr=" + std::to_string(hr), LogLevel::ERR);
        goto cleanup;
    }

    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr)) {
        m_logger.log("AudioCapture: GetDefaultAudioEndpoint failed, hr=" + std::to_string(hr), LogLevel::ERR);
        goto cleanup;
    }

    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&pAudioClient));
    if (FAILED(hr)) {
        m_logger.log("AudioCapture: Activate failed, hr=" + std::to_string(hr), LogLevel::ERR);
        goto cleanup;
    }

    hr = pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) {
        m_logger.log("AudioCapture: GetMixFormat failed, hr=" + std::to_string(hr), LogLevel::ERR);
        goto cleanup;
    }

    {
        bool isFloat32 = (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && pwfx->wBitsPerSample == 32);
        bool isExtFloat32 = false;
        if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
            isExtFloat32 = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && pwfx->wBitsPerSample == 32);
        }

        if (!isFloat32 && !isExtFloat32) {
            m_logger.log("AudioCapture: unsupported format (expected IEEE_FLOAT 32-bit)", LogLevel::ERR);
            CoTaskMemFree(pwfx);
            goto cleanup;
        }

        if (m_sampleRate != 0 && m_channels != 0) {
            if (static_cast<DWORD>(m_sampleRate) != pwfx->nSamplesPerSec ||
                static_cast<WORD>(m_channels) != pwfx->nChannels) {
                m_logger.log("AudioCapture: device format mismatch", LogLevel::ERR);
                CoTaskMemFree(pwfx);
                goto cleanup;
            }
        }
    }

    m_logger.log("AudioCapture: format OK " + std::to_string(pwfx->nSamplesPerSec) +
                 "Hz " + std::to_string(pwfx->nChannels) + "ch", LogLevel::INFO);

    hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                   10000000, 0, pwfx, nullptr);
    CoTaskMemFree(pwfx);
    pwfx = nullptr;

    if (FAILED(hr)) {
        m_logger.log("AudioCapture: Initialize failed, hr=" + std::to_string(hr), LogLevel::ERR);
        goto cleanup;
    }

    hr = pAudioClient->GetService(__uuidof(IAudioCaptureClient),
                                   reinterpret_cast<void**>(&pCapture));
    if (FAILED(hr)) {
        m_logger.log("AudioCapture: GetService failed, hr=" + std::to_string(hr), LogLevel::ERR);
        goto cleanup;
    }

    hr = pAudioClient->Start();
    if (FAILED(hr)) {
        m_logger.log("AudioCapture: Start failed, hr=" + std::to_string(hr), LogLevel::ERR);
        goto cleanup;
    }

    while (!m_stop) {
        UINT32 packetLength = 0;
        if (FAILED(pCapture->GetNextPacketSize(&packetLength))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        while (packetLength > 0) {
            BYTE*  pData;
            UINT32 numFrames;
            DWORD  flags;

            if (FAILED(pCapture->GetBuffer(&pData, &numFrames, &flags, nullptr, nullptr)))
                break;

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && pData && numFrames > 0) {
                size_t   frameSize = numFrames * static_cast<size_t>(m_channels) * sizeof(float);
                uint8_t* copy      = new uint8_t[frameSize];
                std::memcpy(copy, pData, frameSize);

                int64_t ts = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - m_startTime).count();

                {
                    std::lock_guard<std::mutex> lock(m_outMutex);
                    if (m_outQueue.size() >= MAX_QUEUE_SIZE) {
                        delete[] m_outQueue.front().data;
                        m_outQueue.pop();
                    }
                    m_outQueue.push({copy, ts, numFrames});
                }
                m_outCV.notify_one();
            }

            pCapture->ReleaseBuffer(numFrames);

            if (FAILED(pCapture->GetNextPacketSize(&packetLength)))
                break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    pAudioClient->Stop();
    m_logger.log("AudioCapture: stopped", LogLevel::INFO);

cleanup:
    SAFE_RELEASE(pCapture);
    SAFE_RELEASE(pAudioClient);
    SAFE_RELEASE(pDevice);
    SAFE_RELEASE(pEnumerator);
    CoUninitialize();
}
