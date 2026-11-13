// Robustness tests for the metadata parsers that read lengths straight out of
// stream data: the M4A box parser (src/audiobuffer/m4a_metadata.cpp), the
// ID3v2 frame parsers of the MP3 and AAC stream decoders, the OpusTags
// parser and the FLAC header scanner. Their input is the first bytes of a
// network stream (StreamDecoder::decode, used by buffer and pull streams).
// Malformed or hostile lengths must make them stop, never read past the data
// or exhaust the stack, and metadata that arrives in pieces must decode as
// if it had arrived whole.
//
//   ./test/run_metadata_parsers_test.sh [--asan|--wasm32] [scenario]
//
// Scenarios:
//   valid          well-formed tags still parse, for every parser below
//   m4a-nested     4 MiB of nested 'moov' boxes, parsed on a thread with a
//                  1 MiB stack (the iOS main thread and Android's default)
//   m4a-wrap       a box size that wraps `offset + size` on a 32-bit size_t
//   m4a-large-wrap an extended (64-bit) box size of UINT64_MAX
//   mp3-id3-wrap   an ID3v2.3 frame size that wraps `pos + size` (MP3 stream)
//   aac-id3-wrap   the same frame in front of an AAC stream
//   id3-latin1     Latin-1 (and UTF-8) ID3 text comes out as UTF-8, from the
//                  AAC parser and the MP3 decoder
//   id3-utf16      UTF-16 ID3 text with characters outside the Basic
//                  Multilingual Plane, or unpaired surrogates, likewise
//   opus-wrap      an OpusTags vendor length that wraps `pos + length`
//   flac-split-header      a FLAC file whose 64 KiB final metadata block
//                  arrives in two deliveries decodes as when it arrives whole
//   flac-seek-split-header the same split, then a seek: the header must have
//                  been cached for the codec to restart from
//   flac-truncated-header  data that ends inside the FLAC metadata finishes
//                  instead of staying pending
//   metadata-defaults      a default-constructed AudioMetadata holds an
//                  unknown type and zeroed Ogg format fields
//
// The *-wrap scenarios other than m4a-large-wrap can only fail with a 32-bit
// size_t, as in the web build and on armeabi-v7a: run them with --wasm32. On
// a 64-bit host the sums do not wrap and they pass either way. opus-wrap and
// the flac-* scenarios need the Xiph libraries, which the --wasm32 build
// leaves out.
//
// A scenario that can crash runs in a child process, so a crash is reported
// as a failure of that scenario.

#include "audiobuffer/aac_stream_decoder.h"
#include "audiobuffer/m4a_metadata.h"
#include "audiobuffer/mp3_stream_decoder.h"
#include "audiobuffer/stream_decoder.h"

#if !defined(NO_XIPH_LIBS)
#include "audiobuffer/flac_stream_decoder.h"
#include <FLAC/stream_encoder.h>
#include <ogg/ogg.h>
#endif
#include <pthread.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#if !defined(__EMSCRIPTEN__)
#include <sys/wait.h>
#include <unistd.h>
#endif

using Bytes = std::vector<unsigned char>;

static int gAssertions = 0;
static int gFailures = 0;

#define EXPECT(cond, ...)                                       \
    do {                                                        \
        ++gAssertions;                                          \
        if (!(cond)) {                                          \
            std::printf("  FAIL [%s:%d] ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                           \
            std::printf("\n");                                  \
            ++gFailures;                                        \
        }                                                       \
    } while (0)

static void putBe32(Bytes &out, uint32_t v) {
    out.push_back((v >> 24) & 0xFF);
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >> 8) & 0xFF);
    out.push_back(v & 0xFF);
}

static void putLe32(Bytes &out, uint32_t v) {
    out.push_back(v & 0xFF);
    out.push_back((v >> 8) & 0xFF);
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >> 24) & 0xFF);
}

static void putText(Bytes &out, const char *text) {
    out.insert(out.end(), text, text + std::strlen(text));
}

