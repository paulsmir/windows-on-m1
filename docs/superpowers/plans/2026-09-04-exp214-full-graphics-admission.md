# EXP214-Derived Full Graphics Admission Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and hardware-prove a minimal truthful WDDM 3.0 Full Graphics render-admission driver with one internal J313 panel, successful QueryAdapterInfo types 34 and 35, no inert interrupt registration, and no Code 31/43 or watchdog/reset.

**Architecture:** Extend the separate EXP214-derived `render-admission` project, preserving its 1296-byte WDDM 3.0 `DRIVER_INITIALIZATION_DATA` and `DxgkInitialize` path. Add only Windows-facing one-panel topology/VidPn behavior; keep UMD, allocation, context, paging, render, submit, AGX power, MMIO, RTKit, UAT, queue, and scanout fail closed. Keep ISR/DPC/ControlInterrupt/GetScanLine null until a real status/ack/completion contract exists.

**Tech Stack:** C WDDM 3.0 KMD, C ARM64 fail-closed UMD, Python `unittest` source-contract tests, pinned NuGet WDK/SDK 10.0.28000.2526, MSBuild 18.1 preview, PowerShell evidence scripts, current Mu+m1n1 assisted J313 platform.

**Spec:** `docs/superpowers/specs/2026-09-04-exp214-full-graphics-admission-design.md`

## Global Constraints

- Execute inline in the current `feature/j313-gpu-acceleration` checkout; the user explicitly requested autonomous inline execution and no new approval gates.
- Preserve all unrelated dirty and untracked files. Stage and commit only the render-admission implementation, its focused tests, the plan/spec bookkeeping, and experiment ledger rows.
- Preserve exact EXP214 build context: direct source root below `C:\Users\pauls`, inherited root `Directory.Build.props` and targets, WDK/SDK `10.0.28000.2526`, MSBuild `18.1.0-preview-25527-05`, ARM64 Release, analysis, Universal, Inf2Cat, and test signing.
- Keep `DRIVER_INITIALIZATION_DATA` at 1296 bytes and `DXGKDDI_INTERFACE_VERSION_WDDM3_0`.
- Do not use `KMDDOD_INITIALIZATION_DATA` or `DxgkInitializeDisplayOnlyDriver`.
- Do not register ISR, DPC, ControlInterrupt, GetScanLine, PresentDisplayOnly, SystemDisplayEnable, or SystemDisplayWrite.
- Keep `FlipOnVSyncMmIo`, DirectFlip, preemption, scheduling, kernel command buffer, per-engine TDR, and MapAperture2 capabilities zero.
- Do not link or call AGX MMIO, power, HVC, RTKit, firmware, UAT, queue, submission, scheduler, render-job, or scanout code.
- Registry receipts are PASSIVE_LEVEL only and have no `ZwFlushKey`. The SetVidPnSourceAddress path must remain nonpageable and blocking-free.
- Use the accepted current m1n1 and Mu hashes from the spec. Do not use old EXP164/241/379 artifacts.
- Before every hardware run, preregister the exact artifact, hashes, expected checkpoint, failure rule, evidence path, and recovery path in `investigation/EXPERIMENTS.md`.
- Stage only under current-compatible non-AGX with no `/install`; bind only through natural current-G2 enumeration.
- Collect evidence before exact package cleanup; return to a clean current-G2 Code28 baseline after the experiment.

---

### Task 1: Pin the Full Graphics callback and capability contract

**Files:**
- Modify: `tests/test_apple_agx_render_admission.py:29-119`
- Modify: `drivers/apple-agx/render-admission/src/driver.c:14-67`
- Modify: `drivers/apple-agx/render-admission/AppleAgxRenderAdmission.vcxproj:33-38`
- Modify: `drivers/apple-agx/render-admission/include/render_admission.h:43-96`
- Create: `drivers/apple-agx/render-admission/src/display.c`

