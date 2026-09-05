#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/ans-common.XXXXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$here/../ans_scsi.c" "$here/../ans_prp.c" "$here/test_common.c" \
    -o "$build/test_common"
"$build/test_common"
