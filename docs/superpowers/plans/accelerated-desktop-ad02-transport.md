# Accelerated Desktop AD02 Transport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> User override: execute inline in the main process without subagents.

**Goal:** Build a versioned, immutable, allocation-relative Windows/Mesa command transport and prove one dynamic clear path with two distinct geometries/colors through the existing AppleAgx KMD and hardware backend, without advertising a D3D pipeline.

**Architecture:** The UMD owns API state and writes only a bounded command envelope plus WDDM allocation-list references. Dxgkrnl/VidMm owns placement and residency; the KMD copies the envelope once, validates its generation and every allocation-relative range against the current device/context/allocation list, and then translates the validated clear into the existing Render/Patch/Submit/fence backend. Firmware, retained-root, RTKit, UAT and queue ownership do not change.

**Tech Stack:** C11 shared validators, WDK/SDK 10.0.26100.0, MSVC 14.44.35207, WDDM3.0 KMD, D3D10 UMD callbacks, pinned Mesa commit `9aa1215f878b504f66159dd2ead4c7973142126e`, existing AppleAgx render-admission tests and FRYZZING build/sign workflow.

**Spec:** `docs/superpowers/specs/2026-09-08-accelerated-desktop-design.md` and `docs/superpowers/specs/accelerated-desktop-contract.json`.

## Global Constraints

- Keep `D3D11DDICAPS_3DPIPELINESUPPORT.Caps == 0` throughout AD02.
- Do not change AGX firmware ABI, RTKit, retained-root broker, context63, PBE encoding, DCP, IRQ or scheduler capabilities.
- Do not trust or encode a CPU pointer, host physical address, firmware pointer or raw GPU VA from UMD command bytes.
- Every resource reference is an allocation-list index plus checked byte offset/length and access/role flags.
- Copy command bytes exactly once before parsing; a producer mutation after that copy cannot change the accepted snapshot.
- UMD resources remain associated with exact runtime resource handles. WDDM submission residency and completion are not renamed to the private `OpenCount` object lifetime.
- Keep the fixed EXP640 qualification path as a hardware control; do not create a second queue, firmware or memory backend.
- No GPU hardware run until portable ABI, owner/range, KMD snapshot and fake-runtime UMD tests are GREEN.
- One hardware candidate may issue two dynamic clears because the pair is the indivisible discriminator for data-driven color/geometry rather than fixed replay.

## Exact wire contract

All fields use little-endian fixed-width integers. The ABI maximum is 4,096 bytes and 16 allocation references for the first version.

```c
#define APPLE_AGX_WIN32_COMMAND_MAGIC 0x43474157u /* "WAGC" */
#define APPLE_AGX_WIN32_COMMAND_VERSION 1u
#define APPLE_AGX_WIN32_COMMAND_MAX_BYTES 4096u
#define APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES 16u

typedef enum _APPLE_AGX_WIN32_OPCODE {
  AppleAgxWin32OpcodeClear = 1u,
} APPLE_AGX_WIN32_OPCODE;

typedef enum _APPLE_AGX_WIN32_ACCESS {
  AppleAgxWin32AccessRead = 0x1u,
  AppleAgxWin32AccessWrite = 0x2u,
  AppleAgxWin32AccessExecute = 0x4u,
} APPLE_AGX_WIN32_ACCESS;

typedef enum _APPLE_AGX_WIN32_ROLE {
  AppleAgxWin32RoleRenderTarget = 1u,
  AppleAgxWin32RoleVertex = 2u,
  AppleAgxWin32RoleIndex = 3u,
  AppleAgxWin32RoleConstant = 4u,
  AppleAgxWin32RoleTexture = 5u,
  AppleAgxWin32RoleShader = 6u,
  AppleAgxWin32RoleDescriptor = 7u,
} APPLE_AGX_WIN32_ROLE;

typedef struct _APPLE_AGX_WIN32_COMMAND_HEADER {
  uint32_t Magic;
  uint16_t Version;
  uint16_t HeaderBytes;
  uint32_t TotalBytes;
  uint32_t Opcode;
  uint32_t Flags;
  uint32_t Generation;
  uint32_t ReferenceCount;
  uint32_t ReferencesOffset;
  uint32_t PayloadOffset;
  uint32_t PayloadBytes;
  uint64_t ContentHash;
} APPLE_AGX_WIN32_COMMAND_HEADER; /* 48 bytes */

typedef struct _APPLE_AGX_WIN32_ALLOCATION_REFERENCE {
  uint32_t AllocationIndex;
  uint32_t Access;
  uint32_t Role;
  uint32_t Reserved;
  uint64_t Offset;
  uint64_t Bytes;
} APPLE_AGX_WIN32_ALLOCATION_REFERENCE; /* 32 bytes */

typedef struct _APPLE_AGX_WIN32_CLEAR_PAYLOAD {
  uint32_t StructBytes;
  uint32_t Format;
  uint32_t Color;
  uint32_t SurfaceWidth;
  uint32_t SurfaceHeight;
  uint32_t SurfacePitch;
  uint32_t Left;
  uint32_t Top;
  uint32_t Right;
  uint32_t Bottom;
  uint32_t DestinationReference;
  uint32_t Reserved;
} APPLE_AGX_WIN32_CLEAR_PAYLOAD; /* 48 bytes */
```