**Interfaces:**
- Consumes: EXP214 `DRIVER_INITIALIZATION_DATA` and existing lifecycle/render callback names.
- Produces: declarations and registration for the common Full Graphics display callbacks; null interrupt/display-only pointers; `display.c` as the sole one-panel VidPn implementation unit.

- [ ] **Step 1: Write failing callback-vector tests**

Replace the old “render contract but no display-only callbacks” expectation with assertions that the driver registers exactly these common Full Graphics display entries:

```python
for callback in (
    "DxgkDdiSetPointerPosition",
    "DxgkDdiSetPointerShape",
    "DxgkDdiIsSupportedVidPn",
    "DxgkDdiRecommendFunctionalVidPn",
    "DxgkDdiEnumVidPnCofuncModality",
    "DxgkDdiSetVidPnSourceVisibility",
    "DxgkDdiCommitVidPn",
    "DxgkDdiUpdateActiveVidPnPresentPath",
    "DxgkDdiRecommendMonitorModes",
    "DxgkDdiQueryVidPnHWCapability",
    "DxgkDdiSetVidPnSourceAddress",
    "DxgkDdiStopDeviceAndReleasePostDisplayOwnership",
):
    self.assertIn(f"initialization.{callback} =", driver)
```

Assert these entries are absent from the initialization table:

```python
for callback in (
    "DxgkDdiInterruptRoutine",
    "DxgkDdiDpcRoutine",
    "DxgkDdiControlInterrupt",
    "DxgkDdiGetScanLine",
    "DxgkDdiPresentDisplayOnly",
    "DxgkDdiSystemDisplayEnable",
    "DxgkDdiSystemDisplayWrite",
):
    self.assertNotIn(f"initialization.{callback} =", driver)
```

Assert `src\display.c` is linked and that project text still contains none of the forbidden AGX subsystem tokens from the existing test.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
python3 -m unittest tests.test_apple_agx_render_admission
```

Expected: failures name missing Full Graphics display registrations, present inert interrupt registrations, and missing `display.c`.

- [ ] **Step 3: Implement the initialization table and declarations**

In `driver.c`, remove assignments to `DxgkDdiInterruptRoutine`, `DxgkDdiDpcRoutine`, and `DxgkDdiControlInterrupt`. Add assignments to the twelve `AdmissionDdi...` display functions listed in Step 1. Do not assign PresentDisplayOnly, GetScanLine, or SystemDisplay callbacks.

In `render_admission.h`, remove the interrupt/DPC/ControlInterrupt declarations and add the exact WDK function-class declarations:

```c
DXGKDDI_SETPOINTERPOSITION AdmissionDdiSetPointerPosition;
DXGKDDI_SETPOINTERSHAPE AdmissionDdiSetPointerShape;
DXGKDDI_ISSUPPORTEDVIDPN AdmissionDdiIsSupportedVidPn;
DXGKDDI_RECOMMENDFUNCTIONALVIDPN AdmissionDdiRecommendFunctionalVidPn;
DXGKDDI_ENUMVIDPNCOFUNCMODALITY AdmissionDdiEnumVidPnCofuncModality;
DXGKDDI_SETVIDPNSOURCEVISIBILITY AdmissionDdiSetVidPnSourceVisibility;
DXGKDDI_COMMITVIDPN AdmissionDdiCommitVidPn;
DXGKDDI_UPDATEACTIVEVIDPNPRESENTPATH AdmissionDdiUpdateActiveVidPnPresentPath;
DXGKDDI_RECOMMENDMONITORMODES AdmissionDdiRecommendMonitorModes;
DXGKDDI_QUERYVIDPNHWCAPABILITY AdmissionDdiQueryVidPnHWCapability;
DXGKDDI_SETVIDPNSOURCEADDRESS AdmissionDdiSetVidPnSourceAddress;
DXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP
AdmissionDdiStopDeviceAndReleasePostDisplayOwnership;
```

Add `src\display.c` to the project. Define every newly registered callback
with its exact WDK signature and a minimal fail-closed body that validates
required pointers, marks other parameters unreferenced, and returns
`STATUS_NOT_SUPPORTED`. `AdmissionDdiRecommendFunctionalVidPn` returns
`STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN`. This keeps the project
linkable while Tasks 3–5 replace each stub through its own RED/GREEN cycle.

- [ ] **Step 4: Run the focused source-contract test**

Run the same unittest command. Expected: callback-vector tests pass; later behavior tests may remain RED until Tasks 2–4.

- [ ] **Step 5: Commit the independently reviewable callback contract**

Stage only the project, header, driver, fail-closed display unit, and focused
test. Commit message:

```text
Define minimal Full Graphics admission vector
```

---

### Task 2: Implement StartDevice and truthful QueryAdapterInfo admission

**Files:**
- Modify: `tests/test_apple_agx_render_admission.py`
- Modify: `drivers/apple-agx/render-admission/include/render_admission.h:16-41`
- Modify: `drivers/apple-agx/render-admission/src/lifecycle.c:22-106`

**Interfaces:**
- Consumes: `DXGKRNL_INTERFACE`, `DXGK_DEVICE_INFO`, and `DxgkCbAcquirePostDisplayOwnership` supplied by dxgkrnl.
- Produces: a nonpaged `ADMISSION_CONTEXT` with copied start/interface/device/POST state; one source and child; QueryAdapterInfo types 1, 16, 29, 34, 35, and 47.

- [ ] **Step 1: Write failing StartDevice and caps tests**

Add tests requiring these context fields and operations:

```python
for token in (
    "DXGK_START_INFO StartInfo",
    "DXGKRNL_INTERFACE Interface",
    "DXGK_DEVICE_INFO DeviceInformation",
    "DXGK_DISPLAY_INFORMATION PostDisplayInformation",
    "BOOLEAN Started",
    "BOOLEAN DisplayActive",
    "BOOLEAN SourceVisible",
    "ULONG CommittedWidth",
    "ULONG CommittedHeight",
    "ULONG CommittedStride",
):
    self.assertIn(token, header)

