#ifndef AGX_D3D12_JOB_BARRIERS_H
#define AGX_D3D12_JOB_BARRIERS_H

/* OV1 receipt-only (investigation/analysis/OV1-render-compute-overlap.md):
 * which D3D12 ResourceBarrier calls the application recorded between two
 * consecutive GPU jobs, to decide whether the KMD may overlap a compute job
 * with the following render job. Every barrier stays a no-op.
 *
 * The replay feeds the barriers recorded before each command (Add), marks the
 * owner work of each command (Begin ... End) and every submitted Mesa batch
 * takes one summary (Batch, receipt measure-d3d12-job-barriers). Mesa flushes
 * a batch lazily: a hazard or a pass change submits the earlier batch while
 * the first command of the next job is replayed. A submission inside a
 * command's owner work therefore reports the barriers between the previous
 * work command and that command (the job boundary); one between commands
 * (upload boundary, the replay's end) reports those since the last work
 * command. Each set is reported once; a gap no submission took was inside a
 * batch and only counts in Total.
 *
 * Barrier types and flags as d3d12umddi.h (agx_d3d12_barrier_stats.h).
 * Plain C: also host-tested. */
#define AGX_D3D12_JOB_BARRIER_IDS 32u
#define AGX_D3D12_JOB_BARRIER_VALUES 13u

typedef struct AGX_D3D12_JOB_BARRIER_SET {
  unsigned Count, Transitions, Uav, GlobalUav, Aliasing;
  unsigned NonUav; /* OV2: every barrier but a UAV barrier */
  unsigned StateBefore, StateAfter, Flags;
  unsigned Ids, IdHash, IdOverflow;
  unsigned Id[AGX_D3D12_JOB_BARRIER_IDS];
} AGX_D3D12_JOB_BARRIER_SET;

/* OV2 ranges (EXP2199: C3/C5/C7 refused only because their render bound a
 * UAV): a heap byte range [Lo, Hi) a render accessed (Uav: through a UAV
 * view) or a barrier named (Uav 1 UAV barrier, 2 global UAV barrier, 0 any
 * other barrier). Heap is an opaque AgxD3d12Heap identity; NULL = unknown. */
/* OV6 (EXP2274: GravityMark renders refused with 11 = unplaceable): 128
 * entries so a frame's draws with their views do not fill the set; Overflow
 * is a bit set, 1 = the set was full, 2 = an access had no heap (GS/HS/DS,
 * an unplaceable clear), so the receipt can tell them apart. */
#define AGX_D3D12_JOB_RANGES 128u
#define AGX_D3D12_JOB_RANGE_FULL 1u
#define AGX_D3D12_JOB_RANGE_NULL 2u
typedef struct AGX_D3D12_JOB_RANGE {
  const void *Heap;
  unsigned long long Lo, Hi;
  unsigned Uav;
} AGX_D3D12_JOB_RANGE;
typedef struct AGX_D3D12_JOB_RANGE_SET {
  unsigned Count, Overflow, Uav;
  AGX_D3D12_JOB_RANGE R[AGX_D3D12_JOB_RANGES];
  unsigned At[AGX_D3D12_JOB_RANGES]; /* Post: dispatches ended before the entry */
} AGX_D3D12_JOB_RANGE_SET;
/* OV2 cross-execution (receipt, R9 B.4): what survives ExecuteCommandLists
 * for the execution-first compute batch. Kept 0 = the previous execution of
 * this owner ended with a render whose accesses (KeptRender) and the
 * barriers since its last draw (KeptPost) are fully known; else the refusal
 * reason (1, 2, 6, 11) with KeptDetail (bits 0-3 sub-cause, 4-7 break cause).
 * Render collects the accesses of every draw ended since the last render
 * submission that left no other render batch open (a superset of the batch
 * being submitted, exact when one batch is open); Last is its value at the
 * last render submission; Tail the barriers since that render's last draw;
 * Pre the barriers of this execution before its first batch, PreCommitted
 * of them before the last dispatch ended. Foreign counts the non-draw,
 * non-dispatch commands like ForeignSince, but is only reset by such a
 * render submission. Table*: accesses through a descriptor-heap table,
 * which the range sinks do not see. */
/* N4c-v2 (OV2 cross lead): Copies counts the buffer copies of this
 * execution before its first batch whose ranges are in Copy (write = Uav 1
 * the destination, 0 the source); they are in Foreign too. CopyActive: the
 * active command is such a copy. */
/* N4c-v3 (EXP2323 x0: every execution kept 6/2 for one ResolveQueryData
 * whose own writer flush submitted R5): Placed counts the commands in
 * Foreign whose only work is a placed resolve copy into the compute batch
 * (AgxD3d12JobCrossPlace), LastPlaced its value at the last render
 * submission, PlacedActive: the active command is such a resolve, PlacedDst
 * its destination (staged, merged into Render at its end). TableDrawSet /
 * TableDispatchSet: the active draw's / dispatch's descriptor-table
 * accesses (staged, merged at its end into Render / Compute); Compute: the
 * table accesses of the open compute batch. The same-execution sets
 * (RangeDraw, RangeRender, RangeDispatch, RangeCompute) never hold them.
 * ShadowRender / ShadowPost: Last and Tail at the last end even when the
 * kept verdict refused, KeptMask the kept-side conditions of that end
 * (AgxD3d12JobCrossMask bits 0-8) and KeptLast* LastForeign, LastPlaced
 * and LastForeignKinds then (receipt only). */
#define AGX_D3D12_JOB_RANGE_TABLE 4u
#define AGX_D3D12_JOB_KIND_COPY 1u
typedef struct AGX_D3D12_JOB_CROSS {
  unsigned InExecution, Broken, Batches, LastKind, Dirty;
  unsigned Foreign, ForeignKinds, LastForeign, LastForeignKinds;
  unsigned TableDraw, TableDispatch, TableCompute;
  unsigned Kept, KeptDetail, KeptKinds;
  unsigned PreCommitted, PreCommittedOverflow;
  unsigned Copies, CopyActive;
  unsigned Placed, LastPlaced, PlacedActive;
  unsigned KeptMask, KeptLastForeign, KeptLastPlaced, KeptLastForeignKinds;
  AGX_D3D12_JOB_RANGE_SET Render, Last, Tail, Pre, KeptRender, KeptPost, Copy;
  AGX_D3D12_JOB_RANGE_SET PlacedDst, TableDrawSet, TableDispatchSet, Compute, ShadowRender, ShadowPost;
} AGX_D3D12_JOB_CROSS;
typedef struct AGX_D3D12_JOB_BARRIERS {
  AGX_D3D12_JOB_BARRIER_SET Before; /* the gap before the active command */
  AGX_D3D12_JOB_BARRIER_SET Open;   /* since the last command's Begin */
  unsigned Active;                  /* command kind in owner work, 0 none */
  unsigned Total;                   /* every barrier since the last batch */
  unsigned PreviousKind;
  /* OV2 (AgxD3d12JobBarrierIndependent): transitions/aliasing/ranged since
   * the last batch, the gap after the last batch when it was a render (else
   * ~0u), UAV-binding draws (running count and its values at the last two
   * batches), whether the last render may have bound a UAV, and whether this
   * ExecuteCommandLists' barriers are incompletely known. */
  unsigned NonUavSince, GapAfterRender, UavDraws, UavDrawsBase[2], RenderUav, Unknown;
  /* OV2: owner work other than a dispatch ran at or after the last batch's
   * submission (e.g. ResolveQueryData: Asahi flushes the query's render
   * writer and copies the result in the next compute batch, which then
   * depends on that render); dispatches recorded but still unsubmitted when
   * a render was submitted (the next compute batch would hold work from
   * before that render). */
  unsigned ForeignSince, DispatchesPending, EarlyDispatch;
  /* OV3: the active non-dispatch command adds no work to the next compute
   * batch (a resolve deferred into a later render's prelude) */
  unsigned ActiveLocal;
  /* OV2 ranges: the active draw's accesses, the open render batch's, the
   * last submitted render's; the barriers since the last draw ended (Post),
   * of which PostCommitted came before the last dispatch ended. */
  AGX_D3D12_JOB_RANGE_SET RangeDraw, RangeRender, RangeLast, Post;
  /* resolve destinations of this execution: OV3 may copy into any later
   * render's prelude, so every render after them counts as writing them */
  AGX_D3D12_JOB_RANGE_SET Resolved;
  unsigned PostCommitted, PostCommittedOverflow;
  /* EXP2200: driver work put into the open compute batch (a drained
   * resolve) keeps that batch foreign until it is submitted, even across a
   * render batch submitted before it; the command kinds (bit kind) and
   * driver work (bit 31) since the last render batch; dispatches ended
   * since the last batch (OV4 prefix receipt) */
  unsigned ComputeForeign, ForeignKinds, DispatchesSince;
  /* OV5 (receipt): the active dispatch's accesses, the open compute
   * batch's, the last submitted compute batch's; a global UAV barrier since
   * the last dispatch ended. Render ranges carry AGX_D3D12_JOB_VERTEX on
   * vertex-stage accesses (VB, IB, indirect, SO, VS views). */
  AGX_D3D12_JOB_RANGE_SET RangeDispatch, RangeCompute, RangeLastCompute;
  /* OV8 (receipt): every barrier range since the last dispatch ended */
  AGX_D3D12_JOB_RANGE_SET BarriersSinceDispatch;
  /* OV8 1b: draws ended since the last dispatch (0 = still before the first draw);
   * the most recent barrier's kind, resource id and states, and those of the
   * first barrier that orders a compute write */
  unsigned DrawsSinceDispatch, LastBarrierType, LastBarrierId, LastBarrierBefore, LastBarrierAfter;
  unsigned GlobalUavSinceDispatch;
  AGX_D3D12_JOB_CROSS Cross;
} AGX_D3D12_JOB_BARRIERS;
#define AGX_D3D12_JOB_VERTEX 4u
/* OV6b (receipt only): where a range came from; verdicts read bits 0 and 2 only */
#define AGX_D3D12_JOB_SRC_VB 0x008u
#define AGX_D3D12_JOB_SRC_IB 0x010u
#define AGX_D3D12_JOB_SRC_IND_ARGS 0x020u
#define AGX_D3D12_JOB_SRC_IND_CNT 0x040u
#define AGX_D3D12_JOB_SRC_VIEW 0x080u
#define AGX_D3D12_JOB_SRC_ROOT 0x100u
#define AGX_D3D12_JOB_SRC_SO 0x200u
#define AGX_D3D12_JOB_SRC_CLEAR 0x400u
#define AGX_D3D12_JOB_SRC_TARGET 0x800u
#define AGX_D3D12_JOB_SRC_MASK 0xff8u
static inline void AgxD3d12JobRangeClear(AGX_D3D12_JOB_RANGE_SET *s) { s->Count = s->Overflow = s->Uav = 0u; }
/* append [lo, hi) of heap (merged into an entry of the same heap and kind
 * it overlaps or touches); NULL heap or a full set -> Overflow */
