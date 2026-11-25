// Fuzz tests for the mixer-output encoders (src/mixeroutput): WAV, FLAC,
// Ogg Vorbis and Ogg Opus, fed random blocks that contain NaN, +-Inf, values
// far outside [-1, 1], subnormals and exact +-1, with random block lengths
// (0 included), at the channel and rate limits, and finalized with no data.
// Each stream is decoded again with the reference library (libFLAC,
// libvorbisfile, libopus + libogg; WAV parsed by hand) and must be valid,
// have exactly the samples that went in, and hold only finite, bounded
// audio:
//
//   WAV, FLAC  lossless 16-bit: every sample must equal the encoder's
//              documented conversion: NaN -> 0, then clamp to [-1, 1] and
//              truncate x * 32767 toward zero (so -Inf -> -32767);
//   Vorbis     the decoded length equals the input length; samples finite
//              and |x| <= 4 (lossy, but the input was clamped to +-1 or
//              should have been);
//   Opus       the decoded length (end granule - pre-skip) equals the input
//              length at 48 kHz; samples finite and |x| <= 4.
//
//   fuzz          random sessions per encoder (fixed seeds);
//   specials      whole blocks of NaN, +Inf, -Inf, 1e30, subnormals;
//   nan_in_tone   a sine with sparse NaN samples (Vorbis, Opus);
//   zero_blocks   encode() with 0 samples / null input is refused and
//                 changes nothing;
//   limits        1 and 8 channels (Opus 1 and 2) at 8000 and 192000 Hz;
//   empty         finalize() with no data gives a valid, empty stream.
//
// No audio device and no assets are used. Needs libogg, libvorbis, libopus
// and libflac; the script prints SKIPPED without them.
//
//   ./test/common/output_encoders_fuzz_test.sh [--asan] [--strict] [case]

#include "mixeroutput/flac_output_encoder.h"
#include "mixeroutput/opus_output_encoder.h"
#include "mixeroutput/vorbis_output_encoder.h"
#include "mixeroutput/wav_output_encoder.h"

#include <FLAC/stream_decoder.h>
#include <ogg/ogg.h>
#include <opus/opus.h>
#include <vorbis/vorbisfile.h>

#include "../support/c_tests_checks.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

enum Kind { WAV, FLAC, VORBIS, OPUS };
const char *kName[] = {"WAV", "FLAC", "Vorbis", "Opus"};

std::unique_ptr<MixerOutputEncoder> make(Kind k) {
  switch (k) {
    case WAV: return std::make_unique<WavOutputEncoder>();
    case FLAC: return std::make_unique<FlacOutputEncoder>();
    case VORBIS: return std::make_unique<VorbisOutputEncoder>();
    default: return std::make_unique<OpusOutputEncoder>();
  }
}

/// The 16-bit conversion WAV and FLAC document (simd_pcm.h).
int expected16(float x) {
  if (std::isnan(x)) return 0;
  x = x < -1.f ? -1.f : (x > 1.f ? 1.f : x);
  return (int)(x * 32767.0f);
}

// ---------------------------------------------------------------- decoders

struct Decoded {
  bool ok = false;
  std::string error;
  int channels = 0;
  long rate = 0;
  size_t frames = 0;
  std::vector<int> ints;      // WAV/FLAC interleaved, input channel order
  std::vector<float> floats;  // Vorbis/Opus, interleaved (any order)
};

