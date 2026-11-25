// Streaming M4A/MP4 decoding on Android through AMediaExtractor and
// AMediaCodec.
//
// The codec is configured with encoder-delay/padding cleared, so it outputs
// every decoded frame and does no trimming of its own (whether and how it
// trims varies across Android releases). Each output buffer is placed on the
// timeline from its presentation time instead, relative to the first sample,
// which also covers extractors that shift timestamps by the edit list
// (timestamps a decoder clamped to 0 are placed by the next real one). The
// encoder delay and the playable length come from the container itself
// (m4a_gapless), so priming and padding are dropped here and positions match
// the Apple backend and the full decode.

#include "native_stream_decoder.h"

#if defined(__ANDROID__)

#include "m4a_gapless.h"
#include "../simd/simd_pcm.h"

#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#define LOG_TAG "SoLoudNativeStream"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

// AMEDIAFORMAT_KEY_PCM_ENCODING is only declared from API 28 (minSdk is 21).
constexpr const char *kKeyPcmEncoding = "pcm-encoding";
constexpr const char *kKeyEncoderDelay = "encoder-delay";
constexpr const char *kKeyEncoderPadding = "encoder-padding";
constexpr int32_t kPcmEncoding16Bit = 2;
constexpr int32_t kPcmEncodingFloat = 4;
constexpr int32_t kMaxChannels = 8; // SoLoud's MAX_CHANNELS
constexpr int32_t kMaxSampleRate = 768000;

constexpr int64_t kDequeueTimeoutUs = 10000;
// Give up when the codec neither accepts input nor produces output this long.
constexpr auto kStallTimeout = std::chrono::seconds(3);
// Frames decoded before a seek target and dropped, so the codec's
// overlap-add state is settled when the target frame comes out (two AAC
// frames).
constexpr uint64_t kSeekPrerollFrames = 2048;
// The same for AC-3/E-AC-3: Dolby's decoder (c2.dolby.eac3.decoder, Galaxy
// S22) outputs silence for the first ~5250 frames after a flush and then
// fades in (~250 frames): eight AC-3 frames.
constexpr uint64_t kSeekPrerollFramesAc3 = 8 * 1536;
// Dolby's decoder holds one frame back and drops it at end of stream. The
// last access unit is queued again this many times before the end-of-stream
// flag so the real frames come out; what the copies decode to lies past the
// playable length and is never read.
constexpr int kAc3DrainRepeats = 3;
// Output buffers with a clamped timestamp held back while waiting for the
// first one that has a real timestamp (see pump()).
constexpr size_t kMaxBacklogFrames = 16 * 1536;

bool isAc3Family(const std::string &mime) {
    return mime == "audio/ac3" || mime == "audio/eac3" || mime == "audio/eac3-joc";
}

/// Dolby's decoders on Samsung devices run Dolby Atmos processing (DAX) on
/// the decoded PCM by default, a volume leveler whose gain depends on
/// seconds of what was decoded before: the PCM after a seek then never
/// matches the PCM at that position. The vendor parameter only exists on
/// those decoders; AMediaCodec_setParameters is API 26 (minSdk is 21).
void disableDolbyProcessing(AMediaCodec *codec) {
    using SetParametersFn = media_status_t (*)(AMediaCodec *, const AMediaFormat *);
    static const auto setParameters =
        reinterpret_cast<SetParametersFn>(dlsym(RTLD_DEFAULT, "AMediaCodec_setParameters"));
    if (!setParameters) return;
    AMediaFormat *params = AMediaFormat_new();
    if (!params) return;
    AMediaFormat_setInt32(params, "vendor.dolby.dax-state.value", 0);
    setParameters(codec, params); // unknown keys are ignored
    AMediaFormat_delete(params);
}

