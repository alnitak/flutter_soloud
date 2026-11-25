// Gapless M4A on Apple: iTunSMPB encoder delay/padding from Apple's own AAC
// encoder, end to end.
//
// For each case a WAV of exactly N frames (silence, then a 1 kHz tone from
// frame K to the very end) is encoded with afconvert, which writes iTunSMPB
// and no edit list. The test then checks that
//   * parseM4aGapless (src/native_stream/m4a_gapless.cpp) finds iTunSMPB and
//     resolves the delay and the valid length to N at the output rate,
//     including HE-AAC, whose iTunSMPB counts core-rate samples;
//   * NativeAudioDecoder::decodeFile (os_decoder_apple.mm, LoadMode.memory)
//     returns exactly N frames with the tone starting at K and running to the
//     last frame (priming and padding trimmed, nothing cut);
//   * the streaming backend (native_stream_decoder_apple.cpp, LoadMode.disk)
//     reports the same length, reads the same PCM, and seeks
//     sample-accurately into it.
// The bundled-asset parser cases and ffmpeg's edit-list files are covered by
// test/common/native_stream_test.cpp; this one is about lengths and rates
// afconvert can produce.
//
// Build and run with test/macos/m4a_gapless_test.sh.
//
//   m4a_gapless_test <work dir>

#include "native_decoder/native_audio_decoder.h"
#include "native_stream/m4a_gapless.h"
#include "native_stream/native_stream_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

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
    const char *codec;   // afconvert data format: aac (LC), aach (HE), aacp (HE v2)
    unsigned rate;
    unsigned channels;
    uint64_t frames;     // N
    uint64_t toneStart;  // K
};

bool isHe(const Case &c) { return std::strcmp(c.codec, "aac") != 0; }

/// 16-bit PCM WAV: silence for [K] frames, then a 1 kHz tone at 0.5 to the end.
bool writeWav(const std::string &path, const Case &c) {
    const uint32_t dataBytes = static_cast<uint32_t>(c.frames * c.channels * 2);
    std::vector<unsigned char> wav(44 + dataBytes, 0);
    auto put32 = [&wav](size_t at, uint32_t v) { std::memcpy(wav.data() + at, &v, 4); };
    auto put16 = [&wav](size_t at, uint16_t v) { std::memcpy(wav.data() + at, &v, 2); };
    std::memcpy(wav.data(), "RIFF", 4);
    put32(4, 36 + dataBytes);
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, static_cast<uint16_t>(c.channels));
    put32(24, c.rate);
    put32(28, c.rate * c.channels * 2);
    put16(32, static_cast<uint16_t>(c.channels * 2));
    put16(34, 16);
    std::memcpy(wav.data() + 36, "data", 4);
    put32(40, dataBytes);
    for (uint64_t i = c.toneStart; i < c.frames; ++i) {
        const double t = static_cast<double>(i - c.toneStart) / c.rate;
        const int16_t s = static_cast<int16_t>(16384.0 * std::sin(2.0 * M_PI * 1000.0 * t));
        for (unsigned ch = 0; ch < c.channels; ++ch)
            std::memcpy(wav.data() + 44 + (i * c.channels + ch) * 2, &s, 2);
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(wav.data()), static_cast<std::streamsize>(wav.size()));
    return out.good();
}

/// First frame of planar channel 0 above [threshold], or -1.
long firstAbove(const float *s, size_t frames, float threshold) {
    for (size_t i = 0; i < frames; ++i)
        if (std::fabs(s[i]) > threshold) return static_cast<long>(i);
    return -1;
}

float rms(const float *s, size_t from, size_t to) {
    double acc = 0;
    for (size_t i = from; i < to; ++i) acc += static_cast<double>(s[i]) * s[i];
    return to > from ? static_cast<float>(std::sqrt(acc / static_cast<double>(to - from))) : 0.0f;
}

