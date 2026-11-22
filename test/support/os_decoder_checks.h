// Platform-independent checks for the OS-backed decoders: NativeAudioDecoder
// (src/native_decoder/os_decoder_<platform>) and the AACDecoderWrapper
// streaming decoder (src/audiobuffer/aac_stream_decoder_<platform>).
//
// Header-only and plain C++17 (no POSIX, no Win32), so the same expectations
// run against AudioToolbox (test/macos/aac_stream_decoder_apple_test.mm, also
// built for the iOS simulator), FFmpeg on Linux
// (test/linux/os_decoder_linux_test.cpp) and Media Foundation
// (test/windows/os_decoder_windows_test.cpp). The Android MediaCodec suite
// (test/android/android_native_decoder_test.cpp) predates this header and
// keeps its own, stricter copy of the same checks.
//
// Fixtures (example/assets/audio), all passed in as a directory:
//   aac_lc_mono_44100.m4a, aac_lc_stereo_48000.m4a: 2 s of AAC-LC with 0.5 s
//     silence, a 1 kHz tone from 0.5 s to 1.5 s (L 0.5, R 0.25), 0.5 s silence.
//   sample-AAC.aac, sample-AC3.ac3, sample-EAC3.eac3: ~15 s, 44.1 kHz stereo.
//
// Where platforms legitimately differ, a Policy says how; everything else is
// the same contract on every platform.

#ifndef OS_DECODER_CHECKS_H
#define OS_DECODER_CHECKS_H

#include "../../src/native_decoder/native_audio_decoder.h"
#include "../../src/audiobuffer/aac_stream_decoder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace osdec {

inline int &failures() {
    static int count = 0;
    return count;
}

inline int &knownBugs() {
    static int count = 0;
    return count;
}

/// Set OSDEC_STRICT=1 to make known bugs fail the run like any other check.
inline bool strictKnownBugs() {
    const char *v = std::getenv("OSDEC_STRICT");
    return v != nullptr && v[0] == '1';
}

/// A check that currently fails because of a bug already reported in src/
/// ([where] names it). It is printed as KNOWN BUG and does not fail the run
/// unless OSDEC_STRICT=1; once the bug is fixed it passes silently.
#define OSDEC_KNOWN_BUG(cond, where, msg)                                               \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::cout << "  KNOWN BUG (" << (where) << "): " << msg << std::endl;          \
            ++osdec::knownBugs();                                                       \
            if (osdec::strictKnownBugs()) ++osdec::failures();                          \
        }                                                                               \
    } while (0)

/// OSDEC_CHECK, or OSDEC_KNOWN_BUG when [where] is non-null.
#define OSDEC_CHECK_OR_KNOWN(where, cond, msg)                                          \
    do {                                                                                \
        if ((where) != nullptr) OSDEC_KNOWN_BUG(cond, where, msg);                      \
        else OSDEC_CHECK(cond, msg);                                                    \
    } while (0)

#define OSDEC_CHECK(cond, msg)                                                          \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::cout << "  FAIL: " << msg << "  (" #cond ") at " << __FILE__ << ":"     \
                      << __LINE__ << std::endl;                                         \
            ++osdec::failures();                                                        \
        }                                                                               \
    } while (0)

/// How a platform's decoders may differ from the common contract.
struct Policy {
    /// decodeMemory and decodeFile of the same bytes must agree within this
    /// absolute tolerance per sample (0 = bit exact).
    float memFileTolerance = 1e-6f;
    /// A cut-short M4A may decode the frames it still has instead of failing
    /// (AudioToolbox does when `moov` comes first). It must then still return
    /// fewer frames than the whole file, never garbage.
    bool truncatedM4aMayDecode = false;
    /// The AAC-LC M4A fixtures must decode to exactly 2 s with the tone at
    /// 0.5 s .. 1.5 s, i.e. encoder priming and padding trimmed.
    bool exactM4aTrim = true;
    /// AC-3/E-AC-3 decoding may be unavailable (no system decoder). It must
    /// then fail with a message, never return partial audio.
    bool ac3Optional = true;
    /// Elementary streams: allowed frame-count difference from the ffmpeg
    /// reference (each decoder primes and flushes its own way).
    long elementaryFrameSlack = 4096;
    /// Chunked streaming through AACDecoderWrapper vs the whole-buffer decode:
    /// per-sample tolerance and allowed frame-count difference.
    float streamTolerance = 1e-5f;
    long streamFrameSlack = 0;
    /// Known src/ bugs (file:line) that make a chunked-streaming check fail;
    /// null when the platform has none. See OSDEC_KNOWN_BUG.
    ///   smallChunks: chunks smaller than an AAC/AC-3 frame lose audio.
    ///   ac3Tail: the last AC-3/E-AC-3 frame of a stream is not decoded.
    ///   streaming: chunked streaming is broken whatever the chunk size.
    const char *knownBugSmallChunks = nullptr;
    const char *knownBugAc3Tail = nullptr;
    const char *knownBugStreaming = nullptr;
};

