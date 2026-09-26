#pragma once

#ifndef M4A_METADATA_H
#define M4A_METADATA_H

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

/// Metadata extracted from an M4A / MP4 container
struct M4aMetadata {
    std::string title;
    std::string artist;
    std::string albumArtist;
    std::string album;
    std::string date;
    std::string genre;
    std::string composer;
    std::string comment;
    std::string track;
    std::string disc;
    std::string codec;
    int sampleRate = 0;
    int channels = 0;
    int bitrate = 0;
};

/// Parses ISO Base Media File Format (MP4 / M4A / QuickTime) metadata atoms
/// from buffer. Returns true if metadata was found.
bool parseM4aMetadata(const unsigned char *data, size_t size, M4aMetadata &outMeta);

#endif // M4A_METADATA_H
