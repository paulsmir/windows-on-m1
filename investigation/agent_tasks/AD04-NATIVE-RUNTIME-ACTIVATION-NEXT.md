# AD04 next seam: runtime-owned native device

2026-09-13; bounded forward source review while the integrated executable is
being completed. No code/platform edits, hardware operation or readiness claim.
Keep the physical Render/Patch/Submit model and existing ownership/capture path.

## Selected next implementation

Wire the existing per-device Windows factory to the real native screen/context
after runtime device initialization, then connect the pinned Mesa D3D10 frontend
to that factory. This is the smallest route to **runtime-supplied** pKTCallbacks
and the existing composer/pfnRenderCb, rather than another renderer/allocator.
Keep the installed pipeline mask zero until the selected feature-level contract
is implemented and verified; the current one-triangle subset does not satisfy it.

Inspected repository sources (bullet paths are under `drivers/apple-agx/`):

- `mesa/winsys/agx_d3d10_windows.cpp:57-114`: shared Windows initialization is
  already device-scoped, but CreateDevice still invokes the old pipe wrapper.
- `mesa/winsys/agx_win32_pipe_screen.c:355-449`: that wrapper supplies resource
  mapping and context lifetime only; its context has no draw/state/shader/flush
  implementation. It cannot be promoted to the native provider by a flag.
- `render-admission/umd/src/umd_runtime_device.c:87-180`: obtains actual runtime
  callbacks/context/command buffers and initializes the authoritative screen
  owner. `umd_asahi_owner.c`, `umd_asahi_batch_adapter.c`, and
  `umd_draw_composer.c` provide the existing native association and submission.
- `mesa/scripts/native-asahi-batch-lifecycle.py:226` exposes the real
  `AgxWin32AsahiScreenCreate`; `agx_win32_asahi_runtime_test.c` now exercises its
  native resource/shader/clear/draw/flush path. Its controlled platform parameter
  input is an offline fixture, not a hardware inventory.
- `render-admission/umd/AppleAgxRenderAdmissionUmd.vcxproj` currently links the
  old UMD, screen and composer, not the native owner/batch/runtime library.
- Installed `umd.c:244-336` selects a limited WDDM1_3 table and advertises no
  implemented pipeline. `docs/superpowers/specs/accelerated-desktop-contract.json`
  selects D3D10_0/FL10_0 with Mesa frontend reuse; its implementation/test
  inventory remains unfinished.

Pinned Mesa source root is `.local/reference/mesa` in the main checkout; the
frontend paths are under `src/gallium/frontends/d3d10umd/`.
`Adapter.cpp:66-91` creates a screen during OpenAdapter;
`Device.cpp:113-145` selects D3D10 and creates a context with null private data;
`:164-282` installs its DDI table. `Shader.cpp` and `Draw.cpp` supply the existing
DDI-to-Gallium translation. `DxgiFns.cpp:56-66` uses flush_frontbuffer, and
`Device.cpp:303` returns DXGI_STATUS_NO_REDIRECTION. Those are existing software
target assumptions, not an implemented Windows hardware presentation contract.

## Concrete wiring and ownership

1. In `AGX_D3D10_WINDOWS_DEVICE`, own the already existing
   `AGX_WIN32_ASAHI_BACKEND`, `ADMISSION_UMD_ASAHI_OWNER`, native `pipe_screen`
   and native `pipe_context`. After `AdmissionUmdRuntimeDeviceInitialize`,
   obtain `AdmissionUmdAsahiOwnerOperations`, configure
   `AdmissionUmdAsahiBatchOperations`, construct the native screen and context
   with this device owner, and return that context from
   `AgxD3d10WindowsContext`. Reuse ScreenBuffers and native BO identities.
2. Defer native screen creation from Mesa OpenAdapter until CreateDevice has
   the runtime allocator/context callbacks. Keep the adapter's metadata owner;
   pass the per-device owner through the real frontend/device path. Do not add
   a process-global owner, adapter-global rendering context or second pipe
   resource implementation. Close native context/screen before Windows device
   finalization, preserving the tested busy/retirement/destruction-retry rules.
   In particular, the existing wrapper's early ScreenBeginClose cannot precede
   native teardown unchanged: the native backend itself is a live screen owner.
   Serialize device close, retire its native batches, detach the native owner,
   then finalize the Windows screen/context.
