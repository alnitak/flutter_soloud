// The streaming AAC / AC-3 decoder on Windows (AACDecoderWrapper ->
// src/audiobuffer/aac_stream_decoder_windows.cpp, the Media Foundation AAC
// and Dolby MFTs), beyond what os_decoder_windows_test covers:
//
//   1. Tiny chunks (1, 2, 3, 13 bytes) and chunks that end exactly on an
//      ADTS frame boundary give the same PCM as one big buffer.
//   2. A stream shorter than three ADTS frames (1 and 2 frames): the
//      decoder primes on the first frames, so nothing may be lost or hang.
//   3. End of data: setDataEnded() drains everything, hasPendingData() goes
//      false, and decode() after the end returns nothing and no error.
//   4. Threads (fix P9): a decoder created on one thread, initialized and fed
//      on a second, and destroyed on a third, many times; several decoders on
//      parallel threads; a thread that was CoUninitialize()d before using a
//      decoder.
//   5. HE-AAC (SBR) in ADTS (test/windows/he_aac_fixture.h): decodes, at
//      44100 Hz (SBR applied) or 22050 Hz (core only), with the right length
//      and pitch.
//   6. Garbage between frames and an empty decode() before any data.
//
// Built and run by test/windows/aac_stream_decoder_windows_test.ps1 (MSVC),
// and with mingw-w64 under Wine by test/docker/windows_native.sh. The source
// is portable (no Windows API outside the COM case), so it also builds
// against the Linux / Apple implementations.
//
//   aac_stream_decoder_windows_test.exe <asset dir>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../support/os_decoder_checks.h"
#include "he_aac_fixture.h"

#if defined(_WIN32)
#include <windows.h>
#include <objbase.h>
#endif

#include <string>

using namespace osdec;

namespace {

std::string gDir;

/// Byte offsets of the ADTS frames in [b].
std::vector<size_t> adtsFrames(const std::vector<unsigned char> &b) {
    std::vector<size_t> out;
    size_t off = 0;
    while (off + 7 <= b.size() && b[off] == 0xFF && (b[off + 1] & 0xF6) == 0xF0) {
        const size_t len = ((b[off + 3] & 0x03) << 11) | (b[off + 4] << 3) | (b[off + 5] >> 5);
        if (len < 7 || off + len > b.size()) break;
        out.push_back(off);
        off += len;
    }
    return out;
}

struct Pcm {
    std::vector<float> pcm;
    int rate = 0;
    int channels = 0;
    bool ok = false;
    bool finished = false;
    size_t frames() const { return channels > 0 ? pcm.size() / channels : 0; }
};

/// Feeds [pieces] in order, then ends the data and drains.
Pcm feed(DetectedType format, const std::vector<std::vector<unsigned char>> &pieces, size_t maxOut = 0) {
    Pcm d;
    AACDecoderWrapper w(format);
    d.ok = w.initializeDecoder(0, 0);
    if (!d.ok) return d;
    auto pull = [&](std::vector<unsigned char> &in) {
        auto [samples, err] = w.decode(in, &d.rate, &d.channels, maxOut);
        if (err != DecoderError::NoError || (maxOut > 0 && samples.size() > maxOut)) d.ok = false;
        d.pcm.insert(d.pcm.end(), samples.begin(), samples.end());
    };
    for (const auto &p : pieces) {
        std::vector<unsigned char> copy = p;
        pull(copy);
        if (!d.ok) return d;
    }
    w.setDataEnded();
    std::vector<unsigned char> empty;
    for (int i = 0; i < 100000 && d.ok && w.hasPendingData(); ++i) pull(empty);
    d.finished = !w.hasPendingData();
    return d;
}

std::vector<std::vector<unsigned char>> chunked(const std::vector<unsigned char> &b, size_t chunk) {
    std::vector<std::vector<unsigned char>> out;
    for (size_t off = 0; off < b.size(); off += chunk)
        out.emplace_back(b.begin() + static_cast<long>(off),
                         b.begin() + static_cast<long>(std::min(b.size(), off + chunk)));
    return out;
}

float maxDiff(const std::vector<float> &a, const std::vector<float> &b) {
    float worst = a.size() == b.size() ? 0.0f : 1e9f;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) worst = std::max(worst, std::fabs(a[i] - b[i]));
    return worst;
}

