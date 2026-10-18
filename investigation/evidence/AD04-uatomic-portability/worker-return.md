TASK_ID:
AD04-UATOMIC-PORTABILITY-001

ROLE:
Tier B implementation worker

INPUT_COMMIT:
f90402c7589236ec9e8a030f4b825b5a36280866

WORKTREE:
/Users/pavel/public_windows/.worktrees/task-AD04-UATOMIC-PORTABILITY-001

RESULT:
PASS (bounded five-intrinsic contract; later blockers intentionally unresolved)

REFERENCE_CONTRACT:
Pinned Mesa u_atomic.h MSVC branch uses five lowercase 64-bit names. FRYZZING
LLVM20 intrin headers declare uppercase Exchange64, ExchangeAdd64, Increment64,
Decrement64. _InterlockedAdd64 is declared only in clang AArch64 intrin.h
section; x64 requires ExchangeAdd64 plus operand to preserve updated-value Add64.

FILES_CHANGED:
- drivers/apple-agx/mesa/windows-overlay/uatomic_clang_compat.h
- drivers/apple-agx/mesa/windows-overlay/uatomic_semantics_test.c
- drivers/apple-agx/mesa/windows-overlay/uatomic_mesa_semantics_test.c
- drivers/apple-agx/mesa/scripts/test-uatomic-overlay.ps1
- drivers/apple-agx/mesa/scripts/test-uatomic-arm64.ps1
- drivers/apple-agx/mesa/scripts/run-uatomic-agx-control.ps1
- investigation/evidence/AD04-uatomic-portability/*

WORKER_COMMIT:
ea0e0d8bbc42483b89cfa81bfb5f9e9079aead4f

DIFF_SUMMARY:
Separate include-first compatibility header aliases four exact Clang/MSVC
uppercase intrinsic names and supplies one inline x64 Add64 wrapper using
ExchangeAdd64+value. No pinned Mesa source was edited.

INTRINSICS_VERIFIED:
- exchange64: _interlockedexchange64 -> _InterlockedExchange64, previous value
- exchangeadd64: _interlockedexchangeadd64 -> _InterlockedExchangeAdd64, previous value
- increment64: _interlockedincrement64 -> _InterlockedIncrement64, updated value
- decrement64: _interlockeddecrement64 -> _InterlockedDecrement64, updated value
- add64: _interlockedadd64 -> inline ExchangeAdd64+value, updated value; direct
  _InterlockedAdd64 unavailable in x64 Clang headers

SEMANTIC_TESTS:
RED standalone test fails with all five lowercase names undeclared.
GREEN standalone executable tests all five operations with distinguishing
previous/updated values: COMPILE=0, RUN=0.
Including full pinned Mesa u_atomic.h reaches an auxiliary lowercase
_interlockedadd 32-bit diagnostic; this is outside the approved five aliases and
was not modified.

X64_RESULT:
PASS for standalone five-operation semantic test. Exact agx_compile control
with overlay no longer reports the original five lowercase 64-bit diagnostics.

ARM64_RESULT:
PASS compile-only for standalone semantic test; object produced. Runtime not
attempted because builder is x64 and no ARM64 execution was available.

REGRESSION_TESTS:
Scope verifier PASS; git diff --check PASS. Full exact agx control reaches the
next known boundary.

FRESH_COMPILER_RESULT:
clang-cl20.1.8 x86_64-pc-windows-msvc exit1 with overlay and prior derived
fourcc header. Original u_atomic five-name blocker is absent.

FIRST_NEW_BLOCKER:
agx_index static assertion: sizeof(agx_index) evaluates20, expected8. Later
off_t and UTIL_LUT2 diagnostics remain visible. Do not fix here.

SOURCE_SCOPE:
PASS

FORBIDDEN_PATHS_TOUCHED:
NONE

AGX_INDEX_CHANGED:
NO

ASSERTIONS_DISABLED:
NO

WDK_ABI_CHANGED:
NO

CAPABILITIES_CHANGED:
NO

HARDWARE_USED:
NO

RAW_EVIDENCE_PATHS:
investigation/evidence/AD04-uatomic-portability/

UNRESOLVED:
Auxiliary generic Mesa u_atomic fixture exposes lowercase 32-bit _interlockedadd
after the approved five aliases. Architect must decide whether it belongs to a
separate portability task; this worker did not change it.

CONTRACT_VIOLATIONS:
NONE

WORKER_OBSERVATION:
The approved five 64-bit contract is semantically proven and the production
control advances to agx_index. The isolated worker commit is not merged.
