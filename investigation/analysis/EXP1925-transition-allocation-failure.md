# EXP1925: fail closed on resource-transition allocation failure

This is an offline CPU failure fix, not a claim that GravityMark completes or
that memory pressure is solved. Full draft ACTUAL, hashes, decoded receipts,
validation logs and next-run instructions live in the shared handoff
`.local/handoff/codex/exp1925/REPORT.md`.

## Evidence

Evidence root: shared `.local/experiments/EXP1925-heap-staging`.
`evidence/EXP1925/gm1925a-done.txt` records GravityMark exception `c0000005`
at ARM64 UMD RVA `0x43fb4`. The 10k UMD trace ends at lines 4163-4164 with
allocation failure `8007000e` for `0x730000` bytes, then `reject-batch`.
The 2k trace ends at lines 3929-3930 with the same allocation size failing.
The repeated WER descriptions in the second done file lack event timestamps;
they are not proof of two independent identical crashes.

Packaged ARM64 DLL SHA256:
`8c5c21ef0f98d0d9c5f4e009705478936761b9705c321f2452bb05fe59bbaa48`.
Disassembly identifies `agx_flush_writer` at RVA `0x43fac`: the faulting
`ldr x9,[x1,#0x80]` at `0x43fb4` reads the resource BO; `0x43fc0` reads
its handle. Inlined `transition_resource`/`agx_decompress` at `0xa85c`
calls resource_create at `0xa8e8`, saves its unchecked result in x21 at
`0xa8f0`, skips copies if no levels are valid, then passes x21 as x1 to
`agx_flush_writer` at `0xa9c0`. Subsequent instructions swap layout/BO.
No local map/PDB or exception-register dump is supplied. Identification is
from instruction/data flow and source layout; exact crash x1/stack is unknown.

## Contracts and ownership

Primary Mesa sources under `.local/reference/mesa/src/gallium/drivers/asahi`:

- `agx_pipe.c:1131`: transition allocates, copies valid levels, flushes,
  swaps storage; line 1138 only asserts allocation success.
- `agx_pipe.c:1170`: decompression requests a writable-image replacement.
- `agx_batch.c:449`: writer lookup dereferences resource/BO.
- `agx_state.c:120` and `:128`: image binding converts then asserts writable
  layout. Failure must return before that assertion or view publication.
- `agx_blit.c:531`: source/destination legalization precedes blitter entry.
- `agx_state.h:1057` and `agx_pipe.c:615`: pending batches retain separate
  BO references; dropping a temporary resource preserves batch ownership.

Local SDK `winsdk26100/um/d3d12umddi.h:2602` defines SetError. The existing
owner checks context fault at `mesa/winsys/agx_d3d12_windows.cpp:3677` and
returns DEVICE_HUNG at `:3702` (paths relative to `drivers/apple-agx`).

Mesa/UMD owns this CPU transition and its error propagation. KMD placement,
initialization, DMA, interrupts, power and recovery owners remain unchanged.
No ADT/register assumptions or m1n1/Mu changes are necessary for this boundary;
those submodules are unavailable here. Supplied BEFORE/recovery receipts are
used as evidence, without new machine access.

License review: the three Mesa source files carry MIT SPDX notices; inspected
`licenses/MIT` permits modification with notice retention. Existing projection
preserves those notices in generated files. No external implementation file is
vendored; only local guards and narrow source anchors are added.

## Invariant and verification

Allocation failure faults the context before resource use. A prior fault skips
allocation. Copy/flush failure drops the replacement before any BO, layout,
modifier or bind swap. Image binding and blits stop after failed legalization.
Successful conversion keeps its copy/flush/swap order; staging/caps/placement
policies and protected invariants are unchanged.

`test_d3d12_transition_failure` compiles actual projected bodies under
ASan/UBSan: allocation failure with zero/populated valid levels, previous
fault, second-copy failure, flush failure, successful multi-level/empty
conversion, image and blit legalization failures, cleanup after image failure.
RED reproduced nine failed subcases; GREEN passes all three tests. Independent
review confirmed BO lifetime handling. Targeted suite: 53 tests passed.

The required full baseline printed:

```
ran 431 tests in 181 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

## Checkpoint and unknowns

Claude alone rebuilds all packaged native ABI projections, keeping T13 staging
ON and `Wom1GpuFaultInfoRead=1`, and runs the same 10k/2k short profile.
Expected: allocation refusal reaches a context/device error without this AV or
a writable-layout assertion. A new AV, false successful completion, hang or
GPU fault rejects the candidate. Collect timestamped application events and
allocation/first-fault/Execute/staging/reset/omission receipts before ordinary
exact-package recovery.

Remaining unknowns: native compilation/runtime behavior; why allocation fails;
DWM c00001ad and preemption-overlap reset; failures after this CPU boundary.
The runs record only 45/42 completed Executes, so passing Execute 47 is unproven.
Recorded uploads total 1,151,078,400 / 1,149,861,888 bytes and downloads are zero;
C87 download-range optimization is unmeasured and deferred here.
