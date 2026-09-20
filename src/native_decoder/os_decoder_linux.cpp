#if defined(__linux__) && !defined(__ANDROID__)

#include "native_audio_decoder.h"
#include "../audiobuffer/m4a_metadata.h"
#include "../audiobuffer/aac_stream_decoder.h"
#include <dlfcn.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

// Forward declarations of basic FFmpeg structs to avoid needing <libavcodec/avcodec.h> headers
// at compile time on Linux machines that do not have ffmpeg development packages installed.
struct AVFormatContext;
struct AVCodecContext;
struct AVCodec;
struct AVCodecParameters;
struct AVPacket;
struct AVFrame;
struct SwrContext;
struct AVChannelLayout;

namespace {

// Function pointers for dynamic loading from libavformat, libavcodec
typedef struct AVFormatContext* (*pfn_avformat_alloc_context)(void);
typedef int (*pfn_avformat_open_input)(struct AVFormatContext **ps, const char *url, void *fmt, void **options);
typedef int (*pfn_avformat_find_stream_info)(struct AVFormatContext *ic, void **options);
typedef void (*pfn_avformat_close_input)(struct AVFormatContext **s);
typedef int (*pfn_av_read_frame)(struct AVFormatContext *s, struct AVPacket *pkt);

typedef const struct AVCodec* (*pfn_avcodec_find_decoder)(int id);
typedef struct AVCodecContext* (*pfn_avcodec_alloc_context3)(const struct AVCodec *codec);
typedef int (*pfn_avcodec_parameters_to_context)(struct AVCodecContext *codec, const struct AVCodecParameters *par);
typedef int (*pfn_avcodec_open2)(struct AVCodecContext *avctx, const struct AVCodec *codec, void **options);
typedef void (*pfn_avcodec_free_context)(struct AVCodecContext **avctx);
typedef int (*pfn_avcodec_send_packet)(struct AVCodecContext *avctx, const struct AVPacket *avpkt);
typedef int (*pfn_avcodec_receive_frame)(struct AVCodecContext *avctx, struct AVFrame *frame);

typedef struct AVPacket* (*pfn_av_packet_alloc)(void);
typedef void (*pfn_av_packet_free)(struct AVPacket **pkt);
typedef void (*pfn_av_packet_unref)(struct AVPacket *pkt);

typedef struct AVFrame* (*pfn_av_frame_alloc)(void);
typedef void (*pfn_av_frame_free)(struct AVFrame **frame);
typedef void (*pfn_av_frame_unref)(struct AVFrame *frame);

struct FFmpegLoader {
    void *hFormat = nullptr;
    void *hCodec = nullptr;

    pfn_avformat_open_input avformat_open_input = nullptr;
    pfn_avformat_find_stream_info avformat_find_stream_info = nullptr;
    pfn_avformat_close_input avformat_close_input = nullptr;
    pfn_av_read_frame av_read_frame = nullptr;

    pfn_avcodec_find_decoder avcodec_find_decoder = nullptr;
    pfn_avcodec_alloc_context3 avcodec_alloc_context3 = nullptr;
    pfn_avcodec_parameters_to_context avcodec_parameters_to_context = nullptr;
    pfn_avcodec_open2 avcodec_open2 = nullptr;
    pfn_avcodec_free_context avcodec_free_context = nullptr;
    pfn_avcodec_send_packet avcodec_send_packet = nullptr;
    pfn_avcodec_receive_frame avcodec_receive_frame = nullptr;

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

        const char* const formatNames[] = {
            "libavformat.so.61", "libavformat.so.60", "libavformat.so.59", "libavformat.so.58", "libavformat.so", nullptr
        };
        const char* const codecNames[] = {
            "libavcodec.so.61", "libavcodec.so.60", "libavcodec.so.59", "libavcodec.so.58", "libavcodec.so", nullptr
        };

        hFormat = tryDlopen(formatNames);
        hCodec = tryDlopen(codecNames);

        if (!hFormat || !hCodec) {
            return false;
        }

        #define LOAD_SYM(handle, name) \
            name = reinterpret_cast<pfn_##name>(dlsym(handle, #name)); \
            if (!name) return false;

