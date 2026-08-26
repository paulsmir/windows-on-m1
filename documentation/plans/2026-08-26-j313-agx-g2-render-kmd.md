# J313 AGX G2 Render-Only Windows KMD Plan

## Objective

Move the already-qualified private AGX render lifecycle from the assisted
USB/Python harness into a test-signed ARM64 Windows WDDM kernel-mode driver.
The first G2 adapter is render-only: it exposes no display target and leaves
the proven GOP/DCP physical scanout path unchanged.

G2 is complete only when Windows owns AGX firmware, one protected UAT address
space, one context, one queue, interrupt-backed fences, timeout detection and
reset without a host process or per-command EL2 emulation.

## Proven starting point

- root branch `feature/j313-gpu-acceleration` at `86f521b`;
- m1n1 runtime `f76b63ade8756571acd91400283ee68b2f1d65ce`;
- Mu `8b4dc4b4e3ff8606d0af36163acf9de79b7b4737`;
- immutable recovery `.local/recovery/STABLE-j313-8core-native-input-v1/`;
- EXP-20260826-111: ten cold-reset-separated TA+3D renders, ten distinct boot
  cookies and proxy identities, canonical output SHA-256
  `b88456a302464b8f4735e8b09c14e004a9ad8df40fd17562e3d28c48de0ea126`,
  6.318--7.001 ms completion and complete teardown in every cycle.

The accepted G1R fixture is a diagnostic oracle. It must not become a permanent
command-replay API or production UMD.

## Architecture decisions

1. **Direct ownership.** Windows KMD, not m1n1 Python, owns firmware, UAT,
   queues, interrupts, fences and reset while the adapter is started.
2. **No steady-state hypercalls.** Stage 2 and vGIC provide bounded resource
   access and interrupt delivery only. Draw, dispatch, fence and present paths
   never cross USB or synchronously enter EL2.
3. **Separate scanout.** Basic Display and the physical DCP framebuffer remain
   the boot/recovery display until G5 presentation is qualified.
4. **Protected GPU VA.** No arbitrary guest physical address may enter a GPU
   page table. Every mapping belongs to a driver allocation and process/address
   space with checked bounds and permissions.
5. **Interrupt-backed progress.** Queue polling is diagnostic only. Dxgkrnl is
   notified from ISR/DPC after a hardware completion fence.
6. **Bounded failure.** Every firmware wait, submit, fence, teardown and reset
   has a fixed deadline. Timeout invokes a reviewed reset and cannot hang EL2.
7. **Stable baseline isolation.** G2 firmware and driver artifacts are separate
   opt-in candidates. The stable recovery image, native input driver, NVMe,
   eight-core CPU contract and normal Windows profile are never overwritten by
   an unqualified G2 build.

## Primary references

- Microsoft WDDM operation flow and display-miniport DDIs, including
  `DxgkInitialize`, allocation, context, submit, ISR/DPC and TDR contracts;
- Microsoft render-only WDDM and MCDM guidance. MCDM is reference material, not
  the target, because the final goal is graphics and DWM rather than compute;
- Asahi Linux AGX architecture and kernel ownership model;
- Mesa Asahi command generation and the pinned m1n1 firmware/UAT/queue
  structures already qualified by G1R.

External source may inform behavior. Code is reused only after an explicit
license-compatibility review and provenance record.

## Deliverables and gates

### Task 1 — Freeze a generated G2 resource contract

Files:
- `config/j313-agx-g2.json`
- `tools/generate_j313_agx_g2_contract.py`
- `drivers/apple-agx/shared/include/j313_agx_g2.generated.h`
- `tests/test_j313_agx_g2_contract.py`

Define exact J313/G13/V13_5 MMIO, ASC, UAT/DART, IRQ, firmware ABI, page-size,
context, queue and private-VA ranges from the accepted contract. Generation is
deterministic and fails on overlap, unsupported version, guest/DCP/NVMe range,
unbounded size or a mismatch with the G1R pins.

Gate: generator tests fail before implementation, then generated C and JSON
round-trip byte-for-byte and the complete public suite passes.

### Task 2 — Add a host-testable ownership core

Files:
- `drivers/apple-agx/shared/include/apple_agx_state.h`
- `drivers/apple-agx/shared/src/apple_agx_state.c`
- `drivers/apple-agx/shared/tests/apple_agx_state_test.c`

Implement a platform-neutral fail-closed state machine:
`Off -> ResourcesValidated -> FirmwareOwned -> QueueReady -> Running`, plus
`Resetting`, `Stopped` and terminal `Failed`. It owns deadlines, monotonic fence
rules, mapping inventory and rollback order without MMIO or WDK dependencies.

Gate: transition, timeout, duplicate-fence, stale-completion, mapping-overlap,
partial-start and repeated-stop tests pass under ASan/UBSan where available.

### Task 3 — Build a test-signed ARM64 render-only KMD skeleton

