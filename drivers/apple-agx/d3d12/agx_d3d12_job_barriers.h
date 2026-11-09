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
  unsigned GlobalUavSinceDispatch;
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
}

/* type 0 transition, 1 aliasing, 2 UAV (id 0 = global), 3 0022 ranged
 * (aliasing with flag 0x8); before/after: transition states, else 0 */
static inline void AgxD3d12JobBarrierAdd(AGX_D3D12_JOB_BARRIERS *j, unsigned type, unsigned flags,
                                         unsigned before, unsigned after, unsigned id) {
  AGX_D3D12_JOB_BARRIER_SET *s = &j->Open;
  unsigned i;
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
  /* OV2: command kinds 2 dispatch, 5 draw (agx_d3d12_ddi.cpp kCommand*) */
  if (kind == 2u) ++j->DispatchesPending;
  else if (kind != 5u) {
    ++j->ForeignSince;
    j->ForeignKinds |= 1u << (kind & 31u);
  }
}

static inline void AgxD3d12JobBarrierEnd(AGX_D3D12_JOB_BARRIERS *j) {
  /* OV2 ranges: a draw's accesses join its render batch, and the barriers
   * before it order the render's own draws only; a dispatch commits the
   * barriers before it (those after a compute batch's last dispatch order
   * later work, not that batch) */
  /* (an ExecuteIndirect expansion counts only when it replayed draws: a
   * dispatch expansion must not drop the barriers before it) */
  if (j->Active == 5u || j->RangeDraw.Count || j->RangeDraw.Overflow) {
    AgxD3d12JobRangeMerge(&j->RangeRender, &j->RangeDraw);
    AgxD3d12JobRangeMerge(&j->RangeRender, &j->Resolved);
    AgxD3d12JobRangeClear(&j->RangeDraw);
    AgxD3d12JobRangeClear(&j->Post);
    j->PostCommitted = j->PostCommittedOverflow = 0u;
  } else if (j->Active == 2u) {
    j->GlobalUavSinceDispatch = 0u;
    AgxD3d12JobRangeClear(&j->BarriersSinceDispatch);
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
  if (!(type == 2u && !id)) AgxD3d12JobRangeAdd(&j->BarriersSinceDispatch, heap, lo, hi, 0u);
  {
    const unsigned before = j->Post.Count;
    AgxD3d12JobRangeAdd(&j->Post, heap, lo, hi, type == 2u ? (id ? 1u : 2u) : 0u);
    if (j->Post.Count > before) j->Post.At[before] = j->DispatchesSince;
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
#endif
