// The output device changes its sample rate while the engine plays (macOS):
// 44.1 -> 48 -> 96 -> 44.1 kHz. The pitch must not change and nothing may
// crash or go silent.
//
// Uses the BlackHole virtual loopback device (https://existential.audio/
// blackhole/), so the test can hear what the engine plays without touching a
// real output: the engine plays a 1 kHz sine on BlackHole's output, the test
// records BlackHole's input with its own miniaudio capture device (reopened
// after each change, so it always knows the device's current rate) and
// measures the tone's frequency (zero crossings) and level. The device's
// nominal rate is changed through CoreAudio and restored afterwards, also on
// failure or a crash (test/support/coreaudio_test_devices.h). Prints SKIPPED
// without BlackHole. When nothing comes back through the loopback (the
// process may not record audio, a TCC permission), a control tone shows it
// and only the pitch checks are skipped. No real output device or system default is touched.
//
// Build & run with test/macos/sample_rate_change_test.sh.

#import <Foundation/Foundation.h>

#include "../support/coreaudio_test_devices.h"
#include "../support/engine_ffi.h"

#include "backend/miniaudio/miniaudio.h"

#include <cmath>

using namespace enginetest;

extern "C" void setWaveformFreq(unsigned int hash, float newFreq);

namespace {

struct Capture {
    ma_context context;
    ma_device device;
    std::mutex mutex;
    std::vector<float> samples; // channel 0
    unsigned rate = 0;
    bool open = false;
};

void onCapture(ma_device *d, void *, const void *input, ma_uint32 frames) {
    auto *c = static_cast<Capture *>(d->pUserData);
    const float *in = static_cast<const float *>(input);
    std::lock_guard<std::mutex> lock(c->mutex);
    for (ma_uint32 i = 0; i < frames; ++i) c->samples.push_back(in[i * d->capture.channels]);
}

bool openCapture(Capture &c, const std::string &namePart) {
    if (ma_context_init(nullptr, 0, nullptr, &c.context) != MA_SUCCESS) return false;
    ma_device_info *play = nullptr, *cap = nullptr;
    ma_uint32 nPlay = 0, nCap = 0;
    ma_context_get_devices(&c.context, &play, &nPlay, &cap, &nCap);
    const ma_device_id *id = nullptr;
    for (ma_uint32 i = 0; i < nCap; ++i)
        if (std::string(cap[i].name).find(namePart) != std::string::npos) id = &cap[i].id;
    if (!id) {
        ma_context_uninit(&c.context);
        return false;
    }
    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.pDeviceID = id;
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 2;
    cfg.sampleRate = 0; // the device's current rate
    cfg.dataCallback = onCapture;
    cfg.pUserData = &c;
    if (ma_device_init(&c.context, &cfg, &c.device) != MA_SUCCESS) {
        ma_context_uninit(&c.context);
        return false;
    }
    c.rate = c.device.sampleRate;
    c.samples.clear();
    if (ma_device_start(&c.device) != MA_SUCCESS) {
        ma_device_uninit(&c.device);
        ma_context_uninit(&c.context);
        return false;
    }
    c.open = true;
    return true;
}

void closeCapture(Capture &c) {
    if (!c.open) return;
    ma_device_uninit(&c.device);
    ma_context_uninit(&c.context);
    c.open = false;
}

struct Measure {
    double hz = 0, rms = 0;
};

/// Records [ms] of BlackHole's input and measures the tone.
Measure measure(const std::string &namePart, int ms = 600) {
    Capture c;
    Measure m;
    if (!openCapture(c, namePart)) return m;
    sleepMs(150); // let the capture settle
    {
        std::lock_guard<std::mutex> lock(c.mutex);
        c.samples.clear();
    }
    sleepMs(ms);
    std::vector<float> s;
    {
        std::lock_guard<std::mutex> lock(c.mutex);
        s = c.samples;
    }
    const unsigned rate = c.rate;
    closeCapture(c);
    float peak = 0;
    for (float v : s) peak = std::max(peak, std::fabs(v));
    std::printf("  captured %zu frames at %u Hz, peak %.3f\n", s.size(), rate, peak);
    if (s.size() < 1000 || rate == 0) return m;
    // Rising zero crossings with linear interpolation, first to last.
    double first = -1, last = -1;
    int crossings = 0;
    double acc = 0;
    for (size_t i = 1; i < s.size(); ++i) {
        acc += static_cast<double>(s[i]) * s[i];
        if (s[i - 1] < 0 && s[i] >= 0) {
            const double t = (i - 1) + s[i - 1] / (s[i - 1] - s[i]);
            if (first < 0) first = t;
            last = t;
            ++crossings;
        }
    }
    m.rms = std::sqrt(acc / s.size());
    if (crossings > 2) m.hz = (crossings - 1) / ((last - first) / rate);
    return m;
}

/// A tone played on BlackHole by a miniaudio device of the test's own, to
/// check the loopback (and the permission to record it) before blaming the
/// engine.
struct ControlTone {
    ma_context context;
    ma_device device;
    double phase = 0;
};

void onControl(ma_device *d, void *output, const void *, ma_uint32 frames) {
    auto *t = static_cast<ControlTone *>(d->pUserData);
    float *out = static_cast<float *>(output);
    for (ma_uint32 i = 0; i < frames; ++i) {
        const float v = 0.3f * static_cast<float>(std::sin(t->phase));
        t->phase += 2.0 * M_PI * 1000.0 / d->sampleRate;
        for (ma_uint32 c = 0; c < d->playback.channels; ++c) out[i * d->playback.channels + c] = v;
    }
}

Measure measureControl(const std::string &namePart) {
    ControlTone t;
    Measure m;
    if (ma_context_init(nullptr, 0, nullptr, &t.context) != MA_SUCCESS) return m;
    ma_device_info *play = nullptr, *cap = nullptr;
    ma_uint32 nPlay = 0, nCap = 0;
    ma_context_get_devices(&t.context, &play, &nPlay, &cap, &nCap);
    const ma_device_id *id = nullptr;
    for (ma_uint32 i = 0; i < nPlay; ++i)
        if (std::string(play[i].name).find(namePart) != std::string::npos) id = &play[i].id;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.pDeviceID = id;
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.dataCallback = onControl;
    cfg.pUserData = &t;
    if (id && ma_device_init(&t.context, &cfg, &t.device) == MA_SUCCESS) {
        ma_device_start(&t.device);
        m = measure(namePart);
        ma_device_uninit(&t.device);
    }
    ma_context_uninit(&t.context);
    return m;
}

} // namespace