inline std::string joinPath(const std::string &dir, const std::string &name) {
    if (dir.empty()) return name;
    const char last = dir.back();
    if (last == '/' || last == '\\') return dir + name;
#if defined(_WIN32)
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

inline bool fileExists(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    return f.good();
}

inline std::vector<unsigned char> readFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), {});
}

inline void writeFile(const std::string &path, const std::vector<unsigned char> &bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

/// A decode result that frees itself, with how long the decode took.
struct Decoded {
    DecodedAudioData d;
    double seconds = 0;
    Decoded(DecodedAudioData data, double secs) : d(data), seconds(secs) {}
    ~Decoded() { d.freeSamples(); }
    Decoded(const Decoded &) = delete;
    Decoded &operator=(const Decoded &) = delete;
};

template <typename Fn>
inline std::unique_ptr<Decoded> timed(Fn fn) {
    const auto t0 = std::chrono::steady_clock::now();
    DecodedAudioData d = fn();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return std::unique_ptr<Decoded>(new Decoded(d, secs));
}

inline std::unique_ptr<Decoded> decodeFileTimed(const std::string &path) {
    return timed([&] { return NativeAudioDecoder::decodeFile(path.c_str()); });
}

inline std::unique_ptr<Decoded> decodeMemoryTimed(const std::vector<unsigned char> &bytes) {
    return timed([&] { return NativeAudioDecoder::decodeMemory(bytes.data(), bytes.size()); });
}

inline float rms(const float *s, size_t from, size_t to) {
    double acc = 0;
    for (size_t i = from; i < to; ++i) acc += static_cast<double>(s[i]) * s[i];
    return to > from ? static_cast<float>(std::sqrt(acc / static_cast<double>(to - from))) : 0.0f;
}

/// First/last frame where |sample| exceeds [threshold], or -1.
inline void findToneBounds(const float *s, size_t frames, float threshold, long &first, long &last) {
    first = -1;
    last = -1;
    for (size_t i = 0; i < frames; ++i) {
        if (std::fabs(s[i]) > threshold) {
            if (first < 0) first = static_cast<long>(i);
            last = static_cast<long>(i);
        }
    }
}

/// Largest |a - b| over two planar buffers of the same shape; also counts NaNs
/// and out-of-range samples in [a].
inline float maxAbsDiff(const float *a, const float *b, size_t count) {
    float worst = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const float d = std::fabs(a[i] - b[i]);
        if (!(d <= worst)) worst = std::isnan(d) ? INFINITY : d;
    }
    return worst;
}

inline bool allFinite(const float *s, size_t count, float limit = 4.0f) {
    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(s[i]) || std::fabs(s[i]) > limit) return false;
    }
    return true;
}

