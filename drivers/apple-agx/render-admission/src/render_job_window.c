#include "render_job_window.h"

/* rc95: see render_job_window.h. Refusal numbers are stable receipt
 * values. */

unsigned int AdmissionJobWindowUsesSidecar(unsigned int Kind) {
  return Kind == ADMISSION_JOB_WINDOW_KIND_COMPUTE ||
         Kind == ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE ? 1u : 0u;
}

unsigned int AdmissionJobWindowUsesManager(unsigned int Kind) {
  return Kind == ADMISSION_JOB_WINDOW_KIND_RENDER ||
         Kind == ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE ? 1u : 0u;
}

unsigned int AdmissionJobWindowCapabilities(unsigned int Depth2,
                                            unsigned int CapsSwitch) {
  return Depth2 && CapsSwitch ? ADMISSION_JOB_WINDOW_CAPS_RC98 : 0u;
}

unsigned int AdmissionJobWindowSecondSet(unsigned int Kind,
                                         unsigned int Capabilities) {
  return AdmissionJobWindowUsesSidecar(Kind) &&
         (Capabilities & ADMISSION_JOB_WINDOW_CAP_SECOND_SIDECAR) ? 1u : 0u;
}

static unsigned int window_g4(unsigned int Kind) {
  return Kind == ADMISSION_JOB_WINDOW_KIND_RENDER ||
         Kind == ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE ||
         Kind == ADMISSION_JOB_WINDOW_KIND_COMPUTE ? 1u : 0u;
}

unsigned int AdmissionJobWindowBarrier(unsigned int RunningKind,
                                       unsigned int NextKind) {
  unsigned int on_compute;
  if (!window_g4(RunningKind) || !window_g4(NextKind))
    return ADMISSION_JOB_WINDOW_BARRIER_NONE;
  /* N's last stamp: a render (with or without prelude) ends on 3D. */
  on_compute = RunningKind == ADMISSION_JOB_WINDOW_KIND_COMPUTE ? 1u : 0u;
  /* N+1 starts on the compute queue when it is a compute job or a render
   * whose TA waits for its own prelude (D3D-G1b TaLeadingBarrier). */
  if (NextKind == ADMISSION_JOB_WINDOW_KIND_RENDER)
    return on_compute ? ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE
                      : ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D;
  return on_compute ? ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE
                    : ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D;
}

static unsigned int barrier_available(unsigned int Barrier,
                                      unsigned int Capabilities) {
  switch (Barrier) {
    case ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE:
      return 1u; /* D3D-G1b: hardware-validated */
    case ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D:
      return (Capabilities & ADMISSION_JOB_WINDOW_CAP_TA_ON_3D) ? 1u : 0u;
    case ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D:
    case ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE:
      return (Capabilities & ADMISSION_JOB_WINDOW_CAP_COMPUTE_BARRIER) ? 1u
                                                                        : 0u;
    default:
      return 0u;
  }
}

unsigned int AdmissionJobWindowKickClasses(unsigned int On3d) {
  unsigned int classes = (1u << ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE) |
                         (1u << ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE);
  if (On3d)
    classes |= (1u << ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D) |
               (1u << ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D);
  return classes;
}

unsigned int AdmissionJobWindowGeneralClasses(unsigned int TaOnCompute,
    unsigned int TaOn3d, unsigned int ComputeOnCompute) {
  /* EXP2088 proves only compute-on-3D with commit-time notifier publication.
   * The other classes need independent bounded hardware gates. */
  unsigned int classes = 1u << ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D;
  if (TaOnCompute) classes |= 1u << ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE;
  if (TaOn3d) classes |= 1u << ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D;
  if (ComputeOnCompute) classes |= 1u << ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE;
  return classes;
}

