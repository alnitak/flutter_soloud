#if defined(__linux__) && !defined(__ANDROID__)

#include "aac_stream_decoder.h"
#include "../native_decoder/native_audio_decoder.h"
#include <dlfcn.h>
#include <vector>
#include <deque>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <iostream>

struct AVCodec;
struct AVCodecContext;
struct AVCodecParserContext;
struct AVPacket;
struct AVFrame;

namespace {

typedef const struct AVCodec* (*pfn_avcodec_find_decoder_by_name)(const char *name);
typedef const struct AVCodec* (*pfn_avcodec_find_decoder)(int id);
typedef struct AVCodecContext* (*pfn_avcodec_alloc_context3)(const struct AVCodec *codec);
typedef int (*pfn_avcodec_open2)(struct AVCodecContext *avctx, const struct AVCodec *codec, void **options);
typedef void (*pfn_avcodec_free_context)(struct AVCodecContext **avctx);
typedef int (*pfn_avcodec_send_packet)(struct AVCodecContext *avctx, const struct AVPacket *avpkt);
typedef int (*pfn_avcodec_receive_frame)(struct AVCodecContext *avctx, struct AVFrame *frame);

typedef struct AVCodecParserContext* (*pfn_av_parser_init)(int codec_id);
typedef int (*pfn_av_parser_parse2)(struct AVCodecParserContext *s,
                                    struct AVCodecContext *avctx,
                                    uint8_t **poutbuf, int *poutbuf_size,
                                    const uint8_t *buf, int buf_size,
                                    int64_t pts, int64_t dts,
                                    int64_t pos);
typedef void (*pfn_av_parser_close)(struct AVCodecParserContext *s);

typedef struct AVPacket* (*pfn_av_packet_alloc)(void);
typedef void (*pfn_av_packet_free)(struct AVPacket **pkt);
typedef void (*pfn_av_packet_unref)(struct AVPacket *pkt);

typedef struct AVFrame* (*pfn_av_frame_alloc)(void);
typedef void (*pfn_av_frame_free)(struct AVFrame **frame);
typedef void (*pfn_av_frame_unref)(struct AVFrame *frame);

struct LinuxFFmpegCodecLoader {
    void *hCodec = nullptr;

    pfn_avcodec_find_decoder_by_name avcodec_find_decoder_by_name = nullptr;
    pfn_avcodec_find_decoder avcodec_find_decoder = nullptr;
    pfn_avcodec_alloc_context3 avcodec_alloc_context3 = nullptr;
    pfn_avcodec_open2 avcodec_open2 = nullptr;
    pfn_avcodec_free_context avcodec_free_context = nullptr;
    pfn_avcodec_send_packet avcodec_send_packet = nullptr;
    pfn_avcodec_receive_frame avcodec_receive_frame = nullptr;

    pfn_av_parser_init av_parser_init = nullptr;
    pfn_av_parser_parse2 av_parser_parse2 = nullptr;
    pfn_av_parser_close av_parser_close = nullptr;

    pfn_av_packet_alloc av_packet_alloc = nullptr;
    pfn_av_packet_free av_packet_free = nullptr;
    pfn_av_packet_unref av_packet_unref = nullptr;

    pfn_av_frame_alloc av_frame_alloc = nullptr;
    pfn_av_frame_free av_frame_free = nullptr;
    pfn_av_frame_unref av_frame_unref = nullptr;

    bool loaded = false;

    static void* tryDlopen(const char* const names[]) {
        for (int i = 0; names[i] != nullptr; i++) {
            void *h = dlopen(names[i], RTLD_LAZY | RTLD_LOCAL);
            if (h) return h;
        }
        return nullptr;
    }

