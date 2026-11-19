// Standalone native tests for the mixer-output WAV encoder
// (src/mixeroutput/wav_output_encoder.cpp, class WavOutputEncoder).
//
// The encoder only produces 16-bit integer PCM (WAVE_FORMAT_PCM, 16 bits per
// sample) whatever the capture format: there is no int24 or float32 WAV
// output, so these tests pin the int16 layout and check that nothing else is
// ever emitted.
//
//   * header: the 44-byte RIFF/WAVE header for mono, stereo and 6 channels at
//     8/22.05/44.1/48/96 kHz: "RIFF", chunk size = 36 + data size, "WAVE",
//     "fmt " of size 16, format 1, channels, sample rate,
//     byte rate = rate * channels * 2, block align = channels * 2,
//     16 bits, "data" with the payload size;
//   * sizes: totalPcmBytes()/totalPayloadBytes() and the header track the
//     samples encoded over many chunks; writeHeader() with an explicit size
//     uses that size; header + payload parses back to the same frame count;
//   * samples: payload values are little-endian int16 equal to
//     trunc(clamp(x, -1, 1) * 32767), NaN -> 0, at lengths that exercise the
//     SIMD body and the scalar tail;
//   * lifecycle: encode before initialize fails; finalize emits nothing;
//     initialize again resets the counters; encode replaces (does not
//     append to) its output vector.
//
// No audio device and no assets are needed.
//
//   ./test/common/wav_output_encoder_test.sh [--asan] [scenario]
//
// --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. A
// scenario name runs only that scenario.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "mixeroutput/wav_output_encoder.h"

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

uint32_t le32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t le16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
int16_t s16(const uint8_t *p) { return static_cast<int16_t>(le16(p)); }

int16_t refS16(float x)
{
    if (std::isnan(x)) return 0;
    x = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
    return static_cast<int16_t>(std::trunc(x * 32767.0f));
}

// Checks every header field; returns the data size field.
uint32_t checkHeader(const std::vector<uint8_t> &h, int rate, int ch,
                     uint32_t dataSize, const char *ctx)
{
    CHECK(h.size() == 44, "%s: header size %zu", ctx, h.size());
    if (h.size() < 44) return 0;
    const uint8_t *p = h.data();
    CHECK(std::memcmp(p, "RIFF", 4) == 0, "%s: RIFF tag", ctx);
    CHECK(le32(p + 4) == 36 + dataSize, "%s: chunk size %u want %u", ctx,
          le32(p + 4), 36 + dataSize);
    CHECK(std::memcmp(p + 8, "WAVE", 4) == 0, "%s: WAVE tag", ctx);
    CHECK(std::memcmp(p + 12, "fmt ", 4) == 0, "%s: fmt tag", ctx);
    CHECK(le32(p + 16) == 16, "%s: fmt size %u", ctx, le32(p + 16));
    CHECK(le16(p + 20) == 1, "%s: audio format %u (want 1 = PCM)", ctx, le16(p + 20));
    CHECK(le16(p + 22) == ch, "%s: channels %u want %d", ctx, le16(p + 22), ch);
    CHECK(le32(p + 24) == static_cast<uint32_t>(rate), "%s: rate %u want %d", ctx,
          le32(p + 24), rate);
    CHECK(le32(p + 28) == static_cast<uint32_t>(rate * ch * 2),
          "%s: byte rate %u want %d", ctx, le32(p + 28), rate * ch * 2);
    CHECK(le16(p + 32) == ch * 2, "%s: block align %u want %d", ctx, le16(p + 32), ch * 2);
    CHECK(le16(p + 34) == 16, "%s: bits per sample %u", ctx, le16(p + 34));
    CHECK(std::memcmp(p + 36, "data", 4) == 0, "%s: data tag", ctx);
    CHECK(le32(p + 40) == dataSize, "%s: data size %u want %u", ctx, le32(p + 40), dataSize);
    return le32(p + 40);
}

std::vector<float> makeSignal(size_t samples, int channels)
{
    const float edges[] = {0.0f, 1.0f, -1.0f, 1.5f, -1.5f,
                           std::numeric_limits<float>::quiet_NaN(),
                           std::numeric_limits<float>::infinity(), 0.5f, -0.5f};
    std::vector<float> v(samples);
    for (size_t i = 0; i < samples; ++i)
    {
        const size_t frame = i / channels, c = i % channels;
        if (i % 11 == 5)
            v[i] = edges[(i / 11) % (sizeof(edges) / sizeof(edges[0]))];
        else
            v[i] = 0.8f * std::sin(0.01f * static_cast<float>(frame) * (c + 1));
    }
    return v;
}

