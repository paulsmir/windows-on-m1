# EXP214-Derived Full Graphics Admission Design

Date: 2026-09-04

Status: approved design, implementation not started

## Goal

Advance the current J313 Windows GPU boundary from the hardware-validated
EXP404 display-only KMDOD to a separate, minimal, truthful Full Graphics WDDM
3.0 miniport. The candidate must use `DxgkInitialize`, expose exactly one
internal source and target, complete the common display/VidPn admission path,
answer QueryAdapterInfo types 34 and 35 truthfully, and keep the current Air
alive without Code 31, Code 43, watchdog, or reset.

This is an admission milestone, not the final GPU milestone. A successful
candidate is followed immediately by the coherent allocation/context contract,
Windows-driven scanout, and then real AGX submission/completion/fence work.

## Proven Starting Point

### Current platform

- Accepted assisted m1n1:
  `fae3444cc289cf52ea12b81b9db8f3d8bf24bd084f899a751321d2048d9a525a`.
- Current G2 Mu:
  `16c177182e96b63eac852dcfb185cebba9c1d91943c6402106a640848ddc5e06`.
- Current-compatible non-AGX recovery Mu:
  `279bd36ad3bbb1ee5e2393fa965343ea856b4c2b0dd4df2b2add6a8010e3f32c`.
- Clean Windows baseline: APPL0002 Code 28 and unbound; no project Display
  package, AppleAgx service, or SYS; SSH, eight CPUs, NVMe, xHCI, input, and
  Defender/WdFilter healthy.

### EXP404 display-only proof

EXP404 proved one internal 2560x1600x32 display, StartDevice, child topology,
VidPn enumeration/commit/visibility, pointer handling, and PresentDisplayOnly
with APPL0002 Code 0 for a separate 180-second health window. Its runner never
enabled guest AGX interrupts 880 or 881 and observed no watchdog or reset.

EXP404 also proved that an admission driver which does not own AGX interrupt
status or acknowledgement must not register an inert ISR/DPC pair. Registering
that pair made Dxgkrnl connect and unmask current-G2 level interrupts and led to
the earlier cumulative-DPC watchdog.

### EXP214 Full Graphics control

The byte-exact EXP214 source/test composite is
`25718ba071971c8cb94a6847908f0a722ffdb4f6767b9ca4d548514ea6713d63`.
It proves the Full Graphics build and ABI foundation:

- `DRIVER_INITIALIZATION_DATA`;
- ARM64 size 1296 bytes under the pinned WDK;
- `DXGKDDI_INTERFACE_VERSION_WDDM3_0`;
- `DxgkInitialize`;
- DriverEntry, AddDevice, StartDevice, and QueryAdapterInfo execution;
- the direct FRYZZING build rooted under `C:\Users\pauls`, inheriting
  `Directory.Build.props` and `Directory.Build.targets` with WDK/SDK
  `10.0.28000.2526`.

EXP214 is a source/build control only. Its old binary must not be installed.

## Sources and Specifications Inspected

### Project sources

- `drivers/apple-agx/render-admission/src/driver.c`
- `drivers/apple-agx/render-admission/src/lifecycle.c`
- `drivers/apple-agx/render-admission/src/callbacks.c`
- `drivers/apple-agx/render-admission/src/receipts.c`
- `drivers/apple-agx/render-admission/include/render_admission.h`
- `drivers/apple-agx/render-admission/AppleAgxRenderAdmission.inf`
- `drivers/apple-agx/render-admission/umd/src/umd.c`
- `drivers/apple-agx/admission/src/driver.c`
- `drivers/apple-agx/admission/src/lifecycle.c`
- `drivers/apple-agx/windows/src/driver.c`
- `drivers/apple-agx/windows/src/adapter.c`
- `drivers/apple-agx/windows/src/display_windows.c`
- `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgx.asl.inc`
- `m1n1_windows/src/hv_irq_routes.c`
- `m1n1_windows/src/hv_vgic.c`
- `m1n1_windows/src/hv_exc.c`

### Primary Windows references