self.assertIn("DxgkCbGetDeviceInformation", lifecycle)
self.assertIn("DxgkCbAcquirePostDisplayOwnership", lifecycle)
self.assertIn("PostDisplayInformation.Width != 2560", lifecycle)
self.assertIn("PostDisplayInformation.Height != 1600", lifecycle)
self.assertIn("PostDisplayInformation.Pitch != 10240", lifecycle)
self.assertIn("*NumberOfVideoPresentSources = 1", lifecycle)
self.assertIn("*NumberOfChildren = 1", lifecycle)
```

Add tests requiring cases for `DXGKQAITYPE_PHYSICAL_MEMORY_CAPS`, `DXGKQAITYPE_IOMMU_CAPS`, `DXGKQAITYPE_64BITONLYCAPS`, and `DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION`; require `HighestVisibleAddress.QuadPart = 0xFFFFFFFFFFLL`, `iommuCaps->Value = 0`, and `SupportsOnly64Bit = 1`.

Assert the entire lifecycle source lacks `FlipOnVSyncMmIo = TRUE`, `SupportSoftwareDeviceBitmaps`, nonzero preemption/scheduling flags, MMIO, RTKit, UAT, and `DxgkCbNotifyInterrupt`.

- [ ] **Step 2: Run focused tests and verify RED**

Expected: old zero-source StartDevice, missing POST state, missing types 34/35/47/16, and false flip capability fail.

- [ ] **Step 3: Expand the receipt enum and context**

Add receipts for POST acquisition, child/VidPn milestones, and the first SetVidPnSourceAddress attempt. Add the context fields listed in Step 1 plus:

```c
volatile LONG SourceAddressStage;
volatile LONG SourceAddressStatus;
```

Keep all fields nonpaged by retaining `ExAllocatePool2(POOL_FLAG_NON_PAGED, ...)`.

- [ ] **Step 4: Implement StartDevice**

Use exact argument validation, copy `DXGK_START_INFO` and `DXGKRNL_INTERFACE`, zero and fill `DXGK_DEVICE_INFO`, acquire POST ownership, require nonzero physical address and exact 2560x1600x10240 geometry, then set one source/child and initialize:

```c
context->Started = TRUE;
context->DisplayActive = TRUE;
context->SourceVisible = TRUE;
context->CommittedWidth = 2560;
context->CommittedHeight = 1600;
context->CommittedStride = 10240;
*NumberOfVideoPresentSources = 1;
*NumberOfChildren = 1;
```

On any failure, leave both output counts zero and return the exact failing status. StopDevice clears started/display/interface state but does not touch hardware.

- [ ] **Step 5: Implement QueryAdapterInfo as a switch**

For every supported structure: reject null output, distinguish `STATUS_BUFFER_TOO_SMALL`, zero the entire documented structure, and set only the spec values. For `DXGK_DRIVERCAPS`, set one node, `HighestAcceptableAddress=-1`, and `SupportNonVGA=TRUE`; leave `WDDMVersion`, flip, scheduling, preemption, and memory-management caps zero. Record type/size/status once at exit.

- [ ] **Step 6: Run focused tests and verify GREEN**

Run:

```bash
python3 -m unittest tests.test_apple_agx_render_admission
```

Expected: all lifecycle/capability assertions pass.

- [ ] **Step 7: Commit lifecycle and caps**

Commit message:

```text
Add one-panel Full Graphics lifecycle caps
```

---

### Task 3: Implement child, monitor, pointer, and visibility callbacks

**Files:**
- Modify: `tests/test_apple_agx_render_admission.py`
- Modify: `drivers/apple-agx/render-admission/src/display.c`

**Interfaces:**
- Consumes: started `ADMISSION_CONTEXT` and fixed POST geometry.
- Produces: child 0 as one connected internal output, fixed preferred monitor mode, zero VidPn hardware capabilities, and truthful pointer/visibility behavior.

- [ ] **Step 1: Write failing simple-display tests**

Require `display.c` to implement each registered function and assert:

- child relation buffer requires space for the descriptor plus terminating zero descriptor;
- child 0 is `TypeVideoOutput`, internal, always connected, no SDTV, no orientation awareness;
- descriptor query returns `STATUS_GRAPHICS_CHILD_DESCRIPTOR_NOT_SUPPORTED` for child 0;
- monitor mode is 2560x1600 progressive and preferred with unspecified timing frequencies;
- hardware VidPn capabilities are fully zeroed;
- hidden pointer for source 0 returns success while visible cursor returns not supported;
- source IDs other than 0 or `D3DDDI_ID_ALL` are invalid;
- no function references registry, MMIO, allocation, wait, delay, interrupt, AGX, or pageable helpers.

- [ ] **Step 2: Run focused tests and verify RED**

Expected: functions are declared/registered but missing from the new display source.

- [ ] **Step 3: Implement child and descriptor callbacks**

Port only the validated structural behavior from the repository’s EXP404 admission source. Use `RtlZeroMemory`, source/child ID 0, and exact status values; do not copy external Asahi code.

- [ ] **Step 4: Implement monitor and hardware-cap callbacks**

Use the WDK VidPn target-mode-set interface to allocate one mode, set fixed geometry, unspecified frequencies, progressive scan, preferred mode, and add it. Release an allocated mode on any add failure.

- [ ] **Step 5: Implement pointer and visibility callbacks**

SetPointerPosition validates source 0. It succeeds only when the source is inactive or the requested pointer is hidden; otherwise return `STATUS_NOT_SUPPORTED`. SetPointerShape always returns `STATUS_NOT_SUPPORTED` after validation.

SetVidPnSourceVisibility accepts source 0 or all sources. Update `SourceVisible` only for a state that does not claim a hardware blank/power transition. A request that would require an unimplemented hardware operation returns `STATUS_NOT_SUPPORTED` and leaves state unchanged.

- [ ] **Step 6: Run focused tests and verify GREEN**

- [ ] **Step 7: Commit simple display callbacks**

Commit message:

```text
Add fixed-panel Full Graphics callbacks
```

---

### Task 4: Implement the one-path VidPn state machine

**Files:**
- Modify: `tests/test_apple_agx_render_admission.py`
- Modify: `drivers/apple-agx/render-admission/src/display.c`

**Interfaces:**
- Consumes: dxgkrnl VidPn, topology, source-mode-set, and target-mode-set interfaces from `ADMISSION_CONTEXT.Interface`.
- Produces: validation and modality for empty VidPn or exactly one source-0 to target-0 path; fixed mode commit without a new primary or scanout claim.

- [ ] **Step 1: Write failing VidPn behavior tests**

Require these functions and invariants:

```python
for name in (
    "AdmissionDdiIsSupportedVidPn",
    "AdmissionDdiRecommendFunctionalVidPn",
    "AdmissionDdiEnumVidPnCofuncModality",
    "AdmissionDdiCommitVidPn",
    "AdmissionDdiUpdateActiveVidPnPresentPath",
):
    self.assertIn(name, display)