`ContentHash` is FNV-1a64 over `TotalBytes` with the eight bytes of `ContentHash` treated as zero. This hash detects mutation/corruption after construction; it is not an authentication primitive.

---

### Task 1: Portable immutable command envelope

**Files:**
- Create: `drivers/apple-agx/shared/include/apple_agx_win32_abi.h`
- Create: `drivers/apple-agx/shared/src/apple_agx_win32_abi.c`
- Create: `drivers/apple-agx/shared/tests/apple_agx_win32_abi_test.c`
- Create: `tests/test_apple_agx_win32_abi.py`

**Interfaces:**
- Produces `APPLE_AGX_WIN32_COMMAND_VIEW` containing pointers only into caller-owned snapshot bytes.
- Produces `AppleAgxWin32CommandHash`, `AppleAgxWin32CommandValidate`, `AppleAgxWin32ReferenceValidate`.
- Consumes an already copied byte array; it never probes user memory.

- [x] **Step 1: write the RED portable test.** Assert exact struct sizes, one literal valid clear, bad magic/version/header size/total size/count/offset/alignment/hash, arithmetic overflow, stale generation, unknown opcode/flags/access/role, duplicate render target, zero length and out-of-range allocation index.

```c
APPLE_AGX_WIN32_COMMAND_VIEW view;
CHECK(AppleAgxWin32CommandValidate(bytes, sizeof(bytes), 7u, 2u, &view) ==
      AppleAgxWin32AbiSuccess);
header->Generation = 6u;
CHECK(AppleAgxWin32CommandValidate(bytes, sizeof(bytes), 7u, 2u, &view) ==
      AppleAgxWin32AbiStaleGeneration);
```

- [x] **Step 2: run the test and require RED because the ABI/functions do not exist.** Use the repository's existing portable C-test compile pattern from `tests/test_apple_agx_render_job.py`.
- [x] **Step 3: implement only structural/hash/generation validation.** Validate all `offset + size` operations as `offset <= total && size <= total - offset`; require reference and payload arrays to be naturally aligned; require exactly one write-only render-target reference for `AppleAgxWin32OpcodeClear`.
- [x] **Step 4: prove producer mutation isolation.** Copy a valid command into `snapshot`, mutate the producer buffer, then require the validated view/hash and clear fields in `snapshot` to remain byte-exact.
- [x] **Step 5: run portable tests plus existing shared render-job, memory and submission tests GREEN, then commit only the four files.**

### Task 2: Device-owned allocation/range validation

**Files:**
- Create: `drivers/apple-agx/render-admission/include/render_win32_transport.h`
- Create: `drivers/apple-agx/render-admission/src/render_win32_transport.c`
- Create: `drivers/apple-agx/render-admission/tests/render_win32_transport_test.c`
- Modify: `tests/test_apple_agx_render_admission.py` only to compile/run the new production module.

**Interfaces:**

```c
typedef struct _ADMISSION_WIN32_ALLOCATION_FACT {
  uint64_t AllocationToken;
  uint64_t Bytes;
  uint32_t SegmentId;
  uint32_t Writable;
  uint32_t ActiveForDisplay;
  uint32_t Generation;
} ADMISSION_WIN32_ALLOCATION_FACT;

typedef int (*ADMISSION_WIN32_LOOKUP_ALLOCATION)(
    void *Context, uint32_t AllocationIndex,
    ADMISSION_WIN32_ALLOCATION_FACT *Fact);

ADMISSION_WIN32_TRANSPORT_RESULT AdmissionWin32ValidateReferences(
    const APPLE_AGX_WIN32_COMMAND_VIEW *View,
    uint32_t ExpectedGeneration,
    ADMISSION_WIN32_LOOKUP_ALLOCATION Lookup,
    void *LookupContext,
    ADMISSION_WIN32_ALLOCATION_FACT *Facts,
    uint32_t FactCapacity);
```