Decoded decodeWav(WavOutputEncoder &enc, const std::vector<uint8_t> &data) {
  Decoded d;
  std::vector<uint8_t> h;
  enc.writeCurrentHeader(h);
  auto le32 = [&](int o) {
    return (uint32_t)h[o] | (uint32_t)h[o + 1] << 8 | (uint32_t)h[o + 2] << 16 |
           (uint32_t)h[o + 3] << 24;
  };
  if (h.size() != 44 || std::memcmp(h.data(), "RIFF", 4) ||
      std::memcmp(h.data() + 36, "data", 4)) {
    d.error = "bad header";
    return d;
  }
  d.channels = h[22] | h[23] << 8;
  d.rate = le32(24);
  if (le32(40) != data.size()) {
    d.error = "data size " + std::to_string(le32(40)) + " != " +
              std::to_string(data.size());
    return d;
  }
  for (size_t i = 0; i + 1 < data.size(); i += 2)
    d.ints.push_back((int16_t)(data[i] | data[i + 1] << 8));
  d.frames = d.channels ? d.ints.size() / d.channels : 0;
  d.ok = true;
  return d;
}

struct FlacCtx {
  const std::vector<uint8_t> *in;
  size_t pos = 0;
  Decoded *out;
  bool error = false;
};

Decoded decodeFlac(const std::vector<uint8_t> &bytes) {
  Decoded d;
  FlacCtx ctx{&bytes, 0, &d};
  FLAC__StreamDecoder *dec = FLAC__stream_decoder_new();
  auto read = [](const FLAC__StreamDecoder *, FLAC__byte buf[], size_t *n,
                 void *c) {
    auto *x = static_cast<FlacCtx *>(c);
    const size_t left = x->in->size() - x->pos;
    if (left == 0) {
      *n = 0;
      return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    }
    *n = std::min(*n, left);
    std::memcpy(buf, x->in->data() + x->pos, *n);
    x->pos += *n;
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
  };
  auto write = [](const FLAC__StreamDecoder *, const FLAC__Frame *f,
                  const FLAC__int32 *const buf[], void *c) {
    auto *x = static_cast<FlacCtx *>(c);
    const unsigned ch = f->header.channels;
    x->out->channels = (int)ch;
    x->out->rate = f->header.sample_rate;
    for (unsigned i = 0; i < f->header.blocksize; i++)
      for (unsigned k = 0; k < ch; k++) {
        // Back to SoLoud's 7.1 order (the encoder swaps side and back).
        unsigned src = k;
        if (ch == 8 && k >= 4) src = k ^ 2;
        x->out->ints.push_back(buf[src][i]);
      }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
  };
  auto meta = [](const FLAC__StreamDecoder *, const FLAC__StreamMetadata *m,
                 void *c) {
    auto *x = static_cast<FlacCtx *>(c);
    if (m->type == FLAC__METADATA_TYPE_STREAMINFO) {
      x->out->channels = (int)m->data.stream_info.channels;
      x->out->rate = m->data.stream_info.sample_rate;
    }
  };
  auto err = [](const FLAC__StreamDecoder *, FLAC__StreamDecoderErrorStatus,
                void *c) { static_cast<FlacCtx *>(c)->error = true; };
  if (FLAC__stream_decoder_init_stream(dec, read, nullptr, nullptr, nullptr,
                                       nullptr, write, meta, err, &ctx) !=
      FLAC__STREAM_DECODER_INIT_STATUS_OK) {
    d.error = "init";
    FLAC__stream_decoder_delete(dec);
    return d;
  }
  const bool done = FLAC__stream_decoder_process_until_end_of_stream(dec);
  const auto state = FLAC__stream_decoder_get_state(dec);
  FLAC__stream_decoder_finish(dec);
  FLAC__stream_decoder_delete(dec);
  d.frames = d.channels ? d.ints.size() / d.channels : 0;
  d.ok = done && !ctx.error && state == FLAC__STREAM_DECODER_END_OF_STREAM;
  if (!d.ok) d.error = "decode error (state " + std::to_string(state) + ")";
  return d;
}

struct MemFile {
  const std::vector<uint8_t> *data;
  size_t pos = 0;
};