static inline void AgxD3d12JobRangeAdd(AGX_D3D12_JOB_RANGE_SET *s, const void *heap, unsigned long long lo,
                                       unsigned long long hi, unsigned uav) {
  unsigned i;
  if (uav & 1u) s->Uav = 1u;
  if (!heap) {
    s->Overflow |= AGX_D3D12_JOB_RANGE_NULL;
    return;
  }
  if (hi < lo) hi = ~0ull;
  for (i = 0; i < s->Count; ++i) {
    AGX_D3D12_JOB_RANGE *r = &s->R[i];
    if (r->Heap == heap && r->Uav == uav && lo <= r->Hi && r->Lo <= hi) {
      if (lo < r->Lo) r->Lo = lo;
      if (hi > r->Hi) r->Hi = hi;
      return;
    }
  }
  if (s->Count >= AGX_D3D12_JOB_RANGES) {
    s->Overflow |= AGX_D3D12_JOB_RANGE_FULL;
    return;
  }
  s->R[s->Count].Heap = heap;
  s->R[s->Count].Lo = lo;
  s->R[s->Count].Hi = hi;
  s->R[s->Count].Uav = uav;
  ++s->Count;
}
static inline void AgxD3d12JobRangeMerge(AGX_D3D12_JOB_RANGE_SET *to, const AGX_D3D12_JOB_RANGE_SET *from) {
  unsigned i;
  to->Overflow |= from->Overflow;
  if (from->Uav) to->Uav = 1u;
  for (i = 0; i < from->Count; ++i)
    AgxD3d12JobRangeAdd(to, from->R[i].Heap, from->R[i].Lo, from->R[i].Hi, from->R[i].Uav);
}
static inline void AgxD3d12JobRangesReset(AGX_D3D12_JOB_BARRIERS *j) {
  AgxD3d12JobRangeClear(&j->RangeDraw);
  AgxD3d12JobRangeClear(&j->RangeRender);
  AgxD3d12JobRangeClear(&j->RangeLast);
  AgxD3d12JobRangeClear(&j->Post);
  AgxD3d12JobRangeClear(&j->Resolved);
  AgxD3d12JobRangeClear(&j->RangeDispatch);
  AgxD3d12JobRangeClear(&j->RangeCompute);
  AgxD3d12JobRangeClear(&j->Cross.Compute);
  AgxD3d12JobRangeClear(&j->RangeLastCompute);
  AgxD3d12JobRangeClear(&j->BarriersSinceDispatch);
  j->RangeLastCompute.Overflow = 1u; /* no compute batch yet */
  j->GlobalUavSinceDispatch = 0u;
  j->RangeLast.Overflow = 1u; /* no render yet */
  j->PostCommitted = j->PostCommittedOverflow = 0u;
}

/* a 32-bit identity of a D3D12DDI_HRESOURCE pDrvPrivate value; 0 = null */
static inline unsigned AgxD3d12JobBarrierId(unsigned long long pointer) {
  unsigned h;
  if (!pointer) return 0u;
  h = (unsigned)(pointer ^ (pointer >> 32)) * 2654435761u;
  return h ? h : 1u;
}

static inline void AgxD3d12JobBarrierSetClear(AGX_D3D12_JOB_BARRIER_SET *s) {
  unsigned i;
  s->Count = s->Transitions = s->Uav = s->GlobalUav = s->Aliasing = s->NonUav = 0u;
  s->StateBefore = s->StateAfter = s->Flags = 0u;
  s->Ids = s->IdHash = s->IdOverflow = 0u;
  for (i = 0; i < AGX_D3D12_JOB_BARRIER_IDS; ++i) s->Id[i] = 0u;
}

static inline void AgxD3d12JobBarriersInit(AGX_D3D12_JOB_BARRIERS *j) {
  AgxD3d12JobBarrierSetClear(&j->Before);
  AgxD3d12JobBarrierSetClear(&j->Open);
  j->Active = j->Total = j->PreviousKind = 0u;
  j->DrawsSinceDispatch = j->LastBarrierType = j->LastBarrierId = j->LastBarrierBefore = j->LastBarrierAfter = 0u;
  j->NonUavSince = 0u;
  j->GapAfterRender = ~0u;
  j->UavDraws = j->UavDrawsBase[0] = j->UavDrawsBase[1] = 0u;
  j->RenderUav = 1u;
  j->Unknown = 1u;
  j->ForeignSince = j->DispatchesPending = 0u;
  j->EarlyDispatch = 1u;
  j->ActiveLocal = 0u;
  j->ComputeForeign = j->ForeignKinds = j->DispatchesSince = 0u;
  AgxD3d12JobRangesReset(j);
  j->Cross.InExecution = j->Cross.Broken = j->Cross.Batches = j->Cross.LastKind = j->Cross.Dirty = 0u;
  j->Cross.Foreign = j->Cross.ForeignKinds = j->Cross.LastForeign = j->Cross.LastForeignKinds = 0u;
  j->Cross.TableDraw = j->Cross.TableDispatch = j->Cross.TableCompute = 0u;
  j->Cross.Kept = 2u; /* no previous execution */
  j->Cross.KeptDetail = 1u;
  j->Cross.KeptKinds = 0u;
  j->Cross.PreCommitted = j->Cross.PreCommittedOverflow = 0u;
  j->Cross.Copies = j->Cross.CopyActive = 0u;
  j->Cross.Placed = j->Cross.LastPlaced = j->Cross.PlacedActive = 0u;
  j->Cross.KeptMask = 2u; /* kept 2: no previous execution */
  j->Cross.KeptLastForeign = j->Cross.KeptLastPlaced = j->Cross.KeptLastForeignKinds = 0u;
  AgxD3d12JobRangeClear(&j->Cross.PlacedDst);
  AgxD3d12JobRangeClear(&j->Cross.TableDrawSet);
  AgxD3d12JobRangeClear(&j->Cross.TableDispatchSet);
  AgxD3d12JobRangeClear(&j->Cross.ShadowRender);
  AgxD3d12JobRangeClear(&j->Cross.ShadowPost);
  AgxD3d12JobRangeClear(&j->Cross.Copy);
  AgxD3d12JobRangeClear(&j->Cross.Render);
  AgxD3d12JobRangeClear(&j->Cross.Last);
  AgxD3d12JobRangeClear(&j->Cross.Tail);
  AgxD3d12JobRangeClear(&j->Cross.Pre);
  AgxD3d12JobRangeClear(&j->Cross.KeptRender);
  AgxD3d12JobRangeClear(&j->Cross.KeptPost);
}

/* type 0 transition, 1 aliasing, 2 UAV (id 0 = global), 3 0022 ranged
 * (aliasing with flag 0x8); before/after: transition states, else 0 */
static inline void AgxD3d12JobBarrierAdd(AGX_D3D12_JOB_BARRIERS *j, unsigned type, unsigned flags,
                                         unsigned before, unsigned after, unsigned id) {
  AGX_D3D12_JOB_BARRIER_SET *s = &j->Open;
  unsigned i;
  j->LastBarrierBefore = before;
  j->LastBarrierAfter = after;
  ++j->Total;
  ++s->Count;
  s->Flags |= flags;
  if (type != 2u) {
    ++s->NonUav;
    ++j->NonUavSince;
  }
  if (type == 0u) {
    ++s->Transitions;
    s->StateBefore |= before;
    s->StateAfter |= after;
  } else if (type == 1u || (type == 3u && (flags & 0x8u))) {
    ++s->Aliasing;
  } else if (type == 2u) {
    ++s->Uav;
    if (!id) ++s->GlobalUav;
  }
  if (!id) return;
  for (i = 0; i < s->Ids && i < AGX_D3D12_JOB_BARRIER_IDS; ++i)
    if (s->Id[i] == id) return;
  if (s->Ids < AGX_D3D12_JOB_BARRIER_IDS) {
    s->Id[s->Ids++] = id;
    s->IdHash ^= id;
  } else {
    s->IdOverflow = 1u;
  }
}

