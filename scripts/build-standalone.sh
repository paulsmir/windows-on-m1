#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DRY_RUN=${BUILD_STANDALONE_DRY_RUN:-0}
BUILD_TARGET=RELEASE
M1N1_RELEASE=1
PROFILE=release
ARTIFACT_PROFILE=release
MANIFEST_DIRTY=
CHECK_PYTHON=0
DISPLAY=physical
DEBUG=off
M1N1_DIAG=
VALIDATED_DARWIN_CLANG='Homebrew clang version 22.1.8'

usage() {
    echo "usage: $0 [--release|--debug-build] [--check-python]" >&2
    echo "          [--display none|physical|virtual|both] [--debug off|uart|full|monitor]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --release) BUILD_TARGET=RELEASE; M1N1_RELEASE=1; PROFILE=release; shift ;;
        --debug-build) BUILD_TARGET=DEBUG; M1N1_RELEASE=; PROFILE=debug; MANIFEST_DIRTY=--allow-dirty; shift ;;
        --check-python) CHECK_PYTHON=1; shift ;;
        --display) [ "$#" -ge 2 ] || usage; DISPLAY=$2; shift 2 ;;
        --debug) [ "$#" -ge 2 ] || usage; DEBUG=$2; shift 2 ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

case "$DISPLAY" in none|physical|virtual|both) ;; *) usage ;; esac
case "$DEBUG" in off|uart|full|monitor) ;; *) usage ;; esac
if [ "$PROFILE" = debug ] && [ "$DEBUG" = full ]; then
    M1N1_DIAG="DIAG_TRAP_WFX=1 RUNTIME_DIAG_VERBOSE=1"
fi
if [ "$PROFILE" = debug ]; then
    case "$DEBUG" in
        off) ARTIFACT_PROFILE=debug-off ;;
        uart) ARTIFACT_PROFILE=debug-uart ;;
        full) ARTIFACT_PROFILE=debug-forensic ;;
        monitor) ARTIFACT_PROFILE=debug-monitor ;;
    esac
fi
if [ "$PROFILE" = release ] && { [ "$DISPLAY" != physical ] || [ "$DEBUG" != off ]; }; then
    echo "release profile is fixed to --display physical --debug off; use --debug-build for diagnostics" >&2
    exit 2
fi

CONTAINER_MODE=${STANDALONE_BUILD_CONTAINER:-auto}
SKIP_MU=${STANDALONE_SKIP_MU:-0}
MU_ONLY=${STANDALONE_BUILD_MU_ONLY:-0}
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
        echo "docker run --rm -e STANDALONE_IN_CONTAINER=1 -e STANDALONE_BUILD_MU_ONLY=1 -v <git-worktree-root>:/work -v <git-worktree-root>:<git-worktree-root> -w <container-repository-root> $IMAGE scripts/build-standalone.sh ${M1N1_RELEASE:+--release }--display $DISPLAY --debug $DEBUG"
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
        if [ "$BUILD_TARGET" = RELEASE ]; then
            set -- "$@" --release
        else
            set -- "$@" --debug-build
        fi
        set -- "$@" --display "$DISPLAY" --debug "$DEBUG"
        docker run --rm \
            -e STANDALONE_IN_CONTAINER=1 \
            -e STANDALONE_BUILD_MU_ONLY=1 \
            -v "$MOUNT_ROOT:/work" \
            -v "$MOUNT_ROOT:$MOUNT_ROOT" \
            -w "$CONTAINER_ROOT" \
            "$IMAGE" "$@"
        SKIP_MU=1
    fi
fi

