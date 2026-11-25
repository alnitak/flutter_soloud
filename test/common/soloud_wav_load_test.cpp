// Unit tests for loading a whole sound into memory with SoLoud::Wav
// (src/soloud/src/audiosource/wav/soloud_wav.cpp and dr_impl.cpp, both with
// flutter_soloud local patches): WAV, FLAC, MP3 and Ogg through loadMem(),
// load() and loadFile().
//
// Verifies:
//   wav-formats  WAV at 8, 16, 24, 32 bit and float, 8 kHz to 192 kHz, 1 to 8
//                channels (and 10, kept to the first 8): sample count, rate,
//                channels, length and every sample
//   flac         FLAC encoded at test time, the same rates and channels;
//                one with the total length left out of STREAMINFO (a stream
//                encoder without seek-back), which the format allows
//   truncated    WAV, FLAC, MP3 and Ogg cut short: the load succeeds or fails
//                cleanly, the decoded part matches the whole file's, and the
//                voice ends at the decoded data, not after the length the
//                header promised
//   no-data      a valid header with the data missing, a data chunk that
//                claims (nearly) 4 GB in a small file, 0 bytes, garbage
//   instance     a voice reads exactly the decoded frames and ends; seek
//                and rewind; a failed reload leaves an empty sound
//
// The OS decoder fallback (NativeAudioDecoder) is stubbed out here to fail,
// as on a platform without one, so the results do not depend on the host.
//
// Needs no audio device. Uses example/assets/audio for MP3 and Ogg (path
// given by the script; prints SKIPPED if missing), and the Xiph libraries
// for FLAC encoding and Ogg decoding.
//
//   ./test/common/soloud_wav_load_test.sh [--asan] [--strict]
//
// --strict makes the checks marked KNOWN BUG fail the run.

#include "soloud.h"
#include "soloud_file.h"
#include "soloud_wav.h"
#include "dr_mp3.h"
#include "native_audio_decoder.h"
#include "../support/temp_dir.h"

#include <FLAC/metadata.h>
#include <FLAC/stream_encoder.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

// The OS decoder fallback, stubbed to fail (see the header comment).
DecodedAudioData NativeAudioDecoder::decodeFile(const char *) {
  DecodedAudioData r;
  r.errorMessage = "stubbed in soloud_wav_load_test";
  return r;
}
DecodedAudioData NativeAudioDecoder::decodeMemory(const unsigned char *, size_t) {
  DecodedAudioData r;
  r.errorMessage = "stubbed in soloud_wav_load_test";
  return r;
}

using Bytes = std::vector<unsigned char>;

static int gFailures = 0;
static int gChecks = 0;
static int gKnownBugs = 0;
static bool gStrict = false;

