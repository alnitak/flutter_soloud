#pragma once

#ifndef OGG_SEEK_INDEX_H
#define OGG_SEEK_INDEX_H

#if !defined(NO_XIPH_LIBS)

#include <cstdint>
#include <vector>

#ifdef __EMSCRIPTEN__
// For Web include dirs downloaded from git for build
#include "../../xiph/ogg/include/ogg/ogg.h"
#else
#include <ogg/ogg.h>
#endif

/// Tracks Ogg page granule positions against encoded byte offsets so a target
/// time can be mapped to a byte offset for out-of-buffer seeks. Shared by the
/// Opus and Vorbis stream decoders, whose tracking/interpolation logic was
/// previously duplicated.
struct OggSeekIndex {
  struct Entry {
    ogg_int64_t granule;
    uint64_t byteOffset;
  };

  std::vector<Entry> entries;
  uint64_t bytesConsumed = 0;

  /// Account for one page returned by ogg_sync_pageseek ([pageSeekRet] is the
  /// return value of ogg_sync_pageseek: bytes skipped + page size).
  void trackPage(long pageSeekRet, const ogg_page &page) {
    bytesConsumed += static_cast<uint64_t>(pageSeekRet);
    const long pageSize = page.header_len + page.body_len;
    if (pageSize <= 0) return;
    const ogg_int64_t granule = ogg_page_granulepos(&page);
    if (granule >= 0 &&
        (entries.empty() || entries.back().granule != granule)) {
      entries.push_back({granule, bytesConsumed - static_cast<uint64_t>(pageSize)});
    }
  }

  void clear() {
    entries.clear();
    bytesConsumed = 0;
  }

  /// True when the index covers [targetGranule].
  bool covers(ogg_int64_t targetGranule) const {
    return !entries.empty() && entries.back().granule >= targetGranule;
  }

  /// Interpolated byte offset for [targetGranule], or 0 when not covered.
  uint64_t byteOffsetFor(ogg_int64_t targetGranule) const {
    if (!covers(targetGranule)) return 0;
    for (size_t i = 1; i < entries.size(); ++i) {
      if (entries[i].granule >= targetGranule) {
        const Entry &a = entries[i - 1];
        const Entry &b = entries[i];
        if (b.granule == a.granule) return a.byteOffset;
        const double t = static_cast<double>(targetGranule - a.granule) /
                         static_cast<double>(b.granule - a.granule);
        return a.byteOffset +
               static_cast<uint64_t>(t * static_cast<double>(b.byteOffset - a.byteOffset));
      }
    }
    return entries.back().byteOffset;
  }
};

#endif // !defined(NO_XIPH_LIBS)
#endif // OGG_SEEK_INDEX_H