Decoded decodeVorbis(const std::vector<uint8_t> &bytes) {
  Decoded d;
  MemFile mf{&bytes, 0};
  ov_callbacks cb;
  cb.read_func = [](void *p, size_t s, size_t n, void *src) -> size_t {
    auto *m = static_cast<MemFile *>(src);
    const size_t want = s * n, left = m->data->size() - m->pos;
    const size_t k = std::min(want, left);
    std::memcpy(p, m->data->data() + m->pos, k);
    m->pos += k;
    return s ? k / s : 0;
  };
  cb.seek_func = [](void *src, ogg_int64_t off, int whence) -> int {
    auto *m = static_cast<MemFile *>(src);
    long long base = whence == SEEK_SET ? 0
                     : whence == SEEK_CUR ? (long long)m->pos
                                          : (long long)m->data->size();
    long long p = base + off;
    if (p < 0 || p > (long long)m->data->size()) return -1;
    m->pos = (size_t)p;
    return 0;
  };
  cb.close_func = nullptr;
  cb.tell_func = [](void *src) -> long {
    return (long)static_cast<MemFile *>(src)->pos;
  };
  OggVorbis_File vf;
  if (ov_open_callbacks(&mf, &vf, nullptr, 0, cb) != 0) {
    d.error = "ov_open failed";
    return d;
  }
  vorbis_info *vi = ov_info(&vf, -1);
  d.channels = vi->channels;
  d.rate = vi->rate;
  for (;;) {
    float **pcm = nullptr;
    int section = 0;
    long n = ov_read_float(&vf, &pcm, 4096, &section);
    if (n == 0) break;
    if (n < 0) {
      d.error = "ov_read_float " + std::to_string(n);
      ov_clear(&vf);
      return d;
    }
    for (long i = 0; i < n; i++)
      for (int c = 0; c < d.channels; c++) d.floats.push_back(pcm[c][i]);
    d.frames += (size_t)n;
  }
  ov_clear(&vf);
  d.ok = true;
  return d;
}

Decoded decodeOpus(const std::vector<uint8_t> &bytes) {
  Decoded d;
  ogg_sync_state sync;
  ogg_sync_init(&sync);
  char *buf = ogg_sync_buffer(&sync, (long)bytes.size());
  if (!bytes.empty()) std::memcpy(buf, bytes.data(), bytes.size());
  ogg_sync_wrote(&sync, (long)bytes.size());
  ogg_stream_state os;
  bool init = false, eos = false;
  int packetNo = 0, preSkip = 0;
  int64_t lastGranule = -1;
  OpusDecoder *dec = nullptr;
  std::vector<float> all;
  std::vector<float> tmp(5760 * 2);
  ogg_page page;
  while (ogg_sync_pageout(&sync, &page) == 1) {
    if (!init) {
      ogg_stream_init(&os, ogg_page_serialno(&page));
      init = true;
    }
    ogg_stream_pagein(&os, &page);
    if (ogg_page_eos(&page)) eos = true;
    if (ogg_page_granulepos(&page) >= 0) lastGranule = ogg_page_granulepos(&page);
    ogg_packet p;
    while (ogg_stream_packetout(&os, &p) == 1) {
      if (packetNo == 0) {
        if (p.bytes < 19 || std::memcmp(p.packet, "OpusHead", 8)) {
          d.error = "no OpusHead";
          goto done;
        }
        d.channels = p.packet[9];
        preSkip = p.packet[10] | p.packet[11] << 8;
        d.rate = 48000;
        int e = 0;
        dec = opus_decoder_create(48000, d.channels, &e);
        if (e != OPUS_OK) {
          d.error = "decoder create";
          goto done;
        }
      } else if (packetNo >= 2) {
        int n = opus_decode_float(dec, p.packet, (opus_int32)p.bytes,
                                  tmp.data(), 5760, 0);
        if (n < 0) {
          d.error = "opus_decode_float " + std::to_string(n);
          goto done;
        }
        all.insert(all.end(), tmp.begin(), tmp.begin() + (size_t)n * d.channels);
      }
      packetNo++;
    }
  }
  if (!eos || packetNo < 2 || lastGranule < 0) {
    d.error = "incomplete stream";
    goto done;
  }
  {
    // Playable samples: decoded samples through the end granule, minus
    // pre-skip (RFC 7845 section 4).
    const int64_t total = std::min<int64_t>(lastGranule, (int64_t)all.size() / d.channels);
    d.frames = total > preSkip ? (size_t)(total - preSkip) : 0;
    d.floats.assign(all.begin() + (size_t)std::min<int64_t>(preSkip, total) * d.channels,
                    all.begin() + (size_t)total * d.channels);
    d.ok = true;
  }
done:
  if (dec) opus_decoder_destroy(dec);
  if (init) ogg_stream_clear(&os);
  ogg_sync_clear(&sync);
  return d;
}

