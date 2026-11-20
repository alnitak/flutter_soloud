// Standalone native tests for the mixer-output Ogg Vorbis encoder
// (src/mixeroutput/vorbis_output_encoder.cpp, class VorbisOutputEncoder),
// decoded back with the system libvorbisfile. Vorbis is lossy, so these
// tests check duration and rough fidelity, not exact samples.
//
//   * roundtrip: mono, stereo (SIMD deinterleave path) and 3/4 channels
//     (generic path) at 22.05/44.1/48 kHz, fed in uneven chunk sizes. The
//     decoded stream has the right channel count and rate; ov_pcm_total() and
//     the number of decoded frames equal the frames encoded (sample-exact
//     duration); each channel carries a sine at its own frequency, and the
//     decoded channel has its RMS within 10% of the input's, its energy
//     concentrated at that frequency (channel order kept, no crosstalk), and
//     a waveform correlation above 0.98 with the input (time aligned);
//   * lfe_slot (bug O1, fixed): 6- and 8-channel input in SoLoud order
//     (FL FR C LFE BL BR / FL FR C LFE SL SR BL BR) is remapped to Vorbis
//     order (FL C FR BL BR LFE / FL C FR SL SR BL BR LFE): every decoded
//     Vorbis channel carries the matching SoLoud channel, SoLoud's LFE
//     (input channel 3) lands in the Vorbis LFE slot, and a high tone in
//     SoLoud's back-right channel survives (it is no longer band-limited);
//   * silence: silent input decodes to (near) silence;
//   * empty: initialize + finalize with no audio is a valid Ogg Vorbis
//     stream with zero frames;
//   * lifecycle: encode before initialize fails; finalize twice fails; a
//     partial trailing frame is dropped; re-initialize after finalize starts a
//     new valid stream with a new Ogg serial number; a second initialize
//     while running is a no-op that keeps the first format.
//
// No audio device and no assets are needed. Needs libogg, libvorbis,
// libvorbisenc and libvorbisfile (Homebrew `libvorbis` on macOS,
// libvorbis-dev on Linux); if they are missing the script prints SKIPPED.
//
//   ./test/common/vorbis_output_encoder_test.sh [--asan] [scenario]
//
// --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. A
// scenario name runs only that scenario. XIPH_PREFIX names where the Xiph
// libraries are installed.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <vorbis/vorbisfile.h>

#include "mixeroutput/vorbis_output_encoder.h"