- Microsoft `DRIVER_INITIALIZATION_DATA` and `DxgkInitialize` documentation.
- Microsoft display miniport DriverEntry documentation.
- Microsoft QueryAdapterInfo structures from the pinned WDK 28000 headers.
- Microsoft VidPn callback documentation.
- Microsoft `DxgkDdiSetVidPnSourceAddress` IRQL, flip, and VSYNC contract.
- Microsoft KMDOD sample, used only for display callbacks documented as common
  to Full Graphics and display-only miniports.

### AGX reference

Asahi and m1n1 establish that a real AGX owner initializes firmware, queues,
memory translation, and interrupt status/acknowledgement as one hardware
lifecycle. No Asahi code is copied. The admission candidate deliberately owns
none of that lifecycle.

## Alternatives Considered

### 1. Extend the separate EXP214-derived render-admission driver

Selected. This retains the hardware-proven Full Graphics ABI and builder while
keeping the next candidate free of accumulated power, memory, RTKit, queue, and
scanout code. The first failed callback remains attributable.

### 2. Add an admission compile profile to the accumulated AppleAgx driver

Rejected for this boundary. It would link and conditionally bypass many owners
at once, making a pre-Start or early VidPn failure ambiguous. It is appropriate
only after the minimal Full Graphics contract is hardware-proven.

### 3. Convert the EXP404 KMDOD into a Full Graphics driver

Rejected. It would mix `KMDDOD_INITIALIZATION_DATA` assumptions with the Full
Graphics ABI, lose EXP214 as the exact control, and risk transferring
display-only scheduler and presentation behavior into a different driver model.

## Layer Ownership

| Layer | Admission-stage owner | Behavior in this candidate |
|---|---|---|
| ACPI publication and resources | Mu | Publishes current G2 APPL0002 unchanged. |
| Physical-to-guest IRQ routing | m1n1 | Routes remain registered but masked because the driver registers no interrupt callbacks. |
| WDDM lifecycle and VidPn | Windows render-admission KMD | Owns DriverEntry, Add/Start/Stop/Remove, QueryAdapterInfo, one-panel topology, and VidPn validation. |
| UMD load boundary | Render-admission UMD | Exports `OpenAdapter10_2` and returns `E_NOTIMPL`; it does not claim D3D functionality. |
| AGX power, firmware, RTKit, UAT, queues | Nobody in this candidate | No access, mapping, request, or capability. |
| Allocation, context, scheduling, submission | Future coherent milestone | Registered only where Full Graphics initialization requires an entry point; unimplemented operations fail closed and advertise no corresponding capability. |
| Scanout and VSYNC | Future scanout milestone | No flip claim and no successful primary-address programming. The inherited POST image is not counted as Windows-driven scanout. |

## Driver Architecture

The implementation remains under `drivers/apple-agx/render-admission` and
continues to link only admission-specific sources. A new `src/display.c` owns
the one-panel display/VidPn callbacks. It uses only the adapter context and
Dxgkrnl-provided VidPn interfaces; it does not include shared AGX hardware code.

`ADMISSION_CONTEXT` gains only the state required by the Windows-facing
contract:

- physical device object;
- copied `DXGK_START_INFO` and `DXGKRNL_INTERFACE`;
- `DXGK_DEVICE_INFO`;
- `DXGK_DISPLAY_INFORMATION` acquired from POST ownership;
- started, display-active, and source-visible flags;
- committed width, height, stride, and pixel format;
- bounded receipt state for the first unsupported post-admission callback.

No MMIO pointer, physical-memory object, DART/UAT mapping, RTKit endpoint,
queue, fence, or GPU context is added.

## Initialization Callback Contract

The driver keeps the EXP214 `DRIVER_INITIALIZATION_DATA` layout and existing
Full Graphics lifecycle/render entry points, then adds the documented common
display group:

- `DxgkDdiSetPointerPosition`
- `DxgkDdiSetPointerShape`
- `DxgkDdiIsSupportedVidPn`
- `DxgkDdiRecommendFunctionalVidPn`
- `DxgkDdiEnumVidPnCofuncModality`
- `DxgkDdiSetVidPnSourceVisibility`
- `DxgkDdiCommitVidPn`
- `DxgkDdiUpdateActiveVidPnPresentPath`
- `DxgkDdiRecommendMonitorModes`
- `DxgkDdiQueryVidPnHWCapability`
- `DxgkDdiSetVidPnSourceAddress`
- `DxgkDdiStopDeviceAndReleasePostDisplayOwnership`

