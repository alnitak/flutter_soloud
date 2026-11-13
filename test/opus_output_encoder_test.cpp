// Standalone native tests for the Ogg Opus mixer-capture encoder
// (src/mixeroutput/opus_output_encoder.cpp), against the system libopus and
// libogg.
//
//   * granule: every audio page's granule position counts the samples decoded
//     through the last packet completed on it; OpusHead's pre-skip is the
//     encoder's lookahead; the end-of-stream page's granule position is
//     pre-skip plus the captured samples, and the stream decodes at least that
//     many (RFC 7845 sections 4 and 5.1);
//   * preskip: the decoded audio, with pre-skip samples dropped, lines up with
//     the captured audio;
//   * resampler: a ramp captured at 44.1, 22.05 or 96 kHz and resampled to
//     48 kHz in device-sized blocks stays a ramp, in every channel (linear
//     interpolation of a linear signal is exact).
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_opus_output_encoder_test.sh         (plain)
//   ./test/run_opus_output_encoder_test.sh --asan  (AddressSanitizer + UBSan)
//
// A scenario name after the flag runs only that scenario. XIPH_PREFIX names
// where libopus and libogg are installed (default /usr/local).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <ogg/ogg.h>
#include <opus/opus.h>

// The resampler is private; reach its state from here only.
#define private public
#include "mixeroutput/opus_output_encoder.cpp"
#undef private

namespace {

int gFailures = 0;
int gAssertions = 0;

#define EXPECT(condition, format, ...)                                    \
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

constexpr int kChannels = 2;
constexpr double kPi = 3.14159265358979323846;

/// What a capture of [frames] stereo frames at 48 kHz encodes: a 440 Hz sine,
/// fed in device-sized blocks.
std::vector<float> sine(size_t frames)
{
    std::vector<float> pcm(frames * kChannels);
    for (size_t i = 0; i < frames; ++i)
    {
        const float s = 0.5f * static_cast<float>(
                                   std::sin(2.0 * kPi * 440.0 * i / 48000.0));
        pcm[i * kChannels] = s;
        pcm[i * kChannels + 1] = s;
    }
    return pcm;
}

/// A sweep from 200 Hz to 2 kHz over a second: unlike a sine it matches a
/// shifted copy of itself at no lag but zero.
std::vector<float> sweep(size_t frames)
{
    std::vector<float> pcm(frames * kChannels);
    for (size_t i = 0; i < frames; ++i)
    {
        const double t = i / 48000.0;
        const double phase = 200.0 * t + 900.0 * t * t;
        const float s = 0.5f * static_cast<float>(std::sin(2.0 * kPi * phase));
        pcm[i * kChannels] = s;
        pcm[i * kChannels + 1] = s;
    }
    return pcm;
}

std::vector<uint8_t> encodeCapture(const std::vector<float> &pcm)
{
    OpusOutputEncoder encoder;
    std::vector<uint8_t> stream;
    if (!encoder.initialize(48000, kChannels))
        return stream;
    const size_t block = 512 * kChannels;
    std::vector<uint8_t> out;
    for (size_t at = 0; at < pcm.size(); at += block)
    {
        out.clear();
        encoder.encode(pcm.data() + at, std::min(block, pcm.size() - at), out);
        stream.insert(stream.end(), out.begin(), out.end());
    }
    out.clear();
    encoder.finalize(out);
    stream.insert(stream.end(), out.begin(), out.end());
    return stream;
}

struct Page
{
    int64_t granulepos;
    bool eos;
    int audioPackets;
    int64_t decodedThrough; // samples of every audio packet completed so far
};

struct ParsedStream
{
    int preSkip = -1;
    std::vector<Page> pages;
    std::vector<std::vector<uint8_t>> audioPackets;
};

ParsedStream parse(const std::vector<uint8_t> &stream)
{
    ParsedStream parsed;
    ogg_sync_state sync;
    ogg_sync_init(&sync);
    char *buffer = ogg_sync_buffer(&sync, static_cast<long>(stream.size()));
    std::memcpy(buffer, stream.data(), stream.size());
    ogg_sync_wrote(&sync, static_cast<long>(stream.size()));
    ogg_stream_state os;
    bool streamInit = false;
    ogg_page page;
    int packetNo = 0;
    int64_t decoded = 0;
    while (ogg_sync_pageout(&sync, &page) == 1)
    {
        if (!streamInit)
        {
            ogg_stream_init(&os, ogg_page_serialno(&page));
            streamInit = true;
        }
        ogg_stream_pagein(&os, &page);
        ogg_packet packet;
        int audio = 0;
        while (ogg_stream_packetout(&os, &packet) == 1)
        {
            if (packetNo == 0 && packet.bytes >= 19)
                parsed.preSkip = packet.packet[10] | (packet.packet[11] << 8);
            if (packetNo >= 2)
            {
                decoded += opus_packet_get_nb_samples(packet.packet,
                                                      packet.bytes, 48000);
                parsed.audioPackets.emplace_back(packet.packet,
                                                 packet.packet + packet.bytes);
                ++audio;
            }
            ++packetNo;
        }
        parsed.pages.push_back({ogg_page_granulepos(&page),
                                ogg_page_eos(&page) != 0, audio, decoded});
    }
    if (streamInit)
        ogg_stream_clear(&os);
    ogg_sync_clear(&sync);
    return parsed;
}

int encoderLookahead()
{
    int error = 0;
    OpusEncoder *encoder =
        opus_encoder_create(48000, kChannels, OPUS_APPLICATION_AUDIO, &error);
    if (encoder == nullptr)
        return -1;
    opus_int32 lookahead = -1;
    opus_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&lookahead));
    opus_encoder_destroy(encoder);
    return lookahead;
}

