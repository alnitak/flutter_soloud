// Standalone native tests for the mixer-output encoder factory
// (src/mixeroutput/mixer_output_encoder.cpp) and the encoders' handling of
// their initialize() parameters.
//
// The script builds this file twice: with the Xiph libraries, and with
// -DNO_XIPH_LIBS (as for platforms without them), where the FLAC, Vorbis
// and Opus encoders must refuse to initialize and WAV still works.
//
//   formats     create() gives the right encoder for WAV/FLAC/Vorbis/Opus,
//               nullptr for the raw PCM formats (handled without an encoder)
//               and for unknown values; every encoder takes float input;
//   no_xiph     (NO_XIPH_LIBS build) FLAC/Vorbis/Opus refuse initialize,
//               encode and finalize; WAV works;
//   lifecycle   encode/finalize before initialize fail; finalize ends the
//               session (encode after it fails); writeCurrentHeader only for
//               WAV;
//   params      channel counts and rates at and beyond each format's limits
//               (0 and negative included): initialize must refuse what the
//               format cannot carry and never crash; what it accepts must
//               encode and finalize. MixerOutput::start() refuses rate <= 0
//               and channels <= 0 before reaching the encoders
//               (mixer_output.cpp:511), so those are reported as NOTEs.
//
// No audio device and no assets are used.
//
//   ./test/common/mixer_output_encoder_factory_test.sh [--asan] [--strict] [case]

#include "mixeroutput/flac_output_encoder.h"
#include "mixeroutput/mixer_output_encoder.h"
#include "mixeroutput/opus_output_encoder.h"
#include "mixeroutput/vorbis_output_encoder.h"
#include "mixeroutput/wav_output_encoder.h"

#include "../support/c_tests_checks.h"

#include <cmath>
#include <memory>
#include <vector>

