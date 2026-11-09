#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
BUILD_ROOT=$(mktemp -d)
trap 'rm -rf "$BUILD_ROOT"' EXIT HUP INT TERM
CC=${CC:-cc}
for name in transport identify request resources telemetry; do
    extras=""
    if [ "$name" = request ]; then extras="../common/ans_scsi.c ../common/ans_prp.c"; fi
    # Fixed source lists, not caller-provided shell fragments.
    $CC -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
        "tests/${name}_test.c" "$name.c" $extras -o "$BUILD_ROOT/$name"
    "$BUILD_ROOT/$name"
done

python3 tests/reset_driver_test.py
python3 tests/dump_mode_test.py
python3 tests/shutdown_serves_io_test.py
python3 tests/boot_mode_test.py
python3 tests/dual_boot_test.py
python3 tests/install_test.py
