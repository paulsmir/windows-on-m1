# R152 — firmware fetched job5; its buffer manager names absent process mappings

R152 is the **offline diagnosis / proposed-receipt** branch of the assigned
task. No driver fix, package, firmware build, Air access, staging, rearm or
hardware run occurred. The deterministic construction defect is reproduced
RED; there is no GREEN fix claim. EXP866 remains the hardware verdict and
its durable ordinary Code28 receipt remains the recovery reference.

## Answer at the current boundary

Explorer PID5272, graph7, context generation14980002, native sequence5,
scene61 and fence955 passed publication. Firmware consumed both Run messages
and fetched both queues' fifth work. It then sent **Timeout, event slot0**.
This is stronger than the original “both queue consumers remain unchanged”
description: the **completion** cursors remain unchanged; the firmware read
cursors advanced. An absent doorbell or pre-publication KMD rejection does
not explain this snapshot.

The firmware-visible BufferManagerInfo still names page list `0x2020000`
and block list `0x2030000`. The accepted Explorer manager20 instead owns
`0x24d0000` and `0x24e0000`. The graph has no leaf for either stale address
and has valid private-backing leaves for both declared addresses. The active
TA and 3D work both point to this stale BufferManagerInfo. Thus admission
validated one process graph, while materialization retained pointers from
another manager lifetime. The violation belongs to KMD/shared materialization,
not a speculative Windows cap, m1n1 TLB or Mu resource change.

This is a confirmed stale construction contract and the strongest cause of
the fifth-job stall. It is **not** a decoded UAT fault: those registers are
missing, and no current-job hardware-start timestamp is independently tagged.

## Publication, fetch, event and completion evidence

Read-only cdb sessions used the saved EXP866 full kernel dump and matching
package866 PDB on the Windows builder. Scripts, excerpts, exported binary
objects, decoder and hashes are under `investigation/evidence/R152/`.
The initial graph leaf filter had a debugger-expression error; `leaves.ps1`
is the corrected completed traversal. The failed attempt is retained in
`walk-excerpt.txt` and is not used to assert leaf absence.

| Quantity | TA | 3D |
|---|---:|---:|
| Run-channel read / write |5 / 5|5 / 5|
| Work-ring CPU write |6|10|
| Work-ring GPU read (+0x30)|6|10|
| Work-ring GPU done (+0x00)|5|8|
| QueueInfo read1/read2/read3 |6/6/6|10/10/10|
| Pending expected stamp |7a000500|3d000500|
| Current stamp |7a000400|3d000400|
| Event / complete |0 / 0|0 / 0|

Run records retain TA heads2,3,4,5,6 and 3D heads2,4,6,8,10; only the first
has NewQueue1. Queue capacity is0x500, command-channel capacity is0x100;
there is no wrap at four. Event numbers remain TA0 and3D1. The event ring's
first eight records are four `(Flag bit1, Flag bit0)` pairs. Record8 is
type4, counter0, stamp index0; all remaining bytes are zero. Read8/write9
and LastEventMessage preserve that same timeout. There is no GrowTVB,
ChannelError or type0 Fault among these nine records.

The retained completion760, G3.LastCompleted760, native sequence5, eight
flag records, and manager gpu_counter/gpu_counter2=4 corroborate four prior
joined native completions under the serialized queue contract. These are
not a retained list of four Windows fence/process owners. Scheduler954
remains a watermark including CPU work.

LastDrainGuard8 is PrepareEvent; LastIngestGuard2/result9 is runtime
ResetFailed. Timeout handling called terminal quiesce, which could not stop
the firmware. RTKit StopPhase is ApRequested, Running1, CrashlogCrashed0.
These are failed recovery observations, not evidence that event decoding
failed or that RTKit never encountered an error. The later Windows 0x116
at43.603s and live0x141 at43.540s remain unchanged.

Timestamps28/29 are2396453714/2493792337;30/31 are2396450884/2493789680.
They persist across jobs and have no sequence tag. They cannot establish
that fifth-job TA hardware started. Queue fetch does not prove execution
past StartTA, and 3D can wait on TA's fresh-stamp barrier.

## Process/VM and backing comparison

Current broker-client graph: root `0x9d6014000`, mapping generation1400,
lease token5, slot1, JobInFlight1 and LastStatus0. Active work TA/3D and
TA Start/Finalize fields all contain slot1. The retained InitBM is also
slot1 with first stamp7a000100; it was not republished for the new manager.
The other two VM-slot fields remain covered by the inspected seven-field
builder; they were not separately exported in this analysis.

