// Checks for a platform streaming M4A/MP4 decoder (NativeStreamDecoder,
// LoadMode.disk) with priming/padding trimmed from the container's gapless
// info (m4a_gapless.cpp), against the full decode (NativeAudioDecoder).
//
// The script makes the fixtures on the host and writes a manifest, one case
// per line:
//
//   <file> <expect> <rate> <channels> <frames N> <tone start K>
//
// <expect> is "ok" (must stream), "fail" (open must fail cleanly), "trunc"
// (opens, but the data is cut: reads must end, never hang or overrun) or
// "opt" (a codec the device may lack: checked only if it opens, see
// strictPcm), "nomp4" (not MP4: may fail, or must stream cleanly). For "ok"
// cases the tone runs from frame K to the very end of N frames (0 = no
// known tone, length only).
//
// For each streamable case it checks:
//   * format: channels, rate, totalFrames == N (HE-AAC: +-2);
//   * a full read returns exactly totalFrames, then 0 (end of data), and
//     again 0 after another read;
//   * the tone starts at K and runs to the last frame (trimming is
//     sample-accurate);
//   * the same length and PCM as the full decode (os_decoder_android.cpp);
//   * seek(K) and seek(N-300) give the sequential PCM with no frame offset;
//     seek(0) after the end restarts; seek past the end reads 0;
//   * a decoder opened on a pinned descriptor keeps working after the path
//     is unlinked (NativeStreamFile::fd).
//
// Shared by test/android/native_stream_decoder_android_test.cpp (MediaCodec)
// and test/ios/native_stream_decoder_apple_ios_test.cpp (ExtAudioFile on the
// iOS simulator); each defines main() and calls
// runNativeStreamDecoderChecks(argc, argv) with [--strict] <fixture dir>.

#ifndef NATIVE_STREAM_DECODER_CHECKS_H
#define NATIVE_STREAM_DECODER_CHECKS_H

#include "native_decoder/native_audio_decoder.h"
#include "native_stream/native_stream_decoder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

static int gFailures = 0;
static int gKnownBugs = 0;
static bool gStrict = false;

// A check that fails because of a known src/ bug: reported, and counted as a
// failure only with --strict.
#define KNOWN_BUG(cond, id, ...)                                                \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("  KNOWN BUG %s (line %d): ", id, __LINE__);           \
            std::printf(__VA_ARGS__);                                           \
            std::printf("\n");                                                 \
            std::fflush(stdout);                                                \
            ++gKnownBugs;                                                       \
            if (gStrict) ++gFailures;                                           \
        }                                                                       \
    } while (0)

#define EXPECT(cond, ...)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("  FAIL (line %d): ", __LINE__);                       \
            std::printf(__VA_ARGS__);                                           \
            std::printf("\n");                                                  \
            std::fflush(stdout);                                                \
            ++gFailures;                                                        \
        }                                                                       \
    } while (0)

// L3: AC-3 in MP4 through MediaCodec on a device with Dolby's AC-3 decoder
// (Galaxy S22, API 36): the decoder raises the level ~6 dB and runs it
// through a limiter whose state survives a flush and depends on what was
// decoded long before, so where the limiter acts (this loud tone) the PCM
// after seek() differs from the PCM at that position by a few percent.
// Only the "opt" (AC-3) cases are marked; elsewhere this is a plain failure.
#if defined(__ANDROID__)
#define EXPECT_OR_L3(isAc3, cond, ...)                                          \
    do {                                                                        \
        if (isAc3) KNOWN_BUG(cond, "L3", __VA_ARGS__);                          \
        else EXPECT(cond, __VA_ARGS__);                                         \
    } while (0)
#else
#define EXPECT_OR_L3(isAc3, cond, ...) EXPECT(cond, __VA_ARGS__)
#endif

