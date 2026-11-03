# Q22: two paging serialization boundaries, no steady Encoder cache churn

Offline base `fddcffd648db2cbb228e62b0a63e0e0d4b8ac187`.
**This is an investigation and current-contract regression, not a performance
fix.** No driver/firmware change or new runtime switch is implemented.
The KMD's existing CPU update path cannot simply bypass its quiescence wait:
the broker independently forbids publication while that owner has a live job.
An independent paging worker also does not remove node-0 fence ordering.

The requested immediate-publication optimization therefore needs a broader
ownership/protocol change. This note does not prove that every conceivable
KMD-only redesign is impossible; deferred graph publication and CPU admission
would be a different architecture with additional lifetime obligations.

## Evidence and kernel path

[Q21b](D3D12-Q21b-map-paging-hold.md) establishes EXP2114's UMD leaf: 21.560 ms
waiting for MapGpuVirtualAddress's returned paging fence on a fresh Encoder
BO, size bucket <1 MiB. Head submit/completion are 1456749188/1457265108 at
24 MHz (21.496667 ms). Next submit 1457309134 is 1.834417 ms after completion.
It does **not** identify the precise kernel substage or exact BO identity.
EXP2110/2113/2114 are the saved machine observations; no live inspection was
performed. Only those ledger entries were consulted.

Paths below start at `drivers/apple-agx/` unless otherwise stated.
In the tables, `src/` and `umd/` abbreviate paths beneath
`drivers/apple-agx/render-admission/`.

| Stage | Current behavior | Source |
|---|---|---|
| WDDM entry points | BuildPagingBuffer, SubmitCommandVirtual and SetRootPageTable registered; page-table updates arrive as BuildPagingBuffer operations, not a separate registered UpdatePageTable DDI | `render-admission/src/driver.c:79,103,109`; `src/paging_windows.c:47-54` |
| Declaration | One execution node, PagingNode=0; GPU_PHYSICAL update addressing, explicit invalidation, address-space-idle requirement clear | `shared/include/apple_agx_gpuva_g3_caps.h:86-102`; `src/lifecycle.c:817-820,856-864` |
| PTE application | Both accepted GPU_PHYSICAL and CPU_VIRTUAL updates are applied on CPU inside BuildPagingBuffer; the PTE branch emits no DMA record | `src/gpuva_g3_paging_windows.c:1794-1817,1865-1957` |
| Process quiescence | Updates and TLB flushes release the G3 lock while waiting for JobInFlight and LeaseToken to clear; bounded 3 s, job event plus 1 ms backstop | same file `:1663-1697,1812-1859` |
| Graph protection | Parent and leaf mutation refuse JobInFlight; backing grants, native graph generation and publication are coupled | `shared/src/apple_agx_gpuva_g3_graph.c:440,503-640`; `src/gpuva_g3_paging_windows.c:751-752` |
| Deferred paging work | Fill/transfer/monitored-fence operations encode copied records; monitored-fence address resolves to an IPA at build time | `src/gpuva_g3_paging_windows.c:1191-1357`, especially `:1263-1288` |
| Paging submission | System-context SubmitCommandVirtual validates records and copies them to AdmissionCpuQueueSubmit with the scheduler SubmissionFenceId | `src/gpuva_g3_windows.c:4183-4239` |
| Shared queue | CPU paging and render use the same scheduler. Dispatch requires no active/dispatched fence, no pending paging worker, and no dispatch block | `src/work_queue_windows.c:48-58,75-99,113-159` |
| Paging fence store | Worker executes records in order; monitored-fence record writes its FenceValue and issues a memory barrier | `src/paging_windows.c:344-397`; `src/gpuva_g3_paging_windows.c:1429-1476` |
| DMA completion | Terminal bookkeeping precedes synchronized interrupt notification; DMA completion/fault names node0/engine0 | `src/paging_windows.c:262-304,320-342,386-404` |

There are two distinct fences: the paging synchronization object's 64-bit
value returned to the UMD, and the scheduler packet's SubmissionFenceId. The
monitored-fence record carries the former; the interrupt names the latter.
They must not be equated with each other or with the low32 timing receipt.
The source proves both possible ordering delays. EXP2114 has no matched KMD
paging packet/receipt that apportions its 21.560 ms between them.

## Broker ownership is the limiting contract

The local named m1n1 build snapshot supplies these primary implementations:

- `m1n1_windows/src/hv_agx_gpuva_v5.c:300-307`: `publish_entry` checks every
  occupied slot belonging to the owner and returns BUSY if it has jobs,
  **before** storing the descriptor. This includes old-invalid/new-valid
  entries, not only unmaps or replacements.
