# EXP1928: query allocation after terminal residency failure

## Observed boundary

The BEFORE contract (`investigation/EXPERIMENTS.md:62097`) tests clean CPU
failure after MakeResident refusal, with `Wom1PoolHalfSize=1` retained only
as a reproducer and `Wom1GpuFaultInfoRead=1` for GPU evidence.

Saved evidence root: `.local/experiments/EXP1928-batch-fail/` in the shared
repository data directory. Both GravityMark processes reach benchmark setup,
then fail with WER `c0000005` in ARM64 UMD 30.0.1928.0, RVA `0x568d0`.
The 10k/2k wrappers record EXIT at 89/68 seconds; these are process-exit
observations, not measured device-removal times. No process dump, register
context, or system bugcheck is supplied.

`evidence/EXP1928/gm1928a-umd.log:15607` and
`gm1928b-umd.log:15436` record E_OUTOFMEMORY from MakeResident, followed by
Finish refusal, failed backend and first context fault `0x1029f`. Unlike
EXP1927, no subsequent failed compute Begin is recorded. Thus the prior
compute guard remains useful, but the overall clean-failure hypothesis is
**rejected**: both applications still crash, now at another instruction.

The actual ARM64 DLL SHA-256 is
`3418b9ebf4148ce1bc420aca12f5e91766ae13692b1917fa203003a871215e05`.
Offline LLVM disassembly shows:

```text
RVA 568bc: mov w1, #0x10     // 16-byte query allocation
RVA 568c0: mov w2, wzr      // alignment
RVA 568c4: mov w3, #0x8     // AGX_BO_WRITEBACK
RVA 568cc: bl  0x180001bd4 // BO creation
RVA 568d0: ldr x9,[x0,#0x50] // first access to returned BO's cached CPU map
```

This matches `.local/reference/mesa/src/gallium/drivers/asahi/agx_query.c:166`
(16-byte WRITEBACK `"Query"` BO followed by `agx_bo_map`). The function's
writer-generation sentinel initialization and separate shared-occlusion-heap
branch also match the surrounding binary. This is instruction/source
triangulation, not a symbolized stack or proof of the actual X0 value.

## Source evidence and ownership

- `drivers/apple-agx/mesa/winsys/agx_win32_asahi_bo.c:363`: BO creation returns
  NULL when the Windows backend has failed. This is a supported failure
  outcome, not permission to encode or dereference an absent BO.
- Reference `agx_query.c:148-176`: query CPU allocation and dedicated BO
  allocation are unchecked; only the final GPU address is checked, after BO
  dereferences. A failed CPU mapping also escapes that check.
- Reference `agx_query.c:64-108`: shared heap construction assumes allocation
  success, then consumes an occlusion slot before obtaining its CPU mapping.
  Adding an offset to a NULL map can even produce a non-NULL invalid pointer.
- `drivers/apple-agx/mesa/winsys/agx_win32_asahi_bo.c:953`: unreference accepts
  NULL, releases exactly one owned reference, and preserves failed disposal
  for the existing collector. Query code must not invent a second owner.
- `drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp:1748-1776`: the D3D12
  owner lazily creates Gallium queries and already returns E_FAIL for NULL,
  without invoking begin/end on it.
- `drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:6562` and `:6607`: query replay
  records failed HRESULTs and routes execution failure through pfnSetErrorCb.
- `.local/reference/winsdk26100/shared/d3dukmdt.h:1465-1497`: MakeResident may
  fail over budget and supplies NumBytesToTrim; even CantTrimFurther does
  not guarantee residency beyond the maximum budget.
- `.local/reference/mesa/docs/gallium/context.rst:415`: query creation owns
  the object used by begin/end/result/destruction. Query initialization and
  lifetime remain with native Gallium; the Windows projection adapts its
  unchecked allocation assumptions to the fallible Windows BO contract.

This change belongs in the Windows native-source projection. VidMm/KMD owns
residency and paging; firmware, MMIO, interrupts, DMA, power, and recovery
ownership do not change. Initialized m1n1/Mu sources are absent in this
worktree; no firmware change or live-state claim is made. All inspection and
execution here are local. External source bodies remain outside Git with
existing notices; only original projection edits and tests are added.

## Change and regression

`native-asahi-batch-lifecycle.py:198` projects checks for query CPU allocation,
dedicated BO creation/mapping, and shared occlusion heap creation/mapping.
Private-query failure releases its BO/object. Shared-heap map failure keeps
the context-owned heap and leaves the slot available. Retry can use the same
slot. Successful query type/index, generation sentinels, pointers and offsets
are preserved. No capability, ABI layout, KMD, signer, pool or GPUVA policy changes.

`tests/test_d3d12_query_allocation_failure.py` compiles the projected constructors
under ASan/UBSan. Fault injection is limited to allocation/map dependencies.
The original code fails eight subcases; the fix passes dedicated query success,
CPU allocation failure, BO failure, map failure and cleanup, plus shared heap
allocation/BO/map failures, retry, nonzero-offset failure, exhaustion and slot
ownership. Existing compute guards are retained and retested.

## Smallest next checkpoint for Claude

WHY THIS HYPOTHESIS:

1. Both traces end at the same terminal residency/backend boundary, followed
   by the same new WER RVA. Query BO creation is causally downstream of that
   failed-backend contract, without assuming another pool or mapping change.
2. Exact DLL instructions match unchecked query BO use, and the Windows BO
   allocator explicitly returns NULL on a failed backend.
3. Executable projected-source regressions reproduce the unsafe continuation
   and show safe refusal/cleanup after the projection change.

Rebuild the native Mesa projection for ARM64, x64 and WOW64 in a fresh,
hash-recorded package. Keep EXP1928 firmware/full-owner/recovery, KMD behavior,
feature caps and signing. Use one short 2k GravityMark D3D12 1280x720 run with
`AGX_DEVICE_SWITCHES="Wom1GpuFaultInfoRead Wom1PoolHalfSize"` set before
StartDevice. Pool v8 must still report SegmentBytes=1409286144.

When MakeResident fails, expect the existing refusal/context-fault evidence,
then clean execution/query failure (`measure-d3d12-query` E_FAIL if query
replay reaches this constructor), without an ARM64 query-construction AV.
A run without the triggering refusal is inconclusive for this guard.
Any CPU AV or hung termination rejects clean CPU error handling; obtain a
process dump and exception registers rather than extrapolating from the RVA.
Any GPU fault, DWM restart, wrong output or lost SSH separately rejects whole
workload stability. Stop at the decisive failure, collect WER/UMD and KMD
receipts before exact-package ordinary GPU-visible recovery.

WHAT IS STILL UNKNOWN: actual faulting X0 and caller stack; native Windows
compilation and runtime behavior of the new guard; whether another OOM path
fails after this one; why the active residency set does not converge; the
precise process/VA mapping history behind DWM's GPU faults; rendered output,
performance and benchmark completion. This is a CPU failure-handling fix,
not evidence that the GPU memory-pressure problem is solved.
