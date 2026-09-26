#include "../src/native_decoder/native_audio_decoder.h"
#include "../src/audiobuffer/aac_stream_decoder.h"
#include "../src/audiobuffer/mp3_stream_decoder.h"
#include "../src/audiobuffer/m4a_metadata.h"
#include "../src/audiobuffer/metadata_ffi.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
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

    // Two valid chained ADTS frames
    std::vector<unsigned char> twoFramesBuf = validAdts;
    twoFramesBuf.insert(twoFramesBuf.end(), validAdts.begin(), validAdts.end());
    assert(AACDecoderWrapper::checkForValidFrames(twoFramesBuf));

    // Pseudo-ADTS syncword inside payload where next frame does NOT chain
    // (Simulates accidental 0xFF 0xF9 in MP3 stream)
    std::vector<unsigned char> pseudoAdtsBuf(100, 0x12);
    // Insert candidate ADTS at offset 10 with frameLength = 20
    pseudoAdtsBuf[10] = 0xFF; pseudoAdtsBuf[11] = 0xF1;
    pseudoAdtsBuf[12] = 0x50; pseudoAdtsBuf[13] = 0x80;
    pseudoAdtsBuf[14] = 0x02; pseudoAdtsBuf[15] = 0x9F; pseudoAdtsBuf[16] = 0xFC; // frameLength = 20
    // At offset 10 + 20 = 30, bytes are 0x12, not 0xFF!
    assert(!AACDecoderWrapper::checkForValidFrames(pseudoAdtsBuf));

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

static std::vector<unsigned char> makeBox(const char *type, const std::vector<unsigned char> &payload) {
    uint32_t size = 8 + payload.size();
    std::vector<unsigned char> box;
    box.push_back((size >> 24) & 0xFF);
    box.push_back((size >> 16) & 0xFF);
    box.push_back((size >> 8) & 0xFF);
    box.push_back(size & 0xFF);
    box.push_back(type[0]);
    box.push_back(type[1]);
    box.push_back(type[2]);
    box.push_back(type[3]);
    box.insert(box.end(), payload.begin(), payload.end());
    return box;
}

static std::vector<unsigned char> makeDataBox(uint32_t type, const std::vector<unsigned char> &payload) {
    std::vector<unsigned char> inner;
    inner.push_back((type >> 24) & 0xFF);
    inner.push_back((type >> 16) & 0xFF);
    inner.push_back((type >> 8) & 0xFF);
    inner.push_back(type & 0xFF);
    inner.push_back(0); inner.push_back(0); inner.push_back(0); inner.push_back(0); // locale
    inner.insert(inner.end(), payload.begin(), payload.end());
    return makeBox("data", inner);
}

static std::vector<unsigned char> makeTextTagBox(const char *fourcc, const std::string &text) {
    std::vector<unsigned char> textBytes(text.begin(), text.end());
    auto dataBox = makeDataBox(1, textBytes);
    return makeBox(fourcc, dataBox);
}

