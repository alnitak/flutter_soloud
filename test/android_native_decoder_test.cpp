// On-device tests for the Android MediaCodec decoder (os_decoder_android.cpp).
// Build & run with test/run_android_native_decoder_test.sh.
//
// Fixtures (example/assets/audio): 2 s of AAC-LC in M4A with 0.5 s silence,
// a 1 kHz tone from 0.5 s to 1.5 s (L amplitude 0.5, R amplitude 0.25), then
// 0.5 s silence.

#include "../src/native_decoder/native_audio_decoder.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

static int gFailures = 0;

#define CHECK(cond, msg)                                                              \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::cout << "  FAIL: " << msg << "  (" #cond ") at line " << __LINE__     \
                      << std::endl;                                                   \
            ++gFailures;                                                              \
        }                                                                             \
    } while (0)

static std::string gDir;

static std::vector<unsigned char> readFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), {});
}

static void writeFile(const std::string &path, const std::vector<unsigned char> &bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

/// Counts this process's descriptors that point into `prefix`.
static int countFdsUnder(const std::string &prefix) {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) return -1;
    int n = 0;
    char target[PATH_MAX];
    while (dirent *e = readdir(dir)) {
        std::string link = std::string("/proc/self/fd/") + e->d_name;
        ssize_t len = readlink(link.c_str(), target, sizeof(target) - 1);
        if (len <= 0) continue;
        target[len] = 0;
        if (std::strncmp(target, prefix.c_str(), prefix.size()) == 0) ++n;
    }
    closedir(dir);
    return n;
}

static int countEntries(const char *dirPath) {
    DIR *dir = opendir(dirPath);
    if (!dir) return -1;
    int n = 0;
    while (readdir(dir)) ++n;
    closedir(dir);
    return n;
}

struct Decoded {
    DecodedAudioData d;
    double seconds = 0;
    explicit Decoded(DecodedAudioData data, double secs) : d(data), seconds(secs) {}
    ~Decoded() { d.freeSamples(); }
    Decoded(const Decoded &) = delete;
    Decoded &operator=(const Decoded &) = delete;
};

template <typename Fn>
static Decoded *timed(Fn fn) {
    auto t0 = std::chrono::steady_clock::now();
    DecodedAudioData d = fn();
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return new Decoded(d, secs);
}

static float rms(const float *s, size_t from, size_t to) {
    double acc = 0;
    for (size_t i = from; i < to; ++i) acc += static_cast<double>(s[i]) * s[i];
    return to > from ? static_cast<float>(std::sqrt(acc / (to - from))) : 0.0f;
}

/// First/last frame where the envelope exceeds `threshold`.
static void findToneBounds(const float *s, size_t frames, float threshold, long &first, long &last) {
    first = -1;
    last = -1;
    for (size_t i = 0; i < frames; ++i) {
        if (std::fabs(s[i]) > threshold) {
            if (first < 0) first = static_cast<long>(i);
            last = static_cast<long>(i);
        }
    }
}

