#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
BUILD_ROOT=$(mktemp -d)
trap 'rm -rf "$BUILD_ROOT"' EXIT HUP INT TERM
CC=${CC:-cc}
for name in transport identify request resources telemetry dump; do
    extras=""
    if [ "$name" = request ]; then extras="../common/ans_scsi.c ../common/ans_prp.c"; fi
    # Fixed source lists, not caller-provided shell fragments.
    $CC -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
        "tests/${name}_test.c" "$name.c" $extras -o "$BUILD_ROOT/$name"
    "$BUILD_ROOT/$name"
done

# Storport shim harness: the real miniport sources against a fake Storport and controller.
$CC -std=c11 -Wall -Wextra -Werror -Wno-unknown-pragmas -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -DAPPLE_ANS_NATIVE_READ_ONLY=1 -DAPPLE_ANS_NATIVE_WRITE=1 \
    -DAPPLE_ANS_QUALIFICATION_STOP=0 -Itests/shim tests/storport_shim_test.c \
    driver.c native.c direct.c telemetry.c transport.c identify.c request.c resources.c dump.c \
    ../common/ans_scsi.c ../common/ans_prp.c ../common/ans_order.c ../common/ans_direct.c \
    ../common/ans_trim.c ../common/ans_reject.c ../common/ans_bounce.c -o "$BUILD_ROOT/storport_shim"
"$BUILD_ROOT/storport_shim"

python3 tests/reset_driver_test.py
python3 tests/dump_mode_test.py
python3 tests/shutdown_serves_io_test.py
python3 tests/unmap_wiring_test.py
python3 tests/bounce_wiring_test.py
python3 tests/boot_mode_test.py
python3 tests/dual_boot_test.py
python3 tests/install_test.py