```

Assert empty VidPn support, maximum one 0-to-0 path, graphics source mode 2560x1600x10240 A8R8G8B8 direct pixels, preferred target mode, identity-only transform support, correct acquisition/release calls, and no MMIO or AGX token.

- [ ] **Step 2: Run focused tests and verify RED**

- [ ] **Step 3: Implement IsSupportedVidPn**

Return supported for an empty handle. Otherwise query the VidPn interface and topology, accept zero paths or exactly one source-0 path to target 0, reject clones and other IDs, and release every acquired path.

- [ ] **Step 4: Implement modality enumeration**

Use the pinned WDK V1 interfaces. Walk at most one path, validate 0-to-0, create/assign a fixed source mode when not pinned by the current pivot, create/assign a fixed preferred target mode when needed, publish identity-only rotation/scaling support, and release every acquired object on every exit path.

- [ ] **Step 5: Implement CommitVidPn and path update**

Commit accepts source 0, an empty/powered-off topology, or one pinned fixed graphics mode on one 0-to-0 path. It updates only `DisplayActive`, committed geometry, and transform state. It never changes a primary address or claims a hardware latch.

UpdateActiveVidPnPresentPath accepts only source/target 0, identity rotation, and identity/centered scaling permitted by the fixed-mode contract.

RecommendFunctionalVidPn returns `STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN` without mutation, matching the Microsoft sample’s optional recommendation behavior.

- [ ] **Step 6: Run focused tests and the existing Windows display contract tests**

Run:

```bash
python3 -m unittest \
  tests.test_apple_agx_render_admission \
  tests.test_apple_agx_admission_package \
  tests.test_apple_agx_windows_package \
  tests.test_j313_agx_g2_contract \
  tests.test_j313_agx_g2_m1n1_policy \
  tests.test_j313_agx_g2_mu_profile \
  tests.test_verify_j313_agx_g2_aml
