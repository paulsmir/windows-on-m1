# UMD Admission and Resource Lifetime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct the existing UMD's format-query and allocation-lifetime
contracts and obtain an exact cold-path callback trace without advertising a
Direct3D pipeline level.

**Architecture:** Keep the current direct-flip-only UMD truthful. Associate
created allocations with their runtime resource, deallocate non-shared
primaries synchronously as D3D11 requires, and queue all other destruction in
device-owned memory for a Flush/DestroyDevice drain. Because the current UMD
cannot emit immediate-context commands, its Flush owns no render batch; a
future command frontend must add pfnRenderCb before it may set that state.

**Tech Stack:** WDK 10.0.26100 D3D10/11 UMD DDI, C, MSVC ARM64, executable
mock runtime callbacks, OutputDebugString/DBWIN capture, current D3D11 probe.

**Spec:** User review captured at
`/Users/pavel/.codex/attachments/4bdef9f9-ea93-44c3-8bdf-81a2bd569a4e/pasted-text.txt`
plus Microsoft `CheckFormatSupport`, `Changes from Direct3D 10`,
`pfnDeallocateCb`, and `Primary Exceptions` contracts.

## Global Constraints

- Keep `D3D11DDICAPS_3DPIPELINESUPPORT.Caps == 0`.
- Do not add shader, draw, state, RTV, AGX, DCP, firmware, UAT, or capability
  behavior in this change.
- Unsupported existing DXGI formats return capability zero without setting a
  device error; null output still sets `E_INVALIDARG`.
- Created allocations are associated with the exact runtime resource handle.
- Non-shared primaries deallocate inside DestroyResource. Shared or non-primary
  resources are queued and drained by Flush/DestroyDevice.
- The queue owns independent heap nodes and is protected by the device lock;
  no pointer into runtime-owned resource storage survives DestroyResource.
- A deallocation failure is retained for one later drain and is reported via
  `pfnSetErrorCb`; no handle is zeroed before ownership is transferred.
- Cold-path trace never changes the functional return value.

---

### Task 1: Write and run the exact UMD mock-runtime test

**Files:**
- Create: `drivers/apple-agx/render-admission/umd/tests/umd_contract_windows.c`
- Create: `drivers/apple-agx/render-admission/umd/tests/build-contract-test.ps1`
- Create: `tests/test_apple_agx_umd_contract_scripts.py`

**Interfaces:**
- The test calls `OpenAdapter10_2`, adapter functions, CreateDevice, the returned
  device functions, CreateResource, CheckFormatSupport, Flush,
  DestroyResource, DestroyDevice, and CloseAdapter.
- Fake callbacks record exact create-context, allocate, deallocate, set-error,
  and destroy-context arguments.

- [ ] Write a Windows test requiring pipeline caps zero; unsupported
  `DXGI_FORMAT_R8G8B8A8_UNORM` returns caps zero without SetError; an exact
  non-shared primary is associated with its runtime resource and deallocated
  once during DestroyResource; a non-primary/opened resource is queued until
  Flush; a failed deallocation remains queued and retries once at
  DestroyDevice.
- [ ] Add a PowerShell build that compiles the real `umd.c`,
  `direct_flip_contract.c`, and `render_allocation.c` with the test under the
  pinned ARM64 VS/WDK environment.
- [ ] Run against current source and preserve the expected RED: format query
  calls SetError and DestroyResource never calls pfnDeallocateCb.

### Task 2: Implement cold-path trace and format semantics

**Files:**
- Modify: `drivers/apple-agx/render-admission/umd/src/umd.c`
- Modify: `drivers/apple-agx/render-admission/umd/AppleAgxRenderAdmissionUmd.vcxproj`

- [ ] Add `APPLE_AGX_UMD_ADMISSION_TRACE` OutputDebugString records at
  OpenAdapter10_2, GetSupportedVersions, GetCaps, pipeline caps, and
  CreateDevice entry. Enable it only for the current diagnostic build property.
- [ ] Change CheckFormatSupport so every existing unsupported format returns
  zero caps without SetError; retain `E_INVALIDARG` for a null output pointer.
- [ ] Rebuild the test and require the format portion GREEN while the lifetime
  assertions remain RED.

### Task 3: Implement exact resource association and retirement

**Files:**
- Modify: `drivers/apple-agx/render-admission/umd/src/umd.c`

- [ ] Extend device state with an SRW-locked head/tail retirement queue.
- [ ] Extend resource state with runtime resource, kernel resource, primary,
  shared, and a preallocated retirement node.
- [ ] Set `D3DDDICB_ALLOCATE.hResource` to `RuntimeResource.handle`; preserve
  `hKMResource` and allocation handle after successful allocation.
- [ ] For non-shared primaries call pfnDeallocateCb synchronously with the
  runtime resource handle. Queue shared/non-primary/opened resources.
- [ ] Add `AdmissionUmdFlush` to the returned device table. It drains the queue
  because the current UMD has no immediate-context command producer. Its
  implementation must fail closed if future state says a render batch exists.
- [ ] Drain or retry the queue before DestroyDevice returns, then destroy the
  kernel context. Report failures via SetError.
- [ ] Run the complete mock-runtime test GREEN and the existing admission/
  direct-flip tests GREEN.
- [ ] Commit implementation and append a machine-readable ledger row.

### Task 4: Build and run one loader/callback discriminator

**Files:**
- Create artifact workflow under `.local/experiments/EXP648-umd-admission/`
- Modify: `investigation/EXPERIMENTS.md`
- Modify: `investigation/GPU_CURRENT_STATE.md`

- [ ] Pinned build/sign/hash the unchanged-cap UMD/KMD package and DBWIN
  collector plus existing D3D11 probe.
- [ ] Preregister one run. Capture process architecture, image load/unload,
  OpenAdapter/GetSupportedVersions/GetCaps/CreateDevice messages, exact
  D3D11CreateDevice HRESULT, feature level, and device health.
- [ ] Do not issue Render/Present. Collect evidence, exact package cleanup,
  restore ordinary377/392, and select the first real missing frontend contract
  from the callback boundary.
- [ ] Resume the Mesa/frontend plan immediately; no UMD pipeline readiness is
  raised by a trace-only result.
