// Streaming M4A/MP4 decoding on Apple platforms through ExtAudioFile, which
// reads the file incrementally, seeks frame-accurately and, for AAC, applies
// the container's encoder delay and padding itself. For codecs where it
// reports no priming (AC-3, E-AC-3) the container's edit list is applied
// here, as the Android backend does.
//
// A pinned file descriptor is read through AudioFile callbacks with pread(),
// so several voices can decode the same descriptor at once.

#include "native_stream_decoder.h"

#if defined(__APPLE__)

#include "m4a_gapless.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr unsigned int kMaxChannels = 8; // SoLoud's MAX_CHANNELS

class AppleStreamDecoder : public NativeStreamDecoder {
public:
    ~AppleStreamDecoder() override {
        if (mFile) ExtAudioFileDispose(mFile);
        // A wrapped AudioFileID is not closed by ExtAudioFileDispose.
        if (mAudioFile) AudioFileClose(mAudioFile);
    }

    bool open(const NativeStreamFile &file, std::string &error) {
        const std::string &path = file.path;
        OSStatus status = noErr;
        if (file.fd >= 0) {
            struct stat st;
            if (fstat(file.fd, &st) != 0 || st.st_size <= 0) {
                error = "Cannot stat file: " + path;
                return false;
            }
            mFd = file.fd;
            mFileSize = static_cast<SInt64>(st.st_size);
            status = AudioFileOpenWithCallbacks(this, readProc, nullptr, sizeProc, nullptr, 0, &mAudioFile);
            if (status != noErr || !mAudioFile) {
                mAudioFile = nullptr;
                error = "AudioFileOpenWithCallbacks failed with OSStatus: " + std::to_string(status);
                return false;
            }
            status = ExtAudioFileWrapAudioFileID(mAudioFile, false, &mFile);
            if (status != noErr || !mFile) {
                mFile = nullptr;
                error = "ExtAudioFileWrapAudioFileID failed with OSStatus: " + std::to_string(status);
                return false;
            }
        } else {
            CFURLRef url = CFURLCreateFromFileSystemRepresentation(
                kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(path.c_str()),
                static_cast<CFIndex>(path.size()), false);
            if (!url) {
                error = "Failed to create CFURL for path: " + path;
                return false;
            }
            status = ExtAudioFileOpenURL(url, &mFile);
            CFRelease(url);
            if (status != noErr || !mFile) {
                mFile = nullptr;
                error = "ExtAudioFileOpenURL failed with OSStatus: " + std::to_string(status);
                return false;
            }
        }

        AudioStreamBasicDescription fileFormat{};
        UInt32 size = sizeof(fileFormat);
        status = ExtAudioFileGetProperty(mFile, kExtAudioFileProperty_FileDataFormat, &size, &fileFormat);
        if (status != noErr) {
            error = "Failed to get file data format, OSStatus: " + std::to_string(status);
            return false;
        }
        // Same output format as the full decode in os_decoder_apple.mm.
        const unsigned int channels = fileFormat.mChannelsPerFrame > 0 ? fileFormat.mChannelsPerFrame : 2;
        const Float64 sampleRate = fileFormat.mSampleRate > 0 ? fileFormat.mSampleRate : 44100.0;
        if (channels > kMaxChannels) {
            error = "Unsupported channel count: " + std::to_string(channels);
            return false;
        }

        AudioStreamBasicDescription client{};
        client.mSampleRate = sampleRate;
        client.mFormatID = kAudioFormatLinearPCM;
        client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        client.mBitsPerChannel = 32;
        client.mChannelsPerFrame = channels;
        client.mFramesPerPacket = 1;
        client.mBytesPerFrame = 4 * channels;
        client.mBytesPerPacket = 4 * channels;
        status = ExtAudioFileSetProperty(mFile, kExtAudioFileProperty_ClientDataFormat, sizeof(client), &client);
        if (status != noErr) {
            error = "Failed to set client format, OSStatus: " + std::to_string(status);
            return false;
        }

        // In client frames, without priming and remainder frames.
        SInt64 totalFrames = 0;
        size = sizeof(totalFrames);
        status = ExtAudioFileGetProperty(mFile, kExtAudioFileProperty_FileLengthFrames, &size, &totalFrames);
        if (status != noErr || totalFrames <= 0) {
            error = "Failed to get the file length, OSStatus: " + std::to_string(status);
            return false;
        }

        mFormat.channels = channels;
        mFormat.sampleRate = static_cast<float>(sampleRate);
        mFormat.totalFrames = static_cast<uint64_t>(totalFrames);

        if (!hasPrimingInfo()) {
            M4aGaplessInfo gapless;
            uint64_t delay = 0;
            uint64_t valid = 0;
            const bool parsed = mFd >= 0 ? parseM4aGaplessFd(mFd, gapless) : parseM4aGapless(path, gapless);
            if (parsed && gapless.resolve(sampleRate, delay, valid) && delay > 0 &&
                delay < mFormat.totalFrames) {
                mOffset = delay;
                mFormat.totalFrames = std::min<uint64_t>(valid > 0 ? valid : UINT64_MAX, mFormat.totalFrames - delay);
                if (ExtAudioFileSeek(mFile, static_cast<SInt64>(mOffset)) != noErr) {
                    error = "Failed to skip the encoder delay";
                    return false;
                }
            }
        }
        return true;
    }

