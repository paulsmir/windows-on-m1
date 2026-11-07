# D3D12 C82: where Night Raid GT1 frame time goes (EXP1899N, pid 808)

Requested by the pipeline session (2026-10-23).

## Inputs and method

- Source: `measure-d3d12-owner-time` {draws, bind, draw_vbo, unbind, flush,
  retire us} and `measure-d3d12-execute` {lists, commands, replay us, submit
  us} of pid 808 in EXP1899N `user-umd.log`.
- Coverage: 4325 owner submits, 4740 executes, 887679 draws.
- Frame boundary: the 400-700-draw submit that recurs once per frame. 309
  frames; the medians below are over the middle 60%.
- The median frame span is 3.5M QPC ticks: about 146 ms at 24 MHz (about 6.8
  fps; GT1 was reported at about 6.5 fps).

## Median frame

| part | ms | share |
|---|---|---|
| replay (CPU) | 80.6 | 55% |
| - draw_vbo of the GBuffer submit (~540 draws) | 68.1 | 47% |
| - draw_vbo of the other ~1870 draws | 5.5 | 4% |
| - bind and unbind (incl. heap BatchRead/Write) | 2.0 | 1% |
| submit: retire wait | 53.1 | 36% |
| submit: flush | 4.3 | 3% |

Draw mix per frame, from the draw receipts:

- about 1780 depth-only indexed draws, rtvs 0;
- about 460 indexed draws with rtvs 3;
- about 76 ExecuteIndirect draws: rtvs 2, non-indexed strips, MaxCount 1,
  no count buffer;
- about 72 draws with rtvs 2.

Submits with 1000 or more draws (the depth pass) cost 1.2 us per draw in
draw_vbo. The GBuffer submit costs 110-150 us per draw every frame, so this
is not warm-up.

## Ownership

- The cost is inside Asahi `agx_draw_vbo`. The owner's bind work (including
  `AgxWin32AsahiBatchReadResources`) is outside the timed window and small.
- No current receipt says what inside draw_vbo is slow:
  - On Windows, the Asahi `perf_debug` reasons go to `AgxWin32PerfNote`
    (native-asahi-batch-lifecycle.py). Only the D3D10 owner installed the
    hook, so they are absent in D3D12 processes.
- Asahi's indirect fallbacks (`util_draw_indirect`, a CPU map of the
  arguments) apply only with a count buffer or draw_count > 1. Night Raid
  uses neither, so the CPU fallback is not the cause.

## C82 (2000c063): receipt-only discriminator

- The D3D12 owner installs `AgxWin32PerfHook`. It emits `measure-perf-reason`
  in the D3D10 format: {serial, new count, total, presents, text}. Expect
  variant compiles, flush and sync reasons, and BO cache misses per present.
- `measure-d3d12-owner-time` appends three values: indirect draw_vbo us, the
  longest draw_vbo us, and the number of draws of at least 1 ms.

Reading the next Night Raid run:

- SlowDraws times about 1 ms near 68 ms, with a "Compiling ... variant"
  rate per present: per-draw variant misses. Fix the shader-key churn.
- A "Flushing/Syncing due to ..." rate near 500 per present: an intra-pass
  flush or sync dependency, such as a resource read and written in the same
  pass, or an encoder-full condition. Fix that dependency.
- Uniform cost (MaxDrawVbo about 150-300 us, SlowDraws 0) with no reason
  rate: per-draw descriptor or uniform upload proportional to the bound
  textures. Next instrument the `agx_upload_textures`/uniform paths (Mesa
  patch hook).
- Indirect us near 68 ms: the 76 indirect draws are the cost, about 0.9 ms
  each.

## Second fix: the retire wait (36%), architecture

- The D3D12 runtime signals fences on the queue's kernel context.
- The owner submits on its own context, so ExecuteCommandLists must finish
  (AgxD3d12OwnerSubmitAndWait retires) before Signal can be correct.
- Overlapping CPU replay with GPU work needs a GPU-side cross-context
  dependency: the owner context signals a sync object at completion, and the
  queue context waits on it (`pfnSignalSynchronizationObjectFromGpu2Cb` /
  `pfnWaitForSynchronizationObjectFromGpuCb`, WDDM 2.x monitored fences).
- Ceiling: max(80.6, 53.1) instead of 133.7 ms per frame. This is KMD/WDDM
  work and is not started here.
