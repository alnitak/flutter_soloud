#if defined(__linux__) && !defined(__ANDROID__)

#include "native_audio_decoder.h"
#include <dlfcn.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>

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

// Function pointers for dynamic loading from libavformat, libavcodec, libswresample
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
    void *hSwr = nullptr;

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
// AVMediaType enum: AVMEDIA_TYPE_AUDIO is 1 in FFmpeg across all modern versions.
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
    int ch_layout_or_channels; // Depending on FFmpeg version
    int sample_rate;
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
constexpr int AV_SAMPLE_FMT_FLT = 3;
constexpr int AV_SAMPLE_FMT_FLTP = 8;
constexpr int AV_SAMPLE_FMT_S16 = 1;
constexpr int AV_SAMPLE_FMT_S16P = 6;

struct DummyAVFrame {
    uint8_t *data[8];
    int linesize[8];
    uint8_t **extended_data;
    int width, height;
    int nb_samples;
    int format;
    int key_frame;
    // ... remaining fields
};

DecodedAudioData decodeFilePathWithFFmpeg(const char *filePath) {
    DecodedAudioData result;
    if (!gFFmpeg.init()) {
        result.errorMessage = "FFmpeg libraries (libavformat/libavcodec) not available on system";
        return result;
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

    AVPacket *pkt = gFFmpeg.av_packet_alloc();
    AVFrame *frame = gFFmpeg.av_frame_alloc();

    int sampleRate = codecPar->sample_rate > 0 ? codecPar->sample_rate : 44100;
    int channels = codecPar->ch_layout_or_channels > 0 ? codecPar->ch_layout_or_channels : 2;

    std::vector<float> planarChannelData[8];
    for (int c = 0; c < channels && c < 8; c++) {
        planarChannelData[c].reserve(44100 * 5); // 5 sec initial reserve
    }

    while (gFFmpeg.av_read_frame(fmtCtx, pkt) >= 0) {
        // AVPacket offset for stream_index is at offset sizeof(AVBufferRef*) + 4 + ...
        // In FFmpeg AVPacket, stream_index is standard at offset after buf, pts, dts, data, size.
        // We can pass packet to decoder; avcodec_send_packet will decode.
        int sendRes = gFFmpeg.avcodec_send_packet(codecCtx, pkt);
        gFFmpeg.av_packet_unref(pkt);

        if (sendRes == 0) {
            while (gFFmpeg.avcodec_receive_frame(codecCtx, frame) == 0) {
                DummyAVFrame *dFrame = reinterpret_cast<DummyAVFrame*>(frame);
                int nbSamples = dFrame->nb_samples;
                int fmt = dFrame->format;

                // AAC typically decodes into AV_SAMPLE_FMT_FLTP (planar float)
                if (fmt == AV_SAMPLE_FMT_FLTP) {
                    for (int c = 0; c < channels && c < 8; c++) {
                        const float *src = reinterpret_cast<const float *>(dFrame->extended_data ? dFrame->extended_data[c] : dFrame->data[c]);
                        if (src) {
                            planarChannelData[c].insert(planarChannelData[c].end(), src, src + nbSamples);
                        }
                    }
                } else if (fmt == AV_SAMPLE_FMT_FLT) {
                    // Interleaved float
                    const float *src = reinterpret_cast<const float *>(dFrame->data[0]);
                    for (int s = 0; s < nbSamples; s++) {
                        for (int c = 0; c < channels && c < 8; c++) {
                            planarChannelData[c].push_back(src[s * channels + c]);
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
                    for (int s = 0; s < nbSamples; s++) {
                        for (int c = 0; c < channels && c < 8; c++) {
                            planarChannelData[c].push_back(src[s * channels + c] / 32768.0f);
                        }
                    }
                }

                gFFmpeg.av_frame_unref(frame);
            }
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

    // Write to a temporary file descriptor on Linux
    char tempPath[] = "/tmp/soloud_media_XXXXXX";
    int fd = mkstemp(tempPath);
    if (fd < 0) {
        result.errorMessage = "Failed to create temporary file for Linux decoding";
        return result;
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