void testHeader()
{
    const int rates[] = {8000, 22050, 44100, 48000, 96000};
    const int chans[] = {1, 2, 6};
    for (int rate : rates)
        for (int ch : chans)
        {
            char ctx[64];
            std::snprintf(ctx, sizeof ctx, "%d Hz %d ch", rate, ch);
            WavOutputEncoder enc;
            CHECK(enc.initialize(rate, ch), "%s: initialize", ctx);
            CHECK(enc.inputBytesPerSample() == sizeof(float), "%s: input bps", ctx);

            std::vector<uint8_t> h;
            CHECK(enc.writeCurrentHeader(h), "%s: writeCurrentHeader", ctx);
            checkHeader(h, rate, ch, 0, ctx);

            std::vector<float> sig = makeSignal(static_cast<size_t>(ch) * 1000, ch);
            std::vector<uint8_t> out;
            CHECK(enc.encode(sig.data(), sig.size(), out), "%s: encode", ctx);
            CHECK(out.size() == sig.size() * 2, "%s: payload %zu", ctx, out.size());
            enc.writeHeader(h);
            checkHeader(h, rate, ch, static_cast<uint32_t>(ch * 2000), ctx);
        }
}

void testSizes()
{
    const int ch = 2, rate = 44100;
    WavOutputEncoder enc;
    enc.initialize(rate, ch);
    std::vector<uint8_t> file;
    std::vector<uint8_t> h;
    enc.writeCurrentHeader(h);
    file.insert(file.end(), h.begin(), h.end());

    // Uneven chunk sizes (in frames), like a device callback would deliver.
    const size_t chunkFrames[] = {1, 2, 3, 7, 64, 127, 128, 441, 512, 1000, 4096};
    size_t total = 0;
    std::vector<float> all;
    for (size_t rep = 0; rep < 3; ++rep)
        for (size_t f : chunkFrames)
        {
            std::vector<float> sig = makeSignal(f * ch, ch);
            std::vector<uint8_t> out;
            CHECK(enc.encode(sig.data(), sig.size(), out), "encode %zu frames", f);
            CHECK(out.size() == f * ch * 2, "chunk of %zu frames gave %zu bytes", f, out.size());
            file.insert(file.end(), out.begin(), out.end());
            all.insert(all.end(), sig.begin(), sig.end());
            total += f * ch * 2;
            CHECK(enc.totalPcmBytes() == total, "totalPcmBytes %zu want %zu",
                  enc.totalPcmBytes(), total);
            CHECK(enc.totalPayloadBytes() == total, "totalPayloadBytes");
        }

    std::vector<uint8_t> fin{1, 2, 3};
    CHECK(enc.finalize(fin), "finalize");
    CHECK(fin.empty(), "finalize emitted %zu bytes", fin.size());

    // Patch the header as MixerOutput does after capture stops.
    enc.writeCurrentHeader(h);
    std::memcpy(file.data(), h.data(), 44);
    checkHeader(h, rate, ch, static_cast<uint32_t>(total), "final");

    // Parse the file back.
    CHECK(file.size() == 44 + total, "file size %zu", file.size());
    const uint32_t dataSize = le32(file.data() + 40);
    const uint16_t blockAlign = le16(file.data() + 32);
    CHECK(dataSize % blockAlign == 0, "data size not a whole number of frames");
    CHECK(dataSize / blockAlign == all.size() / ch, "frames %u want %zu",
          dataSize / blockAlign, all.size() / ch);
    int bad = 0;
    for (size_t i = 0; i < all.size() && bad < 5; ++i)
    {
        const int16_t got = s16(file.data() + 44 + i * 2);
        if (got != refS16(all[i]))
        {
            ++bad;
            CHECK(false, "sample %zu: got %d want %d", i, got, refS16(all[i]));
        }
    }

    // Explicit size overrides the accumulated one.
    enc.writeHeader(h, 1234);
    checkHeader(h, rate, ch, 1234, "explicit");
    // Size 0 means "use the accumulated count", so an explicit 0 cannot be
    // requested once something was encoded (documented API behaviour).
    enc.writeHeader(h, 0);
    checkHeader(h, rate, ch, static_cast<uint32_t>(total), "explicit 0");
}

