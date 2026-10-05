#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DOCKERFILE="$ROOT/tools/agx-capture-container/Dockerfile"
IMAGE=windows-on-m1-agx-capture:mesa-7a4f2406
OUTPUT=
BUILD_CACHE_ARG=

if [ "${AGX_CAPTURE_ENV_NO_CACHE:-0}" = 1 ]; then
    BUILD_CACHE_ARG=--no-cache
fi

usage()
{
    echo "usage: $0 --output DIRECTORY" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --output)
            [ "$#" -ge 2 ] || usage
            OUTPUT=$2
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "unknown option: $1" >&2
            exit 2
            ;;
    esac
done

[ -n "$OUTPUT" ] || usage

if [ "${AGX_CAPTURE_ENV_DRY_RUN:-0}" = 1 ]; then
    cat <<EOF
docker build${BUILD_CACHE_ARG:+ $BUILD_CACHE_ARG} -t $IMAGE -f tools/agx-capture-container/Dockerfile <repository-root>
docker create $IMAGE
copy /opt/agx-capture/export from the stopped container
docker cp <container>:/opt/agx-capture/export <temporary-directory>
verify-agx-capture-env.py <temporary-directory>/export
publish atomically to $OUTPUT
EOF
    exit 0
fi

case "$OUTPUT" in
    /*) ;;
    *) OUTPUT=$(pwd)/$OUTPUT ;;
esac
[ ! -e "$OUTPUT" ] || {
    echo "output already exists: $OUTPUT" >&2
    exit 2
}

PARENT=$(dirname -- "$OUTPUT")
mkdir -p "$PARENT"
TEMP=$(mktemp -d "$PARENT/.agx-capture-env.XXXXXX")
CONTAINER=
cleanup()
{
    if [ -n "$CONTAINER" ]; then
        docker rm -f "$CONTAINER" >/dev/null 2>&1 || true
    fi
    rm -rf "$TEMP"
}
trap cleanup EXIT HUP INT TERM

docker build $BUILD_CACHE_ARG -t "$IMAGE" -f "$DOCKERFILE" "$ROOT"
CONTAINER=$(docker create "$IMAGE")
docker cp "$CONTAINER:/opt/agx-capture/export" "$TEMP/export"
"$ROOT/proxyenv/bin/python" "$ROOT/tools/verify-agx-capture-env.py" "$TEMP/export"
mv "$TEMP/export" "$OUTPUT"
echo "published AGX capture environment: $OUTPUT"
