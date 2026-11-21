// Standalone native tests for the mixer-output FLAC encoder
// (src/mixeroutput/flac_output_encoder.cpp, class FlacOutputEncoder),
// decoded back with the system libFLAC stream decoder.
//
// The encoder supports a single bit depth: it always writes 16-bit FLAC
// (float input scaled by 32767, clamped, truncated toward zero). There is no
// 24-bit option, so "all bit depths" means checking that 16 is what comes
// out.
//
//   * roundtrip: mono, stereo, 6 and 8 channels at 22.05/44.1/48/96 kHz, fed in
//     uneven chunk sizes; the decoded stream has the right channel count,
//     rate and 16 bits, exactly as many frames as were encoded, and every
//     sample equals trunc(clamp(x, -1, 1) * 32767) (lossless, so exact),
//     including clipping (+-1.5, +-Inf) and NaN -> 0. 8-channel input in
//     SoLoud's 7.1 order (FL FR C LFE SL SR BL BR) comes out in FLAC's 7.1
//     order (FL FR C LFE BL BR SL SR): the side and back pairs are swapped
//     (bug O1); 1, 2 and 6 channels already match and are kept as-is;
//   * flush: bytes emitted before finalize() decode to fewer frames than
//     were encoded (the encoder holds back a partial block); finalize()
//     emits the rest and the full stream decodes completely;
//   * empty: initialize + finalize with no audio is a valid stream with zero
//     frames;
//   * lifecycle: encode before initialize fails; invalid channel counts are
//     rejected; a partial trailing frame is dropped; re-initialize after
//     finalize starts a new valid stream; a second initialize while running
//     is a no-op that keeps the first format.
//
// No audio device and no assets are needed. Needs libFLAC headers and library
// (Homebrew `flac` on macOS, libflac-dev on Linux); if they are missing the
// script prints SKIPPED.
//
//   ./test/common/flac_output_encoder_test.sh [--asan] [scenario]
//
// --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. A
// scenario name runs only that scenario. XIPH_PREFIX names where libFLAC is
// installed.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <FLAC/stream_decoder.h>

#include "mixeroutput/flac_output_encoder.h"