The following pointers remain zero:

- `DxgkDdiPresentDisplayOnly`, because this is a Full Graphics miniport;
- `DxgkDdiInterruptRoutine`;
- `DxgkDdiDpcRoutine`;
- `DxgkDdiControlInterrupt`;
- `DxgkDdiGetScanLine`;
- `DxgkDdiSystemDisplayEnable` and `DxgkDdiSystemDisplayWrite`, because this
  candidate does not yet own a crash-safe scanout framebuffer.

ISR, DPC, and ControlInterrupt form one atomic omission. None may be restored
until a real AGX or scanout interrupt source has implemented status detection,
acknowledgement, duplicate suppression, completion ordering, and teardown.

## StartDevice Contract

`AdmissionDdiStartDevice` performs this exact sequence:

1. Validate all five required arguments.
2. Record entry using the existing PASSIVE_LEVEL device receipt transport.
3. Copy `DXGK_START_INFO` and `DXGKRNL_INTERFACE` into nonpaged context.
4. Call `DxgkCbGetDeviceInformation` and retain the returned device data.
5. Call `DxgkCbAcquirePostDisplayOwnership`.
6. Require the proven J313 geometry: width 2560, height 1600, pitch 10240, and
   a nonzero physical address.
7. Set `NumberOfVideoPresentSources = 1` and `NumberOfChildren = 1`.
8. Initialize the one fixed source/target state as visible but not yet backed by
   a Windows allocation.
9. Record success and return `STATUS_SUCCESS`.

The callback does not map the POST framebuffer or touch AGX/DCP registers. POST
ownership supplies geometry and preserves the inherited visible image only; it
does not prove a Windows primary or flip.

## QueryAdapterInfo Contract

The candidate supports only the following queries, with complete size and null
validation:

| Query | Result |
|---|---|
| `DXGKQAITYPE_DRIVERCAPS` | Zeroed caps, one asymmetric processing node, `HighestAcceptableAddress = -1`, `SupportNonVGA = TRUE`; no flip, scheduling, preemption, kernel-command-buffer, MapAperture2, direct-flip, or per-engine-TDR capability. `WDDMVersion` remains zero because the field is reserved for this interface level. |
| `DXGKQAITYPE_WDDMDEVICECAPS` | Zeroed structure with `WDDMVersion = DXGKDDI_WDDMv3_0`. |
| `DXGKQAITYPE_PHYSICAL_MEMORY_CAPS` (type 34) | Zeroed structure with `HighestVisibleAddress = 0xFFFFFFFFFF`, matching the J313/T8103 40-bit physical visibility limit without claiming allocation support. |
| `DXGKQAITYPE_IOMMU_CAPS` (type 35) | Zeroed structure and `Value = 0`; no Windows logical-remapping or IOMMU callback is implemented. |
| `DXGKQAITYPE_64BITONLYCAPS` | `SupportsOnly64Bit = 1`, matching the ARM64-only KMD/UMD package. |
| `DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION` | Zeroed structure. No virtual-mode or advanced display capability is claimed. |

All other query types return `STATUS_NOT_SUPPORTED`. The receipt records the
exact query type, output size, and status before any later decision.

`FlipOnVSyncMmIo` is explicitly zero. The candidate cannot report a real latch
or VSYNC and therefore must not cause Dxgkrnl to invoke primary-address changes
at DIRQL or connect an interrupt solely to satisfy a false capability.

## One-Panel Topology and VidPn

The topology is fixed and intentionally narrow:

- source ID 0;
- target/child ID 0;
- `TypeVideoOutput`;
- internal connection technology;
- always connected;
- no rotation, cloning, scaling, or SDTV support;
- one graphics source mode, 2560x1600, stride 10240,
  `D3DDDIFMT_A8R8G8B8`, direct pixel access;
