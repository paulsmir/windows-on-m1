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
} AGX_D3D12_JOB_BARRIERS;

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
  else if (kind != 5u) ++j->ForeignSince;
}

static inline void AgxD3d12JobBarrierEnd(AGX_D3D12_JOB_BARRIERS *j) { j->Active = j->ActiveLocal = 0u; }

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
static inline void AgxD3d12JobBarrierForeign(AGX_D3D12_JOB_BARRIERS *j) { ++j->ForeignSince; }

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
  } else {
    j->EarlyDispatch = 0u;
    j->ForeignSince = 0u;
  }
  j->DispatchesPending = j->Active == 2u ? 1u : 0u;
  j->GapAfterRender = kind == 1u ? s->Count : ~0u;
  if (kind == 1u) j->RenderUav = j->UavDraws != j->UavDrawsBase[0] ? 1u : 0u;
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
}
static inline void AgxD3d12JobBarrierPoison(AGX_D3D12_JOB_BARRIERS *j) { j->Unknown = 1u; }

/* OV2: the compute batch now being submitted is independent of the render
 * batch submitted just before it under D3D12 rules: same execution, that
 * render was the previous batch, no barrier between them, no barrier but UAV
 * barriers inside the compute batch, and the render bound no UAV. Call before
 * AgxD3d12JobBarrierBatch for the compute batch. Any doubt -> 0. */
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
  if (foreign) return 6u;
  if (after->NonUav > j->NonUavSince) return 7u;
  return j->NonUavSince - after->NonUav == 0u ? 0u : 7u;
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