/* a command's owner work begins (kind = its recorded command kind) */
static inline void AgxD3d12JobBarrierBegin(AGX_D3D12_JOB_BARRIERS *j, unsigned kind) {
  j->Before = j->Open;
  AgxD3d12JobBarrierSetClear(&j->Open);
  j->Active = kind ? kind : 0xffu;
  j->ActiveLocal = 0u;
  j->Cross.CopyActive = 0u;
  /* OV2: command kinds 2 dispatch, 5 draw (agx_d3d12_ddi.cpp kCommand*) */
  if (kind == 2u) ++j->DispatchesPending;
  else if (kind != 5u) {
    ++j->ForeignSince;
    j->ForeignKinds |= 1u << (kind & 31u);
    ++j->Cross.Foreign;
    j->Cross.ForeignKinds |= 1u << (kind & 31u);
  }
  j->Cross.TableDraw = j->Cross.TableDispatch = 0u;
  j->Cross.PlacedActive = 0u;
  AgxD3d12JobRangeClear(&j->Cross.PlacedDst);
  AgxD3d12JobRangeClear(&j->Cross.TableDrawSet);
  AgxD3d12JobRangeClear(&j->Cross.TableDispatchSet);
}

static inline void AgxD3d12JobBarrierEnd(AGX_D3D12_JOB_BARRIERS *j) {
  /* OV2 ranges: a draw's accesses join its render batch, and the barriers
   * before it order the render's own draws only; a dispatch commits the
   * barriers before it (those after a compute batch's last dispatch order
   * later work, not that batch) */
  /* (an ExecuteIndirect expansion counts only when it replayed draws: a
   * dispatch expansion must not drop the barriers before it) */
  /* cross-execution receipt: the same accesses, kept past render
   * submissions that leave another render batch open */
  if (j->Active == 5u || j->RangeDraw.Count || j->RangeDraw.Overflow) {
    AgxD3d12JobRangeMerge(&j->Cross.Render, &j->RangeDraw);
    AgxD3d12JobRangeMerge(&j->Cross.Render, &j->Resolved);
  }
  if (j->Cross.TableDraw) j->Cross.Render.Overflow |= AGX_D3D12_JOB_RANGE_TABLE;
  if (j->Cross.TableDispatch) j->Cross.TableCompute = 1u;
  /* N4c-v3: the staged table accesses and placed resolve destination join
   * the cross sets only now, after any submission inside the command (a
   * render submitted by this draw's own work must neither take them nor
   * drop them) */
  if (j->Cross.TableDrawSet.Count || j->Cross.TableDrawSet.Overflow)
    AgxD3d12JobRangeMerge(&j->Cross.Render, &j->Cross.TableDrawSet);
  if (j->Cross.TableDispatchSet.Count || j->Cross.TableDispatchSet.Overflow)
    AgxD3d12JobRangeMerge(&j->Cross.Compute, &j->Cross.TableDispatchSet);
  if (j->Cross.PlacedActive) AgxD3d12JobRangeMerge(&j->Cross.Render, &j->Cross.PlacedDst);
  j->Cross.PlacedActive = 0u;
  AgxD3d12JobRangeClear(&j->Cross.PlacedDst);
  AgxD3d12JobRangeClear(&j->Cross.TableDrawSet);
  AgxD3d12JobRangeClear(&j->Cross.TableDispatchSet);
  if (j->Active == 2u || j->RangeDispatch.Count || j->RangeDispatch.Overflow) {
    j->Cross.PreCommitted = j->Cross.Pre.Count;
    j->Cross.PreCommittedOverflow = j->Cross.Pre.Overflow;
  }
  j->Cross.TableDraw = j->Cross.TableDispatch = 0u;
  if (j->Active == 5u || j->RangeDraw.Count || j->RangeDraw.Overflow) {
    ++j->DrawsSinceDispatch;
    AgxD3d12JobRangeMerge(&j->RangeRender, &j->RangeDraw);
    AgxD3d12JobRangeMerge(&j->RangeRender, &j->Resolved);
    AgxD3d12JobRangeClear(&j->RangeDraw);
    AgxD3d12JobRangeClear(&j->Post);
    j->PostCommitted = j->PostCommittedOverflow = 0u;
  } else if (j->Active == 2u) {
    j->GlobalUavSinceDispatch = 0u;
    AgxD3d12JobRangeClear(&j->BarriersSinceDispatch);
    j->DrawsSinceDispatch = 0u;
    ++j->DispatchesSince;
    j->PostCommitted = j->Post.Count;
    j->PostCommittedOverflow = j->Post.Overflow;
  }
  /* OV5: a dispatch's (or a dispatch expansion's) accesses join the open
   * compute batch */
  if (j->RangeDispatch.Count || j->RangeDispatch.Overflow) {
    AgxD3d12JobRangeMerge(&j->RangeCompute, &j->RangeDispatch);
    AgxD3d12JobRangeClear(&j->RangeDispatch);
    j->GlobalUavSinceDispatch = 0u;
  }
  j->Active = j->ActiveLocal = 0u;
}
/* OV2 ranges: the active draw accesses [lo, hi) of heap (uav: through a UAV
 * view); heap NULL = an access the tracker cannot place */
static inline void AgxD3d12JobBarrierDrawRange(AGX_D3D12_JOB_BARRIERS *j, const void *heap, unsigned long long lo,
                                               unsigned long long hi, unsigned uav) {
  AgxD3d12JobRangeAdd(&j->RangeDraw, heap, lo, hi, uav);
}
/* OV5: the active dispatch accesses [lo, hi) of heap (uav: may write) */
static inline void AgxD3d12JobBarrierDispatchRange(AGX_D3D12_JOB_BARRIERS *j, const void *heap,
                                                   unsigned long long lo, unsigned long long hi, unsigned uav) {
  AgxD3d12JobRangeAdd(&j->RangeDispatch, heap, lo, hi, uav & (1u | AGX_D3D12_JOB_SRC_MASK));
}
/* OV2 ranges: a ResolveQueryData destination [lo, hi) of heap */
static inline void AgxD3d12JobBarrierResolveRange(AGX_D3D12_JOB_BARRIERS *j, const void *heap,
                                                  unsigned long long lo, unsigned long long hi) {
  AgxD3d12JobRangeAdd(&j->Resolved, heap, lo, hi, 0u);
}
/* OV2 ranges: the barrier just added (AgxD3d12JobBarrierAdd type) names
 * [lo, hi) of heap; a UAV barrier without a resource is global */
static inline void AgxD3d12JobBarrierRange(AGX_D3D12_JOB_BARRIERS *j, unsigned type, unsigned id, const void *heap,
                                           unsigned long long lo, unsigned long long hi) {
  if (type == 2u && !id) {
    j->GlobalUavSinceDispatch = 1u;
    heap = (const void *)1;
    lo = hi = 0ull;
  }
  if (!(type == 2u && !id) && !j->DrawsSinceDispatch) {
    const unsigned count = j->BarriersSinceDispatch.Count;
    AgxD3d12JobRangeAdd(&j->BarriersSinceDispatch, heap, lo, hi,
                        (type & 3u) | ((id & 0xfffffu) << 12)); /* receipt only */
    if (j->BarriersSinceDispatch.Count > count)
      j->BarriersSinceDispatch.At[count] = (j->LastBarrierBefore & 0xffffu) | (j->LastBarrierAfter << 16);
  }
  {
    const unsigned before = j->Post.Count;
    AgxD3d12JobRangeAdd(&j->Post, heap, lo, hi, type == 2u ? (id ? 1u : 2u) : 0u);
    if (j->Post.Count > before) j->Post.At[before] = j->DispatchesSince;
    /* cross-execution receipt: every barrier since the kept render's last
     * draw, and those of this execution before its first batch */
    AgxD3d12JobRangeAdd(&j->Cross.Tail, heap, lo, hi, type == 2u ? (id ? 1u : 2u) : 0u);
    if (j->Cross.InExecution && !j->Cross.Batches)
      AgxD3d12JobRangeAdd(&j->Cross.Pre, heap, lo, hi, type == 2u ? (id ? 1u : 2u) : 0u);
  }
}

/* the active command's work is foreign to a compute batch after all */
static inline unsigned AgxD3d12JobBarrierForeignActive(const AGX_D3D12_JOB_BARRIERS *j) {
  return j->Active && j->Active != 2u && j->Active != 5u && !j->ActiveLocal ? 1u : 0u;
}

/* OV3: the active command puts no work into the next compute batch (call
 * before its owner work submits anything) */
static inline void AgxD3d12JobBarrierLocal(AGX_D3D12_JOB_BARRIERS *j) {
  if (!AgxD3d12JobBarrierForeignActive(j)) return;
  j->ActiveLocal = 1u;
  if (j->ForeignSince) --j->ForeignSince;
  if (j->Cross.Foreign) --j->Cross.Foreign;
  if (j->Cross.CopyActive) { /* no longer in Foreign: its ranges stay */
    j->Cross.CopyActive = 0u;
    if (j->Cross.Copies) --j->Cross.Copies;
  }
  if (j->Cross.PlacedActive) { /* N4c-v3: no longer in Foreign */
    j->Cross.PlacedActive = 0u;
    if (j->Cross.Placed) --j->Cross.Placed;
    AgxD3d12JobRangeClear(&j->Cross.PlacedDst);
  }
}

/* OV3: owner work outside any command (a deferred resolve copied the
 * native way) joins the current compute batch */
