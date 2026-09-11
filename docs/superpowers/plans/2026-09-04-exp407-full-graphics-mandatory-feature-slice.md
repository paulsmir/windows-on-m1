# EXP407 Full Graphics mandatory-feature slice implementation plan

> Execute inline without a new review gate. Do not bind hardware until Tasks
> 1-10 pass and the experiment is preregistered with exact artifacts.

**Goal:** Make the separate WDDM 3.0 `render-admission` package a truthful
Full Graphics vertical slice whose Type 1 mandatory caps are backed by real
allocation/context/scheduler/TDR/GDI/direct-flip/D589/AGX completion behavior.

**Architecture:** Preserve the EXP406 162-field initialization vector,
candidate Mu and synthetic INTID 889. Reuse current shared and Windows backend
components behind small `render-admission` wrappers. Establish a single
immutable feature-readiness contract in StartDevice and publish the mandatory
Type 1 group atomically. Use current m1n1's hardware-proven retained IOMFB owner
with no new DCP protocol and the existing EXP208 G13/v13.5 materialized graph.

**Pinned environment:** WDK/SDK `10.0.28000.2526`; ARM64 Release; FRYZZING
builder; current-compatible Mu/m1n1; J313 Windows 11.

---

## Task 1: Atomic mandatory-feature readiness contract

**Files:**

- Create `drivers/apple-agx/shared/include/apple_agx_wddm_feature_contract.h`
- Create `drivers/apple-agx/shared/src/apple_agx_wddm_feature_contract.c`
- Create `drivers/apple-agx/shared/tests/apple_agx_wddm_feature_contract_test.c`
- Create `tests/test_apple_agx_wddm_feature_contract.py`
- Modify `drivers/apple-agx/render-admission/AppleAgxRenderAdmission.vcxproj`

**Invariant caught:** Type 1 must never publish a partial mandatory Full
Graphics group. Readiness is false unless WDDM identity, one node, memory,
context, scheduler, DMA-boundary preemption, TDR, GDI KMCB, direct/independent
flip, real latch-backed scanout, Non-VGA stop and matching UMD are all ready.

Write the C unit test first with one all-ready case and a table that clears
each prerequisite independently. Compile/run through the Python harness and
observe RED because the contract does not exist. Implement a pure side-effect-
free evaluator, rerun GREEN, add it to the KMD project, run adjacent tests,
commit, and append the change-ledger row.

## Task 2: Render-admission object and handle model

**Files:**

- Create `drivers/apple-agx/render-admission/include/render_objects.h`
- Create `drivers/apple-agx/render-admission/src/objects.c`
- Modify `render_admission.h`, `callbacks.c`, project and tests

**Invariant caught:** adapter/device/context/allocation handles are typed,
nonpaged, reference-counted and cannot be destroyed while referenced or while
their fence is outstanding.

Implement one adapter, bounded devices/contexts and bounded allocations. Reject
invalid flags, ordinals, affinity, overflow and stale handles before mutation.
CreateDevice/CreateContext become real; destroy paths are idempotent only where
the WDK permits. No hardware calls in this task.

## Task 3: Segment and paging admission

**Files:**

- Reuse `shared/apple_agx_local_segment`, `memory`, `residency`, `uat_table`
- Add `render-admission/src/memory.c` and `paging.c`
- Modify project/header/tests

**Invariant caught:** Windows segment offsets, CPU physical addresses and AGX
virtual addresses are never conflated; Segment 1 uses 4 KiB PFN aperture
semantics while Segment 2 and UAT use checked contiguous/16 KiB mappings.

Implement QuerySegment4, standard/allocation private data, create/open/close/
destroy, BuildPagingBuffer operations required for these segments, context
resource init and replay-safe paging fences. All arithmetic is checked and host
tests cover alignment, boundaries, rollback and replay.

## Task 4: GDI kernel-command-buffer path

**Files:**

- Reuse `shared/apple_agx_gdi` and local-segment translation
- Add `render-admission/src/gdi.c`
- Modify callbacks/project/tests

**Invariant caught:** only supported GDI operations and exact allocation types
produce immutable DMA/private data and patch entries. Unsupported ROPs fail
before submission.

Implement GetStandardAllocationDriverData, CreateAllocation shape validation,
RenderKm and Patch using the one checked Segment-2-to-AGX-VA translation. Keep
`SupportKernelModeCommandBuffer` false until Task 8.

## Task 5: One-node scheduler, fence, preemption and TDR

**Files:**

- Reuse shared scheduler/submission/completion/recovery components
- Add `render-admission/src/scheduler.c`
- Modify interrupt/context/project/tests

**Invariant caught:** completed fences advance monotonically only from real
backend completion; preemption reports a valid last-completed fence; reset
returns an aborted fence within the dxgkrnl interval and preserves paging replay.