inline void checkToneFixture(const Policy &policy, const char *name, const DecodedAudioData &d,
                             unsigned int channels, float rate) {
    OSDEC_CHECK(d.success, name << ": decode failed: " << d.errorMessage);
    if (!d.success) return;
    OSDEC_CHECK(d.channels == channels, name << ": channels " << d.channels);
    OSDEC_CHECK(d.sampleRate == rate, name << ": sample rate " << d.sampleRate);
    OSDEC_CHECK(allFinite(d.samples, d.sampleCount * d.channels), name << ": NaN or out-of-range samples");

    const double duration = d.sampleCount / d.sampleRate;
    long first = 0, last = 0;
    findToneBounds(d.samples, d.sampleCount, 0.1f, first, last);
    const double onset = first / d.sampleRate;
    const double offset = last / d.sampleRate;
    std::cout << "  " << name << ": " << d.sampleCount << " frames (" << duration * 1000.0
              << " ms), tone " << onset * 1000.0 << " ms .. " << offset * 1000.0 << " ms" << std::endl;

    if (policy.exactM4aTrim) {
        // Encoder priming must be trimmed: the tone starts at 500 ms, ends at 1500 ms.
        OSDEC_CHECK(std::fabs(onset - 0.5) < 0.005, name << ": tone onset " << onset * 1000.0 << " ms (expected 500)");
        OSDEC_CHECK(std::fabs(offset - 1.5) < 0.005, name << ": tone end " << offset * 1000.0 << " ms (expected 1500)");
        // Priming and end padding trimmed to exactly 2 s.
        OSDEC_CHECK(d.sampleCount == static_cast<size_t>(2 * rate),
                    name << ": duration " << duration * 1000.0 << " ms (expected 2000)");
    } else {
        OSDEC_CHECK(std::fabs(offset - onset - 1.0) < 0.01, name << ": tone length " << (offset - onset) * 1000.0 << " ms");
        OSDEC_CHECK(duration >= 1.99 && duration < 2.2, name << ": duration " << duration * 1000.0 << " ms");
    }
    if (first < 0) return;

    // Silence around the tone, and its level on each channel.
    const size_t sr = static_cast<size_t>(d.sampleRate);
    const size_t toneStart = static_cast<size_t>(first);
    OSDEC_CHECK(rms(d.samples, 0, toneStart > sr / 10 ? toneStart - sr / 10 : 0) < 0.01f,
                name << ": leading silence not silent");
    const size_t tailFrom = static_cast<size_t>(last) + sr / 10;
    OSDEC_CHECK(rms(d.samples, std::min(tailFrom, d.sampleCount), d.sampleCount) < 0.01f,
                name << ": trailing silence not silent");
    const float toneRms = rms(d.samples, toneStart + sr / 10, toneStart + sr * 9 / 10);
    OSDEC_CHECK(std::fabs(toneRms - 0.5f / std::sqrt(2.0f)) < 0.03f, name << ": L tone RMS " << toneRms);
    if (channels == 2) {
        const float *right = d.samples + d.sampleCount;
        const float rightRms = rms(right, toneStart + sr / 10, toneStart + sr * 9 / 10);
        OSDEC_CHECK(std::fabs(rightRms - 0.25f / std::sqrt(2.0f)) < 0.02f, name << ": R tone RMS " << rightRms);
    }
}

inline void testFixtures(const Policy &policy, const std::string &dir) {
    std::cout << "[Test] AAC-LC M4A fixtures via decodeFile and decodeMemory" << std::endl;
    struct Fixture { const char *file; unsigned int ch; float rate; };
    const Fixture fixtures[] = {
        {"aac_lc_mono_44100.m4a", 1, 44100.0f},
        {"aac_lc_stereo_48000.m4a", 2, 48000.0f},
    };
    for (const auto &f : fixtures) {
        const std::string path = joinPath(dir, f.file);
        auto fromFile = decodeFileTimed(path);
        checkToneFixture(policy, f.file, fromFile->d, f.ch, f.rate);

        const auto bytes = readFile(path);
        auto fromMem = decodeMemoryTimed(bytes);
        OSDEC_CHECK(fromMem->d.success, f.file << ": decodeMemory failed: " << fromMem->d.errorMessage);
        if (fromFile->d.success && fromMem->d.success) {
            const bool sameShape = fromMem->d.sampleCount == fromFile->d.sampleCount &&
                                   fromMem->d.channels == fromFile->d.channels &&
                                   fromMem->d.sampleRate == fromFile->d.sampleRate;
            OSDEC_CHECK(sameShape, f.file << ": memory/file decode shape differs ("
                                          << fromMem->d.sampleCount << " vs " << fromFile->d.sampleCount << " frames)");
            if (sameShape) {
                const float diff = maxAbsDiff(fromMem->d.samples, fromFile->d.samples,
                                              fromFile->d.sampleCount * fromFile->d.channels);
                OSDEC_CHECK(diff <= policy.memFileTolerance, f.file << ": memory/file PCM differs by " << diff);
            }
        }
    }
}

inline void expectFailure(const char *name, const DecodedAudioData &d, double seconds) {
    OSDEC_CHECK(!d.success, name << ": expected failure but decode succeeded");
    OSDEC_CHECK(d.samples == nullptr, name << ": failure returned samples");
    OSDEC_CHECK(!d.errorMessage.empty(), name << ": failure without error message");
    OSDEC_CHECK(seconds < 15.0, name << ": took " << seconds << " s");
    std::cout << "  " << name << ": \"" << d.errorMessage << "\" (" << seconds * 1000.0 << " ms)" << std::endl;
}