- [x] **Step 1: write RED table tests** for wrong device owner, stale allocation generation, read-only destination, active display surface write, zero/misaligned/overflowing range, valid nonzero subrange, noncontiguous allocation-list indices and lookup failure.
- [x] **Step 2: implement the lookup-driven validator** without importing WDK types into the portable ABI module. The callback supplies facts; the validator never accepts a global address or pool-wide containment as ownership.
- [x] **Step 3: add exact overlap rules.** Two writable references may not overlap the same allocation token; a read/write overlap is rejected unless a future opcode documents it. Clear accepts one writable destination and no other reference.
- [x] **Step 4: prove rollback has no state.** Every rejected command leaves the caller's packet, fence, allocation counters and display ownership byte-exact.
- [x] **Step 5: run the new tests plus allocation/completed-output/presentation lifetime suites GREEN and commit.**

### Task 3: KMD copy-once Render translation

**Files:**
- Modify: `drivers/apple-agx/render-admission/include/render_admission.h`
- Modify: `drivers/apple-agx/render-admission/src/callbacks.c`
- Modify: `drivers/apple-agx/render-admission/src/umd_render_windows.c`
- Modify: `drivers/apple-agx/render-admission/src/submission_windows.c`
- Add project entries in `drivers/apple-agx/render-admission/AppleAgxRenderAdmission.vcxproj`.
- Test: existing render-admission production composition tests plus the Task 2 test.

**Interfaces:**

```c
typedef struct _ADMISSION_WIN32_CONTEXT_CREATE {
  uint32_t Magic;      /* 0x43574157, "WAWC" */
  uint16_t Version;    /* 1 */
  uint16_t Bytes;      /* sizeof structure */
  uint32_t Generation; /* nonzero UMD device epoch */
  uint32_t Reserved;
} ADMISSION_WIN32_CONTEXT_CREATE;

typedef struct _ADMISSION_WIN32_RENDER_SNAPSHOT {
  uint32_t Bytes;
  uint32_t Generation;
  unsigned char Storage[APPLE_AGX_WIN32_COMMAND_MAX_BYTES];
  APPLE_AGX_WIN32_COMMAND_VIEW View;
} ADMISSION_WIN32_RENDER_SNAPSHOT;

NTSTATUS AdmissionWin32SnapshotRenderCommand(
    ADMISSION_RENDER_CONTEXT *Context,
    const DXGKARG_RENDER *Args,
    ADMISSION_WIN32_RENDER_SNAPSHOT *Snapshot);
```

- [ ] **Step 1: RED-test context epochs.** A non-system, non-GDI desktop context supplies the exact create struct; zero/stale/unknown fields fail closed. Existing System/GDI and explicitly compiled qualification contexts preserve their proven no-private-data path.
- [ ] **Step 2: implement copy-once.** `AdmissionWin32SnapshotRenderCommand` checks `CommandLength <= 4096`, copies under `__try/__except` into nonpaged local storage exactly once, then invokes the portable validator and Task 2 lookup adapter. No later code reads `Args->pCommand`.
- [ ] **Step 3: translate validated clear payload** into the existing `ADMISSION_GDI_COLOR_FILL_INPUT`, existing DMA shadow and existing Patch/prepatched packet flow. Color, rect, format, pitch and destination range come from the snapshot/allocation fact; no fixed test color or address is introduced.
- [ ] **Step 4: keep submission lifetime with WDDM.** Emit exact `D3DDDI_PATCHLOCATIONLIST` entries for every referenced allocation. The packet retains allocation/context identity through its scheduler fence; `DMA_COMPLETED` remains the point at which dxgkrnl may release in-flight submission references. `AdmissionAllocationOpen/Close` continues to guard driver object lifetime only and is not documented as residency pinning.
- [ ] **Step 5: prove reset behavior.** Device/context teardown invalidates its generation, cancels only unpublished packets, and never reuses a pre-reset snapshot or fence. Failure before packet publication rolls back all local state; failure after publication enters the existing owned fault/reset state and is not immediately freed.
- [ ] **Step 6: run all Render/Patch/Submit/completion/presentation tests GREEN and commit this KMD integration separately.**

