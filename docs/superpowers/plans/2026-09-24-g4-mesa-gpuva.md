# G4 Mesa GPUVA Implementation Plan

> **For agentic workers:** Execute locally in this isolated worktree; the Air and G3 KMD trees are owned by the integration thread.

**Goal:** Prepare the Mesa/UMD winsys to give Asahi BOs real GPU virtual addresses and submit native VA batches after VidMm residency.

**Architecture:** A portable callback state machine enforces reserve/map/paging/submit/completion order. A build flag selects GPUVA BO mapping and direct batch submission; the existing physical capture build remains the default. The UMD callback adapter calls pinned WDK 26100 functions, while KMD private command bytes remain fail closed until the main thread defines their ABI.

**Tech Stack:** C11, Mesa Asahi, WDK 26100 UMD DDI, host ASan/UBSan, `UmdContractTest`.

**Spec:** `docs/superpowers/specs/2026-09-24-gpuva-g4-umd-contract.md`

## Global constraints

- No Air or hardware runs; no G3 KMD edits or `investigation/EXPERIMENTS.md` edits.
- Builder copies and output go only under `C:\Users\pauls\AD04-g4-mesa-va` and a distinct Mesa build directory.
- Reserve VA in 64-KiB units, map in 4-KiB DDI pages, enforce 16-KiB AGX and 39-bit VA bounds.
- Keep physical capture as the default build and disable capture/relocation for the VA build.
- Each implementation commit is followed by a `CHANGES.csv` row with that commit hash.

## Sources and ownership

See the spec's source list. VidMm owns allocation, GPUVA page tables and residency; the UMD owns address assignment, reference collection, ordered callback calls and command bytes; KMD owns private ABI validation and TA/3D completion; m1n1 owns UAT, TLB and recovery; Mu describes APPL0002. EXP782 remains at VidMm level-one paging; no rendering contract has been hardware validated. The first hardware checkpoint is one draw with unchanged native commands and an observed completion, run only by the integration thread with exact Code28 recovery.

## Review focus

- A callback returning a misaligned/out-of-range VA must not publish a BO.
- A pending map or residency fence must finish before submit.
- Failed residency or missing command reference must never call submit.
- Failure after successful submit must preserve BO ownership until completion is known.
- Free must not race in-flight command use.

### Task 1: Portable GPUVA contract and host draw replay

**Files:** `mesa/winsys/agx_win32_gpuva.{h,c}`, `agx_win32_gpuva_test.c`, G4 spec.

**Interface:** `AgxWin32GpuvaBind`, `Submit`, `Retire`, `Unbind`; callback table mirrors WDDM operations without importing WDK into host code.

- [x] Write minimal draw test with fake callback ordering and literal VA/command bytes.
- [x] Run host test RED with the missing GPUVA implementation.
- [x] Implement 64-KiB reserve/map, fence waits, residency and retirement.
- [x] Run host test GREEN with ASan/UBSan.
- [x] Commit after review and verification; append `CHANGES.csv` row (`73861c56`).

### Task 2: Opt-in Mesa `agx_bo` GPUVA mapping

**Files:** `mesa/winsys/agx_win32_asahi_bo.{h,c}`, runtime build script and BO tests.

**Interface:** Enable `APPLE_AGX_GPUVA_WINSYS` only in the G4 build; use a configured callback table to bind the existing VidMm allocation token, publish GPUVA in `agx_bo.va`, free VA before allocation destruction. Keep physical profile unchanged.

- [x] Host callback draw proves the mapped address and 64-KiB reservation.
- [x] Implement the opt-in BO path and compile both VA and physical variants.
- [x] Compile x64 and ARM64 G4 native archives in separate builder directories; commit and ledger (`6acd9c0d`).

### Task 3: UMD callback adapter and direct native batch

**Files:** `render-admission/umd/src/umd_gpuva_windows.c`, runtime device, native batch bridge and `UmdContractTest`.

**Interface:** Adapt `ReserveGpuVirtualAddressCb`, `MapGpuVirtualAddressCb`, `MakeResidentCb`, paging wait, `SubmitCommandCb`, rendering monitored fence and `EvictCb`. `SubmitCommandCb` carries a proposed versioned private header; KMD admission remains blocked until the integration thread accepts this ABI.

- [x] Add callback replay test asserting Reserve→Map→paging wait→MakeResident→paging wait→Submit→render wait→Evict. Allocate remains covered by the existing screen path.
- [x] Implement the WDK adapter and direct native render dispatch; host replay passes.
- [x] Build x64 UmdContractTest and ARM64 UMD in G4-only directories; commit and ledger.

### Task 4: Integration handoff

**Files:** G4 spec, `.local/tandem/REVIEW.md`.

- [x] Record KMD private command ABI, context/fence and mapping requirements as OPEN R64.
- [x] Run focused tests and record the remaining direct compute and hardware gates in the spec.
