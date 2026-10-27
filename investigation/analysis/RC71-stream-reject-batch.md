# RC71: GravityMark with the GPU stream: reject-batch after 78 s, no GPU fault

Offline branch `codex/rc71` (base `aa235ef6`). No hardware, guest, SSH or
experiment directory was written. Evidence (read only):
`.local/experiments/EXP1990-stream-params/evidence/EXP1990/` `gm1990b-umd.log`,
`gm1990b-err.txt`, `gm1990a-umd.log`, `kmd-receipts-probe-end.txt`. The package
was integration `22cd6d7a`; line numbers below are at that commit (identical at
the branch base for every file cited).

## 1. The failing rows

`gm1990b-umd.log:207419-207421`:

| row | encoder | decode |
|---|---|---|
| `reject-batch {00000004 000101f1 0 0}` | `owner_backend_fail` (`agx_d3d12_windows.cpp:345`) via `AgxWin32BackendFailHook` in `AGX_WIN32_ASAHI_FAIL` (`agx_win32_asahi_bo.h:135-140`): `(file << 16) | __LINE__` | kind 4 (backend failure), file 1 = `agx_win32_asahi_bo.c`, line 0x1f1 = **497**: `AgxWin32GpuvaBind` failed in `agx_bo_create` (`:490-497`); the backend is marked Failed. |
| `reject-batch {00000001 000001f9 0 0}` | `batch_refuse` (`agx_win32_gpuva_batch.c:458-463`) | kind 1, line 0x1f9 = **505**: `AgxWin32AsahiBatchBegin` refuses because `b->Failed` (`:500-505`). |
| `measure-d3d12-first-fault {000100b6 0 0 ef3a0030 1}` | `owner_first_fault` (`agx_d3d12_windows.cpp:427-435`) | site kind 1 = projected `agx_batch.c` (`build-native-asahi-state.py:6631-6647`), line 0xb6 = **182**: `agx_batch_init` sees `!AgxWin32AsahiBatchBegin(batch)` and faults the context (projected `agx_batch.c:181-184`, regenerated offline from `22cd6d7a`). flags 0: `ctx->batch` was NULL; 0 draws; context low word `ef3a0030`; first of 1. |

Then `measure-d3d12-execute hr=0x887a0006` (`:207422`) and the app reports
"device removed" at 1:18.752 (`gm1990b-err.txt`). No Wom1GpuFault/GpuReset:
the GPU did not hang; the UMD refused all further work after one failed BO
bind.

The BO being created is the 256 KiB native-ring fallback just before it
(`:207417-207418`): reason 1 (arenas full, 60/60 MiB), refused class 3 =
Encoder (`apple_agx_win32_device_info.h:16-18`). Encoder class means LOW_VA or
encoder intent (`agx_win32_asahi_bo.c:407-410`), i.e. a BO in the 4 GiB USC
window (`apple_agx_g4_submit.h:74-75`, `agx_win32_gpuva.c:91-95`).
`AgxWin32GpuvaBind` fails on a refused VA reserve, a refused map or an
uncertain paging wait (`agx_win32_gpuva.c:86-128`); the `g4-native-reserve-va-cb`
and `g4-native-map-va-cb` rows that would tell which are not in this log.

## 2. Why only the stream run gets there

Receipt totals (whole logs):

| | arm a (stream off, 2499 Executes, 205 s) | arm b (stream on, 542 Executes, 78 s) |
|---|---|---|
| perf "BO cache evict" | 0 | **3125** |
| perf "BO cache miss" | 541 | 3585 |
| `measure-bo-create` (>= 10 ms) | 75 | 852 (533 of them < 1 MiB General) |
| `measure-native-ring-fallback` | 299 | 2439 (886 x 256 KiB, 750 x 64 KiB, 601 x 512 KiB, reason 1) |
| `measure-trim` / `measure-make-resident-trimmed` | 0 / 0 | 2847 / 2497 (none before Execute ~260, then ~9 per Execute) |
| `measure-native-arena-class` occupancy | constant | constant (General 43.75 MiB/175 slots, Encoder 16.25 MiB/41): arenas do not leak |

Per 50 Executes (arm b) fallbacks/creations are flat from the start (about 5
fallbacks per Execute): steady churn, plus residency trimming from Execute 260.

### 2a. Stream outputs churn the BO cache

`agx_win32_emit_indirect_stream` took its outputs from the batch pools
(`agx_win32_indirect_stream_emit.h:196-200` before RC71). Each request,
`MaxCount * record + tail` (about 2.6 MiB) and `MaxCount * usc_stride` (about
7.3 MiB at 128 B), is larger than `POOL_SLAB_SIZE` (256 KiB), so Mesa's
`agx_pool_alloc_aligned_with_bo` creates a dedicated BO each time
(`src/asahi/lib/pool.c:61-68` upstream). `agx_batch_cleanup` releases them
(`agx_pool_cleanup`) into the winsys cache, 64 MiB / 128 entries
(`agx_win32_asahi_bo.h:18-19`), which admits each released BO by evicting the
oldest (`cache_put`, `agx_win32_asahi_bo.c:325-338`). With about 7 stream draws
per Execute this is about 70 MiB per Execute: every Execute flushed the cache,
so the pool slabs, encoders and query BOs (256/512/64 KiB) missed, fell back
past the full native arenas and were created again (AllocateCb, VA reserve,
map, MakeResident paging wait).

