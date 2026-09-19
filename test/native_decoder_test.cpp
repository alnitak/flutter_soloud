#include "../src/native_decoder/native_audio_decoder.h"
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

int main() {
    std::cout << "Running Native Decoder C++ Tests..." << std::endl;
    testHeaderSniffing();
    testInterleavedToPlanar();
    std::cout << "All Native Decoder C++ Tests passed successfully!" << std::endl;
    return 0;
}