    const NativeStreamFormat &format() const override { return mFormat; }

    bool seek(uint64_t frame) override {
        if (frame > mFormat.totalFrames) frame = mFormat.totalFrames;
        if (ExtAudioFileSeek(mFile, static_cast<SInt64>(frame + mOffset)) != noErr) return false;
        mPosition = frame;
        return true;
    }

    long read(float *out, unsigned int maxFrames) override {
        if (mPosition >= mFormat.totalFrames) return 0;
        if (maxFrames > mFormat.totalFrames - mPosition) {
            maxFrames = static_cast<unsigned int>(mFormat.totalFrames - mPosition);
        }
        AudioBufferList list;
        list.mNumberBuffers = 1;
        list.mBuffers[0].mNumberChannels = mFormat.channels;
        list.mBuffers[0].mDataByteSize = maxFrames * mFormat.channels * sizeof(float);
        list.mBuffers[0].mData = out;
        UInt32 frames = maxFrames;
        if (ExtAudioFileRead(mFile, &frames, &list) != noErr) return -1;
        mPosition += frames;
        return static_cast<long>(frames);
    }

private:
    static OSStatus readProc(void *client, SInt64 position, UInt32 requestCount, void *buffer, UInt32 *actualCount) {
        auto *self = static_cast<AppleStreamDecoder *>(client);
        UInt32 done = 0;
        while (done < requestCount && position + done < self->mFileSize) {
            const ssize_t n = pread(self->mFd, static_cast<char *>(buffer) + done, requestCount - done,
                                    static_cast<off_t>(position + done));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) {
                *actualCount = done;
                return kAudioFileUnspecifiedError;
            }
            if (n == 0) break;
            done += static_cast<UInt32>(n);
        }
        *actualCount = done;
        return noErr;
    }

    static SInt64 sizeProc(void *client) { return static_cast<AppleStreamDecoder *>(client)->mFileSize; }

    /// Whether the file declares priming or remainder frames, which
    /// ExtAudioFile then trims by itself.
    bool hasPrimingInfo() const {
        AudioFileID audioFile = nullptr;
        UInt32 size = sizeof(audioFile);
        if (ExtAudioFileGetProperty(mFile, kExtAudioFileProperty_AudioFile, &size, &audioFile) != noErr ||
            !audioFile) {
            return false;
        }
        AudioFilePacketTableInfo info{};
        size = sizeof(info);
        if (AudioFileGetProperty(audioFile, kAudioFilePropertyPacketTableInfo, &size, &info) != noErr) return false;
        return info.mPrimingFrames > 0 || info.mRemainderFrames > 0;
    }

    ExtAudioFileRef mFile = nullptr;
    AudioFileID mAudioFile = nullptr; // set when reading a pinned descriptor
    int mFd = -1;                     // pinned descriptor, not owned
    SInt64 mFileSize = 0;
    NativeStreamFormat mFormat;
    uint64_t mOffset = 0; // container encoder delay not trimmed by ExtAudioFile
    uint64_t mPosition = 0;
};

} // namespace

std::unique_ptr<NativeStreamDecoder> openPlatformNativeStreamDecoder(const NativeStreamFile &file,
                                                                     std::string &error) {
    auto decoder = std::make_unique<AppleStreamDecoder>();
    if (!decoder->open(file, error)) return nullptr;
    return decoder;
}

#endif // defined(__APPLE__)
