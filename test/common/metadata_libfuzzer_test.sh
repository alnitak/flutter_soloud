#!/bin/bash
# Build and run the libFuzzer target for StreamDecoder and the metadata
# parsers (test/common/metadata_libfuzzer_test.cpp):
#  1. the replay build (plain c++) runs the seed corpus, made from
#     example/assets/audio and in-memory fixtures, through the target once;
#  2. when a clang with libFuzzer is found, the fuzzer build (ASan + UBSan +
#     libFuzzer) runs time-boxed (60 s by default) on that corpus, in fork
#     mode so it goes on after a crash. Each crash/timeout input it saves is
#     replayed and reported, and fails the run.
#
# Needs no audio device. Needs the Xiph libraries (brew install opus libogg
# libvorbis flac; apt install libogg-dev libvorbis-dev libopus-dev
# libflac-dev); without them the test is skipped. Step 2 needs a clang that
# supports -fsanitize=fuzzer: Apple clang does not, Homebrew's llvm does
# (brew install llvm). Set LIBFUZZER_CXX to a clang++ to choose one. Without
# it step 2 prints SKIPPED and the script exits 0.
#
#   ./test/common/metadata_libfuzzer_test.sh [--asan] [--strict] [--seconds N]
#
# --asan builds the replay with AddressSanitizer and UndefinedBehaviorSanitizer
# (the fuzzer build always has them).
# --strict is accepted for symmetry with the other tests (no known bugs are
# classified).
# --seconds N sets the fuzzing time (also FUZZ_SECONDS).
# Artifacts and the grown corpus stay in $TMPDIR/metadata_libfuzzer_work.
#
# Verified on macOS (arm64) with Homebrew llvm@22.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=metadata_libfuzzer_test
SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
STRICT=0
SECONDS_BOX="${FUZZ_SECONDS:-60}"
while [ $# -gt 0 ]; do
    case "$1" in
        --asan) SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1); OUT="${OUT}_asan" ;;
        --strict) STRICT=1 ;;
        --seconds) SECONDS_BOX="$2"; shift ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

XIPH_PREFIX=""
for prefix in /opt/homebrew /usr/local /usr; do
    if [ -f "$prefix/include/FLAC/stream_decoder.h" ] && [ -f "$prefix/include/ogg/ogg.h" ] &&
       [ -f "$prefix/include/opus/opus.h" ] && [ -f "$prefix/include/vorbis/codec.h" ]; then
        XIPH_PREFIX="$prefix"
        break
    fi
done
if [ -z "$XIPH_PREFIX" ]; then
    echo "SKIPPED: Xiph libraries not found (brew install opus libogg libvorbis flac)"
    exit 0
fi

if [ "$(uname)" == "Darwin" ]; then
    PLATFORM=(src/audiobuffer/aac_stream_decoder_apple.mm
              -framework AudioToolbox -framework CoreFoundation)
else
    PLATFORM=(src/audiobuffer/aac_stream_decoder_linux.cpp -ldl)
fi
INCLUDES=(-I src -I src/soloud/include -I src/soloud/src -I src/native_decoder -I "$XIPH_PREFIX/include")
SOURCES=(
    src/audiobuffer/stream_decoder.cpp
    src/audiobuffer/m4a_metadata.cpp
    src/audiobuffer/mp3_stream_decoder.cpp
    src/audiobuffer/wav_stream_decoder.cpp
    src/audiobuffer/aac_stream_decoder.cpp
    src/audiobuffer/opus_stream_decoder.cpp
    src/audiobuffer/vorbis_stream_decoder.cpp
    src/audiobuffer/flac_stream_decoder.cpp
    src/audiobuffer/ogg_flac_stream_decoder.cpp
    src/native_stream/m4a_gapless.cpp
    src/soloud/src/audiosource/wav/dr_impl.cpp
)
LIBS=(-L "$XIPH_PREFIX/lib" -lFLAC -lvorbis -logg -lopus -lm)

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

# ---- 1. replay of the seed corpus
c++ -std=c++17 -w -pthread "${SANITIZER[@]}" "${INCLUDES[@]}" \
    -o "$OUT" \
    test/common/$NAME.cpp "${SOURCES[@]}" "${PLATFORM[@]}" "${LIBS[@]}"
"$OUT" example/assets/audio