3. Link the same hash-recorded native library/compiler closure and existing
   owner/adapter into the installed UMD project. Reuse Mesa's DDI table and
   Draw/Shader/Resource state translation. Do not route draw through the old
   map-only pipe or enable the excluded software target/D3DKMT.cpp shims.
4. Supply native constructor parameters from the verified J313 platform/device
   facts. Audit the fields actually consumed by this subset. Do not copy the
   offline zero-filled params struct into a hardware provider. Initialization,
   power, interrupts, UAT ownership and recovery remain in their existing layers.
5. Extend the existing UmdContractTest factory case to reach native
   screen/context and the same real producer/consumer gate. Then exercise the
   frontend DDI Draw/Flush path with the runtime-device callbacks. Keep distinct
   assertions for native construction, actual callback entry, immutable KMD
   plan/DMA/root routing, ordered retirement and two-device ownership isolation.

## Runtime admission is a supported contract

Classification: FULL GRAPHICS. Microsoft documents that CreateDevice supplies
the device callback table and selects the corresponding DDI interface:
[D3D10 initialization](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/initializing-communication-with-the-direct3d-version-10-ddi).
The pipeline mask communicates a supported pipeline level:
[D3D11DDI_3DPIPELINELEVEL](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/ne-d3d10umddi-d3d11ddi_3dpipelinelevel).

ATOMIC CONTRACT: the reported supported DDI version, CreateDevice's matching
DDI table/layout, and advertised supported feature-level behavior must agree.
The pinned `d3d10umddi.h` table/union is the ABI source; the selected inventory
maps its obligations to Mesa source. A registered Draw callback or one native
triangle does not implement required indexed/instanced/resource/shader-stage,
format and lifetime behavior. Review and complete the exact selected inventory
and implemented capabilities before changing the pipeline mask. Do not group
unrelated WDDM scheduler/capability experiments into this admission work.

## Earliest hardware request and fallback distinction

The first real Direct3D-runtime hardware checkpoint is a runtime-created device
whose frontend emits the existing native 16x16 clear/triangle request through
composer/pfnRenderCb and reaches physical AGX completion plus its Windows fence.
Use the existing hardware client project for the application entry point; it
currently has no D3D10/11 CreateDevice mode. A successful request is still not
DXGI Present, DWM or accelerated-desktop acceptance.

There is a narrower qualification fallback in the existing
`windows/one-shot/apple_agx_d3dkmt_render.c`: it already creates a real KMT
device/context, allocations and calls D3DKMTRender. A native mode could delegate
the existing owner/composer's callback operations to those real KMT APIs and
reuse the exact native producer, with no packet replay or fake handles. That
would prove a Windows KMT-backed physical request while caps stay zero. It must
be labelled **KMT qualification**, not runtime-supplied Direct3D callbacks, and
does not replace the selected production factory/frontend work. No new helper
project is needed or selected.

Before either hardware checkpoint: actual integrated producer/consumer execution
PASS, applicable lifetime/failure regressions, x64 execution and ARM64 complete
stack build/link, native root/attachment receipts, driver stack/storage bounds,
static/Universal/Inf2Cat/sign/hash gates, and exact source/package manifests.
Then follow the compact playbook: both control-plane checks, current ordinary
GPU-visible no-package baseline, preregister one causal run, one exact package,
evidence collection and exact-package rollback. Do not reuse historical health
as current state. Native qualification receipts must identify the actual graph;
the legacy fixed-slot receipt generators cannot supply that proof.

Standard allocation-backed DXGI Present/shared-resource/redirection behavior,
accelerated DWM, desktop stability/reset and the compact-state acceptance
criteria remain later required work. This plan does not reopen KMD/platform
architecture or authorize capability advertising based on the one-draw subset.