        LOAD_SYM(hFormat, avformat_open_input);
        LOAD_SYM(hFormat, avformat_find_stream_info);
        LOAD_SYM(hFormat, avformat_close_input);
        LOAD_SYM(hFormat, av_read_frame);

        LOAD_SYM(hCodec, avcodec_find_decoder);
        LOAD_SYM(hCodec, avcodec_alloc_context3);
        LOAD_SYM(hCodec, avcodec_parameters_to_context);
        LOAD_SYM(hCodec, avcodec_open2);
        LOAD_SYM(hCodec, avcodec_free_context);
        LOAD_SYM(hCodec, avcodec_send_packet);
        LOAD_SYM(hCodec, avcodec_receive_frame);

        LOAD_SYM(hCodec, av_packet_alloc);
        LOAD_SYM(hCodec, av_packet_free);
        LOAD_SYM(hCodec, av_packet_unref);

        LOAD_SYM(hCodec, av_frame_alloc);
        LOAD_SYM(hCodec, av_frame_free);
        LOAD_SYM(hCodec, av_frame_unref);

        #undef LOAD_SYM

        loaded = true;
        return true;
    }
};

static FFmpegLoader gFFmpeg;

// Minimal struct offsets from AVStream/AVCodecParameters
constexpr int AVMEDIA_TYPE_AUDIO = 1;

// Struct layout helpers for AVStream and AVCodecParameters
struct DummyAVCodecParameters {
    int codec_type;
    int codec_id;
    uint32_t codec_tag;
    uint8_t *extradata;
    int extradata_size;
    int format;
    int64_t bit_rate;
    int bits_per_coded_sample;
    int bits_per_raw_sample;
    int profile;
    int level;
    int width;
    int height;
    int64_t sample_aspect_ratio;
    int field_order;
    int color_range;
    int color_primaries;
    int color_trc;
    int color_space;
    int chroma_location;
    int video_delay;
    // Audio fields follow at offset >= 104
};

struct DummyAVStream {
    int index;
    int id;
    void *priv_data;
    struct { int64_t num; int64_t den; } time_base;
    int64_t start_time;
    int64_t duration;
    int64_t nb_frames;
    int disposition;
    int discard;
    DummyAVCodecParameters *codecpar;
};

struct DummyAVFormatContext {
    void *av_class;
    void *iformat;
    void *oformat;
    void *priv_data;
    void *pb;
    int ctx_flags;
    unsigned int nb_streams;
    DummyAVStream **streams;
};

// AVFrame audio sample formats
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

struct DummyAVFrame {
    uint8_t *data[8];
    int linesize[8];
    uint8_t **extended_data;
    int width, height;
    int nb_samples;
    int format;
    int key_frame;
};

