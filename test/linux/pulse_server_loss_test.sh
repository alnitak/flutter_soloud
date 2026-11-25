#!/bin/bash
# The PulseAudio server is stopped and restarted while the engine plays on
# miniaudio's PulseAudio backend: nothing blocks, the device state follows,
# playback can be resumed. Builds test/linux/pulse_server_loss_test.cpp
# against the whole plugin (test/support/plugin_build.sh).
#
# The script starts its own PulseAudio server (null sink, private runtime
# directory and socket, PULSE_SERVER pointed at it), so the desktop's sound
# server is never touched.
#
#   test/linux/pulse_server_loss_test.sh [--asan|--tsan] [--strict] [--verbose]
#
# Platform: Linux. Needs: a C/C++ toolchain and the `pulseaudio` daemon binary
# (Ubuntu: pulseaudio; on PipeWire desktops it may be missing, then the test
# prints SKIPPED). No audio hardware, no assets.
#   --asan    AddressSanitizer + UBSan;  --tsan  ThreadSanitizer.
#   --strict  known src/ bugs (KNOWN BUG checks) fail the run.
# Verification: Linux arm64, Ubuntu 24.04 in test/docker.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ "$(uname -s)" != "Linux" ]; then
    echo "SKIPPED: Linux only"
    exit 0
fi
if ! command -v pulseaudio >/dev/null 2>&1; then
    echo "SKIPPED: the pulseaudio daemon is not installed"
    exit 0
fi

SANITIZER=()
OUT="${TMPDIR:-/tmp}/pulse_server_loss_test"
ARGS=()
for arg in "$@"; do
    case "$arg" in
        --asan) SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1); OUT="${OUT}_asan" ;;
        --tsan) SANITIZER=(-fsanitize=thread -g -O1); OUT="${OUT}_tsan" ;;
        --strict|--verbose) ARGS+=("$arg") ;;
        *) echo "unknown argument: $arg"; exit 2 ;;
    esac
done

WORK_DIR="$(mktemp -d)"
PULSE_DIR="$WORK_DIR/pulse"
mkdir -p "$PULSE_DIR"
chmod 700 "$PULSE_DIR"

# The private server's control scripts, passed to the test as commands.
cat > "$WORK_DIR/pulse_start.sh" <<EOF
#!/bin/bash
export PULSE_RUNTIME_PATH="$PULSE_DIR" PULSE_STATE_PATH="$PULSE_DIR" HOME="$PULSE_DIR"
pulseaudio --daemonize=no --exit-idle-time=-1 --system=false --disallow-exit -n \\
    --use-pid-file=no \\
    --load="module-null-sink sink_name=loss_null" \\
    --load="module-native-protocol-unix socket=$PULSE_DIR/native auth-anonymous=1" \\
    > "$PULSE_DIR/server.log" 2>&1 &
echo \$! > "$PULSE_DIR/pid"
for _ in \$(seq 1 50); do
    if PULSE_SERVER="unix:$PULSE_DIR/native" pactl info >/dev/null 2>&1; then
        PULSE_SERVER="unix:$PULSE_DIR/native" pactl set-default-sink loss_null >/dev/null 2>&1
        # An idle null sink stalls new streams (see test/docker/start_pulse.sh).
        PULSE_SERVER="unix:$PULSE_DIR/native" nohup pacat --latency-msec=20 --raw /dev/zero >/dev/null 2>&1 &
        sleep 0.3
        exit 0
    fi
    sleep 0.1
done
cat "$PULSE_DIR/server.log"
exit 1
EOF
cat > "$WORK_DIR/pulse_stop.sh" <<EOF
#!/bin/bash
pid=\$(cat "$PULSE_DIR/pid" 2>/dev/null) || exit 0
kill -9 "\$pid" 2>/dev/null
for _ in \$(seq 1 50); do kill -0 "\$pid" 2>/dev/null || exit 0; sleep 0.1; done
exit 1
EOF
chmod +x "$WORK_DIR/pulse_start.sh" "$WORK_DIR/pulse_stop.sh"
trap '"$WORK_DIR/pulse_stop.sh"; rm -rf "$WORK_DIR"' EXIT

. test/support/plugin_build.sh
PLUGIN_BUILD_NO_COMMON=1 plugin_build "$OUT" test/linux/pulse_server_loss_test.cpp

if ! "$WORK_DIR/pulse_start.sh"; then
    echo "SKIPPED: could not start a private PulseAudio server"
    exit 0
fi
export PULSE_SERVER="unix:$PULSE_DIR/native"

if [ ${#SANITIZER[@]} -gt 0 ]; then
    export LSAN_OPTIONS="suppressions=$REPO_ROOT/test/support/lsan.supp${LSAN_OPTIONS:+:$LSAN_OPTIONS}"
    export TSAN_OPTIONS="suppressions=$REPO_ROOT/test/support/tsan.supp${TSAN_OPTIONS:+ $TSAN_OPTIONS}"
fi
"$OUT" "$WORK_DIR/pulse_stop.sh" "$WORK_DIR/pulse_start.sh" ${ARGS[@]+"${ARGS[@]}"}
