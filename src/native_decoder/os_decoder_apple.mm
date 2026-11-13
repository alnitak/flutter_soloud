#if defined(__APPLE__)

#include "native_audio_decoder.h"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <vector>
#include <cmath>

namespace {

struct MemoryAudioSource {
    const uint8_t *data;
    SInt64 size;
};

OSStatus memAudioReadProc(void *inClientData, SInt64 inPosition, UInt32 inRequestCount, void *buffer, UInt32 *actualCount) {
    MemoryAudioSource *source = static_cast<MemoryAudioSource *>(inClientData);
    if (!source || inPosition < 0 || inPosition > source->size) {
        *actualCount = 0;
        return kAudioFileEndOfFileError;
    }
    SInt64 available = source->size - inPosition;
    UInt32 toCopy = (available < inRequestCount) ? static_cast<UInt32>(available) : inRequestCount;
    if (toCopy > 0) {
        std::memcpy(buffer, source->data + inPosition, toCopy);
    }
    *actualCount = toCopy;
    return noErr;
}

SInt64 memAudioGetSizeProc(void *inClientData) {
    MemoryAudioSource *source = static_cast<MemoryAudioSource *>(inClientData);
    return source ? source->size : 0;
}

DecodedAudioData decodeExtAudioFile(ExtAudioFileRef extAudioFile) {
    DecodedAudioData result;
    if (!extAudioFile) {
        result.errorMessage = "Invalid ExtAudioFileRef";
        return result;
    }

    AudioStreamBasicDescription fileFormat;
    UInt32 propSize = sizeof(fileFormat);
    OSStatus status = ExtAudioFileGetProperty(extAudioFile, kExtAudioFileProperty_FileDataFormat, &propSize, &fileFormat);
    if (status != noErr) {
        result.errorMessage = "Failed to get file data format, OSStatus: " + std::to_string(status);
        return result;
    }

    UInt32 channels = fileFormat.mChannelsPerFrame;
    if (channels == 0) channels = 2;
    Float64 sampleRate = fileFormat.mSampleRate;
    if (sampleRate <= 0) sampleRate = 44100.0;

    // Configure output format: 32-bit float, packed interleaved (matching
    // AppleStreamDecoder's client format across AAC, AC-3, E-AC-3, and ALAC),
    // then SIMD-deinterleave each L1-hot 4096-frame chunk directly into a
    // single planar output allocation (halving peak memory vs staging buffer).
    AudioStreamBasicDescription clientFormat;
    std::memset(&clientFormat, 0, sizeof(clientFormat));
    clientFormat.mSampleRate = sampleRate;
    clientFormat.mFormatID = kAudioFormatLinearPCM;
    clientFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    clientFormat.mBitsPerChannel = 32;
    clientFormat.mChannelsPerFrame = channels;
    clientFormat.mFramesPerPacket = 1;
    clientFormat.mBytesPerFrame = sizeof(float) * channels;
    clientFormat.mBytesPerPacket = sizeof(float) * channels;

    status = ExtAudioFileSetProperty(
        extAudioFile, kExtAudioFileProperty_ClientDataFormat, sizeof(clientFormat), &clientFormat);
    if (status != noErr) {
        result.errorMessage = "Failed to set client format, OSStatus: " + std::to_string(status);
        return result;
    }

    SInt64 totalFrames = 0;
    propSize = sizeof(totalFrames);
    ExtAudioFileGetProperty(extAudioFile, kExtAudioFileProperty_FileLengthFrames, &propSize, &totalFrames);

    const UInt32 kChunkFrames = 4096;
    std::vector<float> chunk(static_cast<size_t>(kChunkFrames) * channels);

    size_t capacity = (totalFrames > 0 && totalFrames < 500000000)
                          ? static_cast<size_t>(totalFrames)
                          : static_cast<size_t>(kChunkFrames) * 4;
    float *planarBuffer = new (std::nothrow) float[capacity * channels];
    if (!planarBuffer) {
        capacity = static_cast<size_t>(kChunkFrames);
        planarBuffer = new (std::nothrow) float[capacity * channels];
        if (!planarBuffer) {
            result.errorMessage = "Out of memory allocating planar audio buffer";
            return result;
        }
    }

    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 1;
    bufferList.mBuffers[0].mNumberChannels = channels;

    size_t framesDecoded = 0;
    while (true) {
        bufferList.mBuffers[0].mDataByteSize = kChunkFrames * channels * sizeof(float);
        bufferList.mBuffers[0].mData = chunk.data();

        UInt32 numFrames = kChunkFrames;
        status = ExtAudioFileRead(extAudioFile, &numFrames, &bufferList);
        if (status != noErr) {
            delete[] planarBuffer;
            result.errorMessage = "ExtAudioFileRead failed with OSStatus: " + std::to_string(status);
            return result;
        }

        if (numFrames == 0) {
            break; // Reached end of file
        }

        if (framesDecoded + numFrames > capacity) {
            size_t newCap = capacity + (capacity >> 1);
            if (newCap < framesDecoded + numFrames) {
                newCap = framesDecoded + numFrames + kChunkFrames;
            }
            float *grown = new (std::nothrow) float[newCap * channels];
            if (!grown) {
                delete[] planarBuffer;
                result.errorMessage = "Out of memory growing planar audio buffer";
                return result;
            }
            for (UInt32 c = 0; c < channels; ++c) {
                std::memcpy(grown + static_cast<size_t>(c) * newCap,
                            planarBuffer + static_cast<size_t>(c) * capacity,
                            framesDecoded * sizeof(float));
            }
            delete[] planarBuffer;
            planarBuffer = grown;
            capacity = newCap;
        }

        SoLoud::deinterleaveFloatsToPlanar(
            chunk.data(), planarBuffer + framesDecoded, capacity, numFrames, channels);
        framesDecoded += numFrames;
    }

    if (framesDecoded == 0) {
        delete[] planarBuffer;
        result.errorMessage = "No audio frames decoded";
        return result;
    }

    if (framesDecoded < capacity) {
        const size_t slack = capacity - framesDecoded;
        float *exact = (slack > framesDecoded / 8)
                           ? new (std::nothrow) float[framesDecoded * channels]
                           : nullptr;
        if (exact) {
            for (UInt32 c = 0; c < channels; ++c) {
                std::memcpy(exact + static_cast<size_t>(c) * framesDecoded,
                            planarBuffer + static_cast<size_t>(c) * capacity,
                            framesDecoded * sizeof(float));
            }
            delete[] planarBuffer;
            planarBuffer = exact;
        } else {
            for (UInt32 c = 1; c < channels; ++c) {
                std::memmove(planarBuffer + static_cast<size_t>(c) * framesDecoded,
                             planarBuffer + static_cast<size_t>(c) * capacity,
                             framesDecoded * sizeof(float));
            }
        }
    }

    result.samples = planarBuffer;
    result.sampleCount = framesDecoded;
    result.channels = channels;
    result.sampleRate = static_cast<float>(sampleRate);
    result.success = true;
    return result;
}

} // anonymous namespace