static void checkToneFixture(const char *name, const DecodedAudioData &d, unsigned int channels, float rate) {
    CHECK(d.success, name << ": decode failed: " << d.errorMessage);
    if (!d.success) return;
    CHECK(d.channels == channels, name << ": channels " << d.channels);
    CHECK(d.sampleRate == rate, name << ": sample rate " << d.sampleRate);

    const double duration = d.sampleCount / d.sampleRate;
    long first = 0, last = 0;
    findToneBounds(d.samples, d.sampleCount, 0.1f, first, last);
    const double onset = first / d.sampleRate;
    const double offset = last / d.sampleRate;
    std::cout << "  " << name << ": " << d.sampleCount << " frames (" << duration * 1000.0
              << " ms), tone " << onset * 1000.0 << " ms .. " << offset * 1000.0 << " ms" << std::endl;

    // Encoder priming must be trimmed: the tone starts at 500 ms, ends at 1500 ms.
    CHECK(std::fabs(onset - 0.5) < 0.005, name << ": tone onset " << onset * 1000.0 << " ms (expected 500)");
    CHECK(std::fabs(offset - 1.5) < 0.005, name << ": tone end " << offset * 1000.0 << " ms (expected 1500)");
    // Duration: priming and end padding trimmed to exactly 2 s.
    CHECK(d.sampleCount == static_cast<size_t>(2 * rate),
          name << ": duration " << duration * 1000.0 << " ms (expected ~2000)");

    // Leading/trailing silence and tone level on channel 0.
    const size_t sr = static_cast<size_t>(d.sampleRate);
    CHECK(rms(d.samples, 0, sr * 4 / 10) < 0.01f, name << ": leading silence not silent");
    CHECK(rms(d.samples, sr * 16 / 10, std::min(d.sampleCount, sr * 19 / 10)) < 0.01f,
          name << ": trailing silence not silent");
    const float toneRms = rms(d.samples, sr * 6 / 10, sr * 14 / 10);
    CHECK(std::fabs(toneRms - 0.5f / std::sqrt(2.0f)) < 0.03f, name << ": L tone RMS " << toneRms);

    if (channels == 2) {
        const float *right = d.samples + d.sampleCount;
        const float rightRms = rms(right, sr * 6 / 10, sr * 14 / 10);
        CHECK(std::fabs(rightRms - 0.25f / std::sqrt(2.0f)) < 0.02f, name << ": R tone RMS " << rightRms);
    }
}

static void testFixtures() {
    std::cout << "[Test] AAC-LC M4A fixtures via decodeFile and decodeMemory" << std::endl;
    struct Fixture { const char *file; unsigned int ch; float rate; };
    const Fixture fixtures[] = {
        {"aac_lc_mono_44100.m4a", 1, 44100.0f},
        {"aac_lc_stereo_48000.m4a", 2, 48000.0f},
    };
    for (const auto &f : fixtures) {
        const std::string path = gDir + "/" + f.file;
        std::unique_ptr<Decoded> fromFile(timed([&] { return NativeAudioDecoder::decodeFile(path.c_str()); }));
        checkToneFixture(f.file, fromFile->d, f.ch, f.rate);

        auto bytes = readFile(path);
        std::unique_ptr<Decoded> fromMem(timed([&] { return NativeAudioDecoder::decodeMemory(bytes.data(), bytes.size()); }));
        CHECK(fromMem->d.success, f.file << ": decodeMemory failed: " << fromMem->d.errorMessage);
        if (fromFile->d.success && fromMem->d.success) {
            CHECK(fromMem->d.sampleCount == fromFile->d.sampleCount && fromMem->d.channels == fromFile->d.channels,
                  f.file << ": memory/file decode shape differs");
            if (fromMem->d.sampleCount == fromFile->d.sampleCount) {
                CHECK(std::memcmp(fromMem->d.samples, fromFile->d.samples,
                                  fromFile->d.sampleCount * fromFile->d.channels * sizeof(float)) == 0,
                      f.file << ": memory/file decoded PCM differs");
            }
        }
    }
}

static void expectFailure(const char *name, const DecodedAudioData &d, double seconds) {
    CHECK(!d.success, name << ": expected failure but decode succeeded");
    CHECK(d.samples == nullptr, name << ": failure returned samples");
    CHECK(!d.errorMessage.empty(), name << ": failure without error message");
    CHECK(seconds < 15.0, name << ": took " << seconds << " s");
    std::cout << "  " << name << ": \"" << d.errorMessage << "\" (" << seconds * 1000.0 << " ms)" << std::endl;
}