if [ "$(uname -s)" = Darwin ]; then
    RUSTUP_BIN=$(brew --prefix rustup 2>/dev/null)/bin
    [ ! -d "$RUSTUP_BIN" ] || PATH="$RUSTUP_BIN:$PATH"
    export PATH
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
mkdir -p dist/j313/$ARTIFACT_PROFILE
create temporary sibling for dist/j313/$ARTIFACT_PROFILE
make -C m1n1_windows clean
make -C m1n1_windows -j$JOBS ${M1N1_RELEASE:+RELEASE=1} $M1N1_DIAG EXTRA_CFLAGS=-DM1N1_STAGE0
copy m1n1_windows/build/m1n1.bin to dist/j313/$ARTIFACT_PROFILE/m1n1-stage0.bin
make -C m1n1_windows clean
make -C m1n1_windows -j$JOBS ${M1N1_RELEASE:+RELEASE=1} $M1N1_DIAG EXTRA_CFLAGS=-DM1N1_STAGE1
copy m1n1_windows/build/m1n1.bin to dist/j313/$ARTIFACT_PROFILE/m1n1-stage1.bin
make -C m1n1_windows clean
build plain chainload m1n1.macho
make -C m1n1_windows -j$JOBS ${M1N1_RELEASE:+RELEASE=1} $M1N1_DIAG
copy plain m1n1.macho to dist/j313/$ARTIFACT_PROFILE/m1n1.macho
python3 tools/generate_guest_layout.py --check
python3 tools/pack_boot.py --stage0-m1n1 dist/j313/$ARTIFACT_PROFILE/m1n1-stage0.bin --stage1-m1n1 dist/j313/$ARTIFACT_PROFILE/m1n1-stage1.bin --firmware mu/Build/MacBookAirMid2020-AARCH64/${BUILD_TARGET}_CLANGPDB/FV/J313MACBOOKAIRMID2020_EFI.fd --layout config/j313-guest-layout.json --output dist/j313/$ARTIFACT_PROFILE/boot.bin --display $DISPLAY --debug $DEBUG --source-commit <m1n1-source-commit> --compiler <compiler-identity>
PYTHONPATH=. python3 -c 'from pathlib import Path; from bootstrap_image import parse_bootstrap; from standalone_image import parse_image; outer, inner = parse_bootstrap(Path("dist/j313/$ARTIFACT_PROFILE/boot.bin").read_bytes()); nested, firmware = parse_image(inner); assert outer.flags == nested.flags; print("validated outer parse_bootstrap and nested parse_image")'
copy m1n1.macho and J313_EFI.fd to dist/j313/$ARTIFACT_PROFILE
write dist/j313/$ARTIFACT_PROFILE/SHA256SUMS and dist/j313/$ARTIFACT_PROFILE/MANIFEST.json
python3 tools/artifact_manifest.py create ${MANIFEST_DIRTY:+--allow-dirty }--root <repository-root> --directory dist/j313/$ARTIFACT_PROFILE --profile $PROFILE
publish complete profile atomically to dist/j313/$ARTIFACT_PROFILE
EOF
    exit 0
fi

cd "$ROOT"
git submodule update --init --recursive

M1N1_SOURCE_COMMIT=$(git -C "$ROOT/m1n1_windows" rev-parse HEAD)
if [ "$(uname -s)" = Darwin ]; then
    M1N1_COMPILER=$("$(brew --prefix llvm)/bin/clang" --version | sed -n '1p')
    if [ "$M1N1_COMPILER" != "$VALIDATED_DARWIN_CLANG" ]; then
        if [ "$BUILD_TARGET" = RELEASE ] || [ "${ALLOW_UNVALIDATED_CLANG:-0}" != 1 ]; then
            echo "m1n1 requires $VALIDATED_DARWIN_CLANG; found: $M1N1_COMPILER" >&2
            exit 2
        fi
        echo "WARNING: unvalidated development compiler: $M1N1_COMPILER" >&2
    fi
else
    M1N1_COMPILER=$(clang --version 2>/dev/null | sed -n '1p' || true)
fi
[ -n "$M1N1_COMPILER" ] || M1N1_COMPILER=unknown

if [ "$SKIP_MU" != 1 ]; then
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
fi

[ "$MU_ONLY" != 1 ] || exit 0

PROFILE_PARENT="$ROOT/dist/j313"
FINAL_DIST="$PROFILE_PARENT/$ARTIFACT_PROFILE"
mkdir -p "$PROFILE_PARENT"
DIST=$(mktemp -d "$PROFILE_PARENT/.${ARTIFACT_PROFILE}.new.XXXXXX")
cleanup_staging() {
    [ ! -d "$DIST" ] || rm -rf "$DIST"
}
trap cleanup_staging EXIT HUP INT TERM

