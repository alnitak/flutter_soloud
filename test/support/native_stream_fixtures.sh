#!/bin/bash
# Makes the fixtures and manifest.txt for test/support/native_stream_decoder_checks.h
# (see the manifest format there). Source it from the repo root, don't run it.
#
#   . test/support/native_stream_fixtures.sh
#   make_native_stream_fixtures <out dir> <scratch dir>
#
# Uses afconvert (macOS) for the exact-length AAC-LC / HE-AAC / HE-AAC v2
# tones and ffmpeg (optional) for AC-3 in MP4, a truncated faststart file and
# a video-only MP4; cases whose tool is missing are left out. Env:
# CASE_FILTER=<regex> keeps only the matching manifest lines.

make_native_stream_fixtures() {
    local FIX="$1"
    local WORK_DIR="$2"
    local MANIFEST="$FIX/manifest.txt"
    local codec rate ch frames start name size
    mkdir -p "$FIX"
    : > "$MANIFEST"

    # Bundled assets: 2 s, silence then a 1 kHz tone from 0.5 s to 1.5 s.
    cp example/assets/audio/aac_lc_mono_44100.m4a example/assets/audio/aac_lc_stereo_48000.m4a "$FIX/"
    echo "aac_lc_mono_44100.m4a ok 44100 1 88200 0" >> "$MANIFEST"
    echo "aac_lc_stereo_48000.m4a ok 48000 2 96000 0" >> "$MANIFEST"

    # Exact-length tones (afconvert writes iTunSMPB, no edit list).
    if command -v afconvert > /dev/null; then
        while read -r codec rate ch frames start; do
            name="${codec}_${rate}_${ch}_${frames}.m4a"
            python3 test/support/tone_wav.py "$WORK_DIR/in.wav" "$rate" "$ch" "$frames" "$start"
            afconvert -f m4af -d "$codec" "$WORK_DIR/in.wav" "$FIX/$name" > /dev/null 2>&1
            echo "$name ok $rate $ch $frames $start" >> "$MANIFEST"
        done <<'CASES'
aac 44100 2 44137 11025
aac 48000 1 12345 4000
aac 32000 2 97023 16000
aac 44100 1 1000 200
aach 44100 2 88201 22050
aach 48000 1 48500 9600
aacp 44100 2 66150 22050
CASES
    else
        echo "afconvert not found: AAC/HE-AAC exact-length cases left out"
    fi

    if command -v ffmpeg > /dev/null; then
        python3 test/support/tone_wav.py "$WORK_DIR/ac3.wav" 48000 2 96000 24000
        # AC-3 in MP4: ffmpeg writes an edit list for the priming it adds.
        if ffmpeg -v error -y -i "$WORK_DIR/ac3.wav" -c:a ac3 -b:a 192k -f mp4 "$FIX/ac3_48000_2.mp4"; then
            echo "ac3_48000_2.mp4 opt 48000 2 96000 24000" >> "$MANIFEST"
        fi
        # moov first, then cut the media data at 60%.
        python3 test/support/tone_wav.py "$WORK_DIR/long.wav" 44100 2 441000 0 440 0.5
        ffmpeg -v error -y -i "$WORK_DIR/long.wav" -c:a aac -b:a 128k -movflags +faststart "$WORK_DIR/faststart.m4a"
        size=$(stat -f %z "$WORK_DIR/faststart.m4a" 2>/dev/null || stat -c %s "$WORK_DIR/faststart.m4a")
        head -c $(( size * 6 / 10 )) "$WORK_DIR/faststart.m4a" > "$FIX/truncated_faststart.m4a"
        echo "truncated_faststart.m4a trunc 44100 2 441000 0" >> "$MANIFEST"
        # An MP4 with only a video track.
        if ffmpeg -v error -y -f lavfi -i color=c=black:s=64x64:d=1 -c:v mpeg4 -f mp4 "$FIX/video_only.mp4"; then
            echo "video_only.mp4 fail 0 0 0 0" >> "$MANIFEST"
        fi
    else
        echo "ffmpeg not found: AC-3, truncated and video-only cases left out"
    fi

    # Not MP4 at all, a garbage file named .m4a, an empty file, a cut header.
    cp example/assets/audio/sample-MP3.mp3 "$FIX/not_mp4.mp3"
    echo "not_mp4.mp3 nomp4 0 0 0 0" >> "$MANIFEST"
    head -c 4096 /dev/urandom > "$FIX/garbage.m4a"
    echo "garbage.m4a fail 0 0 0 0" >> "$MANIFEST"
    : > "$FIX/empty.m4a"
    echo "empty.m4a fail 0 0 0 0" >> "$MANIFEST"
    head -c 64 example/assets/audio/aac_lc_mono_44100.m4a > "$FIX/header_only.m4a"
    echo "header_only.m4a fail 0 0 0 0" >> "$MANIFEST"
    echo "missing_file.m4a fail 0 0 0 0" >> "$MANIFEST"

    if [ -n "${CASE_FILTER:-}" ]; then
        grep -E "$CASE_FILTER" "$MANIFEST" > "$MANIFEST.tmp" || true
        mv "$MANIFEST.tmp" "$MANIFEST"
    fi
}