void runCase(const std::string &workDir, const Case &c) {
    char label[128];
    std::snprintf(label, sizeof(label), "%s %u Hz %uch N=%llu K=%llu", c.codec, c.rate, c.channels,
                  static_cast<unsigned long long>(c.frames), static_cast<unsigned long long>(c.toneStart));
    std::printf("[case] %s\n", label);

    const std::string wav = workDir + "/gapless_in.wav";
    const std::string m4a = workDir + "/gapless_out.m4a";
    std::remove(m4a.c_str());
    EXPECT(writeWav(wav, c), "%s: cannot write the WAV", label);
    const std::string cmd = std::string("afconvert -f m4af -d ") + c.codec + " '" + wav + "' '" + m4a + "' >/dev/null 2>&1";
    if (std::system(cmd.c_str()) != 0) {
        EXPECT(false, "%s: afconvert failed: %s", label, cmd.c_str());
        return;
    }

    // HE-AAC delays and lengths are declared at the core rate, half the
    // output rate, so the resolved valid length can be off by one frame.
    const uint64_t lengthSlack = isHe(c) ? 2 : 0;

    // 1. Container metadata.
    M4aGaplessInfo info;
    EXPECT(parseM4aGapless(m4a, info), "%s: parseM4aGapless failed", label);
    EXPECT(info.hasSmpb, "%s: no iTunSMPB in an afconvert file", label);
    uint64_t delay = 0, valid = 0;
    EXPECT(info.resolve(c.rate, delay, valid), "%s: resolve() found no gapless info", label);
    std::printf("  container: timescale %u, media %llu, smpb delay %u padding %u length %llu, edit %d -> delay %llu valid %llu\n",
                info.mediaTimescale, static_cast<unsigned long long>(info.mediaDuration), info.smpbDelay,
                info.smpbPadding, static_cast<unsigned long long>(info.smpbLength), info.hasEdit ? 1 : 0,
                static_cast<unsigned long long>(delay), static_cast<unsigned long long>(valid));
    const uint64_t lengthDiff = valid > c.frames ? valid - c.frames : c.frames - valid;
    EXPECT(lengthDiff <= lengthSlack, "%s: resolved valid length %llu, expected %llu", label,
           static_cast<unsigned long long>(valid), static_cast<unsigned long long>(c.frames));
    // Apple's AAC encoder primes 2112 samples at its output rate (twice the
    // core 2112 for HE-AAC's SBR output).
    const uint64_t expectedDelay = isHe(c) ? 2 * 2112 : 2112;
    EXPECT(delay == expectedDelay, "%s: resolved delay %llu, expected %llu", label,
           static_cast<unsigned long long>(delay), static_cast<unsigned long long>(expectedDelay));

    // 2. Full decode (LoadMode.memory).
    DecodedAudioData full = NativeAudioDecoder::decodeFile(m4a.c_str());
    EXPECT(full.success, "%s: decodeFile failed: %s", label, full.errorMessage.c_str());
    if (!full.success) return;
    std::printf("  decodeFile: %zu frames, %u ch, %.0f Hz\n", full.sampleCount, full.channels, full.sampleRate);
    EXPECT(full.channels == c.channels, "%s: %u channels", label, full.channels);
    EXPECT(full.sampleRate == static_cast<float>(c.rate), "%s: %.0f Hz", label, full.sampleRate);
    const uint64_t fullDiff = full.sampleCount > c.frames ? full.sampleCount - c.frames : c.frames - full.sampleCount;
    EXPECT(fullDiff <= lengthSlack, "%s: decodeFile gave %zu frames, expected %llu (priming/padding not trimmed?)",
           label, full.sampleCount, static_cast<unsigned long long>(c.frames));
    EXPECT(full.sampleCount == valid, "%s: decodeFile %zu frames, container says %llu", label, full.sampleCount,
           static_cast<unsigned long long>(valid));

    // The tone onset lands on K: trimming is sample-accurate, not just the
    // right length. AAC smears a hard onset over a few ms of pre-echo, so
    // find it at half the tone's amplitude.
    const long onset = firstAbove(full.samples, full.sampleCount, 0.25f);
    const long onsetSlack = isHe(c) ? 192 : 48;
    std::printf("  tone onset at frame %ld (expected %llu)\n", onset, static_cast<unsigned long long>(c.toneStart));
    EXPECT(onset >= 0 && std::labs(onset - static_cast<long>(c.toneStart)) <= onsetSlack,
           "%s: tone onset at %ld, expected %llu +- %ld", label, onset, static_cast<unsigned long long>(c.toneStart),
           onsetSlack);
    // The tone runs to the last frame: the end is not cut nor padded.
    const size_t tail = std::min<size_t>(256, full.sampleCount / 4);
    const float tailRms = rms(full.samples, full.sampleCount - tail, full.sampleCount);
    EXPECT(tailRms > 0.2f, "%s: last %zu frames RMS %.3f, the tone should run to the end", label, tail, tailRms);

    // 3. Streaming backend (LoadMode.disk).
    std::string error;
    std::unique_ptr<NativeStreamDecoder> stream = openPlatformNativeStreamDecoder(m4a, error);
    EXPECT(stream != nullptr, "%s: streaming open failed: %s", label, error.c_str());
    if (stream) {
        const NativeStreamFormat &fmt = stream->format();
        std::printf("  stream: %llu frames, %u ch, %.0f Hz\n", static_cast<unsigned long long>(fmt.totalFrames),
                    fmt.channels, fmt.sampleRate);
        EXPECT(fmt.totalFrames == full.sampleCount, "%s: stream reports %llu frames, decodeFile %zu", label,
               static_cast<unsigned long long>(fmt.totalFrames), full.sampleCount);
        EXPECT(fmt.channels == full.channels && fmt.sampleRate == full.sampleRate, "%s: stream format differs", label);

        if (fmt.channels == full.channels && fmt.channels > 0) {
            const unsigned ch = fmt.channels;
            std::vector<float> chunk(4096 * ch);
            uint64_t read = 0;
            float worst = 0.0f;
            for (int guard = 0; guard < 1000000; ++guard) {
                const long n = stream->read(chunk.data(), 4096);
                EXPECT(n >= 0, "%s: stream read error", label);
                if (n <= 0) break;
                for (long i = 0; i < n; ++i) {
                    if (read + i >= full.sampleCount) break;
                    for (unsigned k = 0; k < ch; ++k)
                        worst = std::max(worst, std::fabs(chunk[i * ch + k] - full.samples[k * full.sampleCount + read + i]));
                }
                read += static_cast<uint64_t>(n);
            }
            std::printf("  stream read %llu frames, max diff from decodeFile %g\n",
                        static_cast<unsigned long long>(read), worst);
            EXPECT(read == fmt.totalFrames, "%s: stream read %llu frames, reported %llu", label,
                   static_cast<unsigned long long>(read), static_cast<unsigned long long>(fmt.totalFrames));
            EXPECT(worst < 1e-4f, "%s: stream PCM differs from decodeFile by %g", label, worst);

            // Seek to the onset and to a frame near the end: sample-accurate.
            for (uint64_t target : {static_cast<uint64_t>(c.toneStart), static_cast<uint64_t>(full.sampleCount > 300 ? full.sampleCount - 300 : 0)}) {
                EXPECT(stream->seek(target), "%s: seek(%llu) failed", label, static_cast<unsigned long long>(target));
                const long n = stream->read(chunk.data(), 256);
                // Max |difference| from the full decode with the stream
                // shifted by [lag] frames.
                auto diffAt = [&](long lag) {
                    float worst = 0.0f;
                    for (long i = 0; i < n; ++i) {
                        const long j = static_cast<long>(target) + i + lag;
                        if (j < 0 || j >= static_cast<long>(full.sampleCount)) continue;
                        for (unsigned k = 0; k < ch; ++k)
                            worst = std::max(worst, std::fabs(chunk[i * ch + k] - full.samples[k * full.sampleCount + j]));
                    }
                    return worst;
                };
                const long expectedN = static_cast<long>(std::min<uint64_t>(256, full.sampleCount - target));
                EXPECT(n == expectedN, "%s: read after seek(%llu) gave %ld frames, expected %ld", label,
                       static_cast<unsigned long long>(target), n, expectedN);
                const float at = diffAt(0), before = diffAt(-1), after = diffAt(1);
                std::printf("  seek(%llu): diff %g (lag -1: %g, +1: %g)\n", static_cast<unsigned long long>(target), at,
                            before, after);
                // After a seek the decoder restarts mid-stream: AAC-LC comes
                // back almost bit-exact, while HE-AAC's SBR state differs from
                // a decode from the start. Either way there must be no offset.
                EXPECT(at < (isHe(c) ? 0.05f : 2e-3f), "%s: after seek(%llu) PCM differs by %g", label,
                       static_cast<unsigned long long>(target), at);
                EXPECT(at < before && at < after, "%s: after seek(%llu) the PCM is off by a frame or more", label,
                       static_cast<unsigned long long>(target));
            }
        }
    }
    full.freeSamples();
}

} // namespace

int main(int argc, char **argv) {
    const char *envTmp = std::getenv("TMPDIR");
    const std::string workDir = argc > 1 ? argv[1] : (envTmp ? envTmp : "/tmp");
    if (std::system("command -v afconvert >/dev/null 2>&1") != 0) {
        std::printf("SKIPPED: afconvert not found\n");
        return 0;
    }

    const Case cases[] = {
        {"aac", 44100, 2, 44100 + 37, 11025},
        {"aac", 48000, 1, 12345, 4000},
        {"aac", 32000, 2, 32000 * 3 + 1023, 16000},
        // Shorter than one AAC packet and than the encoder delay.
        {"aac", 44100, 1, 1000, 200},
        {"aach", 44100, 2, 88200 + 1, 22050},
        {"aach", 48000, 1, 48000 + 500, 9600},
        {"aacp", 44100, 2, 66150, 22050},
    };
    for (const Case &c : cases) runCase(workDir, c);

    if (gFailures == 0) {
        std::printf("All m4a gapless tests passed.\n");
        return 0;
    }
    std::printf("%d m4a gapless check(s) failed.\n", gFailures);
    return 1;
}