Files:
- `drivers/apple-agx/windows/AppleAgx.inf`
- `drivers/apple-agx/windows/AppleAgx.vcxproj`
- `drivers/apple-agx/windows/src/driver.c`
- `drivers/apple-agx/windows/src/adapter.c`
- `drivers/apple-agx/windows/src/resources.c`
- `drivers/apple-agx/windows/scripts/build-driver.ps1`

Register with `DxgkInitialize`, implement the required render-only callback
table, validate the exact ACPI resources and report no child/display target.
The first build performs no AGX write and advertises no usable render node.

Gate: official ARM64 WDK build, code analysis, INF validation and callback-table
tests pass. Installation/removal must not change Basic Display or input.

### Task 4 — Publish an opt-in ACPI device and stage-2 contract

Files:
- `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgx.asl.inc`
- `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/DSDT.asl`
- bounded m1n1 stage-2/vGIC resource policy files

Expose vendor device `APPL0002` only in an explicit G2 firmware profile. `_CRS`
contains only reviewed MMIO and one level-sensitive interrupt; `_DSD` carries
the version and immutable contract hash. Normal stable firmware omits or
disables the device.

Gate: AML tests and a Windows enumeration-only boot show the exact devnode and
resources with the KMD disabled. No AGX clock, firmware or MMIO write occurs.

### Task 5 — Port firmware and power ownership

Files:
- `drivers/apple-agx/windows/src/firmware.c`
- `drivers/apple-agx/windows/src/power.c`
- `drivers/apple-agx/windows/src/mmio.c`

Port only the reviewed G1 lifecycle required for J313/V13_5: power/clock order,
ASC startup, management heartbeat, shared fault snapshot and reverse teardown.
All register access uses generated typed offsets and bounded waits.

Gate: a test-signed driver starts and stops firmware ten times across cold
boots, produces a heartbeat, leaves no fault and preserves stable Windows
shutdown. It does not create a render context yet.

### Task 6 — Implement protected UAT memory and allocations

Files:
- `drivers/apple-agx/windows/src/uat.c`
- `drivers/apple-agx/windows/src/memory.c`
- `drivers/apple-agx/windows/src/allocation.c`

Create one non-zero protected GPU address space and map only KMD-owned private
allocations. Implement cache maintenance, guard pages, teardown invalidation and
VidMm-facing allocation bookkeeping. Context zero and arbitrary guest PFNs are
forbidden.

Gate: deterministic mapping tests plus hardware negative tests prove invalid,
overlapping, executable-data and out-of-range mappings fail without AGX access.

### Task 7 — Submit one fixed command and complete one fence

Files:
- `drivers/apple-agx/windows/src/context.c`
- `drivers/apple-agx/windows/src/queue.c`
- `drivers/apple-agx/windows/src/interrupt.c`
- `drivers/apple-agx/windows/src/fence.c`

Create one context and queue. A development-only administrator escape requests
one built-in bounded no-op/fence operation; user bytes and addresses are never
accepted. Hardware IRQ enters the KMD ISR, DPC validates the monotonically
increasing fence and notifies Dxgkrnl.

Gate: ten cold boots each complete the exact fence within the fixed deadline;
missing, duplicate, stale and spurious interrupts fail closed. Polling cannot
make the gate pass.

### Task 8 — Implement TDR and reset qualification

Files:
- `drivers/apple-agx/windows/src/reset.c`
- `drivers/apple-agx/windows/src/diagnostics.c`
- `drivers/apple-agx/windows/tools/AppleAgxDiag/`

Implement timeout detection, stop/reset/restart callbacks, bounded crash data
and a signed diagnostic tool. Inject a controlled non-completing development
operation and prove Dxgkrnl recovery, allocation quarantine and clean restart.

Gate: repeated timeout, malformed request, process termination, driver restart,
shutdown and surprise-removal tests never hang Windows, NVMe, input or EL2.

### Task 9 — G2 release qualification

Run ten cold boots and a 60-minute queue/fence/reset stress with physical panel,
NVMe, all eight CPUs and native input active. Require no BugCheck, watchdog,
WHEA, storage reset, input loss, AGX fault, leaked UAT root or unbounded wait.

Only then mark the render-only adapter qualified and begin G3 off-screen pixels.
Do not call the desktop accelerated and do not replace the stable recovery tag.

## Later no-loss path

- G3: private off-screen shader and golden-image readback;
- G4: minimum Direct3D UMD with validated resource and shader submission;
- G5: DWM presentation into DCP-compatible allocations, retaining GOP fallback;
- release: measured removal of transitional copies, production power/thermal
  policy, suspend/resume, TDR and representative Direct3D stress.

The final hot path is Direct3D runtime -> UMD -> Dxgkrnl/KMD shared queues ->
AGX firmware/hardware. USB, Python, framebuffer scraping and synchronous EL2
requests remain diagnostics and contribute no steady-state performance cost.

