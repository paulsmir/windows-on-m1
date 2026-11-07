# EXP1931: private CPU maps must not pin idle canonical GPU backing

The delivered change fixes the UMD's residency eligibility policy for T13
private staging. It does **not** establish that GravityMark fits, repair the
full-size pool's creation-time reservation refusal, or validate the half-pool
profile. Those distinctions are required when selecting the next hardware run.

## Saved evidence

Evidence root `E` is `.local/experiments/EXP1931-os-mem` in the shared
repository local directory; `R` is `E/evidence/EXP1931`. This analysis reads
saved files only. BEFORE is `investigation/EXPERIMENTS.md:62415`.

- `R/gm1931a-umd.log:4042` and `R/gm1931b-umd.log:3868`: first failure is
  AllocateCb, HRESULT `0x8007000e`, General GPU-only flags `0x2c`, 128 MiB.
  Four complete v2 snapshots per run contain valid OS and process queries.
- Adjacent first system samples at `:4046` / `:3872` report respectively
  768.387 / 808.508 MiB available physical memory and 3921.008 / 4000.348
  MiB available commit. Total physical/commit limit is 5988.379 / 12132.379
  MiB. This is not exhausted commit space or a failed private VirtualAlloc.
- First storage samples at `:4044` / `:3870` contain 588.938 MiB private
  staging, 570.750 MiB mapped across 86 slots; zero merge baselines. Retry
  leaves 566.063 MiB private, 552.438 MiB mapped across 58 slots. These are
  logical spans; they are not a measurement of resident or safely idle bytes.
- `R/kmd-receipts-probe-end.txt:160`, identical pre-disable receipt:
  Wom1M2PoolDemand v8, segment 2688 MiB, peak reserved 2456.375 MiB,
  peak committed 2463.938 MiB, 28 ReserveFailures = 28 FloorRefusals,
  zero NewSlabFailures/CommitFailures. Last refusal requests 7.25 MiB at
  uptime 202372 ms, reserved/committed 2456.375 MiB, held 2462 MiB,
  HighMemory clear / LowMemory clear. This is the last failure, not an
  atomic sample of the first UMD refusal.
- `R/gm1931a-done.txt` (UTF-16) records DWM/udwm.dll `0xc00001ad`, RVA
  `0xf4a44`. `R/umd.log:13737,13748,14161,14169` shows DWM PID 1216
  refused 512 KiB shader, 256 KiB encoder and 64 KiB General allocations,
  with SetError(E_OUTOFMEMORY). `state-start.txt:2` / `state-end.txt:2`
  shows DWM 1216 -> 8516. The full pool is not DWM-stable in this run.
- `R/kmd-receipts-probe-end.txt:117`: omitted-leaf sequence 22 belongs to
  DWM PID 1216, segment 0, allocation VA 0x1050000, 16384000 bytes,
  953 incomplete native groups. No Wom1GpuFault/Reset/FirmwareRecovery is
  recorded. This historical omission is not proof of a GPU fault or the
  cause of DWM's exception.

Reference EXP1927/28 used the **existing** 1344 MiB half pool: no reservation
or floor refusal, but terminal MakeResident after idle trimming. EXP1928
`gm1928b-umd.log:15436` requests 52 handles and still asks to trim
0x2297999c bytes after 32 slots / 0x24460000 bytes were released. This does
not identify the active set, but gives a specific residency boundary at which
the private-map exclusion matters. Both reference runs had DWM/GPU failures;
neither is a validated operating profile.

## Windows and AGX contract

The owning layer is the UMD's persistent residency list. SDK 26100
`shared/d3dukmdt.h:1475` defines MakeResident's allocation count, paging fence
and NumBytesToTrim; `:1494` defines eviction flags. KMD's pool keeps
`min(lifetime reservations, segment capacity)` backed
(`render_pool_slab.c:22,78`); dropping a UMD residency reference alone cannot
reduce that creation-time promise. The existing smaller segment is therefore
needed to test this change against the known residency failure, rather than
rerunning the unchanged full-pool CreateAllocation refusal.

T13 returns `PrivateStaging` directly without LockCb
(`umd_win32_screen.c:1193`), then publishes it as LockedBase/Mapped at
`:1227`. Its storage was allocated by VirtualAlloc at `:809`; `HeapStaging`
is set only for the private, non-borrowed T13 slot at `:863`.
The pointer therefore does not name canonical GPU pages. VidMm can evict the
canonical allocation without invalidating that pointer or discarding either
the canonical contents or the private CPU stores.

The old `umd_residency_trim.h` excluded all Mapped slots. The new view field
`PrivateCpuMap` is derived under ScreenBufferLock only for
`HeapStaging && PrivateStaging && !StagingAllocation`
(`umd_gpuva_windows.c:287`). Eligibility permits this mapped case, while
SystemDirect, presentation, shared/borrowed/KMT owners, Transition, CopyHeld,
SubmissionHolds and SourceHolds retain their protections.