- one preferred progressive target mode at the same geometry;
- unspecified pixel, horizontal-sync, and vertical-sync frequencies because
  no VSync-control contract exists.

The callbacks validate handles, IDs, topology cardinality, pinned modes, and
paths before mutation. They accept the empty VidPn and the single 0-to-0 path.
Any second source, target, clone path, unsupported transform, or inconsistent
mode fails with the corresponding graphics status.

`CommitVidPn` may return success only for the already active fixed POST mode or
an empty/powered-off path. It updates context state but does not claim a new
scanout address or hardware mode transition.

`SetVidPnSourceVisibility` accepts visibility that preserves the current active
fixed source state. It must not claim that the panel was blanked or powered
when no hardware operation occurred.

Pointer position succeeds only for the documented hidden-pointer/no-active-path
case. A visible hardware cursor remains unsupported.

`QueryVidPnHWCapability` returns a fully zeroed capability structure.

## Primary Address Boundary

`DxgkDdiSetVidPnSourceAddress` is registered because it is part of the Full
Graphics display contract, but this admission candidate has no Windows primary
allocation and no latch/VSYNC path. It therefore:

1. validates adapter state, source ID, arguments, and flags;
2. records the request only when called at PASSIVE_LEVEL;
3. stores a bounded in-memory stage/status snapshot regardless of IRQL;
4. returns `STATUS_NOT_SUPPORTED` without touching MMIO or claiming a flip.

Because `FlipOnVSyncMmIo` is zero, a normal admission call is expected at
PASSIVE_LEVEL. No registry, allocation, lock, wait, delay, or pageable helper is
reachable from a higher-IRQL call.

The first observed invocation of this callback is the next causal boundary. A
firmware-owned POST framebuffer is not substituted for the Windows primary and
does not count as a successful SetVidPnSourceAddress.

## Render and Memory Fail-Closed Contract

The UMD remains loadable and exports `OpenAdapter10_2`, but returns `E_NOTIMPL`.
No Direct3D feature level or Windows-visible acceleration is claimed.

Existing allocation, device, context, paging, patch, render, submit, preempt,
and fence callbacks remain fail closed. Cleanup callbacks may return success
only for an object that was actually created. The candidate neither allocates
an AGX-visible object nor fabricates a context, fence, DMA buffer, or completion.

The one 3D node describes physical engine topology, not current acceleration
support. Node metadata remains bounded to ordinal 0. No scheduling or
preemption capability is advertised, and UMD refusal prevents a render device
from being presented as usable.

## Diagnostics and IRQL Safety

Registry receipts are best-effort diagnostics and may execute only from
callbacks documented and observed at PASSIVE_LEVEL. They contain no explicit
`ZwFlushKey`.

Any callback that can run at DIRQL, DISPATCH_LEVEL, arbitrary IRQL, or bugcheck
time must use only nonpaged context and bounded interlocked memory snapshots.
It must not call registry, file, allocation, wait, delay, pageable, or logging
code that can block.

The foreground m1n1 runner is the exclusive owner of the control serial. Runner
output is tee'd to an experiment-local log. No concurrent `probe.py` is allowed
while the guest is active. SSH and the separate vUART endpoint are the only
observers during a run.

## Offline Verification

Implementation follows RED to GREEN for deterministic contracts:

- exact Full Graphics initialization layout and callback set;
- atomic absence of ISR/DPC/ControlInterrupt/GetScanLine;
- absence of PresentDisplayOnly;
- truthful zero flip/VSync/preemption/scheduler claims;
- StartDevice one-source/one-child and fixed geometry validation;
- Type34 and Type35 size, zeroing, and values;
- one-panel child and VidPn invariants;
- no AGX, MMIO, power, RTKit, UAT, HVC, queue, render, or scanout dependency in
  the project;
- IRQL-safe SetVidPnSourceAddress failure path;
- UMD export and fail-closed behavior.

The focused render-admission suite, relevant WDDM package tests, and current G2
contract tests must pass before WDK build. The source/test composite and frozen
archive are hashed before dispatch to FRYZZING.

The build must reproduce the EXP214 environment exactly:

