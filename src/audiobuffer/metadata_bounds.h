#pragma once

#ifndef METADATA_BOUNDS_H
#define METADATA_BOUNDS_H

#include <cstddef>
#include <cstdint>

/// True when [length] bytes from [offset] lie within the [size] bytes of data.
///
/// Lengths read from media data can use their field's whole unsigned range,
/// so `offset + length` can wrap a 32-bit size_t (web, armeabi-v7a) and pass a
/// `<= size` check. This compares the length with what is left after the
/// offset instead, in 64 bits.
inline bool metadataHasBytes(size_t size, size_t offset, uint64_t length) {
  return offset <= size && length <= static_cast<uint64_t>(size - offset);
}

#endif // METADATA_BOUNDS_H