class AndroidStreamDecoder : public NativeStreamDecoder {
public:
    ~AndroidStreamDecoder() override {
        if (mCodec) {
            if (mStarted) AMediaCodec_stop(mCodec);
            AMediaCodec_delete(mCodec);
        }
        if (mExtractor) AMediaExtractor_delete(mExtractor);
        if (mTrackFormat) AMediaFormat_delete(mTrackFormat);
        if (mFd >= 0) close(mFd);
    }

    bool open(const NativeStreamFile &file, std::string &error) {
        // Reopening the pinned descriptor through /proc gives this decoder
        // its own file offset on the same file, even if the path was deleted
        // or replaced. The extractor reads with the offset, so sharing the
        // pinned descriptor itself between voices would race.
        const std::string &path = file.path;
        const std::string source = file.fd >= 0 ? "/proc/self/fd/" + std::to_string(file.fd) : path;
        mFd = ::open(source.c_str(), O_RDONLY | O_CLOEXEC);
        if (mFd < 0) {
            error = "Cannot open file: " + path;
            return false;
        }
        struct stat st;
        if (fstat(mFd, &st) != 0 || st.st_size <= 0) {
            error = "Cannot stat file: " + path;
            return false;
        }

        M4aGaplessInfo gapless;
        if (!parseM4aGaplessFd(mFd, gapless)) {
            error = "Not an MP4 container with an audio track: " + path;
            return false;
        }

        mFileSize = static_cast<off64_t>(st.st_size);
        mExtractor = AMediaExtractor_new();
        if (!mExtractor || AMediaExtractor_setDataSourceFd(mExtractor, mFd, 0, mFileSize) != AMEDIA_OK) {
            error = "AMediaExtractor_setDataSourceFd failed";
            return false;
        }

        struct FormatDeleter {
            void operator()(AMediaFormat *f) const {
                if (f) AMediaFormat_delete(f);
            }
        };
        std::unique_ptr<AMediaFormat, FormatDeleter> trackFormat;
        std::string mime;
        const size_t tracks = AMediaExtractor_getTrackCount(mExtractor);
        for (size_t i = 0; i < tracks && !trackFormat; i++) {
            std::unique_ptr<AMediaFormat, FormatDeleter> format(
                AMediaExtractor_getTrackFormat(mExtractor, i));
            const char *trackMime = nullptr;
            if (format && AMediaFormat_getString(format.get(), AMEDIAFORMAT_KEY_MIME, &trackMime) &&
                trackMime && std::strncmp(trackMime, "audio/", 6) == 0) {
                mime = trackMime;
                trackFormat = std::move(format);
                mTrack = i;
                AMediaExtractor_selectTrack(mExtractor, i);
            }
        }
        if (!trackFormat) {
            error = "No audio track found";
            return false;
        }
        int64_t durationUs = 0;
        AMediaFormat_getInt64(trackFormat.get(), AMEDIAFORMAT_KEY_DURATION, &durationUs);
        // Trimming is done here (see the top of the file).
        AMediaFormat_setInt32(trackFormat.get(), kKeyEncoderDelay, 0);
        AMediaFormat_setInt32(trackFormat.get(), kKeyEncoderPadding, 0);
        mFirstSampleUs = AMediaExtractor_getSampleTime(mExtractor);
        if (mFirstSampleUs < 0 && AMediaExtractor_getSampleTrackIndex(mExtractor) < 0) mFirstSampleUs = 0;

        mCodec = AMediaCodec_createDecoderByType(mime.c_str());
        if (!mCodec) {
            error = "No decoder for " + mime;
            return false;
        }
        mAc3 = isAc3Family(mime);
        if (!startCodec(trackFormat.get())) {
            error = "Failed to start the decoder for " + mime;
            return false;
        }
        if (mAc3) {
            // Kept for recreateCodec().
            mMime = mime;
            mTrackFormat = trackFormat.release();
        }
        trackFormat.reset();

        // The output format (HE-AAC doubles the rate, for one) is only known
        // once the codec produced output.
        const auto start = std::chrono::steady_clock::now();
        while (mPendingFrames == 0 && !mOutputEOS) {
            if (!pump()) {
                error = "Decoder error while starting " + mime;
                return false;
            }
            if (std::chrono::steady_clock::now() - start > kStallTimeout) {
                error = "Decoder produced no output for " + mime;
                return false;
            }
        }
        if (!mFormatKnown) {
            error = "Decoder produced no audio for " + mime;
            return false;
        }

        // Without gapless metadata resolve() still gives the media duration
        // (and no delay); the extractor's rounded duration is the fallback.
        uint64_t delay = 0;
        uint64_t valid = 0;
        gapless.resolve(mFormat.sampleRate, delay, valid);
        if (valid == 0) {
            valid = static_cast<uint64_t>(std::llround(durationUs * static_cast<double>(mFormat.sampleRate) / 1e6));
        }
        mDelayFrames = static_cast<int64_t>(delay);
        mFormat.totalFrames = valid;
        if (mFormat.totalFrames == 0) {
            error = "Unknown stream length";
            return false;
        }
        return true;
    }