namespace {

int gFailures = 0;
int gAssertions = 0;

#define CHECK(condition, format, ...)                                     \
    do                                                                    \
    {                                                                     \
        ++gAssertions;                                                    \
        if (!(condition))                                                 \
        {                                                                 \
            ++gFailures;                                                  \
            std::fprintf(stderr, "  FAIL [%s:%d] " format "\n", __FILE__, \
                         __LINE__, ##__VA_ARGS__);                        \
        }                                                                 \
    } while (0)

const double kPi = 3.14159265358979323846;

// Test tone of channel c: 330, 600, 870, ... Hz unless overridden.
std::vector<double> gFreqOverride;
double channelFreq(int c)
{
    if (c < static_cast<int>(gFreqOverride.size()) && gFreqOverride[c] > 0)
        return gFreqOverride[c];
    return 330.0 + 270.0 * c;
}

std::vector<float> makeSines(size_t frames, int channels, int rate, float amp)
{
    std::vector<float> v(frames * channels);
    for (size_t f = 0; f < frames; ++f)
        for (int c = 0; c < channels; ++c)
            v[f * channels + c] =
                amp * static_cast<float>(std::sin(2.0 * kPi * channelFreq(c) * f / rate));
    return v;
}

struct Decoded
{
    bool ok = false;
    int channels = 0;
    long rate = 0;
    long long pcmTotal = -1;
    std::vector<std::vector<float>> ch;  // planar
};

struct MemFile
{
    const std::vector<uint8_t> *data;
    size_t pos = 0;
};

size_t memRead(void *ptr, size_t size, size_t nmemb, void *src)
{
    auto *m = static_cast<MemFile *>(src);
    const size_t want = size * nmemb;
    const size_t left = m->data->size() - m->pos;
    const size_t n = want < left ? want : left;
    std::memcpy(ptr, m->data->data() + m->pos, n);
    m->pos += n;
    return size ? n / size : 0;
}

int memSeek(void *src, ogg_int64_t off, int whence)
{
    auto *m = static_cast<MemFile *>(src);
    long long base = whence == SEEK_SET ? 0
                     : whence == SEEK_CUR ? static_cast<long long>(m->pos)
                                          : static_cast<long long>(m->data->size());
    long long p = base + off;
    if (p < 0 || p > static_cast<long long>(m->data->size())) return -1;
    m->pos = static_cast<size_t>(p);
    return 0;
}

long memTell(void *src) { return static_cast<long>(static_cast<MemFile *>(src)->pos); }

Decoded decodeVorbis(const std::vector<uint8_t> &bytes)
{
    Decoded d;
    MemFile mf{&bytes, 0};
    ov_callbacks cb{memRead, memSeek, nullptr, memTell};
    OggVorbis_File vf;
    if (ov_open_callbacks(&mf, &vf, nullptr, 0, cb) != 0) return d;
    vorbis_info *vi = ov_info(&vf, -1);
    d.channels = vi->channels;
    d.rate = vi->rate;
    d.pcmTotal = ov_pcm_total(&vf, -1);
    d.ch.assign(d.channels, {});
    bool err = false;
    for (;;)
    {
        float **pcm = nullptr;
        int section = 0;
        long n = ov_read_float(&vf, &pcm, 4096, &section);
        if (n == 0) break;
        if (n < 0)
        {
            err = true;
            break;
        }
        for (int c = 0; c < d.channels; ++c) d.ch[c].insert(d.ch[c].end(), pcm[c], pcm[c] + n);
    }
    ov_clear(&vf);
    d.ok = !err;
    return d;
}

std::vector<uint8_t> encodeAll(VorbisOutputEncoder &enc, const std::vector<float> &sig, int ch,
                               bool *ok)
{
    std::vector<uint8_t> stream, out;
    const size_t chunkFrames[] = {1, 7, 256, 441, 512, 1024, 3000, 4096};
    const size_t frames = sig.size() / ch;
    size_t f = 0, k = 0;
    *ok = true;
    while (f < frames)
    {
        size_t n = chunkFrames[k++ % (sizeof chunkFrames / sizeof chunkFrames[0])];
        if (n > frames - f) n = frames - f;
        out.clear();
        if (!enc.encode(sig.data() + f * ch, n * ch, out)) *ok = false;
        stream.insert(stream.end(), out.begin(), out.end());
        f += n;
    }
    out.clear();
    if (!enc.finalize(out)) *ok = false;
    stream.insert(stream.end(), out.begin(), out.end());
    return stream;
}

// Power of x[begin, end) at frequency f (Goertzel), normalised by length.
double goertzel(const std::vector<float> &x, size_t begin, size_t end, double f, double rate)
{
    const double w = 2.0 * kPi * f / rate, coeff = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = begin; i < end; ++i)
    {
        const double s = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    const double n = static_cast<double>(end - begin);
    return p / (n * n);
}

// Encodes per-channel sines and checks the decoded stream. Decoded Vorbis
// channel v is compared with input channel vorbisToInput[v] (identity when
// null). The decoded channel [lfeSlot] (if >= 0) is not checked here; its
// decoded RMS is returned in *lfeRms (see testLfeSlot).
void roundtrip(int rate, int ch, size_t frames, int lfeSlot = -1, double *lfeRms = nullptr,
               const int *vorbisToInput = nullptr)
{
    char ctx[64];
    std::snprintf(ctx, sizeof ctx, "%d Hz %d ch %zu frames", rate, ch, frames);
    const float amp = 0.5f;
    std::vector<float> sig = makeSines(frames, ch, rate, amp);

    VorbisOutputEncoder enc;
    CHECK(enc.inputBytesPerSample() == sizeof(float), "%s: input bps", ctx);
    if (!enc.initialize(rate, ch))
    {
        CHECK(false, "%s: initialize failed", ctx);
        return;
    }
    bool ok;
    std::vector<uint8_t> stream = encodeAll(enc, sig, ch, &ok);
    CHECK(ok, "%s: encode/finalize returned false", ctx);
    CHECK(stream.size() > 4 && std::memcmp(stream.data(), "OggS", 4) == 0,
          "%s: no OggS capture pattern", ctx);

    Decoded d = decodeVorbis(stream);
    CHECK(d.ok, "%s: decode failed", ctx);
    CHECK(d.channels == ch, "%s: channels %d", ctx, d.channels);
    CHECK(d.rate == rate, "%s: rate %ld", ctx, d.rate);
    CHECK(d.pcmTotal == static_cast<long long>(frames), "%s: ov_pcm_total %lld want %zu",
          ctx, d.pcmTotal, frames);
    if (d.channels != ch) return;
    CHECK(d.ch[0].size() == frames, "%s: decoded %zu frames want %zu", ctx,
          d.ch[0].size(), frames);
    if (d.ch[0].size() < frames) return;

    // Analyse the middle, away from the start/end transients.
    const size_t b = frames / 4, e = frames - frames / 4;
    const double wantRms = amp / std::sqrt(2.0);
    for (int c = 0; c < ch; ++c)
    {
        if (c == lfeSlot)
        {
            double sq = 0;
            for (size_t i = b; i < e; ++i) sq += static_cast<double>(d.ch[c][i]) * d.ch[c][i];
            if (lfeRms) *lfeRms = std::sqrt(sq / (e - b));
            continue;
        }
        const int in = vorbisToInput ? vorbisToInput[c] : c;
        double sumSq = 0, dot = 0, inSq = 0;
        for (size_t i = b; i < e; ++i)
        {
            const double y = d.ch[c][i], x = sig[i * ch + in];
            sumSq += y * y;
            dot += x * y;
            inSq += x * x;
        }
        const double rms = std::sqrt(sumSq / (e - b));
        const double corr = dot / std::sqrt(inSq * sumSq + 1e-30);
        CHECK(std::fabs(rms - wantRms) < 0.1 * wantRms, "%s ch%d: RMS %.4f want %.4f", ctx,
              c, rms, wantRms);
        CHECK(corr > 0.98, "%s ch%d: correlation with input %.4f", ctx, c, corr);

        const double own = goertzel(d.ch[c], b, e, channelFreq(in), rate);
        for (int o = 0; o < ch; ++o)
        {
            if (o == in) continue;
            if (lfeSlot >= 0 && o == (vorbisToInput ? vorbisToInput[lfeSlot] : lfeSlot))
                continue;
            const double other = goertzel(d.ch[c], b, e, channelFreq(o), rate);
            CHECK(other < own * 1e-3, "%s ch%d: %.0f Hz leak %.3g vs own %.3g", ctx, c,
                  channelFreq(o), other, own);
        }
    }
}

void testRoundtrip()
{
    for (int rate : {22050, 44100, 48000})
        for (int ch : {1, 2})
            roundtrip(rate, ch, static_cast<size_t>(rate) + 777);
    roundtrip(44100, 3, 30000);
    roundtrip(44100, 4, 30000);
}

// Bug O1 (fixed): SoLoud's 5.1 order is FL FR C LFE BL BR and its 7.1 order
// is FL FR C LFE SL SR BL BR; Vorbis wants FL C FR BL BR LFE and
// FL C FR SL SR BL BR LFE. The encoder used to copy input channel i into
// Vorbis channel i, so SoLoud's back-right channel landed in the Vorbis LFE
// slot, which libvorbis band-limits (5.1): a mid/high tone there was gone.
// Now each decoded Vorbis channel must carry its SoLoud channel, the LFE
// input (channel 3) must land in the last Vorbis slot, and a high tone in
// SoLoud's back-right channel must survive.
void testLfeSlot()
{
    // Input (SoLoud) channel for each decoded Vorbis channel.
    static const int k51[6] = {0, 2, 1, 4, 5, 3};
    static const int k71[8] = {0, 2, 1, 4, 5, 6, 7, 3};
    const double inRms = 0.5 / std::sqrt(2.0);
    for (int ch : {6, 8})
    {
        const int *map = ch == 6 ? k51 : k71;
        const int vorbisLfe = ch - 1;
        const int soloudLfe = 3;
        const int soloudBackRight = ch - 1;
        for (double lfeFreq : {60.0, 90.0})
        {
            gFreqOverride.assign(ch, 0.0);
            gFreqOverride[soloudLfe] = lfeFreq;
            // A high tone in SoLoud's back-right channel: it used to be coded
            // as LFE and filtered out.
            gFreqOverride[soloudBackRight] = 2000.0;
            double rms = -1;
            // All non-LFE channels (including back-right at 2 kHz) are
            // checked for RMS, correlation and crosstalk inside roundtrip().
            roundtrip(48000, ch, 30000, vorbisLfe, &rms, map);
            std::printf("  %d ch: %.0f Hz SoLoud LFE (input 3) -> Vorbis LFE slot %d RMS %.4f "
                        "(input %.4f)\n", ch, lfeFreq, vorbisLfe, rms, inRms);
            // libvorbis codes the LFE slot coarsely (5.1 adds ~20% noise),
            // so only check that the tone is there with roughly its level.
            CHECK(rms > 0.6 * inRms && rms < 1.4 * inRms,
                  "%d ch: %.0f Hz LFE tone in Vorbis LFE slot RMS %.4f want %.4f", ch,
                  lfeFreq, rms, inRms);
        }
    }
    gFreqOverride.clear();
}

void testSilence()
{
    const size_t frames = 20000;
    std::vector<float> sig(frames * 2, 0.0f);
    VorbisOutputEncoder enc;
    enc.initialize(44100, 2);
    bool ok;
    std::vector<uint8_t> stream = encodeAll(enc, sig, 2, &ok);
    CHECK(ok, "encode");
    Decoded d = decodeVorbis(stream);
    CHECK(d.ok && d.channels == 2, "decode");
    if (!d.ok || d.channels != 2) return;
    CHECK(d.ch[0].size() == frames, "decoded %zu frames", d.ch[0].size());
    float peak = 0;
    for (auto &c : d.ch)
        for (float v : c) peak = std::fmax(peak, std::fabs(v));
    CHECK(peak < 1e-3f, "silence decoded with peak %g", peak);
}

void testEmpty()
{
    VorbisOutputEncoder enc;
    CHECK(enc.initialize(44100, 2), "initialize");
    std::vector<uint8_t> out;
    CHECK(enc.finalize(out), "finalize");
    CHECK(out.size() > 4 && std::memcmp(out.data(), "OggS", 4) == 0, "no Ogg headers");
    Decoded d = decodeVorbis(out);
    CHECK(d.ok, "empty stream does not open/decode");
    CHECK(d.channels == 2 && d.rate == 44100, "empty stream info %d ch %ld Hz", d.channels,
          d.rate);
    size_t n = d.ch.empty() ? 0 : d.ch[0].size();
    CHECK(n == 0, "empty stream decoded %zu frames", n);
}

uint32_t firstSerial(const std::vector<uint8_t> &s)
{
    if (s.size() < 18) return 0;
    return static_cast<uint32_t>(s[14]) | (static_cast<uint32_t>(s[15]) << 8) |
           (static_cast<uint32_t>(s[16]) << 16) | (static_cast<uint32_t>(s[17]) << 24);
}

void testLifecycle()
{
    std::vector<float> x = makeSines(2000, 2, 44100, 0.3f);
    {
        VorbisOutputEncoder enc;
        std::vector<uint8_t> out;
        CHECK(!enc.encode(x.data(), x.size(), out), "encode before initialize succeeded");
        CHECK(!enc.finalize(out), "finalize before initialize succeeded");
    }
    {
        VorbisOutputEncoder enc;
        enc.initialize(44100, 2);
        std::vector<uint8_t> out{9};
        CHECK(enc.encode(x.data(), 1, out), "sub-frame encode failed");
        CHECK(out.empty(), "sub-frame encode left %zu bytes", out.size());
        CHECK(!enc.encode(nullptr, 2, out), "encode(nullptr) succeeded");
        CHECK(!enc.encode(x.data(), 0, out), "encode of 0 samples succeeded");

        // 2001 samples stereo: the trailing half frame is dropped.
        std::vector<uint8_t> stream;
        out.clear();
        enc.encode(x.data(), 2001, out);
        stream.insert(stream.end(), out.begin(), out.end());
        out.clear();
        CHECK(enc.finalize(out), "finalize");
        stream.insert(stream.end(), out.begin(), out.end());
        Decoded d = decodeVorbis(stream);
        CHECK(d.ok && d.pcmTotal == 1000, "partial frame: %lld frames (want 1000)", d.pcmTotal);
        out.clear();
        CHECK(!enc.finalize(out), "second finalize succeeded");

        // Re-initialize after finalize: new independent stream, new serial.
        CHECK(enc.initialize(22050, 1), "re-initialize after finalize");
        std::vector<float> mono = makeSines(5000, 1, 22050, 0.3f);
        bool ok;
        std::vector<uint8_t> stream2 = encodeAll(enc, mono, 1, &ok);
        CHECK(ok, "second stream encode");
        Decoded d2 = decodeVorbis(stream2);
        CHECK(d2.ok && d2.channels == 1 && d2.rate == 22050 && d2.pcmTotal == 5000,
              "second stream: ok=%d %d ch %ld Hz %lld frames", d2.ok, d2.channels, d2.rate,
              d2.pcmTotal);
        // Serials are random; equal only with probability ~2^-31.
        CHECK(firstSerial(stream) != firstSerial(stream2), "same Ogg serial reused");
    }
    {
        // A second initialize while running is a no-op returning true.
        VorbisOutputEncoder enc;
        enc.initialize(44100, 2);
        CHECK(enc.initialize(48000, 1), "second initialize returned false");
        bool ok;
        std::vector<uint8_t> stream = encodeAll(enc, x, 2, &ok);
        Decoded d = decodeVorbis(stream);
        CHECK(ok && d.ok && d.channels == 2 && d.rate == 44100,
              "second initialize changed the format (%d ch %ld Hz)", d.channels, d.rate);
    }
    {
        // WAV-only hooks keep the base-class defaults.
        VorbisOutputEncoder vorbis;
        MixerOutputEncoder &enc = vorbis;
        enc.initialize(44100, 2);
        std::vector<uint8_t> h{1};
        CHECK(!enc.writeCurrentHeader(h) && h.empty(), "writeCurrentHeader not a no-op");
        CHECK(enc.totalPayloadBytes() == 0, "totalPayloadBytes");
        // Destroyed without finalize: must not leak (LeakSanitizer, Linux --asan).
    }
}

struct Scenario
{
    const char *name;
    void (*run)();
};

const Scenario kScenarios[] = {
    {"roundtrip", testRoundtrip},
    {"lfe_slot", testLfeSlot},
    {"silence", testSilence},
    {"empty", testEmpty},
    {"lifecycle", testLifecycle},
};

}  // namespace

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (const Scenario &s : kScenarios)
    {
        if (only != nullptr && std::strcmp(only, s.name) != 0) continue;
        const int before = gFailures;
        s.run();
        ++ran;
        std::printf("%s %s\n", gFailures == before ? "PASS" : "FAIL", s.name);
        std::fflush(stdout);
    }
    if (ran == 0)
    {
        std::fprintf(stderr, "unknown scenario: %s\n", only);
        return 2;
    }
    std::printf("%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}
