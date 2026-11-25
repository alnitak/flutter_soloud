// AudioToolbox decoders on Apple platforms: NativeAudioDecoder
// (src/native_decoder/os_decoder_apple.mm, ExtAudioFile) and the streaming
// AACDecoderWrapper (src/audiobuffer/aac_stream_decoder_apple.mm,
// AudioFileStream + AudioConverter).
//
// Runs the shared contract in test/support/os_decoder_checks.h with Apple's
// policy, then Apple-specific stream checks: HE-AAC (SBR) ADTS, ICY metadata
// interleaved with real audio, leading garbage before the first frame, and
// reinitialization.
//
// Built and run by test/macos/aac_stream_decoder_apple_test.sh (macOS) and
// test/ios/aac_stream_decoder_apple_test.sh (iOS simulator, via simctl spawn).
//
//   aac_stream_decoder_apple_test <asset dir> <tmp dir> [he-aac.adts]
//
// The HE-AAC fixture is made by the script with afconvert, which is not
// available inside the simulator.

#include "../support/os_decoder_checks.h"

#include <TargetConditionals.h>

using namespace osdec;

namespace {

std::string gDir;
std::string gTmp;

/// The interleaved PCM of [bytes] streamed through a fresh wrapper.
std::vector<float> streamAll(DetectedType format, const std::vector<unsigned char> &bytes, size_t chunk,
                             int &rate, int &channels, AACDecoderWrapper *wrapper = nullptr) {
    std::unique_ptr<AACDecoderWrapper> own;
    if (!wrapper) {
        own.reset(new AACDecoderWrapper(format));
        wrapper = own.get();
        if (!wrapper->initializeDecoder(0, 0)) return {};
    }
    std::vector<float> out;
    for (size_t off = 0; off < bytes.size(); off += chunk) {
        std::vector<unsigned char> piece(bytes.begin() + static_cast<long>(off),
                                         bytes.begin() + static_cast<long>(std::min(bytes.size(), off + chunk)));
        auto [samples, err] = wrapper->decode(piece, &rate, &channels);
        OSDEC_CHECK(err == DecoderError::NoError, "decode returned error " << static_cast<int>(err));
        out.insert(out.end(), samples.begin(), samples.end());
    }
    wrapper->setDataEnded();
    std::vector<unsigned char> none;
    for (int i = 0; i < 100000 && wrapper->hasPendingData(); ++i) {
        auto [samples, err] = wrapper->decode(none, &rate, &channels);
        out.insert(out.end(), samples.begin(), samples.end());
    }
    OSDEC_CHECK(!wrapper->hasPendingData(), "stream never finished draining");
    return out;
}

// HE-AAC's ADTS header carries the 22.05 kHz core rate; the decoder outputs
// 44.1 kHz with SBR. Whatever rate a path reports, the frame count at that
// rate must still be the 15 s of the source, and both paths must agree.
void testHeAacAdts(const std::string &path) {
    std::cout << "[Test] HE-AAC (SBR) ADTS: reported rate matches the decoded length" << std::endl;
    if (path.empty() || !fileExists(path)) {
        std::cout << "  no HE-AAC fixture given (made by the script with afconvert); not run" << std::endl;
        return;
    }
    const auto bytes = readFile(path);
    AacMetadata meta;
    OSDEC_CHECK(AACDecoderWrapper::parseAacAdtsMetadata(bytes.data(), bytes.size(), meta), "ADTS header not parsed");
    std::cout << "  ADTS header: " << meta.sampleRate << " Hz, " << meta.channels << " ch, profile " << meta.profile
              << std::endl;

    auto whole = decodeMemoryTimed(bytes);
    OSDEC_CHECK(whole->d.success, "decodeMemory failed: " << whole->d.errorMessage);
    if (whole->d.success) {
        const double seconds = whole->d.sampleCount / whole->d.sampleRate;
        std::cout << "  decodeMemory: " << whole->d.sampleCount << " frames at " << whole->d.sampleRate << " Hz = "
                  << seconds << " s" << std::endl;
        OSDEC_CHECK(std::fabs(seconds - 15.0) < 0.15, "decodeMemory: " << seconds << " s at the reported rate, expected 15");
    }

    int rate = 0, channels = 0;
    const auto pcm = streamAll(DetectedType::BUFFER_AAC, bytes, 4096, rate, channels);
    OSDEC_CHECK(rate > 0 && channels == 2, "stream format " << channels << "ch " << rate << " Hz");
    if (rate > 0 && channels > 0) {
        const double frames = static_cast<double>(pcm.size()) / channels;
        const double seconds = frames / rate;
        std::cout << "  stream: " << frames << " frames at " << rate << " Hz = " << seconds << " s" << std::endl;
        OSDEC_CHECK(std::fabs(seconds - 15.0) < 0.15, "stream: " << seconds << " s at the reported rate, expected 15");
        if (whole->d.success) {
            // The ADTS header carries the core rate; the stream decoder must
            // decode the HE-AAC entry of the format list, as ExtAudioFile does.
            OSDEC_CHECK(rate == static_cast<int>(whole->d.sampleRate),
                        "stream reports " << rate << " Hz, decodeMemory " << whole->d.sampleRate << " Hz");
            const size_t streamFrames = pcm.size() / static_cast<size_t>(channels);
            OSDEC_CHECK(streamFrames == whole->d.sampleCount,
                        "stream: " << streamFrames << " frames, decodeMemory " << whole->d.sampleCount);
            if (streamFrames == whole->d.sampleCount && channels == static_cast<int>(whole->d.channels)) {
                float worst = 0.0f;
                for (size_t i = 0; i < streamFrames; ++i)
                    for (int c = 0; c < channels; ++c)
                        worst = std::max(worst, std::fabs(pcm[i * channels + c] -
                                                          whole->d.samples[c * whole->d.sampleCount + i]));
                std::cout << "  stream vs decodeMemory: max diff " << worst << std::endl;
                OSDEC_CHECK(worst <= 1e-4f, "HE-AAC stream PCM differs from decodeMemory by " << worst);
            }
        }
    }
}

// ICY metadata blocks interleaved with real AAC frames must be stripped
// without disturbing the audio: same PCM as the plain stream.
void testIcyInterleaved() {
    std::cout << "[Test] ICY metadata interleaved with ADTS audio is stripped losslessly" << std::endl;
    const auto plain = readFile(joinPath(gDir, "sample-AAC.aac"));
    const int metaInt = 8192;
    std::vector<unsigned char> icy;
    int blocks = 0;
    for (size_t off = 0; off < plain.size(); off += metaInt) {
        icy.insert(icy.end(), plain.begin() + static_cast<long>(off),
                   plain.begin() + static_cast<long>(std::min(plain.size(), off + metaInt)));
        if (off + metaInt > plain.size()) break;
        if (blocks % 2 == 0) {
            std::string meta = "StreamTitle='Artist " + std::to_string(blocks) + " - Song " + std::to_string(blocks) + "';";
            while (meta.size() % 16) meta.push_back('\0');
            icy.push_back(static_cast<unsigned char>(meta.size() / 16));
            icy.insert(icy.end(), meta.begin(), meta.end());
        } else {
            icy.push_back(0); // no metadata this interval
        }
        ++blocks;
    }

    int rate = 0, channels = 0;
    const auto reference = streamAll(DetectedType::BUFFER_AAC, plain, 4096, rate, channels);

    AACDecoderWrapper wrapper(DetectedType::BUFFER_AAC);
    OSDEC_CHECK(wrapper.initializeDecoder(0, 0), "initializeDecoder failed");
    wrapper.setIcyMetaInt(metaInt);
    int titles = 0;
    std::string lastTitle;
    wrapper.setTrackChangeCallback([&](const AudioMetadata &m) {
        if (!m.aacMetadata.title.empty()) {
            ++titles;
            lastTitle = m.aacMetadata.title;
        }
    });
    int rate2 = 0, channels2 = 0;
    const auto stripped = streamAll(DetectedType::BUFFER_AAC, icy, 3000, rate2, channels2, &wrapper);
    std::cout << "  " << blocks << " intervals, " << titles << " title callbacks, last \"" << lastTitle << "\", "
              << stripped.size() << " vs " << reference.size() << " samples" << std::endl;
    OSDEC_CHECK(titles >= 1, "no ICY title reached the track-change callback");
    OSDEC_CHECK(stripped.size() == reference.size(),
                "ICY stream decoded " << stripped.size() << " samples, plain " << reference.size());
    if (stripped.size() == reference.size()) {
        const float diff = maxAbsDiff(stripped.data(), reference.data(), reference.size());
        OSDEC_CHECK(diff <= 1e-6f, "ICY stream PCM differs from the plain stream by " << diff);
    }
}

// A stream joined mid-frame (an HTTP radio's first chunk) must resync on the
// first ADTS / AC-3 header and decode the rest.
void testLeadingGarbage() {
    std::cout << "[Test] Bytes before the first frame header are skipped" << std::endl;
    struct Case { const char *file; DetectedType type; };
    for (const Case &c : {Case{"sample-AAC.aac", DetectedType::BUFFER_AAC},
                          Case{"sample-AC3.ac3", DetectedType::BUFFER_AC3}}) {
        const auto plain = readFile(joinPath(gDir, c.file));
        std::vector<unsigned char> joined;
        for (int i = 0; i < 333; ++i) joined.push_back(static_cast<unsigned char>(0x11 + (i % 7)));
        joined.insert(joined.end(), plain.begin(), plain.end());
        int rate = 0, channels = 0, rate2 = 0, channels2 = 0;
        const auto reference = streamAll(c.type, plain, 4096, rate, channels);
        const auto resynced = streamAll(c.type, joined, 4096, rate2, channels2);
        std::cout << "  " << c.file << ": " << resynced.size() << " samples (plain " << reference.size() << ")"
                  << std::endl;
        OSDEC_CHECK(rate2 == rate && channels2 == channels, c.file << ": format changed by the leading bytes");
        OSDEC_CHECK(resynced.size() == reference.size(), c.file << ": " << resynced.size() << " samples, expected "
                                                                << reference.size());
    }
}

// initializeDecoder() resets the decoder: a second stream decodes like the
// first, with nothing carried over.
void testReinitialize() {
    std::cout << "[Test] initializeDecoder() resets a used decoder" << std::endl;
    const auto bytes = readFile(joinPath(gDir, "sample-AAC.aac"));
    std::vector<unsigned char> half(bytes.begin(), bytes.begin() + static_cast<long>(bytes.size() / 2));
    AACDecoderWrapper wrapper(DetectedType::BUFFER_AAC);
    OSDEC_CHECK(wrapper.initializeDecoder(0, 0), "first initializeDecoder failed");
    int rate = 0, channels = 0;
    auto partial = wrapper.decode(half, &rate, &channels);
    (void)partial;
    OSDEC_CHECK(wrapper.initializeDecoder(0, 0), "second initializeDecoder failed");
    const auto again = streamAll(DetectedType::BUFFER_AAC, bytes, 4096, rate, channels, &wrapper);
    int rate2 = 0, channels2 = 0;
    const auto fresh = streamAll(DetectedType::BUFFER_AAC, bytes, 4096, rate2, channels2);
    std::cout << "  reused: " << again.size() << " samples, fresh: " << fresh.size() << std::endl;
    OSDEC_CHECK(again.size() == fresh.size(), "reinitialized decoder produced " << again.size() << " samples, fresh "
                                                                                << fresh.size());
}

} // namespace

int main(int argc, char **argv) {
    gDir = argc > 1 ? argv[1] : "example/assets/audio";
    const char *envTmp = std::getenv("TMPDIR");
    gTmp = argc > 2 && argv[2][0] ? argv[2] : (envTmp ? envTmp : "/tmp");
    const std::string heAac = argc > 3 ? argv[3] : "";

    std::string missing;
    if (!haveFixtures(gDir, missing)) {
        std::cout << "SKIPPED: fixture not found: " << missing << std::endl;
        return 0;
    }

    Policy policy;
    // ExtAudioFile decodes what a cut-short M4A still has when moov comes first.
    policy.truncatedM4aMayDecode = true;
#if TARGET_OS_IPHONE
    // The simulator has no Dolby decoder on some runtimes.
    policy.ac3Optional = true;
#else
    policy.ac3Optional = false;
#endif
    // ExtAudioFile and AudioConverter take slightly different float paths.
    policy.streamTolerance = 1e-5f;
    policy.elementaryFrameSlack = 1024;
    runAll(policy, gDir, gTmp, gTmp);
    testHeAacAdts(heAac);
    testIcyInterleaved();
    testLeadingGarbage();
    testReinitialize();
    return finish("APPLE AAC STREAM DECODER");
}
