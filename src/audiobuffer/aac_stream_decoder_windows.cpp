#if defined(_WIN32) || defined(_WIN64)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "aac_stream_decoder.h"
#include "../native_decoder/native_audio_decoder.h"
#include <windows.h>
#include <mfapi.h>
#include <mftransform.h>
#include <mferror.h>
#include <mfidl.h>
#include <vector>
#include <deque>
#include <algorithm>
#include <iostream>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace {

// Known decoder CLSIDs
// CLSID_CMSAACDecMft: 32D18304-BDAC-4CAE-8C76-FB7042EE5948
static const GUID kCLSID_CMSAACDecMft =
    {0x32d18304, 0xbdac, 0x4cae, {0x8c, 0x76, 0xfb, 0x70, 0x42, 0xee, 0x59, 0x48}};

// CLSID_CMSAC3DecMft: C0E5D9EB-24EE-49E2-8C44-3626774536AE
static const GUID kCLSID_CMSAC3DecMft =
    {0xc0e5d9eb, 0x24ee, 0x49e2, {0x8c, 0x44, 0x36, 0x26, 0x77, 0x45, 0x36, 0xae}};

class WindowsAACImpl : public AACDecoderWrapper::Impl {
public:
    explicit WindowsAACImpl(DetectedType format = DetectedType::BUFFER_AAC)
        : mFormat(format) {}

    ~WindowsAACImpl() override {
        cleanup();
    }

    bool initialize(int engineSamplerate, int engineChannels) override {
        cleanup();

        mTargetSampleRate = engineSamplerate;
        mTargetChannels = engineChannels;

        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            mCoInitialized = true;
        }

        hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) {
            std::cerr << "[flutter_soloud] MFStartup failed: " << hr << std::endl;
            return false;
        }
        mMfStarted = true;

        GUID subType = MFAudioFormat_AAC;
        GUID clsid = kCLSID_CMSAACDecMft;
        if (mFormat == DetectedType::BUFFER_AC3) {
            subType = MFAudioFormat_Dolby_AC3;
            clsid = kCLSID_CMSAC3DecMft;
        } else if (mFormat == DetectedType::BUFFER_EAC3) {
            subType = MFAudioFormat_Dolby_DDPlus;
            clsid = kCLSID_CMSAC3DecMft;
        }

        // 1. Try finding decoder via MFTEnumEx
        MFT_REGISTER_TYPE_INFO inType = { MFMediaType_Audio, subType };
        IMFActivate **ppActivate = nullptr;
        UINT32 count = 0;
        hr = MFTEnumEx(
            MFT_CATEGORY_AUDIO_DECODER,
            MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
            &inType,
            nullptr,
            &ppActivate,
            &count
        );

        if (SUCCEEDED(hr) && count > 0) {
            hr = ppActivate[0]->ActivateObject(IID_PPV_ARGS(&mTransform));
            for (UINT32 i = 0; i < count; i++) ppActivate[i]->Release();
            CoTaskMemFree(ppActivate);
        }

        // 2. Fallback to CoCreateInstance
        if (!mTransform) {
            hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mTransform));
        }

        if (!mTransform) {
            std::cerr << "[flutter_soloud] Failed to create Media Foundation Transform audio decoder for format "
                      << static_cast<int>(mFormat) << std::endl;
            return false;
        }

        mInitialized = true;
        return true;
    }

    std::pair<std::vector<float>, DecoderError>
    decode(std::vector<unsigned char> &buffer, int *samplerate,
           int *channels, size_t maxOutputSamples) override {
        if (!mInitialized || !mTransform) {
            return {{}, DecoderError::FailedToCreateDecoder};
        }

        std::vector<float> decodedData;

        // Drain any remaining samples from previous decode
        if (!mDecodedRemainder.empty()) {
            size_t toTake = mDecodedRemainder.size();
            if (maxOutputSamples > 0 && toTake > maxOutputSamples) {
                toTake = maxOutputSamples;
            }
            decodedData.insert(decodedData.end(), mDecodedRemainder.begin(), mDecodedRemainder.begin() + toTake);
            mDecodedRemainder.erase(mDecodedRemainder.begin(), mDecodedRemainder.begin() + toTake);
            if (maxOutputSamples > 0 && decodedData.size() >= maxOutputSamples) {
                if (samplerate && mStreamSampleRate > 0) *samplerate = mStreamSampleRate;
                if (channels && mStreamChannels > 0) *channels = mStreamChannels;
                return {std::move(decodedData), DecoderError::NoError};
            }
        }

        // Align to syncword if first frame
        if (!mFormatConfigured && !buffer.empty()) {
            if (mFormat == DetectedType::BUFFER_AC3 || mFormat == DetectedType::BUFFER_EAC3) {
                int syncIdx = NativeAudioDecoder::findAc3Syncword(buffer.data(), buffer.size());
                if (syncIdx > 0) {
                    buffer.erase(buffer.begin(), buffer.begin() + syncIdx);
                }
            } else if (mFormat == DetectedType::BUFFER_AAC) {
                if (!NativeAudioDecoder::isAacAdts(buffer.data(), buffer.size())) {
                    for (size_t i = 0; i + 2 <= buffer.size(); ++i) {
                        if (NativeAudioDecoder::isAacAdts(buffer.data() + i, buffer.size() - i)) {
                            buffer.erase(buffer.begin(), buffer.begin() + i);
                            break;
                        }
                    }
                }
            }
        }

        // Configure MFT media types once metadata is discoverable
        if (!mFormatConfigured && !buffer.empty()) {
            if (!configureMediaTypes(buffer)) {
                // Wait for more data to configure types
                return {std::move(decodedData), DecoderError::NoError};
            }
        }

        // Feed frames to MFT
        while (!buffer.empty()) {
            size_t frameLen = getNextFrameLength(buffer.data(), buffer.size());
            if (frameLen == 0) {
                if (mFormat == DetectedType::BUFFER_AC3 || mFormat == DetectedType::BUFFER_EAC3) {
                    int syncIdx = NativeAudioDecoder::findAc3Syncword(buffer.data(), buffer.size());
                    if (syncIdx > 0) {
                        buffer.erase(buffer.begin(), buffer.begin() + syncIdx);
                        continue;
                    }
                } else if (mFormat == DetectedType::BUFFER_AAC) {
                    size_t nextSync = 0;
                    for (size_t i = 1; i + 2 <= buffer.size(); ++i) {
                        if (NativeAudioDecoder::isAacAdts(buffer.data() + i, buffer.size() - i)) {
                            nextSync = i;
                            break;
                        }
                    }
                    if (nextSync > 0) {
                        buffer.erase(buffer.begin(), buffer.begin() + nextSync);
                        continue;
                    }
                }
                break; // Incomplete frame, wait for more data
            }
            if (buffer.size() < frameLen) {
                break; // Incomplete frame, wait for more data
            }

            // Create media buffer and sample for this frame
            IMFMediaBuffer *pBuffer = nullptr;
            HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(frameLen), &pBuffer);
            if (FAILED(hr) || !pBuffer) break;

            BYTE *pData = nullptr;
            hr = pBuffer->Lock(&pData, nullptr, nullptr);
            if (SUCCEEDED(hr) && pData) {
                std::memcpy(pData, buffer.data(), frameLen);
                pBuffer->Unlock();
                pBuffer->SetCurrentLength(static_cast<DWORD>(frameLen));

                IMFSample *pSample = nullptr;
                hr = MFCreateSample(&pSample);
                if (SUCCEEDED(hr) && pSample) {
                    pSample->AddBuffer(pBuffer);

                    hr = mTransform->ProcessInput(0, pSample, 0);
                    pSample->Release();

                    if (SUCCEEDED(hr)) {
                        drainTransform(decodedData, maxOutputSamples);
                    }
                }
            }
            pBuffer->Release();

            buffer.erase(buffer.begin(), buffer.begin() + frameLen);
        }

        if (samplerate && mStreamSampleRate > 0) *samplerate = mStreamSampleRate;
        if (channels && mStreamChannels > 0) *channels = mStreamChannels;

        return {std::move(decodedData), DecoderError::NoError};
    }

    void setDataEnded() override {
        if (!mInitialized || !mTransform) return;

        mTransform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);

        std::vector<float> flushed;
        drainTransform(flushed, 0);
        if (!flushed.empty()) {
            mDecodedRemainder.insert(mDecodedRemainder.end(), flushed.begin(), flushed.end());
        }
    }

    bool hasPendingData() const override {
        return !mDecodedRemainder.empty();
    }

