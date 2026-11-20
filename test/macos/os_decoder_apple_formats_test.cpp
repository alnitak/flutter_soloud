// The Apple full decoder (src/native_decoder/os_decoder_apple.mm,
// NativeAudioDecoder::decodeFile / decodeMemory, LoadMode.memory) across the
// containers and codecs afconvert writes: ALAC (M4A and CAF), AIFF, CAF with
// integer and float PCM, AAC-LC and HE-AAC (M4A and CAF); 1 to 6 channels;
// 8 to 96 kHz; 16- and 24-bit sources.
//
// Each case encodes a source WAV of exactly N frames in which channel k
// carries its own tone (300 + 450 k Hz), then checks the decode against the
// source:
//   * channels, sample rate, and the exact length (lossy: gapless trimming,
//     HE-AAC +-2 frames);
//   * lossless codecs: every sample equals the source within one LSB;
//   * lossy codecs: each channel carries its own tone (Goertzel power), so the
//     channel order is preserved and nothing is silent or swapped;
//   * decodeMemory returns the same samples as decodeFile.
// (Apple's AAC encoder resamples 96 kHz input to 48 kHz, so there is no
// 96 kHz AAC case; 96 kHz is covered by ALAC and AIFF.)
// A case whose encoding afconvert itself refuses (an unsupported rate or
// layout for that codec) is reported and left out.
//
// Build and run with test/macos/os_decoder_apple_formats_test.sh.
//
//   os_decoder_apple_formats_test <work dir>

#include "native_decoder/native_audio_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int gFailures = 0;
int gCases = 0;
int gSkipped = 0;

#define EXPECT(cond, ...)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("  FAIL (line %d): ", __LINE__);                       \
            std::printf(__VA_ARGS__);                                           \
            std::printf("\n");                                                  \
            ++gFailures;                                                        \
        }                                                                       \
    } while (0)

struct Case {
    const char *file;   // afconvert -f
    const char *format; // afconvert -d
    const char *ext;
    unsigned rate;
    unsigned channels;
    unsigned bits; // source bit depth (16 or 24)
    bool lossless;
    bool he;
    const char *layout; // afconvert -l, or nullptr
};

/// Channel k's tone. In a 5.1 layout the sixth channel is the LFE, which the
/// AAC encoder low-passes: it gets a 60 Hz tone.
double toneHz(unsigned ch, unsigned channels = 0) { return channels == 6 && ch == 5 ? 60.0 : 300.0 + 450.0 * ch; }

/// Source samples (planar float, already quantized to [bits]) and the WAV.
std::vector<float> makeSource(const Case &c, uint64_t frames, const std::string &wavPath) {
    std::vector<float> planar(frames * c.channels);
    const unsigned bytes = c.bits / 8;
    const double scale = c.bits == 24 ? 8388607.0 : 32767.0;
    std::vector<unsigned char> data(frames * c.channels * bytes);
    for (uint64_t i = 0; i < frames; ++i) {
        for (unsigned ch = 0; ch < c.channels; ++ch) {
            const double v = 0.4 * std::sin(2.0 * M_PI * toneHz(ch, c.channels) * static_cast<double>(i) / c.rate);
            const int32_t q = static_cast<int32_t>(std::lround(v * scale));
            planar[ch * frames + i] = static_cast<float>(q / (scale + 1.0));
            unsigned char *p = data.data() + (i * c.channels + ch) * bytes;
            for (unsigned b = 0; b < bytes; ++b) p[b] = static_cast<unsigned char>((q >> (8 * b)) & 0xff);
        }
    }
    std::ofstream f(wavPath, std::ios::binary | std::ios::trunc);
    auto u32 = [&f](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&f](uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); };
    f.write("RIFF", 4);
    u32(static_cast<uint32_t>(36 + data.size()));
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(static_cast<uint16_t>(c.channels));
    u32(c.rate);
    u32(c.rate * c.channels * bytes);
    u16(static_cast<uint16_t>(c.channels * bytes));
    u16(static_cast<uint16_t>(c.bits));
    f.write("data", 4);
    u32(static_cast<uint32_t>(data.size()));
    f.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    return planar;
}

