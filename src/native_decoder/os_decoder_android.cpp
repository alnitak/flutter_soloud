#if defined(__ANDROID__)

#include "native_audio_decoder.h"
#include "../audiobuffer/aac_stream_decoder.h"
#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <new>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#define LOG_TAG "SoLoudNativeDecoder"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

// AMEDIAFORMAT_KEY_PCM_ENCODING is only declared from API 28 (minSdk is 21).
// Values are android.media.AudioFormat ENCODING_* constants.
constexpr const char *kKeyPcmEncoding = "pcm-encoding";
constexpr int32_t kPcmEncoding16Bit = 2;
constexpr int32_t kPcmEncodingFloat = 4;

// Mirrors SoLoud's MAX_CHANNELS.
constexpr int32_t kMaxChannels = 8;
constexpr int32_t kMaxSampleRate = 768000;

// Upper bound on the decoded planar float PCM. Protects against malformed or
// unexpectedly long media exhausting memory (2 GiB ~ 1.5h of 48 kHz stereo).
constexpr size_t kMaxDecodedBytes =
    sizeof(void *) >= 8 ? (static_cast<size_t>(2) << 30) : (static_cast<size_t>(512) << 20);

constexpr int64_t kDequeueTimeoutUs = 5000;
// Abort when the codec neither accepts input nor produces PCM for this long.
constexpr auto kStallTimeout = std::chrono::seconds(10);
// Bound on consecutive output dequeues per loop pass so the stall check runs
// even if a codec keeps reporting format/buffer changes.
constexpr int kMaxOutputDequeuesPerPass = 64;
// Input ending this far before the container duration means truncated media.
constexpr int64_t kTruncationSlackUs = 50000;
// Decoded audio beyond the container duration is trimmed only up to this many
// frames (AAC end padding the extractor did not report as encoder-padding).
constexpr size_t kMaxTrailingPaddingFrames = 4096;

struct ExtractorDeleter {
    void operator()(AMediaExtractor *e) const { AMediaExtractor_delete(e); }
};
struct FormatDeleter {
    void operator()(AMediaFormat *f) const { AMediaFormat_delete(f); }
};
using ExtractorPtr = std::unique_ptr<AMediaExtractor, ExtractorDeleter>;
using FormatPtr = std::unique_ptr<AMediaFormat, FormatDeleter>;

/// Owns an AMediaCodec, stopping it (if started) and deleting it on scope exit.
class CodecHandle {
public:
    explicit CodecHandle(AMediaCodec *codec) : mCodec(codec) {}
    ~CodecHandle() {
        if (!mCodec) return;
        if (mStarted) AMediaCodec_stop(mCodec);
        AMediaCodec_delete(mCodec);
    }
    CodecHandle(const CodecHandle &) = delete;
    CodecHandle &operator=(const CodecHandle &) = delete;

    AMediaCodec *get() const { return mCodec; }
    void setStarted() { mStarted = true; }

private:
    AMediaCodec *mCodec;
    bool mStarted = false;
};

class UniqueFd {
public:
    explicit UniqueFd(int fd = -1) : mFd(fd) {}
    ~UniqueFd() {
        if (mFd >= 0) close(mFd);
    }
    UniqueFd(const UniqueFd &) = delete;
    UniqueFd &operator=(const UniqueFd &) = delete;

    int get() const { return mFd; }

private:
    int mFd;
};

DecodedAudioData failure(const std::string &message) {
    LOGE("%s", message.c_str());
    DecodedAudioData result;
    result.errorMessage = message;
    return result;
}

/// Accumulates decoded audio directly into SoLoud's planar layout
/// ([ch0 frames][ch1 frames]...), avoiding a full interleaved intermediate copy.
/// Capacity is per channel; channel c lives at mData[c * mCapacity].
class PlanarPcmBuffer {
public:
    ~PlanarPcmBuffer() { delete[] mData; }

