# Windows Mesa/AGX Graphics Architecture

## Status and scope

This design continues the already approved Full Graphics WDDM architecture.
It does not change Mu, ACPI, the retained-root broker, context-0 ownership,
context-63 paging, IRQ routing, DCP ownership, or the proven AGX firmware
lifecycle. It replaces the incomplete hand-written user-mode graphics surface
with a reusable Mesa frontend and adds the smallest versioned KMD translation
needed for dynamic AGX work.

The final target is a real Apple AGX path from Windows OpenGL through physical
TA/3D execution, exact WDDM fences, and presentation. The first application
acceptance target remains Counter-Strike 1.6 in OpenGL mode. Software rendering
is permitted only as an explicitly labelled build/window-system control and is
never a graphics PASS.

Standing project authorization requires autonomous implementation without a
new approval gate because this design preserves the approved WDDM, platform,
and ownership model. Hardware experiments still change one causal variable and
use the existing exact-package rollback discipline.

## Evidence and primary sources inspected

- Current project HEAD `f9e976bc1a17898775ffbcb36fd4d188402c4229`:
  `drivers/apple-agx/render-admission/umd/src/umd.c`,
  `src/umd_render_windows.c`, `src/submission_windows.c`,
  `src/backend_platform_windows.c`, `src/render_backend_image.c`, the current
  INF, allocation contracts, scheduler, completion, and standard-present code.
- Pinned WDK 10.0.26100 `d3d10umddi.h`, SHA-256
  `61899403d94840fab282dbb7da6faf234e2954bbdb47e3455f0f0572eb4e723a`.
  `D3DWDDM1_3DDI_DEVICEFUNCS` contains the complete state/resource/draw table;
  `D3D11DDICAPS_3DPIPELINESUPPORT` selects a real pipeline level. A nonzero
  bit is not an adapter-admission switch.
- Microsoft WDDM operation flow:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-operation-flow>.
  UMD creates contexts and allocations through runtime callbacks, emits a
  command buffer, and submits through `pfnRenderCb`/`pfnPresentCb`; KMD validates
  and translates before `Patch`/`SubmitCommand` and fence completion.
- Microsoft OpenGL ICD loading contract:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/loading-an-opengl-installable-client-driver>.
  OpenGL name/version/flags are queried through `KMTQAITYPE_UMOPENGLINFO` and
  are separate from Direct3D `UserModeDriverName`.
- Microsoft `D3DKMTSetVidPnSourceOwner` contract:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtsetvidpnsourceowner>.
  This is an ICD operation, not a generic user-process fullscreen shortcut.
- Microsoft graphics-driver-samples commit
  `de4a2161991eda254013da6c18226f5ea06e4a9c`, especially the Render-Only
  `RosUmd` function table and device/context flow. The repository is MIT. It is
  a contract reference, not a source of AGX commands.
- Mesa commit `9aa1215f878b504f66159dd2ead4c7973142126e`:
  WGL frontend/targets, D3D10 UMD frontend, Asahi Gallium driver, AGX compiler,
  and `agx_device_ops_t`. Mesa documents WGL `opengl32.dll` plus
  `libgallium_wgl.dll` on Windows and separates frontends, Gallium, drivers,
  and winsys. Relevant sources are MIT.
- Asahi Linux commit `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`:
  firmware 13.5 support, VM/BO/queue/submit validation, and render work
  materialization. Asahi GPU files used as a reference are dual MIT/GPL; any
  reused portion must retain its MIT notice and be independently reviewed.
- Current m1n1 commit `c6d10e04afdad5314e8ac1e67bc3919b094ab000`:
  `proxyclient/m1n1/agx/uapi.py`, `context.py`, `render.py`, and `shim.py` prove
  the historical division between a Mesa-generated encoder/attachment
  descriptor and owner-side firmware work construction. m1n1 is MIT.
- EXP640 proves repeated physical TA/3D, full-frame results, fences, latches,
  ownership and teardown for the fixed EXP208-derived image. EXP646 proves the
  current Apple D3D11 device is unsupported while Basic/WARP controls pass.
  EXP647 proves a normal interactive process receives `STATUS_ACCESS_DENIED`
  before Render when it attempts direct exclusive VidPN ownership.

