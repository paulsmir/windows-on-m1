TASK_ID:
AD04-UTIL-LUT2-PORTABILITY-005

INPUT_COMMIT:
ec2b22d679f0e12cad540ff2769069c9c5db0b08

RESULT:
PASS for the UTIL_LUT2 portability boundary; stop at the next compiler blocker.

FIRST_DIAGNOSTIC:
Generated agx_builder.h calls UTIL_LUT2; pinned util/lut.h hides its definition
under `#if !defined(_MSC_VER)` while clang-cl defines _MSC_VER.

UTIL_LUT2_OWNER:
src/util/lut.h; consumers are agx_builder.h.py, agx_lower_pseudo.c and
agx_optimizer.c. agx_optimizer.c also reaches util_lut2_invert_source.

INCLUDE_CHAIN:
agx_compile.c -> generated agx_builder.h -> util/lut.h.

MSC_GUARD_CONFIRMED:
YES. RED guard probe with pinned header fails UTIL_LUT2_NOT_DEFINED; derived
guard probe passes.

CLANG_CL_STATEMENT_EXPR:
PASS

CLANG_CL_BUILTIN_CTZ:
PASS

X64_LUT_SEMANTICS:
PASS; compile and execute exact feature probe.

ARM64_COMPILE:
PASS; exact feature probe compile-only.

NATIVE_ORACLE_CASES:
13 LUT2 expressions plus 3 LUT2 inversion-source cases, compared against an
independent scalar truth-table reference.

LUT_ORACLE_MATCH:
13/13 LUT expressions and 3/3 inversion helper cases.

INVERT_HELPER_RESULT:
PASS on x64; compile path PASS on ARM64.

EXISTING_MESA_COMPILER_ABSTRACTION:
NONE suitable for distinguishing real MSVC from clang-cl. detect_cc.h exposes
DETECT_CC_MSVC but does not identify clang separately; __clang__ is the minimum
proven condition.

IMPLEMENTATION_MECHANISM:
Hash-pinned deterministic derived util/lut.h overlay changes only the guard to:
`#if !defined(_MSC_VER) || defined(__clang__)`. Canonical LUT formulas and
helpers remain byte/source identical.

PINNED_UTIL_LUT_CHANGED:
NO

DERIVED_OVERLAY_DIFF:
One preprocessor guard line only; pinned util/lut.h SHA256
23caf6713955e6d9ad6c4300a83a0421cc92e6bcef23688216172f98c9bef456.

AGX_CONSUMER_FILES_CHANGED:
NO

X64_FRESH_COMPILER_RESULT:
UTIL_LUT2 undeclared is gone with all accepted overlays; exact control stops at
the next later compiler diagnostic.

ARM64_FRESH_COMPILER_RESULT:
UTIL_LUT2 undeclared is gone; compile-only control reaches the same later point.

FIRST_NEW_BLOCKER:
The next diagnostic after UTIL_LUT2 is the later compiler boundary already
visible in the fresh control; it was not modified.

UATOMIC_REGRESSION:
NO

AGX_INDEX_REGRESSION:
NO

OFF_T_REGRESSION:
NO

HARDWARE_USED:
NO

WORKER_COMMIT:
PENDING (filled after implementation commit)

RAW_EVIDENCE_PATHS:
investigation/evidence/AD04-util-lut2-portability-005/

CONTRACT_VIOLATIONS:
NONE
