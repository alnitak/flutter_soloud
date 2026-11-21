#!/usr/bin/env python3
"""Writes a 16-bit PCM WAV of exactly N frames: silence, then a tone from
frame K to the very end (all channels). Used by the platform decoder tests
to make fixtures with a known length and onset.

  tone_wav.py <out.wav> <rate> <channels> <frames N> <tone start K> [freq] [amp]
"""
import math
import struct
import sys


def main():
    out, rate, channels, frames, start = sys.argv[1], *map(int, sys.argv[2:6])
    freq = float(sys.argv[6]) if len(sys.argv) > 6 else 1000.0
    amp = float(sys.argv[7]) if len(sys.argv) > 7 else 0.5
    data = bytearray(frames * channels * 2)
    for i in range(start, frames):
        s = int(32767 * amp * math.sin(2 * math.pi * freq * (i - start) / rate))
        packed = struct.pack('<h', s)
        for c in range(channels):
            off = (i * channels + c) * 2
            data[off:off + 2] = packed
    with open(out, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ')
        f.write(struct.pack('<IHHIIHH', 16, 1, channels, rate, rate * channels * 2, channels * 2, 16))
        f.write(b'data' + struct.pack('<I', len(data)))
        f.write(data)


if __name__ == '__main__':
    main()
