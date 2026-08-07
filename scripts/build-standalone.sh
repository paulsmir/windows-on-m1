#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DRY_RUN=${BUILD_STANDALONE_DRY_RUN:-0}
BUILD_TARGET=DEBUG
M1N1_RELEASE=
CHECK_PYTHON=0
DISPLAY=physical
DEBUG=off

usage() {
    echo "usage: $0 [--release] [--check-python]" >&2
    echo "          [--display none|physical|virtual|both] [--debug off|uart|full|monitor]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --release) BUILD_TARGET=RELEASE; M1N1_RELEASE=1; shift ;;
        --check-python) CHECK_PYTHON=1; shift ;;
        --display) [ "$#" -ge 2 ] || usage; DISPLAY=$2; shift 2 ;;
        --debug) [ "$#" -ge 2 ] || usage; DEBUG=$2; shift 2 ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

case "$DISPLAY" in none|physical|virtual|both) ;; *) usage ;; esac
case "$DEBUG" in off|uart|full|monitor) ;; *) usage ;; esac

CONTAINER_MODE=${STANDALONE_BUILD_CONTAINER:-auto}
USE_CONTAINER=0
case "$CONTAINER_MODE" in
    auto)
        if [ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ]; then
            USE_CONTAINER=1
        fi
        ;;
    always)
        USE_CONTAINER=1
        ;;
    never) ;;
    *)
        echo "STANDALONE_BUILD_CONTAINER must be auto, always, or never" >&2
        exit 2
        ;;
esac

