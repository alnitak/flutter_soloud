// Standalone test for mixer output encoders.
//
// Build & run from the flutter_soloud repo root:
//
//   c++ -std=c++17 -O2 -Wall \
//       -I src \
//       -I macos/flutter_soloud/include \
//       -DNO_XIPH_LIBS=0 \
//       -o /tmp/encoder_output_test \
//       test/encoder_output_test.cpp \
//       src/mixeroutput/mixer_output_encoder.cpp \
//       src/mixeroutput/opus_output_encoder.cpp \
//       src/mixeroutput/vorbis_output_encoder.cpp \
//       src/mixeroutput/flac_output_encoder.cpp \
//       macos/flutter_soloud/libs/libopus.a \
//       macos/flutter_soloud/libs/libogg.a \
//       macos/flutter_soloud/libs/libvorbis.a \
//       macos/flutter_soloud/libs/libvorbisenc.a \
//       macos/flutter_soloud/libs/libFLAC.a \
//   && /tmp/encoder_output_test

#include "../src/mixeroutput/opus_output_encoder.h"
#include "../src/mixeroutput/vorbis_output_encoder.h"
#include "../src/mixeroutput/flac_output_encoder.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

std::vector<float> makeSine(int sampleRate, int channels, float durationSeconds,
                            float frequency) {
  const size_t frames = static_cast<size_t>(sampleRate * durationSeconds);
  std::vector<float> out;
  out.reserve(frames * channels);
  for (size_t i = 0; i < frames; ++i) {
    const float sample =
        std::sin(2.0f * static_cast<float>(M_PI) * frequency *
                 static_cast<float>(i) / static_cast<float>(sampleRate));
    for (int ch = 0; ch < channels; ++ch) {
      out.push_back(sample * 0.5f);
    }
  }
  return out;
}

bool writeFile(const char *path, const std::vector<uint8_t> &data) {
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "Failed to open %s for writing\n", path);
    return false;
  }
  f.write(reinterpret_cast<const char *>(data.data()),
          static_cast<std::streamsize>(data.size()));
  return f.good();
}

void dumpHeader(const char *label, const std::vector<uint8_t> &data) {
  std::printf("%s: %zu bytes, first 16 bytes:", label, data.size());
  for (size_t i = 0; i < std::min<size_t>(16, data.size()); ++i) {
    std::printf(" %02x", data[i]);
  }
  std::printf("\n");
}

bool testEncoder(MixerOutputEncoder *encoder, const char *label,
                 const char *path, int sampleRate, int channels,
                 const std::vector<float> &samples) {
  if (!encoder->initialize(sampleRate, channels)) {
    std::fprintf(stderr, "%s: initialize failed\n", label);
    return false;
  }

  std::vector<uint8_t> output;
  std::vector<uint8_t> finalOutput;

  // Feed the whole buffer at once, as the mixer output does.
  if (!encoder->encode(samples.data(), samples.size(), output)) {
    std::fprintf(stderr, "%s: encode failed\n", label);
    return false;
  }

  if (!encoder->finalize(finalOutput)) {
    std::fprintf(stderr, "%s: finalize failed\n", label);
    return false;
  }

  output.insert(output.end(), finalOutput.begin(), finalOutput.end());

  dumpHeader(label, output);

  if (!writeFile(path, output)) {
    return false;
  }

  std::printf("  wrote %s\n", path);
  return true;
}

}  // namespace

int main() {
  const int sampleRate = 48000;
  const int channels = 2;
  const float duration = 1.0f;
  const float frequency = 440.0f;

  const auto samples = makeSine(sampleRate, channels, duration, frequency);

  OpusOutputEncoder opus;
  VorbisOutputEncoder vorbis;
  FlacOutputEncoder flac;

  bool ok = true;
  ok = testEncoder(&opus, "Opus", "/tmp/encoder_test.opus", sampleRate,
                   channels, samples) &&
       ok;
  ok = testEncoder(&vorbis, "Vorbis", "/tmp/encoder_test.ogg", sampleRate,
                   channels, samples) &&
       ok;
  ok = testEncoder(&flac, "FLAC", "/tmp/encoder_test.flac", sampleRate,
                   channels, samples) &&
       ok;

  return ok ? 0 : 1;
}
