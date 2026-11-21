// Standalone native tests for the waveform reader in src/waveform/
// (Waveform::readSamples in waveform.cpp, plus the libvorbis miniaudio
// backend in miniaudio_libvorbis.cpp when the Xiph libraries are available).
// readSamples() decodes a file or a memory buffer with miniaudio and fills
// [numSamplesNeeded] floats: either the first channel's sample at the start
// of each bar (average = false) or the RMS over every channel of each bar
// (average = true). This is what readSamplesFromFile/readSamplesFromMem in
// bindings.cpp export to Dart.
//
// Cases (pass one name to run only that one):
//
//   point     float and 16-bit WAVs, mono and stereo: point samples are the
//             first channel's frame at i * (frames / n);
//   average   per-bar RMS over all channels, also for bars longer than the
//             4096-frame read chunk;
//   range     startTime/endTime select a sub-range; endTime -1 means the
//             whole file; an endTime past the end is clamped; start >= end
//             gives zeros;
//   small     asking for more samples than frames fills only one per frame,
//             zeros after, and never writes past the buffer;
//   errors    NULL output, NULL input, zero samples, garbage data, a missing
//             file;
//   file      the file path and memory buffer paths give identical results;
//   assets    example/assets/audio/{sample-WAV.wav, sample-MP3.mp3,
//             sample-FLAC.flac, sample-vorbis.ogg, tic-1.wav}: readable,
//             finite, non-silent, matching between file and memory; the OGG
//             decodes when built with Xiph, and reports noBackend otherwise.
//             Missing assets are SKIPPED.
//   vorbis    (Xiph builds only) the libvorbis backend on its own:
//             ma_libvorbis_init_file and ma_libvorbis_init_memory open the OGG
//             asset, report its format and length, and decode the same
//             frames (regression for S9: the memory path always failed).
//
// No audio device. Assets: example/assets/audio/* (optional).
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/common/waveform_test.sh [--asan] [case]

#include "enums.h"
#include "waveform/waveform.h"
#if !defined(NO_XIPH_LIBS)
#include "waveform/miniaudio_libvorbis.h"
#include "../support/temp_dir.h"
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

// ----- WAV writer ------------------------------------------------------------

void put16(std::vector<unsigned char> &b, uint16_t v) {
    b.push_back(v & 0xff);
    b.push_back(v >> 8);
}
void put32(std::vector<unsigned char> &b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xff);
}
void putTag(std::vector<unsigned char> &b, const char *t) { b.insert(b.end(), t, t + 4); }

/// Interleaved [frames] x [channels] samples into a WAV, as IEEE float (3) or
/// 16-bit PCM (1).
std::vector<unsigned char> makeWav(const std::vector<float> &interleaved, unsigned channels,
                                   unsigned sampleRate, bool asFloat) {
    const unsigned bps = asFloat ? 4 : 2;
    const uint32_t dataBytes = (uint32_t)(interleaved.size() * bps);
    std::vector<unsigned char> b;
    putTag(b, "RIFF");
    put32(b, 36 + dataBytes);
    putTag(b, "WAVE");
    putTag(b, "fmt ");
    put32(b, 16);
    put16(b, asFloat ? 3 : 1);
    put16(b, (uint16_t)channels);
    put32(b, sampleRate);
    put32(b, sampleRate * channels * bps);
    put16(b, (uint16_t)(channels * bps));
    put16(b, (uint16_t)(bps * 8));
    putTag(b, "data");
    put32(b, dataBytes);
    for (float v : interleaved) {
        if (asFloat) {
            uint32_t u;
            std::memcpy(&u, &v, 4);
            put32(b, u);
        } else {
            const float c = std::fmax(-1.f, std::fmin(1.f, v));
            put16(b, (uint16_t)(int16_t)std::lround(c * 32767.f));
        }
    }
    return b;
}

/// Frame i, channel c of the synthetic signal: a slow ramp on channel 0, so
/// every frame has a distinct value, and its negation scaled on the others.
float synth(unsigned i, unsigned c, unsigned frames) {
    const float ramp = -0.9f + 1.8f * (float)i / (float)frames;
    return c == 0 ? ramp : -0.5f * ramp;
}

