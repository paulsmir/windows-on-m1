# RC66: the small-slot QUERY escapes of EXP1986

Evidence: `.local/experiments/EXP1986-small-arenas/evidence/EXP1986/gm1986{a,b}-umd.log`,
run with `APPLE_AGX_D3D12_CPU_POOL_MAP=0` (gm-d3d12a-1986.ps1), so every CPU-facing BO
is either a native staging slice or write-watched private staging (heap staging).
Paths below are relative to `drivers/apple-agx/`.

## Per-escape cost (steady state, last three quarters of the Executes, medians)

| class | HD count / ms / us each | FHD count / ms / us each |
|---|---|---|
| QUERY <= 64 KiB | 24 / 11.4 / 474 | 51 / 29.5 / 578 |
| QUERY <= 1 MiB | 13 / 16.8 / 1291 | 5 / 4.2 / 833 |
| QUERY > 16 MiB | 1 / 1.9 / 1945 | 1 / 1.7 / 1660 |
| UPLOAD <= 64 KiB | 13 / 0.7 / 53 | 39 / 1.2 / 31 |
| UPLOAD <= 1 MiB | 16 / 1.7 / 105 | 6 / 0.7 / 118 |
| DOWNLOAD <= 1 MiB (both classes) | 15 / 1.35 / 90 | 15 / 1.35 / 90 |

An UPLOAD moves up to 64 KiB through the buffered escape both ways and costs 30-120 us.
A QUERY moves no data and costs 0.5-1.9 ms. The QUERY is the first escape of a slot
transfer; the UPLOAD or DOWNLOAD of the same slot follows immediately.

## Why a QUERY costs ~0.7 ms: it waits for this process's GPU job

`render-admission/src/gpuva_g3_windows.c:1728-1745` loops until
`state->ActiveProcess != p && !p->Graph.JobInFlight && !p->Graph.LeaseToken`; only the
paging-quiescence term is skipped for a QUERY (`:1738`). The wait ends on `JobEvent`, set
by `AdmissionGpuvaG3CompleteJobTimestamp` (`:3623`, `:3638`); BeginJob sets
`ActiveProcess` (`:3434`). Predicates 42-44 (`:1772-1774`) refuse every operation,
QUERY included, while a job runs.

The UMD returns from a submission before the job completes when no download is due
(`render-admission/umd/src/umd_gpuva_windows.c:2077-2085`), and the next submission
uploads its dirty held slots first (`:2334` -> `transfer_held` `:1724` ->
`transfer_heap_slot` `:1321`, QUERY at `:1344`). That first escape therefore waits until
the previous job (and any queued job that begins meanwhile) has ended: it drains the GPU.
The UPLOAD after it finds no job in flight, which is why it is cheap. Each Execute had 26
submissions and 37-56 QUERY escapes, so most submissions with a dirty fallback slot
paid one drain.

Consequences for the candidate fixes:

- (a) caching the QUERY result, (b) batching QUERYs, (c) a non-waiting QUERY: the UPLOAD
  or DOWNLOAD that follows waits on the same three conditions (plus paging quiescence),
  so the drain moves to it. Saving: only the QUERY's own escape, ~30-60 us each,
  1-3 ms per Execute. Not done.
- Making UPLOAD not wait for this process's job would remove the drain for every class,
  but relaxes a KMD gate (EXP871/R157, EXP1002) whose data-hazard role is now carried by
  the UMD (`umd_gpuva_windows.c:1334`, `LastUseFence` when `SubmissionHolds > 1`).
  Proposed for a separate, hardware-gated change; not committed.
- (d) no escape at all for the hot small BOs: chosen.

## Which BOs

- QUERY <= 64 KiB, about 22 per Execute at HD: the D3D12 pipeline-statistics queries.
  `mesa/winsys/agx_d3d12_windows.cpp:2108` creates 11 `PIPE_QUERY_PIPELINE_STATISTICS_SINGLE`
  queries per index (`:2060`); Asahi gives each non-occlusion query its own BO
  (`agx_query.c:166`, 16 bytes), rounded to 64 KiB on Windows
  (`mesa/winsys/agx_win32_asahi_bo.c:400`). Begin writes the BO on the CPU (UPLOAD), the
  GPU writes it, and the persistent CPU map makes it download-due (`umd_gpuva_windows.c:1718`).
  The query heaps are type 2 (pipeline statistics), 3 x 1024 entries.
- QUERY <= 1 MiB: the per-batch encoder BO (`agx_batch.c:83`, 512 KiB) and pool slabs,
  written by the CPU every batch.
- These BOs are recycled through the winsys BO cache with their storage
  (`agx_win32_asahi_bo.c:325`, `:339`; ~120 hits per Execute, 15-39 evictions per run), so a BO
  that fell back once keeps paying escapes for the whole run. 94 (HD) and 142 (FHD)
  64 KiB creations fell back (reason 1), most during scene load (first at Execute 54).

## What filled the 60 MiB

The arenas filled during scene load (50 MiB used by Execute 35, 59 MiB by Execute 54).
No receipt names the occupants. Each Asahi compiled or linked shader owns a BO
(`agx_state.c:1484`, `agx_linker.c:123`), 64 KiB on Windows; all 244 shaders and 244 PSOs
of the HD run were created before the first reason-1 refusal. Shader-class BOs
(`agx_win32_asahi_bo.c:408`) are CPU-written once, so a native slice saves them only their
first upload: a clean write watch returns before the QUERY (`umd_gpuva_windows.c:1332`).

## Change

- `umd/include/umd_native_arena.h:48-51`, `umd/src/umd_win32_screen.c:1012`: Shader-class BOs
  never reserve a native slice; the fallback receipt says reason 6. With canonical CPU maps
  on (default) they remain SystemDirect (`umd_win32_screen.c:939`, unchanged); with
  `CPU_POOL_MAP=0` they use write-watched private staging (`:1088`).
- `umd/src/umd_win32_screen.c:756`, `:1029`: `measure-native-arena-class`
  {1, reason, refused class, native bytes of General/Shader/Encoder, their slot counts}
  after each reason 1 or 5 refusal, so the next run shows what still holds the budget.

Invariant: the native budget (rc62: at most 60 MiB, at most 16 MiB per BO, 28 MiB reserved
for slices up to 1 MiB) is unchanged; only which BOs may take it.

## Separate lead found in the same logs

`measure-perf-reason` counts "Input assembly counters" 2.18 M times at HD (about 1020 per
Execute): with a pipeline-statistics query active, Asahi dispatches `libagx_increment_ia`
with `AGX_BARRIER_ALL` before every draw (`agx_state.c:3864-3876`). That GPU cost is not
addressed here.
