# Linux test environment for flutter_soloud: native tests (scripts and the
# CMake suite) and `flutter test`. Native arch of the host (arm64 on Apple
# silicon, amd64 elsewhere).
#
#   ./test/docker/run.sh linux            # build the image if needed, run all
#   ./test/docker/run.sh linux shell      # interactive shell in /work

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG FLUTTER_VERSION=3.50.0

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential clang lld llvm cmake ninja-build pkg-config git curl \
        ca-certificates unzip xz-utils zip file rsync python3 \
        libogg-dev libvorbis-dev libopus-dev libflac-dev \
        libasound2-dev libasound2-plugins libpulse-dev pulseaudio alsa-utils \
        ffmpeg libavcodec-dev libavformat-dev libavutil-dev libswresample-dev \
        lame flac vorbis-tools gcovr lcov \
        libgtk-3-dev libglu1-mesa \
    && rm -rf /var/lib/apt/lists/*

# Flutter SDK (git checkout works on both amd64 and arm64).
RUN git clone --depth 1 --branch "${FLUTTER_VERSION}" \
        https://github.com/flutter/flutter.git /opt/flutter \
    && git config --global --add safe.directory /opt/flutter
ENV PATH="/opt/flutter/bin:/opt/flutter/bin/cache/dart-sdk/bin:${PATH}"
RUN flutter config --no-analytics >/dev/null \
    && flutter precache --linux --no-android --no-ios --no-web >/dev/null \
    && flutter --version

# A PulseAudio null sink, so tests that need an "audio device" have one.
COPY start_pulse.sh /usr/local/bin/start_pulse.sh
RUN chmod +x /usr/local/bin/start_pulse.sh
# miniaudio tries ALSA first. With no sound card, ALSA's default PCM opens but
# never clocks, so route it to the PulseAudio null sink.
RUN printf 'pcm.!default { type pulse }\nctl.!default { type pulse }\n' > /etc/asound.conf

# Xvfb for the VST3 editor test (test/linux/vst3_editor_linux_test.sh).
# A separate layer so adding it does not rebuild the image above.
RUN apt-get update && apt-get install -y --no-install-recommends xvfb x11-utils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