- `:308-323`: publication synchronizes table stores, invalidates owned slots,
  advances generation only on success, and rolls back on failure.
- `:594-620,622-705`: parent and leaf updates both call that publication path.
- `:804-833`: FlushTlb independently refuses the same owner's running job.
- `src/hv_agx_retained_platform.c:107-123`: table-store ordering and ASID
  invalidation live in the broker backend. The KMD does not own a substitute
  direct UAT store/TLBI path.
- `shared/src/apple_agx_gpuva_g3_graph.c:929-953`: depth-two completion keeps
  the lease until both jobs end. Clearing KMD flags early would not clear the
  broker's job, and would misrepresent ownership.

The pinned submodule object `0d3a75b3beb78fbfdfda3b1bf8ca8dfbcfaa49a7` is not
available in the local object store. For host replays only, source/tests were
copied into this worktree from the existing non-git build snapshot
`.local/m1n1-build/base-0d3a75b3/m1n1`. The handoff hashes the inputs. Its
directory name is not verification against the pinned git tree. No firmware
source was changed, built, installed or launched. Integration must confirm
the deployed broker source identity before relying on this attribution.

The actual Asahi sources provide a useful distinction, not code to copy:
`.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/pgtable.rs:486-521`
stores page descriptors on the CPU; `mmu.rs:346-370` unmaps and invalidates a
range, and `:643-827` handles cached-mapping teardown/firmware flushing.
Asahi owns that protocol directly. Windows' implementation interposes the
v5 ownership broker and cannot assume Asahi's permissions from UMA alone.
The J313 DT includes t8103/t8103-jxxx (`arch/arm64/boot/dts/apple/t8103-j313.dts:12-20`).
No register, power, IRQ, DART or memory-map change is proposed. Mu/ACPI's saved
APPL0002/reserve contract is unchanged, not newly hardware-validated here;
the current Mu submodule/generated ACPI was not available in this worktree.

## Windows contract comparison

Pinned `.local/reference/wdk26100/d3dkmddi.h` defines CPU_VIRTUAL,
GPU_VIRTUAL and GPU_PHYSICAL modes at :2055-2060; the update flags at
:2081-2095; ExplicitPageTableInvalidation, PageTableUpdateRequireAddressSpaceIdle
and PageTableUpdateMode at :2122-2152; DXGK_VIDMMCAPS.PagingNode at :2315;
and the per-process table/range and monitored-fence inputs at
:4685-4709,4783-4787. CPU visibility and the addressing mode do not authorize
signalling a fence before the mapping becomes usable.

Pinned `d3dukmdt.h:1595-1610` defines MapGpuVirtualAddress's returned paging
fence; :1542-1586 distinguishes map, unmap, copy and protection operations.
They are not interchangeable additive updates. SDK `um/d3dumddi.h:3838-3850`
specifies NULL-event synchronous CPU waiting; :3859-3876 offers a GPU-context
wait. The latter cannot order this driver's earlier CPU native-ready work.

Official contract references already recorded by Q21b and R155:
[MapGpuVirtualAddress callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_mapgpuvirtualaddresscb),
[map structure](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-d3dddi_mapgpuvirtualaddress),
[GPU virtual addresses](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-address),
[BuildPagingBuffer](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_buildpagingbuffer).
This offline task inspected pinned headers and existing source-backed notes;
it did not perform a fresh online documentation check.

Changing PageTableUpdateMode to CPU_VIRTUAL would not remove either broker
quiescence or the independently scheduled fence record. Re-advertising
PageTableUpdateRequireAddressSpaceIdle does not establish a faster path;
the current source records the EXP985/986 ordering failure behind its clear
value (`lifecycle.c:859-864`). Early node-0 DMA completion would advance past
older render work. Returning BUSY to VidMm for UpdatePageTable is not a
replacement design either (`gpuva_g3_paging_windows.c:1550-1599`).

## Why no additional UMD cache fix

The complete EXP2110 HD perf interval is trace lines (31030,2065831], perf
serial17/Present60 through serial372/Present5811: **5,751 Present increments,
293,397 cache hits (51.0167/Present), zero misses, zero evictions**. All 372
perf serials are contiguous and each report's changed reason deltas sum to
its global event-counter increment. This is stronger than missing timer
messages. Raw log SHA-256:
`3d0c6c8d547419c1112f75d8685bd3f17f1842a826df9018645fa00030a4e68f`.

| Quantity | Startup through Present60 | Complete steady interval | Tail after Present5811 |
|---|---:|---:|---:|
| Present receipts/increments | 60 | 5751 | 50 |
| Reported cache misses | 758 | 0 | no final cache report |
| Long (>=10 ms) BO creations | 112 | 0 | 4 General-class |
| Long canonical map-paging waits | 66 | 0 | 3 |
| PagingFence calls in retained top-four buckets | 450 | none retained | 48 |

