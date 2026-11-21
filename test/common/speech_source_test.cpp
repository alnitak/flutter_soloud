// Standalone native tests for SoLoud's Speech audio source
// (src/soloud/src/audiosource/speech), which the plugin plays for
// textToSpeech() (Player::textToSpeech in src/player.cpp, a single
// SoLoud::Speech member re-targeted with setText() on every call).
//
// The engine runs on SoLoud's null driver and is mixed by hand, so no audio
// device is opened.
//
// Cases (pass one name to run only that one):
//
//   basic       a short sentence renders finite, non-silent, bounded audio
//               that ends by itself within a plausible duration;
//   duration    longer text renders longer audio;
//   replay      playing the same text twice renders identical audio;
//   params      every base waveform (KW_SAW..KW_WARBLE) renders audio, a
//               different base frequency changes the output, a different
//               base speed changes the duration;
//   empty       empty text, whitespace and punctuation only, digits,
//               non-ASCII bytes: setText succeeds, play does not crash and
//               the voice ends; NULL text is INVALID_PARAMETER. With --asan
//               any UBSan diagnostic fails the run (bug O3: long digit runs
//               overflowed an int; bytes >= 0x80 reached isalpha() & co. as
//               negative chars);
//   long        very long text (~100k characters): setText succeeds, the
//               first seconds render non-silent audio, stop() frees it;
//   retarget    setText() while the speech is playing stops the old voice,
//               like the plugin does when textToSpeech() is called again.
//
// No audio device, no assets.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/common/speech_source_test.sh [--asan] [case]

#include "soloud.h"
#include "soloud_speech.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
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

constexpr unsigned kSr = 44100;
constexpr unsigned kBlock = 512;
constexpr unsigned kCh = 2;

struct Engine {
    SoLoud::Soloud soloud;
    bool ok = false;
    Engine() {
        ok = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER, kSr, kBlock,
                         kCh) == SoLoud::SO_NO_ERROR;
    }
    ~Engine() { soloud.deinit(); }
};

struct Render {
    std::vector<float> samples; // interleaved
    bool ended = false;
    double seconds() const { return samples.size() / (double)(kSr * kCh); }
    double rms() const {
        double a = 0;
        for (float v : samples) a += (double)v * v;
        return samples.empty() ? 0 : std::sqrt(a / samples.size());
    }
    float peak() const {
        float p = 0;
        for (float v : samples) p = std::max(p, std::fabs(v));
        return p;
    }
    bool finite() const {
        for (float v : samples)
            if (!std::isfinite(v)) return false;
        return true;
    }
};

/// Plays [speech] and mixes until its voice ends or [maxSeconds] pass.
Render render(Engine &e, SoLoud::Speech &speech, double maxSeconds) {
    Render r;
    const SoLoud::handle h = e.soloud.play(speech);
    std::vector<float> buf(kBlock * kCh);
    const unsigned maxBlocks = (unsigned)(maxSeconds * kSr / kBlock) + 1;
    for (unsigned b = 0; b < maxBlocks; b++) {
        e.soloud.mix(buf.data(), kBlock);
        r.samples.insert(r.samples.end(), buf.begin(), buf.end());
        if (!e.soloud.isValidVoiceHandle(h)) {
            r.ended = true;
            break;
        }
    }
    if (!r.ended) e.soloud.stop(h);
    return r;
}

/// Seconds from the first to the last sample above [threshold].
double audibleSeconds(const Render &r, float threshold = 1e-3f) {
    long first = -1, last = -1;
    for (size_t i = 0; i < r.samples.size(); i++) {
        if (std::fabs(r.samples[i]) > threshold) {
            if (first < 0) first = (long)i;
            last = (long)i;
        }
    }
    return first < 0 ? 0 : (last - first) / (double)(kSr * kCh);
}

void caseBasic(Engine &e) {
    SoLoud::Speech speech;
    CHECK(speech.setText("Hello world, this is flutter so loud.") == SoLoud::SO_NO_ERROR, "setText");
    CHECK(speech.mFrames > 0, "no speech frames for a sentence");
    const Render r = render(e, speech, 20);
    std::printf("    %d frames -> %.2f s (audible %.2f s), rms %.3f, peak %.3f, ended %d\n",
                speech.mFrames, r.seconds(), audibleSeconds(r), r.rms(), r.peak(), r.ended);
    CHECK(r.ended, "speech did not end within 20 s");
    CHECK(r.finite(), "NaN/Inf in speech output");
    CHECK(r.rms() > 0.01, "speech output is (nearly) silent: rms %f", r.rms());
    CHECK(r.peak() <= 1.0f, "output beyond full scale: %f", r.peak());
    CHECK(audibleSeconds(r) > 0.5 && r.seconds() < 10, "implausible duration %.2f s", r.seconds());
}

