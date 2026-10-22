#ifndef M4A_GAPLESS_H
#define M4A_GAPLESS_H

#include <cstdint>
#include <cstdio>
#include <string>

/// Encoder delay and padding of the first audio track of an MP4/M4A file, as
/// declared by its edit list (`elst`, what ffmpeg and most muxers write) or by
/// the iTunes `iTunSMPB` tag. Decoders output the priming samples, so a
/// player has to drop [delay] frames at the start and stop after the valid
/// length to be gapless and sample-accurate.
struct M4aGaplessInfo {
    /// Whether an audio track (`hdlr` = `soun`) with an `mdhd` was found.
    bool hasAudioTrack = false;
    /// The audio track's media timescale (`mdhd`).
    uint32_t mediaTimescale = 0;
    /// The audio track's media duration in [mediaTimescale] ticks (`mdhd`).
    uint64_t mediaDuration = 0;
    /// The movie timescale (`mvhd`), used by the edit list's durations.
    uint32_t movieTimescale = 0;

    /// Whether the track has an edit list with a non-empty edit.
    bool hasEdit = false;
    /// First non-empty edit: where presentation starts, in media ticks.
    int64_t editMediaTime = 0;
    /// First non-empty edit: its duration, in movie ticks (0 = unknown).
    uint64_t editDuration = 0;

    /// Whether an `iTunSMPB` tag was found.
    bool hasSmpb = false;
    uint32_t smpbDelay = 0;
    uint32_t smpbPadding = 0;
    uint64_t smpbLength = 0;

    /// Resolves the delay and valid length in frames at [sampleRate] (the
    /// decoder's output rate). Prefers the edit list, then iTunSMPB. Returns
    /// false when the file declares neither; [delayFrames] is then 0 and
    /// [validFrames] the media duration (0 if unknown).
    bool resolve(double sampleRate, uint64_t &delayFrames, uint64_t &validFrames) const;
};

/// Parses [path]'s `moov` box. Reads only box headers until `moov`, then
/// `moov` itself (capped at 64 MiB). Returns false if the file is not an
/// MP4 container or has no audio track.
bool parseM4aGapless(const std::string &path, M4aGaplessInfo &out);

/// Same as [parseM4aGapless] for an already-open stdio file.
bool parseM4aGapless(FILE *file, M4aGaplessInfo &out);

#if !defined(_WIN32)
/// Same as [parseM4aGapless] for a file descriptor. Reads with pread(), so
/// the descriptor's file offset is neither used nor moved.
bool parseM4aGaplessFd(int fd, M4aGaplessInfo &out);
#endif

/// Parses an in-memory `moov` box payload (without its 8-byte header).
bool parseM4aGaplessMoov(const unsigned char *data, size_t size, M4aGaplessInfo &out);

#endif // M4A_GAPLESS_H