Trim reserves Transition under the selection lock before EvictCb, records
Resident=false only on success, then releases Transition
(`umd_gpuva_windows.c:295`). CPU stores still address private memory.
No pointer, byte, write-watch state, merge baseline, handle or GPU VA changes.
Mapped private slots remain ineligible while GPU work or a copy holds them.

Before subsequent GPU use, `make_resident` validates the slot, takes CopyHeld
and SubmissionHolds, restores the missing reference and returns its paging
fence (`umd_gpuva_windows.c:600`). The winsys waits that fence before submit
(`mesa/winsys/agx_win32_gpuva.c:192,209`). `transfer_held` synchronizes only
held slots (`umd_gpuva_windows.c:1338`); T13 write-watch upload and readback
merge remain in `heap_staging_transfer` (`:1023`). Completion downloads
GPU writes before releasing the submission's protections. Already-unmapped
slots had the same eviction/restore path before this change.

Asahi primary sources inspected: Mesa `src/asahi/lib/agx_bo.c:354` for
allocation/cache pressure recovery, Linux `drivers/gpu/drm/asahi/gem.rs:1,57,84`
for GEM-backed objects/maps. These native mappings differ from T13's private
Windows CPU copy; their lifetime does not justify pinning the separate
canonical allocation here. No external code was copied.

KMD keeps native-leaf placement, admission, backing and paging; VidMm chooses
residency and preserves evicted allocation contents. Existing m1n1/Mu owns
hardware initialization/exposure, broker mappings, interrupts and power;
integration owns exact-package recovery. Their source submodules are
uninitialized in this checkout (gitlinks 21c3648d / 361a2134). No fresh
firmware behavior conclusion is needed or made. The saved hardware manifest
uses m1n1 0d3a75b3. No firmware, KMD, capacity, capability or signer changes.

## Regression and decision

`test_umd_private_staging_residency.py` executes the production trim,
residency retry and submission bodies under ASan/UBSan. It failed before
the change because EvictCb was never called for the private CPU map.
It now covers successful and refused eviction, all remaining exclusions,
submission/trim interleaving, pointer/data persistence, re-admission with
the paging fence, and a refused request succeeding after private-map trim.
Existing direct-map, persistent residency, staging coherence and pool-headroom
tests retain the broader contracts. Host substitutes cannot prove VidMm
paging contents or real GPU output.

WHY THIS HYPOTHESIS:

1. EXP1931 confirms physical headroom policy refusals, not exhausted commit;
   the separate private maps account for hundreds of MiB of logical backing.
2. EXP1927/28 demonstrate that bounded pool backing moves creation past this
   boundary and exposes residency pressure. Source now identifies a
   deterministic over-pinning rule at that next boundary.
3. T13 private memory and existing protected restore/copy paths allow release
   of idle canonical residency without revoking pointers or weakening DWM's
   headroom gate. The regression proves this missing eligibility behavior.

Do not weaken HighMemory, release mapped CPU storage, drop lifetime backing
without reducing the advertised segment, or tune a new size to the trace.
No new measurement-only candidate is delivered.

## Smallest hardware checkpoint, integration only

Rebuild/package the changed UMD for ARM64/x64/x86; native compilation remains
required. Use the existing full-owner firmware/recovery contract with
Wom1GpuFaultInfoRead=1 and **Wom1PoolHalfSize=1**, T13 enabled
(`APPLE_AGX_D3D12_HEAP_STAGING` absent or 1), same 1280x720 ARM64 D3D12 2K
workload. Confirm SegmentBytes=1409286144 before interpreting results.
This is the changed trim rule against the already-tested half profile,
not a new segment-size search or a promotion of that profile.

Use existing measure-trim, measure-make-resident-trimmed, reject-make-resident,
pool and GPU receipts. Expected: creation progresses, idle private-map
canonical references can be released, residency retries progress beyond the
old failure, CPU pointers and rendering remain correct. Passing the boundary
alone is insufficient: complete the benchmark and require no DWM restart,
c00001ad, MMU fault, GPU reset, AV, hang or paging failure. If it still cannot
fit, preserve the terminal request/trim amounts and stop this hypothesis.
The fixed policy has no special new receipt, so aggregate trim counts alone
do not identify which private maps were evicted. Existing slot/VA trace,
where present, supplies identity. Collect before exact GPU-visible cleanup.

WHAT IS STILL UNKNOWN: fraction of mapped staging actually idle at each
request; whether the remaining indivisible active GPU set fits 1344 MiB;
real eviction/restore correctness and cost; whether DWM's known aperture
fault path reappears under pressure; native builds, sustained stability and
image correctness. The change does not reduce private process commit, and
full-pool CreateAllocation failures remain possible by construction.
