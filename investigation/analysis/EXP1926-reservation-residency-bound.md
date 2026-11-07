# EXP1926: separate live allocations from the backed resident bound

Offline candidate, 2026-10-24. No hardware validation is claimed. Full ACTUAL
draft and run instructions are in `.local/handoff/codex/exp1926/REPORT.md` in
the shared handoff directory. The integration owner records the experiment
and change ledgers after review/merge.

## Evidence and causal decision

EXP1926 used package 30.0.1926.0 from `42f4e891`, including the checked resource
transition allocation from EXP1925. Its BEFORE entry is
`investigation/EXPERIMENTS.md:61903`. Saved evidence is under
`.local/experiments/EXP1926-transition-oom/evidence/EXP1926`:

- `gm1926a-err.txt:1`: 10k first OOM at 49.091 s, creating a 4096x4096 depth
  texture. `gm1926a-umd.log:4252`: failed 0x08100000-byte BO, underlying
  `0x8007000e`. `gm1926b-err.txt:1`: 2k first OOM at 22.779 s, heap creation;
  `gm1926b-umd.log:3878` begins the six recorded BO refusals.
- Both done files report timeout/kill at 106 s, WER 0. Start/end retain DWM
  PID 1216 and explorer PID 4508. There is no device-removal timestamp,
  score, crash dump, GPU reset/fault/recovery receipt, or evidence of a
  completed benchmark. The narrow no-AV/clean-refusal outcome is observed;
  it does not prove every transition failure branch was exercised.
- Probe-end and pre-disable `Wom1M2PoolDemand` v7 agree: segment 2688 MiB,
  peak reserved 2547.875 MiB, peak committed 2548.4375 MiB, seven reservation
  failures, zero slab/commit/release failures. Final held 214 MiB includes
  committed/reserved 195.5 MiB. v7 has no event snapshot or FloorRefusals.
- `gm1926a-umd.log:4205` reports successful trim of 0x08010000 bytes for a
  0x003a0000-byte request; `:4206` reports the retried residency request as
  E_PENDING. Thus the UMD budget retry is exercised before the creation
  refusal. This does not prove a complete eviction/restore cycle or pixel
  correctness under a smaller segment.

`render_pool_slab.c:22` promises `min(Reserved, ReserveCap)` slots, `:78`
backs creation before success, and `:28` retains that promise through empty
slab trimming. `allocation_windows.c:522` reserves until destruction at
`:667`. Eviction does not destroy allocations. The seven failures with no
NewSlabFailures identify the MayGrow floor branch in `render_pool_slab.c:79`;
the final counters are not a simultaneous failure-time memory inventory.

The capacity formula is an upper bound, not a promise that all that capacity
can be locked while the application also holds CPU staging and ordinary RAM.
CreateAllocation can hit the floor before live reservations reach the bound
where further allocation growth stops acquiring backing. Merely calling Evict
cannot remove those lifetime promises. Raising the cap or deleting the floor
does not solve that ownership contract.

## Windows and AGX contract

Primary Microsoft headers inspected locally, without network access:

- WDK 26100 `d3dkmddi.h:2605-2617` and `:2725`: segment Size and CommitLimit;
  the independent CommitLimit applies to aperture segments. Do not advertise
  a large memory segment while capping its physical promise at a smaller size.
- SDK `shared/d3dukmdt.h:1475-1489`: MakeResident uses a paging queue/fence
  and returns NumBytesToTrim on an over-budget failure. The existing UMD
  `umd_gpuva_windows.c:334-361` trims eligible idle references and retries;
  `:302-306` uses EvictOnlyIfNecessary. Budget is a residency target, not
  allocation lifetime, guaranteed available RAM, or physical backing.
- SDK `shared/dxgi1_4.h:1303`: Budget/CurrentUsage and reservation fields.
  No saved per-process query proves what budget GravityMark was given at OOM.

The previously reviewed Microsoft documentation is linked in
`M2-f5-backing-guarantee.md` and `M2-f7c-dwm-headroom.md`: CreateAllocation is
the clean OOM boundary, ordinary BuildPagingBuffer cannot report arbitrary
allocation failure, memory segments are fixed during initialization, and
HighMemoryCondition is a signal for aggressive allocation. Those archived
interpretations were consulted; this task did not refresh the web pages.
The HighMemory gate is a conservative driver policy, not a blanket WDDM
requirement. For this separately locked pool, prior DWM starvation and the
protected T8 contract rule out weakening it without new evidence.

