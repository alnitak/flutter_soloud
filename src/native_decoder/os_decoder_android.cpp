#if defined(__ANDROID__)

#include "native_audio_decoder.h"
#include "../audiobuffer/aac_stream_decoder.h"
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <vector>
#include <string>
#include <cstdio>

namespace {

DecodedAudioData decodeFromExtractor(AMediaExtractor *extractor) {
    DecodedAudioData result;
    if (!extractor) {
        result.errorMessage = "Invalid AMediaExtractor";
        return result;
    }

    size_t numTracks = AMediaExtractor_getTrackCount(extractor);
    int audioTrackIndex = -1;
    const char *mime = nullptr;
    int32_t sampleRate = 44100;
    int32_t channels = 2;
    AMediaFormat *audioFormat = nullptr;

    for (size_t i = 0; i < numTracks; i++) {
        AMediaFormat *format = AMediaExtractor_getTrackFormat(extractor, i);
        const char *trackMime = nullptr;
        if (AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &trackMime)) {
            if (std::strncmp(trackMime, "audio/", 6) == 0) {
                audioTrackIndex = static_cast<int>(i);
                mime = trackMime;
                audioFormat = format;
                AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
                AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
                break;
            }
        }
        AMediaFormat_delete(format);
    }

    if (audioTrackIndex < 0 || !mime) {
        result.errorMessage = "No audio track found in media";
        return result;
    }

    AMediaExtractor_selectTrack(extractor, audioTrackIndex);

    AMediaCodec *codec = AMediaCodec_createDecoderByType(mime);
    if (!codec) {
        AMediaFormat_delete(audioFormat);
        result.errorMessage = "Failed to create AMediaCodec for MIME: " + std::string(mime);
        return result;
    }

    media_status_t status = AMediaCodec_configure(codec, audioFormat, nullptr, nullptr, 0);
    AMediaFormat_delete(audioFormat);
    if (status != AMEDIA_OK) {
        AMediaCodec_delete(codec);
        result.errorMessage = "AMediaCodec_configure failed: " + std::to_string(status);
        return result;
    }

    status = AMediaCodec_start(codec);
    if (status != AMEDIA_OK) {
        AMediaCodec_delete(codec);
        result.errorMessage = "AMediaCodec_start failed: " + std::to_string(status);
        return result;
    }

    std::vector<float> interleavedBuffer;
    bool sawInputEOS = false;
    bool sawOutputEOS = false;
    const int64_t kTimeoutUs = 5000; // 5 ms timeout

    while (!sawOutputEOS) {
        if (!sawInputEOS) {
            // Queue all available input buffers without blocking to pipeline the codec
            while (!sawInputEOS) {
                ssize_t inIndex = AMediaCodec_dequeueInputBuffer(codec, 0);
                if (inIndex < 0) break;
                size_t inBufSize = 0;
                uint8_t *inBuf = AMediaCodec_getInputBuffer(codec, inIndex, &inBufSize);
                if (inBuf) {
                    ssize_t sampleSize = AMediaExtractor_readSampleData(extractor, inBuf, inBufSize);
                    if (sampleSize < 0) {
                        sampleSize = 0;
                        sawInputEOS = true;
                    }
                    int64_t presentationTimeUs = AMediaExtractor_getSampleTime(extractor);
                    uint32_t flags = sawInputEOS ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0;
                    AMediaCodec_queueInputBuffer(codec, inIndex, 0, sampleSize, presentationTimeUs, flags);
                    if (!sawInputEOS) {
                        AMediaExtractor_advance(extractor);
                    }
                } else {
                    break;
                }
            }
        }

        // Drain available output buffers
        AMediaCodecBufferInfo info;
        ssize_t outIndex = AMediaCodec_dequeueOutputBuffer(codec, &info, kTimeoutUs);
        while (outIndex >= 0 || outIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            if (outIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat *newFormat = AMediaCodec_getOutputFormat(codec);
                if (newFormat) {
                    AMediaFormat_getInt32(newFormat, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
                    AMediaFormat_getInt32(newFormat, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
                    AMediaFormat_delete(newFormat);
                }
            } else {
                if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                    sawOutputEOS = true;
                }

                if (info.size > 0) {
                    size_t outBufSize = 0;
                    uint8_t *outBuf = AMediaCodec_getOutputBuffer(codec, outIndex, &outBufSize);
                    if (outBuf) {
                        // AMediaCodec typically outputs 16-bit signed PCM for audio
                        const int16_t *pcm16 = reinterpret_cast<const int16_t *>(outBuf + info.offset);
                        size_t sampleCount = info.size / sizeof(int16_t);
                        size_t startOffset = interleavedBuffer.size();
                        interleavedBuffer.resize(startOffset + sampleCount);
                        for (size_t s = 0; s < sampleCount; ++s) {
                            interleavedBuffer[startOffset + s] = pcm16[s] / 32768.0f;
                        }
                    }
                }
                AMediaCodec_releaseOutputBuffer(codec, outIndex, false);
                if (sawOutputEOS) break;
            }
            outIndex = AMediaCodec_dequeueOutputBuffer(codec, &info, 0);
        }
    }

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);