unsigned int AdmissionJobWindowKickRefusal(
    const ADMISSION_JOB_WINDOW_KICK_STATE *s) {
  const ADMISSION_JOB_WINDOW_JOB *n, *m;
  if (!s) return 99u;
  if (!s->Enabled) return 1u;
  if (s->Stopping || s->Resetting) return 2u;
  /* rc63: a faulted scheduler starts nothing until the TDR engine reset. */
  if (s->Faulted) return 3u;
  /* A preemption request waits for the running job's boundary; a kicked
   * successor could not be taken back from the firmware. */
  if (s->PreemptionPhase != 0u) return 4u;
  n = &s->Running;
  m = &s->Next;
  if (s->InFlight != 1u || n->Fence == 0u) return 50u;
  if (s->WindowPoisoned) return 51u;
  if (!s->NextPrebound || m->Fence == 0u || m->Fence == n->Fence) return 52u;
  if (s->QueuedFence == 0u || s->QueuedFence != m->Fence) return 53u;
  /* A paging packet between them may change mappings N+1 was bound with. */
  if (s->PagingPending ||
      (s->CpuHeadFence != 0u && s->CpuHeadFence == m->Fence)) return 54u;
  if (n->Process == 0ULL || m->Process != n->Process) return 55u;
  if (!window_g4(n->Kind) || !window_g4(m->Kind)) return 56u;
  /* EXP2100: the proven barrier classes cover only no-prelude instances.
   * A prelude uses compute as well as TA/3D, on either side of the pair. */
  if (s->General && !s->Prelude &&
      (n->Kind == ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE ||
       m->Kind == ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE))
    return ADMISSION_JOB_WINDOW_REFUSAL_PRELUDE_UNPROVEN;
  if (AdmissionJobWindowUsesSidecar(n->Kind) &&
      AdmissionJobWindowUsesSidecar(m->Kind) &&
      !(s->Capabilities & ADMISSION_JOB_WINDOW_CAP_SECOND_SIDECAR))
    return 57u;
  if (AdmissionJobWindowUsesManager(n->Kind) &&
      AdmissionJobWindowUsesManager(m->Kind)) {
    /* Legacy admission uses SceneId. Q6 instead requires the complete
     * resident-slot key and exact pending owner; scene objects are per flight.
     * Neither path permits restoring another manager while N runs. */
    if (s->General ? !s->SameManagerSlot :
        (n->SceneId == 0ULL || m->SceneId != n->SceneId)) return 58u;
    if (!(s->Capabilities & ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER))
      return 59u;
  }
  if (!barrier_available(AdmissionJobWindowBarrier(n->Kind, m->Kind),
                         s->Capabilities))
    return 60u;
  /* rc102 (EXP2047): a class the wired window was not switched on for. */
  if (s->KickClasses != 0u &&
      !(s->KickClasses & (1u << AdmissionJobWindowBarrier(n->Kind, m->Kind))))
    return 64u;
  if (s->NextNeedsIdle) return 61u;
  if (s->SetsValid && s->RunningSet == s->NextSet) return 62u;
  return 0u;
}

unsigned int AdmissionJobWindowLeadQueue(unsigned int Barrier) {
  return Barrier == ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE ||
         Barrier == ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D
             ? ADMISSION_JOB_WINDOW_QUEUE_TA : ADMISSION_JOB_WINDOW_QUEUE_COMPUTE;
}

unsigned int AdmissionJobWindowWaitQueue(unsigned int Barrier) {
  return Barrier == ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D ||
         Barrier == ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D
             ? ADMISSION_JOB_WINDOW_QUEUE_3D : ADMISSION_JOB_WINDOW_QUEUE_COMPUTE;
}

void AdmissionJobWindowReceiptInitialize(ADMISSION_JOB_WINDOW_RECEIPT *Receipt) {
  unsigned char *bytes = (unsigned char *)Receipt;
  unsigned int i;
  if (!Receipt) return;
  for (i = 0u; i < (unsigned int)sizeof(*Receipt); ++i) bytes[i] = 0u;
  Receipt->Version = ADMISSION_JOB_WINDOW_RECEIPT_VERSION;
  Receipt->Bytes = (unsigned int)sizeof(*Receipt);
}

void AdmissionJobWindowReceiptKick(ADMISSION_JOB_WINDOW_RECEIPT *Receipt,
                                   unsigned int Fence, unsigned int Barrier,
                                   unsigned int InFlight) {
  if (!Receipt) return;
  ++Receipt->Kicks;
  Receipt->LastKickedFence = Fence;
  if (Barrier < 5u) ++Receipt->Barriers[Barrier];
  if (InFlight > Receipt->MaxInFlight) Receipt->MaxInFlight = InFlight;
}

void AdmissionJobWindowReceiptReported(ADMISSION_JOB_WINDOW_RECEIPT *Receipt,
                                       unsigned int Count) {
  if (!Receipt || Count == 0u) return;
  ++Receipt->Reported[Count < 3u ? Count : 2u];
}