DecodedAudioData decodeFilePathWithFFmpeg(const char *filePath) {
    DecodedAudioData result;
    if (!gFFmpeg.init()) {
        result.errorMessage = "FFmpeg libraries (libavformat/libavcodec) not available on system";
        return result;
    }

    // Step 1: Probe file header for accurate audio metadata
    int probedSampleRate = 0;
    int probedChannels = 0;
    FILE *probeFp = fopen(filePath, "rb");
    if (probeFp) {
        unsigned char probeBuf[16384];
        size_t n = fread(probeBuf, 1, sizeof(probeBuf), probeFp);
        fclose(probeFp);
        if (n >= 8) {
            if (NativeAudioDecoder::isM4aOrMp4(probeBuf, n)) {
                M4aMetadata m4a;
                if (parseM4aMetadata(probeBuf, n, m4a)) {
                    if (m4a.sampleRate > 0) probedSampleRate = m4a.sampleRate;
                    if (m4a.channels > 0) probedChannels = m4a.channels;
                }
            } else if (NativeAudioDecoder::isAacAdts(probeBuf, n)) {
                AacMetadata aac;
                if (AACDecoderWrapper::parseAacAdtsMetadata(probeBuf, n, aac)) {
                    if (aac.sampleRate > 0) probedSampleRate = aac.sampleRate;
                    if (aac.channels > 0) probedChannels = aac.channels;
                }
            } else if (NativeAudioDecoder::isAc3(probeBuf, n)) {
                Ac3Metadata ac3;
                if (AACDecoderWrapper::parseAc3Metadata(probeBuf, n, ac3)) {
                    if (ac3.sampleRate > 0) probedSampleRate = ac3.sampleRate;
                    if (ac3.channels > 0) probedChannels = ac3.channels;
                }
            } else if (NativeAudioDecoder::isEac3(probeBuf, n)) {
                Eac3Metadata eac3;
                if (AACDecoderWrapper::parseEac3Metadata(probeBuf, n, eac3)) {
                    if (eac3.sampleRate > 0) probedSampleRate = eac3.sampleRate;
                    if (eac3.channels > 0) probedChannels = eac3.channels;
                }
            }
        }
    }

    AVFormatContext *fmtCtx = nullptr;
    if (gFFmpeg.avformat_open_input(&fmtCtx, filePath, nullptr, nullptr) != 0 || !fmtCtx) {
        result.errorMessage = "avformat_open_input failed for: " + std::string(filePath);
        return result;
    }

    if (gFFmpeg.avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        gFFmpeg.avformat_close_input(&fmtCtx);
        result.errorMessage = "avformat_find_stream_info failed";
        return result;
    }

    DummyAVFormatContext *dummyCtx = reinterpret_cast<DummyAVFormatContext *>(fmtCtx);
    int audioStreamIndex = -1;
    DummyAVCodecParameters *codecPar = nullptr;

    for (unsigned int i = 0; i < dummyCtx->nb_streams; i++) {
        if (dummyCtx->streams[i] && dummyCtx->streams[i]->codecpar) {
            if (dummyCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                audioStreamIndex = i;
                codecPar = dummyCtx->streams[i]->codecpar;
                break;
            }
        }
    }

    if (audioStreamIndex < 0 || !codecPar) {
        gFFmpeg.avformat_close_input(&fmtCtx);
        result.errorMessage = "No audio stream found in file";
        return result;
    }

    const AVCodec *codec = gFFmpeg.avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        gFFmpeg.avformat_close_input(&fmtCtx);
        result.errorMessage = "Codec decoder not found for audio stream";
        return result;
    }

    AVCodecContext *codecCtx = gFFmpeg.avcodec_alloc_context3(codec);
    if (!codecCtx) {
        gFFmpeg.avformat_close_input(&fmtCtx);
        result.errorMessage = "avcodec_alloc_context3 failed";
        return result;
    }

    if (gFFmpeg.avcodec_parameters_to_context(codecCtx, reinterpret_cast<AVCodecParameters*>(codecPar)) < 0) {
        gFFmpeg.avcodec_free_context(&codecCtx);
        gFFmpeg.avformat_close_input(&fmtCtx);
        result.errorMessage = "avcodec_parameters_to_context failed";
        return result;
    }

    if (gFFmpeg.avcodec_open2(codecCtx, codec, nullptr) < 0) {
        gFFmpeg.avcodec_free_context(&codecCtx);
        gFFmpeg.avformat_close_input(&fmtCtx);
        result.errorMessage = "avcodec_open2 failed";
        return result;
    }

    // Step 2: Determine audio channels and sample rate
    int sampleRate = probedSampleRate;
    int channels = probedChannels;

    if (sampleRate <= 0 || channels <= 0) {
        const uint8_t *parBytes = reinterpret_cast<const uint8_t *>(codecPar);
        // Try FFmpeg 5.1+ layout: nb_channels at 108, sample_rate at 128
        int v5_ch = *reinterpret_cast<const int *>(parBytes + 108);
        int v5_sr = *reinterpret_cast<const int *>(parBytes + 128);
        if (v5_ch >= 1 && v5_ch <= 32 && v5_sr >= 4000 && v5_sr <= 384000) {
            if (channels <= 0) channels = v5_ch;
            if (sampleRate <= 0) sampleRate = v5_sr;
        } else {
            // Try FFmpeg 4.x layout: channels at 112, sample_rate at 116
            int v4_ch = *reinterpret_cast<const int *>(parBytes + 112);
            int v4_sr = *reinterpret_cast<const int *>(parBytes + 116);
            if (v4_ch >= 1 && v4_ch <= 32 && v4_sr >= 4000 && v4_sr <= 384000) {
                if (channels <= 0) channels = v4_ch;
                if (sampleRate <= 0) sampleRate = v4_sr;
            }
        }
    }

    if (sampleRate <= 0) sampleRate = 44100;
    if (channels <= 0) channels = 2;

    AVPacket *pkt = gFFmpeg.av_packet_alloc();
    AVFrame *frame = gFFmpeg.av_frame_alloc();

    std::vector<float> planarChannelData[8];
    for (int c = 0; c < channels && c < 8; c++) {
        planarChannelData[c].reserve(44100 * 5); // 5 sec initial reserve
    }

    auto processFrame = [&](AVFrame *f) {
        DummyAVFrame *dFrame = reinterpret_cast<DummyAVFrame*>(f);
        int nbSamples = dFrame->nb_samples;
        int fmt = dFrame->format;
        if (nbSamples <= 0) return;

        if (fmt == AV_SAMPLE_FMT_FLTP) {
            for (int c = 0; c < channels && c < 8; c++) {
                const float *src = reinterpret_cast<const float *>(dFrame->extended_data ? dFrame->extended_data[c] : dFrame->data[c]);
                if (src) {
                    planarChannelData[c].insert(planarChannelData[c].end(), src, src + nbSamples);
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_FLT) {
            const float *src = reinterpret_cast<const float *>(dFrame->data[0]);
            if (src) {
                for (int s = 0; s < nbSamples; s++) {
                    for (int c = 0; c < channels && c < 8; c++) {
                        planarChannelData[c].push_back(src[s * channels + c]);
                    }
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_S16P) {
            for (int c = 0; c < channels && c < 8; c++) {
                const int16_t *src = reinterpret_cast<const int16_t *>(dFrame->extended_data ? dFrame->extended_data[c] : dFrame->data[c]);
                if (src) {
                    for (int s = 0; s < nbSamples; s++) {
                        planarChannelData[c].push_back(src[s] / 32768.0f);
                    }
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_S16) {
            const int16_t *src = reinterpret_cast<const int16_t *>(dFrame->data[0]);
            if (src) {
                for (int s = 0; s < nbSamples; s++) {
                    for (int c = 0; c < channels && c < 8; c++) {
                        planarChannelData[c].push_back(src[s * channels + c] / 32768.0f);
                    }
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_S32P) {
            for (int c = 0; c < channels && c < 8; c++) {
                const int32_t *src = reinterpret_cast<const int32_t *>(dFrame->extended_data ? dFrame->extended_data[c] : dFrame->data[c]);
                if (src) {
                    for (int s = 0; s < nbSamples; s++) {
                        planarChannelData[c].push_back(src[s] / 2147483648.0f);
                    }
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_S32) {
            const int32_t *src = reinterpret_cast<const int32_t *>(dFrame->data[0]);
            if (src) {
                for (int s = 0; s < nbSamples; s++) {
                    for (int c = 0; c < channels && c < 8; c++) {
                        planarChannelData[c].push_back(src[s * channels + c] / 2147483648.0f);
                    }
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_DBLP) {
            for (int c = 0; c < channels && c < 8; c++) {
                const double *src = reinterpret_cast<const double *>(dFrame->extended_data ? dFrame->extended_data[c] : dFrame->data[c]);
                if (src) {
                    for (int s = 0; s < nbSamples; s++) {
                        planarChannelData[c].push_back(static_cast<float>(src[s]));
                    }
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_DBL) {
            const double *src = reinterpret_cast<const double *>(dFrame->data[0]);
            if (src) {
                for (int s = 0; s < nbSamples; s++) {
                    for (int c = 0; c < channels && c < 8; c++) {
                        planarChannelData[c].push_back(static_cast<float>(src[s * channels + c]));
                    }
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_U8P) {
            for (int c = 0; c < channels && c < 8; c++) {
                const uint8_t *src = reinterpret_cast<const uint8_t *>(dFrame->extended_data ? dFrame->extended_data[c] : dFrame->data[c]);
                if (src) {
                    for (int s = 0; s < nbSamples; s++) {
                        planarChannelData[c].push_back((src[s] - 128) / 128.0f);
                    }
                }
            }
        } else if (fmt == AV_SAMPLE_FMT_U8) {
            const uint8_t *src = reinterpret_cast<const uint8_t *>(dFrame->data[0]);
            if (src) {
                for (int s = 0; s < nbSamples; s++) {
                    for (int c = 0; c < channels && c < 8; c++) {
                        planarChannelData[c].push_back((src[s * channels + c] - 128) / 128.0f);
                    }
                }
            }
        }
    };

    while (gFFmpeg.av_read_frame(fmtCtx, pkt) >= 0) {
        int sendRes = gFFmpeg.avcodec_send_packet(codecCtx, pkt);
        gFFmpeg.av_packet_unref(pkt);

        if (sendRes == 0) {
            while (gFFmpeg.avcodec_receive_frame(codecCtx, frame) == 0) {
                processFrame(frame);
                gFFmpeg.av_frame_unref(frame);
            }
        }
    }

    // Flush remaining frames from decoder
    if (gFFmpeg.avcodec_send_packet(codecCtx, nullptr) == 0) {
        while (gFFmpeg.avcodec_receive_frame(codecCtx, frame) == 0) {
            processFrame(frame);
            gFFmpeg.av_frame_unref(frame);
        }
    }

    gFFmpeg.av_packet_free(&pkt);
    gFFmpeg.av_frame_free(&frame);
    gFFmpeg.avcodec_free_context(&codecCtx);
    gFFmpeg.avformat_close_input(&fmtCtx);

    size_t totalFrames = planarChannelData[0].size();
    if (totalFrames == 0) {
        result.errorMessage = "Decoded 0 audio frames with FFmpeg";
        return result;
    }

    float *planarBuffer = new (std::nothrow) float[totalFrames * channels];
    if (!planarBuffer) {
        result.errorMessage = "Out of memory allocating planar buffer";
        return result;
    }

    for (int c = 0; c < channels; c++) {
        std::memcpy(planarBuffer + c * totalFrames, planarChannelData[c].data(), totalFrames * sizeof(float));
    }

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
    return decodeFilePathWithFFmpeg(filePath);
}

DecodedAudioData NativeAudioDecoder::decodeMemory(const unsigned char *bytes, size_t length) {
    DecodedAudioData result;
    if (!bytes || length == 0) {
        result.errorMessage = "Empty memory buffer";
        return result;
    }

    // Determine extension hint so FFmpeg demuxes without guessing
    const char *ext = ".bin";
    if (NativeAudioDecoder::isM4aOrMp4(bytes, length)) {
        ext = ".m4a";
    } else if (NativeAudioDecoder::isAacAdts(bytes, length)) {
        ext = ".aac";
    } else if (NativeAudioDecoder::isAc3(bytes, length)) {
        ext = ".ac3";
    } else if (NativeAudioDecoder::isEac3(bytes, length)) {
        ext = ".eac3";
    }

    char tempPath[128];
    snprintf(tempPath, sizeof(tempPath), "/tmp/soloud_media_XXXXXX%s", ext);
    int fd = mkstemps(tempPath, static_cast<int>(std::strlen(ext)));
    if (fd < 0) {
        // Fallback to mkstemp if mkstemps fails
        char fallbackPath[] = "/tmp/soloud_media_XXXXXX";
        fd = mkstemp(fallbackPath);
        if (fd < 0) {
            result.errorMessage = "Failed to create temporary file for Linux decoding";
            return result;
        }
        std::strncpy(tempPath, fallbackPath, sizeof(tempPath) - 1);
        tempPath[sizeof(tempPath) - 1] = '\0';
    }

    size_t written = write(fd, bytes, length);
    close(fd);

    if (written != length) {
        unlink(tempPath);
        result.errorMessage = "Failed to write buffer to temporary file";
        return result;
    }

    result = decodeFilePathWithFFmpeg(tempPath);
    unlink(tempPath);
    return result;
}

#endif // defined(__linux__) && !defined(__ANDROID__)