static inline void AgxD3d12JobBarrierForeign(AGX_D3D12_JOB_BARRIERS *j) {
  /* EXP2203: the work belongs to the open compute batch only -- it makes
   * that batch foreign (ComputeForeign, OV2) and its writes unplaced (OV5),
   * and says nothing about a render submitted before that batch */
  j->ComputeForeign = 1u;
  j->ForeignKinds |= 1u << 31;
  j->RangeCompute.Overflow |= AGX_D3D12_JOB_RANGE_NULL; /* OV5: its writes are not placed */
}

/* a batch of kind (1 render, 2 compute) is submitted: values {kind,
 * previous kind, barriers, transitions, UAV, aliasing, OR StateBefore,
 * OR StateAfter, distinct resources, xor of their ids, global UAV barriers,
 * trigger (bits 0-7 active command kind or 0, 8 more than
 * AGX_D3D12_JOB_BARRIER_IDS resources, 16-19 OR of the barrier flags),
 * every barrier since the previous batch} */
static inline void AgxD3d12JobBarrierBatch(AGX_D3D12_JOB_BARRIERS *j, unsigned kind,
                                           unsigned values[AGX_D3D12_JOB_BARRIER_VALUES]) {
  AGX_D3D12_JOB_BARRIER_SET *s = j->Active ? &j->Before : &j->Open;
  values[0] = kind;
  values[1] = j->PreviousKind;
  values[2] = s->Count;
  values[3] = s->Transitions;
  values[4] = s->Uav;
  values[5] = s->Aliasing;
  values[6] = s->StateBefore;
  values[7] = s->StateAfter;
  values[8] = s->Ids;
  values[9] = s->IdHash;
  values[10] = s->GlobalUav;
  values[11] = (j->Active & 0xffu) | (s->IdOverflow << 8) | ((s->Flags & 0xfu) << 16);
  values[12] = j->Total;
  /* OV2 state for the next batch. The command whose owner work submits
   * this batch is already counted: a non-dispatch trigger stays foreign for
   * the next batch, a dispatch trigger belongs to it. */
  if (kind == 1u) {
    j->EarlyDispatch = j->DispatchesPending > (j->Active == 2u ? 1u : 0u) ? 1u : 0u;
    j->ForeignSince = AgxD3d12JobBarrierForeignActive(j);
    /* the receipt's kinds: only those between this render and the next
     * compute batch (and the driver work still in the open compute batch) */
    j->ForeignKinds = (j->ComputeForeign ? 1u << 31 : 0u) |
                      (j->ForeignSince ? 1u << (j->Active & 31u) : 0u);
  } else {
    j->EarlyDispatch = 0u;
    j->ForeignSince = 0u;
    j->ComputeForeign = 0u;
    j->ForeignKinds = 0u;
  }
  j->DispatchesPending = j->Active == 2u ? 1u : 0u;
  j->DispatchesSince = 0u;
  j->GapAfterRender = kind == 1u ? s->Count : ~0u;
  if (kind == 1u) j->RenderUav = j->UavDraws != j->UavDrawsBase[0] ? 1u : 0u;
  if (kind == 1u) {
    j->RangeLast = j->RangeRender;
    AgxD3d12JobRangeClear(&j->RangeRender);
  } else {
    j->RangeLastCompute = j->RangeCompute;
    AgxD3d12JobRangeClear(&j->RangeCompute);
    AgxD3d12JobRangeClear(&j->Cross.Compute);
  }
  j->UavDrawsBase[0] = j->UavDrawsBase[1];
  j->UavDrawsBase[1] = j->UavDraws;
  j->NonUavSince = 0u;
  AgxD3d12JobBarrierSetClear(s);
  j->Total = 0u;
  j->PreviousKind = kind;
}

/* OV2: a draw bound a UAV (its writes may be read by the next compute job
 * after a UAV barrier inside that job). */
static inline void AgxD3d12JobBarrierDrawUav(AGX_D3D12_JOB_BARRIERS *j) { ++j->UavDraws; }

/* OV2: a new ExecuteCommandLists starts; Known = 0 when a list's barriers
 * were not all logged (the whole execution then never pairs). */
static inline void AgxD3d12JobBarrierExecution(AGX_D3D12_JOB_BARRIERS *j, unsigned known) {
  j->PreviousKind = 0u;
  j->GapAfterRender = ~0u;
  j->Unknown = known ? 0u : 1u;
  AgxD3d12JobRangesReset(j);
}
static inline void AgxD3d12JobBarrierPoison(AGX_D3D12_JOB_BARRIERS *j) { j->Unknown = 1u; }

/* OV2 refusal reason for the compute batch now being submitted (EXP2198:
 * the KMD saw ~no flagged pair): 0 independent, 1 barriers not all known,
 * 2 previous batch not a render of this execution, 3 barriers between the
 * render and this batch, 4 the render may have written a UAV, 5 a dispatch
 * recorded before the render, 6 other owner work since the render, 7 a
 * non-UAV barrier inside this batch. First match in that order. */
static inline unsigned AgxD3d12JobBarrierRefusal(const AGX_D3D12_JOB_BARRIERS *j) {
  const AGX_D3D12_JOB_BARRIER_SET *after = j->Active ? &j->Before : &j->Open;
  unsigned foreign;
  if (j->Unknown) return 1u;
  if (j->PreviousKind != 1u) return 2u;
  if (j->GapAfterRender != 0u) return 3u;
  if (j->RenderUav) return 4u;
  if (j->EarlyDispatch) return 5u;
  /* the active command (a draw or other work that submits this batch
   * lazily) comes after it */
  foreign = j->ForeignSince;
  if (AgxD3d12JobBarrierForeignActive(j) && foreign) --foreign;
  if (foreign || j->ComputeForeign) return 6u;
  if (after->NonUav > j->NonUavSince) return 7u;
  return j->NonUavSince - after->NonUav == 0u ? 0u : 7u;
}
/* OV2 ranges: a barrier between the last render's last draw and this
 * compute batch's last dispatch orders the render's accesses before later
 * work: a global UAV barrier when the render accessed a UAV, a UAV barrier
 * on a range the render accessed through a UAV, any other barrier on a
 * range the render accessed at all. Unknown or overflowing sets order. */
/* the first of the first Limit Post barriers that orders the last
 * render's accesses: its index, Limit when none, 0 with unknown sets */
static inline unsigned AgxD3d12JobRangeFirstOrdering(const AGX_D3D12_JOB_BARRIERS *j, unsigned limit,
                                                     unsigned limit_overflow) {
  unsigned b, r;
  if (limit_overflow || j->RangeLast.Overflow) return 0u;
  for (b = 0; b < limit && b < j->Post.Count; ++b) {
    const AGX_D3D12_JOB_RANGE *x = &j->Post.R[b];
    if (x->Uav == 2u) {
      if (j->RangeLast.Uav) return b;
      continue;
    }
    for (r = 0; r < j->RangeLast.Count; ++r) {
      const AGX_D3D12_JOB_RANGE *y = &j->RangeLast.R[r];
      if (y->Heap != x->Heap || x->Lo >= y->Hi || y->Lo >= x->Hi) continue;
      if (x->Uav == 1u && !(y->Uav & 1u)) continue;
      return b;
    }
  }
  return limit;
}
/* OV2 ranges: a barrier between the last render's last draw and this
 * compute batch's last dispatch orders the render's accesses before later
 * work: a global UAV barrier when the render accessed a UAV, a UAV barrier
 * on a range the render accessed through a UAV, any other barrier on a
 * range the render accessed at all. Unknown or overflowing sets order. */
static inline unsigned AgxD3d12JobRangeOrdered(const AGX_D3D12_JOB_BARRIERS *j) {
  if (j->PostCommittedOverflow || j->RangeLast.Overflow) return 1u;
  return AgxD3d12JobRangeFirstOrdering(j, j->PostCommitted, 0u) < j->PostCommitted ? 1u : 0u;
}
/* OV4 receipt: the dispatches of the compute batch before the first barrier
 * that orders the last render's accesses (all of them when none does; 0
 * when the sets are unknown) */
static inline unsigned AgxD3d12JobRangePrefix(const AGX_D3D12_JOB_BARRIERS *j) {
  unsigned b;
  if (j->PostCommittedOverflow || j->RangeLast.Overflow) return 0u;
  b = AgxD3d12JobRangeFirstOrdering(j, j->PostCommitted, 0u);
  return b < j->PostCommitted && b < j->Post.Count ? j->Post.At[b] : j->DispatchesSince;
}
/* OV2 ranges rule (opt-in): reasons 1, 2, 5 and 6 as below; 8 = a barrier
 * since the render orders its accesses (AgxD3d12JobRangeOrdered), which
 * replaces 3 (any gap barrier), 4 (the render bound a UAV) and 7 (any
 * non-UAV barrier inside the compute batch) -- D3D12 orders a render's
 * accesses before later work only through barriers naming them. */
static inline unsigned AgxD3d12JobBarrierRefusalRanges(const AGX_D3D12_JOB_BARRIERS *j) {
  unsigned foreign;
  if (j->Unknown) return 1u;
  if (j->PreviousKind != 1u) return 2u;
  if (j->EarlyDispatch) return 5u;
  foreign = j->ForeignSince;
  if (AgxD3d12JobBarrierForeignActive(j) && foreign) --foreign;
  if (foreign || j->ComputeForeign) return 6u;
  return AgxD3d12JobRangeOrdered(j) ? 8u : 0u;
}
/* OV4 (opt-in): split the open compute batch here -- the barrier just fed
 * (between commands) is the first to order the last render's accesses,
 * the batch has dispatches since that render and those are independent of
 * it under the ranges rule. The owner then submits the batch (the prefix,
 * flagged independent: this barrier is not yet committed); the rest of the
 * compute work follows in a new batch behind it (reason 2: never flagged). */
