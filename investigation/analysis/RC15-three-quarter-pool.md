# RC15: three-quarter native pool, offline candidate

Base c0b5399792b38da42f9144829029a09b89fd330c, codex/rc15.
Implements the fraction-switch option in the rc15 handoff TASK.md. No hardware,
guest, builder, network, package or other-worktree operation was performed.

## Evidence and causal decision

- EXP1927 BEFORE/ACTUAL, investigation/EXPERIMENTS.md:61991/:62020:
  HalfSize avoided reservation/floor failures but later MakeResident failed.
- EXP1940 ACTUAL, :63769: full 2688 MiB segment; 17 reservation failures,
  all floor refusals, zero new-slab or commit failures; peak commitment
  2363.5 MiB. HighMemory clear and LowMemory clear at the last refusal.
  Saved UMD reports ~739–745 MiB available at allocation refusal. rc1 REPORT
  corrects the exp1940 report: probe-end/pre-disable current counts differ;
  peaks and failure tuple agree. These are combined-run peaks, not a per-run
  memory budget or proof of the first 10k failure's entire RAM accounting.
- EXP1951 ACTUAL, :63924: HalfSize has only 38,715,392 bytes remaining for
  a 67,174,400-byte page-in. Peak pool occupancy is the whole 1344 MiB;
  reserve/floor/new-slab/commit failures are zero. VidMm reports unrecoverable
  page-in. This is placement capacity, not a failed physical allocation.
- EXP1952 ACTUAL, :63944: aperture spill moves the failure to incomplete
  native coverage. rc14 is analysis only; no shadow bridge exists.

The headroom gate is not a Windows allocation requirement: it is this KMD's
policy for its separately locked physical pages. Its placement is correct:
`src/render_pool_slab.c:24` promises min(Reserved, ReserveCap), and :78
actually grows backing at reservation time, checking MayGrow before each
new slab. :88 restores the attempted count on failure; :30 preserves earlier
promises during trim. A reservation covered by existing slabs never calls
MayGrow. Consequently the >3 GB logical reservation peak versus 1.3 GB
resident/committed peak does NOT justify deleting the growth gate. Held
physical slabs also include idle promised backing, not just committed slots.

`src/pool_windows.c:252` allocates physical slabs and :265 requests fully
required contiguous chunks. A NULL MDL is handled and the failed slab metadata
freed (:290). `src/allocation_windows.c:519` reserves backing at creation;
:525 destroys the attempted object and returns STATUS_NO_MEMORY. This is the
existing runtime E_OUTOFMEMORY path, not a reason to return success with
unbacked placements. Real page-table publication is a later contract;
UpdatePageTable failure can still poison a process and is not claimed to be
universally recoverable. Preserve creation-time backing guarantees.

## Windows thresholds and limits of offline evidence

HighMemoryCondition means available/free physical memory exceeds an
OS-selected high threshold, permitting aggressive allocation. LowMemoryCondition
means it is below a separate low threshold and unused memory should be returned.
Neither signaled is a legitimate interval, not a contradictory state.
`src/pool_windows.c:393` requires high set and low absent/clear. A missing high
event refuses growth. The gate explains EXP1940 even though ~740 MiB remained.

These semantics are recorded with the Microsoft Standard Event Objects source
in investigation/analysis/M2-f7c-dwm-headroom.md:63. Pinned SDK
.local/reference/winsdk26100/km/wdm.h:23200 declares KeReadStateEvent;
:29769/:29772/:29832 declare the physical-allocation flags/API. No undocumented
available-page symbol or SystemInformation class is introduced. Online documents
were not fetched under this offline task; the prior source analysis is secondary
evidence for prose semantics, not a fresh verification of Microsoft's website.

The exact high/low thresholds for this 6 GB guest are NOT established by the
provided capture or public ABI. ~740 MiB is an observed high-boundary vicinity,
not a guaranteed threshold or RAM-percentage formula. Low remained clear there;
its numeric threshold and hysteresis are unknown. Historical 32/64 MiB folklore
is not used to select a floor. RAM ranges describe total managed memory, not
available pages (`pool_windows.c:664`). No live kernel threshold was queried.

## Change, invariant and implementation sequence

1. Execute a RED regression against the original sizing code: requested 3/4
   must be 2016 MiB rather than 2688; strict reader/start wiring absent.
2. Add `Wom1PoolFraction` device REG_DWORD=3, read once at PASSIVE pool start.
   Other values, failed/malformed/missing reads preserve the default. HalfSize
   takes precedence if accidentally left enabled. Apply 3/4 to the existing
   RAM/window/broker-limited bound, with divide-before-multiply arithmetic,
   retaining the minimum and zero-invalid-bound handling.
3. Derive segment bytes AND ReserveCap from the same result. Test real slab
   promise/placement/reuse, allocation failure, both switches, small windows,
   broker limits, missing RAM and invalid registry values. Run headroom,
   slab, placement, KMD-header tests plus the prescribed complete baseline.
4. Review and commit the behavior/test change and this analysis separately;
   handoff report contains exact hashes, verification and next hardware gate.

For this guest: full 2688 MiB -> three-quarter 2016 MiB (2,113,929,216 bytes,
32,256 granules, 1008 slabs), versus HalfSize 1344 MiB. The normal 64 MiB size step
makes the fraction a multiple of 16 MiB, still whole 2 MiB slabs/64 KiB granules.
No hardware/DDI alignment requires preserving 64 MiB rounding after the fraction.