void caseDuration(Engine &e) {
    SoLoud::Speech speech;
    speech.setText("one");
    const Render shortR = render(e, speech, 20);
    speech.setText("one two three four five six seven eight nine ten");
    const Render longR = render(e, speech, 60);
    std::printf("    'one' %.2f s, 'one..ten' %.2f s\n", audibleSeconds(shortR), audibleSeconds(longR));
    CHECK(shortR.ended && longR.ended, "speech did not end");
    CHECK(audibleSeconds(longR) > 3 * audibleSeconds(shortR), "longer text is not longer: %f vs %f",
          audibleSeconds(longR), audibleSeconds(shortR));
}

void caseReplay(Engine &e) {
    SoLoud::Speech speech;
    speech.setText("repeat after me");
    const Render a = render(e, speech, 20);
    const Render b = render(e, speech, 20);
    CHECK(a.samples.size() == b.samples.size(), "replay length %zu vs %zu", a.samples.size(),
          b.samples.size());
    float d = 0;
    for (size_t i = 0; i < std::min(a.samples.size(), b.samples.size()); i++)
        d = std::max(d, std::fabs(a.samples[i] - b.samples[i]));
    std::printf("    replay: %zu samples, max diff %g\n", a.samples.size(), d);
    CHECK(d == 0.f, "replay differs by %g", d);
}

void caseParams(Engine &e) {
    SoLoud::Speech speech;
    speech.setText("testing waveforms");
    const char *names[] = {"saw", "triangle", "sin", "square", "pulse", "noise", "warble"};
    for (int w = 0; w <= 6; w++) {
        speech.setParams(1330, 10.0f, 0.5f, w);
        const Render r = render(e, speech, 20);
        std::printf("    waveform %-8s rms %.3f, %.2f s\n", names[w], r.rms(), r.seconds());
        CHECK(r.ended && r.finite() && r.rms() > 0.005, "waveform %s: rms %f ended %d", names[w],
              r.rms(), r.ended);
    }
    speech.setParams(1330, 10.0f, 0.5f, 3);
    const Render a = render(e, speech, 20);
    speech.setParams(2500, 10.0f, 0.5f, 3);
    const Render b = render(e, speech, 20);
    float d = 0;
    for (size_t i = 0; i < std::min(a.samples.size(), b.samples.size()); i++)
        d = std::max(d, std::fabs(a.samples[i] - b.samples[i]));
    CHECK(d > 0.01f, "base frequency has no effect (max diff %g)", d);
    // Base speed changes the duration (in klatt it scales the frame length,
    // so a smaller value is in fact faster).
    speech.setParams(1330, 5.0f, 0.5f, 3);
    const Render slow = render(e, speech, 40);
    std::printf("    speed 10 -> %.2f s, speed 5 -> %.2f s\n", a.seconds(), slow.seconds());
    CHECK(slow.ended && slow.seconds() != a.seconds(), "base speed has no effect");
}

void caseEmpty(Engine &e) {
    SoLoud::Speech speech;
    CHECK(speech.setText(nullptr) == SoLoud::INVALID_PARAMETER, "setText(NULL)");
    std::string nonAscii;
    for (int c = 128; c < 256; c++) nonAscii += (char)c;
    std::string controls;
    for (int c = 1; c < 32; c++) controls += (char)c;
    const struct { const char *label; std::string text; } texts[] = {
        {"empty", ""},
        {"spaces", "        "},
        {"punctuation", "!!!???...,,,;;;:::---"},
        // Bug O3 (fixed): digit runs too long for an int used to overflow
        // (UBSan "signed integer overflow" in tts.cpp); now they are spelled
        // out. INT_MAX itself still fits.
        {"digits", "0123456789 1234567890123456789"},
        {"int limits", "2147483647 -2147483647 2147483648 -2147483648 99999999999.5"},
        {"non-ascii bytes", nonAscii},
        {"utf-8", "caf\xc3\xa9 na\xc3\xafve \xe2\x82\xac 100 \xe6\x97\xa5\xe6\x9c\xac"},
        {"control chars", controls},
        {"one letter", "a"},
    };
    for (auto &t : texts) {
        const SoLoud::result res = speech.setText(t.text.c_str());
        // Speech frames are 10 ms each; allow that plus slack.
        const Render r = render(e, speech, speech.mFrames / 100.0 + 5);
        std::printf("    %-16s setText %d, %d frames, %.2f s, rms %.4f, ended %d\n", t.label, res,
                    speech.mFrames, r.seconds(), r.rms(), r.ended);
        CHECK(res == SoLoud::SO_NO_ERROR, "%s: setText %d", t.label, res);
        CHECK(r.ended, "%s: voice did not end", t.label);
        CHECK(r.finite(), "%s: NaN/Inf", t.label);
    }
    // Empty text must not be audible.
    speech.setText("");
    const Render r = render(e, speech, 5);
    CHECK(r.rms() < 1e-3, "empty text is audible (rms %f)", r.rms());
}