static inline unsigned AgxD3d12JobSplitHere(const AGX_D3D12_JOB_BARRIERS *j) {
  if (j->Active || !j->DispatchesSince || !j->Post.Count) return 0u;
  if (AgxD3d12JobBarrierRefusalRanges(j) != 0u) return 0u;
  if (j->Post.Overflow) return 1u; /* an unplaceable barrier orders too */
  return AgxD3d12JobRangeFirstOrdering(j, j->Post.Count, 0u) < j->Post.Count ? 1u : 0u;
}
/* PF1 (APPLE_AGX_D3D12_EXPAND_FOLD, mesa/winsys/agx_win32_expand_ahead.h):
 * the batch took part in a fold -- a render whose counted draws read a VDM
 * stream that a preceding compute batch writes as its last dispatches, or
 * that compute batch. No D3D12 range shows that dependency, so neither may
 * lead or overlap the other: the render's TA never starts before the
 * compute (AgxD3d12JobRenderTaRefusal, prelude bit FOLDED) and the compute
 * is never flagged independent (OV2, ranges, cross lead). 13 = refused. */
#define AGX_D3D12_JOB_FOLDED 2u
#define AGX_D3D12_JOB_REFUSAL_FOLDED 13u
static inline unsigned AgxD3d12JobFoldRefusal(unsigned folded) {
  return folded & AGX_D3D12_JOB_FOLDED ? AGX_D3D12_JOB_REFUSAL_FOLDED : 0u;
}
/* OV5 (EXP2201: Metal starts a render's vertex/TA work while the compute
 * encoder before it runs and orders only the 3D stage): the render batch
 * now being submitted (call before AgxD3d12JobBarrierBatch; prelude: its
 * CDM prelude exists) may run its TA while the compute batch just before it
 * runs, its 3D waiting for that batch. 0 TA-independent; 1 barriers not all
 * known; 2 previous batch not a compute batch of this execution; 6 driver
 * or other non-draw work since; 9 a compute prelude (it queues behind the
 * compute batch and the TA waits for it); 10 a vertex-stage access
 * conflicts with the compute batch (its UAV write vs any vertex access, or
 * any access vs a vertex-stage write); 11 unplaceable accesses; 12 a global
 * UAV barrier after the compute batch with UAVs on both sides. */
static inline unsigned AgxD3d12JobRenderTaRefusal(const AGX_D3D12_JOB_BARRIERS *j, unsigned prelude) {
  const AGX_D3D12_JOB_RANGE_SET *c = &j->RangeLastCompute, *r = &j->RangeRender;
  unsigned a, b;
  if (AgxD3d12JobFoldRefusal(prelude)) return AGX_D3D12_JOB_REFUSAL_FOLDED;
  if (j->Unknown) return 1u;
  if (j->PreviousKind != 2u) return 2u;
  if (prelude) return 9u;
  /* non-draw commands since the compute batch; driver work in a compute
   * batch is that batch's (unplaced writes: 11), not this render's */
  if (j->ForeignSince > AgxD3d12JobBarrierForeignActive(j)) return 6u;
  if (c->Overflow || r->Overflow) return 11u;
  if (j->GlobalUavSinceDispatch && c->Uav && r->Uav) return 12u;
  for (a = 0; a < r->Count; ++a) {
    const AGX_D3D12_JOB_RANGE *x = &r->R[a];
    if (!(x->Uav & AGX_D3D12_JOB_VERTEX)) continue;
    for (b = 0; b < c->Count; ++b) {
      const AGX_D3D12_JOB_RANGE *y = &c->R[b];
      if (y->Heap != x->Heap || x->Lo >= y->Hi || y->Lo >= x->Hi) continue;
      if ((y->Uav & 1u) || (x->Uav & 1u)) return 10u;
    }
  }
  return 0u;
}
/* OV8 receipt verdicts for the render about to be submitted after a compute batch.
 * Range: 0 independent, 10 a render access (every stage) overlaps a compute access
 * with a write on either side, 11 unplaceable. Barrier: 0, 8 a barrier since the
 * last dispatch names a range the compute wrote (or a global UAV barrier), 11
 * unplaceable. Prelude: 0, 9 the render has a prelude and an indirect argument or
 * count range overlaps a compute write. */
static inline unsigned AgxD3d12JobOv8Range(const AGX_D3D12_JOB_BARRIERS *j) {
  const AGX_D3D12_JOB_RANGE_SET *c = &j->RangeLastCompute, *r = &j->RangeRender;
  unsigned a, b;
  if (c->Overflow || r->Overflow) return 11u;
  for (a = 0; a < r->Count; ++a) {
    const AGX_D3D12_JOB_RANGE *x = &r->R[a];
    const unsigned writes = (x->Uav & 1u) || (x->Uav & (AGX_D3D12_JOB_SRC_TARGET | AGX_D3D12_JOB_SRC_CLEAR));
    for (b = 0; b < c->Count; ++b) {
      const AGX_D3D12_JOB_RANGE *y = &c->R[b];
      if (y->Heap != x->Heap || x->Lo >= y->Hi || y->Lo >= x->Hi) continue;
      if ((y->Uav & 1u) || writes) return 10u;
    }
  }
  return 0u;
}
/* OV8 1b: the first pair behind verdict 10, any stage */
static inline unsigned AgxD3d12JobOv8RangePair(const AGX_D3D12_JOB_BARRIERS *j, AGX_D3D12_JOB_RANGE *render,
                                               AGX_D3D12_JOB_RANGE *compute) {
  const AGX_D3D12_JOB_RANGE_SET *c = &j->RangeLastCompute, *r = &j->RangeRender;
  unsigned a, b;
  for (a = 0; a < r->Count; ++a) {
    const AGX_D3D12_JOB_RANGE *x = &r->R[a];
    const unsigned writes = (x->Uav & 1u) || (x->Uav & (AGX_D3D12_JOB_SRC_TARGET | AGX_D3D12_JOB_SRC_CLEAR));
    for (b = 0; b < c->Count; ++b) {
      const AGX_D3D12_JOB_RANGE *y = &c->R[b];
      if (y->Heap != x->Heap || x->Lo >= y->Hi || y->Lo >= x->Hi || !((y->Uav & 1u) || writes)) continue;
      *render = *x;
      *compute = *y;
      return 1u;
    }
  }
  return 0u;
}
/* OV8 1b: the first barrier before the first draw that names a compute write */
static inline unsigned AgxD3d12JobOv8BarrierPair(const AGX_D3D12_JOB_BARRIERS *j, AGX_D3D12_JOB_RANGE *barrier,
                                                 AGX_D3D12_JOB_RANGE *compute, unsigned *states) {
  const AGX_D3D12_JOB_RANGE_SET *c = &j->RangeLastCompute, *s = &j->BarriersSinceDispatch;
  unsigned a, b;
  for (a = 0; a < s->Count; ++a)
    for (b = 0; b < c->Count; ++b) {
      const AGX_D3D12_JOB_RANGE *x = &s->R[a], *y = &c->R[b];
      if (y->Heap == x->Heap && x->Lo < y->Hi && y->Lo < x->Hi && (y->Uav & 1u)) {
        *barrier = *x;
        barrier->Lo = x->Lo;
        *compute = *y;
        if (states) *states = s->At[a];
        return 1u;
      }
    }
  return 0u;
}
static inline unsigned AgxD3d12JobOv8Barrier(const AGX_D3D12_JOB_BARRIERS *j) {
  const AGX_D3D12_JOB_RANGE_SET *c = &j->RangeLastCompute, *s = &j->BarriersSinceDispatch;
  unsigned a, b;
  if (c->Overflow || s->Overflow) return 11u;
  if (j->GlobalUavSinceDispatch && c->Uav) return 8u;
  for (a = 0; a < s->Count; ++a)
    for (b = 0; b < c->Count; ++b) {
      const AGX_D3D12_JOB_RANGE *x = &s->R[a], *y = &c->R[b];
      if (y->Heap == x->Heap && x->Lo < y->Hi && y->Lo < x->Hi && (y->Uav & 1u)) return 8u;
    }
  return 0u;
}
static inline unsigned AgxD3d12JobOv8Prelude(const AGX_D3D12_JOB_BARRIERS *j, unsigned prelude) {
  const AGX_D3D12_JOB_RANGE_SET *c = &j->RangeLastCompute, *r = &j->RangeRender;
  unsigned a, b;
  if (!prelude) return 0u;
  for (a = 0; a < r->Count; ++a) {
    const AGX_D3D12_JOB_RANGE *x = &r->R[a];
    if (!(x->Uav & (AGX_D3D12_JOB_SRC_IND_ARGS | AGX_D3D12_JOB_SRC_IND_CNT))) continue;
    for (b = 0; b < c->Count; ++b) {
      const AGX_D3D12_JOB_RANGE *y = &c->R[b];
      if (y->Heap == x->Heap && x->Lo < y->Hi && y->Lo < x->Hi && (y->Uav & 1u)) return 9u;
    }
  }
  return 0u;
}
/* OV6b: the first pair behind verdict 10 (the same loop): 1 and the pair, or 0 */
static inline unsigned AgxD3d12JobRenderTaConflict(const AGX_D3D12_JOB_BARRIERS *j, AGX_D3D12_JOB_RANGE *render,
                                                   AGX_D3D12_JOB_RANGE *compute) {
  const AGX_D3D12_JOB_RANGE_SET *c = &j->RangeLastCompute, *r = &j->RangeRender;
  unsigned a, b;
  for (a = 0; a < r->Count; ++a) {
    const AGX_D3D12_JOB_RANGE *x = &r->R[a];
    if (!(x->Uav & AGX_D3D12_JOB_VERTEX)) continue;
    for (b = 0; b < c->Count; ++b) {
      const AGX_D3D12_JOB_RANGE *y = &c->R[b];
      if (y->Heap != x->Heap || x->Lo >= y->Hi || y->Lo >= x->Hi) continue;
      if ((y->Uav & 1u) || (x->Uav & 1u)) {
        *render = *x;
        *compute = *y;
        return 1u;
      }
    }
  }
  return 0u;
}
/* OV2: the compute batch now being submitted is independent of the render
 * batch submitted just before it under D3D12 rules: same execution, that
 * render was the previous batch, no barrier between them, no barrier but UAV
 * barriers inside the compute batch, and the render bound no UAV. Call before
 * AgxD3d12JobBarrierBatch for the compute batch. Any doubt -> 0. */