void granulePositionsCountDecodedSamples()
{
    std::printf("granule: positions count decoded samples, the end trims to "
                "the capture\n");
    // 1 s and a partial 20 ms frame, then a length whose padding is shorter
    // than the pre-skip, so the end needs one more packet.
    for (const size_t frames : {static_cast<size_t>(48100),
                                static_cast<size_t>(960 * 25 - 100)})
    {
        const ParsedStream parsed = parse(encodeCapture(sine(frames)));
        const int lookahead = encoderLookahead();
        EXPECT(lookahead > 0 && parsed.preSkip == lookahead,
               "granule: OpusHead pre-skip %d must be the encoder lookahead "
               "%d",
               parsed.preSkip, lookahead);

        int audioPages = 0;
        const Page *eos = nullptr;
        for (const Page &page : parsed.pages)
        {
            if (page.audioPackets == 0)
                continue;
            ++audioPages;
            if (page.eos)
            {
                eos = &page;
                continue;
            }
            EXPECT(page.granulepos == page.decodedThrough,
                   "granule: an audio page completing %lld decoded samples "
                   "must have that granule position, got %lld (%zu frames)",
                   static_cast<long long>(page.decodedThrough),
                   static_cast<long long>(page.granulepos), frames);
        }
        EXPECT(audioPages > 1, "granule: the capture spans several pages");
        EXPECT(eos != nullptr, "granule: the last audio page ends the stream");
        if (eos == nullptr)
            continue;
        const int64_t end = parsed.preSkip + static_cast<int64_t>(frames);
        EXPECT(eos->granulepos == end,
               "granule: the end-of-stream granule position must be pre-skip "
               "+ %zu captured samples = %lld, got %lld",
               frames, static_cast<long long>(end),
               static_cast<long long>(eos->granulepos));
        EXPECT(eos->decodedThrough >= eos->granulepos,
               "granule: the stream must decode the %lld samples its end "
               "position claims, decodes %lld",
               static_cast<long long>(eos->granulepos),
               static_cast<long long>(eos->decodedThrough));
    }
}

