#if defined(_WIN32) || defined(_WIN64)

#include "native_audio_decoder.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>
#include <vector>
#include <string>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

namespace {

struct MFInitScope {
    bool initialized = false;
    MFInitScope() {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
            hr = MFStartup(MF_VERSION);
            initialized = SUCCEEDED(hr);
        }
    }
    ~MFInitScope() {
        if (initialized) {
            MFShutdown();
            CoUninitialize();
        }
    }
};

DecodedAudioData decodeFromReader(IMFSourceReader *pReader) {
    DecodedAudioData result;
    if (!pReader) {
        result.errorMessage = "Invalid IMFSourceReader";
        return result;
    }

    // Configure reader to select only the first audio stream
    HRESULT hr = pReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    hr = pReader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    if (FAILED(hr)) {
        result.errorMessage = "Failed to select first audio stream";
        return result;
    }

    // Request uncompressed 32-bit floating-point PCM
    IMFMediaType *pPartialType = nullptr;
    hr = MFCreateMediaType(&pPartialType);
    if (FAILED(hr)) {
        result.errorMessage = "Failed to create partial media type";
        return result;
    }

    pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pPartialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    pPartialType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);

    hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pPartialType);
    pPartialType->Release();
    if (FAILED(hr)) {
        result.errorMessage = "Failed to set audio output media type to Float32";
        return result;
    }

    // Query the actual negotiated audio parameters
    IMFMediaType *pActualType = nullptr;
    hr = pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pActualType);
    if (FAILED(hr)) {
        result.errorMessage = "Failed to get negotiated media type";
        return result;
    }

    UINT32 channels = 2;
    UINT32 sampleRate = 44100;
    pActualType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
    pActualType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
    pActualType->Release();

    if (channels == 0) channels = 2;
    if (sampleRate == 0) sampleRate = 44100;

    std::vector<float> interleavedBuffer;

    while (true) {
        DWORD flags = 0;
        IMFSample *pSample = nullptr;
        LONGLONG timestamp = 0;

        hr = pReader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &timestamp, &pSample);
        if (FAILED(hr)) {
            result.errorMessage = "Error reading sample from Media Foundation";
            return result;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            break;
        }

        if (pSample) {
            IMFMediaBuffer *pBuffer = nullptr;
            hr = pSample->ConvertToContiguousBuffer(&pBuffer);
            if (SUCCEEDED(hr)) {
                BYTE *pAudioData = nullptr;
                DWORD currentLength = 0;
                hr = pBuffer->Lock(&pAudioData, nullptr, &currentLength);
                if (SUCCEEDED(hr)) {
                    size_t floatCount = currentLength / sizeof(float);
                    const float *srcFloats = reinterpret_cast<const float *>(pAudioData);
                    interleavedBuffer.insert(interleavedBuffer.end(), srcFloats, srcFloats + floatCount);
                    pBuffer->Unlock();
                }
                pBuffer->Release();
            }
            pSample->Release();
        }
    }

    size_t totalFrames = interleavedBuffer.size() / channels;
    if (totalFrames == 0) {
        result.errorMessage = "No audio frames decoded";
        return result;
    }

    float *planarBuffer = new (std::nothrow) float[totalFrames * channels];
    if (!planarBuffer) {
        result.errorMessage = "Out of memory allocating planar buffer";
        return result;
    }

    NativeAudioDecoder::interleavedToPlanar(interleavedBuffer.data(), planarBuffer, totalFrames, channels);

    result.samples = planarBuffer;
    result.sampleCount = totalFrames;
    result.channels = channels;
    result.sampleRate = static_cast<float>(sampleRate);
    result.success = true;
    return result;
}

} // anonymous namespace

DecodedAudioData NativeAudioDecoder::decodeFile(const char *filePath) {
    DecodedAudioData result;
    if (!filePath || std::strlen(filePath) == 0) {
        result.errorMessage = "Empty file path";
        return result;
    }

    MFInitScope mfScope;
    if (!mfScope.initialized) {
        result.errorMessage = "Failed to initialize Media Foundation";
        return result;
    }

    int wideLen = MultiByteToWideChar(CP_UTF8, 0, filePath, -1, nullptr, 0);
    if (wideLen <= 0) {
        result.errorMessage = "Invalid UTF-8 file path";
        return result;
    }

    std::vector<wchar_t> wPath(wideLen);
    MultiByteToWideChar(CP_UTF8, 0, filePath, -1, wPath.data(), wideLen);

    IMFSourceReader *pReader = nullptr;
    HRESULT hr = MFCreateSourceReaderFromURL(wPath.data(), nullptr, &pReader);
    if (FAILED(hr) || !pReader) {
        result.errorMessage = "MFCreateSourceReaderFromURL failed with HRESULT: " + std::to_string(hr);
        return result;
    }

    result = decodeFromReader(pReader);
    pReader->Release();
    return result;
}

DecodedAudioData NativeAudioDecoder::decodeMemory(const unsigned char *bytes, size_t length) {
    DecodedAudioData result;
    if (!bytes || length == 0) {
        result.errorMessage = "Empty memory buffer";
        return result;
    }

    MFInitScope mfScope;
    if (!mfScope.initialized) {
        result.errorMessage = "Failed to initialize Media Foundation";
        return result;
    }

    IStream *pStream = SHCreateMemStream(reinterpret_cast<const BYTE *>(bytes), static_cast<UINT>(length));
    if (!pStream) {
        result.errorMessage = "SHCreateMemStream failed";
        return result;
    }

    IMFByteStream *pByteStream = nullptr;
    HRESULT hr = MFCreateMFByteStreamOnStream(pStream, &pByteStream);
    pStream->Release();

    if (FAILED(hr) || !pByteStream) {
        result.errorMessage = "MFCreateMFByteStreamOnStream failed: " + std::to_string(hr);
        return result;
    }

    IMFSourceReader *pReader = nullptr;
    hr = MFCreateSourceReaderFromByteStream(pByteStream, nullptr, &pReader);
    pByteStream->Release();

    if (FAILED(hr) || !pReader) {
        result.errorMessage = "MFCreateSourceReaderFromByteStream failed: " + std::to_string(hr);
        return result;
    }

    result = decodeFromReader(pReader);
    pReader->Release();
    return result;
}

#endif // defined(_WIN32) || defined(_WIN64)