```

Expected: all tests pass without changing EXP404 or full AppleAgx behavior.

- [ ] **Step 7: Commit the VidPn state machine**

Commit message:

```text
Implement one-path Full Graphics VidPn
```

---

### Task 5: Implement the truthful primary-address boundary

**Files:**
- Modify: `tests/test_apple_agx_render_admission.py`
- Modify: `drivers/apple-agx/render-admission/src/display.c`
- Modify: `drivers/apple-agx/render-admission/src/callbacks.c:1-183`

**Interfaces:**
- Consumes: `DXGKARG_SETVIDPNSOURCEADDRESS` and the context’s committed fixed mode.
- Produces: bounded in-memory source-address receipt and fail-closed status; no flip, latch, VSYNC, or interrupt.

- [ ] **Step 1: Write a failing IRQL-safety test**

Extract the SetVidPnSourceAddress function body and assert it contains argument/source/flag validation, `InterlockedExchange` snapshots, and `STATUS_NOT_SUPPORTED`; assert it contains none of:

```python
for forbidden in (
    "AdmissionRecord", "Zw", "IoOpenDeviceRegistryKey", "ExAllocate",
    "KeWait", "KeDelay", "READ_REGISTER", "WRITE_REGISTER",
    "DxgkCbNotifyInterrupt", "DxgkCbNotifyDpc",
):
    self.assertNotIn(forbidden, source_address_body)
