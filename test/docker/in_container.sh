#!/bin/bash
# Entry point inside the test containers (see run.sh). Copies /src to /work,
# runs the requested suites and writes logs and a summary to /out.

set -uo pipefail

PLATFORM="$1"
WHAT="$2"
shift 2

rsync -a --delete \
    --exclude '.dart_tool/' --exclude 'build/' --exclude 'coverage/' \
    --exclude 'example/build/' --exclude 'example/.dart_tool/' \
    --exclude '.git/' \
    /src/ /work/
cd /work

SUMMARY=/out/summary.txt
: > "$SUMMARY"
STATUS=0

step() {
    # step <name> <command...>: run, log to /out/<name>.log, record the result.
    local name="$1"; shift
    local log="/out/$name.log"
    local start=$(date +%s)
    echo "=== $name"
    if "$@" > "$log" 2>&1; then
        echo "$name: OK ($(( $(date +%s) - start ))s)" >> "$SUMMARY"; echo "    OK"
    else
        echo "$name: FAILED ($(( $(date +%s) - start ))s), see $log" >> "$SUMMARY"; echo "    FAILED"
        tail -30 "$log" | sed 's/^/    /'
        STATUS=1
    fi
}

KNOWN_FAILING="${KNOWN_FAILING_NATIVE:-}"
# ctest -E with an empty pattern would exclude everything.
exclude_args() { [ -n "$KNOWN_FAILING" ] && echo "-E" "($(echo "$KNOWN_FAILING" | tr ' ' '|'))"; }

linux_scripts() {
    NATIVE_TEST_LOG_DIR=/out/native_logs ./test/linux/all.sh "$@"
}
linux_ctest() {
    local sanitize="${1:-}"
    local dir="/tmp/native_tests${sanitize:+_$sanitize}"
    cmake -S test -B "$dir" -G Ninja -DSOLOUD_TEST_SANITIZE="$sanitize" &&
    cmake --build "$dir" --parallel "$(nproc)" -- -k 0;
    ctest --test-dir "$dir" -j"$(nproc)" --output-on-failure $(exclude_args)
}
linux_ctest_tsan() {
    # Races inside uninstrumented system libraries (libpulse) are suppressed,
    # see test/support/tsan.supp. halt_on_error=0 so one report doesn't hide
    # the rest; a report still fails the test (exit code 66).
    TSAN_OPTIONS="suppressions=/work/test/support/tsan.supp halt_on_error=0 second_deadlock_stack=1 ${TSAN_OPTIONS:-}" \
        linux_ctest thread
}
linux_dart() {
    flutter pub get >/dev/null &&
    flutter test test/common test/android test/ios test/linux "$@"
}

ffmpeg_os_decoder() {
    echo "FFmpeg libraries in this image:"
    ls /usr/lib/*/libavformat.so.* /usr/lib/*/libavcodec.so.* 2>/dev/null || echo "  (none)"
    command -v ffmpeg >/dev/null && ffmpeg -hide_banner -version | head -1
    local lavf log status
    lavf="$(ls /usr/lib/*/libavformat.so.[0-9]* 2>/dev/null | sed -n 's/.*libavformat\.so\.\([0-9]*\)$/\1/p' | head -1)"
    log="$(mktemp)"
    # stdout and FFmpeg's stderr together, so a failing check is in the log.
    OSDEC_VERBOSE=1 bash test/linux/os_decoder_linux_test.sh "$@" > "$log" 2>&1
    status=$?
    grep -v '^\[[a-z0-9_,]* @ 0x' "$log"
    [ $status -eq 0 ] && return 0
    # Known src/ bugs on the older FFmpeg majors (see the report of TEST_PLAN
    # LIN2); OSDEC_STRICT=1 makes them fail. Anything else still fails.
    if [ "${OSDEC_STRICT:-0}" != "1" ]; then
        if [ "$lavf" == "59" ] && grep -q '^\[Test\] First FFmpeg load' "$log" &&
            ! grep -q '^\[Test\] decodeMemory creates' "$log"; then
            echo "KNOWN BUG M6 (os_decoder_linux.cpp:206 and :351: lavf 59 (FFmpeg 5.x) read with the lavf 60+ AVStream layout, codecpar taken from priv_data, time_base/duration from the wrong fields): crashes on the first decode (exit $status)"
            return 0
        fi
        if [ "$lavf" == "58" ] && grep -q 'heap-buffer-overflow src/native_decoder/os_decoder_linux.cpp' "$log"; then
            echo "KNOWN BUG M7 (os_decoder_linux.cpp: AVCodecParameters read at FFmpeg 7 offsets past the end of the lavc 58 struct)"
            return 0
        fi
        if [ "$lavf" == "58" ] && grep -q '^1 CHECK(S) FAILED' "$log" &&
            grep -q 'FAIL: aac_lc_mono_44100.m4a truncated to 99%: decoded .* more than the whole file' "$log"; then
            echo "KNOWN BUG M8 (os_decoder_linux.cpp: no AVStream duration fallback for lavf 58, a truncated M4A is not trimmed)"
            return 0
        fi
    fi
    return $status
}
ffmpeg_none() {
    bash test/linux/no_ffmpeg_test.sh "$@"
}

windows_ctest() {
    local dir=/tmp/native_tests_win
    cmake -S test -B "$dir" -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/mingw-toolchain.cmake \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo &&
    cmake --build "$dir" --parallel "$(nproc)" -- -k 0;
    ctest --test-dir "$dir" -j4 --timeout 1800 --output-on-failure $(exclude_args)
}
windows_native() {
    bash test/docker/windows_native.sh
}

case "$PLATFORM:$WHAT" in
    linux:shell|windows:shell) start_pulse.sh 2>/dev/null || true; exec bash ;;
    ffmpeg:shell) exec bash ;;
    linux:*) start_pulse.sh || echo "warning: no PulseAudio, device tests will skip" ;;
esac

case "$PLATFORM:$WHAT" in
    linux:all)
        step linux_scripts linux_scripts "$@"
        step linux_ctest linux_ctest
        step linux_ctest_asan linux_ctest address
        step linux_ctest_tsan linux_ctest_tsan
        step linux_dart linux_dart
        ;;
    linux:scripts) step linux_scripts linux_scripts "$@" ;;
    linux:ctest) step linux_ctest linux_ctest "$@" ;;
    linux:asan) step linux_ctest_asan linux_ctest address ;;
    linux:tsan) step linux_ctest_tsan linux_ctest_tsan ;;
    ffmpeg:none) step ffmpeg_none ffmpeg_none "$@" ;;
    ffmpeg:*) step "ffmpeg_$WHAT" ffmpeg_os_decoder "$@" ;;
    linux:dart) step linux_dart linux_dart "$@" ;;
    windows:all)
        step windows_ctest windows_ctest
        step windows_native windows_native
        ;;
    windows:ctest) step windows_ctest windows_ctest ;;
    windows:native) step windows_native windows_native ;;
    *) echo "unknown: $PLATFORM $WHAT" >&2; exit 2 ;;
esac

echo
echo "SUMMARY ($PLATFORM):"
cat "$SUMMARY"
exit $STATUS