int main(int argc, char **argv) {
    parseArgs(argc, argv);
    @autoreleasepool {
        catest::SystemAudioRestorer::instance();
        const AudioObjectID bh = catest::findOutputByName("BlackHole");
        if (bh == kAudioObjectUnknown || !catest::hasInput(bh)) {
            std::printf("SKIPPED: the BlackHole loopback device is not installed\n");
            return 0;
        }
        const std::string name = catest::deviceName(bh);
        std::printf("loopback: %s, currently %.0f Hz\n", name.c_str(), catest::nominalRate(bh));
        const double rates[] = {44100, 48000, 96000, 44100};
        for (double r : rates)
            if (!catest::supportsRate(bh, r)) {
                std::printf("SKIPPED: %s does not offer %.0f Hz\n", name.c_str(), r);
                return 0;
            }
        ET_CHECK(catest::setNominalRate(bh, rates[0]), "%s at %.0f Hz", name.c_str(), rates[0]);
        const Measure control = measureControl(name);
        std::printf("control tone through the loopback: %.1f Hz, RMS %.3f\n", control.hz, control.rms);
        const bool canHear = control.rms > 0.05 && std::fabs(control.hz - 1000.0) < 10.0;
        if (!canHear)
            std::printf("note: nothing comes back through %s (no permission to record audio for this "
                        "process?): the pitch checks are SKIPPED, the rest runs\n", name.c_str());

        const int engineId = findEngineDevice("BlackHole");
        ET_CHECK(engineId != -2, "the engine lists BlackHole (id %d)", engineId);
        prepareEngineInit(-1);
        setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, -1);
        ET_CHECK(initEngine(engineId, 44100, 2048, 2, 0, 0, 0) == noError, "init on BlackHole at 44.1 kHz");
        unsigned int hash = 0;
        ET_CHECK(loadWaveform(0, false, 1.0f, 0.0f, &hash) == noError, "sine waveform");
        setWaveformFreq(hash, 1000.0f);
        unsigned int h = 0;
        ET_CHECK(play(hash, 0, 0.5f, 0, false, true, 0, 0, -1, -1, 1.0f, &h) == noError, "play 1 kHz");
        ET_CHECK(waitDeviceState(audioDeviceStarted), "device started");

        for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
            if (i > 0) {
                std::printf("[case] %.0f -> %.0f Hz while playing\n", rates[i - 1], rates[i]);
                ET_CHECK(catest::setNominalRate(bh, rates[i]), "device now at %.0f Hz", catest::nominalRate(bh));
                sleepMs(500);
            } else {
                std::printf("[case] %.0f Hz before any change\n", rates[i]);
            }
            ET_CHECK(isInited(), "still initialized");
            const double adv = streamAdvance(h, 500);
            ET_CHECK(adv > 0.4 && adv < 0.6, "mixing at real time (%.3f s in 0.5 s)", adv);
            ET_CHECK(getAudioDeviceState() == audioDeviceStarted, "device started (state %d)",
                     static_cast<int>(getAudioDeviceState()));
            if (canHear) {
                const Measure m = measure(name);
                std::printf("  heard %.1f Hz, RMS %.3f\n", m.hz, m.rms);
                ET_CHECK(m.rms > 0.05, "the tone is heard (RMS %.3f)", m.rms);
                ET_CHECK(std::fabs(m.hz - 1000.0) < 10.0, "pitch unchanged: %.1f Hz (expected 1000)", m.hz);
            }
        }
        std::printf("  events: %s\n", gState.describe().c_str());
        dispose();
        catest::SystemAudioRestorer::instance().restore();
        std::printf("  %s restored to %.0f Hz\n", name.c_str(), catest::nominalRate(bh));
    }
    return finish("sample rate change");
}
