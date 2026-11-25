#!/bin/bash
# Run the test suites in Docker containers.
#
#   ./test/docker/run.sh linux   [all|scripts|ctest|asan|tsan|dart|shell] [-- extra args]
#   ./test/docker/run.sh windows [all|ctest|native|shell]
#   ./test/docker/run.sh ffmpeg  [all|4|5|6|7|8|none] [-- extra args]
#
#   linux    ubuntu 24.04, host architecture: native scripts (test/common +
#            test/linux), the CMake suite (ctest, plus ASan and TSan), flutter test.
#            A PulseAudio null sink provides an output device.
#   windows  mingw-w64 cross build run under Wine (linux/amd64 image,
#            emulated on Apple silicon): the CMake suite and the
#            test/windows sources. An approximation of Windows, see
#            windows-wine.Dockerfile.
#   ffmpeg   small images with one FFmpeg release each (ffmpeg.Dockerfile):
#            test/linux/os_decoder_linux_test.sh against FFmpeg 4.4, 5.1,
#            6.1, 7.1 and 8 (`all` runs every one; 4 and 8 are extras whose
#            base image may be missing and then is not run), `none` = no FFmpeg,
#            for test/linux/no_ffmpeg_test.sh. Extra args go to the scripts
#            (e.g. -- --asan).
#
# The repository is mounted read-only and copied into the container, so the
# host's .dart_tool/ and build/ are never touched. Logs and results go to
# build/docker/<platform>/ on the host. Set REBUILD=1 to rebuild the image.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DOCKER_DIR="$REPO_ROOT/test/docker"
PLATFORM="${1:?usage: run.sh linux|windows|ffmpeg [what]}"
WHAT="${2:-all}"
shift $(( $# >= 2 ? 2 : $# ))
[ "${1:-}" == "--" ] && shift

if [ "$PLATFORM" == "ffmpeg" ]; then
    # One image per FFmpeg release; results in build/docker/ffmpeg/<version>/.
    ffmpeg_base() {
        case "$1" in
            4) echo ubuntu:22.04 ;;
            5) echo debian:bookworm ;;
            6) echo ubuntu:24.04 ;;
            7) echo debian:trixie ;;
            8) echo ubuntu:26.04 ;;
            none) echo ubuntu:24.04 ;;
            *) return 1 ;;
        esac
    }
    VERSIONS=("$WHAT")
    [ "$WHAT" == "all" ] && VERSIONS=(4 5 6 7 8 none)
    OVERALL=0
    RESULTS=()
    for v in "${VERSIONS[@]}"; do
        base="$(ffmpeg_base "$v")" || { echo "unknown FFmpeg version: $v" >&2; exit 2; }
        image="flutter-soloud-test-ffmpeg-$v"
        no_ffmpeg=0
        [ "$v" == "none" ] && no_ffmpeg=1
        if [ "${REBUILD:-0}" == "1" ] || ! docker image inspect "$image" >/dev/null 2>&1; then
            if ! docker build --build-arg BASE="$base" --build-arg NO_FFMPEG="$no_ffmpeg" \
                -t "$image" -f "$DOCKER_DIR/ffmpeg.Dockerfile" "$DOCKER_DIR"; then
                # 4 and 8 are extras: a base image that is gone or not yet
                # released must not fail the required versions.
                if [ "$v" == "4" ] || [ "$v" == "8" ]; then
                    RESULTS+=("ffmpeg $v: image build failed ($base), not run")
                    continue
                fi
                RESULTS+=("ffmpeg $v: image build FAILED ($base)")
                OVERALL=1
                continue
            fi
        fi
        out="$REPO_ROOT/build/docker/ffmpeg/$v"
        mkdir -p "$out"
        if docker run --rm -v "$REPO_ROOT:/src:ro" -v "$out:/out" --cpus="${DOCKER_CPUS:-8}" \
            "$image" bash /src/test/docker/in_container.sh ffmpeg "$v" "$@"; then
            RESULTS+=("ffmpeg $v ($base): OK")
        else
            RESULTS+=("ffmpeg $v ($base): FAILED, see $out")
            OVERALL=1
        fi
    done
    echo
    printf '%s\n' "${RESULTS[@]}"
    exit $OVERALL
fi

case "$PLATFORM" in
    linux)
        IMAGE=flutter-soloud-test-linux
        DOCKERFILE="$DOCKER_DIR/linux.Dockerfile"
        PLATFORM_ARGS=()
        ;;
    windows)
        IMAGE=flutter-soloud-test-windows
        DOCKERFILE="$DOCKER_DIR/windows-wine.Dockerfile"
        PLATFORM_ARGS=(--platform linux/amd64)
        ;;
    *) echo "unknown platform: $PLATFORM" >&2; exit 2 ;;
esac

if [ "${REBUILD:-0}" == "1" ] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build ${PLATFORM_ARGS[@]+"${PLATFORM_ARGS[@]}"} -t "$IMAGE" -f "$DOCKERFILE" "$DOCKER_DIR"
fi

OUT_DIR="$REPO_ROOT/build/docker/$PLATFORM"
mkdir -p "$OUT_DIR"

TTY=()
[ "$WHAT" == "shell" ] && TTY=(-it)

# The prebuilt VST3 SDK (headers and per-platform libraries) is not copied
# with the repository (.dart_tool/ is excluded): mount it when present.
# VST3_PREBUILD_DIR on the host overrides the location.
VST3=()
VST3_HOST="${VST3_PREBUILD_DIR:-$REPO_ROOT/.dart_tool/flutter_soloud/vst3/prebuild}"
if [ -d "$VST3_HOST/include/pluginterfaces" ]; then
    VST3=(-v "$VST3_HOST:/vst3:ro" -e VST3_PREBUILD_DIR=/vst3)
fi

exec docker run --rm ${TTY[@]+"${TTY[@]}"} ${PLATFORM_ARGS[@]+"${PLATFORM_ARGS[@]}"} \
    -v "$REPO_ROOT:/src:ro" \
    -v "$OUT_DIR:/out" \
    -e WHAT="$WHAT" \
    ${VST3[@]+"${VST3[@]}"} \
    --cpus="${DOCKER_CPUS:-8}" \
    "$IMAGE" bash /src/test/docker/in_container.sh "$PLATFORM" "$WHAT" "$@"
