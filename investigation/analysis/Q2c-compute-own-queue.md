# Q2c: the compute work queue as its own Asahi Queue (2026-11-05, offline design)

## Problem (measured)
- About 4 render->compute boundaries per GravityMark frame idle 0.40 ms each, ~6 % of the frame (EXP2131 budget, EXP2134).
- One Asahi Queue (one notifier list, one notifier) carries TA, 3D and the KMD-made compute work queue. The firmware raises
  a notifier only when its processed count == its threshold (EXP2148). At depth two the head's event is therefore deferred
  to the pair's end (Asahi queue/mod.rs run(), U4), and the CPU re-kicks only after the GPU has gone idle.
- EXP2147 (S1, retire at the stamp) removed the gap (0.40 -> 0.07 ms, +3.4..6.1 % FPS on equal windows) but crashed the
  firmware (early reuse of objects before the event).
- EXP2148/2149/2151 (deferred threshold; a second notifier on the same notifier list) lost notifications. Asahi never puts
  two notifiers on one list.

## Asahi contract (drivers/gpu/drm/asahi)
- Per Queue (queue/mod.rs:436-560):
  - NotifierList: shared memory; list_head.next points to itself.
  - Threshold.
  - Notifier: threshold pointer, generation = queue id, cur_count 0, unk_10 0x50.
  - GpuContextData: a 0x40-byte default (fw/workqueue.rs:48-80).
  - Its vtx, frag and comp WorkQueues.
- Each WorkQueue's QueueInfo points at its Queue's notifier_list and gpu_context (fw/workqueue.rs:130-158).
- Event slots and stamps are global (event.rs). Separate Queues have separate thresholds and do not defer each other
  (queue/mod.rs:302-330).
- Resources are freed only at the event (workqueue.rs signal()).

## Our objects today
- Shared by TA, 3D and compute:
  - obj 0 GPUContextData;
  - obj 23 JobList (NotifierList);
  - obj 11 notifier;
  - obj 12 threshold.
- The compute QueueInfo obj 36 is cloned from TA obj 3 with +0x10 = obj 23 (apple_agx_render_shared_memory.c,
  InitializeComputeQueue).
- Compute event slot 2 is already separate from TA (0) and 3D (1). Completion is routed by slot in
  apple_agx_g13_queue_runtime.c.

## Q2c
Compute (QueueInfo 36, ring 37, pointers 38, sidecar 39) becomes Queue B:
- its own NotifierList, Notifier (generation A + 1), Threshold and GpuContextData, placed in the unused sidecar tail
  0xec18-0x10000;
- QueueInfo 36: +0x10 points at list B and +0xa0 at context B.

Renders, with their preludes, stay on A. The A threshold counts renders only (2 per render) and the B threshold counts
compute only (1 per job). In GravityMark's CRCR stream each queue then holds at most one job at depth two, so each job's
event fires at its own end, and slot and sidecar reuse stays event-driven. That is the same rule as Asahi, with no early
retirement.

Cross-queue ordering keeps today's firmware Barriers on global stamps (3D obj 10, compute fw stamp 0x8780).

## WINDOWS CONTRACT
- One node, HwQueuePacketCap 2, DMA_COMPLETED in fence order (a B event that arrives early is held), preemption at the DMA
  buffer boundary.
- ResetEngine re-initialises both Queues.

## WHAT IS STILL UNKNOWN (hardware only)
- Q1: whether the firmware accepts a second NotifierList, Notifier and GpuContextData on the synthesized compute queue.
- Q2: whether a Barrier releases across notifier lists.
- Q4: how fast B starts after the Barrier releases.
- Q5: fault and resume behaviour with one job on each Queue.

## Steps and checkpoints
- H0 (switch Wom1ComputeOwnQueue, default off; implementation on branch exp/q2c-compute-queue):
  - Run at depth one (Wom1Depth2General=0) first, so only Q1 is in play.
  - Pass: suite 119/0, correct images, no lost completion or RTKit crash.
  - Then at depth two:
    - Pass: gap before compute <= 0.1 ms and HD PERF +3..6 % against the off arm (ABBA).
    - Fail signatures:
      - a lost B completion with the firmware alive -> Q1;
      - a crash at the first B kick -> layout;
      - a compute ahead of its render -> Q2.
- Fallback if Q1 fails: S1' (fence at the stamp, objects at the event, three slot copies). Estimated +2-3 %. It keeps
  EXP2147's open risk of three or more jobs on one notifier.