void testM4aMetadataAndOffsets() {
    std::cout << "[Test] M4A Metadata & Struct Offsets..." << std::endl;

    // Check offsets of AudioMetadataFFI
    std::cout << "  Offset mp3Metadata: " << offsetof(AudioMetadataFFI, mp3Metadata) << std::endl;
    std::cout << "  Offset oggMetadata: " << offsetof(AudioMetadataFFI, oggMetadata) << std::endl;
    std::cout << "  Offset aacMetadata: " << offsetof(AudioMetadataFFI, aacMetadata) << std::endl;
    std::cout << "  Offset ac3Metadata: " << offsetof(AudioMetadataFFI, ac3Metadata) << std::endl;
    std::cout << "  Offset eac3Metadata: " << offsetof(AudioMetadataFFI, eac3Metadata) << std::endl;
    std::cout << "  Offset m4aMetadata: " << offsetof(AudioMetadataFFI, m4aMetadata) << std::endl;
    std::cout << "  Sizeof AudioMetadataFFI: " << sizeof(AudioMetadataFFI) << std::endl;

    std::cout << "  Mp3: title=" << offsetof(Mp3MetadataFFI, title)
              << " artist=" << offsetof(Mp3MetadataFFI, artist)
              << " album_artist=" << offsetof(Mp3MetadataFFI, album_artist)
              << " album=" << offsetof(Mp3MetadataFFI, album)
              << " genre=" << offsetof(Mp3MetadataFFI, genre)
              << " date=" << offsetof(Mp3MetadataFFI, date)
              << " comment=" << offsetof(Mp3MetadataFFI, comment)
              << " track=" << offsetof(Mp3MetadataFFI, track)
              << " disc=" << offsetof(Mp3MetadataFFI, disc)
              << " composer=" << offsetof(Mp3MetadataFFI, composer)
              << " stream_url=" << offsetof(Mp3MetadataFFI, stream_url)
              << " sample_rate=" << offsetof(Mp3MetadataFFI, sample_rate)
              << " channels=" << offsetof(Mp3MetadataFFI, channels)
              << " bitrate=" << offsetof(Mp3MetadataFFI, bitrate) << std::endl;

    std::cout << "  Aac: title=" << offsetof(AacMetadataFFI, title)
              << " artist=" << offsetof(AacMetadataFFI, artist)
              << " album_artist=" << offsetof(AacMetadataFFI, album_artist)
              << " album=" << offsetof(AacMetadataFFI, album)
              << " date=" << offsetof(AacMetadataFFI, date)
              << " genre=" << offsetof(AacMetadataFFI, genre)
              << " composer=" << offsetof(AacMetadataFFI, composer)
              << " comment=" << offsetof(AacMetadataFFI, comment)
              << " track=" << offsetof(AacMetadataFFI, track)
              << " disc=" << offsetof(AacMetadataFFI, disc)
              << " stream_url=" << offsetof(AacMetadataFFI, stream_url)
              << " sample_rate=" << offsetof(AacMetadataFFI, sample_rate)
              << " channels=" << offsetof(AacMetadataFFI, channels)
              << " profile=" << offsetof(AacMetadataFFI, profile)
              << " bitrate=" << offsetof(AacMetadataFFI, bitrate)
              << " frame_length=" << offsetof(AacMetadataFFI, frame_length) << std::endl;

    std::cout << "  M4a: title=" << offsetof(M4aMetadataFFI, title)
              << " artist=" << offsetof(M4aMetadataFFI, artist)
              << " album_artist=" << offsetof(M4aMetadataFFI, album_artist)
              << " album=" << offsetof(M4aMetadataFFI, album)
              << " date=" << offsetof(M4aMetadataFFI, date)
              << " genre=" << offsetof(M4aMetadataFFI, genre)
              << " composer=" << offsetof(M4aMetadataFFI, composer)
              << " comment=" << offsetof(M4aMetadataFFI, comment)
              << " track=" << offsetof(M4aMetadataFFI, track)
              << " disc=" << offsetof(M4aMetadataFFI, disc)
              << " codec=" << offsetof(M4aMetadataFFI, codec)
              << " sample_rate=" << offsetof(M4aMetadataFFI, sample_rate)
              << " channels=" << offsetof(M4aMetadataFFI, channels)
              << " bitrate=" << offsetof(M4aMetadataFFI, bitrate) << std::endl;

    // Build synthetic ilst box
    std::vector<unsigned char> ilstPayload;
    auto titleBox = makeTextTagBox("\xa9nam", "Test Title");
    auto artistBox = makeTextTagBox("\xa9" "ART", "Test Artist");
    auto albumArtistBox = makeTextTagBox("aART", "Test Album Artist");
    auto albumBox = makeTextTagBox("\xa9" "alb", "Test Album");
    auto dateBox = makeTextTagBox("\xa9" "day", "2024");
    auto genreBox = makeTextTagBox("\xa9gen", "Electronic");
    auto composerBox = makeTextTagBox("\xa9wrt", "Test Composer");
    auto commentBox = makeTextTagBox("\xa9" "cmt", "Test Comment");

    // trkn data box
    std::vector<unsigned char> trknPayload = {0, 0, 0, 5, 0, 12, 0, 0}; // track 5 of 12
    auto trknBox = makeBox("trkn", makeDataBox(0, trknPayload));

    // disk data box
    std::vector<unsigned char> diskPayload = {0, 0, 0, 1, 0, 2}; // disc 1 of 2
    auto diskBox = makeBox("disk", makeDataBox(0, diskPayload));

    ilstPayload.insert(ilstPayload.end(), titleBox.begin(), titleBox.end());
    ilstPayload.insert(ilstPayload.end(), artistBox.begin(), artistBox.end());
    ilstPayload.insert(ilstPayload.end(), albumArtistBox.begin(), albumArtistBox.end());
    ilstPayload.insert(ilstPayload.end(), albumBox.begin(), albumBox.end());
    ilstPayload.insert(ilstPayload.end(), dateBox.begin(), dateBox.end());
    ilstPayload.insert(ilstPayload.end(), genreBox.begin(), genreBox.end());
    ilstPayload.insert(ilstPayload.end(), composerBox.begin(), composerBox.end());
    ilstPayload.insert(ilstPayload.end(), commentBox.begin(), commentBox.end());
    ilstPayload.insert(ilstPayload.end(), trknBox.begin(), trknBox.end());
    ilstPayload.insert(ilstPayload.end(), diskBox.begin(), diskBox.end());

    auto ilstBox = makeBox("ilst", ilstPayload);

    // meta box has 4 bytes flags/version (0)
    std::vector<unsigned char> metaPayload = {0, 0, 0, 0};
    metaPayload.insert(metaPayload.end(), ilstBox.begin(), ilstBox.end());
    auto metaBox = makeBox("meta", metaPayload);
    auto udtaBox = makeBox("udta", metaBox);

    // stsd box with mp4a audio entry: 2 channels, 44100 Hz
    // Box payload: 16 bytes (reserved + refIdx + sound info) + 2 bytes channels + 6 bytes + 4 bytes sampleRate
    std::vector<unsigned char> mp4aEntry(28, 0);
    mp4aEntry[17] = 2; // 2 channels at payload offset 17 (data offset 25)
    mp4aEntry[24] = 0xAC; mp4aEntry[25] = 0x44; // 44100 = 0xAC44 at payload offset 24..25 (data offset 32..33)
    auto mp4aBox = makeBox("mp4a", mp4aEntry);

    std::vector<unsigned char> stsdPayload = {0, 0, 0, 0, 0, 0, 0, 1}; // version/flags + 1 entry
    stsdPayload.insert(stsdPayload.end(), mp4aBox.begin(), mp4aBox.end());
    auto stsdBox = makeBox("stsd", stsdPayload);
    auto stblBox = makeBox("stbl", stsdBox);
    auto minfBox = makeBox("minf", stblBox);
    auto mdiaBox = makeBox("mdia", minfBox);
    auto trakBox = makeBox("trak", mdiaBox);

    std::vector<unsigned char> moovPayload;
    moovPayload.insert(moovPayload.end(), trakBox.begin(), trakBox.end());
    moovPayload.insert(moovPayload.end(), udtaBox.begin(), udtaBox.end());
    auto moovBox = makeBox("moov", moovPayload);

    // Root container with ftyp + moov
    std::vector<unsigned char> ftypPayload = {'M', '4', 'A', ' ', 0, 0, 0, 0};
    auto ftypBox = makeBox("ftyp", ftypPayload);

    std::vector<unsigned char> m4aFile;
    m4aFile.insert(m4aFile.end(), ftypBox.begin(), ftypBox.end());
    m4aFile.insert(m4aFile.end(), moovBox.begin(), moovBox.end());

    M4aMetadata parsedM4a;
    assert(parseM4aMetadata(m4aFile.data(), m4aFile.size(), parsedM4a));
    assert(parsedM4a.title == "Test Title");
    assert(parsedM4a.artist == "Test Artist");
    assert(parsedM4a.albumArtist == "Test Album Artist");
    assert(parsedM4a.album == "Test Album");
    assert(parsedM4a.date == "2024");
    assert(parsedM4a.genre == "Electronic");
    assert(parsedM4a.composer == "Test Composer");
    assert(parsedM4a.comment == "Test Comment");
    assert(parsedM4a.track == "5/12");
    assert(parsedM4a.disc == "1/2");
    assert(parsedM4a.codec == "mp4a");
    assert(parsedM4a.channels == 2);
    assert(parsedM4a.sampleRate == 44100);

    // Test AAC extended ID3 tags
    std::vector<unsigned char> extId3Tag = {
        'I', 'D', '3', 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 100,
        'T', 'P', 'E', '2', 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 'A', 'A', 'r', 't', 'i', 's', 't',
        'T', 'C', 'O', 'N', 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 'P', 'o', 'p',
        'T', 'C', 'O', 'M', 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 'C', 'o', 'm', 'p',
        'T', 'R', 'C', 'K', 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, '2', '/', '1', '0',
        'T', 'P', 'O', 'S', 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, '1', '/', '2'
    };
    AacMetadata extAacMeta;
    size_t id3Size = 0;
    assert(AACDecoderWrapper::parseId3Tags(extId3Tag.data(), extId3Tag.size(), extAacMeta, id3Size));
    assert(extAacMeta.albumArtist == "AArtist");
    assert(extAacMeta.genre == "Pop");
    assert(extAacMeta.composer == "Comp");
    assert(extAacMeta.track == "2/10");
    assert(extAacMeta.disc == "1/2");

    std::cout << "  Passed!" << std::endl;
}