Implement SubmitCommand, QueryCurrentFence, PreemptCommand,
QueryDependentEngineGroup, QueryEngineStatus, ResetEngine, CollectDbgInfo and
adapter reset/restart. Use DMA-buffer-boundary semantics: active work retires,
pending work is aborted before publication. Test wrap, duplicates, stale events,
reset intervals, paging replay and teardown races. Keep caps false until Task 8.

## Task 6: Current-source retained-owner Scanout ABI v2

**Files:**

- Modify only m1n1 build/profile wiring if needed; do not change the DCP RPC
  implementation
- Extend candidate contract/profile tests

**Invariant caught:** StartDevice can advertise flip support only when broker
ABI v2 carries REGISTERED_POOL, REPEATED_PRESENT, LATCHED_RECEIPT and
LATCHED_IRQ and the sole DCP owner is active.

Build current m1n1 with `DCP_IOMFB_FULL_OWNER` and the existing EXP406
synthetic 889 change. Verify relevant source differs from EXP270 only by the
synthetic route. Run all m1n1 display/scanout host tests. Do not boot hardware
yet.

## Task 7: KMD primary, real flip and VSync completion

**Files:**

- Reuse shared fixed-panel and scanout modules
- Extend `render-admission/src/display.c`, `interrupt.c`, `lifecycle.c`
- Modify header/project/tests

**Invariant caught:** a source-address request at DIRQL never waits and can
refer only to a prevalidated exact primary. A VSync receipt exists only after
exact D589 for the matching sequence/address.

Register the contiguous scanout pool at PASSIVE_LEVEL. Implement Present with
NULL DMA, nonblocking SetVidPnSourceAddress, ISR status/ack plus
DXGK_INTERRUPT_CRTC_VSYNC notification, and DPC. Add stop/release gating, black
fallback and accurate POST data. Test stale/mismatched latch, error, duplicate,
mask, stop races and no registry/file/wait calls on DIRQL paths.

## Task 8: Narrow UMD DirectFlip contract

**Files:**

- Extend `drivers/apple-agx/render-admission/umd/` sources/project
- Extend INF and package tests

**Invariant caught:** the UMD accepts DirectFlip only for the exact KMD primary
format, size, stride, segment and allocation identity; every other combination
is rejected.

Implement only the minimum adapter/device/resource callbacks needed to expose
one compatible path. Keep all shader/feature-level rendering unsupported. Add
PE export/ABI tests and exact KMD/UMD compatibility-vector tests.

## Task 9: Existing AGX backend vertical slice

**Files:**

- Link reviewed shared platform/backend/UAT/G13/EXP208 sources
- Add narrow `render-admission/src/backend.c`
- Reuse Windows platform wrappers only after removing unrelated package state
- Extend tests

**Invariant caught:** one Windows fence maps to one materialized TA+3D graph,
one queue publication and one matching event/stamp completion; no enqueue-only
completion, stale event or duplicate fence is accepted.

Bring up AGX power/ASC/RTKit/UAT/queues only after Tasks 1-8 initialize. Use the
validated EXP208 relocation template, publish one context and one submission,
translate the matching completion to the scheduler, then support repeated
submits. Add fault and bounded rollback paths. Physical AGX IRQs remain
unpublished unless primary-source analysis proves a specific completion line;
poll/event ingress must otherwise remain bounded and truthful.

## Task 10: Atomic Type 1 publication

**Files:**

- Modify `render-admission/src/lifecycle.c`
- Extend render-admission and package tests

**Invariant caught:** no mandatory cap bit can be set independently of its
operational contract.

After StartDevice establishes immutable readiness, publish the entire approved
group in one helper. Assert zero reserved fields and zero optional capabilities.
If readiness is false, fail StartDevice before one source/node is exposed;
never return a partially capable Type 1 response.

## Task 11: Build, preregister and hardware test

Freeze a source archive and reproduce the exact EXP214/EXP406 FRYZZING build
invocation. Require KMD+UMD compile/link/code analysis, Universal validation,
Inf2Cat, signing and hashes. Build candidate Mu and current full-owner m1n1;
decompile AML and prove exactly edge 889 and no 880-888.

Preregister EXP407 with the four deterministic contract sections, atomic caps
group, commits/diff hashes, commands, artifacts, recovery and evidence paths.
Run the standard non-AGX 180-second stage-only phase, then one natural current
candidate bind. Collect at the first honest boundary. If admission and one
submit pass, execute ten additional submissions and the complete health gate.

## Task 12: Exact cleanup and continuation

Copy evidence first. Boot current-compatible non-AGX, delete only the exact
published INF with `/uninstall`, remove only matching residual state, verify no
package/service/module/receipt, restore ordinary current G2 and verify Code28,
SSH, eight CPUs, input/NVMe/xHCI and no fresh 41/129/1001.

Update `EXPERIMENTS.md`, `GPU_CURRENT_STATE.md` and `CHANGES.csv`. Continue from
the first observed callback/backend boundary; do not create per-bit experiments
and do not reuse a rejected package.