## Rejected approaches

### Set a pipeline capability bit and fill the table with stubs

Rejected. A feature-level bit states that the corresponding pipeline exists.
The current UMD implements only resource/direct-flip fragments. Null or
fail-closed shader/draw/state functions do not make feature level 9_1 truthful.

### Port the Microsoft RosUmd implementation as the AGX driver

Rejected as the hardware path. Its table is useful as a WDK ordering reference,
but its renderer and command stream are tied to its sample software/VC4 KMD.
Porting it would create a second graphics implementation and would not advance
AGX shader or queue correctness.

### Continue direct-KMT exclusive-owner experiments

Rejected. EXP641 and EXP647 already distinguish session ownership and access
control before KMD. A proper ICD/runtime path must own fullscreen transitions;
another producer timing or token variant does not reduce the GPU boundary.

### Recommended: Mesa frontends over one Windows AGX winsys/transport

Mesa supplies OpenGL/WGL state tracking, fixed-function compatibility, shader
lowering, NIR, the AGX compiler, resource layouts, and AGX encoder generation.
The project supplies a Windows winsys and WDDM translation. OpenGL and a later
D3D frontend share the same BO, queue, fence, and presentation implementation.
This avoids a second shader compiler and keeps capabilities derived from real
Gallium screen support.

## Ownership model

### Windows/DXGK owned

- adapter/device/context lifetime;
- WDDM allocations and VidMm residency;
- allocation handles, segment placement, paging, and exact physical backing;
- scheduler fences and notification/DPC completion;
- active/pending display ownership and standard presentation.

### Mesa/UMD owned

- OpenGL/D3D API state;
- NIR and AGX shader compilation;
- AGX userspace encoder contents;
- logical BO references and subranges for the current process;
- batching decisions before a WDDM submission.

### KMD/backend owned

- validation of every submitted handle, range, flag, count, format, and queue;
- translation of the bounded public command description to firmware-private
  work objects;
- retained-root broker calls, barriers, TLB maintenance, queue publication,
  rollback, completion, reset, and cleanup;
- all firmware-private pages, private mappings, work queues, stamps, events,
  and root identity.

UMD never receives physical addresses, firmware-private page-table contents,
firmware object pointers, or an interface that can mutate another allocation.

## Components

### 1. `agx_win32_abi`

A small shared C ABI describes query results, BO references, one render command,
sync inputs, and one output fence. Every record has magic, version, total byte
size, generation, reserved-zero fields, and bounded counts. Integer overflow,
unknown flags/versions, misalignment, duplicate write ownership, and ranges
outside the referenced allocation fail closed.

The ABI carries WDDM allocation handles plus offsets and lengths. It never
carries trusted CPU, GPU, or physical pointers. Any user virtual address is
copied only through the documented runtime callback/OS probing contract and is
not dereferenced by a DDI without validation.

### 2. `agx_win32_transport`

This user-mode layer has two adapters over one logical interface:

- WGL/ICD uses D3DKMT open-device-context-allocation-render calls;
- a future Direct3D UMD uses the corresponding runtime callbacks.

Both produce the same command bytes and allocation list. Render/Present, not a
private escape, is the high-frequency path. Escapes are limited to versioned
read-only parameter queries and explicit low-frequency lifecycle operations
that have no WDDM callback equivalent.

### 3. Mesa Windows Asahi winsys

The winsys implements the platform-dependent operations behind Mesa Asahi:
device parameters, BO allocation/map/unmap, VM binding, queue/context creation,
submission, fence wait/query, sharing policy, and frontbuffer presentation.
The first version is deliberately single adapter, single process, single queue,
one color attachment, no compute, no sparse resources, no external sharing,
and no multisampling. Gallium capabilities are derived from implemented ops;
they are not hardcoded above the transport.

### 4. Dynamic KMD render translator

`DxgkDdiRender` validates the ABI and allocation list, snapshots only bounded
scalar metadata, and produces the existing DMA/private records. `Patch` binds
actual resident placements. `SubmitCommand` queues the already validated job.