static void testMalformed() {
    std::cout << "[Test] Malformed, truncated, empty and unsupported input" << std::endl;
    const std::string tmp = gDir + "/malformed.m4a";

    // Empty file and missing file.
    writeFile(tmp, {});
    { std::unique_ptr<Decoded> r(timed([&] { return NativeAudioDecoder::decodeFile(tmp.c_str()); })); expectFailure("empty file", r->d, r->seconds); }
    { std::unique_ptr<Decoded> r(timed([&] { return NativeAudioDecoder::decodeFile((gDir + "/missing.m4a").c_str()); })); expectFailure("missing file", r->d, r->seconds); }
    { std::unique_ptr<Decoded> r(timed([&] { return NativeAudioDecoder::decodeMemory(nullptr, 0); })); expectFailure("empty memory", r->d, r->seconds); }

    // 'ftyp' header followed by garbage.
    std::vector<unsigned char> garbage = {0, 0, 0, 0x18, 'f', 't', 'y', 'p', 'M', '4', 'A', ' '};
    srand(1234);
    for (int i = 0; i < 4096; ++i) garbage.push_back(static_cast<unsigned char>(rand()));
    writeFile(tmp, garbage);
    { std::unique_ptr<Decoded> r(timed([&] { return NativeAudioDecoder::decodeFile(tmp.c_str()); })); expectFailure("ftyp + garbage (file)", r->d, r->seconds); }
    { std::unique_ptr<Decoded> r(timed([&] { return NativeAudioDecoder::decodeMemory(garbage.data(), garbage.size()); })); expectFailure("ftyp + garbage (mem)", r->d, r->seconds); }

    // Truncations of both fixtures (moov at end for ffmpeg, at start for afconvert).
    for (const char *name : {"aac_lc_mono_44100.m4a", "aac_lc_stereo_48000.m4a"}) {
        auto bytes = readFile(gDir + "/" + name);
        for (double frac : {0.05, 0.3, 0.6, 0.9, 0.99}) {
            std::vector<unsigned char> cut(bytes.begin(), bytes.begin() + static_cast<long>(bytes.size() * frac));
            writeFile(tmp, cut);
            std::string label = std::string(name) + " truncated to " + std::to_string(static_cast<int>(frac * 100)) + "%";
            std::unique_ptr<Decoded> r(timed([&] { return NativeAudioDecoder::decodeFile(tmp.c_str()); }));
            expectFailure(label.c_str(), r->d, r->seconds);
        }

        // Corrupted payload: decoding may conceal errors or fail, but must
        // terminate promptly and never return out-of-range data.
        for (unsigned seed = 1; seed <= 5; ++seed) {
            std::vector<unsigned char> bad = bytes;
            srand(seed);
            for (int i = 0; i < 200; ++i) {
                size_t pos = bad.size() / 4 + static_cast<size_t>(rand()) % (bad.size() / 2);
                bad[pos] = static_cast<unsigned char>(rand());
            }
            writeFile(tmp, bad);
            std::unique_ptr<Decoded> r(timed([&] { return NativeAudioDecoder::decodeFile(tmp.c_str()); }));
            CHECK(r->seconds < 15.0, name << " corrupted seed " << seed << ": took " << r->seconds << " s");
            if (r->d.success) {
                CHECK(r->d.samples != nullptr && r->d.sampleCount > 0, "corrupted: success without samples");
                CHECK(r->d.sampleCount < 10u * 48000u, "corrupted: implausible length " << r->d.sampleCount);
            }
            std::cout << "  " << name << " corrupted seed " << seed << ": "
                      << (r->d.success ? "decoded " + std::to_string(r->d.sampleCount) + " frames" : r->d.errorMessage)
                      << std::endl;
        }
    }
    unlink(tmp.c_str());
}

