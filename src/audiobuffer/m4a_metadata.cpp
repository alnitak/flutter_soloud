#include "m4a_metadata.h"
#include <cstring>
#include <vector>

static inline uint32_t readBe32(const unsigned char *p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

static inline uint16_t readBe16(const unsigned char *p) {
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

// Standard ID3v1 genre table for numeric genres (used in 'gnre' box)
static const char *const kId3v1Genres[] = {
    "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge",
    "Hip-Hop", "Jazz", "Metal", "New Age", "Oldies", "Other", "Pop", "R&B",
    "Rap", "Reggae", "Rock", "Techno", "Industrial", "Alternative", "Ska",
    "Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop",
    "Vocal", "Jazz+Funk", "Fusion", "Trance", "Classical", "Instrumental",
    "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise", "AlternRock",
    "Bass", "Soul", "Punk", "Space", "Meditative", "Instrumental Pop",
    "Instrumental Rock", "Ethnic", "Gothic", "Darkwave", "Techno-Industrial",
    "Electronic", "Pop-Folk", "Eurodance", "Dream", "Southern Rock", "Comedy",
    "Cult", "Gangsta", "Top 40", "Christian Rap", "Pop/Funk", "Jungle",
    "Native American", "Cabaret", "New Wave", "Psychadelic", "Rave", "Showtunes",
    "Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz", "Polka", "Retro",
    "Musical", "Rock & Roll", "Hard Rock"
};
static const size_t kNumId3v1Genres = sizeof(kId3v1Genres) / sizeof(kId3v1Genres[0]);

// Helper to extract the payload of a 'data' child atom
static void parseIlstDataBox(const unsigned char *data, size_t size, std::string &outText,
                             uint16_t *outNum1 = nullptr, uint16_t *outNum2 = nullptr) {
    size_t offset = 0;
    while (offset + 8 <= size) {
        uint32_t boxSize = readBe32(data + offset);
        if (boxSize < 8 || offset + boxSize > size) break;
        if (memcmp(data + offset + 4, "data", 4) == 0) {
            // 'data' box: 4 bytes size, 4 bytes 'data', 4 bytes type, 4 bytes locale, payload
            if (boxSize >= 16) {
                uint32_t type = readBe32(data + offset + 8);
                const unsigned char *payload = data + offset + 16;
                size_t payloadLen = boxSize - 16;

                if (type == 1) {
                    // UTF-8 string
                    outText = std::string(reinterpret_cast<const char *>(payload), payloadLen);
                } else if (type == 0 || type == 21) {
                    // Numeric / binary integer data
                    if (outNum1 && payloadLen >= 2) {
                        *outNum1 = readBe16(payload);
                    }
                    if (outNum2 && payloadLen >= 4) {
                        *outNum2 = readBe16(payload + 2);
                    }
                }
            }
            break;
        }
        offset += boxSize;
    }
}

// Parses items inside an 'ilst' atom
static void parseIlst(const unsigned char *data, size_t size, M4aMetadata &meta) {
    size_t offset = 0;
    while (offset + 8 <= size) {
        uint32_t boxSize = readBe32(data + offset);
        if (boxSize < 8 || offset + boxSize > size) break;

        const char *fourcc = reinterpret_cast<const char *>(data + offset + 4);
        const unsigned char *boxPayload = data + offset + 8;
        size_t boxPayloadSize = boxSize - 8;

        if (memcmp(fourcc, "\xa9" "nam", 4) == 0 || memcmp(fourcc, "titl", 4) == 0) {
            parseIlstDataBox(boxPayload, boxPayloadSize, meta.title);
        } else if (memcmp(fourcc, "\xa9" "ART", 4) == 0 || memcmp(fourcc, "perf", 4) == 0) {
            parseIlstDataBox(boxPayload, boxPayloadSize, meta.artist);
        } else if (memcmp(fourcc, "aART", 4) == 0) {
            parseIlstDataBox(boxPayload, boxPayloadSize, meta.albumArtist);
        } else if (memcmp(fourcc, "\xa9" "alb", 4) == 0) {
            parseIlstDataBox(boxPayload, boxPayloadSize, meta.album);
        } else if (memcmp(fourcc, "\xa9" "day", 4) == 0) {
            parseIlstDataBox(boxPayload, boxPayloadSize, meta.date);
        } else if (memcmp(fourcc, "\xa9" "gen", 4) == 0) {
            parseIlstDataBox(boxPayload, boxPayloadSize, meta.genre);
        } else if (memcmp(fourcc, "gnre", 4) == 0) {
            if (meta.genre.empty()) {
                uint16_t genreId = 0;
                parseIlstDataBox(boxPayload, boxPayloadSize, meta.genre, &genreId);
                if (genreId > 0 && (size_t)(genreId - 1) < kNumId3v1Genres) {
                    meta.genre = kId3v1Genres[genreId - 1];
                }
            }
        } else if (memcmp(fourcc, "\xa9" "wrt", 4) == 0) {
            parseIlstDataBox(boxPayload, boxPayloadSize, meta.composer);
        } else if (memcmp(fourcc, "\xa9" "cmt", 4) == 0) {
            parseIlstDataBox(boxPayload, boxPayloadSize, meta.comment);
        } else if (memcmp(fourcc, "trkn", 4) == 0) {
            // trkn payload: 2 bytes reserved, 2 bytes track, 2 bytes total
            size_t sub = 0;
            while (sub + 8 <= boxPayloadSize) {
                uint32_t dSize = readBe32(boxPayload + sub);
                if (dSize < 8 || sub + dSize > boxPayloadSize) break;
                if (memcmp(boxPayload + sub + 4, "data", 4) == 0 && dSize >= 16 + 4) {
                    const unsigned char *d = boxPayload + sub + 16;
                    uint16_t track = readBe16(d + 2);
                    uint16_t total = (dSize >= 16 + 6) ? readBe16(d + 4) : 0;
                    if (track > 0) {
                        meta.track = std::to_string(track);
                        if (total > 0) meta.track += "/" + std::to_string(total);
                    }
                    break;
                }
                sub += dSize;
            }
        } else if (memcmp(fourcc, "disk", 4) == 0) {
            // disk payload: 2 bytes reserved, 2 bytes disc, 2 bytes total
            size_t sub = 0;
            while (sub + 8 <= boxPayloadSize) {
                uint32_t dSize = readBe32(boxPayload + sub);
                if (dSize < 8 || sub + dSize > boxPayloadSize) break;
                if (memcmp(boxPayload + sub + 4, "data", 4) == 0 && dSize >= 16 + 4) {
                    const unsigned char *d = boxPayload + sub + 16;
                    uint16_t disc = readBe16(d + 2);
                    uint16_t total = (dSize >= 16 + 6) ? readBe16(d + 4) : 0;
                    if (disc > 0) {
                        meta.disc = std::to_string(disc);
                        if (total > 0) meta.disc += "/" + std::to_string(total);
                    }
                    break;
                }
                sub += dSize;
            }
        }

        offset += boxSize;
    }
}

// Parses audio sample entry inside 'stsd' box
static void parseStsdEntry(const unsigned char *data, size_t size, M4aMetadata &meta) {
    if (size < 28) return;
    char codecFourcc[5] = {0};
    memcpy(codecFourcc, data + 4, 4);
    meta.codec = codecFourcc;

    // Audio sample entry:
    // Offset 8..13: reserved (6 bytes)
    // Offset 14..15: data_reference_index (2 bytes)
    // Offset 16..23: reserved (8 bytes) / sound info
    // Offset 16 (entry payload offset 24): channel count (2 bytes)
    // Offset 24 (entry payload offset 32): sample rate (4 bytes fixed point 16.16)
    if (size >= 36) {
        meta.channels = readBe16(data + 24);
        meta.sampleRate = readBe16(data + 32); // Integer part of sample rate
    }

    // Inspect child boxes (e.g. 'esds' for bitrate)
    size_t subOffset = 36;
    while (subOffset + 8 <= size) {
        uint32_t subBoxSize = readBe32(data + subOffset);
        if (subBoxSize < 8 || subOffset + subBoxSize > size) break;
        if (memcmp(data + subOffset + 4, "esds", 4) == 0) {
            // elementary stream descriptor
            // Scan for DecoderConfigDescr (tag 0x04) which contains avgBitrate & maxBitrate
            for (size_t i = subOffset + 8; i + 13 <= subOffset + subBoxSize; ++i) {
                if (data[i] == 0x04) {
                    // DecoderConfigDescr tag
                    // Skip tag (1), length (1-4), objectTypeIndication (1), streamType (1), bufferSizeDB (3)
                    size_t descOffset = i + 1;
                    if (descOffset < subOffset + subBoxSize && (data[descOffset] & 0x80)) {
                        while (descOffset < subOffset + subBoxSize && (data[descOffset] & 0x80)) descOffset++;
                        descOffset++;
                    } else {
                        descOffset++;
                    }
                    if (descOffset + 10 <= subOffset + subBoxSize) {
                        uint32_t avgBitrate = readBe32(data + descOffset + 6);
                        if (avgBitrate > 0) {
                            meta.bitrate = avgBitrate;
                        }
                    }
                    break;
                }
            }
        }
        subOffset += subBoxSize;
    }
}

// Recursive box scanner
static void scanBoxes(const unsigned char *data, size_t size, M4aMetadata &meta) {
    size_t offset = 0;
    while (offset + 8 <= size) {
        uint32_t boxSize = readBe32(data + offset);
        size_t headerSize = 8;
        if (boxSize == 1) {
            // 64-bit size
            if (offset + 16 > size) break;
            uint64_t largeSize = (static_cast<uint64_t>(readBe32(data + offset + 8)) << 32) |
                                  readBe32(data + offset + 12);
            if (largeSize < 16 || offset + largeSize > size) break;
            boxSize = static_cast<uint32_t>(largeSize);
            headerSize = 16;
        } else if (boxSize == 0) {
            boxSize = size - offset;
        }

        if (boxSize < headerSize || offset + boxSize > size) break;

        const char *fourcc = reinterpret_cast<const char *>(data + offset + 4);
        const unsigned char *payload = data + offset + headerSize;
        size_t payloadSize = boxSize - headerSize;

        if (memcmp(fourcc, "moov", 4) == 0 ||
            memcmp(fourcc, "udta", 4) == 0 ||
            memcmp(fourcc, "trak", 4) == 0 ||
            memcmp(fourcc, "mdia", 4) == 0 ||
            memcmp(fourcc, "minf", 4) == 0 ||
            memcmp(fourcc, "stbl", 4) == 0) {
            // Recurse into container boxes
            scanBoxes(payload, payloadSize, meta);
        } else if (memcmp(fourcc, "meta", 4) == 0) {
            // QuickTime/MP4 'meta' is a FullBox (has 4 bytes flags/version: 0x00000000)
            size_t metaSkip = 0;
            if (payloadSize >= 4 && readBe32(payload) == 0) {
                metaSkip = 4;
            }
            if (payloadSize > metaSkip) {
                scanBoxes(payload + metaSkip, payloadSize - metaSkip, meta);
            }
        } else if (memcmp(fourcc, "ilst", 4) == 0) {
            parseIlst(payload, payloadSize, meta);
        } else if (memcmp(fourcc, "stsd", 4) == 0) {
            // 'stsd' has 4 bytes flags/version + 4 bytes entry count
            if (payloadSize >= 8) {
                uint32_t entryCount = readBe32(payload + 4);
                size_t entryOffset = 8;
                for (uint32_t i = 0; i < entryCount && entryOffset + 8 <= payloadSize; ++i) {
                    uint32_t entrySize = readBe32(payload + entryOffset);
                    if (entrySize < 8 || entryOffset + entrySize > payloadSize) break;
                    parseStsdEntry(payload + entryOffset, entrySize, meta);
                    entryOffset += entrySize;
                }
            }
        }

        offset += boxSize;
    }
}

bool parseM4aMetadata(const unsigned char *data, size_t size, M4aMetadata &outMeta) {
    if (!data || size < 8) return false;
    scanBoxes(data, size, outMeta);
    return !outMeta.title.empty() || !outMeta.artist.empty() ||
           !outMeta.album.empty() || outMeta.sampleRate > 0;
}
