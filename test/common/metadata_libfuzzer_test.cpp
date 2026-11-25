// libFuzzer target for StreamDecoder and the metadata parsers (see
// test/support/metadata_fuzz_inputs.h for the list), with a replay mode that
// builds without libFuzzer.
//
// The first input byte picks the target, the second the delivery piece size:
//   0  StreamDecoder: format detection and every stream decoder, the data
//      delivered in pieces as BufferStream does, then drained
//   1  every pure parser: id3TextToUtf8 (encodings 0-2 must give valid
//      UTF-8), the AAC parser's ID3/ADTS/AC-3/E-AC-3 parsers,
//      parseM4aMetadata, parseM4aGaplessMoov + resolve()
//   2  ICY: stripIcyMetadataEx with interval (byte 2) + 1; the result must
//      not depend on how the data is split (one piece vs. the given size)
//   3  StreamDecoder with ICY stripping, interval (byte 2) * 64 + 64
// Beyond not crashing (ASan/UBSan), a StreamDecoder run must give a sample
// count that is a multiple of its channel count, and end its drain within
// 200 calls (except AAC / AC-3 / E-AC-3, whose platform decoders have a known
// drain problem, see stream_decoders_fuzz_test.cpp).
//
// Built two ways by metadata_libfuzzer_test.sh:
//  - replay (any compiler, the default target of the CMake suite): runs the
//    seed corpus made from example/assets/audio (and the fixtures of
//    metadata_fuzz_inputs.h) through the target once; `--make-corpus DIR`
//    writes that corpus as files for libFuzzer;
//  - libFuzzer (-DMETADATA_LIBFUZZER, clang with -fsanitize=fuzzer): the
//    script runs it time-boxed on that corpus.
//
//   ./test/common/metadata_libfuzzer_test.sh [--strict] [--seconds N]

#include "../support/metadata_fuzz_inputs.h"
#include "../support/mp4_fixtures.h"
#include "../support/line_buffered_stdout.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace mfz;

static void fail(const char *what) {
  std::fprintf(stderr, "metadata_libfuzzer: invariant broken: %s\n", what);
  std::abort();
}

static std::vector<size_t> piecesFrom(uint8_t b) {
  // 1..64, 65..1500, or 512..16384 bytes, from one byte.
  if (b < 86) return {static_cast<size_t>(1 + b % 64)};
  if (b < 172) return {static_cast<size_t>(65 + (b - 86) * 17)};
  return {static_cast<size_t>(512 + (b - 172) * 190)};
}

static bool platformDecoder(int type) {
  return type == BUFFER_AAC || type == BUFFER_M4A || type == BUFFER_AC3 || type == BUFFER_EAC3;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 3) return 0;
  const uint8_t target = data[0] % 4;
  const std::vector<size_t> pieces = piecesFrom(data[1]);
  const Bytes body(data + 2, data + size);
  switch (target) {
  case 0:
  case 3: {
    const int icy = target == 3 && !body.empty() ? 64 + body[0] * 64 : 0;
    const StreamRun r = runStreamDecoder(body, pieces, icy);
    if (r.badOutput) fail("sample count not a multiple of the channel count");
    if (!r.drainEnded && !platformDecoder(r.type)) fail("drain never ended");
    break;
  }
  case 1: {
    std::string why;
    if (!runPureParsers(body, why)) fail(why.c_str());
    break;
  }
  default: {
    if (body.empty()) break;
    const int metaInt = 1 + body[0];
    const Bytes stream(body.begin() + 1, body.end());
    IcyStripState whole, split;
    Bytes cleanWhole, cleanSplit;
    std::vector<std::string> titlesWhole, titlesSplit;
    stripIcyMetadataEx(stream, metaInt, whole, cleanWhole,
                       [&](const std::string &t, const std::string &u) { titlesWhole.push_back(t + "|" + u); });
    for (size_t off = 0; off < stream.size();) {
      const size_t n = std::min(pieces[0], stream.size() - off);
      const Bytes piece(stream.begin() + off, stream.begin() + off + n);
      off += n;
      stripIcyMetadataEx(piece, metaInt, split, cleanSplit,
                         [&](const std::string &t, const std::string &u) { titlesSplit.push_back(t + "|" + u); });
    }
    if (cleanWhole != cleanSplit) fail("ICY audio depends on the split");
    if (titlesWhole != titlesSplit) fail("ICY titles depend on the split");
    break;
  }
  }
  return 0;
}

// ---------------------------------------------------------------- corpus
struct CorpusEntry {
  std::string name;
  Bytes data;
};