namespace {

int gFailures = 0;
int gAssertions = 0;

#define CHECK(condition, format, ...)                                     \
    do                                                                    \
    {                                                                     \
        ++gAssertions;                                                    \
        if (!(condition))                                                 \
        {                                                                 \
            ++gFailures;                                                  \
            std::fprintf(stderr, "  FAIL [%s:%d] " format "\n", __FILE__, \
                         __LINE__, ##__VA_ARGS__);                        \
        }                                                                 \
    } while (0)

int32_t refS16(float x)
{
    if (std::isnan(x)) return 0;
    x = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
    return static_cast<int32_t>(std::trunc(x * 32767.0f));
}

std::vector<float> makeSignal(size_t frames, int channels, int rate)
{
    const float edges[] = {1.0f, -1.0f, 1.5f, -1.5f,
                           std::numeric_limits<float>::quiet_NaN(),
                           std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity(), 0.0f};
    std::vector<float> v(frames * channels);
    for (size_t f = 0; f < frames; ++f)
        for (int c = 0; c < channels; ++c)
        {
            const size_t i = f * channels + c;
            if (i % 997 == 13)
                v[i] = edges[(i / 997) % (sizeof(edges) / sizeof(edges[0]))];
            else
                v[i] = 0.7f * std::sin(2.0f * 3.14159265f * (220.0f * (c + 1)) *
                                       static_cast<float>(f) / rate) +
                       0.05f * std::sin(static_cast<float>(i) * 1.3f);
        }
    return v;
}

struct Decoded
{
    bool ok = false;
    unsigned channels = 0, rate = 0, bps = 0;
    uint64_t streamInfoTotal = ~0ull;
    bool sawStreamInfo = false;
    bool formatChanged = false;
    std::vector<int32_t> samples;  // interleaved
};

struct DecodeCtx
{
    const std::vector<uint8_t> *data;
    size_t pos = 0;
    Decoded *out;
    bool error = false;
};

FLAC__StreamDecoderReadStatus readCb(const FLAC__StreamDecoder *, FLAC__byte buffer[],
                                     size_t *bytes, void *client)
{
    auto *ctx = static_cast<DecodeCtx *>(client);
    const size_t left = ctx->data->size() - ctx->pos;
    if (left == 0)
    {
        *bytes = 0;
        return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    }
    const size_t n = left < *bytes ? left : *bytes;
    std::memcpy(buffer, ctx->data->data() + ctx->pos, n);
    ctx->pos += n;
    *bytes = n;
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

FLAC__StreamDecoderWriteStatus writeCb(const FLAC__StreamDecoder *, const FLAC__Frame *frame,
                                       const FLAC__int32 *const buffer[], void *client)
{
    auto *ctx = static_cast<DecodeCtx *>(client);
    Decoded *d = ctx->out;
    const unsigned ch = frame->header.channels;
    if ((d->channels != 0 && d->channels != ch) ||
        (d->bps != 0 && d->bps != frame->header.bits_per_sample) ||
        (d->rate != 0 && d->rate != frame->header.sample_rate))
        d->formatChanged = true;
    d->channels = ch;
    d->bps = frame->header.bits_per_sample;
    d->rate = frame->header.sample_rate;
    for (unsigned i = 0; i < frame->header.blocksize; ++i)
        for (unsigned c = 0; c < ch; ++c) d->samples.push_back(buffer[c][i]);
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void metaCb(const FLAC__StreamDecoder *, const FLAC__StreamMetadata *m, void *client)
{
    auto *ctx = static_cast<DecodeCtx *>(client);
    if (m->type == FLAC__METADATA_TYPE_STREAMINFO)
    {
        ctx->out->sawStreamInfo = true;
        ctx->out->streamInfoTotal = m->data.stream_info.total_samples;
        if (ctx->out->channels == 0)
        {
            ctx->out->channels = m->data.stream_info.channels;
            ctx->out->rate = m->data.stream_info.sample_rate;
            ctx->out->bps = m->data.stream_info.bits_per_sample;
        }
    }
}

void errorCb(const FLAC__StreamDecoder *, FLAC__StreamDecoderErrorStatus status, void *client)
{
    auto *ctx = static_cast<DecodeCtx *>(client);
    // A truncated stream (flush test) may end mid-frame; only count real
    // errors while the input is not exhausted.
    if (ctx->pos < ctx->data->size()) ctx->error = true;
    (void)status;
}

Decoded decodeFlac(const std::vector<uint8_t> &bytes, bool allowTruncated = false)
{
    Decoded d;
    DecodeCtx ctx;
    ctx.data = &bytes;
    ctx.out = &d;
    FLAC__StreamDecoder *dec = FLAC__stream_decoder_new();
    if (dec == nullptr) return d;
    if (FLAC__stream_decoder_init_stream(dec, readCb, nullptr, nullptr, nullptr, nullptr,
                                         writeCb, metaCb, errorCb, &ctx) !=
        FLAC__STREAM_DECODER_INIT_STATUS_OK)
    {
        FLAC__stream_decoder_delete(dec);
        return d;
    }
    const bool processed = FLAC__stream_decoder_process_until_end_of_stream(dec) != 0;
    const FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(dec);
    FLAC__stream_decoder_finish(dec);
    FLAC__stream_decoder_delete(dec);
    d.ok = (processed || allowTruncated) && !ctx.error && d.sawStreamInfo &&
           (allowTruncated || state == FLAC__STREAM_DECODER_END_OF_STREAM);
    return d;
}

bool startsWithFlacMagic(const std::vector<uint8_t> &b)
{
    return b.size() >= 4 && std::memcmp(b.data(), "fLaC", 4) == 0;
}

void roundtrip(int rate, int ch, size_t frames)
{
    char ctx[64];
    std::snprintf(ctx, sizeof ctx, "%d Hz %d ch %zu frames", rate, ch, frames);
    std::vector<float> sig = makeSignal(frames, ch, rate);

    FlacOutputEncoder enc;
    CHECK(enc.inputBytesPerSample() == sizeof(float), "%s: input bps", ctx);
    if (!enc.initialize(rate, ch))
    {
        CHECK(false, "%s: initialize failed", ctx);
        return;
    }
    std::vector<uint8_t> stream, out;
    const size_t chunkFrames[] = {1, 3, 64, 441, 512, 1024, 2047, 4096, 5000};
    size_t f = 0, k = 0;
    while (f < frames)
    {
        size_t n = chunkFrames[k++ % (sizeof chunkFrames / sizeof chunkFrames[0])];
        if (n > frames - f) n = frames - f;
        out.clear();
        CHECK(enc.encode(sig.data() + f * ch, n * ch, out), "%s: encode at %zu", ctx, f);
        stream.insert(stream.end(), out.begin(), out.end());
        f += n;
    }
    out.clear();
    CHECK(enc.finalize(out), "%s: finalize", ctx);
    stream.insert(stream.end(), out.begin(), out.end());

    CHECK(startsWithFlacMagic(stream), "%s: no fLaC magic", ctx);
    Decoded d = decodeFlac(stream);
    CHECK(d.ok, "%s: decode failed", ctx);
    CHECK(!d.formatChanged, "%s: format changed mid-stream", ctx);
    CHECK(d.channels == static_cast<unsigned>(ch), "%s: channels %u", ctx, d.channels);
    CHECK(d.rate == static_cast<unsigned>(rate), "%s: rate %u", ctx, d.rate);
    CHECK(d.bps == 16, "%s: bits per sample %u (only 16 is supported)", ctx, d.bps);
    CHECK(d.samples.size() == sig.size(), "%s: decoded %zu samples, encoded %zu", ctx,
          d.samples.size(), sig.size());
    // FLAC channel c of a frame holds SoLoud channel flacToSoLoud(c).
    auto flacToSoLoud = [ch](size_t c) -> size_t {
        static const size_t k71[8] = {0, 1, 2, 3, 6, 7, 4, 5};
        return ch == 8 ? k71[c] : c;
    };
    int bad = 0;
    for (size_t i = 0; i < sig.size() && i < d.samples.size() && bad < 5; ++i)
    {
        const size_t src = (i / ch) * ch + flacToSoLoud(i % ch);
        if (d.samples[i] != refS16(sig[src]))
        {
            ++bad;
            CHECK(false, "%s: sample %zu in=%g got %d want %d", ctx, i, sig[src],
                  d.samples[i], refS16(sig[src]));
        }
    }
    // STREAMINFO cannot be rewritten (no seek callback on a streamed
    // capture), so total_samples stays 0 = "unknown". Documented, not a bug.
    CHECK(d.streamInfoTotal == 0, "%s: STREAMINFO total_samples %llu (expected 0 = unknown)",
          ctx, static_cast<unsigned long long>(d.streamInfoTotal));
}

void testRoundtrip()
{
    for (int ch : {1, 2, 6, 8})
        for (int rate : {22050, 44100, 48000, 96000})
            roundtrip(rate, ch, static_cast<size_t>(rate) / 2 + 123);
    roundtrip(44100, 2, 1);     // shorter than one block
    roundtrip(44100, 1, 4096);  // exactly one default block
}

void testFlush()
{
    const int rate = 44100, ch = 2;
    const size_t frames = 10000;  // not a multiple of the block size
    std::vector<float> sig = makeSignal(frames, ch, rate);
    FlacOutputEncoder enc;
    enc.initialize(rate, ch);
    std::vector<uint8_t> stream, out;
    CHECK(enc.encode(sig.data(), sig.size(), out), "encode");
    stream = out;
    Decoded partial = decodeFlac(stream, true);
    CHECK(partial.samples.size() < sig.size(),
          "all %zu samples were emitted before finalize: nothing to flush?",
          sig.size());
    CHECK(partial.samples.size() % ch == 0, "partial decode not frame aligned");

    out.clear();
    CHECK(enc.finalize(out), "finalize");
    CHECK(!out.empty(), "finalize emitted no bytes");
    stream.insert(stream.end(), out.begin(), out.end());
    Decoded full = decodeFlac(stream);
    CHECK(full.ok, "full decode failed");
    CHECK(full.samples.size() == sig.size(), "after flush decoded %zu of %zu",
          full.samples.size(), sig.size());

    // finalize() twice: the second call has no encoder any more.
    out.clear();
    CHECK(!enc.finalize(out), "second finalize succeeded");
    CHECK(out.empty(), "second finalize emitted bytes");
}

void testEmpty()
{
    FlacOutputEncoder enc;
    CHECK(enc.initialize(48000, 2), "initialize");
    std::vector<uint8_t> out;
    CHECK(enc.finalize(out), "finalize");
    CHECK(startsWithFlacMagic(out), "empty stream lacks the fLaC header");
    Decoded d = decodeFlac(out);
    CHECK(d.ok, "empty stream does not decode");
    CHECK(d.samples.empty(), "empty stream decoded %zu samples", d.samples.size());
    CHECK(d.channels == 2 && d.rate == 48000 && d.bps == 16,
          "empty stream STREAMINFO %u ch %u Hz %u bits", d.channels, d.rate, d.bps);
}

void testLifecycle()
{
    const float x[8] = {0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f, 0.4f, -0.4f};
    {
        FlacOutputEncoder enc;
        std::vector<uint8_t> out;
        CHECK(!enc.encode(x, 8, out), "encode before initialize succeeded");
        CHECK(!enc.finalize(out), "finalize before initialize succeeded");
    }
    {
        FlacOutputEncoder enc;
        CHECK(!enc.initialize(44100, 0), "0 channels accepted");
        FlacOutputEncoder enc9;
        CHECK(!enc9.initialize(44100, 9), "9 channels accepted (FLAC max is 8)");
    }
    {
        // Fewer samples than one frame: success, nothing written now.
        FlacOutputEncoder enc;
        enc.initialize(44100, 2);
        std::vector<uint8_t> out{9, 9};
        CHECK(enc.encode(x, 1, out), "sub-frame encode failed");
        CHECK(out.empty(), "sub-frame encode left %zu bytes", out.size());
        CHECK(!enc.encode(nullptr, 2, out), "encode(nullptr) succeeded");
        CHECK(!enc.encode(x, 0, out), "encode of 0 samples succeeded");

        // A trailing partial frame is dropped: 7 samples stereo = 3 frames.
        std::vector<uint8_t> stream;
        out.clear();
        enc.encode(x, 7, out);
        stream.insert(stream.end(), out.begin(), out.end());
        out.clear();
        enc.finalize(out);
        stream.insert(stream.end(), out.begin(), out.end());
        Decoded d = decodeFlac(stream);
        CHECK(d.ok && d.samples.size() == 6, "partial frame: decoded %zu samples (want 6)",
              d.samples.size());

        // Re-initialize after finalize: a fresh, independent stream.
        CHECK(enc.initialize(22050, 1), "re-initialize after finalize");
        stream.clear();
        out.clear();
        enc.encode(x, 8, out);
        stream.insert(stream.end(), out.begin(), out.end());
        out.clear();
        enc.finalize(out);
        stream.insert(stream.end(), out.begin(), out.end());
        Decoded d2 = decodeFlac(stream);
        CHECK(d2.ok && d2.channels == 1 && d2.rate == 22050 && d2.samples.size() == 8,
              "second stream: ok=%d %u ch %u Hz %zu samples", d2.ok, d2.channels, d2.rate,
              d2.samples.size());
    }
    {
        // A second initialize while running is a no-op returning true; the
        // first format stays (documents current behaviour).
        FlacOutputEncoder enc;
        enc.initialize(44100, 2);
        CHECK(enc.initialize(48000, 1), "second initialize returned false");
        std::vector<uint8_t> stream, out;
        enc.encode(x, 8, out);
        stream = out;
        out.clear();
        enc.finalize(out);
        stream.insert(stream.end(), out.begin(), out.end());
        Decoded d = decodeFlac(stream);
        CHECK(d.ok && d.channels == 2 && d.rate == 44100,
              "second initialize changed the format (%u ch %u Hz)", d.channels, d.rate);
    }
    {
        // WAV-only hooks keep the base-class defaults.
        FlacOutputEncoder flac;
        MixerOutputEncoder &enc = flac;
        CHECK(enc.initialize(44100, 2), "initialize");
        std::vector<uint8_t> h{1};
        CHECK(!enc.writeCurrentHeader(h) && h.empty(),
              "FLAC writeCurrentHeader should be the no-op default");
        CHECK(enc.totalPayloadBytes() == 0, "FLAC totalPayloadBytes");
    }
}

struct Scenario
{
    const char *name;
    void (*run)();
};

const Scenario kScenarios[] = {
    {"roundtrip", testRoundtrip},
    {"flush", testFlush},
    {"empty", testEmpty},
    {"lifecycle", testLifecycle},
};

}  // namespace

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (const Scenario &s : kScenarios)
    {
        if (only != nullptr && std::strcmp(only, s.name) != 0) continue;
        const int before = gFailures;
        s.run();
        ++ran;
        std::printf("%s %s\n", gFailures == before ? "PASS" : "FAIL", s.name);
        std::fflush(stdout);
    }
    if (ran == 0)
    {
        std::fprintf(stderr, "unknown scenario: %s\n", only);
        return 2;
    }
    std::printf("%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}
