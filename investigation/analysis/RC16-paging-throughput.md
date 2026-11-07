# RC16: GravityMark paging throughput (offline)

Base 74345690, branch codex/rc16. Saved EXP1953 evidence only; no hardware,
network, guest, builder, package, or other-worktree access. This is an
optimization candidate, not a measured speedup. The handoff REPORT.md has
commit hashes, verification and the complete per-present CSVs.

## Measurement and ranking

Evidence root: `.local/experiments/EXP1953-pool-fraction/evidence` in the shared
repository. UMD QPC frequency is 24 MHz (also in the decoded DDI receipt).
Receipt numeric payloads are hexadecimal. Execute replay/submit and owner-time
fields are already microseconds, NOT QPC ticks. Per-present bins end at each
`measure-d3d12-present`; owner-time and heap-staging are nested within Execute,
so their durations must not be added to Execute wall time.

The 54/53 total presents include loading UI. The first indirect-draw scene
presents are numbers 41/38, leaving only 14/16 scene presents. Application Ready
is at 37.923/28.589 seconds (`gm1953a-out.txt:31`, `gm1953b-out.txt:31`).
Application elapsed seconds and UMD QPC lack a shared start anchor; do not
pretend the first owner receipt is process start. First-indirect scene boundary
is independently reproducible from the UMD logs. All bins, including loading,
are in the handoff `frames-a.csv` and `frames-b.csv` with UMD line numbers.

For a clean late-scene comparison use bins 47–54 (2k) and 45–53 (10k):
these intervals have zero CopyTextureRegion and zero staged-upload receipts.
This is an explicitly chosen late interval, not a claim all startup is cheap.

| Nested measurement | 2k, 8 intervals | 10k, 9 intervals |
|---|---:|---:|
| Present-to-present wall seconds | 86.953592 | 79.429147 |
| Execute replay seconds | 86.411065 | 78.926009 |
| Final submit seconds | 0.089160 | 0.066255 |
| Outside Execute seconds | 0.453367 | 0.436883 |
| Owner draw_vbo seconds, inside replay | 9.705375 | 11.892744 |
| Owner bind + unbind seconds, inside replay | 0.002403 | 0.001727 |
| Final flush seconds, inside submit | 0.087713 | 0.064897 |
| Final wait/retire seconds, inside submit | 0.001130 | 0.000932 |
| Trim calls / successful trimmed residency requests | 28 / 28 | 51 / 51 |
| Trimmed allocation references / MiB | 986 / 3461.1875 | 1685 / 6767.1875 |
| Allocations in trimmed MakeResident requests | 408 | 1285 |
| Runtime MakeResident receipts | 0 | 0 |
| Heap checked-copy upload / download MiB | 10.1133 / 12.5 | 12.1797 / 25.875 |
| Owner submission fence deltas | 328 | 369 |

Thus 99.38%/99.37% of late wall time is in replay, not final CPU retirement.
Replay includes blocking driver/GPU/paging work: it is not pure CPU recording.
Outside Execute is only an upper bound for recording/application/present cost.
True GPU execution and paging wait seconds are not independently timed in this
capture. Runtime MakeResident receipts are API calls, not all internal residency
requests; the latter log only requests that trimmed. Counts above are a lower
bound on internal MakeResident calls. Trimming drops residency references with
EvictOnlyIfNecessary; it does not prove every trimmed byte was physically copied.

Ranked targets (nested, not three additive wall-time buckets):

1. **Residency/paging stalls during replay.** Late frames repeatedly trim and
   re-resident, with 3.5/5.67 trim calls per interval. A concrete 2k interval
   (present 48, 19.329706 s) has a 16.833562 s silent gap after E_PENDING
   MakeResident (`gm1953a-umd.log:17834`) and before a submit perf receipt
   (:17835), with zero texture-copy receipts. This is strong paging-stall
   evidence, but the silent gap alone cannot assign all its time to paging.
   Source: `umd/src/umd_gpuva_windows.c:398` retries after OOM, :736 submits
   internal residency, :782/:794 waits the paging fence on CPU. The pool reaches
   all 2016 MiB and has 2406 moves, with no commit/reserve/floor failures.
   Decoded KMD counters record **730179 total BuildPagingBuffer retries**:
   729905 VirtualTransfer + 235 VirtualFill + 39 DiscardContent. Slot39 is the
   aggregate, not an additional VirtualFill count; internal-before-normalization
   counters duplicate the operation counts. These are whole-boot totals,
   including both arms and desktop, NOT per-frame attribution.
2. **Draw replay and intermediate submissions.** draw_vbo consumes 9.705/11.893 s
   in these late windows, with 328/369 submission fence increments. Timing source
   `mesa/winsys/agx_d3d12_windows.cpp:3886` includes all work inside draw_vbo,
   including any flush/wait; it is not a shader GPU timer or pure recording cost.
   Replay dispatch/copy/other work plus blocking waits occupy the unclassified
   remainder. Perf counters show 2025/2055 'Begin drain: retire submitted' and
   1646/1647 'Flush writer due to: Blit' messages over whole runs, consistent
   with intermediate submission pressure. Counts have no associated duration.
3. **Loading uploads and checked copies.** Both runs record 5825 texture-copy
   calls and 1996 staged uploads, concentrated near loading/first-scene frames.
   First scene interval 41/38 lasts 34.594/55.210 s (replay 26.532/34.750 s).
   Late frames have none of those texture-copy receipts; checked heap copies
   remain at the byte volumes above, but have no individual timer. Source:
   `d3d12/agx_d3d12_ddi.cpp:6669`, owner staged upload at
   `mesa/winsys/agx_d3d12_windows.cpp:2340`, heap bytes at :125.
   Do not infer a per-frame CPU texture-copy storm from whole-run counts.