void testMp3VsAacDetection() {
    std::cout << "[Test] MP3 vs AAC Stream Detection..." << std::endl;

    // 1. Read MP3 test stream
    std::ifstream mp3File("/tmp/test_stream.mp3", std::ios::binary);
    if (mp3File.is_open()) {
        std::vector<unsigned char> mp3Data((std::istreambuf_iterator<char>(mp3File)),
                                            std::istreambuf_iterator<char>());
        if (!mp3Data.empty()) {
            assert(MP3DecoderWrapper::checkForValidFrames(mp3Data));
            assert(!AACDecoderWrapper::checkForValidFrames(mp3Data));
        }
    }

    // 2. Read AAC test stream
    std::ifstream aacFile("/tmp/test_stream.aac", std::ios::binary);
    if (aacFile.is_open()) {
        std::vector<unsigned char> aacData((std::istreambuf_iterator<char>(aacFile)),
                                            std::istreambuf_iterator<char>());
        if (!aacData.empty()) {
            assert(AACDecoderWrapper::checkForValidFrames(aacData));
            assert(!MP3DecoderWrapper::checkForValidFrames(aacData));
        }
    }

    std::cout << "  Passed!" << std::endl;
}

void testMp3MetadataEmission() {
    std::cout << "[Test] MP3 Metadata Emission..." << std::endl;

    std::ifstream mp3File("/Volumes/NVME/Users/deimos/Music/tests/mp3.mp3", std::ios::binary);
    if (mp3File.is_open()) {
        std::vector<unsigned char> mp3Data((std::istreambuf_iterator<char>(mp3File)),
                                            std::istreambuf_iterator<char>());
        if (!mp3Data.empty()) {
            MP3DecoderWrapper decoder;
            int sr = 44100, ch = 2;
            assert(decoder.initializeDecoder(sr, ch));

            bool metadataReceived = false;
            decoder.setTrackChangeCallback([&](const AudioMetadata &meta) {
                metadataReceived = true;
                assert(meta.mp3Metadata.sampleRate == 44100);
                assert(meta.mp3Metadata.channels == 2);
                assert(meta.mp3Metadata.bitrate > 0);
                assert(meta.mp3Metadata.title == "Don't Leave Me This Way");
                assert(meta.mp3Metadata.artist == "Various Artist");
                assert(meta.mp3Metadata.album == "Top 100 - 80's");
            });

            // Feed a 32 KB chunk
            size_t chunk = std::min(mp3Data.size(), static_cast<size_t>(32768));
            std::vector<unsigned char> firstChunk(mp3Data.begin(), mp3Data.begin() + chunk);
            decoder.decode(firstChunk, &sr, &ch);

            assert(metadataReceived);
        }
    }

    std::cout << "  Passed!" << std::endl;
}

int main() {
    std::cout << "Running Native Decoder C++ Tests..." << std::endl;
    testHeaderSniffing();
    testInterleavedToPlanar();
    testAacStreamDecoder();
    testAc3StreamDecoder();
    testM4aMetadataAndOffsets();
    testMp3VsAacDetection();
    testMp3MetadataEmission();
    std::cout << "All Native Decoder C++ Tests passed successfully!" << std::endl;
    return 0;
}