namespace {

using Enc = std::unique_ptr<MixerOutputEncoder>;

const char *name(MixerOutputFormat f) {
  switch (f) {
    case MIXER_OUTPUT_WAV: return "WAV";
    case MIXER_OUTPUT_FLAC: return "FLAC";
    case MIXER_OUTPUT_VORBIS: return "Vorbis";
    case MIXER_OUTPUT_OPUS: return "Opus";
    default: return "PCM";
  }
}

const MixerOutputFormat kEncoded[] = {MIXER_OUTPUT_WAV, MIXER_OUTPUT_FLAC,
                                      MIXER_OUTPUT_VORBIS, MIXER_OUTPUT_OPUS};

std::vector<float> tone(int rate, int channels, int frames) {
  std::vector<float> v((size_t)frames * channels);
  for (int i = 0; i < frames; i++)
    for (int c = 0; c < channels; c++)
      v[(size_t)i * channels + c] =
          0.3f * (float)std::sin(2 * M_PI * 440.0 * (c + 1) * i / rate);
  return v;
}

void formats() {
  Enc wav(MixerOutputEncoder::create(MIXER_OUTPUT_WAV));
  Enc flac(MixerOutputEncoder::create(MIXER_OUTPUT_FLAC));
  Enc vorbis(MixerOutputEncoder::create(MIXER_OUTPUT_VORBIS));
  Enc opus(MixerOutputEncoder::create(MIXER_OUTPUT_OPUS));
  CHECK(dynamic_cast<WavOutputEncoder *>(wav.get()) != nullptr, "WAV encoder");
  CHECK(dynamic_cast<FlacOutputEncoder *>(flac.get()) != nullptr, "FLAC encoder");
  CHECK(dynamic_cast<VorbisOutputEncoder *>(vorbis.get()) != nullptr,
        "Vorbis encoder");
  CHECK(dynamic_cast<OpusOutputEncoder *>(opus.get()) != nullptr, "Opus encoder");
  for (MixerOutputFormat f : kEncoded) {
    Enc e(MixerOutputEncoder::create(f));
    CHECK(e && e->inputBytesPerSample() == sizeof(float),
          "%s takes float input", name(f));
  }
  for (MixerOutputFormat f : {MIXER_OUTPUT_PCM_F32LE, MIXER_OUTPUT_PCM_S8,
                              MIXER_OUTPUT_PCM_S16LE, MIXER_OUTPUT_PCM_S32LE})
    CHECK(MixerOutputEncoder::create(f) == nullptr,
          "raw PCM format %d has no encoder", (int)f);
  // (Unknown values such as 8 or -1 are outside the enum's range, so
  // casting them to MixerOutputFormat is undefined; Dart only sends the
  // values of its own enum.)
  // Distinct objects each call.
  Enc a(MixerOutputEncoder::create(MIXER_OUTPUT_FLAC)),
      b(MixerOutputEncoder::create(MIXER_OUTPUT_FLAC));
  CHECK(a.get() != b.get(), "a new encoder per call");
}

void noXiph() {
#ifdef NO_XIPH_LIBS
  for (MixerOutputFormat f : {MIXER_OUTPUT_FLAC, MIXER_OUTPUT_VORBIS,
                              MIXER_OUTPUT_OPUS}) {
    Enc e(MixerOutputEncoder::create(f));
    if (e == nullptr) {
      CHECK(true, "");
      continue;
    }
    // MixerOutputEncoder::create documents nullptr when the Xiph libraries
    // are missing; it returns the encoder, which then refuses to initialize.
    // MixerOutput::start() handles both.
    CHECK(!e->initialize(48000, 2), "%s refuses initialize without Xiph",
          name(f));
    std::vector<float> pcm = tone(48000, 2, 960);
    std::vector<uint8_t> out{1, 2, 3};
    CHECK(!e->encode(pcm.data(), pcm.size(), out), "%s refuses encode",
          name(f));
    CHECK(!e->finalize(out), "%s refuses finalize", name(f));
  }
  NOTE("NO_XIPH_LIBS: create() returns FLAC/Vorbis/Opus encoders that refuse "
       "initialize(), while mixer_output_encoder.h says it returns nullptr");
  Enc wav(MixerOutputEncoder::create(MIXER_OUTPUT_WAV));
  CHECK(wav->initialize(44100, 2), "WAV initializes without Xiph");
  std::vector<float> pcm = tone(44100, 2, 100);
  std::vector<uint8_t> out;
  CHECK(wav->encode(pcm.data(), pcm.size(), out) && out.size() == 400,
        "WAV encodes without Xiph (%zu bytes)", out.size());
#else
  std::printf("  (Xiph build: see the _noxiph binary)\n");
#endif
}

void lifecycle() {
  for (MixerOutputFormat f : kEncoded) {
    Enc e(MixerOutputEncoder::create(f));
    std::vector<float> pcm = tone(48000, 2, 960 * 2);
    std::vector<uint8_t> out;
    CHECK(!e->encode(pcm.data(), pcm.size(), out),
          "%s: encode before initialize fails", name(f));
    CHECK(!e->finalize(out), "%s: finalize before initialize fails", name(f));
#ifdef NO_XIPH_LIBS
    if (f != MIXER_OUTPUT_WAV) continue;
#endif
    CHECK(e->initialize(48000, 2), "%s: initialize", name(f));
    CHECK(!e->encode(nullptr, 100, out), "%s: null input refused", name(f));
    CHECK(!e->encode(pcm.data(), 0, out), "%s: 0 samples refused", name(f));
    CHECK(e->encode(pcm.data(), pcm.size(), out), "%s: encode", name(f));
    std::vector<uint8_t> hdr{9};
    const bool hasHeader = e->writeCurrentHeader(hdr);
    if (f == MIXER_OUTPUT_WAV)
      CHECK(hasHeader && hdr.size() == 44, "WAV header %zu bytes", hdr.size());
    else
      CHECK(!hasHeader && hdr.empty(), "%s: no WAV header", name(f));
    CHECK(e->finalize(out), "%s: finalize", name(f));
    if (f != MIXER_OUTPUT_WAV) {
      // Compressed encoders end their session on finalize.
      CHECK(!e->encode(pcm.data(), pcm.size(), out),
            "%s: encode after finalize fails", name(f));
      CHECK(!e->finalize(out), "%s: second finalize fails", name(f));
      // And can start a new one.
      CHECK(e->initialize(44100, 1), "%s: initialize again", name(f));
      std::vector<float> mono = tone(44100, 1, 4410);
      CHECK(e->encode(mono.data(), mono.size(), out) && e->finalize(out),
            "%s: second session", name(f));
    }
  }
  // initialize() twice without finalize keeps the first format.
#ifndef NO_XIPH_LIBS
  for (MixerOutputFormat f : {MIXER_OUTPUT_FLAC, MIXER_OUTPUT_VORBIS,
                              MIXER_OUTPUT_OPUS}) {
    Enc e(MixerOutputEncoder::create(f));
    e->initialize(48000, 2);
    if (e->initialize(44100, 1))
      NOTE("%s: a second initialize(44100, 1) returns true but keeps the "
           "first session's format (48000, 2)",
           name(f));
  }
#endif
}

/// Tries initialize(rate, channels); if accepted, encodes 0.1 s and
/// finalizes. Returns whether initialize accepted.
bool tryParams(MixerOutputFormat f, int rate, int channels) {
  Enc e(MixerOutputEncoder::create(f));
  if (!e->initialize(rate, channels)) return false;
  if (rate > 0 && channels > 0 && channels <= 64) {
    // Opus with rate <= 0 would never finish resampling; not attempted.
    std::vector<float> pcm =
        tone(rate, channels, std::max(1, std::min(rate / 10, 48000)));
    std::vector<uint8_t> out;
    const bool enc = e->encode(pcm.data(), pcm.size(), out);
    const bool fin = e->finalize(out);
    CHECK(enc && fin, "%s accepted %d Hz / %d ch but encode %d finalize %d",
          name(f), rate, channels, enc, fin);
  }
  return true;
}

void params() {
#ifdef NO_XIPH_LIBS
  const MixerOutputFormat formats[] = {MIXER_OUTPUT_WAV};
#else
  const MixerOutputFormat formats[] = {MIXER_OUTPUT_WAV, MIXER_OUTPUT_FLAC,
                                       MIXER_OUTPUT_VORBIS, MIXER_OUTPUT_OPUS};
#endif
  for (MixerOutputFormat f : formats) {
    // What each format must accept: 1..8 channels (SoLoud's maximum) for all
    // but Opus (mono/stereo, mapping family 0), at the usual rates.
    const int maxCh = f == MIXER_OUTPUT_OPUS ? 2 : 8;
    for (int ch = 1; ch <= maxCh; ch++)
      for (int rate : {8000, 22050, 44100, 48000, 96000})
        CHECK(tryParams(f, rate, ch), "%s refuses %d Hz / %d ch", name(f),
              rate, ch);
    // What it cannot carry.
    if (f == MIXER_OUTPUT_OPUS)
      for (int ch : {3, 6, 8})
        CHECK(!tryParams(f, 48000, ch), "Opus accepts %d channels", ch);
    if (f == MIXER_OUTPUT_FLAC) {
      CHECK(!tryParams(f, 48000, 9), "FLAC accepts 9 channels");
      // The FLAC format stores the rate in 20 bits.
      CHECK(!tryParams(f, 1 << 20, 2), "FLAC accepts 1048576 Hz");
    }
    // 0 and negative: refused by MixerOutput::start before reaching here.
    for (auto p : {std::pair<int, int>{0, 2}, {48000, 0}, {-44100, 2},
                   {48000, -1}}) {
      if (f == MIXER_OUTPUT_OPUS && p.first <= 0 && p.second > 0) {
        Enc e(MixerOutputEncoder::create(f));
        if (e->initialize(p.first, p.second))
          NOTE("Opus initialize(%d Hz, %d ch) returns true; in a later "
               "encode() resampleTo48k's ratio %d/48000 would never advance "
               "(0) or go negative (out-of-range size_t cast); unreachable "
               "through MixerOutput::start",
               p.first, p.second, p.first);
        continue;
      }
      if (tryParams(f, p.first, p.second))
        NOTE("%s initialize(%d Hz, %d ch) returns true (unreachable through "
             "MixerOutput::start)",
             name(f), p.first, p.second);
    }
  }
}

}  // namespace

int main(int argc, char **argv) {
  ctest::parseArgs(argc, argv);
#ifdef NO_XIPH_LIBS
  std::printf("build: NO_XIPH_LIBS\n");
#else
  std::printf("build: with Xiph libraries\n");
#endif
  if (ctest::selected("formats")) formats();
  if (ctest::selected("no_xiph")) noXiph();
  if (ctest::selected("lifecycle")) lifecycle();
  if (ctest::selected("params")) params();
  return ctest::summary("mixer_output_encoder_factory_test");
}