void AdmissionJobWindowInitialize(ADMISSION_JOB_WINDOW *Window) {
  unsigned int i;
  if (!Window) return;
  for (i = 0u; i < ADMISSION_JOB_WINDOW_DEPTH; ++i) {
    Window->Entry[i].Fence = 0u;
    Window->Entry[i].State = ADMISSION_JOB_WINDOW_ENTRY_FREE;
  }
  Window->Count = 0u;
  Window->Poisoned = 0u;
}

int AdmissionJobWindowKick(ADMISSION_JOB_WINDOW *Window, unsigned int Fence) {
  if (!Window || Fence == 0u || Window->Count >= ADMISSION_JOB_WINDOW_DEPTH)
    return 0;
  if (Window->Count != 0u && Window->Poisoned) return 0;
  /* Fences are kicked in submission order (32-bit wrap aware). */
  if (Window->Count != 0u &&
      (Fence - Window->Entry[Window->Count - 1u].Fence) - 1u >= 0x80000000u)
    return 0;
  Window->Entry[Window->Count].Fence = Fence;
  Window->Entry[Window->Count].State = ADMISSION_JOB_WINDOW_ENTRY_KICKED;
  ++Window->Count;
  return 1;
}

int AdmissionJobWindowEnded(ADMISSION_JOB_WINDOW *Window, unsigned int Fence,
                            int Dropped) {
  unsigned int i;
  if (!Window || Fence == 0u) return 0;
  for (i = 0u; i < Window->Count; ++i) {
    if (Window->Entry[i].Fence != Fence) continue;
    if (Window->Entry[i].State != ADMISSION_JOB_WINDOW_ENTRY_KICKED) return 0;
    Window->Entry[i].State = Dropped ? ADMISSION_JOB_WINDOW_ENTRY_DROPPED
                                     : ADMISSION_JOB_WINDOW_ENTRY_ENDED;
    if (Dropped) Window->Poisoned = 1u;
    return 1;
  }
  return 0;
}

unsigned int AdmissionJobWindowReportable(const ADMISSION_JOB_WINDOW *Window,
                                          int *Dropped) {
  if (Dropped) *Dropped = 0;
  if (!Window || Window->Count == 0u) return 0u;
  if (Window->Entry[0].State == ADMISSION_JOB_WINDOW_ENTRY_ENDED)
    return Window->Entry[0].Fence;
  if (Window->Entry[0].State == ADMISSION_JOB_WINDOW_ENTRY_DROPPED) {
    if (Dropped) *Dropped = 1;
    return Window->Entry[0].Fence;
  }
  return 0u;
}

int AdmissionJobWindowReported(ADMISSION_JOB_WINDOW *Window,
                               unsigned int Fence) {
  unsigned int i;
  if (!Window || Window->Count == 0u || Fence == 0u ||
      Window->Entry[0].Fence != Fence ||
      (Window->Entry[0].State != ADMISSION_JOB_WINDOW_ENTRY_ENDED &&
       Window->Entry[0].State != ADMISSION_JOB_WINDOW_ENTRY_DROPPED))
    return 0;
  for (i = 1u; i < Window->Count; ++i) Window->Entry[i - 1u] = Window->Entry[i];
  --Window->Count;
  Window->Entry[Window->Count].Fence = 0u;
  Window->Entry[Window->Count].State = ADMISSION_JOB_WINDOW_ENTRY_FREE;
  /* A drained window may start kicking again. */
  if (Window->Count == 0u) Window->Poisoned = 0u;
  return 1;
}

unsigned int AdmissionJobWindowQuiesceFence(const ADMISSION_JOB_WINDOW *Window) {
  if (!Window || Window->Count == 0u) return 0u;
  return Window->Entry[Window->Count - 1u].Fence;
}

unsigned int AdmissionJobWindowResetAbort(ADMISSION_JOB_WINDOW *Window,
                                          unsigned int *Aborted) {
  unsigned int newest = 0u, count = 0u;
  if (Aborted) *Aborted = 0u;
  if (!Window) return 0u;
  /* Every entry still here is unreported (a reported one left the window). */
  if (Window->Count != 0u) {
    newest = Window->Entry[Window->Count - 1u].Fence;
    count = Window->Count;
  }
  AdmissionJobWindowInitialize(Window);
  if (Aborted) *Aborted = count;
  return newest;
}

unsigned int AdmissionJobWindowStampReached(unsigned int Observed,
                                            unsigned int Expected) {
  if (Expected == 0u) return 0u;
  return Observed - Expected < 0x80000000u ? 1u : 0u;
}