    bool init(unsigned int channels, size_t frameHint, std::string &error) {
        delete[] mData;
        mData = nullptr;
        mCapacity = 0;
        mFrames = 0;
        mChannels = channels;
        mMaxFrames = kMaxDecodedBytes / (static_cast<size_t>(channels) * sizeof(float));
        if (mMaxFrames > UINT_MAX) mMaxFrames = UINT_MAX;
        if (frameHint > mMaxFrames) frameHint = mMaxFrames;
        if (frameHint > 0 && reserve(frameHint)) return true;
        // The hint comes from container metadata and may be bogus; start small.
        return grow(1, error);
    }

    size_t frames() const { return mFrames; }

    void truncate(size_t frames) {
        if (frames < mFrames) mFrames = frames;
    }

    bool appendPcm16(const uint8_t *src, size_t frameCount, std::string &error) {
        if (!grow(frameCount, error)) return false;
        for (size_t i = 0; i < frameCount; ++i) {
            for (unsigned int c = 0; c < mChannels; ++c) {
                int16_t s;
                std::memcpy(&s, src + (i * mChannels + c) * sizeof(int16_t), sizeof(s));
                mData[c * mCapacity + mFrames + i] = s / 32768.0f;
            }
        }
        mFrames += frameCount;
        return true;
    }

    bool appendFloat(const uint8_t *src, size_t frameCount, std::string &error) {
        if (!grow(frameCount, error)) return false;
        for (size_t i = 0; i < frameCount; ++i) {
            for (unsigned int c = 0; c < mChannels; ++c) {
                std::memcpy(&mData[c * mCapacity + mFrames + i],
                            src + (i * mChannels + c) * sizeof(float), sizeof(float));
            }
        }
        mFrames += frameCount;
        return true;
    }

    /// Hands over a planar buffer of exactly frames() * channels samples
    /// (allocated with new[]), or nullptr when empty.
    float *release() {
        if (mFrames == 0) return nullptr;
        if (mFrames < mCapacity) {
            const size_t slack = mCapacity - mFrames;
            float *exact = nullptr;
            if (slack > mFrames / 8) {
                exact = new (std::nothrow) float[mFrames * mChannels];
            }
            if (exact) {
                for (unsigned int c = 0; c < mChannels; ++c) {
                    std::memcpy(exact + c * mFrames, mData + c * mCapacity, mFrames * sizeof(float));
                }
                delete[] mData;
                mData = exact;
            } else {
                // Compact in place: each channel moves to a lower address that
                // never overlaps the not-yet-moved channels above it.
                for (unsigned int c = 1; c < mChannels; ++c) {
                    std::memmove(mData + c * mFrames, mData + c * mCapacity, mFrames * sizeof(float));
                }
            }
        }
        float *out = mData;
        mData = nullptr;
        mCapacity = 0;
        mFrames = 0;
        return out;
    }

private:
    bool reserve(size_t newCapacity) {
        // newCapacity <= mMaxFrames, so newCapacity * mChannels cannot overflow.
        float *next = new (std::nothrow) float[newCapacity * mChannels];
        if (!next) return false;
        if (mData) {
            for (unsigned int c = 0; c < mChannels; ++c) {
                std::memcpy(next + c * newCapacity, mData + c * mCapacity, mFrames * sizeof(float));
            }
            delete[] mData;
        }
        mData = next;
        mCapacity = newCapacity;
        return true;
    }

    bool grow(size_t extraFrames, std::string &error) {
        if (extraFrames > mMaxFrames - mFrames) {
            error = "Decoded audio exceeds the maximum supported size";
            return false;
        }
        const size_t needed = mFrames + extraFrames;
        if (needed <= mCapacity) return true;
        size_t target = mCapacity + mCapacity / 2;
        if (target < needed) target = needed;
        if (target < 4096) target = 4096;
        if (target > mMaxFrames) target = mMaxFrames;
        if (reserve(target) || (target > needed && reserve(needed))) return true;
        error = "Out of memory while decoding audio";
        return false;
    }

    float *mData = nullptr;
    size_t mCapacity = 0;
    size_t mFrames = 0;
    size_t mMaxFrames = 0;
    unsigned int mChannels = 0;
};