### 2b. Every eviction while the GPU is busy leaked the BO

`dispose` destroys the allocation, then frees the VA only if
`AgxWin32GpuvaUnbind` succeeds, which requires no submission in flight at all
(`agx_win32_gpuva.c:130-136`); otherwise the VA goes to `PendingVa[64]`
(`agx_win32_asahi_bo.c:256-270`). Only `AgxWin32AsahiCollect` drained the list,
again only with nothing in flight (`:1026-1030`), and the D3D12 owner calls
Collect only at owner destroy and when a presentation or shared texture is
created, opened or destroyed (`agx_d3d12_windows.cpp:465,1797,1883,1961`). With the stream there are always
submissions in flight (up to `AGX_GPUVA_IN_FLIGHT` = 4), so after 64 deferred
releases the list stayed full and every later `dispose` returned 0 after
re-associating the BO (`:256-260`): allocation, residency reference and VA
stay registered with refcnt 0 until the process ends. Cache evictions ignore
that result (`cache_evict_oldest`, `:312-319`).

Arm b had 3125 evictions, so thousands of BOs (largely 2.6 and 7.3 MiB stream
outputs, half of them in the USC window) stayed allocated. This matches the
budget pressure from Execute ~260 (MakeResident E_OUTOFMEMORY, trims of idle
slots that are exactly these leaked BOs) and the final failure being a bind
in the USC window. Arm a evicted nothing, so it never filled the list.

The RC71 host replay (`tests/test_rc71_pending_va_release.py`) reproduces the
defect with the production `dispose()` and GPU VA space: three submissions
always in flight, 400 releases -> before: 64 released, 0 VAs freed.

## 3. Changes

1. `a55c47f2` Free a released BO's VA once its in-flight submissions retire.
   - `AgxWin32GpuvaNewestFence` / `AgxWin32GpuvaUnbindAfter`
     (`agx_win32_gpuva.c`): a deferred VA records the newest submission in
     flight at release and is freed once that fence is no longer held.
     Submissions retire in order (`AgxWin32GpuvaRetire`) and no later one can
     name a destroyed allocation, so nothing can read the range any more.
   - `pending_va_free_retired`/`pending_va_push` (`agx_win32_asahi_bo.c`):
     every `dispose`, every `agx_bo_create` (`AgxWin32AsahiDrainPendingVa`) and
     Collect drain first; a release refused for a full list sets
     `RefusedRelease` and is retried by the next drain that has room (the
     same refcnt-0 walk as Collect). Virtual-range releases use the same list.
   - After: 400 of 400 destroyed, every VA freed exactly once, never while a
     submission that could read it is in flight, at most 8 pending.
2. `73d97dd5` Reuse GPU indirect stream outputs from the context after their
   batch retires (`agx_win32_stream_scratch.h`, projected by
   `native-asahi-indirect-stream.py`).
   - `ctx->windows_stream_spare[2][16]`: the retired batch's VDM/USC outputs
     go back to the context instead of the winsys cache (at most 16 BOs and
     128 MiB per kind; new BOs sized by the request rounded up to 1 MiB,
     smallest fit first; released in `agx_destroy_context`).
   - `agx_batch_cleanup` runs only after `AgxWin32AsahiBatchPoll(batch, 0)`
     (projected `agx_batch.c:240-241`), so a returned BO is idle.
   - `tests/test_rc71_stream_scratch.py` (projected code, 300 batches of 7
     draws, four in flight): before 4200 BOs created and released to the
     winsys; after 56 (first window), 0 in steady state.

Perf notes `GPU stream scratch reuse` / `GPU stream scratch create` appear in
`measure-perf-reason`.

## 4. Unknown

- Whether the failing bind was the VA reserve (USC window exhausted) or the
  map (memory/commit); the reserve/map callback rows are off in this log.
- The exact leaked volume: there is no live-BO or PendingVa receipt.
- Whether 16 spares per kind cover GravityMark's in-flight window (a miss
  creates a BO and an overflow releases one to the winsys cache, as before,
  but now without leaking).
- Retained spares are never trimmed while the context lives (up to 128 MiB
  per kind, only what the workload used at once).

## 5. Checkpoint

GravityMark D3D12 HD 50k, `APPLE_AGX_D3D12_GPU_INDIRECT_STREAM=1`, full 300 s.
Expect: no `reject-batch`, no `measure-d3d12-first-fault`; perf `BO cache
evict` near arm a (0, against 3125), `GPU stream scratch create` at most about
32 with `GPU stream scratch reuse` for the remaining ~3400 stream draws;
`measure-native-ring-fallback` and `measure-bo-create` near arm a's rate;
`measure-trim` absent or rare. Then compare the score with arm a.