/// Fundamental of channel [c] (Hz) from the autocorrelation peak over
/// 150..1200 Hz in the middle of the signal. Zero crossings would count the
/// SBR band's high-frequency content of a low-bitrate HE-AAC stream.
double pitch(const Pcm &d, int c) {
    if (d.channels <= c || d.rate <= 0) return 0;
    const size_t window = 4096;
    if (d.frames() < window * 2) return 0;
    const size_t start = d.frames() / 2 - window / 2;
    auto at = [&](size_t i) { return static_cast<double>(d.pcm[i * d.channels + c]); };
    const size_t minLag = static_cast<size_t>(d.rate / 1200), maxLag = static_cast<size_t>(d.rate / 150);
    double best = -1e30;
    size_t bestLag = 0;
    for (size_t lag = minLag; lag <= maxLag; ++lag) {
        double sum = 0;
        for (size_t i = 0; i < window - maxLag; ++i) sum += at(start + i) * at(start + i + lag);
        if (sum > best) {
            best = sum;
            bestLag = lag;
        }
    }
    return bestLag ? static_cast<double>(d.rate) / bestLag : 0;
}

void testTinyChunks() {
    std::cout << "[Test] Tiny and frame-aligned chunks match one big buffer" << std::endl;
    const auto bytes = readFile(joinPath(gDir, "sample-AAC.aac"));
    // A few seconds is enough and keeps 1-byte feeding fast.
    const auto frames = adtsFrames(bytes);
    OSDEC_CHECK(frames.size() > 200, "sample-AAC.aac has " << frames.size() << " ADTS frames");
    if (frames.size() <= 200) return;
    const std::vector<unsigned char> head(bytes.begin(), bytes.begin() + static_cast<long>(frames[200]));
    const Pcm whole = feed(DetectedType::BUFFER_AAC, {head});
    OSDEC_CHECK(whole.ok && whole.finished && whole.frames() > 0, "whole-buffer decode of 200 frames failed");
    std::cout << "  whole: " << whole.frames() << " frames, " << whole.channels << "ch " << whole.rate << " Hz"
              << std::endl;
    for (size_t chunk : {1, 2, 3, 13}) {
        const Pcm d = feed(DetectedType::BUFFER_AAC, chunked(head, chunk), 4096);
        std::cout << "  " << chunk << "-byte chunks: " << d.frames() << " frames" << std::endl;
        OSDEC_CHECK(d.ok && d.finished, chunk << "-byte chunks: error or still pending");
        OSDEC_CHECK(d.frames() == whole.frames(),
                    chunk << "-byte chunks: " << d.frames() << " frames, whole " << whole.frames());
        OSDEC_CHECK(maxDiff(d.pcm, whole.pcm) <= 1e-5f, chunk << "-byte chunks differ from the whole decode");
    }
    // One ADTS frame per decode() call, exactly on the boundaries.
    std::vector<std::vector<unsigned char>> aligned;
    for (size_t i = 0; i < 200; ++i)
        aligned.emplace_back(head.begin() + static_cast<long>(frames[i]),
                             head.begin() + static_cast<long>(i + 1 < 200 ? frames[i + 1] : head.size()));
    const Pcm a = feed(DetectedType::BUFFER_AAC, aligned);
    std::cout << "  frame-aligned: " << a.frames() << " frames" << std::endl;
    OSDEC_CHECK(a.ok && a.finished && a.frames() == whole.frames(),
                "frame-aligned chunks: " << a.frames() << " frames, whole " << whole.frames());
}

