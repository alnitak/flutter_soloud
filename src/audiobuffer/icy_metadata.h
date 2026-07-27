#pragma once

#ifndef ICY_METADATA_H
#define ICY_METADATA_H

#include <cstddef>
#include <string>
#include <vector>

/// State for stripping ICY (Shoutcast) metadata from a stream. Shared by the
/// FLAC stream decoders, which had identical copies of the stripping loop.
struct IcyStripState {
  int audioBytesCount = 0;   ///< Audio bytes seen since the last metadata block.
  int icyMetaSize = 0;       ///< Remaining metadata bytes to skip (0 = none).
  std::vector<unsigned char> icyMetadata; ///< Current metadata block accumulator.
  std::string streamTitle;   ///< Last reported StreamTitle value.
};

/// Split an ICY stream chunk into pure audio bytes and metadata blocks.
/// Audio bytes are appended to [cleanOut]; [onStreamTitle] is invoked with
/// the new title whenever the StreamTitle metadata changes.
/// [icyMetaInt] is the ICY interval in bytes (must be > 0).
template <typename OnStreamTitle>
inline void stripIcyMetadata(const std::vector<unsigned char> &buffer,
                             int icyMetaInt, IcyStripState &state,
                             std::vector<unsigned char> &cleanOut,
                             OnStreamTitle &&onStreamTitle) {
  cleanOut.reserve(cleanOut.size() + buffer.size());
  for (unsigned char byte : buffer) {
    if (state.icyMetaSize > 0) {
      state.icyMetadata.push_back(byte);
      if (--state.icyMetaSize == 0) {
        const std::string meta(state.icyMetadata.begin(),
                               state.icyMetadata.end());
        const size_t titlePos = meta.find("StreamTitle='");
        if (titlePos != std::string::npos) {
          const size_t endPos = meta.find("';", titlePos + 13);
          if (endPos != std::string::npos) {
            std::string title =
                meta.substr(titlePos + 13, endPos - (titlePos + 13));
            if (title != state.streamTitle) {
              state.streamTitle = title;
              onStreamTitle(title);
            }
          }
        }
        state.icyMetadata.clear();
      }
    } else if (state.audioBytesCount == icyMetaInt) {
      state.icyMetaSize = byte * 16;
      state.audioBytesCount = 0;
    } else {
      cleanOut.push_back(byte);
      ++state.audioBytesCount;
    }
  }
}

#endif // ICY_METADATA_H
