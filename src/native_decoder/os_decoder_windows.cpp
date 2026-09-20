#if defined(_WIN32) || defined(_WIN64)

#include "native_audio_decoder.h"
#include "../audiobuffer/aac_stream_decoder.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>
#include <vector>
#include <string>
#include <fstream>

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

DecodedAudioData decodeElementaryStream(const unsigned char *bytes, size_t length, DetectedType type) {
    DecodedAudioData result;
    AACDecoderWrapper wrapper(type);
    if (!wrapper.initializeDecoder(0, 0)) {
        result.errorMessage = "Failed to initialize elementary stream decoder on Windows";
        return result;
    }

    std::vector<unsigned char> buffer(bytes, bytes + length);
    int sampleRate = 0;
    int channels = 0;

    std::vector<float> allSamples;
    while (!buffer.empty()) {
        auto [samples, err] = wrapper.decode(buffer, &sampleRate, &channels, 0);
        if (!samples.empty()) {
            allSamples.insert(allSamples.end(), samples.begin(), samples.end());
        }
        if (err != DecoderError::NoError && samples.empty()) {
            break;
        }
    }

    wrapper.setDataEnded();
    while (wrapper.hasPendingData()) {
        std::vector<unsigned char> emptyBuf;
        auto [samples, err] = wrapper.decode(emptyBuf, &sampleRate, &channels, 0);
        if (!samples.empty()) {
            allSamples.insert(allSamples.end(), samples.begin(), samples.end());
        } else {
            break;
        }
    }

    if (channels <= 0) channels = 2;
    if (sampleRate <= 0) sampleRate = 44100;

    size_t totalFrames = allSamples.size() / channels;
    if (totalFrames == 0) {
        result.errorMessage = "No audio frames decoded from elementary stream";
        return result;
    }

    float *planarBuffer = new (std::nothrow) float[totalFrames * channels];
    if (!planarBuffer) {
        result.errorMessage = "Out of memory allocating planar buffer";
        return result;
    }

    NativeAudioDecoder::interleavedToPlanar(allSamples.data(), planarBuffer, totalFrames, channels);

    result.samples = planarBuffer;
    result.sampleCount = totalFrames;
    result.channels = static_cast<unsigned int>(channels);
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

    // Check if the file is a raw AC-3 or E-AC-3 elementary stream (which IMFSourceReader has no ByteStreamHandler for)
    std::ifstream file(filePath, std::ios::binary);
    if (file.is_open()) {
        unsigned char header[16] = {0};
        file.read(reinterpret_cast<char*>(header), sizeof(header));
        std::streamsize bytesRead = file.gcount();
        file.seekg(0, std::ios::beg);

        if (bytesRead >= 6 && NativeAudioDecoder::isAc3OrEac3(header, static_cast<size_t>(bytesRead))) {
            std::vector<unsigned char> fileData((std::istreambuf_iterator<char>(file)),
                                                 std::istreambuf_iterator<char>());
            DetectedType type = NativeAudioDecoder::isEac3(header, static_cast<size_t>(bytesRead))
                                    ? DetectedType::BUFFER_EAC3
                                    : DetectedType::BUFFER_AC3;
            return decodeElementaryStream(fileData.data(), fileData.size(), type);
        }
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

    // Raw AC-3 and E-AC-3 elementary streams lack a Media Foundation container ByteStreamHandler.
    // Decode them via the Windows MFT stream decoder.
    if (NativeAudioDecoder::isAc3(bytes, length)) {
        return decodeElementaryStream(bytes, length, DetectedType::BUFFER_AC3);
    }
    if (NativeAudioDecoder::isEac3(bytes, length)) {
        return decodeElementaryStream(bytes, length, DetectedType::BUFFER_EAC3);
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

    // Set byte stream attributes so Media Foundation can resolve MP4 / M4A / AAC container handlers
    IMFAttributes *pAttributes = nullptr;
    if (SUCCEEDED(pByteStream->QueryInterface(IID_PPV_ARGS(&pAttributes))) && pAttributes) {
        if (NativeAudioDecoder::isM4aOrMp4(bytes, length)) {
            pAttributes->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"audio/mp4");
            pAttributes->SetString(MF_BYTESTREAM_ORIGIN_NAME, L"audio.m4a");
        } else if (NativeAudioDecoder::isAacAdts(bytes, length)) {
            pAttributes->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"audio/aac");
            pAttributes->SetString(MF_BYTESTREAM_ORIGIN_NAME, L"audio.aac");
        }
        pAttributes->Release();
    }

    IMFSourceReader *pReader = nullptr;
    hr = MFCreateSourceReaderFromByteStream(pByteStream, nullptr, &pReader);
    pByteStream->Release();

    if (FAILED(hr) || !pReader) {
        // Fallback: write to temporary file with appropriate extension in %TEMP%
        wchar_t tempPath[MAX_PATH];
        wchar_t tempFile[MAX_PATH];
        if (GetTempPathW(MAX_PATH, tempPath) > 0 && GetTempFileNameW(tempPath, L"sld", 0, tempFile) > 0) {
            const wchar_t *ext = L".m4a";
            if (NativeAudioDecoder::isAacAdts(bytes, length)) ext = L".aac";
            std::wstring customTempFile = std::wstring(tempFile) + ext;
            MoveFileW(tempFile, customTempFile.c_str());

            FILE *fp = _wfopen(customTempFile.c_str(), L"wb");
            if (fp) {
                fwrite(bytes, 1, length, fp);
                fclose(fp);

                int utf8Len = WideCharToMultiByte(CP_UTF8, 0, customTempFile.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (utf8Len > 0) {
                    std::vector<char> utf8Path(utf8Len);
                    WideCharToMultiByte(CP_UTF8, 0, customTempFile.c_str(), -1, utf8Path.data(), utf8Len, nullptr, nullptr);
                    result = decodeFile(utf8Path.data());
                }
                DeleteFileW(customTempFile.c_str());
                return result;
            }
        }

        result.errorMessage = "MFCreateSourceReaderFromByteStream failed: " + std::to_string(hr);
        return result;
    }

    result = decodeFromReader(pReader);
    pReader->Release();
    return result;
}

#endif // defined(_WIN32) || defined(_WIN64)