    const NativeStreamFormat &format() const override { return mFormat; }

    bool seek(uint64_t frame) override {
        if (frame >= mFormat.totalFrames) {
            mNext = mFormat.totalFrames;
            return true;
        }
        const uint64_t raw = frame + static_cast<uint64_t>(mDelayFrames);
        const uint64_t preroll = mAc3 ? kSeekPrerollFramesAc3 : kSeekPrerollFrames;
        const uint64_t rawStart = raw > preroll ? raw - preroll : 0;
        const int64_t timeUs = mFirstSampleUs +
            static_cast<int64_t>(std::floor(static_cast<double>(rawStart) * 1e6 / mFormat.sampleRate));
        const bool fromStart = rawStart == 0 || timeUs < 0;
        if (fromStart) {
            // Extractors that apply the edit list give the priming samples
            // negative timestamps, and seekTo() can't reach those (it doesn't
            // move, or fails once the end was reached). Start over instead.
            if (!restartExtractor()) return false;
        } else if (AMediaExtractor_seekTo(mExtractor, timeUs, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC) != AMEDIA_OK) {
            LOGE("AMediaExtractor_seekTo(%lld) failed", static_cast<long long>(timeUs));
            return false;
        }
        if (mAc3 && fromStart) {
            // No room for the preroll that covers the silence Dolby's decoder
            // outputs after a flush (see kSeekPrerollFramesAc3); a new codec
            // decodes the start just like the first time.
            if (!recreateCodec()) return false;
        } else if (AMediaCodec_flush(mCodec) != AMEDIA_OK) {
            LOGE("AMediaCodec_flush failed");
            return false;
        }
        mPendingFrames = 0;
        mPendingOffset = 0;
        mBacklog.clear();
        mBacklogFrames = 0;
        mAnchored = false;
        mLastSample.clear();
        mDrainRepeats = 0;
        mInputEOS = false;
        mOutputEOS = false;
        mNext = frame;
        return true;
    }