The policy leaves 672 MiB more RAM outside the maximum native backing promise
than full size, while adding 672 MiB residency over HalfSize. It is 347.5 MiB below
EXP1940's measured peak commitment. This is a capacity/headroom compromise
selected from the task's allowed options, not an absolute free-RAM guarantee.
The validated High&&!Low gate and immediate safe reclaim remain unchanged.
There is deliberately no new lower absolute floor and no relaxation of a
protected source invariant. Other consumers can still force a legitimate
floor refusal before 2016 MiB is backed. The old minimum 256 MiB remains intact.

Sources after the fix: pool_windows.c:694 sizing, :747 strict reader, :805
start consumption, :850 ReserveCap, :864 published PoolBytes. Tests:
tests/test_g4_pool_residency_limit.py; existing headroom and partial-growth
rollback tests remain active. Default and HalfSize behavior are preserved.

## Ownership and primary implementation boundary

VidMm owns placement and eviction. KMD owns nonpaged slabs, reservations,
native mappings and safe reclaim. Asahi Linux reference
.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/gem.rs:5/:56
uses GEM shmem objects and mapping lifetimes, not this Windows backing promise;
its mmu.rs:1200/:1220 enforces native DMA mapping alignment. No external code
was copied. m1n1 owns pool-window/stage-2 mapping; Mu owns boot carveouts/ACPI.
Initialization, interrupts, DMA protocol, power and recovery are unchanged.

Gitlinks are m1n1 21c3648d and Mu 361a2134. Attempts to read the exact pool-window
and MemoryInitPeiLib source paths from their local Git databases failed (paths
not present in those objects); neither submodule was initialized or fetched.
The historical M2-f7c note describes their ownership but is not substituted for
a current implementation verification. EXP1940/1951/1952 reference m1n1
0d3a75b3. No live ADT/register/ACPI/launch contract was checked. This narrowly
scoped KMD policy does not alter or claim to validate those absent sources.

## Optional paging investigation

`shared/src/apple_agx_physical_topology.c:35` requests 4096 DMA bytes;
`render-admission/src/lifecycle.c:904` and `memory_windows.c:204` request 4096
private bytes. Marker is 16 bytes, record is 128 bytes on the 64-bit ABI
(render_paging.h:27/:34); thus private storage holds 32 records, DMA holds 256.
`gpuva_g3_paging_windows.c:1061` takes the smaller count, then caps at 64 records
(the scheduler's fixed arrays; render_admission.h:75/:373/:743). Noncoalesced
paging splits at 4 KiB; local contiguous runs can coalesce up to 4 MiB (:1135 and
render_paging.h:18). Therefore not every record is restricted to4KiB.

No paging change is bundled. Enlarging only the public DMA buffer would not
help the 32-private-record bottleneck. A separate cheap follow-up could request
8192 private bytes consistently in both queries and the shared contract,
retaining the 64-record cap, with a replay proving twofold noncoalesced coverage
per buffer and unchanged fence/continuation behavior. The ~730k retry count
alone does not establish how much wall time that saves; no speedup is claimed.

## Smallest hardware checkpoint (integration owner only)

WHY THIS HYPOTHESIS:
1. HalfSize exhausts native placement without a KMD allocation failure.
2. Full size exhausts the protected physical headroom before its cap.
3. Three quarters increases resident capacity 50% while capping backing below
   that full-size observed failure boundary; aperture coverage remains strict.

Build pinned WDK on base c0b53997 plus this fix (exact commits in REPORT.md).
Before use preregister package/manifest/SYS/UMD hashes and known-good recovery.
Keep firmware, m1n1, caps, signer and the EXP1951 runtime settings fixed.
Set Wom1GpuFaultInfoRead=1 and Wom1PoolFraction=3; remove/zero
Wom1PoolHalfSize and Wom1SysmemSpill. Restart via the owner's normal controlled
profile to consume the startup switch. Require pool receipt SegmentBytes=
2113929216 before interpreting a run.

Run GravityMark D3D12 2k, then 10k, windowed 1280x720, 240 s timeout, with DxgKrnl
ETW. The smallest gate is 2k first-frame page-in. Do not escalate after device
loss, native coverage refusal or DWM failure. Capture pre/post available RAM,
commit, LOCAL budget/usage, pool v8 counters/peaks, DDI receipts, UMD logs,
frame/image evidence, DWM events and ETW. Require prior process exit and memory
return before the second run. Expected: no unrecoverable page-in, no load-time
floor refusals, rendered frames, and DWM alive. Those are hypotheses, not an
offline result. Collect evidence before exact-package GPU-visible cleanup;
use known-good recovery, emergency GPU-hidden only if ordinary recovery fails.

WHAT IS STILL UNKNOWN: current 2k/10k active-set fit at 2016 MiB; starting/peak
other RAM consumers; actual event thresholds; native WDK compilation; content,
performance and desktop survival. Floor failure would mean insufficient
physical headroom in this run; zero pool failures plus page-in failure would
mean the larger capacity is still inadequate. Neither result licenses a
blind gate removal. No hardware experiment or ledger edit was made here.