static Bytes cat(Bytes a, const Bytes &b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// ------------------------------------------------------------------ M4A
static Bytes box(const char *type, const Bytes &payload) {
    Bytes out;
    putBe32(out, static_cast<uint32_t>(8 + payload.size()));
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

static Bytes ftyp() { return box("ftyp", {'M', '4', 'A', ' ', 0, 0, 0, 0}); }

static Bytes m4aWithTitle(const char *title) {
    Bytes data = {0, 0, 0, 1, 0, 0, 0, 0};
    putText(data, title);
    Bytes meta = {0, 0, 0, 0};
    meta = cat(meta, box("ilst", box("\xa9nam", box("data", data))));
    return cat(ftyp(), box("moov", box("udta", box("meta", meta))));
}

struct ParseJob {
    const Bytes *data;
    bool found;
};

static void *parseOnThread(void *arg) {
    auto *job = static_cast<ParseJob *>(arg);
    M4aMetadata meta;
    job->found = parseM4aMetadata(job->data->data(), job->data->size(), meta);
    return nullptr;
}

static void runM4aNested() {
    // ftyp, then a chain of 'moov' boxes, each the only child of the one
    // before it: one recursive call per 8 bytes of input.
    const size_t levels = (4u << 20) / 8;
    Bytes file = ftyp();
    const size_t base = file.size();
    file.resize(base + levels * 8);
    for (size_t i = 0; i < levels; ++i) {
        const uint32_t size = static_cast<uint32_t>((levels - i) * 8);
        unsigned char *p = file.data() + base + i * 8;
        p[0] = (size >> 24) & 0xFF;
        p[1] = (size >> 16) & 0xFF;
        p[2] = (size >> 8) & 0xFF;
        p[3] = size & 0xFF;
        std::memcpy(p + 4, "moov", 4);
    }

    ParseJob job{&file, true};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1u << 20);
    pthread_t thread;
    const bool started =
        pthread_create(&thread, &attr, parseOnThread, &job) == 0;
    EXPECT(started, "pthread_create failed");
    if (started)
        pthread_join(thread, nullptr);
    pthread_attr_destroy(&attr);
    EXPECT(!job.found, "a file of empty containers has no metadata");
}

static void runM4aWrap() {
    // ftyp (16 bytes), then a 'moov' that declares 2^32 - 16 bytes. With a
    // 32-bit size_t, 16 + (2^32 - 16) wraps to 0 and passes the bound check;
    // inside it a 1 GiB 'free' box moves the read offset far past the data.
    Bytes file = ftyp();
    putBe32(file, 0xFFFFFFF0u);
    putText(file, "moov");
    putBe32(file, 0x40000000u);
    putText(file, "free");
    file.resize(file.size() + 32, 0);

    M4aMetadata meta;
    EXPECT(!parseM4aMetadata(file.data(), file.size(), meta),
           "a box larger than the data must stop the parse");
}

static void runM4aLargeWrap() {
    // An extended-size box at offset 8 whose 64-bit size would wrap its end.
    const Bytes file = {0, 0, 0, 8, 'f', 'r', 'e', 'e',
                        0, 0, 0, 1, 'm', 'o', 'o', 'v',
                        255, 255, 255, 255, 255, 255, 255, 255};
    M4aMetadata meta;
    EXPECT(!parseM4aMetadata(file.data(), file.size(), meta),
           "an extended box larger than the data must stop the parse");
}

// ------------------------------------------------------------------ ID3
// An ID3v2.3 tag (plain 32-bit frame sizes) holding one TIT2 frame. The tag
// header's own size covers only what is really there.
static Bytes id3Tag(uint32_t frameSize, const Bytes &frameBody) {
    Bytes frames;
    putText(frames, "TIT2");
    putBe32(frames, frameSize);
    frames.push_back(0);
    frames.push_back(0);
    frames.insert(frames.end(), frameBody.begin(), frameBody.end());
    frames.resize(frames.size() + 16, 0); // padding

    Bytes tag = {'I', 'D', '3', 3, 0, 0};
    const uint32_t size = static_cast<uint32_t>(frames.size());
    tag.push_back((size >> 21) & 0x7F);
    tag.push_back((size >> 14) & 0x7F);
    tag.push_back((size >> 7) & 0x7F);
    tag.push_back(size & 0x7F);
    return cat(tag, frames);
}

static Bytes latin1Body(const char *text) {
    Bytes body = {0}; // ISO-8859-1
    putText(body, text);
    return body;
}

// A wrapping TIT2: 0xFFFFFFF0 bytes declared, 4 present. 20 + 0xFFFFFFF0
// wraps to 16 with a 32-bit size_t.
static Bytes wrappingId3Tag() { return id3Tag(0xFFFFFFF0u, latin1Body("abc")); }

static void runMp3Id3Wrap() {
    // The tag, then zeros: no MP3 or ADTS frames, so detection settles on
    // "MP3 with ID3" once 512 bytes follow the tag. dr_mp3 reports the tag to
    // the wrapper's metadata callback before it looks for frames.
    Bytes stream = cat(wrappingId3Tag(), Bytes(1024, 0));
    StreamDecoder decoder;
    int sampleRate = 44100, channels = 2;
    std::string title;
    decoder.decode(stream, &sampleRate, &channels,
                   [&](AudioMetadata meta) { title = meta.mp3Metadata.title; },
                   0);
    EXPECT(title.empty(), "a frame larger than the tag must not be read");
}

static void runAacId3Wrap() {
    Bytes tag = wrappingId3Tag();
    AacMetadata meta;
    size_t id3Size = 0;
    AACDecoderWrapper::parseId3Tags(tag.data(), tag.size(), meta, id3Size);
    EXPECT(meta.title.empty(), "a frame larger than the tag must not be read");
}

// Escapes non-ASCII bytes, to print a title.
static std::string escaped(const std::string &text) {
    std::string out;
    char hex[8];
    for (unsigned char c : text) {
        if (c >= 0x20 && c < 0x7F) {
            out.push_back(static_cast<char>(c));
        } else {
            std::snprintf(hex, sizeof(hex), "\\x%02x", c);
            out += hex;
        }
    }
    return out;
}

// Checks the title that a TIT2 frame with [body] yields, as UTF-8, from the
// AAC parser and from the MP3 decoder's metadata report. Four mono MPEG-1
// layer III frames after the tag let dr_mp3 initialize and report it.
static void checkId3Title(const char *what, const Bytes &body, const std::string &expected) {
    const Bytes tag = id3Tag(static_cast<uint32_t>(body.size()), body);
    AacMetadata aac;
    size_t id3Size = 0;
    EXPECT(AACDecoderWrapper::parseId3Tags(tag.data(), tag.size(), aac, id3Size),
           "%s: AAC ID3 not parsed", what);
    EXPECT(aac.title == expected, "%s: AAC title \"%s\", expected \"%s\"", what,
           escaped(aac.title).c_str(), escaped(expected).c_str());

    MP3DecoderWrapper mp3;
    mp3.detectedType = DetectedType::BUFFER_MP3_WITH_ID3;
    std::string title;
    int reports = 0;
    mp3.setTrackChangeCallback([&](AudioMetadata meta) {
        title = meta.mp3Metadata.title;
        ++reports;
    });
    EXPECT(mp3.initializeDecoder(44100, 1), "%s: MP3 initialize", what);
    Bytes frames(417 * 4, 0);
    for (size_t i = 0; i < frames.size(); i += 417) {
        frames[i] = 0xFF;
        frames[i + 1] = 0xFB;
        frames[i + 2] = 0x90; // 128 kbit/s, 44100 Hz
        frames[i + 3] = 0xC0; // mono
    }
    Bytes stream = cat(tag, frames);
    int rate = 44100, channels = 1;
    mp3.decode(stream, &rate, &channels);
    EXPECT(reports > 0, "%s: no MP3 metadata report", what);
    EXPECT(title == expected, "%s: MP3 title \"%s\", expected \"%s\"", what,
           escaped(title).c_str(), escaped(expected).c_str());
}

static void runId3Latin1() {
    std::printf("id3-latin1: Latin-1 and UTF-8 ID3 text comes out as UTF-8\n");
    checkId3Title("Latin-1", {0, 'C', 'a', 'f', 0xE9}, "Caf\xC3\xA9");
    checkId3Title("UTF-8", {3, 'C', 'a', 'f', 0xC3, 0xA9}, "Caf\xC3\xA9");
}

static Bytes utf16Body(uint8_t encoding, bool little, bool bom,
                       std::initializer_list<uint16_t> units) {
    Bytes body = {encoding};
    if (bom) {
        body.push_back(little ? 0xFF : 0xFE);
        body.push_back(little ? 0xFE : 0xFF);
    }
    for (uint16_t unit : units) {
        body.push_back(little ? unit & 0xFF : unit >> 8);
        body.push_back(little ? unit >> 8 : unit & 0xFF);
    }
    return body;
}

static void runId3Utf16() {
    std::printf("id3-utf16: UTF-16 surrogate pairs become one UTF-8 character\n");
    // "A", U+1F600 (the pair D83D DE00), "B".
    const std::string pair = "A\xF0\x9F\x98\x80" "B";
    checkId3Title("UTF-16LE, BOM", utf16Body(1, true, true, {0x41, 0xD83D, 0xDE00, 0x42}), pair);
    checkId3Title("UTF-16BE, BOM", utf16Body(1, false, true, {0x41, 0xD83D, 0xDE00, 0x42}), pair);
    checkId3Title("UTF-16, no BOM", utf16Body(1, true, false, {0x41, 0xD83D, 0xDE00, 0x42}), pair);
    checkId3Title("UTF-16BE", utf16Body(2, false, false, {0x41, 0xD83D, 0xDE00, 0x42}), pair);
    checkId3Title("UTF-16BE with a BOM", utf16Body(2, false, true, {0x41, 0xD83D, 0xDE00, 0x42}), pair);
    // An unpaired surrogate becomes U+FFFD.
    const std::string replaced = "A\xEF\xBF\xBD" "B";
    checkId3Title("lone high surrogate", utf16Body(1, true, true, {0x41, 0xD83D, 0x42}), replaced);
    checkId3Title("lone low surrogate", utf16Body(2, false, false, {0x41, 0xDE00, 0x42}), replaced);
    checkId3Title("high surrogate at the end", utf16Body(1, true, true, {0x41, 0xD83D}),
                  "A\xEF\xBF\xBD");
    // Text below U+D800 keeps its one-unit encoding.
    checkId3Title("BMP text", utf16Body(1, true, true, {0x43, 0x61, 0x66, 0xE9, 0x20AC}),
                  "Caf\xC3\xA9\xE2\x82\xAC");
}

#if !defined(NO_XIPH_LIBS)
// ----------------------------------------------------------------- Opus
static Bytes oggPages(const std::vector<Bytes> &packets) {
    ogg_stream_state os;
    ogg_stream_init(&os, 0x50f1);
    Bytes out;
    for (size_t i = 0; i < packets.size(); ++i) {
        ogg_packet op{};
        op.packet = const_cast<unsigned char *>(packets[i].data());
        op.bytes = static_cast<long>(packets[i].size());
        op.b_o_s = i == 0;
        op.granulepos = 0;
        op.packetno = static_cast<ogg_int64_t>(i);
        ogg_stream_packetin(&os, &op);
        ogg_page og;
        while (ogg_stream_flush(&os, &og)) {
            out.insert(out.end(), og.header, og.header + og.header_len);
            out.insert(out.end(), og.body, og.body + og.body_len);
        }
    }
    ogg_stream_clear(&os);
    return out;
}

static Bytes opusHead() {
    Bytes head;
    putText(head, "OpusHead");
    head.push_back(1);              // version
    head.push_back(2);              // channels
    head.push_back(0x38);           // pre-skip 312, LE
    head.push_back(0x01);
    putLe32(head, 48000);           // input sample rate
    head.push_back(0);              // output gain
    head.push_back(0);
    head.push_back(0);              // channel mapping family
    return head;
}

static Bytes opusTags(uint32_t vendorLength, const char *vendor,
                      const char *comment) {
    Bytes tags;
    putText(tags, "OpusTags");
    putLe32(tags, vendorLength);
    putText(tags, vendor);
    putLe32(tags, comment ? 1 : 0);
    if (comment) {
        putLe32(tags, static_cast<uint32_t>(std::strlen(comment)));
        putText(tags, comment);
    }
    return tags;
}

static void decodeOpus(const Bytes &tags, std::vector<AudioMetadata> &reported) {
    Bytes stream = oggPages({opusHead(), tags});
    StreamDecoder decoder;
    int sampleRate = 48000, channels = 2;
    decoder.decode(stream, &sampleRate, &channels,
                   [&](AudioMetadata meta) { reported.push_back(meta); }, 0);
}

static void runOpusWrap() {
    // 12 + 0xFFFFFFF8 wraps to 4 with a 32-bit size_t.
    std::vector<AudioMetadata> reported;
    decodeOpus(opusTags(0xFFFFFFF8u, "x", nullptr), reported);
    for (const auto &meta : reported)
        EXPECT(meta.oggMetadata.vendor.size() <= 1,
               "a vendor string longer than its packet must not be read");
}

// ----------------------------------------------------------------- FLAC
// The FLAC fixture: STREAMINFO, then one final 64 KiB PADDING block (as a
// large cover picture would be), then the frames of 512 mono samples. Its
// metadata ends at 42 + 4 + 65536 = 65582. The scenarios split it after 44
// bytes, inside the padding block's 4-byte header, and after 62 bytes, 16
// bytes into its payload.
static constexpr size_t kFlacHeaderEnd = 65582;
static constexpr size_t kFlacSplits[] = {44, 62};

static FLAC__StreamEncoderWriteStatus collectFlac(
    const FLAC__StreamEncoder *, const FLAC__byte bytes[], size_t length,
    unsigned, unsigned, void *opaque) {
    auto &out = *static_cast<Bytes *>(opaque);
    out.insert(out.end(), bytes, bytes + length);
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

static Bytes flacWithPadding() {
    Bytes encoded;
    FLAC__StreamEncoder *encoder = FLAC__stream_encoder_new();
    EXPECT(encoder != nullptr, "FLAC encoder allocation");
    if (encoder == nullptr) return {};
    FLAC__stream_encoder_set_channels(encoder, 1);
    FLAC__stream_encoder_set_bits_per_sample(encoder, 16);
    FLAC__stream_encoder_set_sample_rate(encoder, 44100);
    FLAC__stream_encoder_set_total_samples_estimate(encoder, 512);
    const FLAC__StreamEncoderInitStatus status = FLAC__stream_encoder_init_stream(
        encoder, collectFlac, nullptr, nullptr, nullptr, &encoded);
    EXPECT(status == FLAC__STREAM_ENCODER_INIT_STATUS_OK, "FLAC encoder init");
    FLAC__int32 samples[512];
    for (unsigned i = 0; i < 512; ++i)
        samples[i] = static_cast<FLAC__int32>(i * 113 % 32768) - 16384;
    EXPECT(FLAC__stream_encoder_process_interleaved(encoder, samples, 512),
           "FLAC encoder samples");
    EXPECT(FLAC__stream_encoder_finish(encoder), "FLAC encoder finish");
    FLAC__stream_encoder_delete(encoder);

    // Find where the encoder's own metadata ends and its frames begin.
    size_t frames = 4;
    for (;;) {
        if (frames + 4 > encoded.size()) return {};
        const bool last = (encoded[frames] & 0x80) != 0;
        const size_t length = (static_cast<size_t>(encoded[frames + 1]) << 16) |
                              (static_cast<size_t>(encoded[frames + 2]) << 8) |
                              encoded[frames + 3];
        frames += 4 + length;
        if (frames > encoded.size()) return {};
        if (last) break;
    }
    // Keep "fLaC" and STREAMINFO (42 bytes), clear its last-block flag, and
    // replace the rest of the metadata with the final padding block.
    Bytes file(encoded.begin(), encoded.begin() + 42);
    file[4] &= 0x7f;
    file.insert(file.end(), {0x81, 0x01, 0x00, 0x00});
    file.resize(file.size() + 65536, 0);
    file.insert(file.end(), encoded.begin() + frames, encoded.end());
    return file;
}

// Ends the data and decodes what the decoder still holds.
static void drainFlac(FlacDecoderWrapper &decoder, std::vector<float> &output) {
    decoder.setDataEnded();
    int rate = 44100, channels = 1;
    for (int i = 0; i < 8 && decoder.hasPendingData(); ++i) {
        Bytes none;
        auto result = decoder.decode(none, &rate, &channels);
        output.insert(output.end(), result.first.begin(), result.first.end());
    }
}

// Delivers [file] whole (split 0) or as its first [split] bytes and then the
// rest, and returns the samples. StreamDecoder initializes the codec before
// the first delivery reaches it; [initialized] does the same here.
static std::vector<float> deliverFlac(FlacDecoderWrapper &decoder, const Bytes &file,
                                      size_t split, bool initialized) {
    if (initialized) EXPECT(decoder.initializeDecoder(44100, 1), "FLAC initialize");
    int rate = 44100, channels = 1;
    std::vector<float> output;
    Bytes chunk(file.begin(), file.begin() + (split ? split : file.size()));
    auto result = decoder.decode(chunk, &rate, &channels);
    EXPECT(result.second == DecoderError::NoError, "FLAC first delivery status");
    output.insert(output.end(), result.first.begin(), result.first.end());
    if (split) {
        EXPECT(output.empty(), "a partial FLAC header produced samples");
        chunk.assign(file.begin() + split, file.end());
        result = decoder.decode(chunk, &rate, &channels);
        EXPECT(result.second == DecoderError::NoError, "FLAC second delivery status");
        output.insert(output.end(), result.first.begin(), result.first.end());
    }
    return output;
}

static std::vector<float> decodeFlac(const Bytes &file, size_t split, bool initialized) {
    FlacDecoderWrapper decoder;
    std::vector<float> output = deliverFlac(decoder, file, split, initialized);
    drainFlac(decoder, output);
    return output;
}

static void runFlacSplitHeader() {
    const Bytes file = flacWithPadding();
    EXPECT(file.size() > kFlacHeaderEnd, "FLAC fixture has no frames");
    if (file.size() <= kFlacHeaderEnd) return;
    const std::vector<float> reference = decodeFlac(file, 0, true);
    EXPECT(reference.size() == 512, "unsplit FLAC: %zu samples", reference.size());
    for (size_t split : kFlacSplits) {
        for (bool initialized : {false, true}) {
            const std::vector<float> output = decodeFlac(file, split, initialized);
            EXPECT(output == reference,
                   "FLAC split at %zu (initialized=%d): %zu samples, unsplit %zu",
                   split, initialized, output.size(), reference.size());
        }
    }
}

// A pull-stream seek restarts the codec from the cached header, followed by
// the frame bytes from the seek offset.
static void runFlacSeekSplitHeader() {
    const Bytes file = flacWithPadding();
    EXPECT(file.size() > kFlacHeaderEnd, "FLAC fixture has no frames");
    if (file.size() <= kFlacHeaderEnd) return;
    const std::vector<float> reference = decodeFlac(file, 0, true);
    for (size_t split : kFlacSplits) {
        FlacDecoderWrapper decoder;
        deliverFlac(decoder, file, split, true);
        decoder.prepareForSeek(0, kFlacHeaderEnd);
        Bytes chunk(file.begin() + kFlacHeaderEnd, file.end());
        int rate = 44100, channels = 1;
        auto result = decoder.decode(chunk, &rate, &channels);
        EXPECT(result.second == DecoderError::NoError, "FLAC status after the seek");
        std::vector<float> output = result.first;
        drainFlac(decoder, output);
        EXPECT(output == reference,
               "FLAC split at %zu, after a seek: %zu samples, expected %zu",
               split, output.size(), reference.size());
    }
}

// A truncated file, or a stream that stops inside a large picture block, ends
// inside its metadata. The decoder must then stop reporting pending data: the
// native buffer stream's decode loop polls until hasPendingData() is false.
static void runFlacTruncatedHeader() {
    const Bytes file = flacWithPadding();
    EXPECT(file.size() > kFlacHeaderEnd, "FLAC fixture has no frames");
    if (file.size() <= kFlacHeaderEnd) return;
    for (size_t split : kFlacSplits) {
        for (bool initialized : {false, true}) {
            FlacDecoderWrapper decoder;
            if (initialized) EXPECT(decoder.initializeDecoder(44100, 1), "FLAC initialize");
            int rate = 44100, channels = 1;
            Bytes chunk(file.begin(), file.begin() + split);
            auto result = decoder.decode(chunk, &rate, &channels);
            EXPECT(result.first.empty(), "a partial FLAC header produced samples");
            std::vector<float> output;
            drainFlac(decoder, output);
            EXPECT(output.empty(), "a truncated FLAC header produced samples");
            EXPECT(!decoder.hasPendingData(),
                   "FLAC data that ended at %zu, inside the metadata, is still "
                   "pending after 8 decodes (initialized=%d)", split, initialized);
        }
    }
}
#endif // !defined(NO_XIPH_LIBS)

// ---------------------------------------------------------------- valid
static void testValid() {
    std::printf("valid: well-formed tags still parse\n");
    {
        Bytes file = m4aWithTitle("Box Title");
        M4aMetadata meta;
        EXPECT(parseM4aMetadata(file.data(), file.size(), meta), "M4A: no metadata");
        EXPECT(meta.title == "Box Title", "M4A title '%s'", meta.title.c_str());
    }
    {
        Bytes body = latin1Body("Frame Title");
        Bytes tag = id3Tag(static_cast<uint32_t>(body.size()), body);
        AacMetadata meta;
        size_t id3Size = 0;
        EXPECT(AACDecoderWrapper::parseId3Tags(tag.data(), tag.size(), meta, id3Size),
               "AAC ID3: not parsed");
        EXPECT(meta.title == "Frame Title", "AAC ID3 title '%s'", meta.title.c_str());
        EXPECT(id3Size == tag.size(), "AAC ID3 size %zu, expected %zu", id3Size, tag.size());
    }
#if !defined(NO_XIPH_LIBS)
    {
        std::vector<AudioMetadata> reported;
        decodeOpus(opusTags(6, "vendor", "TITLE=Opus Title"), reported);
        EXPECT(reported.size() == 1, "Opus: %zu metadata reports", reported.size());
        if (!reported.empty()) {
            const auto &ogg = reported[0].oggMetadata;
            EXPECT(ogg.vendor == "vendor", "Opus vendor '%s'", ogg.vendor.c_str());
            auto it = ogg.comments.find("TITLE");
            EXPECT(it != ogg.comments.end() && it->second == "Opus Title",
                   "Opus TITLE comment missing or wrong");
        }
    }
#endif
}

// A stream turns each AudioMetadata it reports into the struct Dart reads
// (convertMetadataToFFI() in audiobuffer.cpp and pull_buffer_stream.cpp): it
// maps the type with a switch and copies the fields of every format. A
// default-constructed AudioMetadata, which a decoder fills only in part and
// the streams' callback test hook passes as it is, must hold an unknown type
// and zeros. It is built over bytes of 0xa5, as a stack frame left by earlier
// calls might hold.
static void runMetadataDefaults() {
    std::printf("metadata-defaults: a default-constructed AudioMetadata\n");
    alignas(AudioMetadata) unsigned char storage[sizeof(AudioMetadata)];
    std::memset(storage, 0xa5, sizeof(storage));
    AudioMetadata *meta = new (storage) AudioMetadata;
    const DetectedType type = meta->type;
    EXPECT(type == BUFFER_UNKNOWN, "type %u instead of BUFFER_UNKNOWN",
           static_cast<unsigned int>(type));
    const OggMetadata &ogg = meta->oggMetadata;
    EXPECT(ogg.vorbisInfo.version == 0 && ogg.vorbisInfo.channels == 0 &&
               ogg.vorbisInfo.rate == 0 && ogg.vorbisInfo.bitrate_upper == 0 &&
               ogg.vorbisInfo.bitrate_nominal == 0 &&
               ogg.vorbisInfo.bitrate_lower == 0 &&
               ogg.vorbisInfo.bitrate_window == 0,
           "the Vorbis fields should be zero (rate %ld)", ogg.vorbisInfo.rate);
    EXPECT(ogg.opusInfo.version == 0 && ogg.opusInfo.channels == 0 &&
               ogg.opusInfo.pre_skip == 0 &&
               ogg.opusInfo.input_sample_rate == 0 &&
               ogg.opusInfo.output_gain == 0 && ogg.opusInfo.mapping_family == 0,
           "the Opus fields should be zero (input rate %u)",
           static_cast<unsigned int>(ogg.opusInfo.input_sample_rate));
    EXPECT(ogg.flacInfo.min_blocksize == 0 && ogg.flacInfo.max_blocksize == 0 &&
               ogg.flacInfo.min_framesize == 0 &&
               ogg.flacInfo.max_framesize == 0 &&
               ogg.flacInfo.sample_rate == 0 && ogg.flacInfo.channels == 0 &&
               ogg.flacInfo.bits_per_sample == 0 &&
               ogg.flacInfo.total_samples == 0,
           "the FLAC fields should be zero (sample rate %u)",
           static_cast<unsigned int>(ogg.flacInfo.sample_rate));
    meta->~AudioMetadata();
}

// Runs a scenario that can crash the process in a child, so a stack overflow
// or wild read is reported as a failure instead of ending the run. Under
// Emscripten (no fork) it runs in-process: a trap or abort ends the run.
static void isolated(const char *name, const char *what, void (*fn)()) {
    std::printf("%s: %s\n", name, what);
#if defined(__EMSCRIPTEN__)
    fn();
#else
    std::fflush(stdout);
    int fds[2];
    const bool piped = pipe(fds) == 0;
    const pid_t pid = fork();
    if (pid == 0) {
        gAssertions = 0;
        gFailures = 0;
        fn();
        std::fflush(stdout);
        // Hand the child's counts back to the parent.
        if (piped) {
            const int counts[2] = {gAssertions, gFailures};
            (void)!write(fds[1], counts, sizeof(counts));
        }
        _exit(gFailures == 0 ? 0 : 1);
    }
    int counts[2] = {0, 0};
    if (piped) {
        close(fds[1]);
        if (read(fds[0], counts, sizeof(counts)) != sizeof(counts))
            counts[0] = counts[1] = 0;
        close(fds[0]);
    }
    gAssertions += counts[0];
    gFailures += counts[1];
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status)) {
        EXPECT(false, "%s: the parser crashed (signal %d)", name, WTERMSIG(status));
    } else if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0 && counts[1] == 0)) {
        EXPECT(false, "%s: exit status %d", name, WEXITSTATUS(status));
    }
