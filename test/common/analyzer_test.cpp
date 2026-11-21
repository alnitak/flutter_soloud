// Standalone native tests for the visualization Analyzer (src/analyzer.cpp):
// the FFT and wave windows it hands to the data callback, fed directly with
// interleaved frames through Analyzer::onAudioData() (the engine-output tap)
// and planar frames through onBusAudio() (the bus tap).
//
// Cases (pass one name to run only that one):
//
//   peak       a bin-centred sine peaks in the expected bin, for window sizes
//              128..8192;
//   level      the peak's normalized value matches
//              20*log10(A/2 * 0.42) dB (Blackman coherent gain) mapped
//              through [minDecibels, maxDecibels]; silence is all zeros;
//   window     an off-bin sine (k + 0.5) leaks less than -50 dB five bins
//              away, which only a window like Blackman achieves (a
//              rectangular window leaks about -25 dB there);
//   wave       the wave window is the latest windowSize frames, merged as
//              the average of the channels;
//   channels   VISUALIZATION_CHANNEL_ALL gives one wave+FFT per channel,
//              each peaking at its own tone; a selected channel gives just
//              that one; channel peaks are per channel;
//   kinds      wave-only and FFT-only packets have the other count at 0;
//   smoothing  with smoothing s, a tone followed by silence decays by s per
//              packet (in linear magnitude), and setSmoothing clamps to
//              [0, 1];
//   planar     onBusAudio() (planar, with a stride) gives the same result as
//              onAudioData() (interleaved);
//   invalid    window sizes that are not powers of two in 128..8192, bad
//              kinds, channel counts and channel indices are rejected and
//              leave the analyzer as it was; disabling stops packets.
//
// No audio device, no assets. The analyzer's worker thread is real, so each
// case waits (with a timeout) for the packets it expects.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/common/analyzer_test.sh [--asan] [case]

#include "analyzer.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        ++gChecks;                                                          \
        if (!(cond)) {                                                      \
            ++gFailures;                                                    \
            std::fprintf(stderr, "    FAIL [%s:%d] %s: ", __FILE__,         \
                         __LINE__, #cond);                                  \
            std::fprintf(stderr, __VA_ARGS__);                              \
            std::fprintf(stderr, "\n");                                     \
        }                                                                   \
    } while (0)

constexpr double kPi = 3.14159265358979323846;
constexpr float kSr = 44100.f;

struct Packet {
    int32_t source = -1, channels = 0, wave = 0, fft = 0;
    std::vector<float> data;
    const float *waveOf(int c) const { return data.data() + (size_t)c * wave; }
    const float *fftOf(int c) const { return data.data() + (size_t)channels * wave + (size_t)c * fft; }
};

std::mutex gMutex;
std::vector<Packet> gPackets;

void onPacket(Analyzer &a, const float *data, int32_t channels, int32_t wave, int32_t fft) {
    Packet p;
    p.source = a.sourceId();
    p.channels = channels;
    p.wave = wave;
    p.fft = fft;
    p.data.assign(data, data + (size_t)channels * (wave + fft));
    std::lock_guard<std::mutex> l(gMutex);
    gPackets.push_back(std::move(p));
}

size_t packetCount() {
    std::lock_guard<std::mutex> l(gMutex);
    return gPackets.size();
}

void clearPackets() {
    std::lock_guard<std::mutex> l(gMutex);
    gPackets.clear();
}