#define CHECK(cond, ...)                                                       \
  do {                                                                         \
    ++gChecks;                                                                 \
    if (!(cond)) {                                                             \
      ++gFailures;                                                             \
      std::printf("  FAIL [%s:%d] %s: ", __FILE__, __LINE__, #cond);           \
      std::printf(__VA_ARGS__);                                                \
      std::printf("\n");                                                       \
    }                                                                          \
  } while (0)

// A check that fails because of a known bug in src/ (see where it is used).
// Reported, but fails the run only with --strict.
#define KNOWN_BUG(cond, ...)                                                   \
  do {                                                                         \
    ++gChecks;                                                                 \
    if (!(cond)) {                                                             \
      if (gStrict) ++gFailures; else ++gKnownBugs;                             \
      std::printf("  %s [%s:%d] %s: ", gStrict ? "FAIL" : "KNOWN BUG",        \
                  __FILE__, __LINE__, #cond);                                  \
      std::printf(__VA_ARGS__);                                                \
      std::printf("\n");                                                       \
    }                                                                          \
  } while (0)

static void put16(Bytes &b, uint32_t v) {
  b.push_back(v & 0xFF);
  b.push_back((v >> 8) & 0xFF);
}
static void put32(Bytes &b, uint32_t v) {
  put16(b, v & 0xFFFF);
  put16(b, v >> 16);
}

/// Sample [f] of channel [c]: distinct per channel and frame, in [-0.9, 0.9].
static double sampleValue(size_t f, unsigned int c) {
  return 0.9 * std::sin(0.01 * static_cast<double>(f) * (c + 1) + c);
}

struct WavFixture {
  Bytes bytes;
  std::vector<float> expected; // interleaved, what the decoder must give
  size_t frames = 0;
  unsigned int channels = 0, rate = 0;
};

/// A WAV of [frames] frames. [bits] 8/16/24/32 is integer PCM, -32 float.
/// [dataSizeOverride] replaces the data chunk's size field when non-zero.
static WavFixture makeWav(unsigned int rate, unsigned int channels, int bits,
                          size_t frames, uint32_t dataSizeOverride = 0,
                          bool listChunk = false) {
  WavFixture w;
  w.frames = frames;
  w.channels = channels;
  w.rate = rate;
  const unsigned int bytesPerSample = (bits < 0 ? -bits : bits) / 8;
  const uint32_t dataSize =
      static_cast<uint32_t>(frames * channels * bytesPerSample);
  Bytes data;
  for (size_t f = 0; f < frames; ++f) {
    for (unsigned int c = 0; c < channels; ++c) {
      const double v = sampleValue(f, c);
      switch (bits) {
      case 8: {
        const int x = static_cast<int>(std::lround(v * 127)) + 128;
        data.push_back(static_cast<unsigned char>(x));
        w.expected.push_back((x - 128) / 128.0f);
        break;
      }
      case 16: {
        const int16_t x = static_cast<int16_t>(std::lround(v * 32767));
        put16(data, static_cast<uint16_t>(x));
        w.expected.push_back(x / 32768.0f);
        break;
      }
      case 24: {
        const int32_t x = static_cast<int32_t>(std::lround(v * 8388607));
        data.push_back(x & 0xFF);
        data.push_back((x >> 8) & 0xFF);
        data.push_back((x >> 16) & 0xFF);
        w.expected.push_back(static_cast<float>(x / 8388608.0));
        break;
      }
      case 32: {
        const int32_t x = static_cast<int32_t>(std::lround(v * 2147483647.0));
        put32(data, static_cast<uint32_t>(x));
        w.expected.push_back(static_cast<float>(x / 2147483648.0));
        break;
      }
      case -32: {
        const float x = static_cast<float>(v);
        uint32_t u;
        std::memcpy(&u, &x, 4);
        put32(data, u);
        w.expected.push_back(x);
        break;
      }
      }
    }
  }
  Bytes &b = w.bytes;
  b.insert(b.end(), {'R', 'I', 'F', 'F'});
  put32(b, 0); // patched below
  b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  const bool extensible = channels > 2;
  put32(b, extensible ? 40 : 16);
  put16(b, extensible ? 0xFFFE : (bits < 0 ? 3 : 1));
  put16(b, channels);
  put32(b, rate);
  put32(b, rate * channels * bytesPerSample);
  put16(b, channels * bytesPerSample);
  put16(b, bytesPerSample * 8);
  if (extensible) {
    put16(b, 22);
    put16(b, bytesPerSample * 8);
    put32(b, 0); // no channel mask
    // KSDATAFORMAT_SUBTYPE_PCM or _IEEE_FLOAT
    put32(b, bits < 0 ? 3 : 1);
    const unsigned char guidTail[12] = {0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
                                        0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    b.insert(b.end(), guidTail, guidTail + 12);
  }
  if (listChunk) {
    b.insert(b.end(), {'L', 'I', 'S', 'T'});
    put32(b, 5); // odd size: a pad byte follows
    b.insert(b.end(), {'I', 'N', 'F', 'O', 'x', 0});
  }
  b.insert(b.end(), {'d', 'a', 't', 'a'});
  put32(b, dataSizeOverride ? dataSizeOverride : dataSize);
  b.insert(b.end(), data.begin(), data.end());
  const uint32_t riff = static_cast<uint32_t>(b.size() - 8);
  b[4] = riff & 0xFF;
  b[5] = (riff >> 8) & 0xFF;
  b[6] = (riff >> 16) & 0xFF;
  b[7] = (riff >> 24) & 0xFF;
  return w;
}

/// Sample [f] of channel [c] of a loaded Wav (planar storage).
static float at(const SoLoud::Wav &w, size_t f, unsigned int c) {
  return w.mData[static_cast<size_t>(c) * w.mSampleCount + f];
}

/// Compares the first [frames] frames of [w] with [expected] (interleaved,
/// [srcChannels] wide), channel by channel. Returns the max error.
static double maxError(const SoLoud::Wav &w, const std::vector<float> &expected,
                       unsigned int srcChannels, size_t frames) {
  double err = 0;
  for (size_t f = 0; f < frames; ++f)
    for (unsigned int c = 0; c < w.mChannels; ++c)
      err = std::max(err, std::fabs(static_cast<double>(at(w, f, c)) -
                                    expected[f * srcChannels + c]));
  return err;
}

/// Plays [w] through its own voice, as the mixer would: the frames read
/// until the voice ends (or [limit] frames).
static size_t playFrames(SoLoud::Wav &w, size_t limit) {
  SoLoud::AudioSourceInstance *v = w.createInstance();
  v->init(w, 0);
  v->mChannels = w.mChannels;
  std::vector<float> buf(512 * MAX_CHANNELS);
  size_t total = 0;
  while (!v->hasEnded() && total < limit) {
    const unsigned int got = v->getAudio(buf.data(), 512, 512);
    if (got == 0 && !v->hasEnded()) {
      total = limit; // stuck: reads nothing but does not end
      break;
    }
    total += got;
  }
  delete v;
  return total;
}

static void testWavFormats() {
  std::printf("WAV formats, rates and channels\n");
  const int bitsList[] = {8, 16, 24, 32, -32};
  const unsigned int rates[] = {8000, 44100, 96000, 192000};
  for (int bits : bitsList) {
    for (unsigned int rate : rates) {
      for (unsigned int ch = 1; ch <= 8; ++ch) {
        if (rate != 44100 && ch != 1 && ch != 2 && ch != 8) continue;
        const size_t frames = rate / 10 + 3; // 0.1 s, not a multiple of 1024
        const WavFixture fx = makeWav(rate, ch, bits, frames, 0, ch == 3);
        SoLoud::Wav w;
        const SoLoud::result r =
            w.loadMem(fx.bytes.data(), static_cast<unsigned int>(fx.bytes.size()),
                      true, false);
        CHECK(r == SoLoud::SO_NO_ERROR, "%d bit %u Hz %u ch: load %d", bits,
              rate, ch, (int)r);
        if (r != SoLoud::SO_NO_ERROR) continue;
        CHECK(w.mSampleCount == frames && w.mActualSampleCount == frames &&
                  w.mChannels == ch && w.mBaseSamplerate == (float)rate,
              "%d bit %u Hz %u ch: %u frames (%u actual), %u ch, %g Hz", bits,
              rate, ch, w.mSampleCount, w.mActualSampleCount, w.mChannels,
              w.mBaseSamplerate);
        CHECK(std::fabs(w.getLength() - (double)frames / rate) < 1e-6,
              "%d bit %u Hz %u ch: length %g", bits, rate, ch, w.getLength());
        const double tol = bits == 8 ? 1e-6 : 1e-6;
        const double err = maxError(w, fx.expected, ch, frames);
        CHECK(err <= tol, "%d bit %u Hz %u ch: max sample error %g", bits, rate,
              ch, err);
        CHECK(playFrames(w, frames * 4) == frames,
              "%d bit %u Hz %u ch: a voice plays every frame once", bits, rate,
              ch);
      }
    }
  }
  // More channels than SoLoud mixes: the first 8 are kept.
  const WavFixture fx = makeWav(44100, 10, 16, 3000);
  SoLoud::Wav w;
  const SoLoud::result r = w.loadMem(
      fx.bytes.data(), static_cast<unsigned int>(fx.bytes.size()), true, false);
  CHECK(r == SoLoud::SO_NO_ERROR && w.mChannels == MAX_CHANNELS &&
            w.mSampleCount == 3000,
        "10 channels: load %d, %u ch, %u frames", (int)r, w.mChannels,
        w.mSampleCount);
  if (r == SoLoud::SO_NO_ERROR)
    CHECK(maxError(w, fx.expected, 10, 3000) <= 1e-6,
          "10 channels: the first 8 channels are the source's");
}

struct FlacFixture {
  Bytes bytes;
  std::vector<float> expected; // interleaved
  size_t frames = 0;
};

static FLAC__StreamEncoderWriteStatus collectFlac(const FLAC__StreamEncoder *,
                                                  const FLAC__byte bytes[],
                                                  size_t length, unsigned,
                                                  unsigned, void *opaque) {
  auto &out = *static_cast<Bytes *>(opaque);
  out.insert(out.end(), bytes, bytes + length);
  return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

/// A 16-bit FLAC encoded in memory. [knownLength] false leaves the total
/// sample count out of STREAMINFO (0 = unknown).
static bool makeFlac(unsigned int rate, unsigned int channels, size_t frames,
                     bool knownLength, FlacFixture &fx) {
  fx.frames = frames;
  std::vector<FLAC__int32> pcm(frames * channels);
  for (size_t f = 0; f < frames; ++f)
    for (unsigned int c = 0; c < channels; ++c) {
      const int x = static_cast<int>(std::lround(sampleValue(f, c) * 32767));
      pcm[f * channels + c] = x;
      fx.expected.push_back(x / 32768.0f);
    }
  FLAC__StreamEncoder *enc = FLAC__stream_encoder_new();
  if (!enc) return false;
  FLAC__stream_encoder_set_channels(enc, channels);
  FLAC__stream_encoder_set_bits_per_sample(enc, 16);
  FLAC__stream_encoder_set_sample_rate(enc, rate);
  if (knownLength) FLAC__stream_encoder_set_total_samples_estimate(enc, frames);
  bool ok = FLAC__stream_encoder_init_stream(enc, collectFlac, nullptr, nullptr,
                                             nullptr, &fx.bytes) ==
            FLAC__STREAM_ENCODER_INIT_STATUS_OK;
  ok = ok && FLAC__stream_encoder_process_interleaved(
                 enc, pcm.data(), static_cast<unsigned>(frames));
  ok = FLAC__stream_encoder_finish(enc) && ok;
  FLAC__stream_encoder_delete(enc);
  return ok;
}

static void testFlac() {
  std::printf("FLAC rates and channels\n");
  const unsigned int rates[] = {8000, 44100, 96000, 192000};
  for (unsigned int rate : rates) {
    for (unsigned int ch = 1; ch <= 8; ++ch) {
      if (rate != 44100 && ch != 1 && ch != 2 && ch != 8) continue;
      const size_t frames = rate / 10 + 5;
      FlacFixture fx;
      if (!makeFlac(rate, ch, frames, true, fx)) {
        CHECK(false, "FLAC encoding failed (%u Hz %u ch)", rate, ch);
        continue;
      }
      SoLoud::Wav w;
      const SoLoud::result r = w.loadMem(
          fx.bytes.data(), static_cast<unsigned int>(fx.bytes.size()), true,
          false);
      CHECK(r == SoLoud::SO_NO_ERROR, "FLAC %u Hz %u ch: load %d", rate, ch,
            (int)r);
      if (r != SoLoud::SO_NO_ERROR) continue;
      CHECK(w.mSampleCount == frames && w.mChannels == ch &&
                w.mBaseSamplerate == (float)rate,
            "FLAC %u Hz %u ch: %u frames, %u ch, %g Hz", rate, ch,
            w.mSampleCount, w.mChannels, w.mBaseSamplerate);
      CHECK(maxError(w, fx.expected, ch, frames) <= 1e-6,
            "FLAC %u Hz %u ch: the samples are the source's", rate, ch);
    }
  }

  std::printf("FLAC without the total length in STREAMINFO\n");
  FlacFixture fx;
  if (makeFlac(44100, 2, 20000, false, fx)) {
    SoLoud::Wav w;
    const SoLoud::result r = w.loadMem(
        fx.bytes.data(), static_cast<unsigned int>(fx.bytes.size()), true,
        false);
    // A total sample count of 0 in STREAMINFO means "unknown" (written by
    // encoders that stream and cannot seek back): decoded to the end, as
    // loadogg() does.
    CHECK(r == SoLoud::SO_NO_ERROR && w.mSampleCount == 20000,
          "a valid FLAC of 20000 frames with total samples 0 gives result "
          "%d, %u frames", (int)r, w.mSampleCount);
    if (r == SoLoud::SO_NO_ERROR && w.mSampleCount == 20000)
      CHECK(maxError(w, fx.expected, 2, 20000) <= 1e-6,
            "unknown-length FLAC: the samples are the source's");
  } else {
    CHECK(false, "FLAC encoding failed (unknown length)");
  }
}

static Bytes readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  return Bytes((std::istreambuf_iterator<char>(in)),
               std::istreambuf_iterator<char>());
}

/// Loads [bytes] cut to [keep] bytes; returns the load result.
static SoLoud::result loadCut(SoLoud::Wav &w, const Bytes &bytes, size_t keep) {
  return w.loadMem(bytes.data(), static_cast<unsigned int>(keep), true, false);
}

static void testTruncated(const std::string &assets) {
  std::printf("truncated files\n");
  // WAV: the header promises 10000 frames, the file holds 4000.5.
  {
    const WavFixture fx = makeWav(44100, 2, 16, 10000);
    const size_t keep = fx.bytes.size() - 6000 * 4 + 2;
    SoLoud::Wav w;
    const SoLoud::result r = loadCut(w, fx.bytes, keep);
    CHECK(r == SoLoud::SO_NO_ERROR, "truncated WAV: load %d", (int)r);
    if (r == SoLoud::SO_NO_ERROR) {
      CHECK(maxError(w, fx.expected, 2, 4000) <= 1e-6,
            "truncated WAV: the 4000 frames present are the source's");
      // dr_wav bounds the frame count by the bytes present, so the voice
      // ends at the data.
      const size_t played = playFrames(w, 1000000);
      CHECK(played == 4000, "a WAV holding 4000 of the 10000 frames its "
                            "header promises plays %zu frames", played);
    }
  }
  // FLAC cut at 60%.
  {
    FlacFixture fx;
    if (makeFlac(44100, 2, 44100, true, fx)) {
      SoLoud::Wav w;
      const SoLoud::result r = loadCut(w, fx.bytes, fx.bytes.size() * 6 / 10);
      if (r == SoLoud::SO_NO_ERROR) {
        size_t good = 0; // leading frames equal to the source
        while (good < w.mSampleCount && good < fx.frames &&
               at(w, good, 0) == fx.expected[good * 2] &&
               at(w, good, 1) == fx.expected[good * 2 + 1])
          ++good;
        CHECK(good > fx.frames / 3,
              "truncated FLAC: %zu leading frames match the source", good);
        const size_t played = playFrames(w, 1000000);
        // A voice ends at the frames actually decoded, not at STREAMINFO's
        // count (as loadmp3() and loadogg()). One FLAC block (4096 frames)
        // may be lost at the cut.
        CHECK(played <= good + 4096,
              "a FLAC cut at 60%% decodes %zu frames but plays %zu of the %zu "
              "promised", good, played, fx.frames);
      } else {
        CHECK(r == SoLoud::FILE_LOAD_FAILED, "truncated FLAC: result %d",
              (int)r);
      }
    }
  }
  // MP3 cut in half: the reference is dr_mp3 decoding the same bytes.
  for (const char *name : {"sample-MP3.mp3", "test_mpeg2_24khz_mono.mp3"}) {
    const Bytes mp3 = readFile(assets + "/" + name);
    if (mp3.empty()) {
      CHECK(false, "cannot read %s", name);
      continue;
    }
    for (size_t keep : {mp3.size(), mp3.size() / 2, mp3.size() / 2 + 1}) {
      drmp3_config cfg;
      drmp3_uint64 refFrames = 0;
      float *ref = drmp3_open_memory_and_read_pcm_frames_f32(
          mp3.data(), keep, &cfg, &refFrames, nullptr);
      SoLoud::Wav w;
      const SoLoud::result r = loadCut(w, mp3, keep);
      CHECK(r == SoLoud::SO_NO_ERROR, "%s (%zu bytes): load %d", name, keep,
            (int)r);
      if (r == SoLoud::SO_NO_ERROR && ref) {
        CHECK(w.mActualSampleCount == refFrames && w.mChannels == cfg.channels,
              "%s (%zu bytes): %u frames, dr_mp3 decodes %llu", name, keep,
              w.mActualSampleCount, (unsigned long long)refFrames);
        double err = 0;
        const size_t n = std::min<size_t>(w.mActualSampleCount, refFrames);
        for (size_t f = 0; f < n; ++f)
          for (unsigned int c = 0; c < w.mChannels; ++c)
            err = std::max(err, (double)std::fabs(at(w, f, c) -
                                                  ref[f * cfg.channels + c]));
        CHECK(err == 0, "%s (%zu bytes): max difference from dr_mp3 %g", name,
              keep, err);
        CHECK(playFrames(w, 100000000) == w.mActualSampleCount,
              "%s (%zu bytes): a voice plays the decoded frames", name, keep);
      }
      drmp3_free(ref, nullptr);
    }
  }
  // Ogg (Vorbis and Opus) cut: the decoded part is a prefix of the whole.
  for (const char *name : {"sample-vorbis.ogg", "sample-OPUS.opus"}) {
    const Bytes ogg = readFile(assets + "/" + name);
    if (ogg.empty()) {
      CHECK(false, "cannot read %s", name);
      continue;
    }
    SoLoud::Wav whole;
    const SoLoud::result r = loadCut(whole, ogg, ogg.size());
    CHECK(r == SoLoud::SO_NO_ERROR && whole.mActualSampleCount > 0,
          "%s: load %d, %u frames", name, (int)r, whole.mActualSampleCount);
    if (r != SoLoud::SO_NO_ERROR) continue;
    for (size_t keep : {ogg.size() / 2, ogg.size() / 5, (size_t)200}) {
      SoLoud::Wav w;
      const SoLoud::result rc = loadCut(w, ogg, keep);
      if (rc != SoLoud::SO_NO_ERROR) {
        CHECK(rc == SoLoud::FILE_LOAD_FAILED, "%s cut to %zu: result %d", name,
              keep, (int)rc);
        continue;
      }
      CHECK(w.mActualSampleCount < whole.mActualSampleCount &&
                w.mChannels == whole.mChannels,
            "%s cut to %zu: %u frames of %u", name, keep, w.mActualSampleCount,
            whole.mActualSampleCount);
      double err = 0;
      const size_t n = w.mActualSampleCount > 8192
                           ? w.mActualSampleCount - 8192
                           : 0; // the last packets may differ
      for (size_t f = 0; f < n; ++f)
        for (unsigned int c = 0; c < w.mChannels; ++c)
          err = std::max(err, (double)std::fabs(at(w, f, c) - at(whole, f, c)));
      CHECK(err < 1e-5, "%s cut to %zu: max difference from the whole file %g",
            name, keep, err);
      const size_t played = playFrames(w, 100000000);
      CHECK(played == w.mActualSampleCount,
            "%s cut to %zu: a voice plays %zu frames, %u decoded", name, keep,
            played, w.mActualSampleCount);
    }
  }
}

#if !defined(_WIN32)
/// Runs [fn] in a child process: true when it exits normally with status 0.
template <typename Fn> static bool survives(Fn fn) {
  std::fflush(stdout);
  const pid_t pid = fork();
  if (pid == 0) {
    std::freopen("/dev/null", "w", stderr);
    fn();
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

static void testNoData() {
  std::printf("headers without data\n");
  {
    // A valid header whose data chunk is empty: nothing to play.
    const WavFixture fx = makeWav(44100, 2, 16, 1000);
    const size_t header = fx.bytes.size() - 1000 * 4;
    SoLoud::Wav w;
    const SoLoud::result r = loadCut(w, fx.bytes, header);
    CHECK(r == SoLoud::FILE_LOAD_FAILED ||
              (r == SoLoud::SO_NO_ERROR && playFrames(w, 10000) == 0),
          "a WAV header with its data missing: result %d, %u frames", (int)r,
          w.mSampleCount);
  }
  {
    const WavFixture fx = makeWav(44100, 2, 16, 0);
    SoLoud::Wav w;
    const SoLoud::result r = loadCut(w, fx.bytes, fx.bytes.size());
    CHECK(r == SoLoud::FILE_LOAD_FAILED, "a WAV with a 0-byte data chunk: %d",
          (int)r);
  }
#if !defined(_WIN32)
  // A data chunk that claims nearly 4 GB in a 4 KB file: the load must not
  // try to allocate the claimed size (16 GB of floats for 8-bit mono).
  for (uint32_t claimed : {0xFFFFFFF0u, 0x7FFFFFF0u}) {
    const WavFixture fx = makeWav(44100, 1, 8, 4000, claimed);
    const bool ok = survives([&] {
      SoLoud::Wav w;
      const SoLoud::result r = loadCut(w, fx.bytes, fx.bytes.size());
      _exit(r == SoLoud::FILE_LOAD_FAILED ||
                    (r == SoLoud::SO_NO_ERROR && w.mSampleCount <= 4000)
                ? 0
                : 3);
    });
    // dr_wav bounds the frame count by the bytes present.
    CHECK(ok, "a 4 KB WAV whose data chunk claims 0x%08X bytes crashed the "
              "load or kept the claimed length", claimed);
  }
#endif
  {
    SoLoud::Wav w;
    const unsigned char one = 0;
    CHECK(w.loadMem(&one, 0, true, false) == SoLoud::INVALID_PARAMETER,
          "0 bytes: INVALID_PARAMETER");
    Bytes garbage(5000);
    for (size_t i = 0; i < garbage.size(); ++i)
      garbage[i] = static_cast<unsigned char>((i * 37) ^ 0x5A);
    const SoLoud::result r = loadCut(w, garbage, garbage.size());
    CHECK(r == SoLoud::FILE_LOAD_FAILED, "garbage: %d", (int)r);
    Bytes riffOnly = {'R', 'I', 'F', 'F'};
    CHECK(loadCut(w, riffOnly, riffOnly.size()) == SoLoud::FILE_LOAD_FAILED,
          "a bare RIFF tag fails");
    Bytes flacOnly = {'f', 'L', 'a', 'C', 0, 0, 0};
    CHECK(loadCut(w, flacOnly, flacOnly.size()) == SoLoud::FILE_LOAD_FAILED,
          "a bare fLaC tag fails");
    Bytes oggOnly = {'O', 'g', 'g', 'S', 0};
    CHECK(loadCut(w, oggOnly, oggOnly.size()) == SoLoud::FILE_LOAD_FAILED,
          "a bare OggS tag fails");
  }
}

static void testInstanceAndReload(const std::string &dir) {
  std::printf("voices, seek, file loads and reloads\n");
  const WavFixture fx = makeWav(8000, 2, 16, 8000);
  SoLoud::Wav w;
  CHECK(w.loadMem(fx.bytes.data(), static_cast<unsigned int>(fx.bytes.size()),
                  true, false) == SoLoud::SO_NO_ERROR,
        "load");
  {
    SoLoud::AudioSourceInstance *v = w.createInstance();
    v->init(w, 0);
    v->mChannels = w.mChannels;
    std::vector<float> buf(2 * 256);
    v->seek(0.5, nullptr, 0);
    const unsigned int got = v->getAudio(buf.data(), 256, 256);
    CHECK(got == 256 && buf[0] == fx.expected[4000 * 2] &&
              buf[256] == fx.expected[4000 * 2 + 1],
          "seek(0.5) then a read starts at frame 4000");
    v->seek(10.0, nullptr, 0);
    CHECK(v->getAudio(buf.data(), 256, 256) == 0 && v->hasEnded(),
          "a seek past the end ends the voice");
    v->rewind();
    CHECK(!v->hasEnded() && v->getAudio(buf.data(), 256, 256) == 256 &&
              buf[0] == fx.expected[0],
          "a rewind plays from the start again");
    delete v;
  }
  // load() and loadFile() from disk give the same sound.
  const std::string path = dir + "/load.wav";
  {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(fx.bytes.data()),
              static_cast<std::streamsize>(fx.bytes.size()));
  }
  {
    SoLoud::Wav d;
    CHECK(d.load(path.c_str()) == SoLoud::SO_NO_ERROR && d.mSampleCount == 8000 &&
              maxError(d, fx.expected, 2, 8000) <= 1e-6,
          "load(path)");
    SoLoud::DiskFile f;
    f.open(path.c_str());
    SoLoud::Wav l;
    CHECK(l.loadFile(&f) == SoLoud::SO_NO_ERROR && l.mSampleCount == 8000,
          "loadFile(DiskFile)");
    SoLoud::Wav m;
    CHECK(m.load((dir + "/missing.wav").c_str()) == SoLoud::FILE_NOT_FOUND,
          "load of a missing file");
    const std::string empty = dir + "/empty.wav";
    { std::ofstream out(empty, std::ios::binary); }
    CHECK(m.load(empty.c_str()) == SoLoud::FILE_LOAD_FAILED,
          "load of a 0-byte file");
  }
  // A failed reload after a good one leaves an empty sound, not the old
  // length with no data.
  Bytes garbage(3000, 0x33);
  const SoLoud::result r = loadCut(w, garbage, garbage.size());
  CHECK(r == SoLoud::FILE_LOAD_FAILED, "the reload fails: %d", (int)r);
  CHECK(w.mData == nullptr && w.mSampleCount == 0 && w.getLength() == 0,
        "after a failed reload the sound is empty");
  // A voice of the failed sound reads nothing and ends at once.
  CHECK(w.mActualSampleCount == 0 && playFrames(w, 100000) == 0,
        "after a failed reload mActualSampleCount is %u and a voice never "
        "ends", w.mActualSampleCount);
}

int main(int argc, char **argv) {
  std::string assets;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
    else assets = argv[i];
  }
  if (assets.empty() ||
      !std::filesystem::exists(assets + "/sample-MP3.mp3")) {
    std::printf("SKIPPED: example/assets/audio not found (%s)\n",
                assets.c_str());
    return 0;
  }
  const std::string dir = makeTestTempDir("soloud_wav_load_test");
  testWavFormats();
  testFlac();
  testTruncated(assets);
  testNoData();
  if (!dir.empty()) testInstanceAndReload(dir);
  std::error_code ec;
  if (!dir.empty()) std::filesystem::remove_all(dir, ec);
  std::printf("\n%d checks, %d failures, %d known bugs\n", gChecks, gFailures,
              gKnownBugs);
  return gFailures ? 1 : 0;
}
