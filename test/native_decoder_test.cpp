#include "../src/native_decoder/native_audio_decoder.h"
#include "../src/audiobuffer/aac_stream_decoder.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

void testHeaderSniffing() {
    std::cout << "[Test] Header sniffing..." << std::endl;

    // 1. MP4 / M4A ftyp
    unsigned char m4aHeader[] = {0x00, 0x00, 0x00, 0x20, 'f', 't', 'y', 'p', 'M', '4', 'A', ' '};
    assert(NativeAudioDecoder::isM4aOrMp4(m4aHeader, sizeof(m4aHeader)));
    assert(NativeAudioDecoder::isSupportedNativeFormat(m4aHeader, sizeof(m4aHeader)));

    unsigned char notM4a[] = {0x00, 0x00, 0x00, 0x20, 'm', 'o', 'o', 'v'};
    assert(!NativeAudioDecoder::isM4aOrMp4(notM4a, sizeof(notM4a)));

    // 2. AAC ADTS (syncword 0xFFF)
    unsigned char aacHeader[] = {0xFF, 0xF1, 0x50, 0x80};
    assert(NativeAudioDecoder::isAacAdts(aacHeader, sizeof(aacHeader)));
    assert(NativeAudioDecoder::isSupportedNativeFormat(aacHeader, sizeof(aacHeader)));

    unsigned char notAac[] = {0xFE, 0xF1, 0x50, 0x80};
    assert(!NativeAudioDecoder::isAacAdts(notAac, sizeof(notAac)));

    // 3. AC-3 / E-AC-3 (syncword 0x0B77 / 0x770B)
    unsigned char ac3HeaderBE[] = {0x0B, 0x77, 0x12, 0x34};
    unsigned char ac3HeaderLE[] = {0x77, 0x0B, 0x12, 0x34};
    assert(NativeAudioDecoder::isAc3OrEac3(ac3HeaderBE, sizeof(ac3HeaderBE)));
    assert(NativeAudioDecoder::isAc3OrEac3(ac3HeaderLE, sizeof(ac3HeaderLE)));
    assert(NativeAudioDecoder::isSupportedNativeFormat(ac3HeaderBE, sizeof(ac3HeaderBE)));
    assert(NativeAudioDecoder::isSupportedNativeFormat(ac3HeaderLE, sizeof(ac3HeaderLE)));

    unsigned char notAc3[] = {0x0B, 0x78, 0x12, 0x34};
    assert(!NativeAudioDecoder::isAc3OrEac3(notAc3, sizeof(notAc3)));

    // 4. Null and short checks
    assert(!NativeAudioDecoder::isM4aOrMp4(nullptr, 100));
    assert(!NativeAudioDecoder::isM4aOrMp4(m4aHeader, 4));
    assert(!NativeAudioDecoder::isAacAdts(nullptr, 100));
    assert(!NativeAudioDecoder::isAacAdts(aacHeader, 1));
    assert(!NativeAudioDecoder::isAc3OrEac3(nullptr, 100));
    assert(!NativeAudioDecoder::isAc3OrEac3(ac3HeaderBE, 1));

    std::cout << "  Passed!" << std::endl;
}

void testInterleavedToPlanar() {
    std::cout << "[Test] Interleaved to Planar conversion..." << std::endl;

    // Stereo, 3 frames
    // Interleaved: L0, R0, L1, R1, L2, R2
    const float interleaved[] = {1.0f, 10.0f, 2.0f, 20.0f, 3.0f, 30.0f};
    const size_t frameCount = 3;
    const unsigned int channels = 2;
    float planar[6] = {0};

    NativeAudioDecoder::interleavedToPlanar(interleaved, planar, frameCount, channels);

    // Channel 0: 1.0f, 2.0f, 3.0f
    assert(std::fabs(planar[0] - 1.0f) < 1e-6f);
    assert(std::fabs(planar[1] - 2.0f) < 1e-6f);
    assert(std::fabs(planar[2] - 3.0f) < 1e-6f);

    // Channel 1: 10.0f, 20.0f, 30.0f
    assert(std::fabs(planar[3] - 10.0f) < 1e-6f);
    assert(std::fabs(planar[4] - 20.0f) < 1e-6f);
    assert(std::fabs(planar[5] - 30.0f) < 1e-6f);

    // Safe handling of nulls and zeros
    NativeAudioDecoder::interleavedToPlanar(nullptr, planar, frameCount, channels);
    NativeAudioDecoder::interleavedToPlanar(interleaved, nullptr, frameCount, channels);
    NativeAudioDecoder::interleavedToPlanar(interleaved, planar, 0, channels);
    NativeAudioDecoder::interleavedToPlanar(interleaved, planar, frameCount, 0);

    std::cout << "  Passed!" << std::endl;
}