namespace {

struct Case {
    std::string file;
    std::string expect;
    unsigned rate = 0;
    unsigned channels = 0;
    uint64_t frames = 0;
    uint64_t toneStart = 0;
};

bool isHe(const Case &c) { return c.file.find("aach") != std::string::npos || c.file.find("aacp") != std::string::npos; }

double secondsSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

/// Reads the whole stream; returns the interleaved PCM, or stops at an error.
std::vector<float> readAll(NativeStreamDecoder &d, long &lastResult, unsigned chunkFrames = 4096) {
    const unsigned ch = d.format().channels;
    std::vector<float> all;
    std::vector<float> chunk(static_cast<size_t>(chunkFrames) * ch);
    lastResult = 0;
    for (int guard = 0; guard < 1000000; ++guard) {
        const long n = d.read(chunk.data(), chunkFrames);
        lastResult = n;
        if (n <= 0) break;
        all.insert(all.end(), chunk.begin(), chunk.begin() + n * ch);
    }
    return all;
}

long firstAbove(const std::vector<float> &pcm, unsigned ch, float threshold) {
    for (size_t i = 0; i < pcm.size() / ch; ++i)
        if (std::fabs(pcm[i * ch]) > threshold) return static_cast<long>(i);
    return -1;
}

float rmsCh0(const std::vector<float> &pcm, unsigned ch, size_t from, size_t to) {
    double acc = 0;
    for (size_t i = from; i < to; ++i) acc += static_cast<double>(pcm[i * ch]) * pcm[i * ch];
    return to > from ? static_cast<float>(std::sqrt(acc / static_cast<double>(to - from))) : 0.0f;
}

void runStreamCase(const std::string &dir, const Case &c) {
    const std::string path = dir + "/" + c.file;
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    std::unique_ptr<NativeStreamDecoder> d = openPlatformNativeStreamDecoder(path, error);
    if (c.expect == "opt" && !d) {
        std::printf("  not supported on this device (%s): skipped\n", error.c_str());
        return;
    }
    EXPECT(d != nullptr, "%s: open failed: %s", c.file.c_str(), error.c_str());
    if (!d) return;
    const NativeStreamFormat fmt = d->format();
    const unsigned ch = fmt.channels;
    std::printf("  open %.0f ms: %llu frames, %u ch, %.0f Hz\n", secondsSince(t0) * 1000,
                static_cast<unsigned long long>(fmt.totalFrames), ch, fmt.sampleRate);
    // Android's AAC decoder outputs mono HE-AAC as stereo (implicit
    // parametric-stereo signalling): accepted, both channels carry the tone.
    const bool upmixedHeMono = isHe(c) && c.channels == 1 && ch == 2;
    EXPECT(ch == c.channels || upmixedHeMono, "%s: %u channels, expected %u", c.file.c_str(), ch, c.channels);
    EXPECT(fmt.sampleRate == static_cast<float>(c.rate), "%s: %.0f Hz, expected %u", c.file.c_str(), fmt.sampleRate,
           c.rate);
    const uint64_t slack = isHe(c) ? 2 : 0;
    const uint64_t lenDiff = fmt.totalFrames > c.frames ? fmt.totalFrames - c.frames : c.frames - fmt.totalFrames;
    EXPECT(lenDiff <= slack, "%s: totalFrames %llu, expected %llu", c.file.c_str(),
           static_cast<unsigned long long>(fmt.totalFrames), static_cast<unsigned long long>(c.frames));
    if (ch == 0) return;

    long last = 0;
    const auto t1 = std::chrono::steady_clock::now();
    std::vector<float> pcm = readAll(*d, last);
    const uint64_t got = pcm.size() / ch;
    std::printf("  read %llu frames in %.0f ms\n", static_cast<unsigned long long>(got), secondsSince(t1) * 1000);
    EXPECT(last == 0, "%s: last read returned %ld, expected 0 (end of data)", c.file.c_str(), last);
    const bool ac3 = c.expect == "opt";
    EXPECT(got == fmt.totalFrames, "%s: read %llu frames, totalFrames %llu", c.file.c_str(),
           static_cast<unsigned long long>(got), static_cast<unsigned long long>(fmt.totalFrames));
    std::vector<float> tmp(256 * ch);
    EXPECT(d->read(tmp.data(), 256) == 0, "%s: read after the end did not return 0", c.file.c_str());

    // "opt" codecs (AC-3) differ in decoder latency between platforms (Apple's
    // AC-3 decoder outputs 256 frames earlier than ffmpeg's, its own
    // afconvert agrees), and the iOS simulator's ExtAudioFileRead reads past
    // the declared length for them: only length, end of data and seeks are
    // checked for those, the rest is printed.
    const bool strictPcm = c.expect != "opt";
    if (!strictPcm) {
        const long onset = firstAbove(pcm, ch, 0.25f);
        std::printf("  note: tone onset at %ld (encoded at %llu)\n", onset,
                    static_cast<unsigned long long>(c.toneStart));
    }
    if (strictPcm && c.toneStart > 0 && got > c.toneStart) {
        const long onset = firstAbove(pcm, ch, 0.25f);
        const long onsetSlack = isHe(c) ? 192 : 48;
        std::printf("  tone onset at %ld (expected %llu)\n", onset, static_cast<unsigned long long>(c.toneStart));
        EXPECT(onset >= 0 && std::labs(onset - static_cast<long>(c.toneStart)) <= onsetSlack,
               "%s: tone onset at %ld, expected %llu +- %ld", c.file.c_str(), onset,
               static_cast<unsigned long long>(c.toneStart), onsetSlack);
        const size_t tail = std::min<size_t>(256, got / 4);
        const float tailRms = rmsCh0(pcm, ch, got - tail, got);
        EXPECT(tailRms > 0.2f, "%s: last %zu frames RMS %.3f, the tone should run to the end", c.file.c_str(), tail,
               tailRms);
    }

    // The full decode (LoadMode.memory) of the same file.
    DecodedAudioData full = NativeAudioDecoder::decodeFile(path.c_str());
    if (full.success && !strictPcm) {
        std::printf("  note: decodeFile: %zu frames, %u ch, %.0f Hz\n", full.sampleCount, full.channels,
                    full.sampleRate);
        full.freeSamples();
    } else if (full.success) {
        std::printf("  decodeFile: %zu frames, %u ch, %.0f Hz\n", full.sampleCount, full.channels, full.sampleRate);
        // For an M4A whose length comes from an edit list (ffmpeg, no
        // iTunSMPB) the full decode must also stop at the edit list's length,
        // not at the extractor's duration (mdhd, priming included).
        EXPECT(full.sampleCount == fmt.totalFrames, "%s: decodeFile %zu frames, stream %llu", c.file.c_str(),
               full.sampleCount, static_cast<unsigned long long>(fmt.totalFrames));
        if (full.channels == ch && full.sampleCount >= got) {
            float worst = 0;
            for (size_t i = 0; i < got; ++i)
                for (unsigned k = 0; k < ch; ++k)
                    worst = std::max(worst, std::fabs(pcm[i * ch + k] - full.samples[k * full.sampleCount + i]));
            std::printf("  max diff from decodeFile %g\n", worst);
            EXPECT(worst < 2e-3f, "%s: stream PCM differs from decodeFile by %g", c.file.c_str(), worst);
        }
        full.freeSamples();
    } else {
        EXPECT(false, "%s: decodeFile failed: %s", c.file.c_str(), full.errorMessage.c_str());
    }

    // Seeks: the PCM after seek(target) is the sequential PCM from target.
    std::vector<uint64_t> targets = {c.toneStart, got > 300 ? got - 300 : 0, got / 2 + 7};
    for (uint64_t target : targets) {
        if (target >= got) continue;
        EXPECT(d->seek(target), "%s: seek(%llu) failed", c.file.c_str(), static_cast<unsigned long long>(target));
        const long n = d->read(tmp.data(), 256);
        const long expectedN = static_cast<long>(std::min<uint64_t>(256, got - target));
        EXPECT(n == expectedN, "%s: read after seek(%llu) gave %ld frames, expected %ld", c.file.c_str(),
               static_cast<unsigned long long>(target), n, expectedN);
        auto diffAt = [&](long lag) {
            float worst = 0;
            for (long i = 0; i < n; ++i) {
                const long j = static_cast<long>(target) + i + lag;
                if (j < 0 || j >= static_cast<long>(got)) continue;
                for (unsigned k = 0; k < ch; ++k) worst = std::max(worst, std::fabs(tmp[i * ch + k] - pcm[j * ch + k]));
            }
            return worst;
        };
        const float at = diffAt(0), before = diffAt(-1), after = diffAt(1);
        std::printf("  seek(%llu): diff %g (lag -1: %g, +1: %g)\n", static_cast<unsigned long long>(target), at, before,
                    after);
        // HE-AAC's SBR state after a seek differs from a decode from the
        // start; a frame offset still shows as a larger difference.
        EXPECT_OR_L3(ac3, at < (isHe(c) ? 0.05f : 2e-3f), "%s: after seek(%llu) PCM differs by %g", c.file.c_str(),
               static_cast<unsigned long long>(target), at);
        if (rmsCh0(pcm, ch, target, std::min<uint64_t>(got, target + 256)) > 0.05f)
            EXPECT(at < before && at < after, "%s: after seek(%llu) the PCM is off by a frame or more",
                   c.file.c_str(), static_cast<unsigned long long>(target));
    }

    // Past the end, and back to the start.
    EXPECT(d->seek(fmt.totalFrames + 1000), "%s: seek past the end failed", c.file.c_str());
    EXPECT(d->read(tmp.data(), 256) == 0, "%s: read after a seek past the end did not return 0", c.file.c_str());
    EXPECT(d->seek(0), "%s: seek(0) after the end failed", c.file.c_str());
    const long n0 = d->read(tmp.data(), 256);
    EXPECT(n0 == static_cast<long>(std::min<uint64_t>(256, got)), "%s: read after seek(0) gave %ld", c.file.c_str(),
           n0);
    float worst0 = 0;
    for (long i = 0; i < n0; ++i)
        for (unsigned k = 0; k < ch; ++k) worst0 = std::max(worst0, std::fabs(tmp[i * ch + k] - pcm[i * ch + k]));
    EXPECT(worst0 < 2e-3f, "%s: PCM after seek(0) differs from the first read by %g", c.file.c_str(), worst0);
}

void runFailCase(const std::string &dir, const Case &c) {
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    std::unique_ptr<NativeStreamDecoder> d = openPlatformNativeStreamDecoder(dir + "/" + c.file, error);
    std::printf("  open -> %s (%s) in %.0f ms\n", d ? "decoder" : "nullptr", error.c_str(), secondsSince(t0) * 1000);
    EXPECT(d == nullptr, "%s: open should fail", c.file.c_str());
    EXPECT(!error.empty(), "%s: open failed without an error message", c.file.c_str());
}

/// Not an MP4 container. Android's backend only takes MP4; ExtAudioFile also
/// streams other formats (MP3), which is fine as long as it streams cleanly.
void runNotMp4Case(const std::string &dir, const Case &c) {
    std::string error;
    std::unique_ptr<NativeStreamDecoder> d = openPlatformNativeStreamDecoder(dir + "/" + c.file, error);
    if (!d) {
        std::printf("  open -> nullptr (%s)\n", error.c_str());
        EXPECT(!error.empty(), "%s: open failed without an error message", c.file.c_str());
        return;
    }
    long last = 0;
    std::vector<float> pcm = readAll(*d, last);
    const uint64_t got = d->format().channels ? pcm.size() / d->format().channels : 0;
    std::printf("  opened anyway: %llu frames declared, %llu read, last %ld\n",
                static_cast<unsigned long long>(d->format().totalFrames), static_cast<unsigned long long>(got), last);
    EXPECT(last == 0 && got == d->format().totalFrames, "%s: opened but did not stream cleanly", c.file.c_str());
}

void runTruncCase(const std::string &dir, const Case &c) {
    std::string error;
    std::unique_ptr<NativeStreamDecoder> d = openPlatformNativeStreamDecoder(dir + "/" + c.file, error);
    if (!d) {
        std::printf("  open failed (%s): acceptable for a truncated file\n", error.c_str());
        EXPECT(!error.empty(), "%s: open failed without an error message", c.file.c_str());
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    long last = 0;
    std::vector<float> pcm = readAll(*d, last);
    const uint64_t got = d->format().channels ? pcm.size() / d->format().channels : 0;
    const double s = secondsSince(t0);
    std::printf("  declared %llu frames, read %llu, last result %ld, %.1f s\n",
                static_cast<unsigned long long>(d->format().totalFrames), static_cast<unsigned long long>(got), last,
                s);
    EXPECT(got <= d->format().totalFrames, "%s: read more frames than declared", c.file.c_str());
    EXPECT(s < 15.0, "%s: reading the truncated file took %.1f s", c.file.c_str(), s);
    // A seek into the missing part must not hang either.
    const auto t1 = std::chrono::steady_clock::now();
    if (d->seek(d->format().totalFrames - 100)) {
        std::vector<float> tmp(256 * d->format().channels);
        const long n = d->read(tmp.data(), 256);
        std::printf("  read after seek into the cut part: %ld\n", n);
    }
    EXPECT(secondsSince(t1) < 10.0, "%s: seek/read into the cut part took too long", c.file.c_str());
}

void pinnedDescriptorCase(const std::string &dir, const std::string &file) {
    std::printf("[case] pinned descriptor, path unlinked (%s)\n", file.c_str());
    const std::string copy = dir + "/pinned_copy.m4a";
    {
        std::ifstream in(dir + "/" + file, std::ios::binary);
        std::ofstream out(copy, std::ios::binary | std::ios::trunc);
        out << in.rdbuf();
    }
    const int fd = ::open(copy.c_str(), O_RDONLY | O_CLOEXEC);
    EXPECT(fd >= 0, "cannot open the copy");
    if (fd < 0) return;
    std::string error;
    std::unique_ptr<NativeStreamDecoder> a = openPlatformNativeStreamDecoder(NativeStreamFile{copy, fd}, error);
    EXPECT(a != nullptr, "open with a pinned descriptor failed: %s", error.c_str());
    ::unlink(copy.c_str());
    std::unique_ptr<NativeStreamDecoder> b = openPlatformNativeStreamDecoder(NativeStreamFile{copy, fd}, error);
    EXPECT(b != nullptr, "second open on the pinned descriptor after unlink failed: %s", error.c_str());
    if (a && b) {
        long la = 0, lb = 0;
        // Interleave the two decoders: they must not share a file offset.
        const unsigned ch = a->format().channels;
        std::vector<float> ca(1024 * ch), cb(1024 * ch);
        uint64_t ta = 0, tb = 0;
        bool same = true;
        for (int i = 0; i < 100000; ++i) {
            la = a->read(ca.data(), 1024);
            lb = b->read(cb.data(), 1024);
            if (la > 0) ta += la;
            if (lb > 0) tb += lb;
            if (la != lb || (la > 0 && std::memcmp(ca.data(), cb.data(), la * ch * sizeof(float)) != 0)) same = false;
            if (la <= 0 && lb <= 0) break;
        }
        EXPECT(la == 0 && lb == 0, "pinned decoders ended with %ld / %ld", la, lb);
        EXPECT(ta == a->format().totalFrames && tb == b->format().totalFrames,
               "pinned decoders read %llu / %llu frames, expected %llu", static_cast<unsigned long long>(ta),
               static_cast<unsigned long long>(tb), static_cast<unsigned long long>(a->format().totalFrames));
        EXPECT(same, "two decoders on one pinned descriptor produced different PCM");
    }
    a.reset();
    b.reset();
    ::close(fd);
}

} // namespace

/// Runs the checks; returns the process exit code.
inline int runNativeStreamDecoderChecks(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string dir;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
        else dir = argv[i];
    }
    if (dir.empty()) {
        std::printf("usage: %s [--strict] <dir with manifest.txt>\n", argv[0]);
        return 2;
    }
    std::ifstream manifest(dir + "/manifest.txt");
    if (!manifest) {
        std::printf("FAIL: no manifest in %s\n", dir.c_str());
        return 1;
    }
    std::string line;
    std::string firstOk;
    int cases = 0;
    while (std::getline(manifest, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        Case c;
        in >> c.file >> c.expect >> c.rate >> c.channels >> c.frames >> c.toneStart;
        std::printf("[case] %s (%s)\n", c.file.c_str(), c.expect.c_str());
        std::fflush(stdout);
        ++cases;
        if (c.expect == "ok" || c.expect == "opt") {
            runStreamCase(dir, c);
            if (firstOk.empty() && c.expect == "ok") firstOk = c.file;
        } else if (c.expect == "fail") {
            runFailCase(dir, c);
        } else if (c.expect == "nomp4") {
            runNotMp4Case(dir, c);
        } else if (c.expect == "trunc") {
            runTruncCase(dir, c);
        }
        std::fflush(stdout);
    }
    if (!firstOk.empty()) pinnedDescriptorCase(dir, firstOk);
    EXPECT(cases > 0, "manifest has no cases");

    if (gKnownBugs > 0) std::printf("%d known bug check(s) failed (pass --strict to count them).\n", gKnownBugs);
    if (gFailures == 0) {
        std::printf("All %d native stream decoder cases passed.\n", cases);
        return 0;
    }
    std::printf("%d native stream decoder check(s) failed.\n", gFailures);
    return 1;
}

#endif // NATIVE_STREAM_DECODER_CHECKS_H