(
    cd "$ROOT/m1n1_windows"
    make clean
    if [ -n "$M1N1_RELEASE" ]; then
        make -j"$JOBS" RELEASE=1 EXTRA_CFLAGS=-DM1N1_STAGE0
    else
        make -j"$JOBS" $M1N1_DIAG EXTRA_CFLAGS=-DM1N1_STAGE0
    fi
    cp build/m1n1.bin "$DIST/m1n1-stage0.bin"

    make clean
    if [ -n "$M1N1_RELEASE" ]; then
        make -j"$JOBS" RELEASE=1 EXTRA_CFLAGS=-DM1N1_STAGE1
    else
        make -j"$JOBS" $M1N1_DIAG EXTRA_CFLAGS=-DM1N1_STAGE1
    fi
    cp build/m1n1.bin "$DIST/m1n1-stage1.bin"

    # The stage1 Mach-O is not a chainload image.  Build the assisted-launch
    # artifact separately so the host can never accidentally chainload a
    # binary compiled with M1N1_STAGE1 semantics.
    make clean
    if [ -n "$M1N1_RELEASE" ]; then
        make -j"$JOBS" RELEASE=1
    else
        make -j"$JOBS" $M1N1_DIAG
    fi
    cp build/m1n1.macho "$DIST/m1n1.macho"
    ./tests/run_host_tests.sh
)

python3 "$ROOT/tools/generate_guest_layout.py" --check

FD="$ROOT/mu/Build/MacBookAirMid2020-AARCH64/${BUILD_TARGET}_CLANGPDB/FV/J313MACBOOKAIRMID2020_EFI.fd"
python3 "$ROOT/tools/pack_boot.py" \
    --stage0-m1n1 "$DIST/m1n1-stage0.bin" \
    --stage1-m1n1 "$DIST/m1n1-stage1.bin" \
    --firmware "$FD" \
    --layout "$ROOT/config/j313-guest-layout.json" \
    --output "$DIST/boot.bin" \
    --display "$DISPLAY" \
    --debug "$DEBUG" \
    --source-commit "$M1N1_SOURCE_COMMIT" \
    --compiler "$M1N1_COMPILER"
PYTHONPATH="$ROOT" python3 -c \
    'from pathlib import Path; import sys; from bootstrap_image import parse_bootstrap; from standalone_image import parse_image; outer, inner = parse_bootstrap(Path(sys.argv[1]).read_bytes()); nested, firmware = parse_image(inner); assert outer.flags == nested.flags; print(f"Validated outer flags={outer.flags:#x}, nested flags={nested.flags:#x}, firmware={len(firmware)} bytes")' \
    "$DIST/boot.bin"
cp "$FD" "$DIST/J313_EFI.fd"

if [ "$PROFILE" = release ]; then
    python3 "$ROOT/tools/check_release_binary.py" "$DIST/m1n1.macho"
fi

(
    cd "$DIST"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum boot.bin m1n1-stage0.bin m1n1-stage1.bin m1n1.macho J313_EFI.fd >SHA256SUMS
    else
        shasum -a 256 boot.bin m1n1-stage0.bin m1n1-stage1.bin m1n1.macho J313_EFI.fd >SHA256SUMS
    fi
)
python3 "$ROOT/tools/artifact_manifest.py" create $MANIFEST_DIRTY \
    --root "$ROOT" --directory "$DIST" --profile "$PROFILE" \
    --display "$DISPLAY" --debug "$DEBUG" --compiler "$M1N1_COMPILER" \
    boot.bin m1n1-stage0.bin m1n1-stage1.bin m1n1.macho J313_EFI.fd

BACKUP="$PROFILE_PARENT/.${PROFILE}.old.$$"
if [ -e "$FINAL_DIST" ]; then
    mv "$FINAL_DIST" "$BACKUP"
fi
if mv "$DIST" "$FINAL_DIST"; then
    DIST=
    [ ! -e "$BACKUP" ] || rm -rf "$BACKUP"
else
    [ ! -e "$BACKUP" ] || mv "$BACKUP" "$FINAL_DIST"
    exit 1
fi
trap - EXIT HUP INT TERM
echo "Standalone artifacts: $FINAL_DIST"