    bool init() {
        if (loaded) return true;

        const char* const codecNames[] = {
            "libavcodec.so.63", "libavcodec.so.62", "libavcodec.so.61", "libavcodec.so.60", "libavcodec.so.59", "libavcodec.so.58", "libavcodec.so", nullptr
        };

        hCodec = tryDlopen(codecNames);
        if (!hCodec) return false;

        #define LOAD_SYM(name) \
            name = reinterpret_cast<pfn_##name>(dlsym(hCodec, #name)); \
            if (!name) return false;

        LOAD_SYM(avcodec_find_decoder_by_name);
        LOAD_SYM(avcodec_alloc_context3);
        LOAD_SYM(avcodec_open2);
        LOAD_SYM(avcodec_free_context);
        LOAD_SYM(avcodec_send_packet);
        LOAD_SYM(avcodec_receive_frame);

        LOAD_SYM(av_parser_init);
        LOAD_SYM(av_parser_parse2);
        LOAD_SYM(av_parser_close);

        LOAD_SYM(av_packet_alloc);
        LOAD_SYM(av_packet_free);
        LOAD_SYM(av_packet_unref);

        LOAD_SYM(av_frame_alloc);
        LOAD_SYM(av_frame_free);
        LOAD_SYM(av_frame_unref);

        #undef LOAD_SYM

        // avcodec_find_decoder is optional if avcodec_find_decoder_by_name exists
        avcodec_find_decoder = reinterpret_cast<pfn_avcodec_find_decoder>(dlsym(hCodec, "avcodec_find_decoder"));

        loaded = true;
        return true;
    }
};

static LinuxFFmpegCodecLoader gLinuxFFmpeg;

// Sample format constants
constexpr int AV_SAMPLE_FMT_U8 = 0;
constexpr int AV_SAMPLE_FMT_S16 = 1;
constexpr int AV_SAMPLE_FMT_S32 = 2;
constexpr int AV_SAMPLE_FMT_FLT = 3;
constexpr int AV_SAMPLE_FMT_DBL = 4;
constexpr int AV_SAMPLE_FMT_U8P = 5;
constexpr int AV_SAMPLE_FMT_S16P = 6;
constexpr int AV_SAMPLE_FMT_S32P = 7;
constexpr int AV_SAMPLE_FMT_FLTP = 8;
constexpr int AV_SAMPLE_FMT_DBLP = 9;

struct DummyAVCodec {
    const char *name;
    const char *long_name;
    int type;
    int id;
    int capabilities;
};

struct DummyAVFrame {
    uint8_t *data[8];
    int linesize[8];
    uint8_t **extended_data;
    int width, height;
    int nb_samples;
    int format;
    int key_frame;
};

struct DummyAVPacket {
    void *buf;
    int64_t pts;
    int64_t dts;
    uint8_t *data;
    int size;
    int stream_index;
    int flags;
};

class LinuxAACImpl : public AACDecoderWrapper::Impl {
public:
    explicit LinuxAACImpl(DetectedType format = DetectedType::BUFFER_AAC)
        : mFormat(format) {}

    ~LinuxAACImpl() override {
        cleanup();
    }

    bool initialize(int engineSamplerate, int engineChannels) override {
        cleanup();

        mTargetSampleRate = engineSamplerate;
        mTargetChannels = engineChannels;

        if (!gLinuxFFmpeg.init()) {
            std::cerr << "[flutter_soloud] libavcodec not available on Linux host." << std::endl;
            return false;
        }

        const char *decoderName = "aac";
        int codecId = 86018; // AV_CODEC_ID_AAC
        if (mFormat == DetectedType::BUFFER_AC3) {
            decoderName = "ac3";
            codecId = 86021; // AV_CODEC_ID_AC3
        } else if (mFormat == DetectedType::BUFFER_EAC3) {
            decoderName = "eac3";
            codecId = 86022; // AV_CODEC_ID_EAC3
        }

        const AVCodec *codec = gLinuxFFmpeg.avcodec_find_decoder_by_name(decoderName);
        if (!codec && gLinuxFFmpeg.avcodec_find_decoder) {
            codec = gLinuxFFmpeg.avcodec_find_decoder(codecId);
        }

        if (!codec) {
            std::cerr << "[flutter_soloud] FFmpeg decoder not found for: " << decoderName << std::endl;
            return false;
        }

        const DummyAVCodec *dCodec = reinterpret_cast<const DummyAVCodec*>(codec);
        int realCodecId = dCodec->id;

        mCodecCtx = gLinuxFFmpeg.avcodec_alloc_context3(codec);
        if (!mCodecCtx) {
            std::cerr << "[flutter_soloud] avcodec_alloc_context3 failed." << std::endl;
            return false;
        }

        if (gLinuxFFmpeg.avcodec_open2(mCodecCtx, codec, nullptr) < 0) {
            std::cerr << "[flutter_soloud] avcodec_open2 failed for: " << decoderName << std::endl;
            cleanup();
            return false;
        }

        mParser = gLinuxFFmpeg.av_parser_init(realCodecId);
        if (!mParser) {
            std::cerr << "[flutter_soloud] av_parser_init failed for codec id: " << realCodecId << std::endl;
            cleanup();
            return false;
        }

        mPkt = gLinuxFFmpeg.av_packet_alloc();
        mFrame = gLinuxFFmpeg.av_frame_alloc();

        mInitialized = true;
        return true;
    }