Every queue-wait receipt reports ordered=1 (58/62 calls). C83 is already active
in these captures. Owner `submit_owner`, :3906–3927, flushes then inserts the GPU
wait and skips CPU retire on success. Enabling it again cannot fix these runs.
Asahi primary `mesa/src/gallium/drivers/asahi/agx_blit.c:618` uses compute for
buffer copies, tries blit at :657, then CPU fallback at :662. No blanket
replacement of CopyTextureRegion or synchronization is justified by these logs.

## Fix and contract

Private paging storage was PAGE_SIZE (4096) in both queries, while a 64-bit
ADMISSION_PAGING_RECORD occupies 128 bytes. That admits only 32 records even
though the scheduler arrays accept 64. Public marker storage is 4096 bytes /
16-byte markers = 256 slots, and therefore is not the limiting resource.

The change defines ADMISSION_PAGING_PRIVATE_BYTES as the existing scheduler
maximum times sizeof(record) and publishes it in both paging queries:
`render-admission/include/render_admission.h:75`, `src/lifecycle.c:904`,
`src/memory_windows.c:204`. On the target ABI this requests 8192 private bytes.
It does not enlarge arrays, public DMA buffers, physical backing, or record ABI.
Pinned WDK `d3dkmddi.h:2634` and :11059 define the separate private allocation
fields for the segment and paging-buffer queries. This is a driver-owned
software-buffer capacity correction, not a hardware protocol change.

Encoder `src/gpuva_g3_paging_windows.c:1061` limits by private space, marker space
and remaining scheduler slots. It still splits system/scattered pages at 4 KiB,
coalesces contiguous local runs up to 4 MiB (:1135), and preserves MultipassOffset.
Submission validation `src/gpuva_g3_windows.c:3296` retains the 64-record bound.
For 65 noncoalescible pages, old storage needs 3 passes, new storage needs 2;
full buffers cover 256 KiB rather than 128 KiB. This is NOT a promise to halve
all retries or frame times. Small operations, coalescing, marker space and
existing buffer occupancy can impose different limits.

Regression executes the real encoder with both actual query expressions and
production capacity macros. It checks 64+1 records, exact byte coverage and
addresses, continuation, record validation, and preserves all existing legal
page-size/direction and reserved-bit tests. Sanitizers are enabled. Existing
resubmission, status, ownership and header suites remain applicable.

Ownership: VidMm schedules placement/eviction and allocates the advertised
buffers; KMD owns encoding, CPU paging execution and bounded queued records;
UMD owns residency references and waits. No change to rc6/rc7/rc11 transitions,
submission holds, failure propagation, trim eligibility or headroom. Hysteresis
cannot guarantee the next working set fits in 2016 MiB; suppressing pressure
trim risks reintroducing OOM. It needs per-allocation last-use evidence first.

m1n1 and Mu retain stage-2 and boot/ACPI ownership. Neither is changed. The
submodule source trees are unavailable in this isolated worktree, and no live
ADT/register/interrupt/launch inspection is permitted. The saved EXP1953
contract is 2016 MiB, m1n1 0d3a75b3, firmware/caps/signer unchanged. No hardware
constant or new Windows interface is guessed; no external source was copied.

## Next falsifiable checkpoint (integration owner)

Base 74345690 plus RC16 commits listed in REPORT.md. Pinned WDK build and exact
package/SYS/UMD/manifest hashes before preregistration. Keep
Wom1GpuFaultInfoRead=1, Wom1PoolFraction=3, HalfSize and SysmemSpill absent/0,
and the existing EXP1953 runtime settings, including GPU_WAIT=1.
Run 2k then 10k, windowed 1280x720, 240 s, prior process fully exited between arms.
Require the segment still reports 2113929216 bytes. Preserve known-good recovery;
collect evidence before ordinary exact-package GPU-visible cleanup.

WHY THIS HYPOTHESIS: (1) repeated late-frame pending residency precedes long
replay gaps; (2) 730179 paging-buffer retries occurred without backing failure;
(3) actual advertised private space wastes half the already supported records.

Metric: scene presents per minute AFTER Ready, excluding loading UI. Record a
Ready-to-QPC anchor and harness-close QPC in the next capture; report count,
denominator and rate separately. Approximate EXP1953 scene rates using
14/(240-37.923)*60 and 16/(240-28.589)*60 are 4.16 and 4.54/min, not 13/min
obtained by counting all loading presents. They are provisional: 2k drains to
Done at 246 s and the current clocks have no exact shared start anchor.
Late fully QPC-bounded rates are 5.52/6.80 per minute in the windows above.

Expected: fewer paging-buffer passes per comparable transfer and lower paging
wait/replay time, more post-Ready scene presents, no device loss or DWM fault.
No speedup or regression-free hardware result is claimed. Stop escalation on
new removal, coverage refusal, corrupted content or desktop loss.

WHAT IS STILL UNKNOWN: native WDK compile, actual speedup, GPU busy time,
per-allocation repeated page-in/out, bytes and time copied by VidMm versus UMD,
and present completion/display versus callback count. For a definitive split,
convert existing DxgKrnl ETW PagingOp/BuildPagingBuffer, QueuePacket/DmaPacket
start+stop, MakeResident/Evict and context/fence wait events, with QPC, PID/TID,
context, operation, allocation, bytes and status where available. Include CPU
thread scheduling/wait data if captured; otherwise it needs a later capture.
No guest conversion was attempted here.
