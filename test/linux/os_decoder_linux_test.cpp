// Linux OS decoders: NativeAudioDecoder (src/native_decoder/os_decoder_linux.cpp)
// and the streaming AACDecoderWrapper (src/audiobuffer/aac_stream_decoder_linux.cpp).
// Both dlopen the system FFmpeg (libavformat/libavcodec .so.58 .. .so.63) at
// run time and reach into its structs by hard-coded offsets, so the two cases
// that matter are tested separately:
//
//   * FFmpeg present: the shared contract in test/support/os_decoder_checks.h
//     (fixture lengths and levels, priming trimmed, malformed input, ADTS /
//     AC-3 / E-AC-3, chunked streaming, temp files, threads).
//   * FFmpeg missing: every entry point fails cleanly with a message, nothing
//     crashes, no temporary file is left, and the header-only parts (format
//     sniffing, ADTS/AC-3 metadata, the track-change callback) still work.
//
// The "missing" case does not need a machine without FFmpeg: this file
// defines dlopen(), which the decoders (linked into this executable) call
// instead of libc's, and with OSDEC_HIDE_FFMPEG=1 it refuses libav* names.
// test/linux/os_decoder_linux_test.sh runs the binary both ways.
//
//   os_decoder_linux_test <asset dir> <tmp dir> [--expect-ffmpeg|--expect-no-ffmpeg]
//
// Verified on Linux arm64 against FFmpeg 6.1 (lavf/lavc 60, test/docker), and
// on macOS against FFmpeg 9 dylibs by building patched copies of the two
// sources (see the .sh header), which checks the struct offsets for lavc 63.

#include "../support/os_decoder_checks.h"

#include <dlfcn.h>
#include <unistd.h>

#include <cstring>
#include <string>

using namespace osdec;

// ---------------------------------------------------------------------------
// dlopen() interposer.

static bool hideFfmpeg() {
    const char *v = std::getenv("OSDEC_HIDE_FFMPEG");
    return v != nullptr && v[0] == '1';
}

// Every libav* dlopen() call. With OSDEC_HIDE_FFMPEG=1 they are refused here;
// without FFmpeg on the machine they fail on their own.
static int gLibavOpens = 0;

// glibc declares dlopen() with __THROWNL, which is noexcept in C++11 and
// later; a redeclaration must match it.
#if defined(__GLIBC__)
#define OSDEC_DLOPEN_NOEXCEPT noexcept(true)
#else
#define OSDEC_DLOPEN_NOEXCEPT
#endif

extern "C" void *dlopen(const char *file, int mode) OSDEC_DLOPEN_NOEXCEPT {
    using DlopenFn = void *(*)(const char *, int);
    static DlopenFn real = reinterpret_cast<DlopenFn>(dlsym(RTLD_NEXT, "dlopen"));
    const bool libav = file != nullptr &&
        (std::strstr(file, "libavformat") != nullptr || std::strstr(file, "libavcodec") != nullptr ||
         std::strstr(file, "libavutil") != nullptr || std::strstr(file, "libswresample") != nullptr);
    if (libav) ++gLibavOpens;
    if (libav && hideFfmpeg()) return nullptr;
    return real != nullptr ? real(file, mode) : nullptr;
}