void testSamples()
{
    const size_t lengths[] = {1, 2, 3, 7, 8, 9, 15, 16, 17, 33, 1001};
    for (int ch : {1, 2})
        for (size_t n : lengths)
        {
            WavOutputEncoder enc;
            enc.initialize(48000, ch);
            std::vector<float> sig = makeSignal(n * ch, ch);
            std::vector<uint8_t> out;
            CHECK(enc.encode(sig.data(), sig.size(), out), "encode");
            CHECK(out.size() == sig.size() * 2, "size");
            int bad = 0;
            for (size_t i = 0; i < sig.size() && bad < 5; ++i)
            {
                const int16_t got = s16(out.data() + i * 2);
                if (got != refS16(sig[i]))
                {
                    ++bad;
                    CHECK(false, "ch=%d n=%zu i=%zu in=%g got %d want %d", ch, n,
                          i, sig[i], got, refS16(sig[i]));
                }
            }
        }

    // Known values, byte by byte (little endian).
    WavOutputEncoder enc;
    enc.initialize(44100, 1);
    const float in[] = {1.0f, -1.0f, 0.5f, 2.0f, 0.0f};
    std::vector<uint8_t> out;
    enc.encode(in, 5, out);
    const uint8_t want[] = {0xFF, 0x7F, 0x01, 0x80, 0xFF, 0x3F, 0xFF, 0x7F, 0x00, 0x00};
    CHECK(out.size() == sizeof want && std::memcmp(out.data(), want, sizeof want) == 0,
          "known byte pattern mismatch");
}

void testLifecycle()
{
    {
        WavOutputEncoder enc;
        const float x[4] = {0.1f, 0.2f, 0.3f, 0.4f};
        std::vector<uint8_t> out;
        CHECK(!enc.encode(x, 4, out), "encode before initialize succeeded");
        CHECK(!enc.finalize(out), "finalize before initialize succeeded");
    }
    {
        WavOutputEncoder enc;
        enc.initialize(44100, 2);
        std::vector<uint8_t> out;
        const float x[4] = {0.1f, 0.2f, 0.3f, 0.4f};
        CHECK(!enc.encode(nullptr, 4, out), "encode(nullptr) succeeded");
        CHECK(!enc.encode(x, 0, out), "encode of 0 samples succeeded");

        // encode() replaces the output contents (the base-class comment says
        // "appended"; MixerOutput clears the vector between calls, so this
        // is only a documentation mismatch).
        out.assign(10, 0xEE);
        CHECK(enc.encode(x, 4, out), "encode");
        CHECK(out.size() == 8, "encode appended instead of replacing (%zu)", out.size());

        // Re-initialize resets the byte count and picks up the new format.
        CHECK(enc.totalPcmBytes() == 8, "count before reinit");
        enc.initialize(22050, 1);
        CHECK(enc.totalPcmBytes() == 0, "count not reset by initialize");
        std::vector<uint8_t> h;
        enc.writeCurrentHeader(h);
        checkHeader(h, 22050, 1, 0, "reinit");
    }
    {
        // Through the factory.
        std::unique_ptr<MixerOutputEncoder> enc(MixerOutputEncoder::create(MIXER_OUTPUT_WAV));
        CHECK(enc != nullptr, "factory returned null for WAV");
        if (enc)
        {
            CHECK(enc->initialize(44100, 2), "factory initialize");
            std::vector<uint8_t> h;
            CHECK(enc->writeCurrentHeader(h), "factory writeCurrentHeader");
            checkHeader(h, 44100, 2, 0, "factory");
        }
        CHECK(MixerOutputEncoder::create(MIXER_OUTPUT_PCM_S16LE) == nullptr,
              "factory made an encoder for raw PCM");
    }
}

struct Scenario
{
    const char *name;
    void (*run)();
};

const Scenario kScenarios[] = {
    {"header", testHeader},
    {"sizes", testSizes},
    {"samples", testSamples},
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
    }
    if (ran == 0)
    {
        std::fprintf(stderr, "unknown scenario: %s\n", only);
        return 2;
    }
    std::printf("%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}
