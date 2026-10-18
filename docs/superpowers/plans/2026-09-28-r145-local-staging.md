# R145 — local GPU backing with CPU staging

Specification: `investigation/analysis/R142-render-backing.md`; task authority:
`/Users/pavel/public_windows/.local/tandem/NEXT_TASK_R145.md`.
Base: 79e5b983. Offline only: no Air, package, firmware, signer or caps change.

## Evidence and contract

EXP857 receipt still rejects Render ordinal11/read1/VA0x2f0000 with scattered
segment0 logical PTEs. G3 high-table initialization succeeded. Saved DWM1220
dump instead faults in DXGI factory destruction during exception unwinding;
no evidence identifies that crash as a GPU execution/completion result.
Builder is permitted only for saved-evidence analysis and offline compilation.

Inspected sources: R142 source inventory and current Asahi mmu.rs map_node,
Mesa agx_state.c/agx_batch.c; m1n1 hv_agx_gpuva_v5.c; Mu MemoryInitPeiLib and
J313AppleAgxAbiAdmission.asl.inc; allocation_windows.c, render_win32_transport.c,
gpuva_g3_windows.c, gpuva_g3_paging_windows.c, umd_win32_screen.c,
umd_gpuva_windows.c, agx_win32_asahi_bo.c, agx_win32_gpuva.c/batch.c and
agx_d3d10_windows.cpp. Official Microsoft allocation flags, allocation info,
GPU upload heaps, allocation usage tracking, Escape HardwareAccess and
GetHandleData contracts inspected. Exact links belong in the final analysis.

VidMm retains allocation/residency/VA ownership. UMD owns CPU synchronization
and staging; KMD validates/copies through existing full-local CPU view under
its process graph lock. Mu/m1n1 retain reservation, DMA validation, interrupts,
power and recovery ownership. Windows permits scattered system fallback for
CPU-visible allocations; AGX requires native16K execution backing. No fake
contiguity or direct user pointer/physical address escape is allowed.

## Task 1: Fail executable BO allocation safely

Repair EXP855D's unchecked agx_bo_create -> agx_bo_map NULL path in the Windows
Mesa source transformation. Follow the complete shader error propagation path,
including allocation/map failure and cleanup, without a fake shader or abort.
Own build-native-asahi-state.py and focused shader-failure tests only.
First write/run a deterministic production-path failure replay, then repair it.
Use CC=/tmp/agx-clang-wrapper for host tests; no package/build on Air.
Do not edit external Mesa reference or copy external code without license review.
Do not commit until the controller completes full-suite verification and reads
the canonical review file. Report changed files, RED/GREEN commands, any ABI
compile requirement, and remaining uncertainty.

## Task 2: Specify and implement the validated local-copy boundary

Use a versioned buffered request, runtime allocation handle, allocation offset,
process GPUVA and mapping generation. GetHandleData(DeviceSpecific) must resolve
an open allocation belonging to args hDevice; process/context handles are checked
against attached objects. Match each ResidentPte allocation/offset to the resolved
allocation. Reject system/private/table backing, stale mappings, busy GPU/leases,
closed objects and overflow. Prevalidate an entire bounded chunk before copying.
Only the full LocalView supplies CPU addresses. No allocation flags enabled
until the UMD consumer and regression gates are complete. HardwareAccess plus
MakeResident/WaitPaging and render completion are required at the caller.

## Task 3: Connect canonical/staging allocation and synchronization

GPU execution allocations use CpuVisible0/local2 only. CPU staging uses ordinary
CPU-visible system-capable allocation; no Lock of canonical allocations. Preserve
class0 CDD policy. All native BO classes, imported presentation storage, partial
CPU writes and GPU-write readback need coherent ownership. Keep staging current
after GPU writes; copies must precede externally observable completion and
presentation. Lifetime/rollback must preserve both handles on uncertain failure.
No flag-only intermediate result may be presented as the fix.

## Task 4: Verification, review and commit ledger

Production RED/GREEN for allocation placement, staging lifecycle and shader NULL
path; real G3/G4 regressions for profiles16/64. Full suite compared with the base;
user clarified that acceptance is no new failures/errors versus the measured
unchanged base15 failures/38 errors/2 skips; the task's two-failure wording was wrong.
Offline native callback compilation where affected. Fresh final review, fix
material findings with regressions, then commit without attribution and append
implemented CHANGES.csv rows with full implementation hashes.

## Later hardware discriminator (not authorized here)

Proposed EXP858 single variable is R145 canonical/staging backing versus EXP857,
using identical R143 firmware. Prove native producer contents in local mapped
leaves and bounded exhaustion/eviction behavior before claiming execution or
frames. Recovery remains immutable EXP377/385 hidden exact cleanup after Code0,
then ordinary EXP377/392 Code28. No launch or preregistration in this task.