/// Goertzel power of [hz] in planar channel [ch], over the middle half.
double tonePower(const DecodedAudioData &d, unsigned ch, double hz) {
    const size_t from = d.sampleCount / 4, to = d.sampleCount * 3 / 4;
    const double w = 2.0 * M_PI * hz / d.sampleRate, coeff = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    const float *x = d.samples + ch * d.sampleCount;
    for (size_t i = from; i < to; ++i) {
        const double s = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return (s1 * s1 + s2 * s2 - coeff * s1 * s2) / static_cast<double>(to - from);
}

void runCase(const std::string &dir, const Case &c) {
    const uint64_t frames = static_cast<uint64_t>(c.rate) * 3 / 2 + 77; // 1.5 s, not a packet multiple
    char label[160];
    std::snprintf(label, sizeof(label), "%s/%s %u Hz %u ch %u-bit%s", c.file, c.format, c.rate, c.channels, c.bits,
                  c.layout ? " (layout)" : "");
    std::printf("[case] %s\n", label);
    const std::string wav = dir + "/src.wav";
    const std::string out = dir + "/out." + c.ext;
    std::remove(out.c_str());
    const std::vector<float> src = makeSource(c, frames, wav);
    std::string cmd = std::string("afconvert -f ") + c.file + " -d " + c.format;
    if (c.layout) cmd += std::string(" -l ") + c.layout;
    cmd += " '" + wav + "' '" + out + "' >/dev/null 2>&1";
    if (std::system(cmd.c_str()) != 0) {
        std::printf("  afconvert refused this encoding, left out (%s)\n", cmd.c_str());
        ++gSkipped;
        return;
    }
    ++gCases;

    DecodedAudioData d = NativeAudioDecoder::decodeFile(out.c_str());
    EXPECT(d.success, "%s: decodeFile failed: %s", label, d.errorMessage.c_str());
    if (!d.success) return;
    std::printf("  decodeFile: %zu frames, %u ch, %.0f Hz\n", d.sampleCount, d.channels, d.sampleRate);
    EXPECT(d.channels == c.channels, "%s: %u channels", label, d.channels);
    EXPECT(d.sampleRate == static_cast<float>(c.rate), "%s: %.0f Hz", label, d.sampleRate);
    const uint64_t slack = c.he ? 2 : 0;
    const uint64_t diff = d.sampleCount > frames ? d.sampleCount - frames : frames - d.sampleCount;
    EXPECT(diff <= slack, "%s: %zu frames, the source has %llu", label, d.sampleCount,
           static_cast<unsigned long long>(frames));

    if (d.channels == c.channels) {
        if (c.lossless && d.sampleCount == frames) {
            const double lsb = c.bits == 24 ? 1.0 / 8388608.0 : 1.0 / 32768.0;
            double worst = 0;
            for (size_t i = 0; i < frames * c.channels; ++i)
                worst = std::max(worst, static_cast<double>(std::fabs(d.samples[i] - src[i])));
            std::printf("  max diff from the source: %.3g (%.2f LSB)\n", worst, worst / lsb);
            EXPECT(worst <= 1.5 * lsb, "%s: lossless decode differs from the source by %.2f LSB", label,
                   worst / lsb);
        } else if (!c.lossless) {
            if (c.channels > 2) {
                std::printf("  tone power matrix (row: decoded channel, column: source channel's tone):\n");
                for (unsigned ch = 0; ch < c.channels; ++ch) {
                    std::printf("   ");
                    for (unsigned k = 0; k < c.channels; ++k) std::printf(" %.4f", tonePower(d, ch, toneHz(k, c.channels)));
                    std::printf("\n");
                }
            }
            for (unsigned ch = 0; ch < c.channels; ++ch) {
                const double own = tonePower(d, ch, toneHz(ch, c.channels));
                double other = 0;
                for (unsigned k = 0; k < c.channels; ++k)
                    if (k != ch) other = std::max(other, tonePower(d, ch, toneHz(k, c.channels)));
                EXPECT(own > 0.01 && own > 20 * other,
                       "%s: channel %u: own tone power %.4f, strongest other tone %.4f (channel order?)", label,
                       ch, own, other);
            }
        }
    }

    // The same bytes through decodeMemory.
    std::ifstream in(out, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
    DecodedAudioData m = NativeAudioDecoder::decodeMemory(bytes.data(), bytes.size());
    EXPECT(m.success, "%s: decodeMemory failed: %s", label, m.errorMessage.c_str());
    if (m.success) {
        EXPECT(m.sampleCount == d.sampleCount && m.channels == d.channels, "%s: decodeMemory %zu frames, %u ch",
               label, m.sampleCount, m.channels);
        if (m.sampleCount == d.sampleCount && m.channels == d.channels) {
            double worst = 0;
            size_t first = SIZE_MAX;
            for (size_t i = 0; i < d.sampleCount * d.channels; ++i) {
                const double e = std::fabs(m.samples[i] - d.samples[i]);
                if (e > 0 && first == SIZE_MAX) first = i;
                worst = std::max(worst, e);
            }
            if (worst > 0) std::printf("  decodeMemory vs decodeFile: max diff %g, first at sample %zu\n", worst, first);
            // AudioToolbox's AAC decoder is not bit-exact between two runs.
            EXPECT(worst < 1e-5, "%s: decodeMemory samples differ from decodeFile by %g", label, worst);
        }
        m.freeSamples();
    }
    d.freeSamples();
}

} // namespace

int main(int argc, char **argv) {
    const char *envTmp = std::getenv("TMPDIR");
    const std::string dir = argc > 1 ? argv[1] : (envTmp ? envTmp : "/tmp");
    if (std::system("command -v afconvert >/dev/null 2>&1") != 0) {
        std::printf("SKIPPED: afconvert not found\n");
        return 0;
    }
    const Case cases[] = {
        // Lossless.
        {"m4af", "alac", "m4a", 44100, 2, 16, true, false, nullptr},
        {"m4af", "alac", "m4a", 8000, 1, 16, true, false, nullptr},
        {"m4af", "alac", "m4a", 96000, 6, 24, true, false, nullptr},
        {"m4af", "alac", "m4a", 48000, 3, 24, true, false, nullptr},
        {"AIFF", "BEI16", "aiff", 22050, 2, 16, true, false, nullptr},
        {"AIFF", "BEI24", "aiff", 96000, 1, 24, true, false, nullptr},
        {"AIFF", "BEI16", "aiff", 48000, 6, 16, true, false, nullptr},
        {"caff", "LEI16", "caf", 11025, 4, 16, true, false, nullptr},
        {"caff", "BEI24", "caf", 88200, 2, 24, true, false, nullptr},
        {"caff", "LEF32", "caf", 48000, 2, 24, true, false, nullptr},
        {"caff", "alac", "caf", 32000, 5, 24, true, false, nullptr},
        // Lossy.
        {"m4af", "aac", "m4a", 8000, 1, 16, false, false, nullptr},
        {"m4af", "aac", "m4a", 44100, 2, 16, false, false, nullptr},
        {"m4af", "aac", "m4a", 48000, 6, 16, false, false, "MPEG_5_1_D"},
        {"m4af", "aac", "m4a", 32000, 3, 16, false, false, "MPEG_3_0_A"},
        {"m4af", "aach", "m4a", 44100, 2, 16, false, true, nullptr},
        {"m4af", "aach", "m4a", 48000, 1, 16, false, true, nullptr},
        {"m4af", "aach", "m4a", 32000, 2, 16, false, true, nullptr},
        {"caff", "aac", "caf", 48000, 2, 16, false, false, nullptr},
        {"caff", "aac", "caf", 22050, 1, 16, false, false, nullptr},
    };
    for (const Case &c : cases) runCase(dir, c);

    std::printf("%d cases decoded, %d left out (encoding refused by afconvert).\n", gCases, gSkipped);
    if (gFailures == 0) {
        std::printf("All Apple decoder format checks passed.\n");
        return 0;
    }
    std::printf("%d Apple decoder format check(s) failed.\n", gFailures);
    return 1;
}