private:
    void cleanup() {
        if (mTransform) {
            mTransform->Release();
            mTransform = nullptr;
        }
        if (mMfStarted) {
            MFShutdown();
            mMfStarted = false;
        }
        if (mCoInitialized) {
            CoUninitialize();
            mCoInitialized = false;
        }
        mInitialized = false;
        mFormatConfigured = false;
        mDecodedRemainder.clear();
    }

    size_t getNextFrameLength(const unsigned char *data, size_t size) {
        if (size < 7) return 0;

        if (mFormat == DetectedType::BUFFER_AAC) {
            if (NativeAudioDecoder::isAacAdts(data, size)) {
                uint32_t len = ((static_cast<uint32_t>(data[3] & 0x03) << 11) |
                                (static_cast<uint32_t>(data[4]) << 3) |
                                ((static_cast<uint32_t>(data[5]) >> 5) & 0x07));
                return static_cast<size_t>(len);
            }
        } else if (mFormat == DetectedType::BUFFER_AC3) {
            Ac3Metadata ac3;
            if (AACDecoderWrapper::parseAc3Metadata(data, size, ac3)) {
                return static_cast<size_t>(ac3.frameSize);
            }
        } else if (mFormat == DetectedType::BUFFER_EAC3) {
            Eac3Metadata eac3;
            if (AACDecoderWrapper::parseEac3Metadata(data, size, eac3)) {
                return static_cast<size_t>(eac3.frameSize);
            }
        }
        return 0;
    }

    bool configureOutputTypes() {
        IMFMediaType *pSelected = nullptr;
        mIsFloatOutput = false;

        for (DWORD i = 0; ; i++) {
            IMFMediaType *pAvail = nullptr;
            HRESULT hr = mTransform->GetOutputAvailableType(0, i, &pAvail);
            if (FAILED(hr)) break;

            GUID subType = GUID_NULL;
            pAvail->GetGUID(MF_MT_SUBTYPE, &subType);
            if (subType == MFAudioFormat_Float) {
                if (pSelected) pSelected->Release();
                pSelected = pAvail;
                mIsFloatOutput = true;
                break;
            } else if (subType == MFAudioFormat_PCM && !pSelected) {
                pSelected = pAvail;
                mIsFloatOutput = false;
            } else {
                pAvail->Release();
            }
        }

        if (!pSelected) {
            IMFMediaType *pOutType = nullptr;
            HRESULT hr = MFCreateMediaType(&pOutType);
            if (SUCCEEDED(hr) && pOutType) {
                pOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                pOutType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
                pOutType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
                pOutType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, mStreamSampleRate > 0 ? mStreamSampleRate : 44100);
                pOutType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, mStreamChannels > 0 ? mStreamChannels : 2);
                hr = mTransform->SetOutputType(0, pOutType, 0);
                if (SUCCEEDED(hr)) {
                    mIsFloatOutput = true;
                    pOutType->Release();
                    return true;
                }
                pOutType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
                pOutType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                hr = mTransform->SetOutputType(0, pOutType, 0);
                if (SUCCEEDED(hr)) {
                    mIsFloatOutput = false;
                    pOutType->Release();
                    return true;
                }
                pOutType->Release();
            }
            return false;
        }

        HRESULT hr = mTransform->SetOutputType(0, pSelected, 0);
        UINT32 actualChannels = 0;
        UINT32 actualSampleRate = 0;
        pSelected->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &actualChannels);
        pSelected->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &actualSampleRate);
        if (actualChannels > 0) mStreamChannels = actualChannels;
        if (actualSampleRate > 0) mStreamSampleRate = actualSampleRate;

        pSelected->Release();
        return SUCCEEDED(hr);
    }

    bool configureMediaTypes(const std::vector<unsigned char> &buffer) {
        int sampleRate = 0;
        int channels = 0;

        if (mFormat == DetectedType::BUFFER_AAC) {
            AacMetadata aac;
            if (AACDecoderWrapper::parseAacAdtsMetadata(buffer.data(), buffer.size(), aac)) {
                sampleRate = aac.sampleRate;
                channels = aac.channels;
            }
        } else if (mFormat == DetectedType::BUFFER_AC3) {
            Ac3Metadata ac3;
            if (AACDecoderWrapper::parseAc3Metadata(buffer.data(), buffer.size(), ac3)) {
                sampleRate = ac3.sampleRate;
                channels = ac3.channels;
            }
        } else if (mFormat == DetectedType::BUFFER_EAC3) {
            Eac3Metadata eac3;
            if (AACDecoderWrapper::parseEac3Metadata(buffer.data(), buffer.size(), eac3)) {
                sampleRate = eac3.sampleRate;
                channels = eac3.channels;
            }
        }

        if (sampleRate <= 0) sampleRate = mTargetSampleRate > 0 ? mTargetSampleRate : 44100;
        if (channels <= 0) channels = mTargetChannels > 0 ? mTargetChannels : 2;

        mStreamSampleRate = sampleRate;
        mStreamChannels = channels;

        GUID subType = MFAudioFormat_AAC;
        if (mFormat == DetectedType::BUFFER_AC3) subType = MFAudioFormat_Dolby_AC3;
        else if (mFormat == DetectedType::BUFFER_EAC3) subType = MFAudioFormat_Dolby_DDPlus;

        IMFMediaType *pInType = nullptr;
        if (mFormat == DetectedType::BUFFER_AAC) {
            for (DWORD i = 0; ; i++) {
                IMFMediaType *pAvail = nullptr;
                HRESULT hrAvail = mTransform->GetInputAvailableType(0, i, &pAvail);
                if (FAILED(hrAvail)) break;

                UINT32 payloadType = 0;
                pAvail->GetUINT32(MF_MT_AAC_PAYLOAD_TYPE, &payloadType);
                if (payloadType == 1) { // 1 = ADTS
                    pInType = pAvail;
                    break;
                }
                pAvail->Release();
            }
        }

        if (!pInType) {
            HRESULT hrCreate = MFCreateMediaType(&pInType);
            if (FAILED(hrCreate) || !pInType) return false;

            pInType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            pInType->SetGUID(MF_MT_SUBTYPE, subType);
            if (mFormat == DetectedType::BUFFER_AAC) {
                pInType->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 1); // 1 = ADTS
            }
        }

        pInType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
        pInType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);

        HRESULT hr = mTransform->SetInputType(0, pInType, 0);
        pInType->Release();
        if (FAILED(hr)) return false;

        if (!configureOutputTypes()) return false;

        mTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        mFormatConfigured = true;
        return true;
    }

    void drainTransform(std::vector<float> &out, size_t maxOutputSamples) {
        MFT_OUTPUT_STREAM_INFO streamInfo = {};
        DWORD bufferSize = 65536;
        if (SUCCEEDED(mTransform->GetOutputStreamInfo(0, &streamInfo)) && streamInfo.cbSize > 0) {
            bufferSize = streamInfo.cbSize;
        }

        while (true) {
            IMFMediaBuffer *pBuffer = nullptr;
            HRESULT hr = MFCreateMemoryBuffer(bufferSize, &pBuffer);
            if (FAILED(hr) || !pBuffer) break;

            IMFSample *pSample = nullptr;
            hr = MFCreateSample(&pSample);
            if (FAILED(hr) || !pSample) {
                pBuffer->Release();
                break;
            }

            pSample->AddBuffer(pBuffer);

            MFT_OUTPUT_DATA_BUFFER outBuffer = {};
            outBuffer.dwStreamID = 0;
            outBuffer.pSample = pSample;

            DWORD status = 0;
            hr = mTransform->ProcessOutput(0, 1, &outBuffer, &status);

            if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
                pSample->Release();
                pBuffer->Release();
                configureOutputTypes();
                if (SUCCEEDED(mTransform->GetOutputStreamInfo(0, &streamInfo)) && streamInfo.cbSize > 0) {
                    bufferSize = streamInfo.cbSize;
                }
                continue;
            }

            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                pSample->Release();
                pBuffer->Release();
                break;
            }

            if (outBuffer.pEvents) {
                outBuffer.pEvents->Release();
            }

            if (SUCCEEDED(hr) && outBuffer.pSample) {
                IMFMediaBuffer *pContig = nullptr;
                if (SUCCEEDED(outBuffer.pSample->ConvertToContiguousBuffer(&pContig)) && pContig) {
                    BYTE *pAudio = nullptr;
                    DWORD len = 0;
                    if (SUCCEEDED(pContig->Lock(&pAudio, nullptr, &len)) && pAudio && len > 0) {
                        std::vector<float> frameSamples;
                        if (mIsFloatOutput) {
                            size_t floatCount = len / sizeof(float);
                            const float *src = reinterpret_cast<const float *>(pAudio);
                            frameSamples.insert(frameSamples.end(), src, src + floatCount);
                        } else {
                            size_t sampleCount = len / sizeof(int16_t);
                            const int16_t *src = reinterpret_cast<const int16_t *>(pAudio);
                            frameSamples.reserve(sampleCount);
                            for (size_t s = 0; s < sampleCount; s++) {
                                frameSamples.push_back(src[s] / 32768.0f);
                            }
                        }

                        size_t available = (maxOutputSamples > 0 && out.size() < maxOutputSamples)
                                               ? (maxOutputSamples - out.size())
                                               : (maxOutputSamples == 0 ? frameSamples.size() : 0);
                        size_t toAppend = std::min(frameSamples.size(), available);
                        out.insert(out.end(), frameSamples.begin(), frameSamples.begin() + toAppend);
                        for (size_t s = toAppend; s < frameSamples.size(); s++) {
                            mDecodedRemainder.push_back(frameSamples[s]);
                        }

                        pContig->Unlock();
                    }
                    pContig->Release();
                }
            }

            pSample->Release();
            pBuffer->Release();

            if (FAILED(hr)) break;
        }
    }

    DetectedType mFormat;
    int mTargetSampleRate = 44100;
    int mTargetChannels = 2;
    int mStreamSampleRate = 0;
    int mStreamChannels = 0;

    IMFTransform *mTransform = nullptr;
    bool mCoInitialized = false;
    bool mMfStarted = false;
    bool mInitialized = false;
    bool mFormatConfigured = false;
    bool mIsFloatOutput = true;

    std::deque<float> mDecodedRemainder;
};

} // anonymous namespace

std::unique_ptr<AACDecoderWrapper::Impl> createWindowsAACDecoderImpl(DetectedType format) {
    return std::make_unique<WindowsAACImpl>(format);
}

#endif // defined(_WIN32) || defined(_WIN64)