### Task 4: Mesa-facing Windows UMD transport

**Files:**
- Create: `drivers/apple-agx/mesa/winsys/agx_win32_transport.h`
- Create: `drivers/apple-agx/mesa/winsys/agx_win32_transport.c`
- Create: `drivers/apple-agx/mesa/winsys/agx_win32_transport_test.c`
- Modify incrementally: `drivers/apple-agx/render-admission/umd/src/umd.c`, `umd_internal.h`, UMD project and real mock-runtime test.

**Interfaces:**

```c
typedef struct _AGX_WIN32_TRANSPORT {
  D3D10DDI_HRTDEVICE RuntimeDevice;
  D3D10DDI_HRTCORELAYER RuntimeCoreLayer;
  const D3DDDI_DEVICECALLBACKS *Callbacks;
  HANDLE KernelContext;
  uint32_t Generation;
  uint32_t Terminal;
} AGX_WIN32_TRANSPORT;

HRESULT AgxWin32TransportBuildClear(
    uint32_t generation,
    const APPLE_AGX_WIN32_CLEAR_PAYLOAD *clear,
    uint32_t allocation_index,
    void *command_buffer,
    uint32_t command_capacity,
    uint32_t *command_bytes);

HRESULT AgxWin32TransportSubmit(
    AGX_WIN32_TRANSPORT *transport,
    const void *command,
    uint32_t command_bytes,
    const D3DKMT_HANDLE *allocations,
    const uint32_t *access,
    uint32_t allocation_count);
```

- [ ] **Step 1: RED-test the builder** for exact literal bytes/hash, capacity, stale generation, invalid destination, and mutation after `pfnRenderCb` begins. Fake callbacks must copy the command/allocation list exactly as the runtime contract does.
- [ ] **Step 2: implement D3D10 callback wiring** for `pfnCreateContextCb`, command/allocation/patch buffers, and `pfnRenderCb`. Use `pfnLockCb/pfnUnlockCb` only for supported CPU map/update paths; never treat `D3DKMTLock2` as a submission flush.
- [ ] **Step 3: implement UMD resource ownership adapter.** Created resources use `D3DDDICB_ALLOCATE.hResource`; opened shared resources keep exact runtime/kernel handles; destroy/Flush uses the corrected retirement queue. Per-process handles are never accepted from another device.
- [ ] **Step 4: add Mesa adapter boundary without importing its DRM target.** Expose a `pipe_screen`/resource-facing shim whose BO create/map/submit/fence methods call this Windows transport. Do not call `agx_screen_create(fd, ...)`, DRM ioctls, syncobj fds or the GDI software winsys.
- [ ] **Step 5: keep the selected pipeline unpublished.** The new transport unit and its clear helper compile and pass, but `GetCaps` remains zero because the 121-row AD01 UMD contract is incomplete.
- [ ] **Step 6: run x64 real mock runtime, ARM64 UMD analysis and source-lock/license manifest checks GREEN, then commit.** Any Mesa files later copied into the repository retain their exact license headers and enter a per-file manifest before compilation.

### Task 5: Dynamic clear integration without a second backend

**Files:**
- Create: `drivers/apple-agx/render-admission/src/render_dynamic_windows.c`
- Create: `drivers/apple-agx/shared/include/apple_agx_dynamic_job.h`
- Create: `drivers/apple-agx/shared/src/apple_agx_dynamic_job.c`
- Create: `drivers/apple-agx/shared/tests/apple_agx_dynamic_job_test.c`
- Modify: existing backend materialization entry only where the validated dynamic fields are consumed.

**Interfaces:**

```c
typedef struct _APPLE_AGX_DYNAMIC_CLEAR {
  uint32_t Color;
  uint32_t Format;
  uint32_t SurfaceWidth;
  uint32_t SurfaceHeight;
  uint32_t SurfacePitch;
  uint32_t Left, Top, Right, Bottom;
  uint64_t DestinationGpuVa;
  uint64_t DestinationBytes;
} APPLE_AGX_DYNAMIC_CLEAR;

APPLE_AGX_BOOL AppleAgxDynamicClearValidate(
    const APPLE_AGX_DYNAMIC_CLEAR *clear);
```