```

Require `FlipOnVSyncMmIo` to remain zero and require the full project not to assign ISR/DPC/ControlInterrupt.

- [ ] **Step 2: Run focused tests and verify RED**

- [ ] **Step 3: Implement SetVidPnSourceAddress**

Mark the function nonpageable by keeping it outside any PAGE segment. Validate non-null context/args, started state, source ID 0, and the fixed committed mode. Store entry and terminal status with `InterlockedExchange`; return `STATUS_NOT_SUPPORTED`. Do not inspect or translate PrimaryAddress beyond bounded validation because no allocation contract exists.

- [ ] **Step 4: Tighten fail-closed cleanup callbacks**

Remove the unused `AdmissionDdiControlInterrupt` function. Ensure DestroyDevice/DestroyContext/DestroyProcess do not claim cleanup of an object that was never created; return invalid parameter or no-op only where the WDK signature is void. Keep UMD `OpenAdapter10_2` returning `E_NOTIMPL`.

- [ ] **Step 5: Run focused and integrated tests**

Run the Task 4 integrated command. Also run:

```bash
git diff --check
```

- [ ] **Step 6: Commit the primary boundary**

Commit message:

```text
Fail closed at Full Graphics primary address
```

---

### Task 6: Build and freeze the EXP405 candidate

**Files:**
- Modify: `investigation/EXPERIMENTS.md`
- Modify: `investigation/GPU_CURRENT_STATE.md`
- Create: `.local/experiments/EXP-20260904-405-full-graphics-admission/EXP405-source.tar.gz`
- Create: `.local/experiments/EXP-20260904-405-full-graphics-admission/build-exp405.ps1`
- Create: `.local/experiments/EXP-20260904-405-full-graphics-admission/package/*`

**Interfaces:**
- Consumes: tested render-admission source and EXP214-controlled builder.
- Produces: exact signed ARM64 KMD+UMD package, source/test composite, artifact hashes, and preregistered hardware contract.

- [ ] **Step 1: Run the complete focused offline gate**

Run the Task 4 integrated Python suite and `git diff --check`. Record exact pass count.

- [ ] **Step 2: Freeze source and tests**

Hash the sorted per-file SHA-256 list for `drivers/apple-agx/render-admission` plus `tests/test_apple_agx_render_admission.py`, archive the same paths, and record both hashes.

- [ ] **Step 3: Preregister EXP405 before build/hardware**

Record `WHY THIS HYPOTHESIS`, `ATOMIC CONTRACT`, Windows/AGX/translation/unknown sections, root/m1n1/Mu identities, dirty diff hashes, source archive, exact build/run commands, expected first callback, failure criteria, evidence paths, and recovery artifacts.

- [ ] **Step 4: Build on FRYZZING**

Copy the frozen archive and a guarded build script to `C:\Users\pauls`. Verify archive hash before extraction into a new nonexisting root. Run the repository build script or equivalent exact UMD-then-KMD MSBuild commands. Require zero warnings/errors, Universal validation, Inf2Cat, and signer thumbprint `E9BE15BD2A184BFABA0C8035B3C620C58037A241`.

- [ ] **Step 5: Retrieve and verify artifacts**

Copy KMD SYS/PDB/INF/CAT/CER, UMD DLL/PDB if emitted, build result, and package ZIP. Recompute all hashes locally and update the preregistration before staging.

---

### Task 7: Run EXP405, clean exactly, and advance immediately

**Files:**
- Create: `.local/experiments/EXP-20260904-405-full-graphics-admission/phase-a-exp405.ps1`
- Create: `.local/experiments/EXP-20260904-405-full-graphics-admission/phase-b-arm-exp405.ps1`
- Create: `.local/experiments/EXP-20260904-405-full-graphics-admission/collect-phase-b-exp405.ps1`
- Create: `.local/experiments/EXP-20260904-405-full-graphics-admission/phase-b-health-exp405.ps1`
- Create: `.local/experiments/EXP-20260904-405-full-graphics-admission/cleanup-exp405.ps1`
- Modify: `investigation/EXPERIMENTS.md`
- Modify: `investigation/GPU_CURRENT_STATE.md`

**Interfaces:**
- Consumes: exact EXP405 package and the accepted current/non-AGX platform pair.
- Produces: a hardware verdict at the first Full Graphics boundary, exact cleanup, current-G2 baseline, and the next preregistered causal target.

- [ ] **Step 1: Verify both control planes**

Attempt bounded Air SSH and inspect the active runner/proxy endpoints. Ask for no physical action while SSH or Running proxy is already available.

- [ ] **Step 2: Phase A stage-only gate**

Boot current-compatible non-AGX, verify clean state, copy exact package/scripts, run `pnputil /add-driver <INF>` without `/install`, resolve exact `oemN.inf`, and require all 0–180 second health checkpoints.

- [ ] **Step 3: Arm and naturally bind under current G2**

Record event IDs/boot time/INF hash, shut down normally, and launch accepted m1n1 plus current G2 with foreground output tee'd to an experiment log. Do not run `probe.py` while the runner owns the serial.

- [ ] **Step 4: Collect the first live or recovery verdict**

If SSH appears, immediately collect APPL0002 status, exact INF/SYS/UMD, service state, GPU LUID, QueryAdapterInfo type/status receipts, child/VidPn/source-address receipts, display inventory, SetupAPI, Defender, and System events. If the device is Code0, run a separate 180-second health window.

If SSH does not appear and the runner resets to proxy, boot current-compatible non-AGX, collect cached receipts, SetupAPI, System/Defender EVTX, minidump, exact package state, and resolve the dump with the candidate PDB.

- [ ] **Step 5: Assign the verdict from evidence**

- Confirm Full Graphics admission only if types 34/35, one-panel callbacks, no Code31/43, GPU LUID, stable SSH, no AGX route enable, and the full health window all pass.
- Reject at the exact first non-success callback or PnP state.
- Mark inconclusive only for an external observer/build/platform confounder.

- [ ] **Step 6: Clean the exact experiment package**

After evidence, use current-compatible non-AGX to delete only the resolved exact `oemN.inf` with `/uninstall`, matching stopped service/SYS/staging/receipts if needed. Verify clean health, then restore current G2 Code28 and no new 41/129/1001.

- [ ] **Step 7: Continue without a milestone stop**

If EXP405 confirms admission, update the compact state and immediately derive the pinned-WDK allocation/context/paging/fence translation against the existing system-aperture/local-memory/UAT implementation. If EXP405 stops earlier, preregister the smallest integrated discriminator at the first failed callback. Do not return to KMDOD, EXP398–404 packages, builder archaeology, Defender changes, or AGX hardware before evidence permits it.

---

## Plan Self-Review Checklist

- Every spec section maps to Tasks 1–7.
- No task changes Mu, m1n1, AGX ownership, or capability scope.
- The only hardware candidate is the integrated EXP405 contract; trivial callbacks are not split into separate reboots.
- All deterministic source changes have an explicit RED and GREEN step.
- The build and hardware steps name exact tools, profiles, health windows, evidence, and recovery.
- The plan contains no unresolved implementation choice or approval gate.