- direct source root under `C:\Users\pauls`;
- inherited `C:\Users\pauls\Directory.Build.props` and targets;
- MSBuild `18.1.0-preview-25527-05`;
- NuGet WDK/SDK `10.0.28000.2526`;
- ARM64 Release;
- code analysis;
- Universal validation;
- Inf2Cat;
- test signing.

The build records ZIP, SYS, INF, CAT, CER, PDB, UMD, source archive, and test
hashes. No package can be staged until these identities are entered in the
experiment ledger.

## Hardware Experiment

The integrated candidate uses the established two-phase protocol.

### Phase A: stage only

1. Boot accepted m1n1 plus current-compatible non-AGX Mu.
2. Verify SSH, eight CPUs, healthy NVMe/xHCI/input, APPL0002 absent, and no
   project package/service/SYS.
3. Verify every candidate file and signer.
4. Run `pnputil /add-driver <exact INF>` without `/install`.
5. Record exact `oemN.inf` and wait 180 seconds.
6. Require no service/module/receipt, no 41/129/1001, and healthy system state.

### Phase B: natural current-G2 bind

1. Arm the exact staged INF and event baseline.
2. Shut down cleanly and launch accepted m1n1 plus current G2 Mu.
3. Do not use `/install`, rescan, enable, restart-device, or concurrent proxy
   access.
4. Observe runner route transitions and bounded SSH availability.
5. Collect DriverEntry/Add/Start/Query/child/VidPn receipts, APPL0002 problem
   code, service/SYS hashes, GPU LUID, video-controller inventory, SetupAPI,
   events, and dump if present.
6. If Windows reaches Code 0, run a separate 180-second live health window.

### Pass

- exact package naturally selected;
- DriverEntry, DxgkInitialize, AddDevice, and StartDevice return success;
- QueryAdapterInfo types 34 and 35 return success with the specified values;
- one internal source/target reaches the implemented child and VidPn callbacks;
- APPL0002 has neither Code 31 nor Code 43;
- GPU LUID is present;
- SSH, eight CPUs, NVMe, xHCI, input, and Defender/WdFilter remain healthy;
- no 41, 129, 1001, watchdog, reset, or AGX interrupt-route enable through the
  full post-bind window.

### Failure and verdict

The first non-success receipt, PnP problem, unexpected route enable, timeout,
or reset is the next boundary. No lower AGX subsystem is blamed without direct
evidence. A crash dump is resolved with the exact private SYS/PDB.

Evidence is collected before rollback. Every result is classified confirmed,
rejected, or inconclusive; no package is silently retried.

## Recovery

After evidence, boot accepted m1n1 plus current-compatible non-AGX Mu. Resolve
the exact published INF by its recorded hash, then:

- `pnputil /delete-driver <exact oemN.inf> /uninstall`;
- remove only the matching stopped service/SYS if package removal leaves it;
- remove experiment staging and experiment receipt values;
- rescan;
- prove the package, service, SYS, bind, and module are absent;
- prove SSH, eight CPUs, NVMe, xHCI, and input are healthy.

Finally restore normal current G2 and require fresh APPL0002 Code 28, no GPU
package/service/SYS, and no new 41/129/1001.

## Next Milestones After Admission

Success does not end development. The next work proceeds in this order:

1. Derive the coherent WDDM allocation/context/paging/fence contract from the
   pinned WDK and map it to the accepted system-aperture/local-memory/UAT model.
2. Replace the fail-closed UMD/KMD object path with real allocation and context
   creation, without enabling unsupported capability bits.
3. Implement Windows-driven primary scanout through the existing nonblocking
   scanout ABI, real DCP latch, exactly-once VSYNC, and repeated flips.
4. Reintroduce ISR/DPC only with real status/ack/duplicate-suppression and
   teardown behavior.
5. Connect the existing validated EXP208/materialized TA+3D graph to a Windows
   submit, hardware completion, and monotonically advancing Windows fence.
6. Prove at least ten repeated submits, no stale/duplicate completion, no TDR,
   no Event 129/watchdog, and stable display/input/eight-CPU health.

The goal remains a basic accelerated Windows-visible workload, not merely a
buildable or admitted driver.