inline void testMalformed(const Policy &policy, const std::string &dir, const std::string &tmpDir) {
    std::cout << "[Test] Malformed, truncated, empty and unsupported input" << std::endl;
    const std::string tmp = joinPath(tmpDir, "osdec_malformed.m4a");

    writeFile(tmp, {});
    { auto r = decodeFileTimed(tmp); expectFailure("empty file", r->d, r->seconds); }
    { auto r = decodeFileTimed(joinPath(dir, "missing.m4a")); expectFailure("missing file", r->d, r->seconds); }
    { auto r = timed([] { return NativeAudioDecoder::decodeMemory(nullptr, 0); }); expectFailure("empty memory", r->d, r->seconds); }
    { auto r = timed([] { return NativeAudioDecoder::decodeFile(""); }); expectFailure("empty path", r->d, r->seconds); }

    // 'ftyp' header followed by garbage.
    std::vector<unsigned char> garbage = {0, 0, 0, 0x18, 'f', 't', 'y', 'p', 'M', '4', 'A', ' '};
    std::srand(1234);
    for (int i = 0; i < 4096; ++i) garbage.push_back(static_cast<unsigned char>(std::rand()));
    writeFile(tmp, garbage);
    { auto r = decodeFileTimed(tmp); expectFailure("ftyp + garbage (file)", r->d, r->seconds); }
    { auto r = decodeMemoryTimed(garbage); expectFailure("ftyp + garbage (mem)", r->d, r->seconds); }

    // Truncations of both fixtures (moov at the end for ffmpeg's, at the start for afconvert's).
    for (const char *name : {"aac_lc_mono_44100.m4a", "aac_lc_stereo_48000.m4a"}) {
        const auto bytes = readFile(joinPath(dir, name));
        auto whole = decodeMemoryTimed(bytes);
        for (double frac : {0.05, 0.3, 0.6, 0.9, 0.99}) {
            std::vector<unsigned char> cut(bytes.begin(), bytes.begin() + static_cast<long>(bytes.size() * frac));
            writeFile(tmp, cut);
            const std::string label = std::string(name) + " truncated to " + std::to_string(static_cast<int>(frac * 100)) + "%";
            auto r = decodeFileTimed(tmp);
            if (policy.truncatedM4aMayDecode && r->d.success) {
                OSDEC_CHECK(r->d.samples != nullptr && r->d.sampleCount > 0, label << ": success without samples");
                // The cut may only have removed bytes after the audio.
                OSDEC_CHECK(!whole->d.success || r->d.sampleCount <= whole->d.sampleCount,
                            label << ": decoded " << r->d.sampleCount << " frames, more than the whole file");
                OSDEC_CHECK(allFinite(r->d.samples, r->d.sampleCount * r->d.channels), label << ": out-of-range samples");
                std::cout << "  " << label << ": decoded the " << r->d.sampleCount << " frames it has" << std::endl;
            } else {
                expectFailure(label.c_str(), r->d, r->seconds);
            }
        }

        // Corrupted payload: decoding may conceal errors or fail, but must
        // terminate promptly and never return out-of-range data.
        for (unsigned seed = 1; seed <= 5; ++seed) {
            std::vector<unsigned char> bad = bytes;
            std::srand(seed);
            for (int i = 0; i < 200; ++i) {
                const size_t pos = bad.size() / 4 + static_cast<size_t>(std::rand()) % (bad.size() / 2);
                bad[pos] = static_cast<unsigned char>(std::rand());
            }
            writeFile(tmp, bad);
            auto r = decodeFileTimed(tmp);
            OSDEC_CHECK(r->seconds < 15.0, name << " corrupted seed " << seed << ": took " << r->seconds << " s");
            if (r->d.success) {
                OSDEC_CHECK(r->d.samples != nullptr && r->d.sampleCount > 0, "corrupted: success without samples");
                OSDEC_CHECK(r->d.sampleCount < 10u * 48000u, "corrupted: implausible length " << r->d.sampleCount);
                OSDEC_CHECK(allFinite(r->d.samples, r->d.sampleCount * r->d.channels, 16.0f),
                            "corrupted: NaN or wildly out-of-range samples");
            } else {
                OSDEC_CHECK(r->d.samples == nullptr && !r->d.errorMessage.empty(), "corrupted: failure without message");
            }
            std::cout << "  " << name << " corrupted seed " << seed << ": "
                      << (r->d.success ? "decoded " + std::to_string(r->d.sampleCount) + " frames" : r->d.errorMessage)
                      << std::endl;
        }
    }
    std::remove(tmp.c_str());
}

