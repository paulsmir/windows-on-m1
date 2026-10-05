TASK_ID:
AD04-OFFT-PORTABILITY-004

INPUT_COMMIT:
a39fc611da1612c05a93bf02846c595ce6958acf

RESULT:
PASS for proven internal compiler branch-offset portability boundary; stop at next blocker.

OFF_T_FIRST_DIAGNOSTIC:
`agx_compiler.h:478: off_t offset, last_offset;` reached through
`agx_compile.c -> agx_compile.h/agx_compiler.h`; related `agx_pack.c` has
branch-fixup off_t offset and off_t target uses.

OWNER_FILE:
src/asahi/compiler/agx_compiler.h and src/asahi/compiler/agx_pack.c

OWNER_LINE:
agx_compiler.h 478; agx_pack.c 12 and 1197

INCLUDE_CHAIN:
agx_compile.c -> agx_compile.h -> agx_compiler.h; agx_pack.c includes
agx_compiler.h. No POSIX file API is involved in these branch offsets.

OWNER_CLASSIFICATION:
Internal emitted-binary branch offset (runtime compiler value), not a file offset,
mmap contract or large-file capability check.

REQUIRED_WIDTH:
32-bit unsigned offset storage, matching util_dynarray.size (unsigned). The
branch displacement remains explicitly int32_t after subtracting offsets.

REQUIRED_SEMANTICS:
Nonnegative byte offsets within the emitted binary; unsigned storage for block,
fixup and target offset values; signed int32_t branch displacement calculation.

ROOT_CAUSE:
POSIX `off_t` was used for an internal compiler byte offset without a Windows
definition. Windows build has no required file-offset semantics at this owner.

WINDOWS_EXISTING_ABSTRACTION_FOUND:
YES: util_dynarray.size is the existing unsigned byte-size abstraction used at
every assignment to block offsets.

IMPLEMENTATION_MECHANISM:
Hash-pinned deterministic Windows-derived overlay changes only three proven
internal declarations from off_t to unsigned: agx_block offset/last_offset,
agx_branch_fixup offset, and agx_fixup_branch target. Pinned source unchanged.

GLOBAL_OFF_T_TYPEDEF_USED:
NO

PINNED_SOURCE_CHANGED:
NO

X64_RESULT:
Focused internal width probe COMPILE=0. Exact derived agx_compile control no
longer reports off_t; it reaches UTIL_LUT2.

ARM64_RESULT:
Focused internal width probe COMPILE=0. Exact derived agx_compile control no
longer reports off_t; it reaches UTIL_LUT2.

TESTS:
RED is the prior exact off_t diagnostic. GREEN is the x64 and ARM64 derived
control plus width/branch-patch probe. Source hash guards and diff check pass.

FRESH_COMPILER_RESULT:
off_t unknown type is removed from the first-error position on x64 and ARM64.

FIRST_NEW_BLOCKER:
UTIL_LUT2 undeclared in generated agx_builder.h. It was not modified.

AGX_INDEX_REGRESSION:
NO

HARDWARE_USED:
NO

WORKER_COMMIT:
3416c77dded5a543792259254c3625169e84e8e3

RAW_EVIDENCE_PATHS:
investigation/evidence/AD04-offt-portability-004/

CONTRACT_VIOLATIONS:
NONE