DecodedAudioData decodeFromExtractor(AMediaExtractor *extractor) {
    if (!extractor) return failure("Invalid AMediaExtractor");

    const size_t numTracks = AMediaExtractor_getTrackCount(extractor);
    int audioTrackIndex = -1;
    std::string mime;
    FormatPtr audioFormat;
    for (size_t i = 0; i < numTracks; i++) {
        FormatPtr format(AMediaExtractor_getTrackFormat(extractor, i));
        const char *trackMime = nullptr;
        if (format && AMediaFormat_getString(format.get(), AMEDIAFORMAT_KEY_MIME, &trackMime) &&
            trackMime && std::strncmp(trackMime, "audio/", 6) == 0) {
            audioTrackIndex = static_cast<int>(i);
            mime = trackMime;
            audioFormat = std::move(format);
            break;
        }
    }
    if (audioTrackIndex < 0) return failure("No audio track found in media");

    int64_t durationUs = 0;
    if (!AMediaFormat_getInt64(audioFormat.get(), AMEDIAFORMAT_KEY_DURATION, &durationUs) || durationUs < 0) {
        durationUs = 0;
    }

    if (AMediaExtractor_selectTrack(extractor, audioTrackIndex) != AMEDIA_OK) {
        return failure("AMediaExtractor_selectTrack failed");
    }

    CodecHandle codec(AMediaCodec_createDecoderByType(mime.c_str()));
    if (!codec.get()) return failure("Failed to create AMediaCodec for MIME: " + mime);

    media_status_t status = AMediaCodec_configure(codec.get(), audioFormat.get(), nullptr, nullptr, 0);
    if (status != AMEDIA_OK) {
        return failure("AMediaCodec_configure failed for " + mime + ": " + std::to_string(status));
    }
    status = AMediaCodec_start(codec.get());
    if (status != AMEDIA_OK) return failure("AMediaCodec_start failed: " + std::to_string(status));
    codec.setStarted();

    PlanarPcmBuffer pcm;
    std::string error;
    bool outputConfigured = false;
    int32_t channels = 0;
    int32_t sampleRate = 0;
    int32_t encoding = kPcmEncoding16Bit;

    // Reads the codec's negotiated output format. Rejects unsupported layouts
    // and channel/rate changes once PCM has been accumulated.
    auto applyOutputFormat = [&]() -> bool {
        FormatPtr fmt(AMediaCodec_getOutputFormat(codec.get()));
        if (!fmt) {
            error = "AMediaCodec_getOutputFormat returned null";
            return false;
        }
        int32_t newChannels = 0;
        int32_t newRate = 0;
        int32_t newEncoding = kPcmEncoding16Bit; // MediaCodec default when the key is absent
        AMediaFormat_getInt32(fmt.get(), AMEDIAFORMAT_KEY_CHANNEL_COUNT, &newChannels);
        AMediaFormat_getInt32(fmt.get(), AMEDIAFORMAT_KEY_SAMPLE_RATE, &newRate);
        AMediaFormat_getInt32(fmt.get(), kKeyPcmEncoding, &newEncoding);

        if (newChannels < 1 || newChannels > kMaxChannels) {
            error = "Unsupported decoder output channel count: " + std::to_string(newChannels);
            return false;
        }
        if (newRate < 1 || newRate > kMaxSampleRate) {
            error = "Unsupported decoder output sample rate: " + std::to_string(newRate);
            return false;
        }
        if (newEncoding != kPcmEncoding16Bit && newEncoding != kPcmEncodingFloat) {
            error = "Unsupported decoder output PCM encoding: " + std::to_string(newEncoding);
            return false;
        }
        if (outputConfigured && pcm.frames() > 0 && (newChannels != channels || newRate != sampleRate)) {
            error = "Decoder output format changed mid-stream (" + std::to_string(channels) + "ch/" +
                    std::to_string(sampleRate) + "Hz -> " + std::to_string(newChannels) + "ch/" +
                    std::to_string(newRate) + "Hz)";
            return false;
        }
        if (!outputConfigured || newChannels != channels) {
            // Pre-size from the container duration at the actual output rate
            // (HE-AAC/SBR may double the rate reported by the extractor).
            const double hint = static_cast<double>(durationUs) * newRate / 1e6 + 8192;
            const size_t frameHint = durationUs > 0 && hint < static_cast<double>(kMaxDecodedBytes)
                ? static_cast<size_t>(hint)
                : 0;
            if (!pcm.init(static_cast<unsigned int>(newChannels), frameHint, error)) return false;
        }
        channels = newChannels;
        sampleRate = newRate;
        encoding = newEncoding;
        outputConfigured = true;
        return true;
    };

    bool sawInputEOS = false;
    bool sawOutputEOS = false;
    int64_t lastSampleTimeUs = -1;
    int64_t maxSampleDeltaUs = 0;
    auto lastProgress = std::chrono::steady_clock::now();

    while (!sawOutputEOS) {
        bool progressed = false;

        // Queue all available input buffers without blocking to pipeline the codec.
        while (!sawInputEOS) {
            ssize_t inIndex = AMediaCodec_dequeueInputBuffer(codec.get(), 0);
            if (inIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER) break;
            if (inIndex < 0) {
                return failure("AMediaCodec_dequeueInputBuffer failed: " + std::to_string(inIndex));
            }
            size_t inBufSize = 0;
            uint8_t *inBuf = AMediaCodec_getInputBuffer(codec.get(), static_cast<size_t>(inIndex), &inBufSize);
            if (!inBuf) return failure("AMediaCodec_getInputBuffer returned null");

            ssize_t sampleSize = AMediaExtractor_readSampleData(extractor, inBuf, inBufSize);
            int64_t presentationTimeUs = 0;
            uint32_t flags = 0;
            if (sampleSize < 0) {
                // A pending sample that cannot be read means truncated media or
                // an oversized sample, not a clean end of stream.
                if (AMediaExtractor_getSampleTrackIndex(extractor) >= 0) {
                    return failure("Failed to read compressed sample (truncated or corrupt media)");
                }
                // The extractor also reports a clean end when the sample table
                // points past the end of a truncated file.
                if (durationUs > 0 && lastSampleTimeUs >= 0 &&
                    lastSampleTimeUs + 2 * maxSampleDeltaUs + kTruncationSlackUs < durationUs) {
                    return failure("Media is truncated: compressed audio ends at " +
                                   std::to_string(lastSampleTimeUs / 1000) + " ms of " +
                                   std::to_string(durationUs / 1000) + " ms");
                }
                sampleSize = 0;
                sawInputEOS = true;
                flags = AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
            } else {
                presentationTimeUs = AMediaExtractor_getSampleTime(extractor);
                if (presentationTimeUs < 0) presentationTimeUs = 0;
                if (lastSampleTimeUs >= 0 && presentationTimeUs - lastSampleTimeUs > maxSampleDeltaUs) {
                    maxSampleDeltaUs = presentationTimeUs - lastSampleTimeUs;
                }
                if (presentationTimeUs > lastSampleTimeUs) lastSampleTimeUs = presentationTimeUs;
            }
            status = AMediaCodec_queueInputBuffer(codec.get(), static_cast<size_t>(inIndex), 0,
                                                  static_cast<size_t>(sampleSize), presentationTimeUs, flags);
            if (status != AMEDIA_OK) {
                return failure("AMediaCodec_queueInputBuffer failed: " + std::to_string(status));
            }
            if (!sawInputEOS) AMediaExtractor_advance(extractor);
            progressed = true;
        }

        // Drain available output buffers.
        int64_t timeoutUs = kDequeueTimeoutUs;
        for (int pass = 0; pass < kMaxOutputDequeuesPerPass && !sawOutputEOS; ++pass) {
            AMediaCodecBufferInfo info{};
            ssize_t outIndex = AMediaCodec_dequeueOutputBuffer(codec.get(), &info, timeoutUs);
            timeoutUs = 0;
            if (outIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER) break;
            if (outIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                if (!applyOutputFormat()) return failure(error);
                continue;
            }
            if (outIndex == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
            if (outIndex < 0) {
                return failure("AMediaCodec_dequeueOutputBuffer failed: " + std::to_string(outIndex));
            }

            const size_t index = static_cast<size_t>(outIndex);
            if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                sawOutputEOS = true;
                progressed = true;
            }

            if (info.size > 0) {
                if (!outputConfigured && !applyOutputFormat()) {
                    AMediaCodec_releaseOutputBuffer(codec.get(), index, false);
                    return failure(error);
                }
                size_t outBufSize = 0;
                uint8_t *outBuf = AMediaCodec_getOutputBuffer(codec.get(), index, &outBufSize);
                const size_t bytesPerFrame = static_cast<size_t>(channels) *
                    (encoding == kPcmEncodingFloat ? sizeof(float) : sizeof(int16_t));
                if (!outBuf) {
                    error = "AMediaCodec_getOutputBuffer returned null";
                } else if (info.offset < 0 || static_cast<size_t>(info.offset) > outBufSize ||
                           static_cast<size_t>(info.size) > outBufSize - static_cast<size_t>(info.offset)) {
                    error = "Decoder output buffer bounds are invalid (offset " + std::to_string(info.offset) +
                            ", size " + std::to_string(info.size) + ", capacity " + std::to_string(outBufSize) + ")";
                } else if (static_cast<size_t>(info.size) % bytesPerFrame != 0) {
                    error = "Decoder output contains a partial PCM frame";
                } else {
                    const uint8_t *src = outBuf + info.offset;
                    const size_t frameCount = static_cast<size_t>(info.size) / bytesPerFrame;
                    if (encoding == kPcmEncodingFloat) {
                        pcm.appendFloat(src, frameCount, error);
                    } else {
                        pcm.appendPcm16(src, frameCount, error);
                    }
                }
                if (!error.empty()) {
                    AMediaCodec_releaseOutputBuffer(codec.get(), index, false);
                    return failure(error);
                }
                progressed = true;
            }
            AMediaCodec_releaseOutputBuffer(codec.get(), index, false);
        }

        const auto now = std::chrono::steady_clock::now();
        if (progressed) {
            lastProgress = now;
        } else if (now - lastProgress > kStallTimeout) {
            return failure("Decoder stalled: no progress for " +
                           std::to_string(std::chrono::duration_cast<std::chrono::seconds>(kStallTimeout).count()) +
                           "s (" + std::to_string(pcm.frames()) + " frames decoded)");
        }
    }

    if (!outputConfigured || pcm.frames() == 0) return failure("No audio frames decoded from stream");

    // Priming is trimmed by the codec (encoder-delay), but end padding is only
    // trimmed when the container reports encoder-padding (iTunSMPB). Fall back
    // to the container duration (e.g. an MP4 edit list) for small excesses.
    if (durationUs > 0) {
        const double expected = static_cast<double>(durationUs) * sampleRate / 1e6;
        const size_t expectedFrames = static_cast<size_t>(expected + 0.5);
        if (expectedFrames > 0 && pcm.frames() > expectedFrames &&
            pcm.frames() - expectedFrames <= kMaxTrailingPaddingFrames) {
            pcm.truncate(expectedFrames);
        }
    }

    DecodedAudioData result;
    result.sampleCount = pcm.frames();
    result.samples = pcm.release();
    result.channels = static_cast<unsigned int>(channels);
    result.sampleRate = static_cast<float>(sampleRate);
    result.success = true;
    return result;
}

