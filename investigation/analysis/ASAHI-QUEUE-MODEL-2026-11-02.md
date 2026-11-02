# Asahi/Apple firmware queue model vs the AppleAgx KMD (2026-11-02)

Question (user): why does macOS run GravityMark FHD at 44 FPS (22.5 ms) when
our frame is 36 ms, and can the KMD submit work the way Asahi (= the macOS
firmware protocol) does?

## Measured (EXP2073, FHD 100k, median per frame)

8 jobs per frame, strictly one at a time: C0 2.3, R1 7.9-8.2 (main 3D),
C2 2.8, R3 0.6, C4 8.6 (lighting), R5 0.8 (+TA 2.0), C6 2.0, R7 1.1 ms.
Launch per job (completion of N -> start of N+1): compute 0.31 ms, render
0.56-0.59 ms, C6 1.64 ms (a DWM job ahead in 95% of frames). About 4.3 ms of
launch plus about 1 ms late per 36 ms frame. Nothing overlaps.

## Asahi (sources inspected)

- `drivers/gpu/drm/asahi/workqueue.rs`: the firmware schedules work commands
  from per-type ring buffers (vertex, fragment, compute); `MAX_JOB_SLOTS` 127
  per queue; completion by event stamps.
- `queue/mod.rs`: one queue per context holds three sub-queues `q_vtx`,
  `q_frag`, `q_comp` (`:126-128`). One submission carries several render and
  compute commands; dependencies between them are firmware `Barrier` commands
  that wait on another sub-queue's stamp (`:787-820`, `vdm_barrier`,
  `cdm_barrier`). The CPU does not sit between two jobs.
- `buffer.rs:1-30`: one tiled vertex buffer (buffer manager) per render queue
  is shared by all its render jobs; each job only gets a scene
  (`new_scene`, `:544`). There is no per-job save/restore of the manager.

## AppleAgx KMD (from `docs/superpowers/plans/2026-10-28-firmware-depth-2-job-window.md`)

Single-job state: one RenderPacket, one scheduler ActiveFence, one backend
PendingJob, one queue-provider PendingFence, one compute sidecar (object 39),
G3 ActiveProcess/BeginJob, and the buffer-manager objects {1,20,21,22,13}
restored before and saved after every render job, keyed per job.

The depth-2 window (rc102) kept that model and only let a second job start
when it had the same process, the same manager key and no paging fence:
EXP2051 C->R failed with manager_bind, EXP2052 found under 1% of pairs
eligible (3 kicks in about 2300 jobs) and hung (0x116). Parked.

## Difference that matters

| | Asahi / macOS | AppleAgx |
|---|---|---|
| jobs given to firmware | many per queue (ring of 127) | one at a time |
| compute vs render | separate sub-queues, firmware barriers | one serial stream |
| ordering | firmware stamps + Barrier commands | CPU observes completion, then kicks |
| buffer manager | one per render queue, its firmware objects owned by that queue for its lifetime | per-process manager memory (page list, block list, TVB heap: `apple_agx_g3_private_storage.h`, key = process + those 3 ranges), but ONE set of firmware manager objects {1,20,21,22,13} per adapter, restored before and saved after every render job (they are swapped between DWM and the app) |

Correction (same day): the manager key is already per process, as in Asahi;
it does not change between GravityMark's render jobs. What blocks a
successor render is the save/restore protocol around the single shared
object set: a render successor cannot be built while the previous render's
manager save is pending (`AdmissionJobWindowManagerNeedsIdle`, refusal 61,
EXP2051 manager_bind). Asahi has no save/restore: each queue's buffer objects
stay firmware-owned.
Hypothesis: per-context firmware manager objects (no save/restore), a
separate compute sub-queue and firmware barriers remove the per-job CPU
turnaround.

## WHAT IS STILL UNKNOWN

1. Whether the firmware build we drive (m1n1 path, J313 13.5 firmware)
   accepts a shared manager across render jobs with different targets (U1 of
   the depth-2 plan).
2. How deep macOS actually queues for GravityMark, and whether compute and
   render overlap on this GPU: m1n1 `proxyclient/hv/trace_agx.py` running
   macOS under the hypervisor answers it directly.
3. How WDDM's one-fence-per-DMA-buffer contract maps onto multi-command
   firmware submissions (dxgkrnl scheduling, preemption, TDR).

## Next

1. Map each single-job object above to its Asahi counterpart (owner, lifetime).
2. Hardware unknown 1 as one variable: shared manager at depth 1.
3. macOS hv trace of GravityMark Metal (needs a macOS boot to fetch the
   kernelcache).