inline void testElementaryStreams(const Policy &policy, const std::string &dir) {
    std::cout << "[Test] Elementary streams (ADTS AAC, AC-3, E-AC-3)" << std::endl;
    // Reference frame counts from ffmpeg.
    struct Stream { const char *file; size_t refFrames; bool optional; };
    const Stream streams[] = {
        {"sample-AAC.aac", 662528, false},
        {"sample-AC3.ac3", 662016, policy.ac3Optional},
        {"sample-EAC3.eac3", 662016, policy.ac3Optional},
    };
    for (const auto &st : streams) {
        const std::string path = joinPath(dir, st.file);
        const auto bytes = readFile(path);
        auto fromFile = decodeFileTimed(path);
        auto fromMem = decodeMemoryTimed(bytes);
        for (Decoded *r : {fromFile.get(), fromMem.get()}) {
            const char *how = r == fromFile.get() ? "file" : "mem";
            OSDEC_CHECK(r->seconds < 30.0, st.file << " (" << how << "): took " << r->seconds << " s");
            if (!r->d.success) {
                OSDEC_CHECK(st.optional, st.file << " (" << how << "): decode failed: " << r->d.errorMessage);
                OSDEC_CHECK(!r->d.errorMessage.empty() && r->d.samples == nullptr,
                            st.file << " (" << how << "): failure without message");
                std::cout << "  " << st.file << " (" << how << "): \"" << r->d.errorMessage << "\"" << std::endl;
                continue;
            }
            const long diff = static_cast<long>(r->d.sampleCount) - static_cast<long>(st.refFrames);
            std::cout << "  " << st.file << " (" << how << "): " << r->d.sampleCount << " frames, "
                      << r->d.channels << "ch " << r->d.sampleRate << " Hz, ref diff " << diff << " ("
                      << r->seconds * 1000.0 << " ms)" << std::endl;
            OSDEC_CHECK(std::labs(diff) <= policy.elementaryFrameSlack,
                        st.file << " (" << how << "): decoded " << r->d.sampleCount << " frames, expected ~" << st.refFrames);
            const bool formatOk = (st.file == std::string("sample-AAC.aac"))
                ? (r->d.channels == 2 && r->d.sampleRate == 44100.0f)
                : ((r->d.channels == 2 || r->d.channels == 6) && r->d.sampleRate == 44100.0f);
            OSDEC_CHECK(formatOk, st.file << ": unexpected format (" << r->d.channels << "ch " << r->d.sampleRate << "Hz)");
            OSDEC_CHECK(allFinite(r->d.samples, r->d.sampleCount * r->d.channels), st.file << ": out-of-range samples");
            OSDEC_CHECK(rms(r->d.samples, 0, r->d.sampleCount) > 0.01f, st.file << ": decoded silence");
        }
        if (fromFile->d.success && fromMem->d.success) {
            OSDEC_CHECK(fromFile->d.sampleCount == fromMem->d.sampleCount,
                        st.file << ": file/memory frame counts differ");
        }
    }
}

/// Number of complete ADTS frames in [size] bytes (walks frame headers).
inline size_t countAdtsFrames(const unsigned char *data, size_t size) {
    size_t offset = 0, frames = 0;
    while (offset + 7 <= size && data[offset] == 0xFF && (data[offset + 1] & 0xF6) == 0xF0) {
        const size_t len = ((data[offset + 3] & 0x03) << 11) | (data[offset + 4] << 3) | (data[offset + 5] >> 5);
        if (len < 7 || offset + len > size) break;
        offset += len;
        ++frames;
    }
    return frames;
}

inline void testAdtsWithAc3LikeBytes(const Policy &policy, const std::string &dir) {
    std::cout << "[Test] ADTS followed by AC-3-like bytes still decodes as AAC" << std::endl;
    auto bytes = readFile(joinPath(dir, "sample-AAC.aac"));
    const size_t aacBytes = bytes.size();
    auto reference = decodeMemoryTimed(bytes);
    OSDEC_CHECK(reference->d.success, "reference ADTS decode failed: " << reference->d.errorMessage);
    if (!reference->d.success) return;

    const unsigned char ac3Like[] = {0x0B, 0x77, 0x00, 0x00, 0x00, 0x40};
    bytes.insert(bytes.end(), std::begin(ac3Like), std::end(ac3Like));
    OSDEC_CHECK(NativeAudioDecoder::findAc3Syncword(bytes.data(), bytes.size()) == static_cast<int>(aacBytes),
                "test fixture does not contain the expected AC-3-like suffix");

    auto decoded = decodeMemoryTimed(bytes);
    OSDEC_CHECK(decoded->d.success, "ADTS with AC-3-like bytes failed: " << decoded->d.errorMessage);
    if (!decoded->d.success) return;
    const bool sameShape = decoded->d.sampleRate == reference->d.sampleRate &&
                           decoded->d.channels == reference->d.channels &&
                           decoded->d.sampleCount == reference->d.sampleCount;
    OSDEC_CHECK(sameShape, "AC-3-like suffix changed the decode: " << decoded->d.sampleCount << " frames, expected "
                                                                    << reference->d.sampleCount);
    if (sameShape) {
        const float diff = maxAbsDiff(decoded->d.samples, reference->d.samples,
                                      reference->d.sampleCount * reference->d.channels);
        OSDEC_CHECK(diff <= policy.memFileTolerance, "AC-3-like suffix changed the PCM by " << diff);
    }
}