// ----------------------------------------------------------------- driver

struct Session {
  Kind kind;
  int rate, channels;
  std::vector<std::vector<float>> blocks;  // each a whole number of frames
};

/// The Opus encoder's linear resampler emits a 48 kHz frame only once the
/// input frame after it has arrived, so the stream ends up to one input
/// frame early: up to ceil(48000 / rate) output frames (plus rounding).
size_t opusFramesTolerance(int rate) {
  return rate == 48000 ? 0 : (size_t)std::ceil(48000.0 / rate) + 1;
}

void runSession(const Session &s, const char *label, float maxPeak = 4.0f) {
  auto enc = make(s.kind);
  if (!enc->initialize(s.rate, s.channels)) {
    CHECK(false, "%s %s: initialize(%d, %d) failed", kName[s.kind], label,
          s.rate, s.channels);
    return;
  }
  std::vector<uint8_t> stream, out;
  std::vector<float> input;
  bool encOk = true;
  for (const auto &b : s.blocks) {
    out.assign(3, 0xEE);
    const bool r = enc->encode(b.empty() ? nullptr : b.data(), b.size(), out);
    if (b.empty()) {
      CHECK(!r, "%s %s: 0-sample block accepted", kName[s.kind], label);
      continue;
    }
    if (!r) encOk = false;
    stream.insert(stream.end(), out.begin(), out.end());
    input.insert(input.end(), b.begin(), b.end());
  }
  CHECK(encOk, "%s %s: an encode() call failed", kName[s.kind], label);
  out.clear();
  CHECK(enc->finalize(out), "%s %s: finalize failed", kName[s.kind], label);
  stream.insert(stream.end(), out.begin(), out.end());
  if (s.kind != WAV) {  // WAV streams on after finalize (header patching)
    std::vector<float> one(s.channels, 0.f);
    CHECK(!enc->encode(one.data(), one.size(), out),
          "%s %s: encode after finalize accepted", kName[s.kind], label);
  }

  const size_t inFrames = input.size() / s.channels;
  Decoded d;
  switch (s.kind) {
    case WAV: d = decodeWav(*static_cast<WavOutputEncoder *>(enc.get()), stream); break;
    case FLAC: d = decodeFlac(stream); break;
    case VORBIS: d = decodeVorbis(stream); break;
    case OPUS: d = decodeOpus(stream); break;
  }
  CHECK(d.ok, "%s %s (%d Hz, %d ch, %zu frames): stream does not decode: %s",
        kName[s.kind], label, s.rate, s.channels, inFrames, d.error.c_str());
  if (!d.ok) return;
  CHECK(d.channels == s.channels, "%s %s: %d channels decoded", kName[s.kind],
        label, d.channels);
  if (s.kind == WAV || s.kind == FLAC) {
    CHECK(d.rate == s.rate, "%s %s: rate %ld", kName[s.kind], label, d.rate);
    CHECK(d.frames == inFrames, "%s %s: %zu frames decoded, %zu in",
          kName[s.kind], label, d.frames, inFrames);
    size_t bad = 0, first = 0;
    for (size_t i = 0; i < std::min(d.ints.size(), input.size()); i++)
      if (d.ints[i] != expected16(input[i])) {
        if (!bad) first = i;
        bad++;
      }
    CHECK(bad == 0, "%s %s: %zu samples differ from the 16-bit conversion "
          "(first at %zu: input %g -> %d, expected %d)",
          kName[s.kind], label, bad, first, bad ? input[first] : 0.f,
          bad ? d.ints[first] : 0, bad ? expected16(input[first]) : 0);
    return;
  }
  // Lossy formats.
  size_t expectFrames = inFrames;
  size_t tolFrames = 0;
  if (s.kind == OPUS && s.rate != 48000) {
    expectFrames = (size_t)std::llround((double)inFrames * 48000.0 / s.rate);
    tolFrames = opusFramesTolerance(s.rate);
  }
  const long diff = (long)d.frames - (long)expectFrames;
  CHECK(std::labs(diff) <= (long)tolFrames,
        "%s %s (%d Hz, %d ch): %zu frames decoded, %zu expected",
        kName[s.kind], label, s.rate, s.channels, d.frames, expectFrames);
  size_t nonFinite = 0;
  float worst = 0;
  for (float v : d.floats) {
    if (!std::isfinite(v)) nonFinite++;
    else worst = std::max(worst, std::fabs(v));
  }
  CHECK(nonFinite == 0 && worst <= maxPeak,
        "%s %s (%d Hz, %d ch): decoded audio has %zu non-finite samples, "
        "peak %g",
        kName[s.kind], label, s.rate, s.channels, nonFinite, worst);
}