if [ "$USE_CONTAINER" = 1 ] && [ "$CHECK_PYTHON" = 0 ] && [ "${STANDALONE_IN_CONTAINER:-0}" != 1 ]; then
    IMAGE=windows-on-m1-build:local
    if [ "$DRY_RUN" = 1 ]; then
        echo "docker build -t $IMAGE -f <repository-root>/Dockerfile.build <repository-root>"
        echo "docker run --rm -e STANDALONE_IN_CONTAINER=1 -v <git-worktree-root>:/work -v <git-worktree-root>:<git-worktree-root> -w <container-repository-root> $IMAGE scripts/build-standalone.sh ${M1N1_RELEASE:+--release }--display $DISPLAY --debug $DEBUG"
    else
        COMMON_DIR=$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir)
        MOUNT_ROOT=$(dirname "$COMMON_DIR")
        case "$ROOT" in
            "$MOUNT_ROOT") CONTAINER_ROOT=/work ;;
            "$MOUNT_ROOT"/*) CONTAINER_ROOT=/work${ROOT#"$MOUNT_ROOT"} ;;
            *)
                echo "repository root is outside Git worktree root: $ROOT" >&2
                exit 2
                ;;
        esac
        docker build -t "$IMAGE" -f "$ROOT/Dockerfile.build" "$ROOT"
        set -- "$CONTAINER_ROOT/scripts/build-standalone.sh"
        [ "$BUILD_TARGET" != RELEASE ] || set -- "$@" --release
        set -- "$@" --display "$DISPLAY" --debug "$DEBUG"
        docker run --rm \
            -e STANDALONE_IN_CONTAINER=1 \
            -v "$MOUNT_ROOT:/work" \
            -v "$MOUNT_ROOT:$MOUNT_ROOT" \
            -w "$CONTAINER_ROOT" \
            "$IMAGE" "$@"
        exit
    fi
fi

python_is_compatible() {
    command -v "$1" >/dev/null 2>&1 &&
        "$1" -c 'import sys; raise SystemExit(0 if (3, 10) <= sys.version_info[:2] < (3, 13) else 1)' \
            >/dev/null 2>&1
}

MU_PYTHON_SELECTED='<python3.10-through-3.12>'
if [ "$DRY_RUN" != 1 ] || [ "$CHECK_PYTHON" = 1 ]; then
    MU_PYTHON_SELECTED=
    if [ -n "${MU_PYTHON:-}" ]; then
        if ! python_is_compatible "$MU_PYTHON"; then
            echo "Mu build requires Python >=3.10 and <3.13; MU_PYTHON=$MU_PYTHON is incompatible" >&2
            exit 2
        fi
        MU_PYTHON_SELECTED=$MU_PYTHON
    else
        for candidate in python3.12 python3.11 python3.10 python3.9 /usr/bin/python3 python3; do
            if python_is_compatible "$candidate"; then
                MU_PYTHON_SELECTED=$(command -v "$candidate")
                break
            fi
        done
        if [ -z "$MU_PYTHON_SELECTED" ]; then
            echo "Mu build requires Python >=3.10 and <3.13; set MU_PYTHON to a compatible interpreter" >&2
            exit 2
        fi
    fi
fi

if [ "$CHECK_PYTHON" = 1 ]; then
    VERSION=$($MU_PYTHON_SELECTED -c 'import sys; print(".".join(map(str, sys.version_info[:3])))')
    echo "$MU_PYTHON_SELECTED $VERSION"
    exit 0
fi

if command -v nproc >/dev/null 2>&1; then
    JOBS=$(nproc)
else
    JOBS=$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
fi

if [ "$DRY_RUN" = 1 ]; then
    cat <<EOF
cd <repository-root>
git submodule update --init --recursive
$MU_PYTHON_SELECTED -m venv .build/mu-venv
.build/mu-venv/bin/stuart_setup -c Platform/MacBookAirMid2020Pkg/PlatformBuild.py TOOL_CHAIN_TAG=CLANGPDB
.build/mu-venv/bin/stuart_update -c Platform/MacBookAirMid2020Pkg/PlatformBuild.py TOOL_CHAIN_TAG=CLANGPDB
.build/mu-venv/bin/stuart_build -c Platform/MacBookAirMid2020Pkg/PlatformBuild.py TOOL_CHAIN_TAG=CLANGPDB TARGET=$BUILD_TARGET BLD_*_AIC_BUILD=FALSE
make -j$JOBS ${M1N1_RELEASE:+RELEASE=1}
python3 tools/generate_guest_layout.py --check
python3 tools/pack_boot.py --m1n1 m1n1_windows/build/m1n1.bin --firmware mu/Build/MacBookAirMid2020-AARCH64/${BUILD_TARGET}_CLANGPDB/FV/J313MACBOOKAIRMID2020_EFI.fd --layout config/j313-guest-layout.json --output dist/j313/boot.bin --display $DISPLAY --debug $DEBUG
copy m1n1.macho and J313_EFI.fd to dist/j313
write dist/j313/SHA256SUMS
EOF
    exit 0
fi

cd "$ROOT"
git submodule update --init --recursive

VENV="$ROOT/.build/mu-venv"
if [ ! -x "$VENV/bin/python" ] || ! python_is_compatible "$VENV/bin/python"; then
    "$MU_PYTHON_SELECTED" -m venv --clear "$VENV"
fi
"$VENV/bin/pip" install -q --upgrade pip
"$VENV/bin/pip" install -q -r "$ROOT/mu/pip-requirements.txt"
"$VENV/bin/pip" install -q "setuptools<81" wheel

PLATFORM=Platform/MacBookAirMid2020Pkg/PlatformBuild.py
(
    cd "$ROOT/mu"
    "$VENV/bin/stuart_setup" -c "$PLATFORM" TOOL_CHAIN_TAG=CLANGPDB
    rm -f MU_BASECORE/BaseTools/Bin/nasm_ext_dep.yaml \
        MU_BASECORE/BaseTools/Bin/iasl_ext_dep.yaml \
        MU_BASECORE/.pytool/Plugin/UncrustifyCheck/uncrustify_ext_dep.yaml
    "$VENV/bin/stuart_update" -c "$PLATFORM" TOOL_CHAIN_TAG=CLANGPDB
    "$VENV/bin/stuart_build" -c "$PLATFORM" TOOL_CHAIN_TAG=CLANGPDB \
        "TARGET=$BUILD_TARGET" 'BLD_*_AIC_BUILD=FALSE'
)

(
    cd "$ROOT/m1n1_windows"
    if [ -n "$M1N1_RELEASE" ]; then
        make -j"$JOBS" RELEASE=1
    else
        make -j"$JOBS"
    fi
    ./tests/run_host_tests.sh
)

python3 "$ROOT/tools/generate_guest_layout.py" --check

DIST="$ROOT/dist/j313"
FD="$ROOT/mu/Build/MacBookAirMid2020-AARCH64/${BUILD_TARGET}_CLANGPDB/FV/J313MACBOOKAIRMID2020_EFI.fd"
mkdir -p "$DIST"
python3 "$ROOT/tools/pack_boot.py" \
    --m1n1 "$ROOT/m1n1_windows/build/m1n1.bin" \
    --firmware "$FD" \
    --layout "$ROOT/config/j313-guest-layout.json" \
    --output "$DIST/boot.bin" \
    --display "$DISPLAY" \
    --debug "$DEBUG"
cp "$ROOT/m1n1_windows/build/m1n1.macho" "$DIST/m1n1.macho"
cp "$FD" "$DIST/J313_EFI.fd"

(
    cd "$DIST"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum boot.bin m1n1.macho J313_EFI.fd >SHA256SUMS
    else
        shasum -a 256 boot.bin m1n1.macho J313_EFI.fd >SHA256SUMS
    fi
)
echo "Standalone artifacts: $DIST"