The broker's successful lease contract writes rootPA|VALID|(slot<<48),
TTBR1=0, invalidates that ASID and reads back both words. JOB_BEGIN follows
that accepted lease. Its actual MMIO TTBR words cannot be recovered from
this dump; broker success is the source-backed software evidence, not a
substitute for a hardware TTBR snapshot. G13 Asahi also binds per-VM TTBR0,
leaves TTBR1 zero, and invalidates the previous owner's ASID. Asahi additionally
uses handoff locking. There is no measured fifth-job difference selecting
that handoff/TLB path over the concrete stale pointers.

All nine surviving process-list entries were enumerated. Graph3 is absent.
Explorer has two surviving contexts14980001/14980002; the first has no
retained private fence/root-set receipt, while the second owns955.
Graph5 retains a different SetRoot root but no private manager/scene.
Freed prior scenes and scalar completion fields prevent assigning all
jobs1–4 to processes or proving job5 is Explorer's first native submission.
Do not turn creation order, current PID, or a VA coincidence into that proof.

| Process range | Current Explorer VA | Bytes |
|---|---:|---:|
| Page list |0x24d0000|0x10000|
| Block list |0x24e0000|0x10000|
| TVB heap |0x24f0000|0x400000|
| User buffer |0x2440000|0x20000|
| Tilemap |0x2460000|0x10000|
| Heap metadata |0x2470000|0x10000|
| Tail-pointer cache |0x2480000|0x10000|
| Preemption scratch |0x2490000|0x10000|
| Auxiliary framebuffer |0x24a0000|0x10000|

Graph walk: root9d6014000/index0 → middle9d601c000/index1 →
private leaf table91ecc0000. Index0x134 maps page-list91ecd0000;
index0x138 maps block-list91ece0000, both PrivateBacking. No leaves at
index8 or0xc exist anywhere in the completed graph traversal, including
that table. These are the indices for the stale lists. This is a software
graph observation after timeout; it does not manufacture absent UAT memory.

Native render is16x16, flags4, one layer/sample, sample size8;
VDM stream0x1860000, scissor0x1820e40, depth-bias0x1820e80.
The 4MiB/32-block manager meets the declared capacity formula. BufferManager
block_count32, page_count128, control total/write32/32 do not demonstrate
exhaustion. No GrowTVB or partial-render progress receipt exists.
BufferThing.user_buffer is0x2440000 and matches this job, so it is not a
second measured stale pointer here. Previous jobs' native attachment,
texture, USC and TVB bytes/owners were overwritten or freed; comparisons
against them are unavailable, not equal. Native image-side PhysicalAddress
fields on process objects are template staging fields; do not confuse them
with the graph's real private backing IPAs above.

## Deterministic replay and why no speculative InitBM fix

`run-manager-probe.py` compiles the actual shared-memory fixture and production
materializer under ASan/UBSan. It first binds legal synthetic lists3100000/
3120000, then supplies6100000/6120000 after the initial manager is initialized.
The real `BuildActiveG4Job(IncludeInitBm=false)` succeeds while retaining the
first pointers. The desired current-pointer assertion fails (SIGABRT), as
recorded in `manager-probe-result.json`. Synthetic placements test a general
lifetime property; no trace address is an admission rule.

The same probe tries the obvious `IncludeInitBm=true` workaround. It updates
the pointers **and erases the live queue-pointer bytes**. In current source,
IncludeInitBm also sets InitializePersistent and copies/relocates all
36 firmware objects, including queue rings, job list, pointers and event
control. Clearing BufferManagerInitialized alone is therefore not a fix.
Updating only pointer words is also insufficient: Asahi preserves each
buffer's mutable Info/BlockControl/Counter across inactive slot periods and
emits InitBuffer when its slot is rebound. An A→B→A sequence needs those
owner-specific counters and lists, not freshly reset generic template state.

A safe correction must separate queue lifetime, per-manager firmware state,
and per-scene state; retain manager identity/generation and its mutable
context0 objects until genuine retirement; select/rebind at an idle boundary;
and keep queue/event/stamp sequence state intact. Required RED→GREEN coverage
includes A→B→A, same VA/different root, same owner/new manager generation,
same manager/new scene, aborted publication, active-job refusal, and exact
InitBM/head accounting. ARM64 /W4 /WX /analyze and baseline-compared full
suite are required when that driver correction exists. They are **not run or
claimed** for this analysis-only change.

