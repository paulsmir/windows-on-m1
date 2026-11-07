# RC17: reduce residency-trim overshoot

Offline candidate on codex/rc17, base 6c27b19d. No hardware or performance
validation. Read README/TASK, RC16 report/analysis, EXP1953/1954 ACTUAL and
saved EXP1954 UMD/pool/DDI receipts. Evidence root is the shared repository's
`.local/experiments/EXP1954-paging-private/evidence`; handoff rc17 contains
analyze.py, per-present CSVs, decoded receipts and test logs.

## Evidence and causal decision

Payloads are hexadecimal; QPC is 24 MHz (DDI receipt). Present intervals are
callback-to-callback, not displayed FPS. These late intervals follow scene
startup; rows identify exact UMD source evidence, inclusive line ranges.

| Arm/frame | gm1954 UMD lines | Seconds | Trim calls / refs | Dropped MiB / requested MiB | Trimmed MakeResident / handles / E_PENDING |
|---|---|---:|---|---|---|
| a/43 fast | a:17033–17306 | 0.560066 | 4 / 148 | 388.875 / 132.450 | 4 / 90 / 4 |
| a/45 slow | a:17583–17844 | 24.998506 | 7 / 199 | 746.438 / 563.038 | 6 / 161 / 6 |
| b/42 fast | b:16922–17189 | 0.953620 | 0 / 0 | 0 / 0 | 0 / 0 / 0 |
| b/44 slow | b:17466–17728 | 15.899239 | 7 / 148 | 652.375 / 520.975 | 7 / 128 / 7 |

The counts include only internally logged *trimmed* residency requests, not
all MakeResident calls. E_PENDING counts identify requests requiring paging
synchronization, not an independent count or timing of waits. Slow a/45 replay
is 24.730616 s and b/44 is 15.879661 s. Their checked CPU upload/download
volumes are 1.141/1.563 and 1.172/2.875 MiB, with zero texture-copy calls.
Fast a/43 has 33.762/1.625 MiB and one texture-copy call; fast b/42 has
3.902/3.000 MiB and two. CPU copy volume does not explain the bimodality.

Last ten present intervals: a drops 5376.313 MiB for 2930.775 MiB requested
(48 trims, 45 pending trimmed requests); b drops 4547.500 for 2644.713 MiB
(48/48). These are repeated reference bytes, NOT distinct working-set sizes.
Requests generally need one trim round; the examples include at most two,
so the round-16 exhaust-all fallback is not the immediate cause here.

Pool receipt: 2113929216-byte segment, 2113732608 peak committed bytes,
2827 moves, no reserve/commit/floor failures. DDI slot39 records 358505
BuildPagingBuffer retries (whole boot, both apps and desktop). RC16 halved
roughly 730k retries without improving scene rate significantly. Do not add
operation slots to the aggregate or infer a per-frame transfer volume from it.

**Decision:** excessive reference release is source-proven and directly
measurable. Correct the terminal LRU overshoot first. Its effect on physical
paging and frame rate is a hypothesis, not a demonstrated root cause of all
15–25-second stalls. Neither “working set exceeds segment” nor “the exact
allocations are evicted then reused next frame” can be established from these
aggregate receipts. A full segment is insufficient to prove either claim.

## Sources, ownership and Windows contract

Paths below are relative to `drivers/apple-agx/` except pinned references.

- `render-admission/umd/include/umd_residency_trim.h:37`: only eligible idle
  allocations are selectable; CPU maps/holds/transitions remain excluded.
  Old selection at :68 simply consumed the next LRU slot, even when its size
  greatly exceeded the remaining deficit. Updated :68–85 bounds that terminal
  overshoot by the smallest single remaining allocation that covers it.
- `render-admission/umd/src/umd_gpuva_windows.c:340`: selection uses slot bytes
  and LastUseFence. :1955 updates the last-use fence after submission. No
  evidence was found for a missing GPU-submit LRU update.
- Same file :343–355 reserves chosen slots under the lock; :362 uses
  EvictOnlyIfNecessary; :371 clears Resident only on successful eviction.
  :398–446 retries OOM against NumBytesToTrim, retains bounded exhaustion and
  the final CantTrimFurther request. This is demand-driven trimming, not an
  unconditional per-frame eviction. :736 and :782–794 restore residency and
  wait paging before access. rc6/rc7/rc11 ownership/error paths are unchanged.
- `.local/reference/wdk26100/d3dukmdt.h:1466–1488`: CantTrimFurther permits
  current-budget overcommit, still subject to maximum budget; MustSucceed
  can put the device in error; NumBytesToTrim specifies retry shortfall and
  PagingFenceValue synchronizes access. :1497 defines EvictOnlyIfNecessary.
  `d3dumddi.h:4371` supplies the actual callback ABI.
