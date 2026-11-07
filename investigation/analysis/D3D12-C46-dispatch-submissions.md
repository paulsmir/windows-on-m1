# D3D12 C46: a dispatch does not force a submission

## Evidence

EXP1850 ModelViewer F (`x64-ModelViewerF-umd.log`, `measure-d3d12-execute`):
the frame's main command list (178 commands) spends ~42 ms in replay and
~4.6 ms in submit+wait; its ~106 draws take ~1.8 ms of that replay
(`measure-d3d12-owner-time`). The rest of the list is ~70 dispatches and the
ExecuteIndirect expansions, about 0.5 ms per command.

`AgxD3d12OwnerDispatch` called `pipe->memory_barrier(PIPE_BARRIER_ALL)` after
every `launch_grid`. Asahi's `agx_memory_barrier` flushes every active batch
with `incoherent_writes`, which a writable SSBO or image sets
(`agx_uniforms.c`, `agx_batch_track_image`): each UAV-writing dispatch was one
kernel submission, with at most `AGX_GPUVA_IN_FLIGHT` (4) in flight.

## WINDOWS CONTRACT

D3D12 orders dependent work through the application's barriers (UAV and
transition barriers). Dispatches without a barrier may overlap; executing them
in order is a valid, stricter implementation.

## AGX/ASAHI CONTRACT

- `agx_launch_internal` follows every launch with `agx_cdm_barrier`, which sets
  the cache flush/invalidate bits (libagx_dgc.h: "we hit this with blits, which
  need -- in between dispatches -- the PBE cache to be flushed and the texture
  cache to be invalidated"), so the launches of one compute batch are ordered
  and see each other's writes.
- Writable SSBOs and images register the batch as the resource's writer
  (`agx_batch_writes_range`); a read from another batch (draw, copy, indirect
  arguments: `agx_launch_grid` reads `info->indirect`) flushes the writer
  first (`agx_batch_reads` -> `agx_flush_writer_except`); CPU maps sync the
  writer (`agx_prepare_for_map`; the owner's CPU copy calls
  `agx_sync_writer`).
- `agx_memory_barrier` exists for GL's `glMemoryBarrier`, whose incoherent
  writes are not otherwise synchronised.

## TRANSLATION

No `memory_barrier` after a launch. ResourceBarrier stays a no-op: in-batch
CDM barriers and Asahi's cross-batch hazard tracking cover every D3D12 barrier.

## Instrumentation

`measure-d3d12-owner-fences {fence delta, newest fence}` per submit
(`AgxWin32AsahiContextNewestFence`): the submissions one ExecuteCommandLists
caused.

## WHAT IS STILL UNKNOWN

The submission count after the change and the remaining per-frame cost.

## Checkpoint

ModelViewer F/H frame time below EXP1850's ~58 ms with identical H pixels;
probes (compute chains C5/C41/C42) and samples unchanged.
