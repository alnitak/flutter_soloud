# Minimal Linux images to run test/linux/os_decoder_linux_test.sh (and
# no_ffmpeg_test.sh) against different FFmpeg releases. os_decoder_linux.cpp
# reads AVStream / AVCodecParameters fields at hard-coded offsets that differ
# between libavformat majors, so each supported major gets its own image:
#
#   FFmpeg 4.4  lavf 58   ubuntu:22.04
#   FFmpeg 5.1  lavf 59   debian:bookworm
#   FFmpeg 6.1  lavf 60   ubuntu:24.04
#   FFmpeg 7.1  lavf 61   debian:trixie
#   FFmpeg 8.x  lavf 62   ubuntu:26.04
#
# Built by run.sh (`./test/docker/run.sh ffmpeg [4|5|6|7|8|all]`) with
# --build-arg BASE=<image>. Native arch of the host. NO_FFMPEG=1 builds the
# same image without any FFmpeg library (for test/linux/no_ffmpeg_test.sh).

ARG BASE=ubuntu:24.04
FROM ${BASE}

ARG DEBIAN_FRONTEND=noninteractive
ARG NO_FFMPEG=0
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates rsync file python3 \
    && if [ "$NO_FFMPEG" != "1" ]; then \
        apt-get install -y --no-install-recommends ffmpeg; \
    fi \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