/// Waits until more than [before] packets arrived; returns the newest.
bool waitPacket(size_t before, Packet &out, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> l(gMutex);
            if (gPackets.size() > before) {
                out = gPackets.back();
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

/// Interleaved frames of per-channel sines.
std::vector<float> tones(unsigned frames, const std::vector<double> &freqs,
                         const std::vector<float> &amps, unsigned startFrame = 0) {
    const unsigned ch = (unsigned)freqs.size();
    std::vector<float> v((size_t)frames * ch);
    for (unsigned i = 0; i < frames; i++)
        for (unsigned c = 0; c < ch; c++)
            v[(size_t)i * ch + c] =
                amps[c] * (float)std::sin(2 * kPi * freqs[c] * (startFrame + i) / kSr);
    return v;
}

/// Feeds [frames] to [a] and waits for the packet that analyzes them. The
/// worker only analyzes a window once a whole new one has been captured, and
/// nothing is written while waiting, so each call yields one packet.
bool feed(Analyzer &a, const std::vector<float> &interleaved, int channels, Packet &p) {
    // Respect the worker's ~12 ms dispatch interval so the previous window
    // cannot still be pending.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const size_t before = packetCount();
    a.onAudioData(interleaved.data(), (unsigned)(interleaved.size() / channels), channels);
    return waitPacket(before, p);
}

int argmax(const float *v, int n, int from = 1) {
    int best = from;
    for (int i = from; i < n; i++)
        if (v[i] > v[best]) best = i;
    return best;
}

float normalizedDb(double db, float minDb, float maxDb) {
    double v = (db - minDb) / (maxDb - minDb);
    return (float)std::max(0.0, std::min(1.0, v));
}

void resetGlobals() {
    Analyzer::setSmoothing(0.f);
    Analyzer::setMinMaxDecibels(-160.f, 0.f);
}

// ----- cases -------------------------------------------------------------------

void casePeak() {
    resetGlobals();
    Analyzer a(101);
    for (int window : {128, 256, 512, 1024, 2048, 4096, 8192}) {
        CHECK(a.setVisualizationEnabled(true, window, VISUALIZATION_FFT, VISUALIZATION_CHANNEL_MERGED, 2) ==
                  noError,
              "enable %d", window);
        const int bin = window / 8 + 3;
        const double f = bin * kSr / window;
        Packet p;
        const bool got = feed(a, tones(window, {f, f}, {0.5f, 0.5f}), 2, p);
        CHECK(got, "window %d: no packet", window);
        if (!got) continue;
        CHECK(p.fft == window / 2 && p.wave == 0 && p.channels == 1, "window %d: counts %d/%d/%d",
              window, p.wave, p.fft, p.channels);
        const int peak = argmax(p.fftOf(0), p.fft);
        std::printf("    window %4d: tone at bin %4d (%.1f Hz) -> peak bin %4d\n", window, bin, f, peak);
        CHECK(peak == bin, "window %d: peak at %d, expected %d", window, peak, bin);
    }
    a.setVisualizationEnabled(false);
}

void caseLevel() {
    Analyzer a(102);
    const int window = 1024, bin = 40;
    const double f = bin * kSr / window;
    for (auto range : {std::pair<float, float>{-160.f, 0.f}, {-100.f, -30.f}, {-60.f, 0.f}}) {
        Analyzer::setSmoothing(0.f);
        Analyzer::setMinMaxDecibels(range.first, range.second);
        CHECK(a.setVisualizationEnabled(true, window, VISUALIZATION_FFT, VISUALIZATION_CHANNEL_MERGED, 1) ==
                  noError,
              "enable");
        for (float amp : {1.0f, 0.1f, 0.01f}) {
            Packet p;
            if (!feed(a, tones(window, {f}, {amp}), 1, p)) {
                CHECK(false, "no packet");
                continue;
            }
            // |X[k]| / N for a bin-centred sine of amplitude A with a
            // (non-periodic, N-1 denominator) Blackman window is about
            // A/2 * 0.42.
            const double db = 20 * std::log10(amp / 2 * 0.42);
            const float expect = normalizedDb(db, range.first, range.second);
            const float got = p.fftOf(0)[bin];
            std::printf("    range [%4.0f, %4.0f] dB, amplitude %5.2f: bin %d = %.4f (expected %.4f)\n",
                        range.first, range.second, amp, bin, got, expect);
            CHECK(std::fabs(got - expect) < 0.01f, "level: got %f expected %f", got, expect);
            for (int k = 0; k < p.fft; k++)
                CHECK(p.fftOf(0)[k] >= 0.f && p.fftOf(0)[k] <= 1.f, "bin %d out of [0,1]: %f", k,
                      p.fftOf(0)[k]);
        }
    }
    // Silence: everything is at or below minDecibels -> 0.
    Analyzer::setMinMaxDecibels(-100.f, -30.f);
    Packet p;
    const std::vector<float> silence(window, 0.f);
    CHECK(feed(a, silence, 1, p), "no packet for silence");
    float maxv = 0;
    for (int k = 0; k < p.fft; k++) maxv = std::max(maxv, p.fftOf(0)[k]);
    CHECK(maxv == 0.f, "silence FFT not all zero (max %f)", maxv);
    a.setVisualizationEnabled(false);
    resetGlobals();
}

void caseWindow() {
    resetGlobals();
    Analyzer a(103);
    const int window = 2048;
    CHECK(a.setVisualizationEnabled(true, window, VISUALIZATION_FFT, VISUALIZATION_CHANNEL_MERGED, 1) == noError,
          "enable");
    const double bin = 100.5;
    const double f = bin * kSr / window;
    Packet p;
    CHECK(feed(a, tones(window, {f}, {0.5f}), 1, p), "no packet");
    // Normalized values are linear in dB over the 160 dB range.
    const float *v = p.fftOf(0);
    const int pk = argmax(v, p.fft);
    double worstLeak = -1e9;
    for (int k = 1; k < p.fft; k++) {
        if (std::fabs(k - bin) < 5) continue;
        worstLeak = std::max(worstLeak, (double)(v[k] - v[pk]) * 160.0);
    }
    std::printf("    off-bin tone at %.1f: peak bin %d, worst leakage >= 5 bins away %.1f dB\n", bin,
                pk, worstLeak);
    CHECK(pk == 100 || pk == 101, "off-bin peak at %d", pk);
    CHECK(worstLeak < -50, "leakage %f dB: window not applied?", worstLeak);
    a.setVisualizationEnabled(false);
}

void caseWave() {
    resetGlobals();
    Analyzer a(104);
    const int window = 256;
    CHECK(a.setVisualizationEnabled(true, window, VISUALIZATION_WAVE, VISUALIZATION_CHANNEL_MERGED, 2) == noError,
          "enable");
    // Feed 3 windows of a ramp; the packet holds the last 256 frames,
    // averaged over the two channels.
    const unsigned frames = 3 * window;
    std::vector<float> in(frames * 2);
    for (unsigned i = 0; i < frames; i++) {
        in[2 * i] = (float)i / frames;
        in[2 * i + 1] = 0.5f;
    }
    Packet p;
    CHECK(feed(a, in, 2, p), "no packet");
    CHECK(p.wave == window && p.fft == 0 && p.channels == 1, "counts %d/%d/%d", p.wave, p.fft, p.channels);
    float worst = 0;
    for (int i = 0; i < window; i++) {
        const unsigned frame = frames - window + i;
        const float expect = 0.5f * ((float)frame / frames + 0.5f);
        worst = std::max(worst, std::fabs(p.waveOf(0)[i] - expect));
    }
    std::printf("    merged wave = mean of channels over the latest %d frames: worst error %.2e\n",
                window, worst);
    CHECK(worst < 1e-6f, "merged wave error %g", worst);
    CHECK(std::fabs(a.getChannelPeak(0) - (float)(frames - 1) / frames) < 1e-6f, "peak ch0 %f",
          a.getChannelPeak(0));
    CHECK(std::fabs(a.getChannelPeak(1) - 0.5f) < 1e-6f, "peak ch1 %f", a.getChannelPeak(1));
    CHECK(a.getChannelPeak(5) == 0.f && a.getChannelPeak(-1) == 0.f, "invalid channel peak");

    // Fewer frames than a window: no new packet.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const size_t before = packetCount();
    std::vector<float> partial(100 * 2, 0.25f);
    a.onAudioData(partial.data(), 100, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(packetCount() == before, "a packet was sent for less than a window of new audio");
    a.setVisualizationEnabled(false);
    CHECK(a.getChannelPeak(0) == 0.f, "peak while disabled %f", a.getChannelPeak(0));
}

void caseChannels() {
    resetGlobals();
    Analyzer a(105);
    const int window = 1024;
    const int bins[] = {20, 50, 90, 130};
    std::vector<double> freqs;
    std::vector<float> amps;
    for (int b : bins) {
        freqs.push_back(b * kSr / window);
        amps.push_back(0.4f);
    }
    // All channels.
    CHECK(a.setVisualizationEnabled(true, window, VISUALIZATION_WAVE_AND_FFT, VISUALIZATION_CHANNEL_ALL, 4) ==
              noError,
          "enable all");
    Packet p;
    CHECK(feed(a, tones(window, freqs, amps), 4, p), "no packet");
    CHECK(p.channels == 4 && p.wave == window && p.fft == window / 2, "counts %d/%d/%d", p.channels,
          p.wave, p.fft);
    for (int c = 0; c < p.channels && c < 4; c++) {
        const int pk = argmax(p.fftOf(c), p.fft);
        CHECK(pk == bins[c], "channel %d: peak %d expected %d", c, pk, bins[c]);
        CHECK(std::fabs(p.waveOf(c)[window - 1] - amps[c] * (float)std::sin(2 * kPi * freqs[c] * (window - 1) / kSr)) < 1e-6f,
              "channel %d wave mismatch", c);
    }
    std::printf("    CHANNEL_ALL: 4 channels peak at bins %d %d %d %d\n", argmax(p.fftOf(0), p.fft),
                argmax(p.fftOf(1), p.fft), argmax(p.fftOf(2), p.fft), argmax(p.fftOf(3), p.fft));
    // One selected channel.
    for (int sel = 0; sel < 4; sel++) {
        CHECK(a.setVisualizationEnabled(true, window, VISUALIZATION_FFT, sel, 4) == noError, "enable %d", sel);
        Packet q;
        CHECK(feed(a, tones(window, freqs, amps), 4, q), "no packet");
        CHECK(q.channels == 1 && argmax(q.fftOf(0), q.fft) == bins[sel], "channel %d selection: peak %d",
              sel, argmax(q.fftOf(0), q.fft));
    }
    // Per-channel peaks, whatever the selection.
    std::vector<float> lv(window * 4);
    for (int i = 0; i < window; i++)
        for (int c = 0; c < 4; c++) lv[i * 4 + c] = (c + 1) * 0.1f * (i % 2 ? 1 : -1);
    Packet r;
    CHECK(feed(a, lv, 4, r), "no packet");
    for (int c = 0; c < 4; c++)
        CHECK(std::fabs(a.getChannelPeak(c) - (c + 1) * 0.1f) < 1e-6f, "peak ch%d %f", c, a.getChannelPeak(c));
    // Fewer captured channels than lanes: mono audio into a stereo analyzer.
    CHECK(a.setVisualizationEnabled(true, window, VISUALIZATION_WAVE, VISUALIZATION_CHANNEL_MERGED, 2) ==
              noError,
          "enable stereo");
    std::vector<float> mono(window, 0.3f);
    Packet m;
    CHECK(feed(a, mono, 1, m), "no packet for mono capture");
    CHECK(std::fabs(m.waveOf(0)[10] - 0.3f) < 1e-6f, "mono into stereo merged: %f (expected 0.3)",
          m.waveOf(0)[10]);
    a.setVisualizationEnabled(false);
}

void caseKinds() {
    resetGlobals();
    Analyzer a(106);
    const struct { VisualizationKind k; int wave, fft; } kinds[] = {
        {VISUALIZATION_WAVE, 512, 0}, {VISUALIZATION_FFT, 0, 256}, {VISUALIZATION_WAVE_AND_FFT, 512, 256}};
    for (auto &k : kinds) {
        CHECK(a.setVisualizationEnabled(true, 512, k.k, VISUALIZATION_CHANNEL_MERGED, 2) == noError, "enable");
        CHECK(a.kind() == k.k && a.windowSize() == 512, "getters");
        Packet p;
        CHECK(feed(a, tones(512, {1000, 1000}, {0.3f, 0.3f}), 2, p), "no packet");
        CHECK(p.wave == k.wave && p.fft == k.fft && p.data.size() == (size_t)(k.wave + k.fft),
              "kind %d: wave %d fft %d", k.k, p.wave, p.fft);
        CHECK(p.source == 106, "source id %d", p.source);
    }
    a.setVisualizationEnabled(false);
}

void caseSmoothing() {
    Analyzer a(107);
    const int window = 1024, bin = 64;
    const double f = bin * kSr / window;
    Analyzer::setMinMaxDecibels(-160.f, 0.f);
    const float s = 0.5f;
    Analyzer::setSmoothing(s);
    CHECK(a.setVisualizationEnabled(true, window, VISUALIZATION_FFT, VISUALIZATION_CHANNEL_MERGED, 1) == noError,
          "enable");
    // The smoothed magnitudes start at zero: after n packets of the same tone
    // the magnitude is (1 - s^n) of the steady value.
    Packet p;
    std::vector<float> vals;
    for (int i = 0; i < 6; i++) {
        CHECK(feed(a, tones(window, {f}, {0.5f}), 1, p), "no packet");
        vals.push_back(p.fftOf(0)[bin]);
    }
    const double steadyDb = 20 * std::log10(0.5 / 2 * 0.42);
    for (int i = 0; i < 6; i++) {
        const double expectDb = steadyDb + 20 * std::log10(1 - std::pow(s, i + 1));
        CHECK(std::fabs(vals[i] - normalizedDb(expectDb, -160, 0)) < 0.01f, "rise %d: %f vs %f", i,
              vals[i], normalizedDb(expectDb, -160, 0));
    }
    // Then silence: each packet halves the linear magnitude (-6.02 dB, which
    // is 6.02/160 in normalized units).
    float prev = vals.back();
    for (int i = 0; i < 4; i++) {
        CHECK(feed(a, std::vector<float>(window, 0.f), 1, p), "no packet");
        const float v = p.fftOf(0)[bin];
        const float step = (prev - v) * 160.f;
        CHECK(std::fabs(step - 6.02f) < 0.2f, "decay step %d: %f dB", i, step);
        prev = v;
    }
    std::printf("    smoothing %.1f: rise", s);
    for (float v : vals) std::printf(" %.3f", v);
    std::printf(", then -6.02 dB per silent packet\n");
    // Clamping: values outside [0, 1] are clamped (1 = frozen).
    Analyzer::setSmoothing(5.f);
    const float frozen = prev;
    CHECK(feed(a, tones(window, {f}, {0.5f}), 1, p), "no packet");
    CHECK(std::fabs(p.fftOf(0)[bin] - frozen) < 1e-6f, "smoothing 5 not clamped to 1 (%f vs %f)",
          p.fftOf(0)[bin], frozen);
    Analyzer::setSmoothing(-3.f);
    CHECK(feed(a, tones(window, {f}, {0.5f}), 1, p), "no packet");
    CHECK(std::fabs(p.fftOf(0)[bin] - normalizedDb(steadyDb, -160, 0)) < 0.01f,
          "smoothing -3 not clamped to 0 (%f)", p.fftOf(0)[bin]);
    // setMinMaxDecibels ignores min >= max.
    Analyzer::setMinMaxDecibels(-10.f, -20.f);
    CHECK(feed(a, tones(window, {f}, {0.5f}), 1, p), "no packet");
    CHECK(std::fabs(p.fftOf(0)[bin] - normalizedDb(steadyDb, -160, 0)) < 0.01f,
          "inverted dB range was applied (%f)", p.fftOf(0)[bin]);
    a.setVisualizationEnabled(false);
    resetGlobals();
}

void casePlanar() {
    resetGlobals();
    Analyzer a(108), b(109);
    const int window = 512;
    CHECK(a.setVisualizationEnabled(true, window, VISUALIZATION_WAVE_AND_FFT, VISUALIZATION_CHANNEL_ALL, 2) ==
              noError,
          "enable a");
    CHECK(b.setVisualizationEnabled(true, window, VISUALIZATION_WAVE_AND_FFT, VISUALIZATION_CHANNEL_ALL, 2) ==
              noError,
          "enable b");
    const auto inter = tones(window, {700, 2100}, {0.4f, 0.2f});
    // Planar copy with a stride larger than the frame count, like a bus
    // buffer.
    const unsigned stride = window + 64;
    std::vector<float> planar(stride * 2, 99.f);
    for (int i = 0; i < window; i++) {
        planar[i] = inter[2 * i];
        planar[stride + i] = inter[2 * i + 1];
    }
    Packet pa, pb;
    CHECK(feed(a, inter, 2, pa), "no interleaved packet");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const size_t before = packetCount();
    b.onBusAudio(planar.data(), window, stride, 2);
    CHECK(waitPacket(before, pb), "no planar packet");
    CHECK(pb.source == 109, "planar packet source %d", pb.source);
    CHECK(pa.data == pb.data, "planar and interleaved captures differ");
    std::printf("    interleaved and planar (stride %u) packets identical: %d\n", stride, pa.data == pb.data);
    a.setVisualizationEnabled(false);
    b.setVisualizationEnabled(false);
}

void caseInvalid() {
    resetGlobals();
    Analyzer a(110);
    CHECK(a.setVisualizationEnabled(true, 512, VISUALIZATION_FFT, 1, 2) == noError, "enable");
    const struct { int w; int kind; int ch; int chans; } bad[] = {
        {100, 1, -1, 2}, {64, 1, -1, 2}, {16384, 1, -1, 2}, {1000, 1, -1, 2}, {0, 1, -1, 2},
        // Kind 3 is the only out-of-range kind that is a valid value of the
        // enum type (values 0..3); anything else would itself be UB.
        {512, 3, -1, 2}, {512, 1, 2, 2}, {512, 1, -3, 2}, {512, 1, -1, 0},
        {512, 1, -1, 9}, {512, 1, 8, 8},
    };
    for (auto &b : bad) {
        const PlayerErrors e = a.setVisualizationEnabled(true, b.w, (VisualizationKind)b.kind, b.ch, b.chans);
        CHECK(e == invalidParameter, "window %d kind %d ch %d/%d: %d", b.w, b.kind, b.ch, b.chans, e);
        CHECK(a.isVisualizationEnabled() && a.windowSize() == 512 && a.kind() == VISUALIZATION_FFT &&
                  a.channelSelection() == 1,
              "rejected settings changed the analyzer");
    }
    // Still working after the rejected calls.
    Packet p;
    CHECK(feed(a, tones(512, {1000, 3000}, {0.1f, 0.4f}), 2, p), "no packet after rejected calls");
    CHECK(argmax(p.fftOf(0), p.fft) == (int)std::lround(3000 * 512 / kSr), "selected channel 1 peak %d",
          argmax(p.fftOf(0), p.fft));
    // Valid extremes.
    CHECK(a.setVisualizationEnabled(true, 128, VISUALIZATION_WAVE, -2, 8) == noError, "128 / 8 ch");
    CHECK(a.setVisualizationEnabled(true, 8192, VISUALIZATION_WAVE_AND_FFT, 7, 8) == noError, "8192 / ch 7");
    // Disabled: nothing is captured or sent; disabling twice is harmless.
    a.setVisualizationEnabled(false);
    a.setVisualizationEnabled(false);
    CHECK(!a.isVisualizationEnabled(), "still enabled");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const size_t before = packetCount();
    const auto lots = tones(8192, {440, 440}, {0.5f, 0.5f});
    a.onAudioData(lots.data(), 8192, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(packetCount() == before, "packets sent while disabled");
    CHECK(a.getChannelPeak(0) == 0.f, "peak while disabled %f", a.getChannelPeak(0));
    // Null / empty input is ignored.
    CHECK(a.setVisualizationEnabled(true, 256, VISUALIZATION_WAVE, -1, 2) == noError, "re-enable");
    a.onAudioData(nullptr, 256, 2);
    a.onAudioData(lots.data(), 0, 2);
    a.onAudioData(lots.data(), 256, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK(packetCount() == before, "null/empty input produced a packet");
    a.setVisualizationEnabled(false);
}

} // namespace

int main(int argc, char **argv) {
    Analyzer::setDataCallback(onPacket);
    const struct { const char *name; void (*fn)(); } cases[] = {
        {"peak", casePeak}, {"level", caseLevel}, {"window", caseWindow},
        {"wave", caseWave}, {"channels", caseChannels}, {"kinds", caseKinds},
        {"smoothing", caseSmoothing}, {"planar", casePlanar}, {"invalid", caseInvalid},
    };
    const char *only = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0) continue;
        ran++;
        clearPackets();
        const int before = gFailures;
        std::printf("[%s]\n", c.name);
        c.fn();
        std::printf("  %s %s\n", gFailures == before ? "PASS" : "FAIL", c.name);
    }
    Analyzer::disableAll();
    Analyzer::setDataCallback(nullptr);
    if (!ran) {
        std::fprintf(stderr, "unknown case '%s'\n", only);
        return 2;
    }
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    if (gFailures) {
        std::printf("FAILED\n");
        return 1;
    }
    std::printf("ALL PASSED\n");
    return 0;
}
