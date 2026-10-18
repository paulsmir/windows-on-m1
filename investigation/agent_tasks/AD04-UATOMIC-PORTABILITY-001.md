# TASK_ID: AD04-UATOMIC-PORTABILITY-001

TIER: B
MODEL: Spark implementation worker (real code-capable invocation required)
INPUT_COMMIT: f81ffe46f6e1c56ab1909401debceffcd8bcb08b
WORKTREE: disposable task worktree created by the architect runner
BRANCH: agent/ad04-uatomic-portability-001

## GOAL

Implement the smallest separate Windows clang-cl compatibility overlay for the
pinned Mesa `u_atomic` MSVC branch. Resolve only the observed legacy lowercase
64-bit intrinsic spellings to documented uppercase Clang/MSVC intrinsics while
preserving atomic semantics. Stop at the first newly exposed compiler boundary.

## ARCHITECT CONTRACT

`.local/reference/mesa` is immutable and must not be edited. The overlay is
separate, hash-pinned and consumed only by Windows compiler/build composition.
Verify exact declarations in compiler headers. The only observed aliases are:

```text
_interlockedexchange64     -> _InterlockedExchange64
_interlockedexchangeadd64  -> _InterlockedExchangeAdd64
_interlockedincrement64    -> _InterlockedIncrement64
_interlockeddecrement64    -> _InterlockedDecrement64
_interlockedadd64          -> _InterlockedAdd64
```

Do not guess aliases or alter ordering, width, calling convention or values.

## WHY_THIS_TASK_EXISTS

AD04-CLANG-DISCRIMINATOR b5 is source-read-only and hardware-free. Its first
diagnostic is the lowercase/uppercase intrinsic mismatch in `u_atomic.h`.
Later `agx_index` size20!=8, `off_t`, and `UTIL_LUT2` are known but out of scope
until a fresh compile reaches them.

## ALLOWED_PATHS

- `drivers/apple-agx/mesa/windows-overlay/`
- `drivers/apple-agx/mesa/scripts/`
- `tests/test_agent_orchestration.py` for a focused overlay test only
- `investigation/evidence/AD04-uatomic-portability/`

## READ_ONLY_PATHS

- `.local/reference/mesa/src/util/u_atomic.h`
- `.local/reference/mesa/src/util/simple_mtx.h`
- `.local/reference/mesa/src/asahi/compiler/`
- builder compiler headers and `investigation/evidence/AD04-asahi-windows-compiler/clang-b5-bundle/`

## FORBIDDEN_PATHS / ACTIONS

Do not edit pinned/generated Mesa, `agx_index`, bitfield packing, `off_t`,
`UTIL_LUT2`, NIR/shader semantics, WDK ABI, feature levels, capabilities, KMD,
UAT, firmware, RTKit, display, hardware scripts or packages. Do not delete
assertions, add permissive flags, change atomic semantics, launch hardware, or
fix later compiler blockers.

SOURCE_CHANGE_ALLOWED: YES, only in ALLOWED_PATHS
HARDWARE_ALLOWED: NO
NETWORK_ALLOWED: YES, builder header verification only

## REQUIRED VERIFICATION

Write a focused alias/overlay test; run the unchanged Clang control with the
overlay; record the first diagnostic after it; run scope verification; produce
a clean isolated commit and compact RETURN_TEMPLATE evidence.

## STOP CONDITIONS

Stop if pinned source, ABI/layout, flags or architecture must change, or after
the first new compiler diagnostic. Do not fix the next blocker in this task.
