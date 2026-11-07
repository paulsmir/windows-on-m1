# RC49: windowed GravityMark composition discriminator

## Decision and implementation plan

The EXP1973 report establishes visible taskbar flicker, not its cause. Do not
change scheduling, preemption caps, damage handling or the DCP protocol on this
evidence. Deliver a bounded, read-only capture of the missing queue/latch timing.
The task explicitly permits this discriminator when no correct fix is proven.

1. RED/GREEN: exercise the actual scanout ISR with a queued flip timestamp;
   preserve a queue event only for its exact matched latch, including identity.
   Stale interrupts and periodic ticks must not manufacture queue/latch pairs.
2. Add a bounded `--vsync-follow SECONDS` mode to the existing BLT probe. Poll
   the existing nonpaged snapshot at 50 ms, emit only new events, and expose
   generation and clock correlation. No arm, GPU work, registry writes or
   hardware synchronization escapes. Default probe behavior stays unchanged.
3. Add and test an offline analyzer for EXP1973 serial/UMD/display-ring evidence
   and the new capture. Reject unsupported binary layouts; expose capture loss,
   duplicate snapshots and missing queue/latch pairs rather than invent timing.
4. Run affected host tests, then `cd tests && python3 run_against_baseline.py`.
   Read tandem review, commit small changes, and write the handoff REPORT.md.

## Ownership and source boundary

Windows VidSch owns priority selection on the advertised software-scheduled node.
The KMD owns ordered dispatch, boundary-preemption notification and exact-latch
display notification. Asahi's firmware queues have a priority argument, while
this KMD uses one serialized node and the fixed group-1 transport doorbells.
m1n1 owns A407/A408/D589 and scanout broker publication; Mu owns boot resources.
None of those contracts, IRQ routes, mappings, power or recovery change here.

Primary sources inspected: WDK 26100 `d3dkmddi.h` (PreemptCommand, scheduling
priority, flip DDIs) and SDK `shared/d3dkmdt.h` (preemption granularity);
Asahi `drivers/gpu/drm/asahi/queue/mod.rs:445,477,526`; KMD `lifecycle.c:775`,
`scheduler_windows.c:384`, `work_queue_windows.c:77`, `display.c:898`,
`scanout_windows.c:938,979,1175`; UMD `umd_gpuva_windows.c:45,1782` and
`umd.c:1120`. The local submodule object stores lack both pinned gitlinks and
the experiment's named m1n1 revision. Read the immutable rc30 handoff snapshot
instead: `base-m1n1/src/dcp.c:621,661` and `dcp_iomfb_latch.c:60` enforce exact
D589 identity and allow D589 during the synchronous A408 call. This is reference
protocol evidence, not verification of EXP1973's exact binary. Mu source is
unavailable in this checkout; no firmware edit is proposed. Offline instruction
precludes live ADT/register reads and web requests.

The diagnostic preserves current launch/recovery and package contracts. The
smallest next checkpoint is a same-profile windowed benchmark with before/after
animated-desktop controls, bounded vsync capture, UMD trace and timestamped panel
video. A large inter-flip gap with short queue-to-observed-latch time is upstream
of scanout; promptly queued flips with large observed-latch latency move the
boundary to KMD/broker/DCP (including delayed interrupt delivery). Regular exact
latches with visible corruption instead require surface/content evidence.
The integration owner performs ordinary exact-package recovery; RC49 runs none.

## Verification and detailed evidence

### EXP1973 findings

`full.log` has 2,401 A408 records, 2,403 exact-D589 records and 2,399 common
IDs. Applied-only text: 949 and 2214; latch-only text: 154, 194, 207, 1279.
The text has no timestamps. Neither cadence nor lost hardware swaps follows
from these omissions; D589 may precede the A408 log line by protocol design.

The v2 `Wom1DisplayRing1131.bin` retains only 256 of 2,764 records, starting
at QPC 6027772937 and ending at 7319817796 (24 MHz). These are 128 MPO3 calls
and their 128 forwarded legacy calls, not 256 distinct flips. All retained
MPO3 records have flags 5/detail 0x101 (one enabled plane); legacy records have
flags 4/detail 2. Median *retained* flip gap is 118.914 ms; p95 554.199 ms,
max 23,957.108 ms. This ring filters out ordinary <=50-ms gaps, and its tail
includes post-app idle time: these are not frame-rate percentiles.

The app's 1,318 present receipts span QPC 1542153619..6339056057. DWM pid1224
phase-5 fence-wait receipts in that interval: 1,921 waits, median 1.093 ms,
p95 1.468 ms, max 40.650 ms. Before/after median waits are 0.813/0.836 ms.
Inter-wait medians are 19.537/96.461/16.734 ms before/during/after. Phase 2
(paging-fence wait, `umd_gpuva_windows.c:826`) has exceptional waits up to
171.712 ms during the app. These are
submission events, not a frame counter. Slow app updates and sparse desktop
damage can explain low composition cadence; these numbers do not establish
DWM starvation or corrupt taskbar pixels. `shots/` is empty, and no aligned
panel video or persistent scanout timeline was supplied.