    long read(float *out, unsigned int maxFrames) override {
        const unsigned int channels = mFormat.channels;
        unsigned int produced = 0;
        auto lastProgress = std::chrono::steady_clock::now();
        while (produced < maxFrames && mNext < mFormat.totalFrames) {
            if (mPendingOffset < mPendingFrames) {
                const int64_t frame = pendingFrame();
                const uint64_t available = mPendingFrames - mPendingOffset;
                if (frame < static_cast<int64_t>(mNext)) {
                    // Priming, or decoded ahead of a seek target.
                    mPendingOffset += static_cast<size_t>(
                        std::min<uint64_t>(available, static_cast<uint64_t>(static_cast<int64_t>(mNext) - frame)));
                    continue;
                }
                uint64_t n = std::min<uint64_t>({available, maxFrames - produced, mFormat.totalFrames - mNext});
                if (frame > static_cast<int64_t>(mNext)) {
                    // A hole in the timestamps: fill it so positions hold.
                    n = std::min<uint64_t>(n, static_cast<uint64_t>(frame - static_cast<int64_t>(mNext)));
                    std::memset(out + produced * channels, 0, n * channels * sizeof(float));
                } else {
                    std::memcpy(out + produced * channels, mPending.data() + mPendingOffset * channels,
                                n * channels * sizeof(float));
                    mPendingOffset += n;
                }
                produced += static_cast<unsigned int>(n);
                mNext += n;
                lastProgress = std::chrono::steady_clock::now();
                continue;
            }
            if (mOutputEOS) break;
            const uint64_t before = mOutputBuffers;
            if (!pump()) return produced > 0 ? produced : -1;
            if (mOutputBuffers != before) {
                lastProgress = std::chrono::steady_clock::now();
            } else if (std::chrono::steady_clock::now() - lastProgress > kStallTimeout) {
                LOGE("Decoder stalled");
                return produced > 0 ? produced : -1;
            }
        }
        return produced;
    }

private:
    bool startCodec(AMediaFormat *format) {
        if (AMediaCodec_configure(mCodec, format, nullptr, nullptr, 0) != AMEDIA_OK ||
            AMediaCodec_start(mCodec) != AMEDIA_OK) {
            return false;
        }
        mStarted = true;
        if (mAc3) disableDolbyProcessing(mCodec);
        return true;
    }

    /// Replaces the codec with a new one (stop() and start() keep the state
    /// of Dolby's decoder).
    bool recreateCodec() {
        if (mStarted) AMediaCodec_stop(mCodec);
        mStarted = false;
        AMediaCodec_delete(mCodec);
        mCodec = AMediaCodec_createDecoderByType(mMime.c_str());
        if (!mCodec || !startCodec(mTrackFormat)) {
            LOGE("Failed to recreate the decoder for %s", mMime.c_str());
            return false;
        }
        return true;
    }

    bool restartExtractor() {
        AMediaExtractor *extractor = AMediaExtractor_new();
        if (!extractor || AMediaExtractor_setDataSourceFd(extractor, mFd, 0, mFileSize) != AMEDIA_OK ||
            AMediaExtractor_selectTrack(extractor, mTrack) != AMEDIA_OK) {
            if (extractor) AMediaExtractor_delete(extractor);
            LOGE("Failed to reopen the extractor");
            return false;
        }
        AMediaExtractor_delete(mExtractor);
        mExtractor = extractor;
        return true;
    }

    int64_t pendingFrame() const {
        return mPendingRaw - mDelayFrames + static_cast<int64_t>(mPendingOffset);
    }

    /// Frames from the first sample to the start of an output buffer.
    int64_t rawFrameAt(int64_t ptsUs) const {
        return static_cast<int64_t>(
            std::llround(static_cast<double>(ptsUs - mFirstSampleUs) * mFormat.sampleRate / 1e6));
    }

    /// Makes the held-back output (mBacklog) the pending PCM.
    void takeBacklog() {
        mPending.swap(mBacklog);
        mPendingFrames = mBacklogFrames;
        mPendingOffset = 0;
        mPendingRaw = mBacklogRaw;
        mBacklog.clear();
        mBacklogFrames = 0;
    }