std::vector<float> synthSignal(unsigned frames, unsigned channels) {
    std::vector<float> s((size_t)frames * channels);
    for (unsigned i = 0; i < frames; i++)
        for (unsigned c = 0; c < channels; c++) s[(size_t)i * channels + c] = synth(i, c, frames);
    return s;
}

ReadSamplesErrors readMem(const std::vector<unsigned char> &data, float start, float end,
                          unsigned long n, bool average, float *out) {
    return Waveform::readSamples(nullptr, data.data(), (unsigned long)data.size(), start, end, n,
                                 average, out);
}

std::string tempPath(const char *name) { return testTempPath(name); }

bool writeFile(const std::string &path, const std::vector<unsigned char> &d) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(d.data(), 1, d.size(), f) == d.size();
    std::fclose(f);
    return ok;
}

bool readFile(const std::string &path, std::vector<unsigned char> &d) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    d.resize((size_t)n);
    const bool ok = std::fread(d.data(), 1, d.size(), f) == d.size();
    std::fclose(f);
    return ok;
}

// ----- cases -------------------------------------------------------------------

void casePoint() {
    const unsigned sr = 44100, frames = 44100;
    for (unsigned channels : {1u, 2u}) {
        for (bool asFloat : {true, false}) {
            const auto wav = makeWav(synthSignal(frames, channels), channels, sr, asFloat);
            for (unsigned long n : {1ul, 100ul, 1000ul, 44100ul}) {
                std::vector<float> out(n, 123.f);
                const ReadSamplesErrors e = readMem(wav, 0, -1, n, false, out.data());
                CHECK(e == readSamplesNoError, "point ch%u float%d n%lu: error %d", channels,
                      asFloat, n, e);
                const unsigned long step = frames / n;
                float worst = 0;
                for (unsigned long i = 0; i < n; i++)
                    worst = std::max(worst, std::fabs(out[i] - synth((unsigned)(i * step), 0, frames)));
                // s16: written as x*32767, decoded as /32768, plus rounding.
                const float tol = asFloat ? 1e-7f : 2.f / 32767.f;
                CHECK(worst <= tol, "point ch%u float%d n%lu: worst error %g", channels, asFloat,
                      n, worst);
                if (n == 100)
                    std::printf("    %s %u ch, 100 points: worst error %.2e\n",
                                asFloat ? "f32" : "s16", channels, worst);
            }
        }
    }
}

void caseAverage() {
    const unsigned sr = 8000;
    // 4 s stereo: channel 0 = a, channel 1 = -a, with a = 0.5 in the first
    // half and 0.25 in the second. Each bar's RMS is a.
    for (unsigned frames : {8000u * 4, 8000u * 40}) {
        std::vector<float> s((size_t)frames * 2);
        for (unsigned i = 0; i < frames; i++) {
            const float a = i < frames / 2 ? 0.5f : 0.25f;
            s[2 * i] = a;
            s[2 * i + 1] = -a;
        }
        const auto wav = makeWav(s, 2, sr, true);
        // 10 bars over 40 s -> 32000 frames each, more than the 4096-frame
        // read chunk.
        const unsigned long n = 10;
        std::vector<float> out(n);
        CHECK(readMem(wav, 0, -1, n, true, out.data()) == readSamplesNoError, "average read");
        bool ok = true;
        for (unsigned long i = 0; i < n; i++) {
            const float expect = i < n / 2 ? 0.5f : 0.25f;
            if (std::fabs(out[i] - expect) > 1e-5f) ok = false;
        }
        std::printf("    %u frames, %lu bars of %u frames: RMS", frames, n, frames / 10);
        for (float v : out) std::printf(" %.3f", v);
        std::printf("\n");
        CHECK(ok, "average: bar RMS does not match");
    }
    // RMS of a full-scale sine over whole periods is 1/sqrt(2).
    std::vector<float> sine(44100);
    for (unsigned i = 0; i < sine.size(); i++) sine[i] = sinf(2 * (float)M_PI * 440.f * i / 44100.f);
    const auto wav = makeWav(sine, 1, 44100, true);
    std::vector<float> out(10);
    CHECK(readMem(wav, 0, -1, 10, true, out.data()) == readSamplesNoError, "sine average read");
    for (float v : out)
        CHECK(std::fabs(v - (float)M_SQRT1_2) < 1e-4f, "sine bar RMS %f", v);
}