DecodedAudioData NativeAudioDecoder::decodeFile(const char *filePath) {
    DecodedAudioData result;
    if (!filePath || std::strlen(filePath) == 0) {
        result.errorMessage = "Empty file path";
        return result;
    }

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
                                                          reinterpret_cast<const UInt8 *>(filePath),
                                                          std::strlen(filePath), false);
    if (!url) {
        result.errorMessage = "Failed to create CFURL for path: " + std::string(filePath);
        return result;
    }

    ExtAudioFileRef extAudioFile = nullptr;
    OSStatus status = ExtAudioFileOpenURL(url, &extAudioFile);
    CFRelease(url);

    if (status != noErr || !extAudioFile) {
        result.errorMessage = "ExtAudioFileOpenURL failed with OSStatus: " + std::to_string(status);
        return result;
    }

    result = decodeExtAudioFile(extAudioFile);
    ExtAudioFileDispose(extAudioFile);
    return result;
}

DecodedAudioData NativeAudioDecoder::decodeMemory(const unsigned char *bytes, size_t length) {
    DecodedAudioData result;
    if (!bytes || length == 0) {
        result.errorMessage = "Empty memory buffer";
        return result;
    }

    MemoryAudioSource source;
    source.data = bytes;
    source.size = static_cast<SInt64>(length);

    AudioFileID audioFileID = nullptr;
    OSStatus status = AudioFileOpenWithCallbacks(&source,
                                                memAudioReadProc,
                                                nullptr, // inWriteFunc
                                                memAudioGetSizeProc,
                                                nullptr, // inSetSizeFunc
                                                0,       // inFileTypeHint (0 = auto-detect)
                                                &audioFileID);

    if (status != noErr || !audioFileID) {
        result.errorMessage = "AudioFileOpenWithCallbacks failed with OSStatus: " + std::to_string(status);
        return result;
    }

    ExtAudioFileRef extAudioFile = nullptr;
    status = ExtAudioFileWrapAudioFileID(audioFileID, false, &extAudioFile);
    if (status != noErr || !extAudioFile) {
        AudioFileClose(audioFileID);
        result.errorMessage = "ExtAudioFileWrapAudioFileID failed with OSStatus: " + std::to_string(status);
        return result;
    }

    result = decodeExtAudioFile(extAudioFile);

    ExtAudioFileDispose(extAudioFile);
    AudioFileClose(audioFileID);
    return result;
}

#endif // defined(__APPLE__)