namespace {

std::string gDir;
std::string gTmp;

/// Whether the decoders will find FFmpeg: the same names they try, through
/// the interposer above.
bool ffmpegLoadable() {
    const char *const formats[] = {"libavformat.so.63", "libavformat.so.62", "libavformat.so.61", "libavformat.so.60",
                                   "libavformat.so.59", "libavformat.so.58", "libavformat.so", nullptr};
    const char *const codecs[] = {"libavcodec.so.63", "libavcodec.so.62", "libavcodec.so.61", "libavcodec.so.60",
                                  "libavcodec.so.59", "libavcodec.so.58", "libavcodec.so", nullptr};
    auto any = [](const char *const *names, std::string &which) {
        for (int i = 0; names[i]; ++i) {
            if (void *h = dlopen(names[i], RTLD_LAZY | RTLD_LOCAL)) {
                dlclose(h);
                which = names[i];
                return true;
            }
        }
        return false;
    };
    std::string f, c;
    const bool ok = any(formats, f) && any(codecs, c);
    if (ok) std::cout << "FFmpeg found: " << f << ", " << c << std::endl;
    return ok;
}

/// Entries of the test's temporary directory (main() points TMPDIR at it)
/// named soloud_media_* (decodeMemory's temporary files).
long countSoloudTempFiles() {
    std::error_code ec;
    long n = 0;
    for (std::filesystem::directory_iterator it(gTmp, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().filename().string().rfind("soloud_media_", 0) == 0) ++n;
    }
    return ec ? -1 : n;
}

void testWithoutFfmpeg() {
    std::cout << "[Test] Without FFmpeg every entry point fails cleanly" << std::endl;
    const long tempBefore = countSoloudTempFiles();
    // Not counting the probe in ffmpegLoadable().
    const int libavOpensBefore = gLibavOpens;

    for (const char *name : {"aac_lc_stereo_48000.m4a", "sample-AAC.aac", "sample-AC3.ac3", "sample-EAC3.eac3"}) {
        const std::string path = joinPath(gDir, name);
        auto fromFile = decodeFileTimed(path);
        expectFailure((std::string(name) + " (file)").c_str(), fromFile->d, fromFile->seconds);
        OSDEC_CHECK(fromFile->d.errorMessage.find("FFmpeg") != std::string::npos,
                    name << ": message does not say FFmpeg is missing: " << fromFile->d.errorMessage);
        const auto bytes = readFile(path);
        auto fromMem = decodeMemoryTimed(bytes);
        expectFailure((std::string(name) + " (mem)").c_str(), fromMem->d, fromMem->seconds);
        OSDEC_CHECK(fromMem->d.errorMessage.find("FFmpeg") != std::string::npos,
                    name << ": message does not say FFmpeg is missing: " << fromMem->d.errorMessage);
    }
    OSDEC_CHECK(countSoloudTempFiles() == tempBefore, "decodeMemory left soloud_media_* files behind");

    // The stream decoder refuses to initialize, and decode() reports it
    // instead of crashing, for every format.
    const auto adts = readFile(joinPath(gDir, "sample-AAC.aac"));
    const auto ac3 = readFile(joinPath(gDir, "sample-AC3.ac3"));
    struct Case { DetectedType type; const std::vector<unsigned char> *bytes; const char *name; };
    for (const Case &c : {Case{DetectedType::BUFFER_AAC, &adts, "AAC"}, Case{DetectedType::BUFFER_AC3, &ac3, "AC-3"},
                          Case{DetectedType::BUFFER_EAC3, &ac3, "E-AC-3"}}) {
        AACDecoderWrapper wrapper(c.type);
        OSDEC_CHECK(!wrapper.initializeDecoder(44100, 2), c.name << ": initializeDecoder succeeded without FFmpeg");
        bool metadata = false;
        wrapper.setTrackChangeCallback([&](const AudioMetadata &) { metadata = true; });
        std::vector<unsigned char> chunk(c.bytes->begin(), c.bytes->begin() + 8192);
        int rate = 0, channels = 0;
        auto [samples, err] = wrapper.decode(chunk, &rate, &channels);
        std::cout << "  " << c.name << " stream: decode -> error " << static_cast<int>(err) << ", " << samples.size()
                  << " samples, metadata callback " << (metadata ? "fired" : "not fired") << std::endl;
        OSDEC_CHECK(samples.empty(), c.name << ": samples without a decoder");
        OSDEC_CHECK(err != DecoderError::NoError, c.name << ": decode() without a decoder reported NoError");
        wrapper.setDataEnded();
        // Nothing can be decoded, so nothing is pending: a "drain while
        // pending" loop must end.
        OSDEC_CHECK(!wrapper.hasPendingData(), c.name << ": pending data without a decoder");
        if (c.type != DetectedType::BUFFER_EAC3) {
            // Header metadata comes from the wrapper's own parser.
            OSDEC_CHECK(metadata, c.name << ": the track-change callback did not fire from the header");
        }
    }

    // Format sniffing and header parsing are FFmpeg-free.
    OSDEC_CHECK(NativeAudioDecoder::isAacAdts(adts.data(), adts.size()), "ADTS fixture not sniffed as ADTS");
    OSDEC_CHECK(NativeAudioDecoder::isAc3(ac3.data(), ac3.size()), "AC-3 fixture not sniffed as AC-3");
    AacMetadata aac;
    OSDEC_CHECK(AACDecoderWrapper::parseAacAdtsMetadata(adts.data(), adts.size(), aac) && aac.sampleRate == 44100 &&
                    aac.channels == 2,
                "ADTS header parse: " << aac.sampleRate << " Hz " << aac.channels << " ch");
    const int attempts = gLibavOpens - libavOpensBefore;
    std::cout << "  " << attempts << " libav* dlopen() calls by the decoders ("
              << (hideFfmpeg() ? "refused: FFmpeg hidden" : "failed: FFmpeg not installed") << ")" << std::endl;
    OSDEC_CHECK(attempts > 0, "the decoders never tried to dlopen FFmpeg");
}

/// Decoding from many threads at once while FFmpeg is loaded for the first
/// time: the loaders are lazily initialized globals.
void testFirstLoadRace() {
    std::cout << "[Test] First FFmpeg load from several threads at once" << std::endl;
    const auto adts = readFile(joinPath(gDir, "sample-AAC.aac"));
    std::vector<unsigned char> head(adts.begin(), adts.begin() + 32768);
    std::atomic<int> failures{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> pool;
    for (int t = 0; t < 8; ++t) {
        pool.emplace_back([&, t] {
            while (!go.load()) std::this_thread::yield();
            if (t % 2 == 0) {
                DecodedAudioData d = NativeAudioDecoder::decodeMemory(head.data(), head.size());
                if (!d.success) ++failures;
                d.freeSamples();
            } else {
                AACDecoderWrapper w(DetectedType::BUFFER_AAC);
                if (!w.initializeDecoder(0, 0)) ++failures;
            }
        });
    }
    go = true;
    for (auto &th : pool) th.join();
    OSDEC_CHECK(failures == 0, failures << " threads failed to decode/initialize on the first load");
}

/// decodeMemory writes its temporary file to $TMPDIR, not a hard-coded /tmp.
void testTmpdirHonored() {
    std::cout << "[Test] decodeMemory creates its temporary file in $TMPDIR" << std::endl;
    const auto adts = readFile(joinPath(gDir, "sample-AAC.aac"));
    const std::string missing = joinPath(gTmp, "no_such_dir");
    setenv("TMPDIR", missing.c_str(), 1);
    DecodedAudioData bad = NativeAudioDecoder::decodeMemory(adts.data(), 32768);
    std::cout << "  TMPDIR=" << missing << ": " << (bad.success ? "decoded" : bad.errorMessage) << std::endl;
    OSDEC_CHECK(!bad.success && bad.errorMessage.find("no_such_dir") != std::string::npos,
                "decodeMemory did not try $TMPDIR (" << bad.errorMessage << ")");
    bad.freeSamples();
    setenv("TMPDIR", gTmp.c_str(), 1);
    DecodedAudioData good = NativeAudioDecoder::decodeMemory(adts.data(), 32768);
    OSDEC_CHECK(good.success, "decodeMemory with TMPDIR=" << gTmp << " failed: " << good.errorMessage);
    good.freeSamples();
}

} // namespace

int main(int argc, char **argv) {
    gDir = argc > 1 ? argv[1] : "example/assets/audio";
    gTmp = argc > 2 && argv[2][0] ? argv[2] : "/tmp";
    const std::string expect = argc > 3 ? argv[3] : "";
    // decodeMemory's temporary files go to $TMPDIR; keep them where the
    // checks below can count them.
    setenv("TMPDIR", gTmp.c_str(), 1);

    std::string missing;
    if (!haveFixtures(gDir, missing)) {
        std::cout << "SKIPPED: fixture not found: " << missing << std::endl;
        return 0;
    }

    const bool have = ffmpegLoadable();
    std::cout << "Mode: " << (have ? "FFmpeg present" : "FFmpeg missing")
              << (hideFfmpeg() ? " (hidden by OSDEC_HIDE_FFMPEG=1)" : "") << std::endl;
    if (expect == "--expect-ffmpeg" && !have) {
        std::cout << "  FAIL: FFmpeg was expected but cannot be loaded" << std::endl;
        return 1;
    }
    if (expect == "--expect-no-ffmpeg" && have) {
        std::cout << "  FAIL: FFmpeg was expected to be hidden" << std::endl;
        return 1;
    }

    if (have) {
        testFirstLoadRace();
        Policy policy;
        // decodeMemory goes through a temporary file into the same FFmpeg
        // path as decodeFile.
        policy.memFileTolerance = 0.0f;
        // libavformat decodes what a cut-short moov-first M4A still has.
        policy.truncatedM4aMayDecode = true;
        // Distribution FFmpeg builds can lack the AC-3 decoders.
        policy.ac3Optional = true;
        // decodeMemory/decodeFile use libavformat's demuxer, the stream
        // decoder libavcodec's parser: same decoder, same samples.
        policy.streamTolerance = 1e-6f;
        const long tempBefore = countSoloudTempFiles();
        testTmpdirHonored();
        runAll(policy, gDir, gTmp, gTmp);
        OSDEC_CHECK(countSoloudTempFiles() == tempBefore, "decodeMemory left soloud_media_* files behind");
    } else {
        testWithoutFfmpeg();
    }
    return finish(have ? "LINUX OS DECODER (FFMPEG)" : "LINUX OS DECODER (NO FFMPEG)");
}