void caseRange() {
    const unsigned sr = 1000, frames = 4000; // 4 s
    const auto wav = makeWav(synthSignal(frames, 1), 1, sr, true);
    // [1 s, 3 s): 2000 frames, 20 points with step 100 from frame 1000.
    std::vector<float> out(20);
    CHECK(readMem(wav, 1.f, 3.f, 20, false, out.data()) == readSamplesNoError, "range read");
    float worst = 0;
    for (unsigned i = 0; i < 20; i++)
        worst = std::max(worst, std::fabs(out[i] - synth(1000 + i * 100, 0, frames)));
    CHECK(worst < 1e-7f, "range [1,3): worst error %g", worst);

    // endTime -1 == whole file == endTime far past the end.
    std::vector<float> a(40), b(40), c(40);
    readMem(wav, 0, -1, 40, false, a.data());
    readMem(wav, 0, 4.f, 40, false, b.data());
    readMem(wav, 0, 100.f, 40, false, c.data());
    CHECK(a == b, "endTime -1 differs from endTime = duration");
    CHECK(a == c, "endTime past the end is not clamped");

    // start >= end: zeros, no error.
    std::vector<float> z(10, 7.f);
    CHECK(readMem(wav, 3.f, 2.f, 10, false, z.data()) == readSamplesNoError, "start > end error");
    bool zeros = true;
    for (float v : z) zeros = zeros && v == 0.f;
    CHECK(zeros, "start > end did not give zeros");
    std::fill(z.begin(), z.end(), 7.f);
    CHECK(readMem(wav, 10.f, -1, 10, true, z.data()) == readSamplesNoError, "start past end error");
    zeros = true;
    for (float v : z) zeros = zeros && v == 0.f;
    CHECK(zeros, "start past end did not give zeros");
    std::printf("    sub-range worst error %.2e; clamping and empty ranges ok\n", worst);
}

void caseSmall() {
    const unsigned frames = 50;
    const auto wav = makeWav(synthSignal(frames, 2), 2, 1000, true);
    for (bool average : {false, true}) {
        // Guard floats around the output detect writes outside it (ASan
        // catches them too, the guards make it visible without it).
        const unsigned long n = 500;
        std::vector<float> buf(n + 16, 9.f);
        float *out = buf.data() + 8;
        CHECK(readMem(wav, 0, -1, n, average, out) == readSamplesNoError, "small read");
        bool guards = true;
        for (int i = 0; i < 8; i++) guards = guards && buf[i] == 9.f && buf[8 + n + i] == 9.f;
        CHECK(guards, "average=%d: wrote outside the output buffer", average);
        bool tailZero = true;
        for (unsigned long i = frames; i < n; i++) tailZero = tailZero && out[i] == 0.f;
        CHECK(tailZero, "average=%d: samples past the file's frames are not zero", average);
        float worst = 0;
        for (unsigned i = 0; i < frames; i++) {
            const float expect = average
                ? std::sqrt(0.5f * (synth(i, 0, frames) * synth(i, 0, frames) +
                                    synth(i, 1, frames) * synth(i, 1, frames)))
                : synth(i, 0, frames);
            worst = std::max(worst, std::fabs(out[i] - expect));
        }
        CHECK(worst < 1e-6f, "average=%d: per-frame values off by %g", average, worst);
    }
    std::printf("    500 samples from 50 frames: one per frame, zeros after, no overrun\n");
}