DecodedAudioData decodeFromFd(int fd, off64_t length) {
    ExtractorPtr extractor(AMediaExtractor_new());
    if (!extractor) return failure("Cannot create AMediaExtractor");

    media_status_t status = AMediaExtractor_setDataSourceFd(extractor.get(), fd, 0, length);
    if (status != AMEDIA_OK) {
        return failure("AMediaExtractor_setDataSourceFd failed (unsupported or corrupt media): " +
                       std::to_string(status));
    }
    return decodeFromExtractor(extractor.get());
}

size_t skipId3Header(const unsigned char *data, size_t size) {
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

DecodedAudioData decodeElementaryStream(std::vector<unsigned char> buffer, DetectedType type) {
    AACDecoderWrapper wrapper(type);
    if (!wrapper.initializeDecoder(0, 0)) {
        return failure("Failed to initialize elementary stream decoder on Android for format " +
                       std::to_string(static_cast<int>(type)));
    }

    int sampleRate = 0;
    int channels = 0;
    PlanarPcmBuffer pcm;
    std::string error;
    bool pcmInitialized = false;
    int pcmChannels = 0;

    auto appendSamples = [&](const std::vector<float> &samples) -> bool {
        if (samples.empty()) return true;
        if (channels < 1 || channels > kMaxChannels) {
            error = "Unsupported elementary stream channel count: " + std::to_string(channels);
            return false;
        }
        if (!pcmInitialized) {
            if (!pcm.init(static_cast<unsigned int>(channels), 0, error)) return false;
            pcmInitialized = true;
            pcmChannels = channels;
        } else if (channels != pcmChannels) {
            error = "Elementary stream channel count changed mid-stream";
            return false;
        }
        const size_t frameCount = samples.size() / static_cast<size_t>(channels);
        return pcm.appendFloat(reinterpret_cast<const uint8_t *>(samples.data()), frameCount, error);
    };

    wrapper.setDataEnded();
    auto [samples, err] = wrapper.decode(buffer, &sampleRate, &channels, 0);
    if (!appendSamples(samples)) return failure(error);

    int maxPasses = 50;
    while (wrapper.hasPendingData() && --maxPasses > 0) {
        std::vector<unsigned char> emptyBuf;
        auto [samplesMore, errMore] = wrapper.decode(emptyBuf, &sampleRate, &channels, 0);
        if (!appendSamples(samplesMore)) return failure(error);
    }

    if (sampleRate <= 0 || sampleRate > kMaxSampleRate) sampleRate = 44100;
    if (pcm.frames() == 0) return failure("No audio frames decoded from elementary stream on Android");

    DecodedAudioData result;
    result.sampleCount = pcm.frames();
    result.samples = pcm.release();
    result.channels = static_cast<unsigned int>(channels);
    result.sampleRate = static_cast<float>(sampleRate);
    result.success = true;
    return result;
}

/// Reads the whole file into memory (for elementary streams, which are decoded
/// from a buffer rather than via AMediaExtractor).
bool readWholeFile(int fd, off64_t size, std::vector<unsigned char> &out) {
    if (size <= 0 || static_cast<uint64_t>(size) > SIZE_MAX) return false;
    out.resize(static_cast<size_t>(size));
    size_t total = 0;
    while (total < out.size()) {
        ssize_t n = pread64(fd, out.data() + total, out.size() - total, static_cast<off64_t>(total));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

/// Creates an unlinked temporary file holding `bytes` so AMediaExtractor can
/// read in-memory container data through a file descriptor. Prefers $TMPDIR
/// (the app cache dir on Android) and falls back to tmpfile().
int createTempFileWithBytes(const unsigned char *bytes, size_t length, std::string &error) {
    int fd = -1;
    const char *tmpDir = std::getenv("TMPDIR");
    if (tmpDir && *tmpDir) {
        std::string path = std::string(tmpDir) + "/soloud_decode_XXXXXX";
        fd = mkstemp(&path[0]);
        if (fd >= 0) unlink(path.c_str());
    }
    if (fd < 0) {
        FILE *fp = std::tmpfile();
        if (fp) {
            fd = dup(fileno(fp));
            std::fclose(fp);
        }
    }
    if (fd < 0) {
        error = "Failed to create temporary file for in-memory extraction (errno " + std::to_string(errno) + ")";
        return -1;
    }

    size_t written = 0;
    while (written < length) {
        ssize_t n = write(fd, bytes + written, length - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            error = "Failed to write buffer to temporary file (errno " + std::to_string(errno) + ")";
            close(fd);
            return -1;
        }
        written += static_cast<size_t>(n);
    }
    return fd;
}

template <typename Fn>
DecodedAudioData guardAllocations(Fn &&fn) {
    try {
        return fn();
    } catch (const std::bad_alloc &) {
        return failure("Out of memory while decoding audio");
    } catch (const std::exception &e) {
        return failure(std::string("Native decoder error: ") + e.what());
    }
}

} // anonymous namespace

DecodedAudioData NativeAudioDecoder::decodeFile(const char *filePath) {
    if (!filePath || std::strlen(filePath) == 0) return failure("Empty file path");

    return guardAllocations([&]() -> DecodedAudioData {
        UniqueFd fd(open(filePath, O_RDONLY | O_CLOEXEC));
        if (fd.get() < 0) return failure("Cannot open file: " + std::string(filePath));

        struct stat st;
        if (fstat(fd.get(), &st) != 0) return failure("Cannot stat file: " + std::string(filePath));
        if (st.st_size <= 0) return failure("Empty file: " + std::string(filePath));

        // Raw elementary streams (AC-3, E-AC-3, AAC ADTS) are not handled by
        // AMediaExtractor and are decoded from memory instead.
        unsigned char header[128];
        ssize_t headerRead = pread64(fd.get(), header, sizeof(header), 0);
        const size_t bytesRead = headerRead > 0 ? static_cast<size_t>(headerRead) : 0;
        if (bytesRead >= 6 && !NativeAudioDecoder::isM4aOrMp4(header, bytesRead)) {
            DetectedType type = DetectedType::BUFFER_UNKNOWN;
            int syncIdx = NativeAudioDecoder::findAc3Syncword(header, bytesRead);
            if (syncIdx >= 0 && static_cast<size_t>(syncIdx) + 6 <= bytesRead) {
                type = NativeAudioDecoder::isEac3(header + syncIdx, bytesRead - syncIdx)
                           ? DetectedType::BUFFER_EAC3
                           : DetectedType::BUFFER_AC3;
            } else {
                size_t id3Skip = skipId3Header(header, bytesRead);
                if (NativeAudioDecoder::isAacAdts(header + id3Skip, bytesRead - id3Skip)) {
                    type = DetectedType::BUFFER_AAC;
                }
            }
            if (type != DetectedType::BUFFER_UNKNOWN) {
                std::vector<unsigned char> fileData;
                if (!readWholeFile(fd.get(), st.st_size, fileData)) {
                    return failure("Cannot read file: " + std::string(filePath));
                }
                return decodeElementaryStream(std::move(fileData), type);
            }
        }

        return decodeFromFd(fd.get(), st.st_size);
    });
}

DecodedAudioData NativeAudioDecoder::decodeMemory(const unsigned char *bytes, size_t length) {
    if (!bytes || length == 0) return failure("Empty memory buffer");

    return guardAllocations([&]() -> DecodedAudioData {
        if (!NativeAudioDecoder::isM4aOrMp4(bytes, length)) {
            // Check if the buffer is a raw AC-3 or E-AC-3 elementary stream
            int syncIdx = NativeAudioDecoder::findAc3Syncword(bytes, length);
            if (syncIdx >= 0 && static_cast<size_t>(syncIdx) + 6 <= length) {
                const unsigned char *h = bytes + syncIdx;
                size_t rem = length - syncIdx;
                if (NativeAudioDecoder::isEac3(h, rem)) {
                    return decodeElementaryStream(std::vector<unsigned char>(bytes, bytes + length),
                                                  DetectedType::BUFFER_EAC3);
                } else if (NativeAudioDecoder::isAc3(h, rem)) {
                    return decodeElementaryStream(std::vector<unsigned char>(bytes, bytes + length),
                                                  DetectedType::BUFFER_AC3);
                }
            }

            // Check if the buffer is a raw AAC ADTS elementary stream (possibly with ID3)
            size_t id3Skip = skipId3Header(bytes, length);
            if (NativeAudioDecoder::isAacAdts(bytes + id3Skip, length - id3Skip)) {
                return decodeElementaryStream(std::vector<unsigned char>(bytes, bytes + length),
                                              DetectedType::BUFFER_AAC);
            }
        }

        // AMediaExtractor_setDataSourceCustom needs API 28, so containers (M4A,
        // MP4, ...) are read back through an unlinked temporary file.
        std::string error;
        UniqueFd fd(createTempFileWithBytes(bytes, length, error));
        if (fd.get() < 0) return failure(error);
        return decodeFromFd(fd.get(), static_cast<off64_t>(length));
    });
}

#endif // defined(__ANDROID__)