static std::vector<CorpusEntry> makeCorpus(const std::string &assets) {
  std::vector<CorpusEntry> out;
  auto add = [&](const std::string &name, uint8_t target, uint8_t piece, const Bytes &payload,
                 size_t limit = 64 * 1024) {
    Bytes b = {target, piece};
    b.insert(b.end(), payload.begin(), payload.begin() + std::min(limit, payload.size()));
    out.push_back({name, b});
  };
  static const char *files[] = {"8_bit_mentality.mp3", "test_mpeg2_24khz_mono.mp3", "sample-MP3.mp3",
                                "sample-OPUS.opus",    "sample-vorbis.ogg",         "sample-FLAC.flac",
                                "sample-WAV.wav",      "tic-1.wav",                 "aac_lc_mono_44100.m4a",
                                "aac_lc_stereo_48000.m4a", "sample-AAC.aac",        "sample-AC3.ac3",
                                "sample-EAC3.eac3"};
  for (const char *f : files) {
    const Bytes data = readFile(assets + "/" + f, 64 * 1024);
    if (data.empty()) continue;
    add(std::string("stream_") + f, 0, 200, data);
    add(std::string("pieces_") + f, 0, 30, data, 16 * 1024);
    add(std::string("parsers_") + f, 1, 0, data, 16 * 1024);
  }
  // The whole M4A files for the box parsers (moov can be at the end).
  for (const char *f : {"aac_lc_mono_44100.m4a", "aac_lc_stereo_48000.m4a"}) {
    const Bytes data = readFile(assets + "/" + f);
    if (!data.empty()) add(std::string("parsers_full_") + f, 1, 0, data, data.size());
  }
  // Fixtures with rich metadata.
  {
    Bytes mp3 = readFile(assets + "/test_mpeg2_24khz_mono.mp3", 16 * 1024);
    if (mp3.empty()) mp3 = mp3Frames(20);
    for (uint8_t major : {3, 4}) {
      const Bytes tag = richId3Tag(major);
      add("id3v2" + std::to_string(major) + "_tag", 1, 0, tag);
      add("id3v2" + std::to_string(major) + "_mp3", 0, 100, mp4fx::cat(tag, mp3));
    }
    mp4fx::AudioTrack t;
    t.frameSizes = {300, 310, 290, 305};
    t.hasEdit = true;
    using namespace mp4fx;
    const Bytes items = cat(textItem("\xa9nam", "Title"), numberPairItem("trkn", 3, 12), gnreItem(18),
                            freeformItem("iTunSMPB", " 00000000 00000840 000001CA 0000000000001000"));
    add("m4a_synthetic", 1, 0, makeMp4(t, udtaWithIlst(items)).bytes);
    size_t end = 0;
    add("flac_picture", 0, 200, flacWithPicture(end));
    add("oggflac_picture", 0, 200, flacWithPicture(end, true));
    // ICY: 16-byte interval with titles, and over MP3.
    IcyCase c;
    c.metaInt = 16;
    c.audio.assign(100, 0x42);
    auto txt = [](const char *s) { Bytes b; putText(b, s); return b; };
    c.blocks = {txt("StreamTitle='A';"), {}, txt("StreamTitle='B';StreamUrl='u';"), txt("x")};
    Bytes icy = {15};
    const Bytes s = icyStream(c);
    icy.insert(icy.end(), s.begin(), s.end());
    add("icy_16", 2, 5, icy);
    IcyCase m;
    m.metaInt = 64 + 7 * 64;
    m.audio = mp3;
    m.blocks.assign(m.audio.size() / m.metaInt + 1, txt("StreamTitle='Radio';"));
    Bytes icyMp3 = {7};
    const Bytes sm = icyStream(m);
    icyMp3.insert(icyMp3.end(), sm.begin(), sm.end());
    add("icy_mp3", 3, 200, icyMp3);
  }
  return out;
}

#if !defined(METADATA_LIBFUZZER)
// Replay: runs the seed corpus (or the files given) through the target.
int main(int argc, char **argv) {
  lineBufferedStdout();
  std::string assets = "example/assets/audio";
  std::string corpusDir;
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--make-corpus") && i + 1 < argc) corpusDir = argv[++i];
    else if (!std::strcmp(argv[i], "--strict")) continue;
    else if (!std::strcmp(argv[i], "--file") && i + 1 < argc) files.push_back(argv[++i]);
    else assets = argv[i];
  }
  if (!files.empty()) {
    for (const std::string &f : files) {
      const Bytes d = readFile(f);
      std::printf("replay %s (%zu bytes)\n", f.c_str(), d.size());
      LLVMFuzzerTestOneInput(d.data(), d.size());
    }
    std::printf("PASSED\n");
    return 0;
  }
  const std::vector<CorpusEntry> corpus = makeCorpus(assets);
  if (corpus.empty()) {
    std::printf("SKIPPED: no seeds (assets missing from %s)\n", assets.c_str());
    return 0;
  }
  if (!corpusDir.empty()) {
    for (const CorpusEntry &e : corpus) {
      const std::string path = corpusDir + "/" + e.name;
      FILE *f = std::fopen(path.c_str(), "wb");
      if (!f) {
        std::printf("cannot write %s\n", path.c_str());
        return 1;
      }
      std::fwrite(e.data.data(), 1, e.data.size(), f);
      std::fclose(f);
    }
    std::printf("wrote %zu seeds to %s\n", corpus.size(), corpusDir.c_str());
    return 0;
  }
  for (const CorpusEntry &e : corpus) LLVMFuzzerTestOneInput(e.data.data(), e.data.size());
  std::printf("replayed %zu seeds through the fuzz target\nPASSED\n", corpus.size());
  return 0;
}
#endif