inline void testTruncatedAdts(const std::string &dir) {
    std::cout << "[Test] ADTS cut mid-frame decodes every complete frame" << std::endl;
    const auto bytes = readFile(joinPath(dir, "sample-AAC.aac"));
    auto full = decodeMemoryTimed(bytes);
    OSDEC_CHECK(full->d.success, "full ADTS decode failed: " << full->d.errorMessage);
    if (!full->d.success) return;
    const size_t totalFrames = countAdtsFrames(bytes.data(), bytes.size());
    for (double frac : {0.25, 0.5, 0.9}) {
        const size_t cut = static_cast<size_t>(bytes.size() * frac) + 3;
        const size_t kept = countAdtsFrames(bytes.data(), cut);
        auto r = timed([&] { return NativeAudioDecoder::decodeMemory(bytes.data(), cut); });
        OSDEC_CHECK(r->d.success, "cut at " << frac << ": " << r->d.errorMessage);
        OSDEC_CHECK(r->seconds < 10.0, "cut at " << frac << ": took " << r->seconds << " s");
        // Each AAC frame is 1024 PCM frames; only the dropped ones may be missing.
        const size_t expected = full->d.sampleCount - (totalFrames - kept) * 1024;
        std::cout << "  cut at " << frac << ": " << kept << "/" << totalFrames << " ADTS frames -> "
                  << r->d.sampleCount << " PCM frames (expected " << expected << ")" << std::endl;
        OSDEC_CHECK(r->d.sampleCount == expected,
                    "cut at " << frac << ": decoded " << r->d.sampleCount << " frames, expected " << expected);
    }
}

/// Feeds [bytes] to an AACDecoderWrapper in [chunk]-byte pieces, pulling at
/// most [maxOut] samples per call, the way the buffer streams do. Returns the
/// interleaved PCM, or an empty vector with [ok] false on an error.
inline std::vector<float> streamDecode(DetectedType format, const std::vector<unsigned char> &bytes, size_t chunk,
                                       size_t maxOut, int &sampleRate, int &channels, bool &ok,
                                       bool &finished) {
    AACDecoderWrapper wrapper(format);
    ok = wrapper.initializeDecoder(0, 0);
    finished = false;
    std::vector<float> interleaved;
    sampleRate = 0;
    channels = 0;
    if (!ok) return interleaved;
    auto pull = [&](std::vector<unsigned char> &in) {
        auto [samples, err] = wrapper.decode(in, &sampleRate, &channels, maxOut);
        if (err != DecoderError::NoError || (maxOut > 0 && samples.size() > maxOut)) ok = false;
        interleaved.insert(interleaved.end(), samples.begin(), samples.end());
    };
    for (size_t off = 0; off < bytes.size() && ok; off += chunk) {
        std::vector<unsigned char> piece(bytes.begin() + static_cast<long>(off),
                                         bytes.begin() + static_cast<long>(std::min(bytes.size(), off + chunk)));
        pull(piece);
    }
    wrapper.setDataEnded();
    std::vector<unsigned char> empty;
    for (int i = 0; i < 1000000 && ok && wrapper.hasPendingData(); ++i) pull(empty);
    finished = !wrapper.hasPendingData();
    return interleaved;
}

/// The known bug, if any, that a chunked-streaming check with [chunk]-byte
/// pieces runs into.
inline const char *streamKnownBug(const Policy &policy, size_t chunk) {
    if (policy.knownBugStreaming) return policy.knownBugStreaming;
    return chunk < 2048 ? policy.knownBugSmallChunks : nullptr;
}