void testAacStreamDecoder() {
    std::cout << "[Test] AAC Stream Decoder..." << std::endl;

    // Valid 7-byte ADTS frame header with frame length = 16 bytes:
    // Syncword: 0xFFF, ID: 0 (MPEG-4), Layer: 00, Protection: 1 (no CRC) -> 0xFF 0xF1
    // Profile: 01 (AAC-LC), freq_idx: 0100 (44.1kHz), priv: 0, ch: 010 (stereo) -> 0x50 0x80
    // frame length: 16 bytes (byte 4 = 0x02)
    std::vector<unsigned char> validAdts = {
        0xFF, 0xF1, 0x50, 0x80, 0x02, 0x1F, 0xFC,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09
    };
    assert(AACDecoderWrapper::checkForValidFrames(validAdts));

    // Test parseAacAdtsMetadata
    AacMetadata aacMeta;
    assert(AACDecoderWrapper::parseAacAdtsMetadata(validAdts.data(), validAdts.size(), aacMeta));
    assert(aacMeta.sampleRate == 44100);
    assert(aacMeta.channels == 2);
    assert(aacMeta.profile == "LC");
    assert(aacMeta.frameLength == 16);

    // Truncated buffer (< 7 bytes)
    std::vector<unsigned char> shortBuf = {0xFF, 0xF1, 0x50};
    assert(!AACDecoderWrapper::checkForValidFrames(shortBuf));
    assert(!AACDecoderWrapper::parseAacAdtsMetadata(shortBuf.data(), shortBuf.size(), aacMeta));

    // Garbage buffer
    std::vector<unsigned char> garbageBuf = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    assert(!AACDecoderWrapper::checkForValidFrames(garbageBuf));
    assert(!AACDecoderWrapper::parseAacAdtsMetadata(garbageBuf.data(), garbageBuf.size(), aacMeta));

    // Test decoder instance lifecycle and onTrackChange callback
    AACDecoderWrapper decoder;
    bool initOk = decoder.initializeDecoder(44100, 2);
#if defined(__APPLE__)
    assert(initOk);
#endif

    bool aacCallbackFired = false;
    decoder.setTrackChangeCallback([&](const AudioMetadata &meta) {
        aacCallbackFired = true;
        assert(meta.type == DetectedType::BUFFER_AAC);
        assert(meta.aacMetadata.sampleRate == 44100);
        assert(meta.aacMetadata.channels == 2);
    });

    // Empty buffer decode should not crash and return NoError
    std::vector<unsigned char> emptyBuf;
    int sampleRate = 0;
    int channels = 0;
    auto [decoded, err] = decoder.decode(emptyBuf, &sampleRate, &channels);
    assert(err == DecoderError::NoError);
    assert(!decoder.hasPendingData());
    assert(!aacCallbackFired);

    // Decoding valid ADTS buffer fires metadata callback
    std::vector<unsigned char> adtsBuf = validAdts;
    decoder.decode(adtsBuf, &sampleRate, &channels);
    assert(aacCallbackFired);

    decoder.setDataEnded();
    assert(!decoder.hasPendingData());

    // Test AAC with prepended ID3 tag
    std::vector<unsigned char> id3Tag = {
        'I', 'D', '3', 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 32,
        // TIT2 (Title: "Song") -> len 5 (1 encoding + 4 text)
        'T', 'I', 'T', '2', 0x00, 0x00, 0x00, 0x05, 0x00, 0x00,
        0x00, 'S', 'o', 'n', 'g',
        // TPE1 (Artist: "Artist") -> len 7 (1 encoding + 6 text)
        'T', 'P', 'E', '1', 0x00, 0x00, 0x00, 0x07, 0x00, 0x00,
        0x00, 'A', 'r', 't', 'i', 's', 't'
    };
    AacMetadata parsedId3;
    size_t id3Size = 0;
    assert(AACDecoderWrapper::parseId3Tags(id3Tag.data(), id3Tag.size(), parsedId3, id3Size));
    assert(parsedId3.title == "Song");
    assert(parsedId3.artist == "Artist");
    assert(id3Size == 42);

    // Buffer with ID3 + ADTS payload: verify ADTS detected after ID3 tag
    std::vector<unsigned char> id3AndAac = id3Tag;
    id3AndAac.insert(id3AndAac.end(), validAdts.begin(), validAdts.end());
    assert(NativeAudioDecoder::isAacAdts(id3AndAac.data() + id3Size, id3AndAac.size() - id3Size));
    assert(AACDecoderWrapper::checkForValidFrames(std::vector<unsigned char>(id3AndAac.begin() + id3Size, id3AndAac.end())));

    // Test AAC stream decoder stripping ID3 and notifying metadata
    AACDecoderWrapper id3AacDecoder;
    id3AacDecoder.initializeDecoder(44100, 2);
    bool id3Fired = false;
    id3AacDecoder.setTrackChangeCallback([&](const AudioMetadata &m) {
        id3Fired = true;
        assert(m.type == DetectedType::BUFFER_AAC);
        assert(m.aacMetadata.title == "Song");
        assert(m.aacMetadata.artist == "Artist");
    });
    std::vector<unsigned char> id3BufCopy = id3AndAac;
    id3AacDecoder.decode(id3BufCopy, &sampleRate, &channels);
    assert(id3Fired);

    // Test ICY metadata stripping on AAC decoder
    AACDecoderWrapper icyAacDecoder;
    icyAacDecoder.initializeDecoder(44100, 2);
    icyAacDecoder.setIcyMetaInt(16);
    bool icyFired = false;
    icyAacDecoder.setTrackChangeCallback([&](const AudioMetadata &m) {
        icyFired = true;
        assert(m.type == DetectedType::BUFFER_AAC);
        assert(m.aacMetadata.title == "Radio Song");
        assert(m.aacMetadata.artist == "Radio Artist");
    });
    // Build 16 bytes audio + 1 byte len (2 * 16 = 32 bytes) + 32 bytes metadata + 16 bytes audio
    std::vector<unsigned char> icyBuf(16, 0x11);
    icyBuf.push_back(3); // 3 * 16 = 48 bytes metadata
    std::string metaStr = "StreamTitle='Radio Artist - Radio Song';";
    while (metaStr.size() < 48) metaStr.push_back(' ');
    icyBuf.insert(icyBuf.end(), metaStr.begin(), metaStr.end());
    icyBuf.insert(icyBuf.end(), 16, 0x22);

    icyAacDecoder.decode(icyBuf, &sampleRate, &channels);
    assert(icyFired);

    std::cout << "  Passed!" << std::endl;
}