void testVeryShortStreams() {
    std::cout << "[Test] Streams of 1 and 2 ADTS frames" << std::endl;
    const auto bytes = readFile(joinPath(gDir, "sample-AAC.aac"));
    const auto frames = adtsFrames(bytes);
    if (frames.size() < 4) {
        OSDEC_CHECK(false, "not enough ADTS frames");
        return;
    }
    const Pcm ref = feed(DetectedType::BUFFER_AAC,
                             {std::vector<unsigned char>(bytes.begin(), bytes.begin() + static_cast<long>(frames[3]))});
    for (int n : {1, 2}) {
        const std::vector<unsigned char> part(bytes.begin(), bytes.begin() + static_cast<long>(frames[n]));
        for (size_t chunk : {part.size(), static_cast<size_t>(5)}) {
            const Pcm d = feed(DetectedType::BUFFER_AAC, chunked(part, chunk));
            std::cout << "  " << n << " frame(s), " << chunk << "-byte chunks: " << d.frames() << " PCM frames"
                      << std::endl;
            OSDEC_CHECK(d.ok, n << " frame(s): decode error");
            OSDEC_CHECK(d.finished, n << " frame(s): still pending after setDataEnded()");
            // Every frame decodes to 1024 PCM frames; a decoder may hold back
            // its priming, but it must give the frame(s) it got, not nothing.
            OSDEC_CHECK(d.frames() > 0, n << " frame(s) decoded to nothing");
            OSDEC_CHECK(d.frames() <= static_cast<size_t>(n) * 1024 + 2048,
                        n << " frame(s) decoded to " << d.frames() << " PCM frames");
            if (d.ok && d.frames() > 0 && ref.ok) {
                OSDEC_CHECK(d.rate == ref.rate && d.channels == ref.channels,
                            n << " frame(s): format " << d.channels << "ch " << d.rate << " Hz, expected "
                              << ref.channels << "ch " << ref.rate << " Hz");
            }
        }
    }
}

void testEndOfData() {
    std::cout << "[Test] End of data" << std::endl;
    const auto bytes = readFile(joinPath(gDir, "sample-AAC.aac"));
    AACDecoderWrapper w(DetectedType::BUFFER_AAC);
    OSDEC_CHECK(w.initializeDecoder(0, 0), "initializeDecoder");
    int rate = 0, channels = 0;
    std::vector<unsigned char> empty;
    auto [none, err0] = w.decode(empty, &rate, &channels);
    OSDEC_CHECK(err0 == DecoderError::NoError && none.empty(), "decode() with no data yet: error " << (int)err0);
    const std::vector<unsigned char> head(bytes.begin(), bytes.begin() + std::min<size_t>(bytes.size(), 32768));
    size_t got = 0;
    {
        std::vector<unsigned char> in = head; // decode() consumes its input
        auto [s, e] = w.decode(in, &rate, &channels, 2048);
        OSDEC_CHECK(e == DecoderError::NoError, "decode() of the first 32 KiB: error " << (int)e);
        got += s.size();
    }
    w.setDataEnded();
    int calls = 0;
    while (w.hasPendingData() && calls < 100000) {
        auto [s, e] = w.decode(empty, &rate, &channels, 2048);
        OSDEC_CHECK(e == DecoderError::NoError, "drain: error " << (int)e);
        if (e != DecoderError::NoError) break;
        got += s.size();
        ++calls;
    }
    OSDEC_CHECK(!w.hasPendingData(), "still pending after " << calls << " drain calls");
    auto [after, e2] = w.decode(empty, &rate, &channels, 2048);
    OSDEC_CHECK(e2 == DecoderError::NoError && after.empty(),
                "decode() after the end returned " << after.size() << " samples, error " << (int)e2);
    w.setDataEnded(); // twice: harmless
    OSDEC_CHECK(!w.hasPendingData(), "setDataEnded() twice brought data back");
    // Compare with the whole decode of the same 32 KiB.
    const Pcm whole = feed(DetectedType::BUFFER_AAC, {head});
    std::cout << "  drained " << got << " samples in " << calls << " calls (whole: " << whole.pcm.size() << ")"
              << std::endl;
    OSDEC_CHECK(got == whole.pcm.size(), "2048-sample pulls gave " << got << " samples, whole " << whole.pcm.size());
}