/// Chunked streaming decode of [file] matches the whole-buffer decode.
inline void testChunkedStreaming(const Policy &policy, const std::string &dir, const char *file,
                                 DetectedType format, size_t chunk, size_t maxOut) {
    std::cout << "[Test] Chunked streaming of " << file << " (" << chunk << "-byte chunks, max " << maxOut
              << " samples per call) matches the whole decode" << std::endl;
    const auto bytes = readFile(joinPath(dir, file));
    auto whole = decodeMemoryTimed(bytes);
    const bool optional = format != DetectedType::BUFFER_AAC && policy.ac3Optional;
    if (!whole->d.success) {
        OSDEC_CHECK(optional, file << ": whole decode failed: " << whole->d.errorMessage);
        std::cout << "  no decoder for " << file << ", skipped: " << whole->d.errorMessage << std::endl;
        return;
    }

    int sampleRate = 0, channels = 0;
    bool ok = false, finished = false;
    const auto interleaved = streamDecode(format, bytes, chunk, maxOut, sampleRate, channels, ok, finished);
    OSDEC_CHECK_OR_KNOWN(policy.knownBugStreaming, ok, file << ": chunked decode failed to initialize, returned an error or exceeded maxOutputSamples");
    OSDEC_CHECK_OR_KNOWN(policy.knownBugStreaming, finished, file << ": chunked decode still has pending data");
    OSDEC_CHECK_OR_KNOWN(streamKnownBug(policy, chunk),
                channels == static_cast<int>(whole->d.channels) && sampleRate == static_cast<int>(whole->d.sampleRate),
                file << ": chunked format " << channels << "ch " << sampleRate << " Hz, whole "
                     << whole->d.channels << "ch " << whole->d.sampleRate << " Hz");
    if (channels <= 0) return;

    const size_t frames = interleaved.size() / static_cast<size_t>(channels);
    const long diff = static_cast<long>(frames) - static_cast<long>(whole->d.sampleCount);
    std::cout << "  chunked: " << frames << " frames, whole: " << whole->d.sampleCount << " frames" << std::endl;
    const char *known = streamKnownBug(policy, chunk);
    if (!known && format != DetectedType::BUFFER_AAC && diff == -1536) known = policy.knownBugAc3Tail;
    OSDEC_CHECK_OR_KNOWN(known, std::labs(diff) <= policy.streamFrameSlack,
                         file << ": chunked decode produced " << frames << " frames, expected " << whole->d.sampleCount);
    if (channels != static_cast<int>(whole->d.channels)) return;
    const size_t common = std::min(frames, whole->d.sampleCount);
    float worst = 0.0f;
    for (size_t i = 0; i < common; ++i) {
        for (int c = 0; c < channels; ++c) {
            const float d = std::fabs(interleaved[i * channels + c] - whole->d.samples[c * whole->d.sampleCount + i]);
            if (!(d <= worst)) worst = std::isnan(d) ? INFINITY : d;
        }
    }
    OSDEC_CHECK_OR_KNOWN(streamKnownBug(policy, chunk), worst <= policy.streamTolerance,
                         file << ": chunked and whole decode differ by up to " << worst);
}

/// Repeated success/failure cycles leave no temporary files behind.
inline void testRepeatedCycles(const std::string &dir, const std::string &tmpDir, const std::string &scanDir,
                               int cycles = 30) {
    std::cout << "[Test] Repeated success/failure cycles leave no temporary files" << std::endl;
    const std::string good = joinPath(dir, "aac_lc_mono_44100.m4a");
    const std::string bad = joinPath(tmpDir, "osdec_cycle_bad.m4a");
    const auto bytes = readFile(good);
    writeFile(bad, std::vector<unsigned char>(bytes.begin(), bytes.begin() + static_cast<long>(bytes.size() / 2)));

    auto countEntries = [](const std::string &path) -> long {
        std::error_code ec;
        long n = 0;
        for (std::filesystem::directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) ++n;
        return ec ? -1 : n;
    };
    { DecodedAudioData d = NativeAudioDecoder::decodeFile(good.c_str()); d.freeSamples(); }
    const long before = scanDir.empty() ? 0 : countEntries(scanDir);
    int unexpected = 0;
    for (int i = 0; i < cycles; ++i) {
        DecodedAudioData a = NativeAudioDecoder::decodeFile(good.c_str());
        DecodedAudioData b = NativeAudioDecoder::decodeMemory(bytes.data(), bytes.size());
        DecodedAudioData c = NativeAudioDecoder::decodeFile(bad.c_str());
        if (!a.success || !b.success) ++unexpected;
        a.freeSamples();
        b.freeSamples();
        c.freeSamples();
    }
    const long after = scanDir.empty() ? 0 : countEntries(scanDir);
    std::cout << "  " << cycles << " cycles, entries in " << (scanDir.empty() ? "(none)" : scanDir) << ": "
              << before << " -> " << after << std::endl;
    OSDEC_CHECK(unexpected == 0, unexpected << " cycles failed to decode the good fixture");
    OSDEC_CHECK(after == before, "temporary files left behind in " << scanDir);
    std::remove(bad.c_str());
}