- [ ] **Step 1: RED-test two clears** with different colors and rectangles and literal expected patched scalar sets. Change geometry, color and destination independently; reject out-of-bounds pitch/range and an active display destination.
- [ ] **Step 2: map the validated clear into the one existing backend image/materializer.** Reuse current queue objects, firmware job, retained-root mappings and completion/fence. EXP208 fixed structures may provide the encoder template, but every variable named above comes from the validated request.
- [ ] **Step 3: prove there is no fixed replay.** The two materialized jobs have different command hashes at the exact documented dynamic scalar locations and produce different predicted destination hashes while preserving invariant firmware pointers.
- [ ] **Step 4: run all shared/backend/submission tests GREEN and pinned KMD/UMD analysis/Universal/sign/version/hash gates. Commit the dynamic consumer separately.**

### Task 6: One AD02 hardware discriminator

**Files:**
- Create experiment-local package/workflow/evidence under `.local/experiments/EXP649-dynamic-win32-transport/` unless the ledger already assigns a later unused ID.
- Modify before and after run: `investigation/EXPERIMENTS.md`.
- Update after verdict: `investigation/GPU_CURRENT_STATE.md`, `investigation/ACCELERATED_DESKTOP_ROADMAP.md`, `investigation/CHANGES.csv`.

**Interfaces:** one exact qualification producer uses the Task 4 transport and issues two clear envelopes on inactive owned allocations.

- [ ] **Step 1: write preregistration with the four architecture sections.**

```text
WINDOWS CONTRACT: D3D10 UMD pfnRenderCb with allocation list and patch list;
VidMm owns placement/residency and the KMD completes exact submission fences.
AGX/ASAHI CONTRACT: existing retained-root/runtime/queue backend consumes one
validated destination/color/geometry job and returns TA/3D completion.
TRANSLATION: copied envelope → owned allocation-relative facts → existing DMA
shadow/Patch/Submit → existing materializer → exact fence/output verification.
WHAT IS STILL UNKNOWN: whether two distinct data-driven requests survive the
live Windows/KMD/backend boundary and produce their two predicted hardware outputs.
```

- [ ] **Step 2: record `WHY THIS HYPOTHESIS:`** using (1) EXP640 fixed backend proof, (2) all AD02 offline mutation/range tests, and (3) exact two-request job hashes. Freeze source/dirty hashes, overlay manifest, WDK build command, package/producer hashes and ordinary/full-owner recovery artifacts.
- [ ] **Step 3: clean preflight and natural bind.** Require ordinary Code28/no package before staging; install only the exact hash-gated candidate; boot the current full-owner platform.
- [ ] **Step 4: execute two requests.** Request A clears a full or bounded inactive surface with color A; request B uses a distinct valid rectangle and color B. Preserve exact command snapshots/hashes, allocation identities/segments/ranges, Render/Patch/Submit/fence, TA/3D completion and full relevant output oracle for both.
- [ ] **Step 5: classify independently.** `TRANSPORT_HW_PROVEN` requires two copied envelopes, two validated allocations, two physical jobs and two exact fences. `DYNAMIC_CLEAR_HW_PROVEN` additionally requires both predicted output regions/hashes and unchanged guard regions. Neither changes D3D feature-level or desktop readiness.
- [ ] **Step 6: collect health and exact cleanup.** Failed/intermediate candidate returns to ordinary Code28. If both AD02 hardware predicates pass, update roadmap AD02=HW_PROVEN and begin the AD03 compiler/encoder plan; do not install a nonzero pipeline candidate.

## Self-review

- Spec coverage: immutable snapshot, owner/generation/range validation, mutation defense, map/upload, WDDM residency distinction, reset, sharing boundary, dynamic clear and one bounded hardware discriminator each have an owning task.
- No parallel backend: Tasks 3 and 5 explicitly reuse current DMA shadow, scheduler, materializer, queues and fence completion.
- Type consistency: all later signatures consume the exact header/reference/clear structures defined at the top; generation is the same UMD device epoch stored in the KMD context.
- Promise boundary: AD02 can prove transport and dynamic clear only. Pipeline mask, D3D device, DWM and standard Present remain AD04–AD06 gates.
- Execution choice is already fixed by the user: continue inline with `superpowers:executing-plans`; do not ask for subagents.
