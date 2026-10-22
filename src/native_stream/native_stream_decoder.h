#ifndef NATIVE_STREAM_DECODER_H
#define NATIVE_STREAM_DECODER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

/// Platforms with a streaming M4A/MP4 decoder backend. Elsewhere
/// `LoadMode.disk` keeps decoding these formats in full.
#if (defined(__APPLE__) || defined(__ANDROID__)) && !defined(__EMSCRIPTEN__)
#define NATIVE_STREAM_SUPPORTED 1
#else
#define NATIVE_STREAM_SUPPORTED 0
#endif

/// Output format of a [NativeStreamDecoder].
struct NativeStreamFormat {
    unsigned int channels = 0;
    float sampleRate = 0.0f;
    /// Playable frames, with encoder delay and padding already excluded.
    uint64_t totalFrames = 0;
};

/// An incremental, seekable decoder producing interleaved float PCM. Frame 0
/// is the first playable frame: encoder delay (priming) is skipped and the
/// stream ends after [NativeStreamFormat::totalFrames].
///
/// Calls block on file I/O and codec work, so they must never run on the
/// audio thread or under the engine's lifecycle locks. An instance is used
/// by one thread at a time.
class NativeStreamDecoder {
public:
    virtual ~NativeStreamDecoder() = default;

    virtual const NativeStreamFormat &format() const = 0;

    /// Positions the decoder so the next [read] starts exactly at [frame].
    virtual bool seek(uint64_t frame) = 0;

    /// Decodes up to [maxFrames] frames into [out] (interleaved, [format]
    /// channels). Returns the number of frames written, 0 at the end of the
    /// stream, or -1 on a decoder error.
    virtual long read(float *out, unsigned int maxFrames) = 0;
};

/// The file a decoder reads.
struct NativeStreamFile {
    std::string path;
    /// When >= 0, a descriptor of the file opened when the source loaded,
    /// which decoders read instead of [path]: it keeps that exact file
    /// readable even if [path] is deleted or replaced. Several decoders read
    /// it at once, so they must not use (or move) its file offset. Owned by
    /// the caller and valid for the decoder's lifetime.
    int fd = -1;
};

using NativeStreamDecoderFactory =
    std::function<std::unique_ptr<NativeStreamDecoder>(const NativeStreamFile &file, std::string &error)>;

/// Opens [file] with this platform's streaming backend (ExtAudioFile on
/// Apple, MediaExtractor + MediaCodec on Android). Returns nullptr with
/// [error] set when the file can't be streamed, or on platforms without a
/// backend.
std::unique_ptr<NativeStreamDecoder> openPlatformNativeStreamDecoder(const NativeStreamFile &file,
                                                                     std::string &error);

/// Same as above for a file by path alone.
inline std::unique_ptr<NativeStreamDecoder> openPlatformNativeStreamDecoder(const std::string &path,
                                                                            std::string &error) {
    return openPlatformNativeStreamDecoder(NativeStreamFile{path, -1}, error);
}

#endif // NATIVE_STREAM_DECODER_H