The first dynamic job reuses the hardware-proven EXP208 firmware skeleton but
replaces hardcoded encoder, attachment, geometry, and shader bindings only when
each field has an explicit owner and range proof. The frozen EXP208 image is a
byte-level control, not a second runtime implementation. As coverage grows,
the translator follows the current Asahi Linux firmware-13.5 materializer.

### 5. Presentation

Windowed WGL presentation is runtime-managed. A rendered inactive WDDM
allocation is completed, then submitted through the standard present path and
retained until exact replacement/latch retirement. Fullscreen ownership is
requested only by the installed OpenGL ICD after the Windows OpenGL/runtime
transition, never by a standalone producer shortcut.

The qualification scanout path remains available only as a diagnostic control
and is not reported as standard Present or final acceptance.

### 6. Frontends

The first production frontend is Mesa WGL/OpenGL because CS 1.6 is the explicit
acceptance target. Build both native ARM64 (diagnostic applications) and x86
(legacy CS 1.6 under Windows-on-ARM emulation) binaries. The INF publishes
`OpenGLDriverName`, `OpenGLVersion`, and `OpenGLFlags` only after loader tests
prove the exact architecture-specific ICD is valid.

Mesa's D3D10 UMD frontend is a later consumer of the same Gallium screen and
transport. The current hand-written Direct3D UMD keeps pipeline caps zero until
the replacement frontend has executable feature-level coverage. DWM may use
WARP during the OpenGL bring-up; software output is never confused with AGX.

## Ordered implementation checkpoints

1. Build Mesa WGL with softpipe for ARM64 and x86 as an uninstalled control.
   Prove loader, pixel-format, context, and SwapBuffers behavior; record the
   renderer as software. This validates only the toolchain and WGL packaging.
2. Implement and test `agx_win32_abi` codecs and ownership/range validation in
   the project without changing KMD capabilities or installing a package.
3. Implement a fake user/kernel transport pair and run Mesa winsys lifecycle
   tests: query, BO create/map, bind, submit, fence, present, and rollback.
4. Cross-build Mesa WGL against the fake transport for ARM64 and x86. Require
   exact dependency manifests and no fallback masquerading as AGX.
5. Add the KMD dynamic-clear translator. Compare its validated job against the
   frozen hardware-proven EXP208 control offline, then run one hardware clear
   from Mesa-originated command bytes and require a WDDM fence.
6. Add dynamic vertex/fragment encoder, shader BO, texture BO, and attachment
   support one invariant at a time. Hardware checkpoints are triangle, textured
   triangle, repeated frames, and correct standard Present.
7. Publish the ICD in the exact package only after the loader and hardware
   renderer path pass. Verify renderer/version strings, submission counters,
   fences, and physical latches.
8. Run a minimal OpenGL compatibility workload, then CS 1.6 menu, map load, and
   a real game scene. The final known-good package remains installed and active.
9. Integrate the Mesa D3D frontend only after the shared transport is stable;
   advertise only the feature level passed by its conformance subset.

## Failure and recovery rules

- Every command or allocation failure has an exact transport guard/status;
  no validation path returns fabricated success.
- A submitted fence completes exactly once. Device removal cancels outstanding
  work and prevents reuse of its generation.
- A failure before queue publication rolls back all Windows-owned mappings.
  A failure after publication retains ownership until completion/reset resolves
  it.
- No full-frame scan, disk flush, compiler work, or user-memory copy occurs at
  DIRQL or under the scheduler spinlock.
- Hardware runs remain hash-gated and experiment-local. Evidence precedes exact
  package cleanup and ordinary377/392 restoration, except for the final accepted
  package, which remains installed.

## Acceptance

Intermediate readiness is recorded separately for loader, BO/VM transport,
dynamic translation, hardware submit, completion/fence, Present, and OpenGL.
The mission ends only when Apple KMD/UMD/ICD are active; Windows-originated AGX
TA/3D and exact fences repeat; frames present through the supported path; the
OpenGL renderer is the AGX implementation; and Counter-Strike 1.6 renders a
real scene without an immediate deterministic TDR, bugcheck, hang, corruption,
or GPU-caused input/storage regression.