Linux Asahi `drivers/gpu/drm/asahi/gem.rs:5-7,56-57` uses GEM shmem objects;
Mesa `src/asahi/lib/agx_bo.c:355-385` reclaims cached BOs on allocation failure.
Neither says this Windows driver's lifetime nonpaged reservations become
pageable merely because VidMm evicts a BO. No external code was copied.

KMD owns physical slabs, reservation accounting, pool-window mappings and the
advertised segment. VidMm owns resident placement and system-store eviction;
UMD owns residency references and safe trimming. The existing system transfer
path remains in `gpuva_g3_paging_windows.c`. m1n1 owns broker/stage-2 mapping
safety and Mu owns firmware-reserved DDR (segment 2), as recorded in M2-f7c.
Their documented pinned source paths could not be read from the submodule
objects in this offline checkout; no new firmware conclusion is inferred.
No ADT/register/IRQ, initialization sequence, DMA protocol, power or recovery
change is required for this software boundary.

## Candidate: fixed smaller resident segment, protected floor retained

At StartDevice, device-key `Wom1PoolHalfSize` of type REG_DWORD and value 1
halves the established RAM/window/broker-bounded segment, rounding down to
the existing 64 MiB step and retaining the 256 MiB minimum. A pre-existing
invalid/zero capacity stays zero. No other value enables the policy.
`AdmissionPoolSegmentBytes` and `AdmissionPoolHalfSizeEnabled` in
`pool_windows.c` run before metadata allocation and segment publication.

The same resulting `bytes/count` sizes metadata, `ReserveCap`, PoolBytes,
CPU window and the advertised topology. On the saved J313 configuration,
2688 MiB becomes 1344 MiB. This is an explicit experimental reduction,
not a new default, benchmark-specific allocation filter, or numerical claim
that 1344 MiB is the optimal working set. It tests eviction while keeping
enough margin to make the reservation cap reachable before physical pressure.
The headroom gate still vetoes any new reservation slab when HighMemory is
absent. Paging promises, overlap behavior and broker release safety remain.

Once live allocations exceed that smaller bound, creation no longer needs
additional reservation backing solely for their excess. VidMm must schedule
their resident sets within the actual smaller segment and use the existing
eviction/restore path. A single simultaneously required GPU set larger than
the segment can still fail MakeResident. This candidate cannot guarantee
arbitrary overcommit, remove CPU-staging cost, or promise DWM survival under
all other consumers of RAM.

## Evidence for the next decision

Pool receipt v8 keeps the 304-byte v7 prefix and appends FloorRefusals plus
the last failed request size, post-rollback reserved/held/committed bytes,
interrupt-time milliseconds and post-failure high/low event samples. Total
size is 360 bytes. Event values are 0 clear, 1 set, 2 unavailable. Samples
are not the atomic inputs of the earlier policy decision. Cumulative floor
counts cannot attribute the last failure when slab failures also occurred.
Success does not erase the previous failed request.

`decode_wom1_gpu_fault.py Wom1M2PoolDemand.bin` decodes v7/v8; v7's missing
fields remain unknown. Automatic JSON/REG fault collections keep their old
selection behavior so unsupported historical pool versions cannot break them.
The existing work-item writer does not explicitly flush reservation-only
failures (`receipts.c:2163-2166`); collect while SSH is alive before cleanup.
No post-bugcheck durability guarantee is added.

## Verification and remaining uncertainty

Executable tests cover sizing bounds/default, actual registry reading,
overcommitted reservations and a full backed resident set under pressure,
failure capture/preservation, the native C receipt layout, old collectors and
malformed schemas. The headroom fixture now advances the paging epoch after
a simulated BUSY unmap clears, matching the existing retry contract; no
production reclaim rule changed. The real broker-capacity test still requires
the missing m1n1 header in this fresh worktree.

The next integration checkpoint is the identical 2k/1280x720 D3D12 short
profile with `Wom1GpuFaultInfoRead=1 Wom1PoolHalfSize=1`. Verify 1344 MiB in
the receipt first. Observe creation past the old OOM boundary, trim/residency
progress, zero slab/commit failures and DWM/SSH survival. MakeResident
exhaustion instead identifies a working-set/trim boundary. Reject any new
AV, DWM c00001ad, reset, MMU fault or paging poison. Stop after this one
discriminator; do not sweep sizes without evidence. Ordinary visible exact-
package recovery remains integration-owned.

The branch inherits MEM1 cache reclamation, which was not in EXP1926's
package. It has independent offline RED/GREEN but needs hardware validation;
benchmark progress in a combined package must not be attributed solely to
the pool switch. Native WDK compilation, residency convergence, image
correctness, sustained benchmark completion and memory headroom remain unknown.