void caseErrors() {
    const auto wav = makeWav(synthSignal(100, 1), 1, 1000, true);
    float out[16];
    CHECK(readMem(wav, 0, -1, 16, false, nullptr) == failedToReadPcmFrames, "NULL output");
    CHECK(Waveform::readSamples(nullptr, nullptr, 0, 0, -1, 16, false, out) == failedToReadPcmFrames,
          "NULL path and buffer");
    CHECK(Waveform::readSamples(nullptr, wav.data(), 0, 0, -1, 16, false, out) == failedToReadPcmFrames,
          "zero dataSize");
    CHECK(readMem(wav, 0, -1, 0, false, out) == readSamplesNoError, "zero samples");
    std::vector<unsigned char> garbage(4096);
    for (size_t i = 0; i < garbage.size(); i++) garbage[i] = (unsigned char)(i * 37 + 11);
    CHECK(readMem(garbage, 0, -1, 16, false, out) == noBackend, "garbage data");
    // Truncated WAV: header only.
    std::vector<unsigned char> header(wav.begin(), wav.begin() + 44);
    const ReadSamplesErrors th = readMem(header, 0, -1, 16, true, out);
    std::printf("    header-only WAV -> %d\n", th);
    CHECK(th == readSamplesNoError || th == noBackend, "header-only WAV: %d", th);
    // "OggS" magic with nothing behind it.
    std::vector<unsigned char> fakeOgg = {'O', 'g', 'g', 'S', 0, 0, 0, 0, 1, 2, 3};
    CHECK(readMem(fakeOgg, 0, -1, 16, false, out) == noBackend, "fake OGG");
    CHECK(Waveform::readSamples("/nonexistent/dir/missing.wav", nullptr, 0, 0, -1, 16, false, out) ==
              noBackend,
          "missing file");
}

void caseFile() {
    const auto wav = makeWav(synthSignal(22050, 2), 2, 22050, false);
    const std::string path = tempPath("flutter_soloud_waveform_test.wav");
    if (!writeFile(path, wav)) {
        std::printf("    SKIPPED: cannot write %s\n", path.c_str());
        return;
    }
    for (bool average : {false, true}) {
        std::vector<float> a(300), b(300);
        CHECK(Waveform::readSamples(path.c_str(), nullptr, 0, 0.1f, 0.9f, 300, average, a.data()) ==
                  readSamplesNoError,
              "file read");
        CHECK(readMem(wav, 0.1f, 0.9f, 300, average, b.data()) == readSamplesNoError, "mem read");
        CHECK(a == b, "average=%d: file and memory results differ", average);
    }
    std::remove(path.c_str());
    std::printf("    file and memory reads identical\n");
}

void caseAssets() {
    const struct { const char *file; bool isOgg; } assets[] = {
        {"example/assets/audio/sample-WAV.wav", false},
        {"example/assets/audio/tic-1.wav", false},
        {"example/assets/audio/sample-MP3.mp3", false},
        {"example/assets/audio/sample-FLAC.flac", false},
        {"example/assets/audio/sample-vorbis.ogg", true},
    };
    for (auto &a : assets) {
        std::vector<unsigned char> data;
        if (!readFile(a.file, data)) {
            std::printf("    SKIPPED: %s not found (run from the repo root)\n", a.file);
            continue;
        }
        const unsigned long n = 256;
        std::vector<float> fileOut(n), memOut(n), points(n);
        const ReadSamplesErrors ef =
            Waveform::readSamples(a.file, nullptr, 0, 0, -1, n, true, fileOut.data());
        const ReadSamplesErrors em = readMem(data, 0, -1, n, true, memOut.data());
#if defined(NO_XIPH_LIBS)
        if (a.isOgg) {
            std::printf("    %-40s built without Xiph: file %d, memory %d (noBackend = %d)\n",
                        a.file, ef, em, noBackend);
            CHECK(ef == noBackend && em == noBackend, "%s without Xiph: %d/%d", a.file, ef, em);
            continue;
        }
#endif
        CHECK(ef == readSamplesNoError && em == readSamplesNoError, "%s: errors %d/%d", a.file, ef,
              em);
        const ReadSamplesErrors ep = readMem(data, 0, -1, n, false, points.data());
        CHECK(ep == readSamplesNoError, "%s: point sampling error %d", a.file, ep);
        double maxRms = 0;
        unsigned nonZero = 0;
        bool finite = true, inRange = true;
        for (unsigned long i = 0; i < n; i++) {
            finite = finite && std::isfinite(fileOut[i]) && std::isfinite(points[i]);
            inRange = inRange && fileOut[i] >= 0 && fileOut[i] <= 1.5f && std::fabs(points[i]) <= 1.5f;
            maxRms = std::max(maxRms, (double)fileOut[i]);
            if (fileOut[i] > 1e-4f) nonZero++;
        }
        std::printf("    %-40s %lu bars: max RMS %.3f, %u non-silent bars\n", a.file, n, maxRms,
                    nonZero);
        CHECK(finite, "%s: NaN/Inf", a.file);
        CHECK(inRange, "%s: values out of range", a.file);
        CHECK(maxRms > 0.01, "%s: silent waveform", a.file);
        float diff = 0;
        for (unsigned long i = 0; i < n; i++) diff = std::max(diff, std::fabs(fileOut[i] - memOut[i]));
        CHECK(diff < 1e-6f, "%s: file and memory differ by %g", a.file, diff);
    }
}

