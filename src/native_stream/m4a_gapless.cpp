#include "m4a_gapless.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#define fseeko _fseeki64
#define ftello _ftelli64
using FileOffset = long long;
#else
using FileOffset = off_t;
#endif

namespace {

constexpr uint64_t kMaxMoovSize = 64ull << 20;

uint32_t be32(const unsigned char *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint64_t be64(const unsigned char *p) {
    return (uint64_t(be32(p)) << 32) | be32(p + 4);
}

/// One child box of a container payload.
struct Box {
    uint32_t type = 0;
    const unsigned char *data = nullptr; // payload
    size_t size = 0;                     // payload size
};

constexpr uint32_t fourcc(const char (&s)[5]) {
    return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16) |
           (uint32_t(uint8_t(s[2])) << 8) | uint32_t(uint8_t(s[3]));
}

/// Iterates the boxes of a container payload.
class BoxReader {
public:
    BoxReader(const unsigned char *data, size_t size) : mData(data), mSize(size) {}

    bool next(Box &box) {
        if (mPos + 8 > mSize) return false;
        const unsigned char *p = mData + mPos;
        uint64_t size = be32(p);
        size_t header = 8;
        if (size == 1) {
            if (mPos + 16 > mSize) return false;
            size = be64(p + 8);
            header = 16;
        } else if (size == 0) {
            size = mSize - mPos;
        }
        if (size < header || size > mSize - mPos) return false;
        box.type = be32(p + 4);
        box.data = p + header;
        box.size = static_cast<size_t>(size - header);
        mPos += static_cast<size_t>(size);
        return true;
    }

private:
    const unsigned char *mData;
    size_t mSize;
    size_t mPos = 0;
};

bool findChild(const unsigned char *data, size_t size, uint32_t type, Box &out) {
    BoxReader reader(data, size);
    Box box;
    while (reader.next(box)) {
        if (box.type == type) {
            out = box;
            return true;
        }
    }
    return false;
}

/// Track-level facts gathered from one `trak`.
struct TrackInfo {
    bool isAudio = false;
    bool hasMdhd = false;
    uint32_t timescale = 0;
    uint64_t duration = 0;
    bool hasEdit = false;
    int64_t editMediaTime = 0;
    uint64_t editDuration = 0;
};

void parseElst(const Box &elst, TrackInfo &track) {
    if (elst.size < 8) return;
    const unsigned version = elst.data[0];
    const uint32_t count = be32(elst.data + 4);
    const size_t entrySize = version == 1 ? 20 : 12;
    const unsigned char *p = elst.data + 8;
    for (uint32_t i = 0; i < count && size_t(p - elst.data) + entrySize <= elst.size; ++i, p += entrySize) {
        uint64_t duration;
        int64_t mediaTime;
        if (version == 1) {
            duration = be64(p);
            mediaTime = static_cast<int64_t>(be64(p + 8));
        } else {
            duration = be32(p);
            mediaTime = static_cast<int32_t>(be32(p + 4));
        }
        // -1 marks an empty edit (a presentation offset), which plays
        // nothing from the media; the first edit with media is the one that
        // trims the encoder delay.
        if (mediaTime < 0) continue;
        track.hasEdit = true;
        track.editMediaTime = mediaTime;
        track.editDuration = duration;
        return;
    }
}

void parseTrak(const Box &trak, TrackInfo &track) {
    Box mdia;
    if (findChild(trak.data, trak.size, fourcc("mdia"), mdia)) {
        Box hdlr;
        // hdlr: version/flags (4), pre_defined (4), handler_type (4).
        if (findChild(mdia.data, mdia.size, fourcc("hdlr"), hdlr) && hdlr.size >= 12) {
            track.isAudio = be32(hdlr.data + 8) == fourcc("soun");
        }
        Box mdhd;
        if (findChild(mdia.data, mdia.size, fourcc("mdhd"), mdhd) && mdhd.size >= 4) {
            if (mdhd.data[0] == 1 && mdhd.size >= 32) {
                track.timescale = be32(mdhd.data + 20);
                track.duration = be64(mdhd.data + 24);
                track.hasMdhd = true;
            } else if (mdhd.data[0] == 0 && mdhd.size >= 20) {
                track.timescale = be32(mdhd.data + 12);
                track.duration = be32(mdhd.data + 16);
                track.hasMdhd = true;
            }
        }
    }
    Box edts;
    Box elst;
    if (findChild(trak.data, trak.size, fourcc("edts"), edts) &&
        findChild(edts.data, edts.size, fourcc("elst"), elst)) {
        parseElst(elst, track);
    }
}

/// Parses " 00000000 00000840 000001CA 00000000001E5636 ..." (hex fields:
/// reserved, delay, padding, original length).
bool parseSmpbText(const std::string &text, M4aGaplessInfo &out) {
    std::vector<unsigned long long> fields;
    const char *p = text.c_str();
    while (*p && fields.size() < 4) {
        while (*p == ' ') ++p;
        if (!*p) break;
        char *end = nullptr;
        unsigned long long value = std::strtoull(p, &end, 16);
        if (end == p) return false;
        fields.push_back(value);
        p = end;
    }
    if (fields.size() < 4) return false;
    out.hasSmpb = true;
    out.smpbDelay = static_cast<uint32_t>(fields[1]);
    out.smpbPadding = static_cast<uint32_t>(fields[2]);
    out.smpbLength = fields[3];
    return true;
}

/// Looks for the `----` freeform item named iTunSMPB in an `ilst` payload.
void parseIlst(const Box &ilst, M4aGaplessInfo &out) {
    BoxReader items(ilst.data, ilst.size);
    Box item;
    while (items.next(item)) {
        if (item.type != fourcc("----")) continue;
        BoxReader parts(item.data, item.size);
        Box part;
        bool isSmpb = false;
        std::string value;
        while (parts.next(part)) {
            // name/data: version/flags (4) [+ type/locale (4) for data].
            if (part.type == fourcc("name") && part.size >= 4) {
                isSmpb = std::string(reinterpret_cast<const char *>(part.data + 4), part.size - 4) == "iTunSMPB";
            } else if (part.type == fourcc("data") && part.size >= 8) {
                value.assign(reinterpret_cast<const char *>(part.data + 8), part.size - 8);
            }
        }
        if (isSmpb && parseSmpbText(value, out)) return;
    }
}

void parseUdta(const Box &udta, M4aGaplessInfo &out) {
    Box meta;
    if (!findChild(udta.data, udta.size, fourcc("meta"), meta)) return;
    // `meta` is a full box (4 bytes version/flags) in MP4, but QuickTime
    // writers omit them; detect by checking whether a child header fits.
    const unsigned char *data = meta.data;
    size_t size = meta.size;
    if (size >= 12 && be32(data) == 0) {
        data += 4;
        size -= 4;
    }
    Box ilst;
    if (findChild(data, size, fourcc("ilst"), ilst)) parseIlst(ilst, out);
}

} // namespace