/// Several threads decoding at once get the same PCM as one thread alone.
inline void testConcurrentDecoding(const std::string &dir, int threads = 6, int iterations = 6) {
    std::cout << "[Test] Concurrent decoding" << std::endl;
    const std::string mono = joinPath(dir, "aac_lc_mono_44100.m4a");
    const std::string stereo = joinPath(dir, "aac_lc_stereo_48000.m4a");
    const std::string adts = joinPath(dir, "sample-AAC.aac");
    const auto adtsBytes = readFile(adts);
    auto refMono = decodeFileTimed(mono);
    auto refStereo = decodeFileTimed(stereo);
    auto refAdts = decodeMemoryTimed(adtsBytes);
    if (!refMono->d.success || !refStereo->d.success || !refAdts->d.success) {
        OSDEC_CHECK(false, "reference decodes failed");
        return;
    }
    std::atomic<int> mismatches{0};
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&, t] {
            for (int i = 0; i < iterations; ++i) {
                const int kind = (t + i) % 3;
                const DecodedAudioData &ref = kind == 0 ? refMono->d : kind == 1 ? refStereo->d : refAdts->d;
                DecodedAudioData d = kind == 0   ? NativeAudioDecoder::decodeFile(mono.c_str())
                                     : kind == 1 ? NativeAudioDecoder::decodeFile(stereo.c_str())
                                                 : NativeAudioDecoder::decodeMemory(adtsBytes.data(), adtsBytes.size());
                if (!d.success || d.sampleCount != ref.sampleCount || d.channels != ref.channels ||
                    maxAbsDiff(d.samples, ref.samples, ref.sampleCount * ref.channels) > 1e-6f) {
                    ++mismatches;
                }
                d.freeSamples();
            }
        });
    }
    for (auto &th : pool) th.join();
    OSDEC_CHECK(mismatches == 0, mismatches << " concurrent decodes failed or differed");
}

/// Whether every fixture this header needs is in [dir].
inline bool haveFixtures(const std::string &dir, std::string &missing) {
    for (const char *f : {"aac_lc_mono_44100.m4a", "aac_lc_stereo_48000.m4a", "sample-AAC.aac", "sample-AC3.ac3",
                          "sample-EAC3.eac3"}) {
        if (!fileExists(joinPath(dir, f))) {
            missing = joinPath(dir, f);
            return false;
        }
    }
    return true;
}

/// Runs every check above. [tmpDir] is where the checks write scratch files;
/// [scanDir] is the directory the decoder itself creates temporary files in
/// (empty to skip that check).
inline void runAll(const Policy &policy, const std::string &dir, const std::string &tmpDir,
                   const std::string &scanDir) {
    testFixtures(policy, dir);
    testMalformed(policy, dir, tmpDir);
    testElementaryStreams(policy, dir);
    testAdtsWithAc3LikeBytes(policy, dir);
    testTruncatedAdts(dir);
    testChunkedStreaming(policy, dir, "sample-AAC.aac", DetectedType::BUFFER_AAC, 4096, 4096);
    testChunkedStreaming(policy, dir, "sample-AAC.aac", DetectedType::BUFFER_AAC, 7, 0);
    testChunkedStreaming(policy, dir, "sample-AC3.ac3", DetectedType::BUFFER_AC3, 4096, 4096);
    testChunkedStreaming(policy, dir, "sample-AC3.ac3", DetectedType::BUFFER_AC3, 500, 0);
    testChunkedStreaming(policy, dir, "sample-EAC3.eac3", DetectedType::BUFFER_EAC3, 4096, 4096);
    testChunkedStreaming(policy, dir, "sample-EAC3.eac3", DetectedType::BUFFER_EAC3, 333, 0);
    testChunkedStreaming(policy, dir, "sample-AAC.aac", DetectedType::BUFFER_AAC, 100, 1000);
    testRepeatedCycles(dir, tmpDir, scanDir);
    testConcurrentDecoding(dir);
}

/// Prints the summary line and returns the process exit code.
inline int finish(const char *suite) {
    if (knownBugs() > 0) {
        std::cout << knownBugs() << " KNOWN BUG check(s) failed"
                  << (strictKnownBugs() ? " (counted: OSDEC_STRICT=1)" : " (not counted; OSDEC_STRICT=1 counts them)")
                  << std::endl;
    }
    if (failures() == 0) {
        std::cout << "ALL " << suite << " TESTS PASSED" << std::endl;
        return 0;
    }
    std::cout << failures() << " CHECK(S) FAILED in " << suite << std::endl;
    return 1;
}

} // namespace osdec

#endif // OS_DECODER_CHECKS_H