void testThreads() {
    std::cout << "[Test] Decoders across threads (create / feed / destroy on different threads)" << std::endl;
    const auto bytes = readFile(joinPath(gDir, "sample-AAC.aac"));
    const std::vector<unsigned char> head(bytes.begin(), bytes.begin() + std::min<size_t>(bytes.size(), 16384));
    const Pcm ref = feed(DetectedType::BUFFER_AAC, {head});
    std::atomic<int> errors{0};
    for (int round = 0; round < 20; ++round) {
        std::unique_ptr<AACDecoderWrapper> w;
        std::thread([&] { w.reset(new AACDecoderWrapper(DetectedType::BUFFER_AAC)); }).join();
        size_t samples = 0;
        std::thread([&] {
            if (!w->initializeDecoder(0, 0)) {
                errors++;
                return;
            }
            std::vector<unsigned char> in = head;
            int r = 0, c = 0;
            auto [s, e] = w->decode(in, &r, &c);
            if (e != DecoderError::NoError) errors++;
            samples += s.size();
        }).join();
        std::thread([&] {
            w->setDataEnded();
            std::vector<unsigned char> empty;
            int r = 0, c = 0;
            for (int i = 0; i < 10000 && w->hasPendingData(); ++i) {
                auto [s, e] = w->decode(empty, &r, &c);
                if (e != DecoderError::NoError) {
                    errors++;
                    break;
                }
                samples += s.size();
            }
        }).join();
        std::thread([&] { w.reset(); }).join();
        if (samples != ref.pcm.size()) errors++;
    }
    OSDEC_CHECK(errors == 0, errors.load() << " of 20 cross-thread rounds failed or decoded a different length");

    // Several decoders at once on parallel threads.
    std::vector<std::thread> threads;
    std::atomic<int> mismatches{0};
    for (int t = 0; t < 6; ++t) {
        threads.emplace_back([&, t] {
            const Pcm d = feed(DetectedType::BUFFER_AAC, chunked(head, 1000 + 37 * t), 4096);
            if (!d.ok || d.pcm.size() != ref.pcm.size() || maxDiff(d.pcm, ref.pcm) > 1e-5f) mismatches++;
        });
    }
    for (auto &t : threads) t.join();
    OSDEC_CHECK(mismatches == 0, mismatches.load() << " of 6 parallel decoders differed from the reference");

#if defined(_WIN32)
    // A thread whose own COM was initialized and torn down before the
    // decoder exists (an app worker thread pool).
    bool ok = false;
    std::thread([&] {
        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) CoUninitialize();
        const Pcm d = feed(DetectedType::BUFFER_AAC, {head});
        ok = d.ok && d.pcm.size() == ref.pcm.size();
    }).join();
    OSDEC_CHECK(ok, "decoding on a thread that already did CoInitializeEx/CoUninitialize failed");
#endif
}