    bool applyOutputFormat() {
        AMediaFormat *format = AMediaCodec_getOutputFormat(mCodec);
        if (!format) return false;
        int32_t channels = 0;
        int32_t rate = 0;
        int32_t encoding = kPcmEncoding16Bit;
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate);
        AMediaFormat_getInt32(format, kKeyPcmEncoding, &encoding);
        AMediaFormat_delete(format);
        if (channels < 1 || channels > kMaxChannels || rate < 1 || rate > kMaxSampleRate ||
            (encoding != kPcmEncoding16Bit && encoding != kPcmEncodingFloat)) {
            LOGE("Unsupported decoder output: %d ch, %d Hz, encoding %d", channels, rate, encoding);
            return false;
        }
        if (mFormatKnown && (static_cast<unsigned int>(channels) != mFormat.channels ||
                             static_cast<float>(rate) != mFormat.sampleRate)) {
            LOGE("Decoder output format changed mid-stream");
            return false;
        }
        mFormat.channels = static_cast<unsigned int>(channels);
        mFormat.sampleRate = static_cast<float>(rate);
        mEncoding = encoding;
        mFormatKnown = true;
        return true;
    }

    /// Queues all the input the codec takes, then waits briefly for one
    /// output buffer and stores it as the pending PCM, or holds it back (the
    /// previous pending PCM must be consumed). Returns false on a codec error.
    bool pump() {
        while (!mInputEOS) {
            const ssize_t index = AMediaCodec_dequeueInputBuffer(mCodec, 0);
            if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) break;
            if (index < 0) return false;
            size_t capacity = 0;
            uint8_t *buffer = AMediaCodec_getInputBuffer(mCodec, static_cast<size_t>(index), &capacity);
            if (!buffer) return false;
            const ssize_t size = AMediaExtractor_readSampleData(mExtractor, buffer, capacity);
            if (size < 0 && mAc3 && mDrainRepeats < kAc3DrainRepeats && !mLastSample.empty() &&
                mLastSample.size() <= capacity) {
                // See kAc3DrainRepeats.
                std::memcpy(buffer, mLastSample.data(), mLastSample.size());
                mLastSampleUs += mLastSampleDurationUs;
                mDrainRepeats++;
                if (AMediaCodec_queueInputBuffer(mCodec, static_cast<size_t>(index), 0, mLastSample.size(),
                                                 static_cast<uint64_t>(mLastSampleUs), 0) != AMEDIA_OK) {
                    return false;
                }
                continue;
            }
            if (size < 0) {
                mInputEOS = true;
                if (AMediaCodec_queueInputBuffer(mCodec, static_cast<size_t>(index), 0, 0, 0,
                                                 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != AMEDIA_OK) {
                    return false;
                }
                break;
            }
            const int64_t timeUs = AMediaExtractor_getSampleTime(mExtractor);
            if (mAc3) {
                if (!mLastSample.empty() && timeUs > mLastSampleUs) mLastSampleDurationUs = timeUs - mLastSampleUs;
                mLastSample.assign(buffer, buffer + size);
                mLastSampleUs = timeUs;
            }
            if (AMediaCodec_queueInputBuffer(mCodec, static_cast<size_t>(index), 0, static_cast<size_t>(size),
                                             static_cast<uint64_t>(timeUs), 0) != AMEDIA_OK) {
                return false;
            }
            AMediaExtractor_advance(mExtractor);
        }

        for (int attempt = 0; attempt < 8; attempt++) {
            AMediaCodecBufferInfo info{};
            const ssize_t index = AMediaCodec_dequeueOutputBuffer(mCodec, &info, kDequeueTimeoutUs);
            if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) return true;
            if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                if (!applyOutputFormat()) return false;
                continue;
            }
            if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
            if (index < 0) return false;

            bool ok = true;
            if (info.size > 0) {
                if (!mFormatKnown && !applyOutputFormat()) ok = false;
                size_t capacity = 0;
                const uint8_t *buffer = ok ? AMediaCodec_getOutputBuffer(mCodec, static_cast<size_t>(index), &capacity)
                                           : nullptr;
                const size_t bytesPerSample = mEncoding == kPcmEncodingFloat ? sizeof(float) : sizeof(int16_t);
                const size_t bytesPerFrame = bytesPerSample * mFormat.channels;
                if (!buffer || info.offset < 0 || static_cast<size_t>(info.offset) > capacity ||
                    static_cast<size_t>(info.size) > capacity - static_cast<size_t>(info.offset) ||
                    static_cast<size_t>(info.size) % bytesPerFrame != 0) {
                    ok = false;
                } else {
                    const size_t frames = static_cast<size_t>(info.size) / bytesPerFrame;
                    const size_t samples = frames * mFormat.channels;
                    // Timestamps before the first sample's come out clamped
                    // to 0 from some decoders (Dolby's): those buffers are
                    // held back and placed right before the first buffer
                    // with a real timestamp (decoded PCM is contiguous).
                    const bool clamped = !mAnchored && mFirstSampleUs < 0 && info.presentationTimeUs <= 0;
                    const size_t base = mBacklog.size();
                    mBacklog.resize(base + samples);
                    float *dst = mBacklog.data() + base;
                    const uint8_t *src = buffer + info.offset;
                    if (mEncoding == kPcmEncodingFloat) {
                        std::memcpy(dst, src, samples * sizeof(float));
                    } else if ((reinterpret_cast<uintptr_t>(src) & 1u) == 0u &&
                               samples <= UINT_MAX) {
                        SoLoud::convertInt16ToFloats(
                            reinterpret_cast<const int16_t *>(src),
                            dst,
                            static_cast<unsigned int>(samples));
                    } else {
                        for (size_t i = 0; i < samples; i++) {
                            int16_t s;
                            std::memcpy(&s, src + i * sizeof(int16_t), sizeof(s));
                            dst[i] = s * (1.0f / 32768.0f);
                        }
                    }
                    if (clamped && mBacklogFrames == 0) mBacklogRaw = rawFrameAt(info.presentationTimeUs);
                    mBacklogFrames += frames;
                    if (!clamped) {
                        mBacklogRaw = rawFrameAt(info.presentationTimeUs) - static_cast<int64_t>(base / mFormat.channels);
                        mAnchored = true;
                    }
                    if (!clamped || mBacklogFrames >= kMaxBacklogFrames) takeBacklog();
                    mOutputBuffers++;
                }
            }
            if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                mOutputEOS = true;
                // Never got a real timestamp: place them by the first one.
                if (mBacklogFrames > 0) takeBacklog();
            }
            AMediaCodec_releaseOutputBuffer(mCodec, static_cast<size_t>(index), false);
            return ok;
        }
        return true;
    }

    int mFd = -1;
    off64_t mFileSize = 0;
    size_t mTrack = 0;
    AMediaExtractor *mExtractor = nullptr;
    AMediaCodec *mCodec = nullptr;
    bool mStarted = false;

    NativeStreamFormat mFormat;
    bool mFormatKnown = false;
    int32_t mEncoding = kPcmEncoding16Bit;
    int64_t mFirstSampleUs = 0;
    int64_t mDelayFrames = 0;

    bool mAc3 = false; // AC-3/E-AC-3, see kSeekPrerollFramesAc3, kAc3DrainRepeats
    std::string mMime;                    // AC-3 only, for recreateCodec()
    AMediaFormat *mTrackFormat = nullptr; // AC-3 only, for recreateCodec()

    std::vector<float> mPending; // interleaved output of the last buffer
    size_t mPendingFrames = 0;
    size_t mPendingOffset = 0;
    int64_t mPendingRaw = 0; // frames from the first sample to mPending[0]
    uint64_t mOutputBuffers = 0;

    std::vector<float> mBacklog; // output with clamped timestamps, see pump()
    size_t mBacklogFrames = 0;
    int64_t mBacklogRaw = 0;
    bool mAnchored = false; // a buffer with a real timestamp came out

    std::vector<uint8_t> mLastSample; // AC-3 only, see kAc3DrainRepeats
    int64_t mLastSampleUs = 0;
    int64_t mLastSampleDurationUs = 0;
    int mDrainRepeats = 0;

    uint64_t mNext = 0; // next playable frame read() returns
    bool mInputEOS = false;
    bool mOutputEOS = false;
};

} // namespace

std::unique_ptr<NativeStreamDecoder> openPlatformNativeStreamDecoder(const NativeStreamFile &file,
                                                                     std::string &error) {
    auto decoder = std::make_unique<AndroidStreamDecoder>();
    if (!decoder->open(file, error)) {
        LOGE("%s", error.c_str());
        return nullptr;
    }
    return decoder;
}

#endif // defined(__ANDROID__)