    if (channels <= 0) channels = 2;
    size_t totalFrames = interleavedBuffer.size() / channels;
    if (totalFrames == 0) {
        result.errorMessage = "No audio frames decoded from stream";
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
    result.channels = static_cast<unsigned int>(channels);
    result.sampleRate = static_cast<float>(sampleRate);
    result.success = true;
    return result;
}

static size_t skipId3Header(const unsigned char *data, size_t size) {
    if (data && size >= 10 && std::memcmp(data, "ID3", 3) == 0) {
        uint32_t tagSize = ((data[6] & 0x7F) << 21) |
                           ((data[7] & 0x7F) << 14) |
                           ((data[8] & 0x7F) << 7) |
                           (data[9] & 0x7F);
        size_t total = 10 + tagSize;
        if ((data[5] & 0x10) != 0) total += 10; // footer
        return total <= size ? total : size;
    }
    return 0;
}

DecodedAudioData decodeElementaryStream(const unsigned char *bytes, size_t length, DetectedType type) {
    DecodedAudioData result;
    AACDecoderWrapper wrapper(type);
    if (!wrapper.initializeDecoder(0, 0)) {
        result.errorMessage = "Failed to initialize elementary stream decoder on Android for format " +
                              std::to_string(static_cast<int>(type));
        return result;
    }

    std::vector<unsigned char> buffer(bytes, bytes + length);
    int sampleRate = 0;
    int channels = 0;

    std::vector<float> allSamples;
    wrapper.setDataEnded();
    auto [samples, err] = wrapper.decode(buffer, &sampleRate, &channels, 0);
    if (!samples.empty()) {
        allSamples.insert(allSamples.end(), samples.begin(), samples.end());
    }

    int maxPasses = 50;
    while (wrapper.hasPendingData() && --maxPasses > 0) {
        std::vector<unsigned char> emptyBuf;
        auto [samplesMore, errMore] = wrapper.decode(emptyBuf, &sampleRate, &channels, 0);
        if (!samplesMore.empty()) {
            allSamples.insert(allSamples.end(), samplesMore.begin(), samplesMore.end());
        }
    }

    if (channels <= 0) channels = 2;
    if (sampleRate <= 0) sampleRate = 44100;

    size_t totalFrames = allSamples.size() / channels;
    if (totalFrames == 0) {
        result.errorMessage = "No audio frames decoded from elementary stream on Android";
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

    // Check if the file is a raw elementary stream (AC-3, E-AC-3, or AAC ADTS)
    FILE *fp = std::fopen(filePath, "rb");
    if (fp) {
        unsigned char header[128];
        size_t bytesRead = std::fread(header, 1, sizeof(header), fp);
        if (bytesRead >= 6) {
            int syncIdx = NativeAudioDecoder::findAc3Syncword(header, bytesRead);
            if (syncIdx >= 0 && static_cast<size_t>(syncIdx) + 6 <= bytesRead) {
                DetectedType type = NativeAudioDecoder::isEac3(header + syncIdx, bytesRead - syncIdx)
                                        ? DetectedType::BUFFER_EAC3
                                        : DetectedType::BUFFER_AC3;
                std::fseek(fp, 0, SEEK_END);
                long fileSize = std::ftell(fp);
                std::fseek(fp, 0, SEEK_SET);
                if (fileSize > 0) {
                    std::vector<unsigned char> fileData(static_cast<size_t>(fileSize));
                    size_t readCount = std::fread(fileData.data(), 1, fileData.size(), fp);
                    std::fclose(fp);
                    return decodeElementaryStream(fileData.data(), readCount, type);
                }
            } else {
                size_t id3Skip = skipId3Header(header, bytesRead);
                if (NativeAudioDecoder::isAacAdts(header + id3Skip, bytesRead - id3Skip)) {
                    std::fseek(fp, 0, SEEK_END);
                    long fileSize = std::ftell(fp);
                    std::fseek(fp, 0, SEEK_SET);
                    if (fileSize > 0) {
                        std::vector<unsigned char> fileData(static_cast<size_t>(fileSize));
                        size_t readCount = std::fread(fileData.data(), 1, fileData.size(), fp);
                        std::fclose(fp);
                        return decodeElementaryStream(fileData.data(), readCount, DetectedType::BUFFER_AAC);
                    }
                }
            }
        }
        std::fclose(fp);
    }

    int fd = open(filePath, O_RDONLY);
    if (fd < 0) {
        result.errorMessage = "Cannot open file: " + std::string(filePath);
        return result;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        result.errorMessage = "Cannot stat file: " + std::string(filePath);
        return result;
    }

    AMediaExtractor *extractor = AMediaExtractor_new();
    if (!extractor) {
        close(fd);
        result.errorMessage = "Cannot create AMediaExtractor";
        return result;
    }

    media_status_t status = AMediaExtractor_setDataSourceFd(extractor, fd, 0, st.st_size);
    if (status != AMEDIA_OK) {
        AMediaExtractor_delete(extractor);
        close(fd);
        result.errorMessage = "AMediaExtractor_setDataSourceFd failed: " + std::to_string(status);
        return result;
    }

    result = decodeFromExtractor(extractor);

    AMediaExtractor_delete(extractor);
    close(fd);
    return result;
}

DecodedAudioData NativeAudioDecoder::decodeMemory(const unsigned char *bytes, size_t length) {
    DecodedAudioData result;
    if (!bytes || length == 0) {
        result.errorMessage = "Empty memory buffer";
        return result;
    }

    // Check if the buffer is a raw AC-3 or E-AC-3 elementary stream
    int syncIdx = NativeAudioDecoder::findAc3Syncword(bytes, length);
    if (syncIdx >= 0 && static_cast<size_t>(syncIdx) + 6 <= length) {
        const unsigned char *h = bytes + syncIdx;
        size_t rem = length - syncIdx;
        if (NativeAudioDecoder::isEac3(h, rem)) {
            return decodeElementaryStream(bytes, length, DetectedType::BUFFER_EAC3);
        } else if (NativeAudioDecoder::isAc3(h, rem)) {
            return decodeElementaryStream(bytes, length, DetectedType::BUFFER_AC3);
        }
    }

    // Check if the buffer is a raw AAC ADTS elementary stream (possibly with ID3)
    size_t id3Skip = skipId3Header(bytes, length);
    if (NativeAudioDecoder::isAacAdts(bytes + id3Skip, length - id3Skip)) {
        return decodeElementaryStream(bytes, length, DetectedType::BUFFER_AAC);
    }

    // Write to a temporary anonymous file for AMediaExtractor_setDataSourceFd (for M4A, MP4, etc.)
    FILE *tmpFile = std::tmpfile();
    if (!tmpFile) {
        result.errorMessage = "Failed to create temporary file for in-memory extraction";
        return result;
    }

    size_t written = std::fwrite(bytes, 1, length, tmpFile);
    std::fflush(tmpFile);
    if (written != length) {
        std::fclose(tmpFile);
        result.errorMessage = "Failed to write buffer to temporary file";
        return result;
    }

    int fd = fileno(tmpFile);
    if (fd < 0) {
        std::fclose(tmpFile);
        result.errorMessage = "Failed to get file descriptor from temporary file";
        return result;
    }

    AMediaExtractor *extractor = AMediaExtractor_new();
    if (!extractor) {
        std::fclose(tmpFile);
        result.errorMessage = "Cannot create AMediaExtractor";
        return result;
    }

    media_status_t status = AMediaExtractor_setDataSourceFd(extractor, fd, 0, length);
    if (status != AMEDIA_OK) {
        AMediaExtractor_delete(extractor);
        std::fclose(tmpFile);
        result.errorMessage = "AMediaExtractor_setDataSourceFd failed: " + std::to_string(status);
        return result;
    }

    result = decodeFromExtractor(extractor);

    AMediaExtractor_delete(extractor);
    std::fclose(tmpFile);
    return result;
}

#endif // defined(__ANDROID__)