- Microsoft's pinned D3D12Residency sample,
  `.local/reference/dxgs-68907e31-more/Samples/Desktop/D3D12Residency/src/d3dx12Residency.h:587–606`,
  trims by budget and completed sync-point eligibility; :1339 invokes this
  against usage plus bytes to make resident. It supports budget-driven
  management, not withholding all references indefinitely. This is a sample,
  not evidence of VidMm's exact internal victim policy.
- Asahi `.local/reference/mesa/src/gallium/drivers/asahi/agx_batch.c:234–255`
  releases batch BO references at cleanup. Linux BO lifetime is distinct from
  WDDM persistent residency references; copying that release policy would
  repeat the EXP978 per-submit paging failure. No external code was copied.

UMD owns residency references, victim selection, holds and paging waits;
VidMm owns process budget, physical placement and actual eviction; KMD owns
native backing and paging transfers. m1n1 owns virtualization/interrupt routing
and Mu owns firmware/ACPI. This change does not alter those layers, DMA,
interrupts, power, firmware, caps or recovery. Their source submodules are
unpopulated here; no new hardware assumption depends on them. Live state
inspection is prohibited for this task. Saved EXP1954 contract remains the
2016 MiB segment, m1n1 0d3a75b3 and unchanged firmware/signing.

Dropping references with EvictOnlyIfNecessary already lets VidMm decide which
physical pages must leave. Merely waiting on E_OUTOFMEMORY has no accepted
paging fence to wait for; returning it abandons a request that releasing idle
references could satisfy. Claiming CantTrimFurther before trying eligible
slots discards the current-budget cooperation. Keep the rc7 fallback.

Priorities can guide eviction but cannot increase capacity. All ordinary
requests currently use NORMAL; the trace does not identify which allocations
should outrank others. Arbitrarily elevating heaps could retain cold data
and starve desktop work. Fence hysteresis likewise risks withholding the only
available victims; fences represent many internal submits, not scene frames.
Neither is needed for this narrow, falsifiable correction.

## Implementation and regression plan

Keep the LRU prefix while it is smaller than the requested shortfall. When
its next allocation would overshoot, scan remaining eligible slots and choose
the smallest one that still covers the remainder. Ties preserve LRU and slot
order. No smaller alternative means unchanged behavior. Zero-byte requests
still release one allocation (now the smallest). Full exhaustion remains
available; eligibility, locks, retry count and errors are unchanged. One
extra linear scan happens only at the terminal choice, under the existing
selection lock; no additional allocation or callback is introduced.

Regression executes production trim_residency and AdmissionUmdTrimChoose
with a fake callback boundary under ASan/UBSan. For a 12 MiB deficit, old LRU
chooses 4+128 MiB; new choice is 4+16 MiB. An 8 MiB held slot and a 1 MiB
CPU-mapped slot remain untouched, equal-size alternatives use LRU, and the
large heap remains selectable when needed. Existing tests cover zero-byte
progress, every exclusion, exhaustion, eviction races, pending fences,
overbudget arbitration, persistent residency and preserved errors.

RED observed the 20 MiB assertion fail against old code; GREEN passes all
10 targeted tests. Full baseline and exact commits are in REPORT.md.

## Integration checkpoint and unknowns

WHY THIS HYPOTHESIS:
1. Late traces drop 1.72–1.83 times the requested reference bytes, exposing
   more allocations to eviction than budget cooperation needs.
2. Selection ignores size at its terminal LRU slot; the regression reproduces
   132 MiB released when 20 MiB suffices, without weakening ownership.
3. Paging retry reduction alone was neutral, and slow late frames contain
   very little CPU copy traffic; reducing avoidable residency churn is the
   next bounded behavior experiment.

Candidate: base 6c27b19d plus RC17 commits, no other policy changes. Integration
owner builds with pinned WDK and preregisters exact package/manifest hashes,
with known-good recovery preserved. Wom1GpuFaultInfoRead=1 and
Wom1PoolFraction=3; HalfSize and SysmemSpill absent/0. Keep prior runtime
settings, including GPU_WAIT=1, trim enabled, CantTrimFurther default.

Run 2k then 10k, 1280x720 windowed, 240 seconds per arm, process fully exited
between arms. Confirm segment 2113929216 bytes. Compare post-Ready scene
presents/min and interval distribution to EXP1954, recording exact Ready and
close QPC. Also compare dropped/requested bytes per frame. Improvement in
trim bytes without scene throughput improvement rejects overshoot as the
principal performance cause; do not call it a speedup. A full allocation
working-set/page-in explanation needs per-allocation VidMm ETW (bytes,
operation, timing and allocation identity), not more aggregate counters.

Stop on removal, content corruption, native-coverage refusal or DWM failure.
Collect evidence before the established exact-package GPU-visible recovery.
No run is performed by RC17.

WHAT IS STILL UNKNOWN: available smaller victims in the captured real frames;
actual physical bytes paged in/out per frame; unique per-frame working set;
next-frame victim reuse; performance effect of retaining large older heaps
instead of smaller newer victims; WDK compile and hardware correctness.