Five slow Encoder-class creations occur after Presents43,51,55,56,57
(lines22236,26463,28563,29250,29610). None is steady-state. The exact count
of **all** steady maps/paging waits is unknown: all 5,867 steady Execute
profiles are truncated to four buckets. Their omitted sums bound instrumented
paging time only loosely at 0..<2.364941 ms/Present, shared with every other
omitted site; this is not measured paging time or recoverable GPU idle.
Imports, aliases and submit residency are not counted by BO-cache misses.

Source chain: primary Mesa `agx_batch.c:89-126` allocates batch pools and
encoder; `src/asahi/lib/pool.c:16-42,59-76` allocates/grows slabs.
`mesa/winsys/agx_win32_indirect_stream_emit.h:226-230` lazily adds one CDM
encoder to a render batch. Windows `agx_win32_asahi_bo.c:317-370,452-457`
reuses whole mapped BOs with exact size/flags/class and refuses held entries.
`agx_d3d12_windows.cpp:513-556` defines complete delta reporting.
The bound is based on `render-admission/umd/include/umd_wait_profile.h:75-97`.

Q3PC's serial warm-ups (`windows/icd/agx_d3d12_probe.c:8773-8801`) can reuse
two encoders; a still-pending render+prelude and a compute successor need
three. This is a plausible first-concurrency capacity explanation, not the
measured EXP2114 allocation identity. The supplied receipt cannot distinguish
encoder from low-VA slab, mismatch from capacity, or eviction. A permanent
reserve or broader cache match is not justified by these logs.

## Regression, design and remaining gates

`tests/test_g4_paging_serialization.py` executes:

1. Real v5 broker publication: fresh leaf and parent, destructive unmap and
   protection change are refused while the owner's job runs; another owner
   can proceed. Job end with lease retained permits publication and TLB ack.
   An injected invalidation failure rolls back the new descriptor and does
   not advance generation. The fresh VA aliases already registered backing;
   it is not a fresh-allocation residency test. Sync failure and failed
   rollback quarantine are not modeled by this fixture.
2. Real AdmissionCpuQueueSubmit/AdmissionDispatchQueuedWork and shared
   scheduler: a monitored-fence packet queues behind active render and is
   dispatched only after it retires. OS worker enqueue is a counter stub;
   actual fence-memory writes and interrupt notification are source-inspected,
   not executed by this test. Completion is simulated through the scheduler.

Both pass with patterned automatic storage, ASan and UBSan. The handoff's
`desired-behavior-red.log` instead demands that a fresh leaf publish during
the running job and fails on the unmodified broker. That desired behavior
remains **RED**, not a completed RED-to-GREEN performance fix. A separate
isolated mutation removes the broker's guard and makes the committed
current-contract regression fail (`guard-sensitivity.log`). Production
sources remain unchanged.

A full immediate-publication candidate needs a negotiated default-off broker
additive-update capability plus KMD classification and independent paging
fence routing. The broker must itself enforce old-invalid to new-valid
entries and complete 16 KiB groups; destructive changes, eviction, backing
revocation, table reuse/root relocation and both depth-two jobs retain their
lifetime barriers. Native-ready/submit validation must remain valid.
Default-off and mixed-version behavior, preemption/reset, TLB/sync failure,
and separate node identities need real runtime coverage. An alternative
deferred-publication architecture has not been implemented or excluded.

`AGENTS.md:206-207` explicitly requires permission for G2 m1n1 implementation.
No such source change is silently included in this KMD task. The handoff
DESIGN.md gives the proposed expanded scope and test obligations. REPORT.md
records actual verification, commits and unmet requirements; no native
MSVC `/W4 /WX` or x86/x64/ARM64 DLL-link claim is made for this analysis-only
candidate. The general older `g3_vidmm_replay.py` also has pre-existing
extraction/shim drift; its compile failure is retained in the report rather
than being represented as a passing full-DDI replay.

Integration-only checkpoint after a complete owner fix: Q3PC must retain
path3/reason0 and exact pixels/compute/IA counts and show
**head_submit < next_submit < head_complete**, with no reset/removal. Keep
prelude execution gating intact. Then require the D3D12 suite 119 PASS/0 FAIL
plus known C21E skip, GravityMark HD/FHD 100k temporal0 versus EXP2110
5949/4803 with images and uncensored paging evidence, and exact-package
rollback to ordinary GPU-visible Code28 after collection. No hardware run
is justified by these test/analysis commits alone.
