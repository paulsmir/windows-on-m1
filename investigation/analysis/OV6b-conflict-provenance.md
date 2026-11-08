# OV6b: conflict provenance for the OV5 vertex-stage verdict (spec for fork 6, 2026-11-08)

Class: D (receipt-only, zero cost when the OV2 feature is off). Owner of the analysis: GM session; implementer: fork 6.

## Question this answers
EXP2275: with finite ranges (OV6) R2/R4/R6 (fork numbering) report no-prelude verdict 10 = a vertex-stage range of the
render overlaps a range of the preceding compute batch with a write on either side. Is that a real data dependency
(the VS reads what the compute wrote: then Apple's c15||R16 overlap is not available to us the same way) or an
attribution artefact (a vertex-stage UAV *binding* counted as a write, a root descriptor bounded to a whole resource
while the compute wrote another sub-range, the compute batch's driver work such as query copies)?

## WINDOWS CONTRACT
D3D12 orders work only by ResourceBarrier on the resources both sides touch; the verdict's inputs are the application's
bindings and the shader's declared registers (mapped root), so the receipt must name, per conflict, the binding kind on
both sides and whether the render side is a declared UAV write or a read.

## TRANSLATION (what to implement)

### 1. Provenance bits on job ranges (`drivers/apple-agx/d3d12/agx_d3d12_job_barriers.h`)
`AGX_D3D12_JOB_RANGE.Uav` already carries bit 0 (may write), bit 2 (`AGX_D3D12_JOB_VERTEX`). Add source bits, defined once:
```
#define AGX_D3D12_JOB_SRC_VB       0x008u  /* IASetVertexBuffers */
#define AGX_D3D12_JOB_SRC_IB       0x010u  /* IASetIndexBuffer */
#define AGX_D3D12_JOB_SRC_IND_ARGS 0x020u  /* ExecuteIndirect argument buffer */
#define AGX_D3D12_JOB_SRC_IND_CNT  0x040u  /* ExecuteIndirect count buffer */
#define AGX_D3D12_JOB_SRC_VIEW     0x080u  /* descriptor (table/heap) view: CBV/SRV/UAV */
#define AGX_D3D12_JOB_SRC_ROOT     0x100u  /* root descriptor CBV/SRV/UAV */
#define AGX_D3D12_JOB_SRC_SO       0x200u  /* stream-output target / filled-size */
#define AGX_D3D12_JOB_SRC_CLEAR    0x400u  /* ClearRenderTargetView/DepthStencil/UAV */
#define AGX_D3D12_JOB_SRC_TARGET   0x800u  /* RTV/DSV bound for the draw */
#define AGX_D3D12_JOB_SRC_MASK     0xff8u
```
`AgxD3d12JobRangeAdd` merges entries with equal `Uav` only, so provenance fragments entries by kind: that is intended
(128 entries, EXP2275 showed no overflow). The verdicts (`AgxD3d12JobRenderTaRefusal`, `AgxD3d12JobRangeFirstOrdering`)
must mask provenance out where they compare kinds: they test `Uav & 1u` and `Uav & AGX_D3D12_JOB_VERTEX` only -- verify
each use with `grep -n "Uav" agx_d3d12_job_barriers.h` and keep the tests GREEN.

### 2. Emit the bits at the sources (`agx_d3d12_ddi.cpp`)
- `JobStageRanges`: descriptor views -> `| AGX_D3D12_JOB_SRC_VIEW`; root heaps (`*_heap[]/offset`) -> `| AGX_D3D12_JOB_SRC_ROOT`.
- Draw ranges (the block ending in `JobStageRanges(sink, d.vs_bind, vertex)`): VB -> SRC_VB, IB -> SRC_IB,
  `JobIndirectRanges` -> SRC_IND_ARGS / SRC_IND_CNT (two calls or a flag pair), SO -> SRC_SO, clears -> SRC_CLEAR,
  RTV/DSV -> SRC_TARGET. The GS/HS/DS `sink(nullptr, ...)` stays (no-heap access).
- Dispatch ranges (`ReplayDispatch`): same VIEW/ROOT/IND bits.

### 3. The first conflicting pair (`agx_d3d12_job_barriers.h`)
```
/* OV6b: the pair behind verdict 10 (render range x with AGX_D3D12_JOB_VERTEX, compute range y of the same heap,
 * overlapping, with (y->Uav & 1) || (x->Uav & 1)); FALSE when the verdict is not 10 */
static inline unsigned AgxD3d12JobRenderTaConflict(const AGX_D3D12_JOB_BARRIERS *j,
    AGX_D3D12_JOB_RANGE *render, AGX_D3D12_JOB_RANGE *compute);
```
Same loop as the 10 branch of `AgxD3d12JobRenderTaRefusal`, returning the first pair.

### 4. Receipt (`agx_d3d12_windows.cpp`, in the `kind == 1u && AgxWin32Ov2Feature && PreviousKind == 2u` branch)
When `AgxD3d12JobRenderTaRefusal(&owner_job_barriers, 0u) == 10u` and fewer than 64 executions have emitted (static
counter, reset never), emit once per render:
`measure-d3d12-ov2-conflict`, 12 values: [render index in this execution (owner_ov5_renders), x.Heap id
(AgxD3d12JobBarrierId), x.Lo lo32, x.Lo hi32, x.Hi lo32, x.Hi hi32, x.Uav, y.Heap id, y.Lo lo32, y.Hi lo32, y.Hi hi32, y.Uav].
(y.Lo hi32 omitted: heap offsets fit 32 bits on this platform but keep Lo/Hi of x complete; cap is 32 values.)

### 5. Decoder (`tools/agx_ov6_conflict_decode.py`)
Histogram per render index: (x kinds, x write?, y kinds, y write?, same heap, overlap bytes) with the SRC names; print the
top 5 per render.

### 6. Tests
- Extend `tests/test_ov6_exact_ranges.py` (or a new `test_ov6b_conflict_provenance.py`): host-compiled scenario -- a render
  with a VB range (SRC_VB|VERTEX) overlapping a compute UAV write (SRC_ROOT|1) returns 10 and the pair with those exact
  flags; a vertex-stage UAV binding (x.Uav & 1) against a compute *read* also returns 10 (document it: that is the
  attribution case the receipt must expose); provenance bits never change verdicts 0/9/11/12 (compare against the
  pre-change results of the existing SETS scenario).
- Source contracts: every `sink(` / `JobHeapRange(` / `JobDescriptorRange(` call in the draw/dispatch range blocks carries
  a SRC bit (regex), `AGX_D3D12_JOB_SRC_MASK` masked wherever `Uav` is compared for merging in `AgxD3d12JobRangeAdd`?
  -- NO: merging stays exact-equality (fragmentation intended); assert that the verdict functions do not read SRC bits.
- Gate: `tests/run_against_baseline.py` (3 known test_g4_live_* failures only), builder syncheck NATIVE_OWNER rc=0
  (the NONE variant fails at HEAD on AgxD3d12BarrierCount; refresh render-admission/include, umd/include, shared/include,
  mesa/dxil headers into the syncheck tree first, as I did on 2026-11-08).

## Hardware step (one run, receipt-only)
Same shape as EXP2275: package from the new HEAD, Mu MUANSCPPC002, switches Wom1JobDiagnostics=1 Wom1Ov2NoHazardLead=1,
steps suite + d HD PERF; decode `measure-d3d12-ov2-conflict` rows. Control for perf: none needed (receipt-only).

## Verdict rules (I read them)
- R6 pair = (VERTEX | SRC_ROOT or SRC_VIEW read) vs (compute SRC_ROOT|1 write), same heap, overlap inside one placed
  resource -> real resource-level dependency: the VS reads what C5 wrote. Then the overlap lever for R6 is only sub-resource
  tracking (byte ranges already exact -> the app really aliases) -> R6 closed; focus on R8 + prelude.
- x.Uav & 1 with a VS UAV declared but the compute side a read -> attribution: fix = treat a vertex-stage UAV binding as a
  write only if the VS actually stores (needs the shader's store usage; possible from the NIR info) -> OV6c.
- Pair with SRC_IND_ARGS/SRC_IND_CNT -> the arguments come from the compute: real, and it also explains the prelude
  (expansion must wait for C). Then only the prelude placement line remains.
- Pair with SRC_TARGET/SRC_CLEAR on the vertex side -> a classification bug in the DDI (targets are not vertex accesses).