static void testElementaryStreams() {
    std::cout << "[Test] Elementary streams (ADTS AAC, AC-3, E-AC-3)" << std::endl;
    // Reference frame counts from ffmpeg; MediaCodec may differ by its own
    // priming/flush behavior, but never by more than a couple of frames.
    struct Stream { const char *file; size_t refFrames; };
    const Stream streams[] = {
        {"sample-AAC.aac", 662528},
        {"sample-AC3.ac3", 662016},
        {"sample-EAC3.eac3", 662016},
    };
    for (const auto &st : streams) {
        const std::string path = gDir + "/" + st.file;
        auto bytes = readFile(path);
        std::unique_ptr<Decoded> fromFile(timed([&] { return NativeAudioDecoder::decodeFile(path.c_str()); }));
        std::unique_ptr<Decoded> fromMem(timed([&] { return NativeAudioDecoder::decodeMemory(bytes.data(), bytes.size()); }));
        for (Decoded *r : {fromFile.get(), fromMem.get()}) {
            const char *how = r == fromFile.get() ? "file" : "mem";
            CHECK(r->seconds < 30.0, st.file << " (" << how << "): took " << r->seconds << " s");
            if (!r->d.success) {
                // Some devices ship no AC-3/E-AC-3 decoder; that must be an
                // error, never partial audio.
                CHECK(!r->d.errorMessage.empty() && r->d.samples == nullptr,
                      st.file << " (" << how << "): failure without message");
                std::cout << "  " << st.file << " (" << how << "): \"" << r->d.errorMessage << "\"" << std::endl;
                continue;
            }
            const long diff = static_cast<long>(r->d.sampleCount) - static_cast<long>(st.refFrames);
            std::cout << "  " << st.file << " (" << how << "): " << r->d.sampleCount << " frames, "
                      << r->d.channels << "ch " << r->d.sampleRate << " Hz, ref diff " << diff << " ("
                      << r->seconds * 1000.0 << " ms)" << std::endl;
            CHECK(std::labs(diff) <= 4096, st.file << " (" << how << "): decoded " << r->d.sampleCount
                                                  << " frames, expected ~" << st.refFrames);
            CHECK(r->d.channels == 2 && r->d.sampleRate == 44100.0f, st.file << ": unexpected format");
        }
    }
}

/// Number of complete ADTS frames in `size` bytes (walks frame headers).
static size_t countAdtsFrames(const unsigned char *data, size_t size) {
    size_t offset = 0, frames = 0;
    while (offset + 7 <= size && data[offset] == 0xFF && (data[offset + 1] & 0xF6) == 0xF0) {
        const size_t len = ((data[offset + 3] & 0x03) << 11) | (data[offset + 4] << 3) | (data[offset + 5] >> 5);
        if (len < 7 || offset + len > size) break;
        offset += len;
        ++frames;
    }
    return frames;
}

static void testTruncatedAdts() {
    std::cout << "[Test] ADTS cut mid-frame decodes every complete frame" << std::endl;
    auto bytes = readFile(gDir + "/sample-AAC.aac");
    DecodedAudioData full = NativeAudioDecoder::decodeMemory(bytes.data(), bytes.size());
    CHECK(full.success, "full ADTS decode failed: " << full.errorMessage);
    const size_t totalFrames = countAdtsFrames(bytes.data(), bytes.size());
    for (double frac : {0.25, 0.5, 0.9}) {
        const size_t cut = static_cast<size_t>(bytes.size() * frac) + 3;
        const size_t kept = countAdtsFrames(bytes.data(), cut);
        std::unique_ptr<Decoded> r(timed([&] { return NativeAudioDecoder::decodeMemory(bytes.data(), cut); }));
        CHECK(r->d.success, "cut at " << frac << ": " << r->d.errorMessage);
        CHECK(r->seconds < 10.0, "cut at " << frac << ": took " << r->seconds << " s");
        // Each AAC frame is 1024 PCM frames; only the dropped ones may be missing.
        const size_t expected = full.sampleCount - (totalFrames - kept) * 1024;
        std::cout << "  cut at " << frac << ": " << kept << "/" << totalFrames << " ADTS frames -> "
                  << r->d.sampleCount << " PCM frames (expected " << expected << ")" << std::endl;
        CHECK(r->d.sampleCount == expected, "cut at " << frac << ": decoded " << r->d.sampleCount
                                                       << " frames, expected " << expected);
    }
    full.freeSamples();
}

