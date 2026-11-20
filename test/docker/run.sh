#!/bin/bash
# Run the test suites in Docker containers.
#
#   ./test/docker/run.sh linux   [all|scripts|ctest|dart|shell] [-- extra args]
#   ./test/docker/run.sh windows [all|ctest|native|shell]
#
#   linux    ubuntu 24.04, host architecture: native scripts (test/common +
#            test/linux), the CMake suite (ctest, plus ASan), flutter test.
#            A PulseAudio null sink provides an output device.
#   windows  mingw-w64 cross build run under Wine (linux/amd64 image,
#            emulated on Apple silicon): the CMake suite and the
#            test/windows sources. An approximation of Windows, see
#            windows-wine.Dockerfile.
#
# The repository is mounted read-only and copied into the container, so the
# host's .dart_tool/ and build/ are never touched. Logs and results go to
# build/docker/<platform>/ on the host. Set REBUILD=1 to rebuild the image.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DOCKER_DIR="$REPO_ROOT/test/docker"
PLATFORM="${1:?usage: run.sh linux|windows [what]}"
WHAT="${2:-all}"
shift $(( $# >= 2 ? 2 : $# ))
[ "${1:-}" == "--" ] && shift

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

exec docker run --rm ${TTY[@]+"${TTY[@]}"} ${PLATFORM_ARGS[@]+"${PLATFORM_ARGS[@]}"} \
    -v "$REPO_ROOT:/src:ro" \
    -v "$OUT_DIR:/out" \
    -e WHAT="$WHAT" \
    --cpus="${DOCKER_CPUS:-8}" \
    "$IMAGE" bash /src/test/docker/in_container.sh "$PLATFORM" "$WHAT" "$@"