void caseLong(Engine &e) {
    SoLoud::Speech speech;
    std::string text;
    const char *words[] = {"the ", "quick ", "brown ", "fox ", "jumps ", "over ", "lazy ", "dogs. "};
    for (int i = 0; text.size() < 100000; i++) text += words[i % 8];
    const auto t0 = std::chrono::steady_clock::now();
    const SoLoud::result res = speech.setText(text.c_str());
    const double setMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("    %zu chars: setText %d in %.1f ms, %d frames, %u elements\n", text.size(), res,
                setMs, speech.mFrames, speech.mElement.getSize());
    CHECK(res == SoLoud::SO_NO_ERROR, "long setText %d", res);
    CHECK(speech.mFrames > 1000, "long text produced only %d frames", speech.mFrames);
    // Render the first 3 s only: the whole text is hours of speech.
    const Render r = render(e, speech, 3);
    std::printf("    first %.2f s: rms %.3f, ended %d\n", r.seconds(), r.rms(), r.ended);
    CHECK(!r.ended, "long speech ended within 3 s");
    CHECK(r.finite() && r.rms() > 0.01, "long speech: rms %f", r.rms());
    CHECK(e.soloud.getActiveVoiceCount() == 0, "voice still active after stop");

    // A single 100k-character "word".
    std::string word(100000, 'a');
    CHECK(speech.setText(word.c_str()) == SoLoud::SO_NO_ERROR, "long word setText");
    const Render w = render(e, speech, 1);
    std::printf("    100k-letter word: %d frames, first second rms %.3f\n", speech.mFrames, w.rms());
    CHECK(w.finite(), "long word: NaN/Inf");
}

void caseRetarget(Engine &e) {
    SoLoud::Speech speech;
    speech.setText("this sentence is interrupted before it can finish speaking");
    const SoLoud::handle h = e.soloud.play(speech);
    std::vector<float> buf(kBlock * kCh);
    for (int i = 0; i < 20; i++) e.soloud.mix(buf.data(), kBlock);
    CHECK(e.soloud.isValidVoiceHandle(h), "first voice ended too early");
    CHECK(speech.setText("second") == SoLoud::SO_NO_ERROR, "setText while playing");
    CHECK(!e.soloud.isValidVoiceHandle(h), "setText did not stop the playing voice");
    const Render r = render(e, speech, 10);
    CHECK(r.ended && r.rms() > 0.005, "second text did not play (rms %f)", r.rms());
    // Destroying the source while it plays stops its voice.
    {
        SoLoud::Speech temp;
        temp.setText("temporary speech object");
        e.soloud.play(temp);
        e.soloud.mix(buf.data(), kBlock);
    }
    e.soloud.mix(buf.data(), kBlock);
    CHECK(e.soloud.getActiveVoiceCount() == 0, "voice of a destroyed Speech still active");
    std::printf("    setText stops the old voice; destroyed source stops its voice\n");
}

} // namespace

int main(int argc, char **argv) {
    Engine e;
    if (!e.ok) {
        std::printf("SKIPPED: SoLoud null driver failed to initialize\n");
        return 0;
    }
    const struct { const char *name; void (*fn)(Engine &); } cases[] = {
        {"basic", caseBasic}, {"duration", caseDuration}, {"replay", caseReplay},
        {"params", caseParams}, {"empty", caseEmpty}, {"long", caseLong},
        {"retarget", caseRetarget},
    };
    const char *only = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0) continue;
        ran++;
        const int before = gFailures;
        std::printf("[%s]\n", c.name);
        c.fn(e);
        std::printf("  %s %s\n", gFailures == before ? "PASS" : "FAIL", c.name);
    }
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