void testAc3StreamDecoder() {
    std::cout << "[Test] AC-3 / E-AC-3 Stream Decoder..." << std::endl;

    // AC-3 header: 0x0B, 0x77, crc1 (2 bytes), fscod/frmsizecod, bsid <= 10
    // Byte 4: fscod = 0 (48kHz), frmsizecod = 24 (256 kbps) -> (0 << 6) | 24 = 0x18
    // Byte 5: bsid = 8 (0x08 << 3 = 0x40), bsmod = 0
    // Byte 6: acmod = 2 (stereo -> 2 << 5 = 0x40)
    std::vector<unsigned char> ac3Frame = {
        0x0B, 0x77, 0x12, 0x34, 0x18, 0x40, 0x40, 0x00
    };
    assert(NativeAudioDecoder::isAc3(ac3Frame.data(), ac3Frame.size()));
    assert(!NativeAudioDecoder::isEac3(ac3Frame.data(), ac3Frame.size()));
    assert(AACDecoderWrapper::checkForValidAc3Frames(ac3Frame));
    assert(NativeAudioDecoder::findAc3Syncword(ac3Frame.data(), ac3Frame.size()) == 0);

    Ac3Metadata ac3Meta;
    assert(AACDecoderWrapper::parseAc3Metadata(ac3Frame.data(), ac3Frame.size(), ac3Meta));
    assert(ac3Meta.sampleRate == 48000);
    assert(ac3Meta.channels == 2);
    assert(ac3Meta.bitrate == 256000);
    assert(ac3Meta.bsid == 8);
    assert(ac3Meta.acmod == 2);
    assert(ac3Meta.frameSize == 1024);

    // E-AC-3 header: 0x0B, 0x77, strmtyp/substreamid/frmsiz (2 bytes), fscod, bsid = 16 (0x10 << 3 = 0x80)
    // Byte 2, 3: strmtyp = 0, substreamid = 0, frmsiz = 207 (frame_size = (207 + 1) * 2 = 416 bytes) -> 0x00, 0xCF
    // Byte 4: fscod = 0 (48kHz), numblkscod = 3 (6 blocks), acmod = 2 (stereo -> 2 << 1 = 0x04) -> 0x34
    // Byte 5: bsid = 16 (0x10 << 3 = 0x80)
    std::vector<unsigned char> eac3Frame = {
        0x0B, 0x77, 0x00, 0xCF, 0x34, 0x80, 0x00, 0x00
    };
    assert(NativeAudioDecoder::isEac3(eac3Frame.data(), eac3Frame.size()));
    assert(!NativeAudioDecoder::isAc3(eac3Frame.data(), eac3Frame.size()));
    assert(AACDecoderWrapper::checkForValidAc3Frames(eac3Frame));

    Eac3Metadata eac3Meta;
    assert(AACDecoderWrapper::parseEac3Metadata(eac3Frame.data(), eac3Frame.size(), eac3Meta));
    assert(eac3Meta.sampleRate == 48000);
    assert(eac3Meta.channels == 2);
    assert(eac3Meta.bsid == 16);
    assert(eac3Meta.frameSize == 416);
    assert(eac3Meta.numBlocks == 6);

    // Frame with 10 bytes prefix offset
    std::vector<unsigned char> offsetAc3(10, 0xAA);
    offsetAc3.insert(offsetAc3.end(), ac3Frame.begin(), ac3Frame.end());
    assert(AACDecoderWrapper::checkForValidAc3Frames(offsetAc3));
    assert(NativeAudioDecoder::findAc3Syncword(offsetAc3.data(), offsetAc3.size()) == 10);
    Ac3Metadata offsetMeta;
    assert(AACDecoderWrapper::parseAc3Metadata(offsetAc3.data(), offsetAc3.size(), offsetMeta));
    assert(offsetMeta.sampleRate == 48000);
    assert(offsetMeta.channels == 2);

    // Decoder initialization for AC3 and EAC3
    AACDecoderWrapper ac3Decoder(DetectedType::BUFFER_AC3);
    bool initAc3Ok = ac3Decoder.initializeDecoder(48000, 2);
#if defined(__APPLE__)
    assert(initAc3Ok);
#endif

    bool ac3CallbackFired = false;
    ac3Decoder.setTrackChangeCallback([&](const AudioMetadata &meta) {
        ac3CallbackFired = true;
        assert(meta.type == DetectedType::BUFFER_AC3);
        assert(meta.ac3Metadata.sampleRate == 48000);
        assert(meta.ac3Metadata.channels == 2);
    });

    AACDecoderWrapper eac3Decoder(DetectedType::BUFFER_EAC3);
    bool initEac3Ok = eac3Decoder.initializeDecoder(48000, 2);
#if defined(__APPLE__)
    assert(initEac3Ok);
#endif

    bool eac3CallbackFired = false;
    eac3Decoder.setTrackChangeCallback([&](const AudioMetadata &meta) {
        eac3CallbackFired = true;
        assert(meta.type == DetectedType::BUFFER_EAC3);
        assert(meta.eac3Metadata.sampleRate == 48000);
        assert(meta.eac3Metadata.channels == 2);
    });

    // Empty buffer decode returns NoError
    std::vector<unsigned char> emptyBuf;
    int sampleRate = 0;
    int channels = 0;
    auto [decoded, err] = ac3Decoder.decode(emptyBuf, &sampleRate, &channels);
    assert(err == DecoderError::NoError);
    assert(decoded.empty());
    assert(!ac3CallbackFired);

    // Decode AC3 frame triggers metadata callback
    ac3Decoder.decode(ac3Frame, &sampleRate, &channels);
    assert(ac3CallbackFired);

    // Decode EAC3 frame triggers metadata callback
    eac3Decoder.decode(eac3Frame, &sampleRate, &channels);
    assert(eac3CallbackFired);

    ac3Decoder.setDataEnded();
    eac3Decoder.setDataEnded();

    std::cout << "  Passed!" << std::endl;
}

int main() {
    std::cout << "Running Native Decoder C++ Tests..." << std::endl;
    testHeaderSniffing();
    testInterleavedToPlanar();
    testAacStreamDecoder();
    testAc3StreamDecoder();
    std::cout << "All Native Decoder C++ Tests passed successfully!" << std::endl;
    return 0;
}