bool parseM4aGaplessMoov(const unsigned char *data, size_t size, M4aGaplessInfo &out) {
    out = M4aGaplessInfo();
    BoxReader reader(data, size);
    Box box;
    while (reader.next(box)) {
        if (box.type == fourcc("mvhd") && box.size >= 4) {
            if (box.data[0] == 1 && box.size >= 24) {
                out.movieTimescale = be32(box.data + 20);
            } else if (box.size >= 16) {
                out.movieTimescale = be32(box.data + 12);
            }
        } else if (box.type == fourcc("trak") && !out.hasAudioTrack) {
            TrackInfo track;
            parseTrak(box, track);
            if (track.isAudio && track.hasMdhd && track.timescale > 0) {
                out.hasAudioTrack = true;
                out.mediaTimescale = track.timescale;
                out.mediaDuration = track.duration;
                out.hasEdit = track.hasEdit;
                out.editMediaTime = track.editMediaTime;
                out.editDuration = track.editDuration;
            }
        } else if (box.type == fourcc("udta")) {
            parseUdta(box, out);
        }
    }
    return out.hasAudioTrack;
}

bool parseM4aGapless(FILE *file, M4aGaplessInfo &out) {
    out = M4aGaplessInfo();
    if (!file) return false;
    if (fseeko(file, 0, SEEK_END) != 0) return false;
    const FileOffset end = ftello(file);
    if (end <= 0) return false;
    const uint64_t fileSize = static_cast<uint64_t>(end);
    uint64_t pos = 0;
    bool first = true;
    while (pos + 8 <= fileSize) {
        if (fseeko(file, static_cast<FileOffset>(pos), SEEK_SET) != 0) return false;
        unsigned char header[16];
        if (fread(header, 1, 8, file) != 8) return false;
        uint64_t size = be32(header);
        uint64_t headerSize = 8;
        const uint32_t type = be32(header + 4);
        if (first && type != fourcc("ftyp")) return false;
        first = false;
        if (size == 1) {
            if (fread(header + 8, 1, 8, file) != 8) return false;
            size = be64(header + 8);
            headerSize = 16;
        } else if (size == 0) {
            // Box extends to the end of the file.
            size = fileSize - pos;
        }
        // Also guarantees pos + size can't overflow and always advances.
        if (size < headerSize || size > fileSize - pos) return false;
        if (type == fourcc("moov")) {
            const uint64_t payload = size - headerSize;
            if (payload > kMaxMoovSize) return false;
            std::vector<unsigned char> moov(static_cast<size_t>(payload));
            if (!moov.empty() && fread(moov.data(), 1, moov.size(), file) != moov.size()) return false;
            return parseM4aGaplessMoov(moov.data(), moov.size(), out);
        }
        pos += size;
    }
    return false;
}

