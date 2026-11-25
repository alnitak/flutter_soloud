#pragma once

#ifndef ID3_TEXT_H
#define ID3_TEXT_H

#include "metadata_bounds.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace id3_text_detail {
inline void appendUtf8(std::string &out, uint32_t codepoint) {
  if (codepoint < 0x80) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}
} // namespace id3_text_detail

/// Converts the body of an ID3v2 text frame (its encoding byte, then the
/// text) to UTF-8, the encoding metadata strings cross the native boundary
/// in, whatever the frame's encoding.
///
/// Encoding 0 is Latin-1, whose bytes from 0x80 need two-byte sequences.
/// Encodings 1 (UTF-16, little-endian unless a byte-order mark says
/// otherwise) and 2 (UTF-16BE) combine a surrogate pair into one character
/// and replace an unpaired surrogate with U+FFFD; their text ends at the
/// first NUL. Encoding 3 is UTF-8 and passes through. Trailing NULs are
/// dropped.
inline std::string id3TextToUtf8(const unsigned char *data, size_t frameSize) {
  if (frameSize <= 1)
    return {};
  const uint8_t encoding = data[0];
  const unsigned char *text = data + 1;
  size_t length = frameSize - 1;
  std::string out;

  if (encoding == 1 || encoding == 2) {
    bool little = encoding == 1;
    if (length >= 2 && ((text[0] == 0xFF && text[1] == 0xFE) ||
                        (text[0] == 0xFE && text[1] == 0xFF))) {
      little = text[0] == 0xFF;
      text += 2;
      length -= 2;
    }
    const auto unitAt = [&](size_t pos) -> uint32_t {
      return little ? (text[pos] | (static_cast<uint32_t>(text[pos + 1]) << 8))
                    : ((static_cast<uint32_t>(text[pos]) << 8) | text[pos + 1]);
    };
    size_t pos = 0;
    while (metadataHasBytes(length, pos, 2)) {
      uint32_t codepoint = unitAt(pos);
      pos += 2;
      if (codepoint == 0)
        break;
      if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
        // A high surrogate must be followed by a low one.
        const bool paired = metadataHasBytes(length, pos, 2) &&
                            unitAt(pos) >= 0xDC00 && unitAt(pos) <= 0xDFFF;
        if (paired) {
          codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (unitAt(pos) - 0xDC00);
          pos += 2;
        } else {
          codepoint = 0xFFFD;
        }
      } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
        codepoint = 0xFFFD; // a low surrogate on its own
      }
      id3_text_detail::appendUtf8(out, codepoint);
    }
    return out;
  }

  while (length > 0 && text[length - 1] == 0)
    --length;
  if (encoding == 0) {
    for (size_t i = 0; i < length; ++i)
      id3_text_detail::appendUtf8(out, text[i]);
  } else {
    out.assign(reinterpret_cast<const char *>(text), length);
  }
  return out;
}

/// Converts the text of an ID3v2 COMM frame body (encoding byte, 3-byte
/// language, description ended by a NUL in the frame's encoding, then the
/// comment) to UTF-8, as id3TextToUtf8() does for text frames.
inline std::string id3CommentToUtf8(const unsigned char *data, size_t frameSize) {
  if (frameSize <= 4)
    return {};
  const uint8_t encoding = data[0];
  const bool wide = encoding == 1 || encoding == 2;
  size_t pos = 4;
  // Skip the description and its terminator.
  if (wide) {
    while (metadataHasBytes(frameSize, pos, 2) &&
           (data[pos] != 0 || data[pos + 1] != 0))
      pos += 2;
    pos = metadataHasBytes(frameSize, pos, 2) ? pos + 2 : frameSize;
  } else {
    while (pos < frameSize && data[pos] != 0)
      ++pos;
    pos = pos < frameSize ? pos + 1 : frameSize;
  }
  if (pos >= frameSize)
    return {};
  std::vector<unsigned char> text;
  text.reserve(frameSize - pos + 1);
  text.push_back(encoding);
  text.insert(text.end(), data + pos, data + frameSize);
  return id3TextToUtf8(text.data(), text.size());
}

#endif // ID3_TEXT_H