# ---- 2. time-boxed libFuzzer run
# Only with a real host compiler (the CMake generator runs this script with
# stand-in compilers that print nothing).
if ! c++ --version 2>/dev/null | grep -qiE 'clang|gcc|g\+\+'; then
    echo "SKIPPED: libFuzzer run (no host compiler)"
    exit 0
fi
FUZZ_CXX="${LIBFUZZER_CXX:-}"
if [ -z "$FUZZ_CXX" ]; then
    CANDIDATES=()
    if command -v brew >/dev/null 2>&1; then
        P="$(brew --prefix llvm 2>/dev/null || true)"
        [ -n "$P" ] && CANDIDATES+=("$P/bin/clang++")
    fi
    for d in /opt/homebrew/opt/llvm /opt/homebrew/opt/llvm@* /usr/local/opt/llvm /usr/local/opt/llvm@*; do
        CANDIDATES+=("$d/bin/clang++")
    done
    [ "$(uname)" != "Darwin" ] && CANDIDATES+=("$(command -v clang++ 2>/dev/null || true)")
    WORK_PROBE="$(mktemp -d)"
    printf '#include <cstdint>\n#include <cstddef>\nextern "C" int LLVMFuzzerTestOneInput(const uint8_t *, size_t) { return 0; }\n' \
        > "$WORK_PROBE/probe.cpp"
    for c in "${CANDIDATES[@]}"; do
        [ -x "$c" ] || continue
        if "$c" -fsanitize=fuzzer "$WORK_PROBE/probe.cpp" -o "$WORK_PROBE/probe" >/dev/null 2>&1; then
            FUZZ_CXX="$c"
            break
        fi
    done
    rm -rf "$WORK_PROBE"
fi
if [ -z "$FUZZ_CXX" ]; then
    echo "SKIPPED: libFuzzer run: no clang with -fsanitize=fuzzer (Apple clang lacks it; brew install llvm)"
    exit 0
fi
echo "libFuzzer compiler: $FUZZ_CXX"

SYSROOT=()
if [ "$(uname)" == "Darwin" ]; then
    SYSROOT=(-isysroot "$(xcrun --show-sdk-path)")
fi
WORK="${TMPDIR:-/tmp}/metadata_libfuzzer_work"
FUZZER="$WORK/${NAME}_fuzzer"
mkdir -p "$WORK/corpus" "$WORK/artifacts"
rm -f "$WORK"/artifacts/*
"$FUZZ_CXX" -std=c++17 -w -pthread -g -O1 "${SYSROOT[@]}" -DMETADATA_LIBFUZZER \
    -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer "${INCLUDES[@]}" \
    -o "$FUZZER" \
    test/common/$NAME.cpp "${SOURCES[@]}" "${PLATFORM[@]}" "${LIBS[@]}"
"$OUT" example/assets/audio --make-corpus "$WORK/corpus"

echo "fuzzing for $SECONDS_BOX s (corpus $WORK/corpus)"
set +e
"$FUZZER" -fork=2 -ignore_crashes=1 -ignore_timeouts=1 -ignore_ooms=1 \
    -max_total_time="$SECONDS_BOX" -max_len=70000 -timeout=25 -rss_limit_mb=4096 \
    -detect_leaks=0 -print_final_stats=1 \
    -artifact_prefix="$WORK/artifacts/" "$WORK/corpus" > "$WORK/fuzz.log" 2>&1
STATUS=$?
set -e
grep -E "^#[0-9]+.*(DONE|INITED)|stat::number_of_executed_units|cov:" "$WORK/fuzz.log" | tail -3 || true
if [ $STATUS -ne 0 ] && ! ls "$WORK"/artifacts/* >/dev/null 2>&1; then
    echo "FAILED: libFuzzer exited with $STATUS and saved no input (see $WORK/fuzz.log)"
    tail -30 "$WORK/fuzz.log"
    exit 1
fi

# ---- report what the fuzzer saved
FAILED=0
for a in "$WORK"/artifacts/*; do
    [ -f "$a" ] || continue
    LOG="$a.log"
    "$FUZZER" -timeout=25 "$a" > "$LOG" 2>&1 || true
    echo "FAIL: $(basename "$a") (replay: $FUZZER $a)"
    grep -E "ERROR|runtime error|SUMMARY|invariant|#[0-4] " "$LOG" | head -12
    FAILED=$((FAILED + 1))
done
echo "libFuzzer: $FAILED failures"
if [ $FAILED -ne 0 ]; then
    echo "FAILED"
    exit 1
fi
echo "PASSED"