/// Random sample. The encoders' contract is input in [-1, 1]
/// (mixer_output_encoder.h); SoLoud's output clipping guarantees it except
/// for NaN, which passes through a clamp. WAV and FLAC clamp anyway, so they
/// also get values outside the contract; Vorbis and Opus ([inContract])
/// get NaN, subnormals and exact +-1 only.
float specialValue(std::mt19937 &rng, bool inContract = false) {
  std::uniform_real_distribution<float> u(0.f, 1.f), s(-1.f, 1.f);
  float r = u(rng);
  if (inContract && r >= 0.70f && r < 0.82f) r = 0.5f;  // in-range instead
  if (inContract && r >= 0.86f && r < 0.92f) r = 0.84f;  // NaN instead
  if (r < 0.70f) return s(rng);
  if (r < 0.78f) return s(rng) * 1e30f;
  if (r < 0.82f) return s(rng) * 3.f;
  if (r < 0.86f) return std::numeric_limits<float>::quiet_NaN();
  if (r < 0.89f) return std::numeric_limits<float>::infinity();
  if (r < 0.92f) return -std::numeric_limits<float>::infinity();
  if (r < 0.95f) return s(rng) * 1e-40f;  // subnormal
  if (r < 0.975f) return 1.f;
  return -1.f;
}

int maxChannels(Kind k) { return k == OPUS ? 2 : 8; }

void fuzz() {
  for (Kind k : {WAV, FLAC, VORBIS, OPUS}) {
    std::mt19937 rng(1000 + k);
    const int rates[] = {8000, 22050, 44100, 48000, 96000};
    for (int session = 0; session < 16; session++) {
      Session s;
      s.kind = k;
      s.rate = rates[rng() % 5];
      s.channels = 1 + (int)(rng() % maxChannels(k));
      const int blocks = 1 + (int)(rng() % 25);
      for (int b = 0; b < blocks; b++) {
        const int frames = (rng() % 6 == 0) ? 0 : (int)(rng() % 3000);
        std::vector<float> v((size_t)frames * s.channels);
        for (auto &x : v) x = specialValue(rng, k == VORBIS || k == OPUS);
        s.blocks.push_back(std::move(v));
      }
      runSession(s, ("fuzz#" + std::to_string(session)).c_str());
    }
  }
}

