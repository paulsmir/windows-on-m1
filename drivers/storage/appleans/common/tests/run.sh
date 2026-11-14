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

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$here/../ans_order.c" "$here/test_order.c" -o "$build/test_order"
"$build/test_order"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$here/../ans_direct.c" "$here/test_direct.c" -o "$build/test_direct"
"$build/test_direct"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$here/../ans_scsi.c" "$here/../ans_trim.c" "$here/test_trim.c" -o "$build/test_trim"
"$build/test_trim"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$here/../ans_reject.c" "$here/test_reject.c" -o "$build/test_reject"
"$build/test_reject"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$here/../ans_bounce.c" "$here/../ans_scsi.c" "$here/../ans_prp.c" "$here/../../windows/request.c" \
    "$here/test_bounce.c" -o "$build/test_bounce"
"$build/test_bounce"
