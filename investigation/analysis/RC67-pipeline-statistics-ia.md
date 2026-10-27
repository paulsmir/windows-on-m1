# RC67: pipeline-statistics IA updates of GravityMark D3D12 (EXP1986)

Evidence: `.local/experiments/EXP1986-small-arenas/evidence/EXP1986/gm1986a-umd.log` (HD 50k).
Upstream paths are under `.local/reference/mesa/src`; repository paths under `drivers/apple-agx/`.

## Where the 2.18 M updates come from

| receipt (whole run, 2136 Executes) | count | per Execute |
|---|---|---|
| perf reason "Input assembly counters" (no restart variant) | 2,184,311 | ~1020 |
| D3D12 draws (`measure-d3d12-owner-time` word 0, median 13) | 27,355 | 13 |
| counted ExecuteIndirect, `measure-d3d12-counted-path` {2, 60000, ..., 7} | 14,566 | ~6.8 |
| dispatch ExecuteIndirect (kind 3, MaxCount 1) | 153,964 | ~72 (no IA) |

Every counted draw takes path 2, reason 7 (`mesa/winsys/agx_win32_indirect_stream_emit.h:45`:
the GPU stream is opt-in). The wrapper reads the count on the CPU and calls `draw_vbo` with
`draw_count = min(count, MaxCount)` (`mesa/winsys/agx_win32_counted_indirect.h:59`). The native
body unrolls that call into one draw per record (`gallium/drivers/asahi/agx_state.c:4881`), and
each unrolled draw runs the IA gate (`agx_state.c:4930-4953`). So ~1007 of the ~1020 updates per
Execute belong to ~7 counted calls (~150 records each); the 13 D3D12 draws contribute ~13.

## What one update costs

- **CDM dispatch and barrier.** `agx_ia_update` (`agx_state.c:3826-3876`) uploads a draw
  descriptor (direct draws), registers five query writers and calls `libagx_increment_ia`
  (`:3874`): `agx_launch_precomp` (`:3030-3050`) allocates USC words in the pipeline pool and
  uploads the arguments to the batch pool, then `agx_launch_internal` encodes the launch and an
  unconditional CDM barrier (`:3009`). The `AGX_BARRIER_ALL` argument itself is ignored by the
  Gallium precomp launcher; the barrier after every launch is what serializes the one-invocation
  kernels. All of it is the batch's compute command, which the render command waits for (D3D-G1:
  `agx_batch_submit` render barrier on the compute command), so ~1020 serialized dispatches run
  before each frame's large render batches start.
- **Batch split.** None per draw. A render batch is flushed only when its CDM encoder is
  nearly full (`agx_state.c:3024-3025`, "CDM overfull"). A G13 direct launch plus barrier is
  36 bytes (`agx_pack.h` CDM lengths; at most 52 with the indirect and G14X words) and the
  encoder is 0x80000 bytes (`agx_batch.c:83`), so a split needs over 10,000 updates in one
  batch; EXP1986 has ~1020 per Execute over ~1.8 batches with 100+ draws, and no
  "CDM overfull" perf reason was recorded.
- **Extra BOs.** None per draw. The 11 `PIPE_QUERY_PIPELINE_STATISTICS_SINGLE` queries per
  D3D12 index (`mesa/winsys/agx_d3d12_windows.cpp:2108`) each own a 16-byte BO
  (`agx_query.c:166`), created once per index; a draw only re-adds them to the batch list.
  Their escape cost is RC66's subject and is unchanged here.

## Change

`mesa/winsys/agx_win32_query_ia.h`, projected by `mesa/scripts/native-asahi-query-ia.py`:
before the native unroll, one `libagx_win32_query_ia_records` dispatch
(`mesa/winsys/agx_win32_query_ia.cl`) counts every record of the call, and the unrolled draws
skip their own update (`ctx->windows_query_ia_records` in the native gate).

Invariants:

- The kernel reads record i at `offset + i * stride`, the address unrolled draw i reads. The
  count is the CPU-known `draw_count`, never more.
- Same arithmetic as native `poly_increment_ia` (`poly/geometry.h:587-600`) for non-patch draws:
  vertices = r[0] * r[1] into IA vertices and VS invocations; primitives =
  `u_decomposed_prims_for_vertices(mode, r[0]) * r[1]` into IA primitives and the clipper
  counters; low 32 bits; atomic because the records run in parallel.
- Same query state: the same writer registration (`agx_get_query_address`), and the same
  clipper rule (no clipper counting with a GS or TES bound). Absent queries get no address.
- The dispatch sits at the CDM position of the first native update and reads the arguments
  through `agx_indirect_buffer_ptr`, so its ordering and hazards are the native ones.
- Native per-draw counting stays for: primitive restart (index scan), patches, a passthrough GS
  (counts recursively with the GS bound), a render condition (evaluated per unrolled draw), the
  GPU stream (counts in its own kernel), GPU counts, single draws and malformed record bounds.

Result per GravityMark HD Execute: ~1020 IA dispatches + CDM barriers become ~7 batched
dispatches + ~13 native ones. RC57 statistics probes use the GPU stream path or single draws,
both unchanged.

## Not changed

- One `PIPELINE_STATISTICS` query or a shared BO for the 11 single queries: a CPU Begin zeroes
  each query slot after syncing only that query's writers (`agx_query.c:290`). With a shared
  BO the Windows transport uploads the BO as a whole, which can overwrite another query's
  in-flight GPU result. Needs its own design; RC66 owns these escapes.
- CPU arithmetic for direct draws: ~13 per Execute, not worth a second mechanism.
- Counting only the counters the app reads: D3D12 resolves all 11 values.
