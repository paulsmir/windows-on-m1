#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DRY_RUN=0
if [ "${1:-}" = "--dry-run" ]; then
    DRY_RUN=1
    shift
fi

ACTION=${1:-}
[ "$#" -gt 0 ] && shift
DISK=
IMAGE=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --disk)
            DISK=${2:-}
            shift 2
            ;;
        --image)
            IMAGE=${2:-}
            shift 2
            ;;
        *)
            echo "unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

usage() {
    echo "usage: sudo $0 {inspect|install|restore} --disk diskXsY [--image boot.bin]" >&2
    exit 2
}

case "$ACTION" in inspect|install|restore) ;; *) usage ;; esac
case "$DISK" in *[!a-zA-Z0-9]*) usage ;; esac
case "$DISK" in disk[0-9]*s[0-9]*) ;; *) usage ;; esac
if [ "$ACTION" = install ] && [ -z "$IMAGE" ]; then
    usage
fi

if [ "$DRY_RUN" = 1 ]; then
    if [ "$ACTION" = install ]; then
        echo "validate outer bootstrap manifest: $IMAGE"
        echo "validate nested standalone manifest: $IMAGE"
    fi
    echo "diskutil mount $DISK"
    echo "resolve mounted target: <mount-point>/m1n1/boot.bin"
    case "$ACTION" in
        inspect)
            echo "show target and backup SHA-256"
            ;;
        install)
            echo "create backup once: /var/backups/m1n1-windows/$DISK.boot.bin.original"
            echo "copy to temporary sibling: <mount-point>/m1n1/.boot.bin.new"
            echo "verify SHA-256"
            echo "atomic rename to <mount-point>/m1n1/boot.bin"
            ;;
        restore)
            echo "restore backup: /var/backups/m1n1-windows/$DISK.boot.bin.original"
            echo "verify SHA-256"
            ;;
    esac
    exit 0
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "run this command with sudo" >&2
    exit 1
fi

case "$IMAGE" in
    ""|/*) ;;
    *) IMAGE="$ROOT/$IMAGE" ;;
esac

if [ "$ACTION" = install ]; then
    [ -f "$IMAGE" ] || { echo "image not found: $IMAGE" >&2; exit 1; }
    PYTHONPATH="$ROOT" python3 -c \
        'import pathlib, sys; from bootstrap_image import parse_bootstrap; from standalone_image import parse_image; outer, inner = parse_bootstrap(pathlib.Path(sys.argv[1]).read_bytes()); nested, firmware = parse_image(inner); assert outer.flags == nested.flags' \
        "$IMAGE"
    MANIFEST=$(dirname "$IMAGE")/MANIFEST.json
    [ -f "$MANIFEST" ] || { echo "artifact manifest not found: $MANIFEST" >&2; exit 1; }
    python3 "$ROOT/tools/artifact_manifest.py" verify "$MANIFEST"
fi

diskutil mount "$DISK" >/dev/null
MOUNT_POINT=$(diskutil info "$DISK" | awk -F': *' '/Mount Point/ {print $2; exit}')
[ -n "$MOUNT_POINT" ] && [ -d "$MOUNT_POINT" ] || {
    echo "unable to resolve mount point for $DISK" >&2
    exit 1
}
BOOTBIN="$MOUNT_POINT/m1n1/boot.bin"
[ -f "$BOOTBIN" ] || { echo "expected target not found: $BOOTBIN" >&2; exit 1; }

BACKUP_DIR=/var/backups/m1n1-windows
BACKUP="$BACKUP_DIR/$DISK.boot.bin.original"
mkdir -p "$BACKUP_DIR"

hash_file() {
    shasum -a 256 "$1" | awk '{print $1}'
}

install_atomically() {
    source=$1
    destination=$2
    temporary="$(dirname "$destination")/.boot.bin.new.$$"
    trap 'rm -f "$temporary"' EXIT HUP INT TERM
    cp "$source" "$temporary"
    chmod 0644 "$temporary"
    [ "$(hash_file "$source")" = "$(hash_file "$temporary")" ] || {
        echo "temporary copy failed SHA-256 verification" >&2
        exit 1
    }
    mv -f "$temporary" "$destination"
    trap - EXIT HUP INT TERM
    sync
}

case "$ACTION" in
    inspect)
        ls -lh "$BOOTBIN"
        shasum -a 256 "$BOOTBIN"
        if [ -f "$BACKUP" ]; then
            echo "Original backup: $BACKUP"
            shasum -a 256 "$BACKUP"
        else
            echo "Original backup has not been created yet."
        fi
        ;;
    install)
        if [ ! -f "$BACKUP" ]; then
            cp -p "$BOOTBIN" "$BACKUP"
            chmod 0600 "$BACKUP"
            echo "Original backup created: $BACKUP"
        fi
        install_atomically "$IMAGE" "$BOOTBIN"
        [ "$(hash_file "$IMAGE")" = "$(hash_file "$BOOTBIN")" ] || {
            echo "installed image failed SHA-256 verification" >&2
            exit 1
        }
        echo "Installed standalone image: $BOOTBIN"
        shasum -a 256 "$BOOTBIN"
        echo "Rollback: sudo $0 restore --disk $DISK"
        ;;
    restore)
        [ -f "$BACKUP" ] || { echo "backup not found: $BACKUP" >&2; exit 1; }
        install_atomically "$BACKUP" "$BOOTBIN"
        [ "$(hash_file "$BACKUP")" = "$(hash_file "$BOOTBIN")" ] || {
            echo "restored image failed SHA-256 verification" >&2
            exit 1
        }
        echo "Restored original image: $BOOTBIN"
        shasum -a 256 "$BOOTBIN"
        ;;
esac
