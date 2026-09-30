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

    // Set client format to 32-bit floating point interleaved PCM
    AudioStreamBasicDescription clientFormat;
    std::memset(&clientFormat, 0, sizeof(clientFormat));
    clientFormat.mSampleRate = sampleRate;
    clientFormat.mFormatID = kAudioFormatLinearPCM;
    clientFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    clientFormat.mBitsPerChannel = 32;
    clientFormat.mChannelsPerFrame = channels;
    clientFormat.mFramesPerPacket = 1;
    clientFormat.mBytesPerFrame = 4 * channels;
    clientFormat.mBytesPerPacket = 4 * channels;

    status = ExtAudioFileSetProperty(extAudioFile, kExtAudioFileProperty_ClientDataFormat, sizeof(clientFormat), &clientFormat);
    if (status != noErr) {
        result.errorMessage = "Failed to set client format, OSStatus: " + std::to_string(status);
        return result;
    }

    // Try to get total frame count hint
    SInt64 totalFrames = 0;
    propSize = sizeof(totalFrames);
    ExtAudioFileGetProperty(extAudioFile, kExtAudioFileProperty_FileLengthFrames, &propSize, &totalFrames);

    std::vector<float> interleavedBuffer;
    if (totalFrames > 0 && totalFrames < 500000000) { // Safety bound (~3 hours of 44.1kHz audio)
        interleavedBuffer.reserve(static_cast<size_t>(totalFrames * channels));
    }

    const UInt32 kChunkFrames = 4096;
    std::vector<float> chunk(kChunkFrames * channels);

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
            result.errorMessage = "ExtAudioFileRead failed with OSStatus: " + std::to_string(status);
            return result;
        }

        if (numFrames == 0) {
            break; // Reached end of file
        }

        interleavedBuffer.insert(interleavedBuffer.end(), chunk.begin(), chunk.begin() + (numFrames * channels));
        framesDecoded += numFrames;
    }

    if (framesDecoded == 0) {
        result.errorMessage = "No audio frames decoded";
        return result;
    }

    // Allocate planar buffer for SoLoud
    float *planarBuffer = new (std::nothrow) float[framesDecoded * channels];
    if (!planarBuffer) {
        result.errorMessage = "Out of memory allocating planar audio buffer";
        return result;
    }

    NativeAudioDecoder::interleavedToPlanar(interleavedBuffer.data(), planarBuffer, framesDecoded, channels);

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
