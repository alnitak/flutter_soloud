#if defined(__ANDROID__)

#include "native_audio_decoder.h"
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <vector>
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
            ssize_t inIndex = AMediaCodec_dequeueInputBuffer(codec, kTimeoutUs);
            if (inIndex >= 0) {
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
                }
            }
        }

        AMediaCodecBufferInfo info;
        ssize_t outIndex = AMediaCodec_dequeueOutputBuffer(codec, &info, kTimeoutUs);
        if (outIndex >= 0) {
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
        } else if (outIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *newFormat = AMediaCodec_getOutputFormat(codec);
            if (newFormat) {
                AMediaFormat_getInt32(newFormat, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
                AMediaFormat_getInt32(newFormat, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
                AMediaFormat_delete(newFormat);
            }
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

} // anonymous namespace

DecodedAudioData NativeAudioDecoder::decodeFile(const char *filePath) {
    DecodedAudioData result;
    if (!filePath || std::strlen(filePath) == 0) {
        result.errorMessage = "Empty file path";
        return result;
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

    // Write to a temporary anonymous file for AMediaExtractor_setDataSourceFd
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