### Windows contract and comparison

WDK `d3dkmddi.h:4407` names the preemption fence/node/engine; `:1017` requires
the last *completed* submission in DMA_PREEMPTED. SDK `d3dkmdt.h:1287` separates
DMA-boundary from primitive/pixel/shader and compute-dispatch/thread granularity.
`d3dkmddi.h:11183` describes non-hardware context priority (system 0..31 and
in-process ordering); `:5320` is the separate hardware-context scheduling API.
`d3dkmthk.h:4500,5952` exposes context scheduling priority to the runtime.

DWM's high-priority scheduling intent must be realized through Windows VidSch
and truthful preemption/completion. It is not permission to identify dwm.exe
inside the KMD and reorder accepted fence-bearing packets. Our advertised
contract is one node, DMA-buffer-boundary preemption, max one queued flip
(`lifecycle.c:775`); it is not finer-grained hardware preemption. The dispatcher
requires no active/dispatched job and no preemption block
(`work_queue_windows.c:90`). `scheduler_windows.c:428` captures the cutoff and
active fence, drops never-started work and waits for the actual boundary before
notification. Asahi firmware queues accept priority, but this transport binds
one group at priority 1 (`backend_platform_windows.c:2383`), with one firmware
job dispatched at a time. There is no evidence that merely changing that group
would let DWM interrupt an in-flight app packet.

Without finer preemption, avoiding starvation means bounded indivisible jobs,
prompt boundary notification and yielding to VidSch between jobs. Arbitrary
driver-side queue sorting would violate ordering. The supplied trace does not
capture DWM's effective context priority or a preemption-request-to-completion
timeline; neither a missing priority callback nor a starvation fix is proven.
Official documentation entry points for the integration owner (not fetched in
this offline task): Microsoft Learn `display/gpu-preemption`,
`ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_preemptcommand`, and
`ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_notifycontextprioritychange`.

### Diagnostic and falsifiable checkpoint

Queue entry stores one timestamp before the existing broker call. Only an exact
matched latch inserts kind 8 followed by existing kind 3, both under the ISR's
existing serialization. Kind 8 is retrospective: sort by event sequence for
loss detection and use its timestamp only with the matching pending sequence
and address. The receipt reports guest-observed queue-to-latch latency, including
MMIO and interrupt-delivery delay; it cannot measure the panel's physical phase.
No ring growth, new escape ABI, GPU access, per-frame log write or registry write
is added to the KMD. The existing 32 rolling slots can still overflow if the
50-ms reader stalls. The analyzer exposes internal gaps, incomplete sample tails,
unpaired events, clock resets and conflicting duplicates. A ring gap is lost
measurement, never an inferred hardware fault. Old UMD app/DWM joins require
explicit `--same-boot` provenance because frequency/PID alone cannot prove it.

On the next integration-owned run, use the existing exact package/recovery
process, unchanged firmware/caps, and a rebuilt probe:

```
apple_agx_blt_probe.exe --vsync-follow 300 > vsync.txt
python3 tools/agx_composition_analyze.py --vsync vsync.txt > vsync.json
```

Capture 20 seconds of an animated desktop, one windowed GravityMark run, then
20 seconds of the same desktop animation. Keep the UMD logs, raw display ring,
ETW GPU scheduling/context/preemption events, and timestamped video of the
taskbar. The clock samples bracket QueryInterruptTimePrecise in QPC to join
guest events with UMD time; external video still needs an operator-visible marker.
Do not infer starvation from an idle desktop's missing flips.

At a visibly marked flicker, examine only loss-free intervals:

- Late queue entry, short queue-to-latch latency: boundary is upstream of
  scanout. Correlate DWM phase-5/phase-2 and ETW ready/preempt/completion events.
  Long app active-packet/preemption wait supports bounded-job scheduling work;
  short waits do not.
- Prompt queue entry, long queue-to-observed-latch latency: investigate
  broker/DCP/interrupt delivery, without conflating it with missing DWM work.
- Regular exact latches and short waits while taskbar pixels flash: capture
  the composed surface at the marked interval and compare with video. This
  points toward content/lifetime/damage or scanout content, not a proved priority
  failure. Retained unchanged MPO3 flags alone cannot exclude earlier switches.

Final source references, test output and review dispositions are also in the
task's handoff REPORT.md. This is a measurement-gap correction; the visual
flicker remains unproven and no hardware verdict is claimed.