    std::pair<std::vector<float>, DecoderError>
    decode(std::vector<unsigned char> &buffer, int *samplerate,
           int *channels, size_t maxOutputSamples) override {
        if (!mInitialized || !mCodecCtx || !mParser) {
            return {{}, DecoderError::FailedToCreateDecoder};
        }

        std::vector<float> decodedData;

        // Serve any leftover decoded samples first
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
        if (!mFormatInitialized && !buffer.empty()) {
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

        // Parse incoming bytes into packets
        while (!buffer.empty()) {
            uint8_t *poutbuf = nullptr;
            int poutbuf_size = 0;
            int consumed = gLinuxFFmpeg.av_parser_parse2(
                mParser, mCodecCtx,
                &poutbuf, &poutbuf_size,
                buffer.data(), static_cast<int>(buffer.size()),
                0, 0, 0
            );

            if (poutbuf_size > 0 && poutbuf) {
                if (!mFormatInitialized) {
                    if (mFormat == DetectedType::BUFFER_AAC) {
                        AacMetadata aac;
                        if (AACDecoderWrapper::parseAacAdtsMetadata(poutbuf, poutbuf_size, aac)) {
                            mStreamSampleRate = aac.sampleRate;
                            mStreamChannels = aac.channels;
                            mFormatInitialized = true;
                        }
                    } else if (mFormat == DetectedType::BUFFER_AC3) {
                        Ac3Metadata ac3;
                        if (AACDecoderWrapper::parseAc3Metadata(poutbuf, poutbuf_size, ac3)) {
                            mStreamSampleRate = ac3.sampleRate;
                            mStreamChannels = ac3.channels;
                            mFormatInitialized = true;
                        }
                    } else if (mFormat == DetectedType::BUFFER_EAC3) {
                        Eac3Metadata eac3;
                        if (AACDecoderWrapper::parseEac3Metadata(poutbuf, poutbuf_size, eac3)) {
                            mStreamSampleRate = eac3.sampleRate;
                            mStreamChannels = eac3.channels;
                            mFormatInitialized = true;
                        }
                    }
                }

                auto dummyPkt = reinterpret_cast<DummyAVPacket*>(mPkt);
                dummyPkt->data = poutbuf;
                dummyPkt->size = poutbuf_size;

                int sendRes = gLinuxFFmpeg.avcodec_send_packet(mCodecCtx, mPkt);
                gLinuxFFmpeg.av_packet_unref(mPkt);

                if (sendRes == 0) {
                    while (gLinuxFFmpeg.avcodec_receive_frame(mCodecCtx, mFrame) == 0) {
                        processFrame(mFrame, decodedData, maxOutputSamples);
                        gLinuxFFmpeg.av_frame_unref(mFrame);
                    }
                }
            }

            if (consumed > 0) {
                buffer.erase(buffer.begin(), buffer.begin() + consumed);
            }

            if (consumed <= 0 && poutbuf_size <= 0) {
                break;
            }

            if (maxOutputSamples > 0 && decodedData.size() >= maxOutputSamples) {
                break;
            }
        }

        if (samplerate && mStreamSampleRate > 0) *samplerate = mStreamSampleRate;
        if (channels && mStreamChannels > 0) *channels = mStreamChannels;

        return {std::move(decodedData), DecoderError::NoError};
    }

    void setDataEnded() override {
        if (!mInitialized || !mCodecCtx) return;

        // Flush parser
        uint8_t *poutbuf = nullptr;
        int poutbuf_size = 0;
        if (mParser) {
            gLinuxFFmpeg.av_parser_parse2(mParser, mCodecCtx, &poutbuf, &poutbuf_size, nullptr, 0, 0, 0, 0);
            if (poutbuf_size > 0 && poutbuf && mPkt) {
                auto dummyPkt = reinterpret_cast<DummyAVPacket*>(mPkt);
                dummyPkt->data = poutbuf;
                dummyPkt->size = poutbuf_size;
                gLinuxFFmpeg.avcodec_send_packet(mCodecCtx, mPkt);
                gLinuxFFmpeg.av_packet_unref(mPkt);
            }
        }

        // Flush decoder
        if (gLinuxFFmpeg.avcodec_send_packet(mCodecCtx, nullptr) == 0 && mFrame) {
            std::vector<float> flushed;
            while (gLinuxFFmpeg.avcodec_receive_frame(mCodecCtx, mFrame) == 0) {
                processFrame(mFrame, flushed, 0);
                gLinuxFFmpeg.av_frame_unref(mFrame);
            }
            if (!flushed.empty()) {
                mDecodedRemainder.insert(mDecodedRemainder.end(), flushed.begin(), flushed.end());
            }
        }
    }

    bool hasPendingData() const override {
        return !mDecodedRemainder.empty();
    }

private:
    void cleanup() {
        if (mPkt) {
            gLinuxFFmpeg.av_packet_free(&mPkt);
            mPkt = nullptr;
        }
        if (mFrame) {
            gLinuxFFmpeg.av_frame_free(&mFrame);
            mFrame = nullptr;
        }
        if (mParser) {
            gLinuxFFmpeg.av_parser_close(mParser);
            mParser = nullptr;
        }
        if (mCodecCtx) {
            gLinuxFFmpeg.avcodec_free_context(&mCodecCtx);
            mCodecCtx = nullptr;
        }
        mInitialized = false;
        mFormatInitialized = false;
        mDecodedRemainder.clear();
    }

    void processFrame(AVFrame *f, std::vector<float> &out, size_t maxOutputSamples) {
        DummyAVFrame *dFrame = reinterpret_cast<DummyAVFrame*>(f);
        int nbSamples = dFrame->nb_samples;
        int fmt = dFrame->format;
        if (nbSamples <= 0) return;

        if (!mFormatInitialized) {
            auto dummyPkt = reinterpret_cast<DummyAVPacket*>(mPkt);
            // Determine sample rate and channels from metadata or context
            if (mFormat == DetectedType::BUFFER_AAC) {
                AacMetadata aac;
                if (dummyPkt && dummyPkt->data && dummyPkt->size > 0 &&
                    AACDecoderWrapper::parseAacAdtsMetadata(dummyPkt->data, dummyPkt->size, aac)) {
                    mStreamSampleRate = aac.sampleRate;
                    mStreamChannels = aac.channels;
                }
            } else if (mFormat == DetectedType::BUFFER_AC3) {
                Ac3Metadata ac3;
                if (dummyPkt && dummyPkt->data && dummyPkt->size > 0 &&
                    AACDecoderWrapper::parseAc3Metadata(dummyPkt->data, dummyPkt->size, ac3)) {
                    mStreamSampleRate = ac3.sampleRate;
                    mStreamChannels = ac3.channels;
                }
            } else if (mFormat == DetectedType::BUFFER_EAC3) {
                Eac3Metadata eac3;
                if (dummyPkt && dummyPkt->data && dummyPkt->size > 0 &&
                    AACDecoderWrapper::parseEac3Metadata(dummyPkt->data, dummyPkt->size, eac3)) {
                    mStreamSampleRate = eac3.sampleRate;
                    mStreamChannels = eac3.channels;
                }
            }

            if (mStreamSampleRate <= 0) mStreamSampleRate = mTargetSampleRate > 0 ? mTargetSampleRate : 44100;
            if (mStreamChannels <= 0) mStreamChannels = mTargetChannels > 0 ? mTargetChannels : 2;
            mFormatInitialized = true;
        }

        // Extract actual sample rate from the decoded AVFrame
        const uint8_t *fb = reinterpret_cast<const uint8_t*>(f);
        int frameSampleRate = 0;
        const int candidateOffsets[] = {180, 184, 188, 192, 204, 208};
        for (int off : candidateOffsets) {
            int val = *reinterpret_cast<const int*>(fb + off);
            if (val == 44100 || val == 48000 || val == 32000 || val == 22050 ||
                val == 24000 || val == 16000 || val == 11025 || val == 12000 ||
                val == 8000 || val == 88200 || val == 96000) {
                frameSampleRate = val;
                break;
            }
        }

        if (frameSampleRate > 0) {
            mStreamSampleRate = frameSampleRate;
        } else if (mFormat == DetectedType::BUFFER_AAC && nbSamples >= 2048 && mStreamSampleRate <= 24000 && mStreamSampleRate > 0) {
            // HE-AAC (SBR) doubles the core ADTS sample rate (e.g. 22050 -> 44100, 24000 -> 48000)
            mStreamSampleRate *= 2;
        }

        int ch = mStreamChannels > 0 ? mStreamChannels : 2;
        if (ch == 1 && (fmt == AV_SAMPLE_FMT_FLTP || fmt == AV_SAMPLE_FMT_S16P || fmt == AV_SAMPLE_FMT_S32P)) {
            const void *plane1 = dFrame->extended_data ? dFrame->extended_data[1] : dFrame->data[1];
            if (plane1 != nullptr) {
                ch = 2;
                mStreamChannels = 2;
            }
        }
        size_t totalNewSamples = static_cast<size_t>(nbSamples * ch);

        std::vector<float> frameSamples;
        frameSamples.reserve(totalNewSamples);

        if (fmt == AV_SAMPLE_FMT_FLTP) {
            for (int s = 0; s < nbSamples; s++) {
                for (int c = 0; c < ch && c < 8; c++) {
                    const float *plane = reinterpret_cast<const float *>(dFrame->extended_data ? dFrame->extended_data[c] : dFrame->data[c]);
                    frameSamples.push_back(plane ? plane[s] : 0.0f);
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_FLT) {
            const float *src = reinterpret_cast<const float *>(dFrame->data[0]);
            if (src) {
                frameSamples.insert(frameSamples.end(), src, src + totalNewSamples);
            }
        } else if (fmt == AV_SAMPLE_FMT_S16P) {
            for (int s = 0; s < nbSamples; s++) {
                for (int c = 0; c < ch && c < 8; c++) {
                    const int16_t *plane = reinterpret_cast<const int16_t *>(dFrame->extended_data ? dFrame->extended_data[c] : dFrame->data[c]);
                    frameSamples.push_back(plane ? (plane[s] / 32768.0f) : 0.0f);
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_S16) {
            const int16_t *src = reinterpret_cast<const int16_t *>(dFrame->data[0]);
            if (src) {
                for (size_t i = 0; i < totalNewSamples; i++) {
                    frameSamples.push_back(src[i] / 32768.0f);
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_S32P) {
            for (int s = 0; s < nbSamples; s++) {
                for (int c = 0; c < ch && c < 8; c++) {
                    const int32_t *plane = reinterpret_cast<const int32_t *>(dFrame->extended_data ? dFrame->extended_data[c] : dFrame->data[c]);
                    frameSamples.push_back(plane ? (plane[s] / 2147483648.0f) : 0.0f);
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_S32) {
            const int32_t *src = reinterpret_cast<const int32_t *>(dFrame->data[0]);
            if (src) {
                for (size_t i = 0; i < totalNewSamples; i++) {
                    frameSamples.push_back(src[i] / 2147483648.0f);
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_DBLP) {
            for (int s = 0; s < nbSamples; s++) {
                for (int c = 0; c < ch && c < 8; c++) {
                    const double *plane = reinterpret_cast<const double *>(dFrame->extended_data ? dFrame->extended_data[c] : dFrame->data[c]);
                    frameSamples.push_back(plane ? static_cast<float>(plane[s]) : 0.0f);
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_DBL) {
            const double *src = reinterpret_cast<const double *>(dFrame->data[0]);
            if (src) {
                for (size_t i = 0; i < totalNewSamples; i++) {
                    frameSamples.push_back(static_cast<float>(src[i]));
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_U8P) {
            for (int s = 0; s < nbSamples; s++) {
                for (int c = 0; c < ch && c < 8; c++) {
                    const uint8_t *plane = reinterpret_cast<const uint8_t *>(dFrame->extended_data ? dFrame->extended_data[c] : dFrame->data[c]);
                    frameSamples.push_back(plane ? ((plane[s] - 128) / 128.0f) : 0.0f);
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_U8) {
            const uint8_t *src = reinterpret_cast<const uint8_t *>(dFrame->data[0]);
            if (src) {
                for (size_t i = 0; i < totalNewSamples; i++) {
                    frameSamples.push_back((src[i] - 128) / 128.0f);
                }
            }
        }

        size_t availableSpace = (maxOutputSamples > 0 && out.size() < maxOutputSamples)
                                    ? (maxOutputSamples - out.size())
                                    : (maxOutputSamples == 0 ? frameSamples.size() : 0);

        size_t toAppend = std::min(frameSamples.size(), availableSpace);
        out.insert(out.end(), frameSamples.begin(), frameSamples.begin() + toAppend);
        for (size_t i = toAppend; i < frameSamples.size(); i++) {
            mDecodedRemainder.push_back(frameSamples[i]);
        }
    }

    DetectedType mFormat;
    int mTargetSampleRate = 44100;
    int mTargetChannels = 2;
    int mStreamSampleRate = 0;
    int mStreamChannels = 0;

    AVCodecContext *mCodecCtx = nullptr;
    AVCodecParserContext *mParser = nullptr;
    AVPacket *mPkt = nullptr;
    AVFrame *mFrame = nullptr;

    bool mInitialized = false;
    bool mFormatInitialized = false;
    std::deque<float> mDecodedRemainder;
};

} // anonymous namespace

std::unique_ptr<AACDecoderWrapper::Impl> createLinuxAACDecoderImpl(DetectedType format) {
    return std::make_unique<LinuxAACImpl>(format);
}

#endif // defined(__linux__) && !defined(__ANDROID__)