static void testResourceCleanup() {
    std::cout << "[Test] Resource cleanup across repeated success/failure cycles" << std::endl;
    const std::string good = gDir + "/aac_lc_mono_44100.m4a";
    const std::string bad = gDir + "/cleanup_bad.m4a";
    auto bytes = readFile(good);
    writeFile(bad, std::vector<unsigned char>(bytes.begin(), bytes.begin() + bytes.size() / 2));
    const char *tmpDir = std::getenv("TMPDIR");

    // Warm up once so lazily-created framework resources are not counted.
    { DecodedAudioData d = NativeAudioDecoder::decodeFile(good.c_str()); d.freeSamples(); }
    // Only descriptors the decoder opens itself (source files and temporary
    // files) are checked. Codec2 buffers are released through hwbinder, which a
    // bare executable doesn't service; the in-app Dart test covers those.
    const int fdsBefore = countFdsUnder(gDir);
    const int tmpBefore = tmpDir ? countEntries(tmpDir) : 0;

    for (int i = 0; i < 50; ++i) {
        DecodedAudioData a = NativeAudioDecoder::decodeFile(good.c_str());
        DecodedAudioData b = NativeAudioDecoder::decodeMemory(bytes.data(), bytes.size());
        DecodedAudioData c = NativeAudioDecoder::decodeFile(bad.c_str());
        CHECK(a.success && b.success && !c.success, "cycle " << i << " unexpected result");
        a.freeSamples();
        b.freeSamples();
        c.freeSamples();
    }

    const int fdsAfter = countFdsUnder(gDir);
    const int tmpAfter = tmpDir ? countEntries(tmpDir) : 0;
    std::cout << "  open fds " << fdsBefore << " -> " << fdsAfter << ", TMPDIR entries " << tmpBefore << " -> "
              << tmpAfter << std::endl;
    CHECK(fdsAfter <= fdsBefore, "file descriptors leaked: " << fdsBefore << " -> " << fdsAfter);
    CHECK(tmpAfter == tmpBefore, "temporary files left behind in TMPDIR");
    unlink(bad.c_str());
}

static void testConcurrentDecoding() {
    std::cout << "[Test] Concurrent decoding" << std::endl;
    const std::string mono = gDir + "/aac_lc_mono_44100.m4a";
    const std::string stereo = gDir + "/aac_lc_stereo_48000.m4a";
    DecodedAudioData refMono = NativeAudioDecoder::decodeFile(mono.c_str());
    DecodedAudioData refStereo = NativeAudioDecoder::decodeFile(stereo.c_str());
    std::atomic<int> mismatches{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 6; ++t) {
        threads.emplace_back([&, t] {
            const bool useMono = (t % 2) == 0;
            const DecodedAudioData &ref = useMono ? refMono : refStereo;
            for (int i = 0; i < 8; ++i) {
                DecodedAudioData d = NativeAudioDecoder::decodeFile((useMono ? mono : stereo).c_str());
                if (!d.success || d.sampleCount != ref.sampleCount ||
                    std::memcmp(d.samples, ref.samples, ref.sampleCount * ref.channels * sizeof(float)) != 0) {
                    ++mismatches;
                }
                d.freeSamples();
            }
        });
    }
    for (auto &th : threads) th.join();
    CHECK(mismatches == 0, mismatches << " concurrent decodes failed or differed");
    refMono.freeSamples();
    refStereo.freeSamples();
}

int main(int argc, char **argv) {
    // App processes run a binder thread pool; without one, remote extractor
    // references to our file descriptors are never released.
    if (void *binder = dlopen("libbinder_ndk.so", RTLD_NOW)) {
        if (auto start = reinterpret_cast<void (*)()>(dlsym(binder, "ABinderProcess_startThreadPool"))) start();
    }
    gDir = argc > 1 ? argv[1] : "/data/local/tmp/soloud_decoder_test";
    testFixtures();
    testMalformed();
    testElementaryStreams();
    testTruncatedAdts();
    testResourceCleanup();
    testConcurrentDecoding();
    if (gFailures == 0) {
        std::cout << "ALL ANDROID NATIVE DECODER TESTS PASSED" << std::endl;
        return 0;
    }
    std::cout << gFailures << " CHECK(S) FAILED" << std::endl;
    return 1;
}