void testHeAac() {
    std::cout << "[Test] HE-AAC (SBR) ADTS" << std::endl;
    const std::vector<unsigned char> he(kHeAacAdts, kHeAacAdts + sizeof(kHeAacAdts));
    const auto frames = adtsFrames(he);
    OSDEC_CHECK(frames.size() == 25, "fixture has " << frames.size() << " ADTS frames, expected 25");
    for (size_t chunk : {he.size(), static_cast<size_t>(100)}) {
        const Pcm d = feed(DetectedType::BUFFER_AAC, chunked(he, chunk), 4096);
        const double seconds = d.rate > 0 ? d.frames() / static_cast<double>(d.rate) : 0;
        const double left = pitch(d, 0), right = pitch(d, 1);
        std::cout << "  " << chunk << "-byte chunks: " << d.frames() << " frames, " << d.channels << "ch " << d.rate
                  << " Hz (" << seconds << " s), pitch L " << left << " Hz, R " << right << " Hz" << std::endl;
        OSDEC_CHECK(d.ok && d.finished, "HE-AAC: decode error or still pending");
        OSDEC_CHECK(d.channels == 2, "HE-AAC: " << d.channels << " channels");
        OSDEC_CHECK(d.rate == 44100 || d.rate == 22050, "HE-AAC: " << d.rate << " Hz");
        // 25 frames of 1024 core samples = 1.16 s; priming may be dropped.
        OSDEC_CHECK(seconds > 0.9 && seconds < 1.3, "HE-AAC: " << seconds << " s, expected about 1.1 s");
        OSDEC_CHECK(std::fabs(left - 440) < 25 && std::fabs(right - 660) < 35,
                    "HE-AAC: pitch L " << left << " Hz / R " << right << " Hz, expected 440 / 660");
        if (d.rate == 22050) std::cout << "  note: SBR not applied (core-only output at 22050 Hz)" << std::endl;
    }
}

void testGarbage() {
    std::cout << "[Test] Garbage between ADTS frames" << std::endl;
    const auto bytes = readFile(joinPath(gDir, "sample-AAC.aac"));
    const auto frames = adtsFrames(bytes);
    if (frames.size() < 60) return;
    std::vector<unsigned char> dirty;
    for (size_t i = 0; i < 50; ++i) {
        dirty.insert(dirty.end(), bytes.begin() + static_cast<long>(frames[i]),
                     bytes.begin() + static_cast<long>(frames[i + 1]));
        if (i % 10 == 5) dirty.insert(dirty.end(), {0x00, 0x12, 0x34, 0x56, 0x78, 0x9a, 0x00});
    }
    const Pcm d = feed(DetectedType::BUFFER_AAC, chunked(dirty, 700), 4096);
    std::cout << "  " << d.frames() << " PCM frames from 50 ADTS frames with junk" << std::endl;
    OSDEC_CHECK(d.finished, "garbage: still pending after the end");
    OSDEC_CHECK(d.frames() >= 40 * 1024, "garbage: only " << d.frames() << " PCM frames survived the junk");
}

bool runningUnderWine() {
#if defined(_WIN32)
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
#else
    return false;
#endif
}

} // namespace

int main(int argc, char **argv) {
    gDir = argc > 1 ? argv[1] : "example/assets/audio";
    if (!fileExists(joinPath(gDir, "sample-AAC.aac"))) {
        std::cout << "SKIPPED: fixture not found: " << joinPath(gDir, "sample-AAC.aac") << std::endl;
        return 0;
    }
    {
        AACDecoderWrapper probe(DetectedType::BUFFER_AAC);
        if (!probe.initializeDecoder(0, 0)) {
            std::cout << "SKIPPED: no AAC decoder on this system (Media Foundation AAC MFT missing)" << std::endl;
            return 0;
        }
    }
    if (runningUnderWine()) {
        // Wine's AAC MFT (winegstreamer) accepts the ADTS input type and then
        // produces no output at all, so every check would fail for a reason
        // that says nothing about Windows. Only real Windows can run this.
        const auto bytes = readFile(joinPath(gDir, "sample-AAC.aac"));
        const std::vector<unsigned char> head(bytes.begin(), bytes.begin() + std::min<size_t>(bytes.size(), 16384));
        const Pcm d = feed(DetectedType::BUFFER_AAC, {head});
        if (d.frames() == 0) {
            std::cout << "SKIPPED: running under Wine, whose AAC MFT decodes nothing (Wine limitation)" << std::endl;
            return 0;
        }
    }
    testTinyChunks();
    testVeryShortStreams();
    testEndOfData();
    testThreads();
    testHeAac();
    testGarbage();
    return finish("AAC STREAM DECODER (WINDOWS)");
}