/// Decodes the stream and finds the lag, in samples, at which the decoded
/// audio after pre-skip best matches the captured sweep: 0 when pre-skip is
/// the encoder's delay.
void preSkipAlignsTheDecodedAudio()
{
    std::printf("preskip: the decoded audio after pre-skip lines up with the "
                "capture\n");
    const size_t frames = 48000;
    const std::vector<float> pcm = sweep(frames);
    const ParsedStream parsed = parse(encodeCapture(pcm));
    int error = 0;
    OpusDecoder *decoder = opus_decoder_create(48000, kChannels, &error);
    EXPECT(decoder != nullptr, "preskip: create a decoder");
    if (decoder == nullptr)
        return;
    std::vector<float> decoded;
    std::vector<float> frame(5760 * kChannels);
    for (const auto &packet : parsed.audioPackets)
    {
        const int n = opus_decode_float(decoder, packet.data(),
                                        static_cast<opus_int32>(packet.size()),
                                        frame.data(), 5760, 0);
        if (n > 0)
            decoded.insert(decoded.end(), frame.begin(),
                           frame.begin() + n * kChannels);
    }
    opus_decoder_destroy(decoder);
    const size_t skip = parsed.preSkip < 0 ? 0 : parsed.preSkip;
    EXPECT(decoded.size() / kChannels >= skip + frames,
           "preskip: the stream decodes pre-skip + every captured sample");
    if (decoded.size() / kChannels < skip + frames)
        return;

    // Compare a stretch away from the start, where the codec has converged,
    // at lags around the pre-skip.
    int bestLag = 0;
    double bestError = 1e30;
    for (int lag = -400; lag <= 400; ++lag)
    {
        double error2 = 0;
        for (size_t i = 4800; i < 4800 + 9600; ++i)
        {
            const double d = decoded[(skip + i + lag) * kChannels] -
                             pcm[i * kChannels];
            error2 += d * d;
        }
        if (error2 < bestError)
        {
            bestError = error2;
            bestLag = lag;
        }
    }
    EXPECT(bestLag == 0,
           "preskip: the decoded audio after a pre-skip of %zu must line up "
           "with the capture, it is %d samples off",
           skip, bestLag);
}

void resamplerKeepsARampARamp()
{
    std::printf("resampler: a ramp stays a ramp across block boundaries\n");
    struct Case
    {
        int rate;
        int channels;
        size_t block;
    };
    for (const Case c : {Case{44100, 1, 512}, Case{44100, 2, 441},
                         Case{22050, 2, 256}, Case{96000, 1, 480}})
    {
        OpusOutputEncoder encoder;
        EXPECT(encoder.initialize(c.rate, c.channels),
               "resampler: initialize %d Hz, %d channels", c.rate, c.channels);
        // Channel 0 counts input frames up, channel 1 counts them down.
        const double ratio = c.rate / 48000.0;
        std::vector<float> out;
        std::vector<float> in(c.block * c.channels);
        for (size_t b = 0; b < 8; ++b)
        {
            for (size_t i = 0; i < c.block; ++i)
                for (int ch = 0; ch < c.channels; ++ch)
                    in[i * c.channels + ch] =
                        (ch == 0 ? 1.0f : -1.0f) *
                        static_cast<float>(b * c.block + i);
            encoder.resampleTo48k(in.data(), in.size());
            out.insert(out.end(), encoder.m_resampledBuffer.begin(),
                       encoder.m_resampledBuffer.end());
        }
        // Output frame j is input position j * ratio.
        const size_t frames = out.size() / c.channels;
        double worst = 0;
        size_t worstAt = 0;
        for (size_t j = 0; j < frames; ++j)
            for (int ch = 0; ch < c.channels; ++ch)
            {
                const double expected = (ch == 0 ? 1.0 : -1.0) * j * ratio;
                const double error = std::fabs(out[j * c.channels + ch] -
                                               expected);
                if (error > worst)
                {
                    worst = error;
                    worstAt = j;
                }
            }
        const size_t expectedFrames =
            static_cast<size_t>((8 * c.block - 1) / ratio) + 1;
        EXPECT(frames + 1 >= expectedFrames && frames <= expectedFrames,
               "resampler: %d Hz in %zu-frame blocks gives %zu frames, "
               "expected about %zu",
               c.rate, c.block, frames, expectedFrames);
        EXPECT(worst < 1e-2,
               "resampler: %d Hz, %d channels, in %zu-frame blocks must stay "
               "a ramp, worst error %.3f input frames at output frame %zu",
               c.rate, c.channels, c.block, worst, worstAt);
    }
}

} // namespace

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : nullptr;
    const char *const kScenarios[] = {"granule", "preskip", "resampler"};
    if (only != nullptr)
    {
        bool known = false;
        for (const char *name : kScenarios)
            known = known || std::strcmp(only, name) == 0;
        if (!known)
        {
            std::fprintf(stderr, "Unknown scenario '%s'.\n", only);
            return 2;
        }
    }
    auto wanted = [&](const char *name)
    { return only == nullptr || std::strcmp(only, name) == 0; };

    if (wanted("granule"))
        granulePositionsCountDecodedSamples();
    if (wanted("preskip"))
        preSkipAlignsTheDecodedAudio();
    if (wanted("resampler"))
        resamplerKeepsARampARamp();

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}
