# Approximate Windows test environment for flutter_soloud: the CMake suite and
# the Windows-specific native tests cross-compiled with mingw-w64 (x86_64) and
# run under Wine. linux/amd64 image (emulated on Apple silicon).
#
# This is not MSVC and Wine's Media Foundation / WASAPI are partial
# reimplementations, so a failure here may be a Wine limitation rather than
# a bug; a pass is a good sign, not proof. Real Windows remains the reference.
#
#   ./test/docker/run.sh windows          # build the image if needed, run all
#   ./test/docker/run.sh windows shell

FROM --platform=linux/amd64 ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN dpkg --add-architecture i386 && apt-get update && apt-get install -y --no-install-recommends \
        mingw-w64 g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix \
        cmake ninja-build make python3 rsync file ca-certificates \
        wine wine64 xvfb \
        gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
        gstreamer1.0-plugins-ugly gstreamer1.0-libav \
    && rm -rf /var/lib/apt/lists/*

# posix threads model, needed for std::thread / std::mutex.
RUN update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix \
 && update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix

ENV WINEDEBUG=-all WINEPREFIX=/root/.wine WINEARCH=win64
RUN (wineboot --init >/dev/null 2>&1 || true) && (wineserver -w || true)

COPY mingw-toolchain.cmake /opt/mingw-toolchain.cmake
WORKDIR /work