## Smallest receipt and proposed EXP867

WHY THIS HYPOTHESIS:
1. The firmware consumed/fetched the fifth work and reported timeout0; lack
   of notification is less likely than a job-owned input failure.
2. Both work items point to an active manager containing lists absent from
   the selected process graph, despite correct current declared lists.
3. Real materialization reproduces stale pointers; the naive InitBM replay
   also reproducibly damages queue state, so it cannot be the experiment.

**EXP867 proposal: one variable, a passive final-materialization receipt**
on exact package866 source17ee8894, unchanged R143 firmware, Flush excluded.
This is a proposal, not preregistration/readiness or hardware authorization.
Do not run it merely to rediscover the known mismatch: first try to close
the safe ownership correction offline. Use this receipt-only discriminator
if the remaining transition/access cause cannot be resolved there.

At the existing serialized worker boundary, after active relocation and before
either queue publication, retain a bounded native-job history (e.g.16 entries)
in nonpaged adapter state: sequence/fence, Windows PID/context generation,
graph ID/root/mapping generation/slot/token, manager ID/generation, scene
ID/generation, declared and actual page/block/user-buffer pointers, manager
Info/control/counter snapshots, selected InitBM and expected ring heads.
Record the graph-walk outcome for each actual pointer using the existing
read-only walk while the process state lock protects the snapshot. Mark the
entry complete last; never change acceptance, mappings, counters or timeouts.
Join the existing completion path to that entry and retain the first terminal
event plus queue read/done/write values. Capture before quiesce, not after
ResetFromTimeout. No new MMIO register reads or disk flush in the submit path.

Expected discriminator: the first failure correlates with an owner/generation
transition and a stale/absent actual list, or it occurs with all final pointers
and their graph translations correct. The latter rejects the pointer mismatch
as sufficient and moves the boundary to firmware/GPU fault capture. Completed
entries settle whether job5 is a first process switch without guessing prior
fence ownership. Lack of a complete entry is inconclusive, never a zero fault.

A future approved run must hash/package/preregister its exact source and
receipt layout first. Preserve the EXP866 recovery contract: collect dumps
and original-boot receipts/ETL first, exact package cleanup through mandatory
hiddenCode45 after a bugcheck, then durable ordinary Code28. No retry/rearm
is implied. Require fresh joined completion across the observed transition,
then the existing600s and nonzero-scanout checkpoints; these remain unproven.

## Sources and ownership

Inspected EXP866 dump, matching PDBs, publication/event/work/BM objects, process
graph and original verdict; R151 next-boundary/sequence code and R137 private
storage lifetime. This offline task uses saved hardware state; no new live
ADT/register measurement was needed or authorized.

Primary sources: Asahi `queue/render.rs` (VM slot and InitBuffer on rebind),
`buffer.rs` (per-buffer objects, active slot/token and scene lifetime),
`fw/buffer.rs`, `fw/channels.rs`, `mmu.rs`; current m1n1
`hv_agx_gpuva_v5.c`, `hv_agx_retained_platform.c`, and G13/V13_5
`fw/agx/{channels,cmdqueue,microsequence}.py`; Mu
`J313AppleAgxAbiAdmission.asl.inc` and the saved EXP866/R143 launch contract.
Official Microsoft [per-process GPUVA](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/per-process-gpu-virtual-address-spaces)
and [SubmitCommandVirtual](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_submitcommandvirtual)
support the process/context ownership boundary.

Local implementation: `gpuva_g3_windows.c`, `render_backend_image.c`,
`backend_platform_windows.c`, `apple_agx_g13_queue_{provider,runtime}.c`,
`apple_agx_platform_provider.c`, `apple_agx_g13_codec.c`,
`apple_agx_render_shared_memory.c`, `apple_agx_g4_builder.c`,
`apple_agx_gpuva_g3_graph.c`, and the template's existing relocation schema.

KMD owns active object construction, queue/event processing, manager/scene
lifetimes and Windows completion. VidMm owns the process GPUVA contract;
R137 owns the reserved private range/backing lease. m1n1 owns TTBR publication,
TLB invalidation and inherited broker/power state. Mu exposes the retained
reserve/ACPI contract and is not involved per job. Genuine GPU-stop recovery
remains fail-closed. No external code was copied.