bool parseM4aGapless(const std::string &path, M4aGaplessInfo &out) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) {
        out = M4aGaplessInfo();
        return false;
    }
    const bool ok = parseM4aGapless(file, out);
    std::fclose(file);
    return ok;
}

bool M4aGaplessInfo::resolve(double sampleRate, uint64_t &delayFrames, uint64_t &validFrames) const {
    delayFrames = 0;
    validFrames = 0;
    if (!hasAudioTrack || sampleRate <= 0 || mediaTimescale == 0) return false;

    auto mediaToFrames = [&](uint64_t ticks) {
        return static_cast<uint64_t>(std::llround(static_cast<double>(ticks) * sampleRate / mediaTimescale));
    };
    const uint64_t mediaFrames = mediaToFrames(mediaDuration);

    if (hasEdit) {
        delayFrames = mediaToFrames(static_cast<uint64_t>(editMediaTime));
        if (editDuration > 0 && movieTimescale > 0) {
            validFrames = static_cast<uint64_t>(
                std::llround(static_cast<double>(editDuration) * sampleRate / movieTimescale));
        } else {
            validFrames = mediaFrames > delayFrames ? mediaFrames - delayFrames : 0;
        }
        return true;
    }
    if (hasSmpb && smpbLength > 0) {
        // iTunSMPB counts samples at the encoder's output rate, which for
        // HE-AAC is twice the core rate; scale when the media timescale is
        // the core rate.
        double scale = 1.0;
        if (mediaFrames > 0) {
            const double declared = static_cast<double>(smpbDelay) + smpbPadding + smpbLength;
            const double ratio = mediaFrames / declared;
            if (ratio > 1.5) scale = 2.0;
            else if (ratio < 0.75) scale = 0.5;
        }
        delayFrames = static_cast<uint64_t>(smpbDelay * scale);
        validFrames = static_cast<uint64_t>(static_cast<double>(smpbLength) * scale);
        return true;
    }
    validFrames = mediaFrames;
    return false;
}