void specials() {
  const float vals[] = {std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity(), 1e30f,
                        -1e30f, 1e-42f, 1.0f, -1.0f};
  const char *names[] = {"NaN", "+Inf", "-Inf", "1e30", "-1e30", "subnormal",
                         "+1", "-1"};
  for (Kind k : {WAV, FLAC, VORBIS, OPUS}) {
    for (int i = 0; i < 8; i++) {
      // Out-of-contract values only for the encoders that clamp.
      const bool outOfContract = !std::isnan(vals[i]) && std::fabs(vals[i]) > 1.f;
      if (outOfContract && (k == VORBIS || k == OPUS)) continue;
      Session s{k, 48000, 2, {}};
      // A tone, a block of the special value, then the tone again: the
      // stream must survive and keep encoding afterwards.
      std::vector<float> toneBlock(4800 * 2);
      for (size_t j = 0; j < toneBlock.size(); j++)
        toneBlock[j] = 0.5f * (float)std::sin(j * 0.03);
      s.blocks.push_back(toneBlock);
      s.blocks.push_back(std::vector<float>(4800 * 2, vals[i]));
      s.blocks.push_back(toneBlock);
      runSession(s, names[i]);
    }
  }
}

void nanInTone() {
  // A 0.5 sine with a NaN every 400 samples, as a mix that went NaN for a
  // moment would reach the encoder on ARM (SoLoud's NEON and scalar clip
  // let NaN through). Decoded, it must stay a sine of about 0.5.
  for (Kind k : {VORBIS, OPUS})
    for (int rate : {8000, 44100, 48000}) {
      std::vector<float> v((size_t)rate * 2);
      for (size_t i = 0; i < v.size(); i++)
        v[i] = (i % 400 < 3) ? std::numeric_limits<float>::quiet_NaN()
                             : 0.5f * (float)std::sin(i * 0.05);
      Session s{k, rate, 1, {v}};
      // A 0.5 sine (NaN as 0) never decodes anywhere near 1.0.
      runSession(s, "nan-in-tone", 1.0f);
    }
}

void zeroBlocks() {
  for (Kind k : {WAV, FLAC, VORBIS, OPUS}) {
    // Only empty blocks: same as finalizing with no data.
    Session s{k, 44100, 2, {{}, {}, {}}};
    runSession(s, "only-zero-blocks");
    // Interleaved with data.
    Session t{k, 44100, 1, {{}, std::vector<float>(1000, 0.25f), {},
                            std::vector<float>(1, -0.5f), {}}};
    runSession(t, "zero-blocks-between");
  }
}

void limits() {
  for (Kind k : {WAV, FLAC, VORBIS, OPUS}) {
    for (int ch : {1, maxChannels(k)}) {
      for (int rate : {8000, 192000}) {
        std::mt19937 rng(rate + ch);
        Session s{k, rate, ch, {}};
        for (int b = 0; b < 6; b++) {
          std::vector<float> v((size_t)(1 + rng() % 4000) * ch);
          for (auto &x : v) x = specialValue(rng, k == VORBIS || k == OPUS);
          s.blocks.push_back(std::move(v));
        }
        runSession(s, "limits");
      }
    }
  }
}

void empty() {
  for (Kind k : {WAV, FLAC, VORBIS, OPUS})
    for (int ch : {1, 2}) {
      Session s{k, 48000, ch, {}};
      runSession(s, "empty");
    }
}

}  // namespace

int main(int argc, char **argv) {
  ctest::parseArgs(argc, argv);
  if (ctest::selected("empty")) empty();
  if (ctest::selected("zero_blocks")) zeroBlocks();
  if (ctest::selected("specials")) specials();
  if (ctest::selected("nan_in_tone")) nanInTone();
  if (ctest::selected("limits")) limits();
  if (ctest::selected("fuzz")) fuzz();
  return ctest::summary("output_encoders_fuzz_test");
}
