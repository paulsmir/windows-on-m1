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
  s->Count = s->Transitions = s->Uav = s->GlobalUav = s->Aliasing = 0u;
  s->StateBefore = s->StateAfter = s->Flags = 0u;
  s->Ids = s->IdHash = s->IdOverflow = 0u;
  for (i = 0; i < AGX_D3D12_JOB_BARRIER_IDS; ++i) s->Id[i] = 0u;
}

static inline void AgxD3d12JobBarriersInit(AGX_D3D12_JOB_BARRIERS *j) {
  AgxD3d12JobBarrierSetClear(&j->Before);
  AgxD3d12JobBarrierSetClear(&j->Open);
  j->Active = j->Total = j->PreviousKind = 0u;
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
}

static inline void AgxD3d12JobBarrierEnd(AGX_D3D12_JOB_BARRIERS *j) { j->Active = 0u; }

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
  AgxD3d12JobBarrierSetClear(s);
  j->Total = 0u;
  j->PreviousKind = kind;
}

#endif