static inline unsigned AgxD3d12JobBarrierIndependent(const AGX_D3D12_JOB_BARRIERS *j) {
  return AgxD3d12JobBarrierRefusal(j) == 0u ? 1u : 0u;
}
/* OV2 cross-execution (receipt; R9 B.4: AgxD3d12JobBarrierExecution forgets
 * the previous batch, so the execution-first compute batch is always
 * refused with 2 whatever it touches). The functions below keep the last
 * render of an execution across the boundary and give a second, shadow
 * verdict for that one compute batch from exact ranges only; the coarse
 * counters (GapAfterRender, RenderUav, EarlyDispatch) are never read. */

/* the active draw (dispatch 0) or dispatch (dispatch 1) reaches resources
 * through a descriptor-heap table: accesses no range set holds */
static inline void AgxD3d12JobCrossUnplaced(AGX_D3D12_JOB_BARRIERS *j, unsigned dispatch) {
  if (dispatch) j->Cross.TableDispatch = 1u;
  else j->Cross.TableDraw = 1u;
}
/* owner work the tracker cannot attribute; cause 1 a batch submitted
 * outside an execution, 2 driver work outside one, 3 the newest fence moved
 * between two executions, 4 pending owner work at the begin, 5 a deferred
 * upload, 6 deferred resolves, 7 another owner or queue, 8 nested or
 * unfinished executions, 9 a batch from another thread. Nothing is kept
 * past it. */
static inline void AgxD3d12JobCrossBreak(AGX_D3D12_JOB_BARRIERS *j, unsigned cause) {
  AGX_D3D12_JOB_CROSS *c = &j->Cross;
  cause &= 0xfu;
  if (!cause) cause = 0xfu;
  if (c->InExecution) {
    if (!c->Broken) c->Broken = cause;
    return;
  }
  c->KeptMask |= 4u;
  if (c->Kept) return; /* already refused for an earlier cause */
  c->Kept = 6u;
  c->KeptDetail = 1u | (cause << 4);
}
/* an ExecuteCommandLists starts (after AgxD3d12JobBarrierExecution); cause
 * != 0: the owner saw work between the two executions (CrossBreak codes) */
static inline void AgxD3d12JobCrossBegin(AGX_D3D12_JOB_BARRIERS *j, unsigned cause) {
  AGX_D3D12_JOB_CROSS *c = &j->Cross;
  if (c->InExecution) { /* the previous one never ended */
    c->InExecution = 0u;
    c->Kept = 6u;
    c->KeptDetail = 1u | (8u << 4);
    c->KeptMask |= 4u;
  }
  if (cause) AgxD3d12JobCrossBreak(j, cause);
  c->InExecution = 1u;
  c->Broken = c->Batches = c->LastKind = c->Dirty = 0u;
  c->Foreign = c->ForeignKinds = c->LastForeign = c->LastForeignKinds = 0u;
  c->TableDraw = c->TableDispatch = c->TableCompute = 0u;
  c->PreCommitted = c->PreCommittedOverflow = 0u;
  c->Copies = c->CopyActive = 0u;
  c->Placed = c->LastPlaced = c->PlacedActive = 0u;
  AgxD3d12JobRangeClear(&c->PlacedDst);
  AgxD3d12JobRangeClear(&c->TableDrawSet);
  AgxD3d12JobRangeClear(&c->TableDispatchSet);
  AgxD3d12JobRangeClear(&c->Copy);
  AgxD3d12JobRangeClear(&c->Render);
  AgxD3d12JobRangeClear(&c->Last);
  AgxD3d12JobRangeClear(&c->Tail);
  AgxD3d12JobRangeClear(&c->Pre);
}
/* N4c-v2: the active command is a buffer copy dst[dlo, dhi) <- src[slo, shi)
 * that joins the open compute batch. Before the execution's first batch
 * its ranges are compared with the kept render (AgxD3d12JobCrossRefusal)
 * instead of refusing as other owner work; once per command, and never for
 * a command already marked local. A NULL heap cannot be placed. */
static inline void AgxD3d12JobCrossCopy(AGX_D3D12_JOB_BARRIERS *j, const void *dst, unsigned long long dlo,
                                        unsigned long long dhi, const void *src, unsigned long long slo,
                                        unsigned long long shi) {
  AGX_D3D12_JOB_CROSS *c = &j->Cross;
  if (!c->InExecution || c->Batches || j->Active != AGX_D3D12_JOB_KIND_COPY || j->ActiveLocal || c->CopyActive)
    return;
  c->CopyActive = 1u;
  ++c->Copies;
  AgxD3d12JobRangeAdd(&c->Copy, dst, dlo, dhi, 1u);
  AgxD3d12JobRangeAdd(&c->Copy, src, slo, shi, 0u);
}
/* N4c-v3: the active command is a ResolveQueryData whose only work is a
 * copy into the compute batch (Asahi agx_get_query_result_resource: its
 * writers are flushed, the copy goes to the compute batch or the CPU, never
 * to a render batch) writing [lo, hi) of heap. A render submitted by its
 * own writer flush does not wait for it, so it is not other owner work for
 * that render (LastPlaced); the destination joins the next render's
 * accesses as a write (conservative). Once per command; never for a command
 * already marked local. */
static inline void AgxD3d12JobCrossPlace(AGX_D3D12_JOB_BARRIERS *j, const void *heap, unsigned long long lo,
                                         unsigned long long hi) {
  AGX_D3D12_JOB_CROSS *c = &j->Cross;
  if (!AgxD3d12JobBarrierForeignActive(j) || c->PlacedActive) return;
  c->PlacedActive = 1u;
  ++c->Placed;
  AgxD3d12JobRangeAdd(&c->PlacedDst, heap, lo, hi, 0u);
}
/* N4c-v3: the active draw (dispatch 0) or dispatch (1) accesses [lo, hi)
 * of heap through a descriptor-heap table (flags as the range sinks: bit 0
 * may write, AGX_D3D12_JOB_VERTEX, AGX_D3D12_JOB_SRC_*); staged until the
 * command ends */
static inline void AgxD3d12JobCrossTableRange(AGX_D3D12_JOB_BARRIERS *j, unsigned dispatch, const void *heap,
                                              unsigned long long lo, unsigned long long hi, unsigned flags) {
  if (dispatch) AgxD3d12JobRangeAdd(&j->Cross.TableDispatchSet, heap, lo, hi, flags & (1u | AGX_D3D12_JOB_SRC_MASK));
  else AgxD3d12JobRangeAdd(&j->Cross.TableDrawSet, heap, lo, hi, flags);
}
/* the compute batch now being submitted is the first batch of its execution */
static inline unsigned AgxD3d12JobCrossFirst(const AGX_D3D12_JOB_BARRIERS *j) {
  return j->Cross.InExecution && !j->Cross.Batches && j->PreviousKind == 0u ? 1u : 0u;
}
/* a batch of kind is submitted (call before AgxD3d12JobBarrierBatch);
 * clean: no other render batch holding work stays open, so every draw ended
 * so far is in a submitted batch or in this one */
static inline void AgxD3d12JobCrossBatch(AGX_D3D12_JOB_BARRIERS *j, unsigned kind, unsigned clean) {
  AGX_D3D12_JOB_CROSS *c = &j->Cross;
  const unsigned active = AgxD3d12JobBarrierForeignActive(j);
  unsigned placed;
  ++c->Batches;
  c->LastKind = kind;
  if (kind != 1u) {
    c->TableCompute = 0u;
    return;
  }
  c->Last = c->Render;
  /* the command whose owner work submits the render comes after it */
  c->LastForeign = c->Foreign - (active && c->Foreign ? 1u : 0u);
  c->LastForeignKinds = c->ForeignKinds;
  /* N4c-v3: so does a placed resolve whose writer flush submits it */
  placed = active && c->PlacedActive ? 1u : 0u;
  c->LastPlaced = c->Placed - (placed && c->Placed ? 1u : 0u);
  if (clean && !c->Dirty) c->Tail = j->Post;
  c->Dirty = clean ? 0u : 1u;
  if (clean) {
    AgxD3d12JobRangeClear(&c->Render);
    c->Foreign = active;
    c->ForeignKinds = active ? 1u << (j->Active & 31u) : 0u;
    c->Placed = placed;
  }
}
/* the execution ended and everything was submitted (ok = 0: it failed):
 * decide what the next execution's first compute may be compared with */