#if !defined(NO_XIPH_LIBS)
void caseVorbis() {
    const char *path = "example/assets/audio/sample-vorbis.ogg";
    std::vector<unsigned char> data;
    if (!readFile(path, data)) {
        std::printf("    SKIPPED: %s not found (run from the repo root)\n", path);
        return;
    }
    ma_libvorbis fromFile, fromMem;
    const ma_result rf = ma_libvorbis_init_file(path, nullptr, nullptr, &fromFile);
    CHECK(rf == MA_SUCCESS, "ma_libvorbis_init_file: %d", rf);
    if (rf != MA_SUCCESS) return;
    ma_format fmt;
    ma_uint32 ch = 0, sr = 0;
    ma_uint64 len = 0;
    ma_libvorbis_get_data_format(&fromFile, &fmt, &ch, &sr, nullptr, 0);
    ma_libvorbis_get_length_in_pcm_frames(&fromFile, &len);
    std::printf("    init_file: %u ch, %u Hz, %llu frames\n", ch, sr, (unsigned long long)len);
    CHECK(ch >= 1 && ch <= 8 && sr >= 8000 && len > 0, "file format %u/%u/%llu", ch, sr,
          (unsigned long long)len);

    // Regression for S9: ma_memory_stream_read returned size_t (bytes read)
    // but was stored as an ma_read_proc returning ma_result, so every
    // successful read was taken as an error and init_memory always failed.
    const ma_result rm = ma_libvorbis_init_memory(data.data(), data.size(), nullptr, nullptr, &fromMem);
    std::printf("    init_memory: result %d\n", rm);
    CHECK(rm == MA_SUCCESS, "ma_libvorbis_init_memory: %d", rm);
    if (rm != MA_SUCCESS) {
        ma_libvorbis_uninit(&fromFile, nullptr);
        return;
    }
    // Same decoded frames from both.
    std::vector<float> a(4096 * ch), b(4096 * ch);
    ma_uint64 na = 0, nb = 0;
    ma_libvorbis_read_pcm_frames(&fromFile, a.data(), 4096, &na);
    ma_libvorbis_read_pcm_frames(&fromMem, b.data(), 4096, &nb);
    CHECK(na == nb && a == b, "file and memory decode differ");
    ma_libvorbis_uninit(&fromMem, nullptr);
    ma_libvorbis_uninit(&fromFile, nullptr);
}
#endif

} // namespace

int main(int argc, char **argv) {
    const struct { const char *name; void (*fn)(); } cases[] = {
        {"point", casePoint}, {"average", caseAverage}, {"range", caseRange},
        {"small", caseSmall}, {"errors", caseErrors}, {"file", caseFile},
        {"assets", caseAssets},
#if !defined(NO_XIPH_LIBS)
        {"vorbis", caseVorbis},
#endif
    };
#if defined(NO_XIPH_LIBS)
    std::printf("(built with NO_XIPH_LIBS: OGG is expected to report noBackend)\n");
#endif
    const char *only = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0) continue;
        ran++;
        const int before = gFailures;
        std::printf("[%s]\n", c.name);
        c.fn();
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
