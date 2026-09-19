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

    // Truncated buffer (< 7 bytes)
    std::vector<unsigned char> shortBuf = {0xFF, 0xF1, 0x50};
    assert(!AACDecoderWrapper::checkForValidFrames(shortBuf));

    // Garbage buffer
    std::vector<unsigned char> garbageBuf = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    assert(!AACDecoderWrapper::checkForValidFrames(garbageBuf));

    // Test decoder instance lifecycle
    AACDecoderWrapper decoder;
    bool initOk = decoder.initializeDecoder(44100, 2);
#if defined(__APPLE__)
    assert(initOk);
#endif

    // Empty buffer decode should not crash and return NoError
    std::vector<unsigned char> emptyBuf;
    int sampleRate = 0;
    int channels = 0;
    auto [decoded, err] = decoder.decode(emptyBuf, &sampleRate, &channels);
    assert(err == DecoderError::NoError);
    assert(!decoder.hasPendingData());
    decoder.setDataEnded();
    assert(!decoder.hasPendingData());

    std::cout << "  Passed!" << std::endl;
}

void testAc3StreamDecoder() {
    std::cout << "[Test] AC-3 / E-AC-3 Stream Decoder..." << std::endl;

    // AC-3 header: 0x0B, 0x77, crc1 (2 bytes), fscod/frmsizecod, bsid <= 10
    // Byte 4: fscod = 0 (48kHz) -> 0x00
    // Byte 5: bsid = 8 (0x08 << 3 = 0x40)
    std::vector<unsigned char> ac3Frame = {
        0x0B, 0x77, 0x12, 0x34, 0x00, 0x40, 0x00, 0x00
    };
    assert(NativeAudioDecoder::isAc3(ac3Frame.data(), ac3Frame.size()));
    assert(!NativeAudioDecoder::isEac3(ac3Frame.data(), ac3Frame.size()));
    assert(AACDecoderWrapper::checkForValidAc3Frames(ac3Frame));
    assert(NativeAudioDecoder::findAc3Syncword(ac3Frame.data(), ac3Frame.size()) == 0);

    // E-AC-3 header: 0x0B, 0x77, strmtyp/substreamid/frmsiz (2 bytes), fscod, bsid = 16 (0x10 << 3 = 0x80)
    std::vector<unsigned char> eac3Frame = {
        0x0B, 0x77, 0x00, 0xCF, 0x00, 0x80, 0x00, 0x00
    };
    assert(NativeAudioDecoder::isEac3(eac3Frame.data(), eac3Frame.size()));
    assert(!NativeAudioDecoder::isAc3(eac3Frame.data(), eac3Frame.size()));
    assert(AACDecoderWrapper::checkForValidAc3Frames(eac3Frame));

    // Frame with 10 bytes prefix offset
    std::vector<unsigned char> offsetAc3(10, 0xAA);
    offsetAc3.insert(offsetAc3.end(), ac3Frame.begin(), ac3Frame.end());
    assert(AACDecoderWrapper::checkForValidAc3Frames(offsetAc3));
    assert(NativeAudioDecoder::findAc3Syncword(offsetAc3.data(), offsetAc3.size()) == 10);

    // Decoder initialization for AC3 and EAC3
    AACDecoderWrapper ac3Decoder(DetectedType::BUFFER_AC3);
    bool initAc3Ok = ac3Decoder.initializeDecoder(48000, 2);
#if defined(__APPLE__)
    assert(initAc3Ok);
#endif

    AACDecoderWrapper eac3Decoder(DetectedType::BUFFER_EAC3);
    bool initEac3Ok = eac3Decoder.initializeDecoder(48000, 2);
#if defined(__APPLE__)
    assert(initEac3Ok);
#endif

    // Empty buffer decode returns NoError
    std::vector<unsigned char> emptyBuf;
    int sampleRate = 0;
    int channels = 0;
    auto [decoded, err] = ac3Decoder.decode(emptyBuf, &sampleRate, &channels);
    assert(err == DecoderError::NoError);
    assert(decoded.empty());

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