static inline void AgxD3d12JobCrossEnd(AGX_D3D12_JOB_BARRIERS *j, unsigned ok) {
  AGX_D3D12_JOB_CROSS *c = &j->Cross;
  unsigned kept = 0u, detail = 0u;
  if (!c->InExecution) { /* an end without its begin */
    c->Kept = 6u;
    c->KeptDetail = 1u | (8u << 4);
    c->KeptMask |= 4u;
    AgxD3d12JobRangeClear(&c->ShadowRender);
    AgxD3d12JobRangeClear(&c->ShadowPost);
    return;
  }
  c->InExecution = 0u;
  c->KeptKinds = c->LastForeignKinds | c->ForeignKinds | (j->ComputeForeign ? 1u << 31 : 0u);
  /* N4c-v3 receipt (AgxD3d12JobCrossMask): every kept-side condition and
   * the kept render's sets, whatever the verdict below */
  c->KeptLastForeign = c->LastForeign;
  c->KeptLastPlaced = c->LastPlaced;
  c->KeptLastForeignKinds = c->LastForeignKinds;
  c->KeptMask = (j->Unknown ? 1u : 0u) | (c->LastKind != 1u ? 2u : 0u) | (!ok || c->Broken ? 4u : 0u) |
                (c->LastForeign - c->LastPlaced ? 8u : 0u) | (c->Foreign ? 0x10u : 0u) |
                (j->ComputeForeign ? 0x20u : 0u) | ((c->Last.Overflow & 7u) << 6);
  c->ShadowRender = c->Last;
  c->ShadowPost = c->Tail;
  if (j->Unknown) {
    kept = 1u;
    detail = 2u;
  } else if (!ok) {
    kept = 6u;
    detail = 4u;
  } else if (c->Broken) {
    kept = 6u;
    detail = 1u | (c->Broken << 4);
  } else if (c->LastKind != 1u) {
    kept = 2u;
    detail = c->LastKind == 2u ? 2u : 1u;
  } else if ((c->LastForeign - c->LastPlaced) || c->Foreign || j->ComputeForeign) {
    /* N4c-v3: placed resolves before the kept render are not other owner
     * work for it; any command after its submission still is */
    kept = 6u;
    detail = 2u;
  } else if (c->Last.Overflow) {
    kept = 11u;
    detail = c->Last.Overflow & AGX_D3D12_JOB_RANGE_TABLE ? 2u : 1u;
  }
  c->Kept = kept;
  c->KeptDetail = detail;
  if (!kept) {
    c->KeptRender = c->Last;
    c->KeptPost = c->Tail;
  } else {
    AgxD3d12JobRangeClear(&c->KeptRender);
    AgxD3d12JobRangeClear(&c->KeptPost);
  }
}
/* a kept render access the compute batch must not overlap with a write on
 * either side: 1 the render may write it (a UAV, a target, a clear, stream
 * output, a resolve destination or an unclassified access), 0 it only
 * reads it */
static inline unsigned AgxD3d12JobCrossRenderWrites(const AGX_D3D12_JOB_RANGE *r) {
  if (r->Uav & 1u) return 1u;
  if (r->Uav & (AGX_D3D12_JOB_SRC_TARGET | AGX_D3D12_JOB_SRC_CLEAR | AGX_D3D12_JOB_SRC_SO)) return 1u;
  return r->Uav & AGX_D3D12_JOB_SRC_MASK ? 0u : 1u;
}
/* N4c-v2: a placed copy range x against a kept render range y: 5 the
 * render may write what the copy reads or writes, 6 the copy writes what
 * the render reads, 0 they do not order each other */
static inline unsigned AgxD3d12JobCrossCopyPair(const AGX_D3D12_JOB_RANGE *x, const AGX_D3D12_JOB_RANGE *y) {
  if (y->Heap != x->Heap || x->Lo >= y->Hi || y->Lo >= x->Hi) return 0u;
  if (AgxD3d12JobCrossRenderWrites(y)) return 5u;
  return x->Uav & 1u ? 6u : 0u;
}
/* the receipt: the number of conflicting {copy, kept render} range pairs
 * and the first of them (NULL when none) */
static inline unsigned AgxD3d12JobCrossCopyConflicts(const AGX_D3D12_JOB_BARRIERS *j,
                                                     const AGX_D3D12_JOB_RANGE **copy,
                                                     const AGX_D3D12_JOB_RANGE **render) {
  const AGX_D3D12_JOB_CROSS *c = &j->Cross;
  unsigned i, k, n = 0u;
  *copy = *render = 0;
  for (i = 0; i < c->Copy.Count; ++i)
    for (k = 0; k < c->KeptRender.Count; ++k)
      if (AgxD3d12JobCrossCopyPair(&c->Copy.R[i], &c->KeptRender.R[k]) && !n++) {
        *copy = &c->Copy.R[i];
        *render = &c->KeptRender.R[k];
      }
  return n;
}
/* a barrier range b since the kept render: 3 it names a range the compute
 * batch accesses, 4 it is a global UAV barrier and the compute batch
 * accesses a UAV, 0 neither. (A barrier naming only what the render
 * accessed, e.g. the back buffer's transition to PRESENT, orders nothing
 * the compute touches: the direct comparison covers every shared range.) */
/* (N4c-v3: the compute batch's accesses are RangeCompute and its
 * descriptor-table accesses Cross.Compute) */
static inline unsigned AgxD3d12JobCrossBarrier(const AGX_D3D12_JOB_BARRIERS *j, const AGX_D3D12_JOB_RANGE *b) {
  unsigned i, s;
  if (b->Uav == 2u) return j->RangeCompute.Uav || j->Cross.Compute.Uav ? 4u : 0u;
  for (s = 0; s < 2u; ++s) {
    const AGX_D3D12_JOB_RANGE_SET *set = s ? &j->Cross.Compute : &j->RangeCompute;
    for (i = 0; i < set->Count; ++i) {
      const AGX_D3D12_JOB_RANGE *x = &set->R[i];
      if (x->Heap == b->Heap && b->Lo < x->Hi && x->Lo < b->Hi) return 3u;
    }
  }
  return 0u;
}
/* shadow verdict for the execution-first compute batch now being submitted
 * (AgxD3d12JobCrossFirst): 0 independent of the kept render, 1 barriers not
 * all known (detail 1 this execution, 2 the previous one), 2 the previous
 * execution did not end with a render (1 no execution or no batch, 2 it
 * ended with a compute batch), 6 other owner work (1 between the
 * executions or unattributable, bits 4-7 the CrossBreak cause; 2 in or
 * after the kept render; 3 before this batch; 4 the previous execution
 * failed), 8 the ranges order the two (1 the compute accesses what the
 * render may write, 2 it writes what the render reads, 3 a barrier since
 * the render names a range the compute accesses, 4 a global UAV barrier
 * since the render and the compute accesses a UAV), 11 an access cannot
 * be placed (1 kept set, 2 a heap table in the kept render, 3 the compute
 * set, 4 a heap table in the compute, 5 a barrier, 6 a compute batch
 * without a tracked dispatch, 7 a placed copy's range). First match.
 * N4c-v2: the copies placed by AgxD3d12JobCrossCopy are not other owner
 * work here; their ranges order the two like the compute's (8: 5 the
 * render may write what a copy accesses, 6 a copy writes what the render
 * reads). N4c-v3: the compute's accesses include its descriptor-table
 * accesses (Cross.Compute); a resolve placed before the kept render's
 * submission (AgxD3d12JobCrossPlace) is not other owner work for it. */
