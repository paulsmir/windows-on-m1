# D3D-G3: occlusion queries and predicates (2026-10-14)

D3D-phase-plan step after MRT (D3D-G2b, EXP1282). Feature level 10_0 requires
`D3D10DDI_QUERY_OCCLUSION` (UINT64 samples passed) and
`D3D10DDI_QUERY_OCCLUSIONPREDICATE` (BOOL). Before this step the KMD refused
every render naming an occlusion heap, and the d3d10umd projection answered
`E_NOTIMPL` for every query type except EVENT.

## Sources inspected

**Asahi kernel**
- `fw/fragment.rs`:
  - `JobParameters1` (G13: work + 0x80) places `isp_oclqry_base` between
    `isp_zls_pixels` (+0xc8) and `zls_ctrl` (+0xd8), so at +0xd0.
  - `JobParameters3` (work + 0x4b8) has the depth-bias and scissor
    `ArrayAddr`s (16 bytes each), then `isp_oclqry_base` at +0x4d8.
- `queue/render.rs:657/821`: both fields are `cmdbuf.isp_oclqry_base`.
  - `:762` is the G14X register array (0x15311), which G13 lacks.
  - Nothing else in the kernel concerns occlusion.

**Asahi Mesa**
- `agx_query.c`:
  - The context's occlusion heap is one `AGX_MAX_OCCLUSION_QUERIES` (32768)
    x u64 WRITEBACK BO.
  - `agx_get_occlusion_heap` returns its VA when the batch uses it.
    `agx_add_query_to_batch` adds the BO to the batch and records the writer
    generation.
  - `begin_query` syncs the writers, then zeroes the slot from the CPU.
  - `get_query_result` syncs the writers and reads the slot; it ignores `wait`
    (TODO).
- `agx_state.c:3555/3693`: per draw, the PPP `FRAGMENT_OCCLUSION_QUERY` index
  and the visibility mode (COUNTING for counters, BOOLEAN otherwise) when
  `active_queries && occlusion_query`.
- `genxml/cmdbuf.xml:526`: the index field is 15 bits, so the hardware reaches
  at most 32768 slots (256 KiB) from the base.
- `agx_pipe.c:1601` passes `agx_get_occlusion_heap(batch)` into the render
  command.

**Captured G13 template:** obj18 (WorkCommand3D) holds 0 at both +0xd0 and
+0x4d8.

**Windows / WDK**
- `d3d10umddi.h`: `D3D10DDI_QUERY_MISCFLAG_PREDICATEHINT` = 1 and
  `D3D10_DDI_GET_DATA_DO_NOT_FLUSH` = 1.
- Stock d3d10umd `Query.cpp`:
  - `TranslateQueryType`.
  - `GetData` returns `DXGI_DDI_ERR_WASSTILLDRAWING` while the result is
    pending.
  - BOOL for predicates, UINT64 for occlusion.

## Contract and ownership

| Layer | Owns |
|---|---|
| Mesa (UMD) | heap allocation, slot index per query, per-draw PPP index and visibility mode, CPU zero at Begin, result read |
| winsys | copies `isp_oclqry_base` into the native render (unchanged); the heap BO is in the batch BO list, hence resident |
| UMD screen | the heap is `SystemDirect` (unshared, at most 16 MiB). CPU and GPU use one allocation with no staging download, so it need not be in the written set |
| KMD parser | the whole 256 KiB from the base must be mapped writable in the process graph, and the base 8-byte aligned |
| KMD builder | `isp_oclqry_base` in JP1 +0xd0 and JP3 +0x4d8 (0 without queries: unchanged firmware input) |
| d3d10umd projection | OCCLUSION and OCCLUSIONPREDICATE map to `PIPE_QUERY_OCCLUSION_COUNTER` and `PIPE_QUERY_OCCLUSION_PREDICATE` (PREDICATEHINT allowed for predicates); EVENT keeps the Windows fence path |
| GetData | calls `get_query_result(wait=false)`. The native Mesa overlay flushes the query's writers, retires completed batches (`agx_cleanup_batches`), and reports not-ready while any writer is pending (`WASSTILLDRAWING`). It submits under DONOTFLUSH too, so a polling caller always finishes. A failed submission is reported (`AgxD3d10WindowsFlushStatus`) |

## Differences from upstream

- Upstream `get_query_result` blocks regardless of `wait`; the Windows
  projection honours it, because D3D GetData polls.
- Stock d3d10umd passes `wait = DO_NOT_FLUSH`, which is inverted. The
  projection always polls.

## Known gaps (not in this step)

- `SetPredication` stays `E_NOTIMPL` (predicated rendering).
- `set_active_query_state` is still the Windows no-op, so u_blitter draws
  (copies) inside an active query would be counted.
- Timestamp, pipeline-statistics and SO-statistics queries remain fail-closed.
- Mesa supports one active occlusion query per context. D3D allows
  overlapping occlusion queries, which this projection does not support.

## Offline verification

- `tests/apple_agx_g4_submit_test.c`:
  - the parser asks for exactly `APPLE_AGX_G4_OCCLUSION_HEAP_BYTES`, writable,
    at the base;
  - it refuses a misaligned base and a heap that crosses the 39-bit VA top.
  - Before the change it asked for one byte.
- `tests/apple_agx_g4_builder_test.c`:
  - an aligned heap binds and a misaligned one does not;
  - both JP fields carry the heap, and both are 0 without one.
- The overlay was prepared locally against the pinned Mesa reference:
  `Query.cpp` and `State.h` were transformed, and the `agx_query.c` anchor is
  unique.

## Hardware checkpoint

EXP1283 runs `agx_query_smoke` (x86 WOW64 UMD32 and ARM64 UMD). Each frame has:
- four occlusion queries with distinct exact pixel counts;
- a hidden predicate (FALSE) and a visible predicate (TRUE).

The whole EXP1282 regression set runs as well.

Recovery: rec1283h.
