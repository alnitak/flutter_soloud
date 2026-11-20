#!/bin/bash
# Build and run the standalone concurrency test of Player (src/player.cpp)
# through the exports Dart uses (src/bindings.cpp), see
# test/common/player_concurrency_test.cpp: loader threads loading the same
# and new sounds, the UI thread playing / stopping / disposing them, a
# realtime feeder, a samples reader and the voice-ended / file-loaded
# callbacks, while the engine is torn down and re-initialized repeatedly.
#
# Needs no audio device and no assets: miniaudio's null backend only
# (MA_ENABLE_ONLY_SPECIFIC_BACKENDS + MA_ENABLE_NULL). No Xiph libraries.
#
#   ./test/common/player_concurrency_test.sh [--asan|--tsan] [--strict]
#       [--api-threads N] [--cycles N]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
# with ThreadSanitizer. --api-threads (default 1, as Dart has one UI
# isolate) runs more threads calling the play/stop/dispose exports at once.
# --strict (passed to the test) makes checks marked KNOWN BUG fail the run.
#
# Verified on macOS arm64 (plain, --asan, --tsan).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=player_concurrency_test
SANITIZER=(-O1 -g)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
elif [ "${1:-}" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread -g -O1)
    OUT="${OUT}_tsan"
    shift
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# pffft.c is C99: the CMake build forces C for it, so do the same here.
cc -std=gnu99 -O2 -c "${SANITIZER[@]}" \
    -I src/pffft \
    -o "$WORK_DIR/pffft.o" \
    src/pffft/pffft.c

# The OS decoder backends are compiled per platform, like src/CMakeLists.txt.
PLATFORM_SOURCES=()
PLATFORM_LIBS=()
if [ "$(uname)" == "Darwin" ]; then
    PLATFORM_SOURCES=(
        src/native_decoder/os_decoder_apple.mm
        src/audiobuffer/aac_stream_decoder_apple.mm
    )
    PLATFORM_LIBS=(-framework AudioToolbox -framework CoreFoundation
                   -framework Foundation)
else
    PLATFORM_SOURCES=(src/native_decoder/os_decoder_linux.cpp)
fi

c++ -std=c++17 -w -pthread "${SANITIZER[@]}" \
    -DWITH_MINIAUDIO \
    -DMA_ENABLE_ONLY_SPECIFIC_BACKENDS -DMA_ENABLE_NULL \
    -DNO_XIPH_LIBS \
    -I src/soloud/include \
    -I src/soloud/src \
    -I src/pffft \
    -I src \
    -I src/native_decoder \
    -o "$OUT" \
    test/common/$NAME.cpp \
    "$WORK_DIR/pffft.o" \
    src/soloud_common.cpp \
    src/device_lifecycle_test_hooks.cpp \
    src/bindings.cpp \
    src/player.cpp \
    src/analyzer.cpp \
    src/synth/*.cpp \
    src/waveform/waveform.cpp \
    src/waveform/miniaudio_libvorbis.cpp \
    src/audiobuffer/*.cpp \
    src/native_stream/*.cpp \
    src/native_decoder/native_audio_decoder.cpp \
    "${PLATFORM_SOURCES[@]}" \
    src/filters/*.cpp \
    src/mixeroutput/*.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/filter/*.cpp \
    src/soloud/src/audiosource/ay/*.cpp \
    src/soloud/src/audiosource/monotone/*.cpp \
    src/soloud/src/audiosource/noise/*.cpp \
    src/soloud/src/audiosource/sfxr/*.cpp \
    src/soloud/src/audiosource/speech/*.cpp \
    src/soloud/src/audiosource/tedsid/*.cpp \
    src/soloud/src/audiosource/vic/*.cpp \
    src/soloud/src/audiosource/vizsn/*.cpp \
    src/soloud/src/audiosource/wav/*.cpp \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    "${PLATFORM_LIBS[@]}" \
    -ldl -lm

STRICT=0
for arg in "$@"; do [ "$arg" == "--strict" ] && STRICT=1; done

# Known bug B2: Player::applyPauseState() and Player::play() read
# ActiveSound::handle without sounds_mutex while the audio thread's
# voice-ended dispatch erases from it (Player::removeHandle). TSan reports it
# as a data race; it is suppressed (and reported as KNOWN BUG) unless
# --strict is given.
TSAN_SUPP="$WORK_DIR/tsan.supp"
cat > "$TSAN_SUPP" <<'SUPP'
race:Player::applyPauseState
race:Player::play(unsigned int, unsigned int&
SUPP
if [ "${SANITIZER[0]}" == "-fsanitize=thread" ]; then
    TSAN_DEFAULT="halt_on_error=1 second_deadlock_stack=1"
    if [ "$STRICT" == 0 ]; then
        TSAN_DEFAULT="$TSAN_DEFAULT suppressions=$TSAN_SUPP print_suppressions=1"
    fi
    export TSAN_OPTIONS="${TSAN_OPTIONS:-$TSAN_DEFAULT}"
fi
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
# The decoders print a line per stream load, and loadFile() warns when the
# engine is down (which loads racing a deinit expect): drop those.
set +e
"$OUT" "$@" 2>&1 | grep -v -e '^\[WavStream::parse\]' \
    -e '^WARNING (from SoLoud C++ binding code): the player has not yet been initialized' \
    | tee "$WORK_DIR/output.txt"
STATUS=${PIPESTATUS[0]}
if grep -q -e '^ *[0-9]* race:Player::applyPauseState' \
        -e '^ *[0-9]* race:Player::play' "$WORK_DIR/output.txt"; then
    echo "  KNOWN BUG B2 (TSan: Player::applyPauseState / Player::play read" \
        "ActiveSound::handle without sounds_mutex while the audio thread" \
        "erases from it in Player::removeHandle; src/player.cpp:1819," \
        "2688-2706, 3131)"
fi
exit "$STATUS"