static inline unsigned AgxD3d12JobCrossRefusal(const AGX_D3D12_JOB_BARRIERS *j, unsigned *detail) {
  const AGX_D3D12_JOB_CROSS *c = &j->Cross;
  unsigned foreign, i, k, s, named = 0u;
  *detail = 0u;
  if (j->Unknown) {
    *detail = 1u;
    return 1u;
  }
  if (c->Kept) {
    *detail = c->KeptDetail;
    return c->Kept;
  }
  if (c->Broken) {
    *detail = 1u | (c->Broken << 4);
    return 6u;
  }
  foreign = c->Foreign - (c->Copies < c->Foreign ? c->Copies : c->Foreign);
  /* an active placed copy is already taken out */
  if (AgxD3d12JobBarrierForeignActive(j) && !c->CopyActive && foreign) --foreign;
  if (foreign || j->ComputeForeign) {
    *detail = 3u;
    return 6u;
  }
  if (j->RangeCompute.Overflow || c->Compute.Overflow) {
    *detail = 3u;
    return 11u;
  }
  if (c->TableCompute) {
    *detail = 4u;
    return 11u;
  }
  if (c->KeptPost.Overflow || c->PreCommittedOverflow) {
    *detail = 5u;
    return 11u;
  }
  if (!j->DispatchesSince) {
    *detail = 6u;
    return 11u;
  }
  if (c->Copy.Overflow) {
    *detail = 7u;
    return 11u;
  }
  for (s = 0; s < 2u; ++s) {
    const AGX_D3D12_JOB_RANGE_SET *set = s ? &c->Compute : &j->RangeCompute;
    for (i = 0; i < set->Count; ++i) {
      const AGX_D3D12_JOB_RANGE *x = &set->R[i];
      for (k = 0; k < c->KeptRender.Count; ++k) {
        const AGX_D3D12_JOB_RANGE *y = &c->KeptRender.R[k];
        if (y->Heap != x->Heap || x->Lo >= y->Hi || y->Lo >= x->Hi) continue;
        if (AgxD3d12JobCrossRenderWrites(y)) {
          *detail = 1u;
          return 8u;
        }
        if (x->Uav & 1u) {
          *detail = 2u;
          return 8u;
        }
      }
    }
  }
  for (i = 0; i < c->Copy.Count; ++i)
    for (k = 0; k < c->KeptRender.Count; ++k) {
      const unsigned n = AgxD3d12JobCrossCopyPair(&c->Copy.R[i], &c->KeptRender.R[k]);
      if (n) {
        *detail = n;
        return 8u;
      }
    }
  for (i = 0; i < c->KeptPost.Count; ++i) {
    const unsigned n = AgxD3d12JobCrossBarrier(j, &c->KeptPost.R[i]);
    if (n && (!named || n < named)) named = n;
  }
  for (i = 0; i < c->PreCommitted && i < c->Pre.Count; ++i) {
    const unsigned n = AgxD3d12JobCrossBarrier(j, &c->Pre.R[i]);
    if (n && (!named || n < named)) named = n;
  }
  if (named) {
    *detail = named;
    return 8u;
  }
  return 0u;
}
/* N4c-v3 full refusal mask (receipt only: never a verdict). Every condition
 * of AgxD3d12JobCrossRefusal for the execution-first compute batch,
 * evaluated independently (no first match), the ranges against the kept
 * render's shadow sets (kept even when the kept verdict refused). Bits: 0
 * barriers not all known (this or the previous execution); kept side: 1 no
 * render last (2/x), 2 broken, failed or work between the executions, 3
 * unplaced other owner work before the kept render's submission
 * (LastForeign - LastPlaced), 4 commands after it (Foreign), 5 driver work
 * in a compute batch (ComputeForeign), 6/7/8 the kept set FULL/NULL/TABLE;
 * current side: 9 Broken, 10 unplaced other owner work, 11 ComputeForeign,
 * 12 RangeCompute or Cross.Compute overflow, 13 a heap table in the compute
 * (TableCompute), 14 a barrier set overflow, 15 no tracked dispatch, 16
 * Copy overflow; ranges: 17 8/1, 18 8/2, 19 8/5, 20 8/6, 21 8/3, 22 8/4.
 * Contract: with the kept verdict 0, mask 0 exactly when the verdict is 0. */
#define AGX_D3D12_JOB_MASK_BITS 23u
#define AGX_D3D12_JOB_MASK_CLASSES 4u /* Pairs/First: 8/1, 8/2, 8/5, 8/6 */
typedef struct AGX_D3D12_JOB_CROSS_CONFLICT {
  AGX_D3D12_JOB_RANGE Current; /* the compute (8/1, 8/2) or copy (8/5, 8/6) range */
  AGX_D3D12_JOB_RANGE Kept;    /* the kept render's range */
} AGX_D3D12_JOB_CROSS_CONFLICT;
typedef struct AGX_D3D12_JOB_CROSS_MASK {
  unsigned Mask;
  unsigned Pairs[AGX_D3D12_JOB_MASK_CLASSES];
  unsigned KeptLastForeign, KeptLastPlaced, KeptLastForeignKinds; /* kept side */
  unsigned Foreign, Copies, ForeignKinds;                         /* current side */
  unsigned ShadowRender, Compute, RangeCompute, Copy;             /* set counts */
  AGX_D3D12_JOB_CROSS_CONFLICT First[AGX_D3D12_JOB_MASK_CLASSES][2];
} AGX_D3D12_JOB_CROSS_MASK;
static inline void AgxD3d12JobCrossMaskPair(AGX_D3D12_JOB_CROSS_MASK *m, unsigned cls, const AGX_D3D12_JOB_RANGE *x,
                                            const AGX_D3D12_JOB_RANGE *y) {
  if (m->Pairs[cls] < 2u) {
    m->First[cls][m->Pairs[cls]].Current = *x;
    m->First[cls][m->Pairs[cls]].Kept = *y;
  }
  ++m->Pairs[cls];
}
static inline unsigned AgxD3d12JobCrossMask(const AGX_D3D12_JOB_BARRIERS *j, AGX_D3D12_JOB_CROSS_MASK *m) {
  const AGX_D3D12_JOB_CROSS *c = &j->Cross;
  const AGX_D3D12_JOB_RANGE_SET *r = &c->ShadowRender;
  unsigned mask, foreign, i, k, s, n;
  for (i = 0; i < AGX_D3D12_JOB_MASK_CLASSES; ++i) {
    m->Pairs[i] = 0u;
    for (k = 0; k < 2u; ++k) {
      m->First[i][k].Current.Heap = m->First[i][k].Kept.Heap = 0;
      m->First[i][k].Current.Lo = m->First[i][k].Current.Hi = m->First[i][k].Kept.Lo = m->First[i][k].Kept.Hi = 0ull;
      m->First[i][k].Current.Uav = m->First[i][k].Kept.Uav = 0u;
    }
  }
  m->KeptLastForeign = c->KeptLastForeign;
  m->KeptLastPlaced = c->KeptLastPlaced;
  m->KeptLastForeignKinds = c->KeptLastForeignKinds;
  m->Foreign = c->Foreign;
  m->Copies = c->Copies;
  m->ForeignKinds = c->ForeignKinds;
  m->ShadowRender = r->Count;
  m->Compute = c->Compute.Count;
  m->RangeCompute = j->RangeCompute.Count;
  m->Copy = c->Copy.Count;
  mask = c->KeptMask & 0x1ffu;
  if (c->Kept && !mask) mask = 4u; /* a kept refusal always shows */
  if (j->Unknown) mask |= 1u;
  if (c->Broken) mask |= 1u << 9;
  foreign = c->Foreign - (c->Copies < c->Foreign ? c->Copies : c->Foreign);
  if (AgxD3d12JobBarrierForeignActive(j) && !c->CopyActive && foreign) --foreign;
  if (foreign) mask |= 1u << 10;
  if (j->ComputeForeign) mask |= 1u << 11;
  if (j->RangeCompute.Overflow || c->Compute.Overflow) mask |= 1u << 12;
  if (c->TableCompute) mask |= 1u << 13;
  if (c->ShadowPost.Overflow || c->PreCommittedOverflow) mask |= 1u << 14;
  if (!j->DispatchesSince) mask |= 1u << 15;
  if (c->Copy.Overflow) mask |= 1u << 16;
  for (s = 0; s < 2u; ++s) {
    const AGX_D3D12_JOB_RANGE_SET *set = s ? &c->Compute : &j->RangeCompute;
    for (i = 0; i < set->Count; ++i)
      for (k = 0; k < r->Count; ++k) {
        const AGX_D3D12_JOB_RANGE *x = &set->R[i], *y = &r->R[k];
        if (y->Heap != x->Heap || x->Lo >= y->Hi || y->Lo >= x->Hi) continue;
        if (AgxD3d12JobCrossRenderWrites(y)) AgxD3d12JobCrossMaskPair(m, 0u, x, y);
        else if (x->Uav & 1u) AgxD3d12JobCrossMaskPair(m, 1u, x, y);
      }
  }
  for (i = 0; i < c->Copy.Count; ++i)
    for (k = 0; k < r->Count; ++k) {
      n = AgxD3d12JobCrossCopyPair(&c->Copy.R[i], &r->R[k]);
      if (n) AgxD3d12JobCrossMaskPair(m, n == 5u ? 2u : 3u, &c->Copy.R[i], &r->R[k]);
    }
  for (i = 0; i < AGX_D3D12_JOB_MASK_CLASSES; ++i)
    if (m->Pairs[i]) mask |= 1u << (17u + i);
  for (i = 0; i < c->ShadowPost.Count; ++i) {
    n = AgxD3d12JobCrossBarrier(j, &c->ShadowPost.R[i]);
    if (n) mask |= 1u << (n == 3u ? 21u : 22u);
  }
  for (i = 0; i < c->PreCommitted && i < c->Pre.Count; ++i) {
    n = AgxD3d12JobCrossBarrier(j, &c->Pre.R[i]);
    if (n) mask |= 1u << (n == 3u ? 21u : 22u);
  }
  m->Mask = mask;
  return mask;
}
/* OV2 cross-execution apply (opt-in): the verdict in use for a compute
 * batch. With the switch on, the execution-first compute batch (first) takes
 * the shadow verdict; every other batch, and every batch with the switch
 * off, keeps the verdict it had (independent). */
static inline unsigned AgxD3d12JobCrossApply(unsigned enabled, unsigned first, unsigned shadow, unsigned independent) {
  if (!enabled || !first) return independent;
  return shadow == 0u ? 1u : 0u;
}
/* the receipt's histogram of shadow reasons, saturating nibbles: bits 0-3
 * independent, 4-7 reason 1, 8-11 reason 2, 12-15 reason 6, 16-19 reason 8,
 * 20-23 any other */
static inline unsigned AgxD3d12JobCrossCount(unsigned histogram, unsigned reason) {
  const unsigned slot = reason == 0u ? 0u : reason == 1u ? 1u : reason == 2u ? 2u : reason == 6u ? 3u
                        : reason == 8u ? 4u : 5u;
  if (((histogram >> (slot * 4u)) & 0xfu) != 0xfu) histogram += 1u << (slot * 4u);
  return histogram;
}

#endif