#endif
}

int main(int argc, char **argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    const auto want = [&](const char *name) { return only.empty() || only == name; };

    if (want("valid")) testValid();
    if (want("m4a-nested"))
        isolated("m4a-nested", "4 MiB of nested boxes on a 1 MiB stack", runM4aNested);
    if (want("m4a-wrap"))
        isolated("m4a-wrap", "a box size that wraps a 32-bit offset", runM4aWrap);
    if (want("m4a-large-wrap"))
        isolated("m4a-large-wrap", "an extended box whose end wraps", runM4aLargeWrap);
    if (want("mp3-id3-wrap"))
        isolated("mp3-id3-wrap", "an ID3 frame size that wraps (MP3 stream)", runMp3Id3Wrap);
    if (want("aac-id3-wrap"))
        isolated("aac-id3-wrap", "an ID3 frame size that wraps (AAC stream)", runAacId3Wrap);
    if (want("id3-latin1")) runId3Latin1();
    if (want("id3-utf16")) runId3Utf16();
    if (want("metadata-defaults")) runMetadataDefaults();
#if !defined(NO_XIPH_LIBS)
    if (want("opus-wrap"))
        isolated("opus-wrap", "an OpusTags vendor length that wraps", runOpusWrap);
    if (want("flac-split-header"))
        isolated("flac-split-header", "a FLAC header split across deliveries",
                 runFlacSplitHeader);
    if (want("flac-seek-split-header"))
        isolated("flac-seek-split-header", "a seek after a split FLAC header",
                 runFlacSeekSplitHeader);
    if (want("flac-truncated-header"))
        isolated("flac-truncated-header", "data that ends inside the FLAC metadata",
                 runFlacTruncatedHeader);
#endif

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}
