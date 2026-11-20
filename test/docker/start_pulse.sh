#!/bin/bash
# Start a PulseAudio daemon with a null sink inside the container, so
# miniaudio finds an output device (no sound is produced). Safe to call twice.
if pulseaudio --check 2>/dev/null; then
    exit 0
fi
pulseaudio --daemonize=yes --exit-idle-time=-1 --system=false \
    --disallow-exit -n \
    --load="module-null-sink sink_name=null_out" \
    --load="module-native-protocol-unix" >/dev/null 2>&1
for _ in $(seq 1 20); do
    if pulseaudio --check 2>/dev/null && pactl set-default-sink null_out >/dev/null 2>&1; then
        # An idle null sink stalls a newly connected stream for about 0.5 s
        # (no callbacks at all through the native Pulse backend), which
        # breaks the device clock checks. A silent stream that never ends
        # keeps the sink running.
        nohup pacat --latency-msec=20 --raw /dev/zero >/dev/null 2>&1 &
        sleep 0.5
        exit 0
    fi
    sleep 0.2
done
echo "start_pulse.sh: PulseAudio did not start" >&2
exit 1
