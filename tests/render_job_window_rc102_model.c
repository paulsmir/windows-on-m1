/* rc102: the depth-2 job window driven through the real C state machine.
 *
 * Compiled with the real queue runtime, queue provider, backend runtime,
 * scheduler and render_job_window.c (tests/test_render_job_window.py).
 * The firmware model owns the TA, 3D and compute rings: it runs the
 * published entries strictly in ring order per queue, a Barrier entry
 * waits for its firmware stamp, a 3D work item waits for its own TA, a job
 * notifies the shared notifier per queue and the notifier raises one Flag
 * event (with every queue that notified) only when the CPU-written
 * threshold is reached; the driver stamps of every notified queue are held
 * until then (EXP2034 U4). A faulting or hung job halts the firmware and
 * sends Fault/Timeout; the resume (queue runtime Io.Recover) drops it and,
 * by variant, runs or kills the job behind it (rc95 U5).
 *
 * The KMD model is the worker of backend_platform_windows.c in miniature:
 * dispatch at depth 1, the depth-2 decision (AdmissionJobWindowKickRefusal)
 * and kick (AppleAgxSchedulerKickQueuedFence + AppleAgxBackendRuntime
 * SubmitSuccessor with the leading Barrier on the running job's last
 * stamp), the completion callback (AdmissionBackendComplete: a kicked
 * fence is adopted, i.e. activated, inside its own completion when it ends
 * in the same batch), preemption (AppleAgxSchedulerBeginBoundaryPreemption),
 * the TDR reset (quiesce by polling until neither job is held, then
 * AppleAgxSchedulerResetEngine) and VidSch (2-deep submission, resubmission
 * of never-run packets).
 *
 * Checked per scenario: W1 report order; W2 at most two jobs with the
 * firmware; W3 the pair rules; W4 nothing kicked while preempting/faulted/
 * resetting or paging, nothing reported while the scheduler is faulted
 * outside a reset; W5 every fence ends exactly once, never a kicked fence
 * handed back; W6 a reset returns with the firmware empty and
 * LastAbortedFenceId covers every unreported kicked fence; W7 no kick into
 * a poisoned window; B1 a batched pair is reported in one poll; B2 a
 * successor completes only with its own stamps or as dropped. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "apple_agx_g13_queue_provider.h"
#include "apple_agx_backend_runtime.h"
#include "apple_agx_scheduler.h"
#include "render_job_window.h"

#define K_R ADMISSION_JOB_WINDOW_KIND_RENDER
#define K_RP ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE
#define K_C ADMISSION_JOB_WINDOW_KIND_COMPUTE
enum { OUT_OK, OUT_FAULT, OUT_HANG };
enum { Q_TA, Q_3D, Q_CO, NQ };
enum { IT_WORK = 1, IT_BARRIER, IT_INITBM };
#define MAXF 64
#define MAXI 1024
#define RING 64u
#define FW_TIMEOUT 40u
#define TDR_TICKS 25u
#define RESET_BOUND 200u
static const unsigned EVENT_NUMBER[NQ] = {7u, 8u, 9u};

typedef struct { unsigned kind, outcome, dur; unsigned long long proc; } jd;
typedef struct {
  unsigned type, queue, fence, wait_queue, wait_value, dur, outcome, notify;
  unsigned stamp, started, start, killed;
} item;

typedef struct world {
  const char *name;
  /* firmware */
  APPLE_AGX_BACKEND_U64 ring[NQ][RING];
  volatile APPLE_AGX_BACKEND_U32 write[NQ], done[NQ], stamp[NQ];
  unsigned fwstamp[NQ], held[NQ], held_valid[NQ];
  unsigned threshold, count, pending_bits;
  unsigned halted, fault_fence, fault_kind, timeout_counter;
  item items[MAXI]; unsigned nitems;
  unsigned char events[64][APPLE_AGX_G13_EVENT_MESSAGE_SIZE]; unsigned nev, evr;
  int runs_after_drop;
  /* KMD */
  APPLE_AGX_G13_QUEUE_PROVIDER prov;
  APPLE_AGX_BACKEND_RUNTIME be;
  APPLE_AGX_SCHEDULER sch;
  ADMISSION_JOB_WINDOW win;
  ADMISSION_JOB_WINDOW_RECEIPT rec;
  unsigned char arena[64], dma[16];
  unsigned notified_total; /* threshold the builds wrote */
  unsigned depth2;          /* Wom1JobDepth2Kick */
  unsigned caps;
  unsigned faulted, resetting, preempt, paging, prebound_at;
  unsigned in_reset_quiesce;
  unsigned reports_this_poll, max_batch;
  /* VidSch */
  jd plan[MAXF]; unsigned nplan, nexplan;
  jd job[MAXF + 1]; unsigned nsub;
  unsigned final_[MAXF + 1], kicked[MAXF + 1], kicked_behind[MAXF + 1];
  unsigned rep[MAXF], nr, repd[MAXF + 1], resub[MAXF + 1], aborted[MAXF + 1];
  unsigned kick_tick[MAXF + 1];
  unsigned now, max_fw, resets, last_aborted;
  unsigned gate; /* VidSch depth: fence n is submitted once n-gate ended */
  void (*event)(struct world *);
  unsigned violations;
} world;
static world W;

static void violate(world *w, const char *what, unsigned f) {
  printf("VIOLATION [%s] %s fence %u\n", w->name, what, f);
  ++w->violations;
}

/* ---- firmware --------------------------------------------------------- */
static unsigned long long item_address(unsigned index) {
  return 0x1500000000ULL + (unsigned long long)index * 0x40ULL;
}
static item *item_at(world *w, unsigned long long address) {
  unsigned long long index = (address - 0x1500000000ULL) / 0x40ULL;
  return index < w->nitems ? &w->items[index] : NULL;
}
static unsigned new_item(world *w, unsigned type, unsigned queue, unsigned fence) {
  item *it = &w->items[w->nitems];
  memset(it, 0, sizeof(*it));
  it->type = type; it->queue = queue; it->fence = fence; it->stamp = fence;
  it->outcome = OUT_OK;
  return w->nitems++;
}
static int reached(unsigned observed, unsigned expected) {
  return expected != 0u && observed - expected < 0x80000000u;
}
static void put32(unsigned char *p, unsigned v) {
  p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static void put64(unsigned char *p, unsigned long long v) {
  put32(p, (unsigned)v); put32(p + 4, (unsigned)(v >> 32));
}
static void send_event(world *w, unsigned kind, unsigned long long bits) {
  unsigned char *m = w->events[w->nev++ % 64u];
  memset(m, 0, APPLE_AGX_G13_EVENT_MESSAGE_SIZE);
  put32(m, kind);
  if (kind == (unsigned)AppleAgxG13EventFlag) { put64(m + 4, bits); put64(m + 12, 0ULL); }
  if (kind == (unsigned)AppleAgxG13EventTimeout) { put64(m + 4, ++w->timeout_counter); put32(m + 12, 0u); }
}
static void notify(world *w, unsigned q, unsigned stamp) {
  ++w->count;
  w->pending_bits |= 1u << q;
  if (stamp) { w->held[q] = stamp; w->held_valid[q] = 1; }
  if (w->count >= w->threshold) {
    unsigned long long bits = 0ULL;
    unsigned i;
    for (i = 0; i < NQ; ++i) {
      if (w->held_valid[i]) { w->stamp[i] = w->held[i]; w->held_valid[i] = 0; }
      if (w->pending_bits & (1u << i)) bits |= 1ULL << EVENT_NUMBER[i];
    }
    w->pending_bits = 0;
    send_event(w, (unsigned)AppleAgxG13EventFlag, bits);
  }
}
static unsigned in_firmware(world *w) {
  unsigned q, seen[MAXF + 1], n = 0;
  memset(seen, 0, sizeof(seen));
  for (q = 0; q < NQ; ++q) {
    unsigned i;
    for (i = w->done[q]; i != w->write[q]; i = (i + 1u) % RING) {
      item *it = item_at(w, w->ring[q][i]);
      if (it && !seen[it->fence]) { seen[it->fence] = 1; ++n; }
    }
  }
  return n;
}
static void firmware(world *w) {
  unsigned q, progress = 1;
  if (w->halted) return;
  while (progress) {
    progress = 0;
    for (q = 0; q < NQ; ++q) {
      item *it;
      if (w->done[q] == w->write[q]) continue;
      it = item_at(w, w->ring[q][w->done[q]]);
      if (!it) { violate(w, "firmware: unknown ring entry", 0); w->halted = 1; return; }
      if (it->killed) {
        /* a killed command still counts at the notifier (no stamp) */
        w->done[q] = (w->done[q] + 1u) % RING; progress = 1;
        if (it->notify) notify(w, q, 0u);
        continue;
      }
      if (it->type == IT_INITBM) { w->done[q] = (w->done[q] + 1u) % RING; progress = 1; continue; }
      if (it->type == IT_BARRIER || (it->type == IT_WORK && it->wait_value)) {
        if (!reached(w->fwstamp[it->wait_queue], it->wait_value)) continue;
        if (it->type == IT_BARRIER) {
          w->done[q] = (w->done[q] + 1u) % RING; progress = 1; continue;
        }
      }
      if (!it->started) { it->started = 1; it->start = w->now; }
      if (it->outcome == OUT_FAULT && w->now - it->start >= 1u) {
        w->halted = 1; w->fault_fence = it->fence; w->fault_kind = (unsigned)AppleAgxG13EventFault;
        send_event(w, (unsigned)AppleAgxG13EventFault, 0ULL);
        return;
      }
      if (it->outcome == OUT_HANG) {
        if (w->now - it->start >= FW_TIMEOUT) {
          w->halted = 1; w->fault_fence = it->fence; w->fault_kind = (unsigned)AppleAgxG13EventTimeout;
          send_event(w, (unsigned)AppleAgxG13EventTimeout, 0ULL);
          return;
        }
        continue;
      }
      if (w->now - it->start < it->dur) continue;
      w->fwstamp[q] = it->stamp;
      w->done[q] = (w->done[q] + 1u) % RING;
      if (it->notify) notify(w, q, it->stamp);
      progress = 1;
    }
  }
}
/* Asahi recover(): the culprit's entries are dropped (done advances, no
 * stamps); behind it the successor runs (its Barrier sees the dropped
 * job's firmware stamp) or is killed with it (U5 variants). */
static APPLE_AGX_BACKEND_BOOL fw_recover(void *ctx, APPLE_AGX_BACKEND_U32 fence,
                                         APPLE_AGX_BACKEND_U32 kind) {
  world *w = ctx;
  unsigned i, victim = w->fault_fence, next = 0;
  (void)fence; (void)kind;
  if (!w->halted) return APPLE_AGX_BACKEND_TRUE;
  for (i = 0; i < w->nitems; ++i)
    if (w->items[i].fence > victim && (!next || w->items[i].fence < next)) next = w->items[i].fence;
  for (i = 0; i < w->nitems; ++i) {
    item *it = &w->items[i];
    if (it->fence == victim) {
      it->killed = 1;
      if (w->runs_after_drop && it->type == IT_WORK) {
        /* the killed job's firmware stamp releases its successor */
        if (reached(it->stamp, w->fwstamp[it->queue]) || !w->fwstamp[it->queue]) w->fwstamp[it->queue] = it->stamp;
      }
    }
    if (!w->runs_after_drop && next && it->fence == next) it->killed = 1;
  }
  w->halted = 0; w->fault_fence = 0;
  return APPLE_AGX_BACKEND_TRUE;
}

/* ---- queue runtime IO -------------------------------------------------- */
static APPLE_AGX_BACKEND_BOOL io_flush(void *c, const void *a, APPLE_AGX_BACKEND_U32 b) {
  (void)c; return a != NULL && b != 0u;
}
static void io_barrier(void *c) { (void)c; }
static APPLE_AGX_BACKEND_BOOL io_publish(void *c, volatile APPLE_AGX_BACKEND_U32 *a,
                                         APPLE_AGX_BACKEND_U32 v) {
  (void)c; *a = v; return APPLE_AGX_BACKEND_TRUE;
}
static APPLE_AGX_BACKEND_BOOL io_read(void *c, const volatile APPLE_AGX_BACKEND_U32 *a,
                                      APPLE_AGX_BACKEND_U32 *v) {
  (void)c; if (!a || !v) return APPLE_AGX_BACKEND_FALSE; *v = *a; return APPLE_AGX_BACKEND_TRUE;
}
static APPLE_AGX_BACKEND_BOOL io_send(void *c, APPLE_AGX_BACKEND_U32 q,
                                      const unsigned char m[APPLE_AGX_G13_RUN_MESSAGE_SIZE]) {
  (void)c; (void)q; (void)m; return APPLE_AGX_BACKEND_TRUE;
}
static APPLE_AGX_BACKEND_BOOL io_quiesce(void *c, APPLE_AGX_BACKEND_U32 f) {
  world *w = c; violate(w, "queue quiesce (terminal failure)", f); return APPLE_AGX_BACKEND_TRUE;
}

/* The platform's BuildSubmission (apple_agx_platform_provider.c). */
static APPLE_AGX_BACKEND_BOOL build_submission(void *c, const APPLE_AGX_BACKEND_JOB_IMAGE *j,
    APPLE_AGX_BACKEND_U32 fence, APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *s) {
  world *w = c; unsigned i;
  memset(s, 0, sizeof(*s));
  s->Fence = fence; s->NowTicks = 1000ULL + w->now;
  if (j->Kind == AppleAgxBackendJobCompute) {
    s->Compute.PreparedRanges[0].Address = w->arena; s->Compute.PreparedRanges[0].Bytes = 64u;
    s->Compute.PreparedRangeCount = 1u;
    s->Compute.GpuAddresses[0] = j->ComputeWorkAddresses[0]; s->Compute.GpuAddressCount = 1u;
    s->Compute.ExpectedStamp = j->ComputeExpectedStamp;
  } else {
    for (i = 0; i < 2u; ++i) {
      s->Ta.GpuAddresses[i] = j->TaWorkAddresses[i];
      s->Ta.PreparedRanges[i].Address = w->arena; s->Ta.PreparedRanges[i].Bytes = 64u;
      s->D3.GpuAddresses[i] = j->D3WorkAddresses[i];
      s->D3.PreparedRanges[i].Address = w->arena; s->D3.PreparedRanges[i].Bytes = 64u;
    }
    s->Ta.GpuAddressCount = s->Ta.PreparedRangeCount = 2u;
    s->D3.GpuAddressCount = s->D3.PreparedRangeCount = 2u;
    s->Ta.ExpectedStamp = j->TaExpectedStamp; s->D3.ExpectedStamp = j->D3ExpectedStamp;
    if (j->ComputeWorkAddressCount) {
      s->Compute.GpuAddresses[0] = j->ComputeWorkAddresses[0]; s->Compute.GpuAddressCount = 1u;
      s->Compute.PreparedRanges[0].Address = w->arena; s->Compute.PreparedRanges[0].Bytes = 64u;
      s->Compute.PreparedRangeCount = 1u;
      s->Compute.ExpectedStamp = j->ComputeExpectedStamp;
    }
  }
  if (j->LeadBarrierAddress) {
    s->LeadBarrierRange.Address = w->arena; s->LeadBarrierRange.Bytes = 64u;
  }
  return APPLE_AGX_BACKEND_TRUE;
}

/* ---- backend IO (the KMD) ---------------------------------------------- */
static APPLE_AGX_BOOL be_map(void *c, void **cpu, APPLE_AGX_U64 *gpu, APPLE_AGX_U32 *b) {
  world *w = c; *cpu = w->arena; *gpu = 0x1400000000ULL; *b = 64u; return APPLE_AGX_TRUE;
}
static APPLE_AGX_BOOL be_unmap(void *c, void *cpu, APPLE_AGX_U32 b) { (void)c; (void)cpu; (void)b; return APPLE_AGX_TRUE; }
static APPLE_AGX_BOOL be_resolve(void *c, const APPLE_AGX_BACKEND_SUBMISSION *s,
                                 const unsigned char **bytes, APPLE_AGX_U32 *count) {
  world *w = c; (void)s; *bytes = w->dma; *count = 16u; return APPLE_AGX_TRUE;
}
static APPLE_AGX_BOOL be_flush(void *c, const void *a, APPLE_AGX_U32 b) { (void)c; (void)a; (void)b; return APPLE_AGX_TRUE; }
static APPLE_AGX_BOOL be_acquire(void *c, void *a, APPLE_AGX_U32 b, APPLE_AGX_RENDER_TEMPLATE_ROOTS *r) {
  (void)c; (void)a; (void)b; (void)r; return APPLE_AGX_TRUE;
}
static unsigned job_notifications(unsigned kind) {
  return kind == K_C ? 1u : kind == K_RP ? 3u : 2u;
}
/* The KMD build (AdmissionExternalBuildJob): work items, expected stamps
 * and done pointers from the provider's plan, the threshold, and for a
 * successor its leading Barrier on the running job's last firmware stamp
 * (AdmissionJobWindowBarrier / LeadQueue / WaitQueue). */
static APPLE_AGX_BOOL be_relocate(void *c, void *a, APPLE_AGX_U32 ab,
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *r, const unsigned char *bytes, APPLE_AGX_U32 n,
    const APPLE_AGX_BACKEND_SUBMISSION *s, APPLE_AGX_BACKEND_JOB_IMAGE *j) {
  world *w = c;
  unsigned fence = s->Submission.Fence, kind = w->job[fence].kind;
  APPLE_AGX_G13_QUEUE_JOB_PLAN plan;
  unsigned successor = w->prov.PendingFence != 0u;
  unsigned ta, d3a, d3b;
  (void)a; (void)ab; (void)r; (void)bytes; (void)n;
  memset(&plan, 0, sizeof(plan));
  if (!(successor ? AppleAgxG13QueueProviderPlanSuccessor(&w->prov, &plan)
                  : AppleAgxG13QueueProviderPlanJob(&w->prov, &plan)))
    return APPLE_AGX_FALSE;
  memset(j, 0, sizeof(*j));
  if (kind == K_C) {
    unsigned co = new_item(w, IT_WORK, Q_CO, fence);
    w->items[co].dur = w->job[fence].dur; w->items[co].notify = 1;
    w->items[co].outcome = w->job[fence].outcome;
    j->Kind = AppleAgxBackendJobCompute;
    j->ComputeWorkAddresses[0] = item_address(co); j->ComputeWorkAddressCount = 1u;
    j->ComputeEvent = EVENT_NUMBER[Q_CO]; j->ComputeExpectedStamp = fence;
    j->ComputeExpectedDonePointer = plan.ComputeExpectedDonePointer;
  } else {
    unsigned initbm = new_item(w, IT_INITBM, Q_TA, fence);
    ta = new_item(w, IT_WORK, Q_TA, fence);
    d3a = new_item(w, IT_WORK, Q_3D, fence);
    d3b = new_item(w, IT_WORK, Q_3D, fence);
    w->items[ta].dur = 2u; w->items[ta].notify = 1;
    w->items[ta].outcome = w->job[fence].outcome;
    w->items[d3a].dur = w->job[fence].dur; w->items[d3a].wait_queue = Q_TA;
    w->items[d3a].wait_value = fence;
    w->items[d3b].notify = 1;
    j->Kind = AppleAgxBackendJobRender;
    j->TaWorkAddresses[0] = item_address(initbm); j->TaWorkAddresses[1] = item_address(ta);
    j->D3WorkAddresses[0] = item_address(d3a); j->D3WorkAddresses[1] = item_address(d3b);
    j->TaWorkAddressCount = j->D3WorkAddressCount = 2u;
    j->TaEvent = EVENT_NUMBER[Q_TA]; j->D3Event = EVENT_NUMBER[Q_3D];
    j->TaExpectedStamp = j->D3ExpectedStamp = fence;
    j->TaExpectedDonePointer = plan.TaExpectedDonePointer;
    j->D3ExpectedDonePointer = plan.D3ExpectedDonePointer;
    if (kind == K_RP) {
      unsigned co = new_item(w, IT_WORK, Q_CO, fence);
      unsigned bar = new_item(w, IT_BARRIER, Q_TA, fence);
      w->items[co].dur = 1u; w->items[co].notify = 1;
      w->items[bar].wait_queue = Q_CO; w->items[bar].wait_value = fence;
      j->ComputeWorkAddresses[0] = item_address(co); j->ComputeWorkAddressCount = 1u;
      j->ComputeEvent = EVENT_NUMBER[Q_CO]; j->ComputeExpectedStamp = fence;
      j->ComputeExpectedDonePointer = plan.ComputeExpectedDonePointer;
      j->TaBarrierAddress = item_address(bar);
      j->TaExpectedDonePointer = plan.TaBarrierExpectedDonePointer;
    }
  }
  if (successor) {
    const APPLE_AGX_BACKEND_JOB_IMAGE *head = &w->be.PendingJob;
    unsigned head_kind = head->Kind == AppleAgxBackendJobCompute ? K_C :
        head->ComputeWorkAddressCount ? K_RP : K_R;
    unsigned barrier = AdmissionJobWindowBarrier(head_kind, kind);
    unsigned lead = AdmissionJobWindowLeadQueue(barrier);
    unsigned wait = AdmissionJobWindowWaitQueue(barrier);
    unsigned b = new_item(w, IT_BARRIER, lead == ADMISSION_JOB_WINDOW_QUEUE_TA ? Q_TA : Q_CO, fence);
    if (barrier == ADMISSION_JOB_WINDOW_BARRIER_NONE) return APPLE_AGX_FALSE;
    w->items[b].wait_queue = wait == ADMISSION_JOB_WINDOW_QUEUE_3D ? Q_3D : Q_CO;
    w->items[b].wait_value = wait == ADMISSION_JOB_WINDOW_QUEUE_3D ?
        head->D3ExpectedStamp : head->ComputeExpectedStamp;
    j->LeadBarrierAddress = item_address(b);
    AdmissionJobWindowReceiptKick(&w->rec, fence, barrier, 2u);
  }
  /* Asahi run(): the threshold counts every kicked job's notifications. */
  w->notified_total += job_notifications(kind);
  w->threshold = w->notified_total;
  return APPLE_AGX_TRUE;
}
static APPLE_AGX_BOOL be_publish(void *c) { (void)c; return APPLE_AGX_TRUE; }

static void report(world *w, unsigned f, int dropped) {
  if (w->faulted && !w->in_reset_quiesce) violate(w, "W4 report while faulted", f);
  if (w->final_[f]) violate(w, "W5 fence ends twice", f);
  if (w->nr && f < w->rep[w->nr - 1]) violate(w, "W1 report order", f);
  w->rep[w->nr++] = f; w->repd[f] = dropped; w->final_[f] = 1;
  ++w->reports_this_poll;
}
/* AdmissionBackendComplete: a kicked successor whose end came in the
 * running job's batch is adopted (activated) here; then the ordinary
 * scheduler completion and the DMA_COMPLETED / DMA_PAGE_FAULTED report. */
static APPLE_AGX_BOOL be_complete(void *c, APPLE_AGX_U32 fence, APPLE_AGX_U32 node,
                                  APPLE_AGX_U32 engine, APPLE_AGX_BACKEND_COMPLETION_STATUS st) {
  world *w = c;
  int dropped = st == AppleAgxBackendCompletionDropped;
  if (AppleAgxSchedulerActiveFence(&w->sch, 0u, 0u) == 0u &&
      AppleAgxSchedulerKickedFence(&w->sch) == fence) {
    if (!AppleAgxSchedulerActivateFence(&w->sch, 0u, 0u, fence)) {
      violate(w, "adoption refused", fence); return APPLE_AGX_FALSE;
    }
    ++w->rec.Adopted;
  }
  if (AppleAgxSchedulerActiveFence(&w->sch, node, engine) != fence) {
    violate(w, "completion of a fence that is not active", fence); return APPLE_AGX_FALSE;
  }
  if (!(dropped ? AppleAgxSchedulerFaultActiveFence(&w->sch, node, engine, fence)
                : AppleAgxSchedulerCompleteActiveFence(&w->sch, node, engine, fence))) {
    violate(w, "scheduler refused the completion", fence); return APPLE_AGX_FALSE;
  }
  if (!AdmissionJobWindowEnded(&w->win, fence, dropped) ||
      AdmissionJobWindowReportable(&w->win, NULL) != fence ||
      !AdmissionJobWindowReported(&w->win, fence))
    violate(w, "window out of order", fence);
  report(w, fence, dropped);
  if (AppleAgxSchedulerPreemptionPhase(&w->sch) == AppleAgxPreemptionWaitCurrentBoundary)
    (void)AppleAgxSchedulerObserveBoundaryCompletion(&w->sch, node, engine, fence);
  return APPLE_AGX_TRUE;
}
static APPLE_AGX_BOOL be_retire(void *c, APPLE_AGX_U32 f, APPLE_AGX_U32 n, APPLE_AGX_U32 e) {
  world *w = c; (void)n; (void)e; violate(w, "teardown retire", f); return APPLE_AGX_TRUE;
}

static void init(world *w, const char *name) {
  APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG config;
  APPLE_AGX_G13_QUEUE_RUNTIME_IO rio;
  APPLE_AGX_G13_QUEUE_PROVIDER_IO pio;
  APPLE_AGX_BACKEND_IO io;
  unsigned q;
  memset(w, 0, sizeof(*w));
  w->name = name; w->depth2 = 1; w->runs_after_drop = 1; w->gate = 2;
  w->caps = ADMISSION_JOB_WINDOW_CAPS_RC98;
  memset(&config, 0, sizeof(config));
  for (q = 0; q < NQ; ++q) {
    APPLE_AGX_G13_QUEUE_BINDING *b = q == Q_TA ? &config.Ta : q == Q_3D ? &config.D3 : &config.Compute;
    b->QueueType = q == Q_TA ? (unsigned)AppleAgxG13QueueTa : q == Q_3D ?
        (unsigned)AppleAgxG13Queue3d : (unsigned)AppleAgxG13QueueCompute;
    b->QueueInfoGpuAddress = 0x1600000000ULL + q * 0x1000ULL;
    b->RingCpuAddress = w->ring[q]; b->RingCapacity = RING;
    b->CpuWritePointer = &w->write[q]; b->GpuDonePointer = &w->done[q];
    b->Stamp = &w->stamp[q]; b->EventNumber = EVENT_NUMBER[q];
  }
  config.HasCompute = APPLE_AGX_BACKEND_TRUE; config.TimeoutTicks = 1000000ULL;
  memset(&rio, 0, sizeof(rio));
  rio.Context = w; rio.FlushForDevice = io_flush; rio.MemoryBarrier = io_barrier;
  rio.PublishU32 = io_publish; rio.ReadU32 = io_read; rio.SendRunMessage = io_send;
  rio.Quiesce = io_quiesce; rio.Recover = fw_recover;
  pio.Context = w; pio.BuildSubmission = build_submission;
  if (!AppleAgxG13QueueProviderInitialize(&w->prov, &config, &rio, &pio))
    violate(w, "provider init", 0);
  memset(&io, 0, sizeof(io));
  io.Context = &w->prov;
  (void)AppleAgxG13QueueProviderInstall(&io);
  if (!io.Queues.Create(&w->prov)) violate(w, "queue create", 0);
  AppleAgxBackendRuntimeInitialize(&w->be, 1ULL);
  w->be.Io.Context = w;
  w->be.Io.Memory.Map = be_map; w->be.Io.Memory.Unmap = be_unmap;
  w->be.Io.Memory.Resolve = be_resolve; w->be.Io.Memory.FlushForDevice = be_flush;
  w->be.Io.Memory.FlushForCpu = be_flush;
  w->be.Io.Image.AcquirePrepared = be_acquire; w->be.Io.Image.Relocate = be_relocate;
  w->be.Io.RenderContext.Publish = be_publish; w->be.Io.RenderContext.Unpublish = be_publish;
  w->be.Io.Queues = io.Queues;
  w->be.Io.Complete = be_complete; w->be.Io.Retire = be_retire;
  /* The queue functions take the provider as their context. */
  w->be.ArenaCpuAddress = w->arena; w->be.ArenaGpuAddress = 0x1400000000ULL;
  w->be.ArenaBytes = 64u; w->be.ArenaMapped = APPLE_AGX_TRUE;
  w->be.QueuesCreated = APPLE_AGX_TRUE; w->be.ContextPublished = APPLE_AGX_TRUE;
  w->be.Phase = AppleAgxBackendRuntimeReady;
  AppleAgxSchedulerInitialize(&w->sch);
  AdmissionJobWindowInitialize(&w->win);
  AdmissionJobWindowReceiptInitialize(&w->rec);
}
static void retarget(world *w);

/* The backend's Io.Context is the world (the KMD's runtime); the queue
 * functions it calls need the provider: the KMD delegates (AdmissionQueues*)
 * re-target them, as here. */
static APPLE_AGX_BOOL q_run3d(void *c, const APPLE_AGX_BACKEND_JOB_IMAGE *j, APPLE_AGX_U32 f);
static APPLE_AGX_BACKEND_IO PROVIDER_QUEUES;
#define DELEGATE(name, member) \
  static APPLE_AGX_BOOL name(void *c, const APPLE_AGX_BACKEND_JOB_IMAGE *j, APPLE_AGX_U32 f) { \
    world *w = c; return PROVIDER_QUEUES.Queues.member(&w->prov, j, f); }
DELEGATE(q_run3d, Run3d)
DELEGATE(q_runta, RunTa)
DELEGATE(q_runcompute, RunCompute)
DELEGATE(q_runsuccessor, RunSuccessor)
#undef DELEGATE
static APPLE_AGX_BOOL q_stop(void *c, APPLE_AGX_U32 f) { world *w = c; return PROVIDER_QUEUES.Queues.Stop(&w->prov, f); }
static APPLE_AGX_BOOL q_reset(void *c, APPLE_AGX_U32 f) { world *w = c; return PROVIDER_QUEUES.Queues.Reset(&w->prov, f); }
static void retarget(world *w) {
  PROVIDER_QUEUES.Queues = w->be.Io.Queues;
  w->be.Io.Queues.Run3d = q_run3d; w->be.Io.Queues.RunTa = q_runta;
  w->be.Io.Queues.RunCompute = q_runcompute; w->be.Io.Queues.RunSuccessor = q_runsuccessor;
  w->be.Io.Queues.Stop = q_stop; w->be.Io.Queues.Reset = q_reset;
}

static APPLE_AGX_BACKEND_SUBMISSION submission_for(world *w, unsigned fence) {
  APPLE_AGX_BACKEND_SUBMISSION s;
  memset(&s, 0, sizeof(s));
  s.Submission.Kind = AppleAgxSubmissionGdi; s.Submission.Fence = fence;
  s.Submission.DmaBytes = 16u; s.ContextIdentity = 1ULL;
  s.PrivateData = w->dma; s.PrivateDataBytes = 16u; s.PrivateDataEnd = 16u;
  s.DmaSubmissionEnd = 16u;
  return s;
}

/* ---- KMD worker -------------------------------------------------------- */
static void apply(world *w, APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *b) {
  /* AppleAgxPlatformComposerApplyEvent */
  while (b->AppliedObservationCount < b->ObservationCount) {
    if (AppleAgxBackendRuntimeObserve(&w->be, &b->Observations[b->AppliedObservationCount]) !=
        AppleAgxBackendRuntimeResultOk) {
      violate(w, "observation refused", b->CompletedFence); w->faulted = 1; return;
    }
    ++b->AppliedObservationCount;
  }
}
static void poll(world *w) {
  APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH b;
  w->reports_this_poll = 0;
  while (w->evr != w->nev) {
    unsigned char *m = w->events[w->evr++ % 64u];
    if (w->prov.Phase != AppleAgxG13QueueProviderSubmitted &&
        w->prov.Phase != AppleAgxG13QueueProviderComputeOnlySubmitted) continue;
    if (!AppleAgxG13QueueProviderIngestEvent(&w->prov, m, APPLE_AGX_G13_EVENT_MESSAGE_SIZE, &b)) {
      violate(w, "event ingest failed", w->prov.PendingFence); w->faulted = 1; return;
    }
    apply(w, &b);
  }
  if (w->be.Phase == AppleAgxBackendRuntimeSubmitted) {
    if (!AppleAgxG13QueueProviderCheckTimeout(&w->prov, 1000ULL + w->now, &b)) {
      violate(w, "timeout check failed", w->prov.PendingFence); w->faulted = 1; return;
    }
    apply(w, &b);
  }
  if (w->reports_this_poll > w->max_batch) w->max_batch = w->reports_this_poll;
  if (w->reports_this_poll) AdmissionJobWindowReceiptReported(&w->rec, w->reports_this_poll);
}
/* The worker after the running job's completion: a kicked successor that
 * did not end with it becomes the running job (the adoption). */
static void adopt(world *w) {
  unsigned kicked = AppleAgxSchedulerKickedFence(&w->sch);
  if (!kicked || AppleAgxSchedulerActiveFence(&w->sch, 0u, 0u) != 0u) return;
  if (w->be.Phase != AppleAgxBackendRuntimeSubmitted ||
      w->be.PendingSubmission.Submission.Fence != kicked) {
    violate(w, "kicked fence lost by the backend", kicked); return;
  }
  if (!AppleAgxSchedulerActivateFence(&w->sch, 0u, 0u, kicked)) violate(w, "adoption refused", kicked);
  else ++w->rec.Adopted;
  w->prebound_at = 0;
}

static void check_pair(world *w, unsigned a, unsigned b) {
  if (w->job[a].proc != w->job[b].proc) violate(w, "W3 two processes in the firmware", b);
  if (AdmissionJobWindowUsesManager(w->job[a].kind) && AdmissionJobWindowUsesManager(w->job[b].kind))
    violate(w, "W3 two manager bindings live", b);
}
static void kick_successor(world *w) {
  ADMISSION_JOB_WINDOW_KICK_STATE s;
  APPLE_AGX_BACKEND_SUBMISSION sub;
  unsigned n = AppleAgxSchedulerActiveFence(&w->sch, 0u, 0u);
  unsigned m = AppleAgxSchedulerQueuedFence(&w->sch, 0u, 0u);
  if (!w->depth2 || !n || !m || w->be.SuccessorPending || AppleAgxSchedulerKickedFence(&w->sch))
    return;
  if (w->prebound_at == 0 || w->prebound_at > w->now) {
    if (w->prebound_at == 0) w->prebound_at = w->now + 1;
    return;
  }
  memset(&s, 0, sizeof(s));
  s.Enabled = 1; s.Resetting = w->resetting; s.Faulted = w->faulted;
  s.PreemptionPhase = (unsigned)AppleAgxSchedulerPreemptionPhase(&w->sch);
  s.PagingPending = w->paging; s.QueuedFence = m;
  s.InFlight = w->win.Count; s.WindowPoisoned = w->win.Poisoned;
  s.Capabilities = w->caps; s.NextPrebound = 1;
  s.Running.Fence = n; s.Running.Kind = w->job[n].kind; s.Running.Process = w->job[n].proc;
  s.Next.Fence = m; s.Next.Kind = w->job[m].kind; s.Next.Process = w->job[m].proc;
  s.Running.SceneId = s.Next.SceneId = 1;
  s.NextNeedsIdle = w->job[m].kind != K_C &&
      (!w->prov.Runtime.BufferManagerInitialized || w->prov.Runtime.TaFirstRun);
  if (AdmissionJobWindowKickRefusal(&s) != 0u) return;
  if (w->paging || w->preempt || w->faulted || w->resetting)
    violate(w, "W4 kick while paging/preempting/faulted/resetting", m);
  if (w->win.Poisoned) violate(w, "W7 kick into a poisoned window", m);
  if (!AppleAgxSchedulerKickQueuedFence(&w->sch, 0u, 0u, m)) { violate(w, "scheduler refused the kick", m); return; }
  sub = submission_for(w, m);
  if (AppleAgxBackendRuntimeSubmitSuccessor(&w->be, &sub) != AppleAgxBackendRuntimeResultOk) {
    violate(w, "successor submission failed", m); w->faulted = 1; return;
  }
  check_pair(w, n, m);
  if (!AdmissionJobWindowKick(&w->win, m)) violate(w, "window refused the kick", m);
  w->kicked[m] = 1; w->kicked_behind[m] = 1; w->kick_tick[m] = w->now;
  if (w->win.Count > w->max_fw) w->max_fw = w->win.Count;
}
static void dispatch(world *w) {
  APPLE_AGX_BACKEND_SUBMISSION sub;
  unsigned m = AppleAgxSchedulerQueuedFence(&w->sch, 0u, 0u);
  if (!m || AppleAgxSchedulerActiveFence(&w->sch, 0u, 0u) ||
      w->be.Phase != AppleAgxBackendRuntimeReady || w->faulted || w->resetting ||
      AppleAgxSchedulerDispatchBlocked(&w->sch))
    return;
  if (!AppleAgxSchedulerActivateFence(&w->sch, 0u, 0u, m)) { violate(w, "activation refused", m); return; }
  sub = submission_for(w, m);
  if (AppleAgxBackendRuntimeSubmit(&w->be, &sub) != AppleAgxBackendRuntimeResultOk) {
    violate(w, "submission failed", m); w->faulted = 1; return;
  }
  if (!AdmissionJobWindowKick(&w->win, m)) violate(w, "window refused the depth-1 kick", m);
  w->kicked[m] = 1; w->kick_tick[m] = w->now; w->prebound_at = 0;
  if (w->win.Count > w->max_fw) w->max_fw = w->win.Count;
}
static void kmd(world *w) {
  if (w->faulted || w->resetting) return;
  poll(w);
  adopt(w);
  kick_successor(w);
  dispatch(w);
}

/* ---- VidSch, preemption, reset ----------------------------------------- */
static void add(world *w, unsigned kind, unsigned outcome, unsigned long long proc) {
  jd *j = &w->plan[w->nplan++];
  j->kind = kind; j->outcome = outcome; j->proc = proc; j->dur = kind == K_C ? 3u : 5u;
}
static unsigned finished(world *w) {
  unsigned f, n = 0; for (f = 1; f <= w->nsub; ++f) n += w->final_[f] != 0; return n;
}
static void vidsch(world *w) {
  while (w->nexplan < w->nplan && w->nsub + 1 <= finished(w) + w->gate && !w->preempt &&
         !w->resetting && w->nsub < MAXF) {
    w->job[++w->nsub] = w->plan[w->nexplan++];
    if (!AppleAgxSchedulerQueueFence(&w->sch, 0u, 0u, w->nsub)) violate(w, "queue refused", w->nsub);
  }
}
static void resubmit(world *w, unsigned f) {
  if (w->kicked[f]) violate(w, "W5 a kicked job is handed back to VidSch", f);
  if (w->final_[f]) violate(w, "W5 resubmit of an ended fence", f);
  w->resub[f] = 1; w->final_[f] = 3;
  if (w->nplan < MAXF) w->plan[w->nplan++] = w->job[f];
}
static void preempt_complete(world *w) {
  APPLE_AGX_PREEMPTION p;
  unsigned i, f;
  if (!w->preempt || AppleAgxSchedulerPreemptionPhase(&w->sch) != AppleAgxPreemptionReadyToNotify)
    return;
  if (!AppleAgxSchedulerClaimBoundaryPreemption(&w->sch, &p) ||
      !AppleAgxSchedulerCommitBoundaryPreemption(&w->sch, p.PreemptionFence))
    violate(w, "preemption notification", 0);
  for (i = 0; i < w->sch.PreemptedFenceCount; ++i)
    if (w->kicked[w->sch.PreemptedFenceQueue[i]])
      violate(w, "W5 preemption hands a kicked fence back", w->sch.PreemptedFenceQueue[i]);
  for (f = 1; f <= w->nsub; ++f) if (!w->final_[f] && !w->kicked[f]) resubmit(w, f);
  w->sch.PreemptedFenceCount = 0u;
  w->preempt = 0;
}
static void begin_preempt(world *w) {
  if (!AppleAgxSchedulerBeginBoundaryPreemption(&w->sch, 0u, 0u, 1000u + w->now,
          AppleAgxSchedulerLastSubmittedFence(&w->sch, 0u, 0u),
          AppleAgxSchedulerActiveFence(&w->sch, 0u, 0u)))
    violate(w, "preemption refused", 0);
  w->preempt = 1;
}
/* DxgkDdiResetEngine (G4-r3, rc97): fault the scheduler, quiesce by
 * polling until the firmware holds neither job of the window (their ends
 * are reported through the ordinary completion), then the engine reset. */
static void reset_engine(world *w) {
  unsigned t, f, last = 0, aborted = 0, newest = AdmissionJobWindowQuiesceFence(&w->win);
  ++w->resets;
  w->faulted = 1; w->resetting = 1; w->in_reset_quiesce = 1;
  for (t = 0; t < RESET_BOUND; ++t) {
    if (!AppleAgxSchedulerActiveFence(&w->sch, 0u, 0u) && !AppleAgxSchedulerKickedFence(&w->sch) &&
        w->be.Phase != AppleAgxBackendRuntimeSubmitted)
      break;
    ++w->now; firmware(w);
    poll(w);
    adopt(w);
  }
  w->in_reset_quiesce = 0;
  if (in_firmware(w)) violate(w, "W6 reset returned with a job in the firmware", newest);
  if (w->be.Phase != AppleAgxBackendRuntimeReady || w->prov.Phase != AppleAgxG13QueueProviderCreated)
    violate(w, "W6 reset left the backend busy", newest);
  if (!AppleAgxSchedulerResetEngine(&w->sch, 0u, 0u, &last)) violate(w, "engine reset", 0);
  (void)AdmissionJobWindowResetAbort(&w->win, &aborted);
  w->last_aborted = last;
  w->rec.ResetAborts += aborted;
  for (f = 1; f <= w->nsub; ++f) {
    if (w->final_[f]) continue;
    if (w->kicked[f] && f <= last) { w->aborted[f] = 1; w->final_[f] = 2; continue; }
    if (w->kicked[f]) violate(w, "W6 kicked job above LastAbortedFenceId", f);
    resubmit(w, f);
  }
  w->sch.PreemptedFenceCount = 0u;
  w->prebound_at = 0; w->preempt = 0;
  w->faulted = 0; w->resetting = 0;
}

static void run(world *w, unsigned ticks) {
  unsigned t;
  for (t = 0; t < ticks; ++t) {
    ++w->now;
    vidsch(w);
    if (w->event) w->event(w);
    kmd(w);
    firmware(w);
    if (in_firmware(w) > 2u) violate(w, "W2 more than two jobs in the firmware", 0);
    kmd(w);
    preempt_complete(w);
    if (w->win.Count && !w->resetting) {
      unsigned h = w->win.Entry[0].Fence;
      if (w->now - w->kick_tick[h] >= TDR_TICKS + (w->kicked_behind[h] ? TDR_TICKS : 0u)) reset_engine(w);
    }
  }
}
static unsigned finish(world *w) {
  unsigned f;
  if (getenv("MODEL_TRACE")) {
    printf("[%s] kicks %u maxfw %u batch1 %u batch2 %u barriers %u/%u/%u/%u adopted %u resets %u last_aborted %u\n",
        w->name, w->rec.Kicks, w->max_fw, w->rec.Reported[1], w->rec.Reported[2],
        w->rec.Barriers[1], w->rec.Barriers[2], w->rec.Barriers[3], w->rec.Barriers[4],
        w->rec.Adopted, w->resets, w->last_aborted);
    for (f = 1; f <= w->nsub; ++f)
      printf("  f%u kind %u final %u kicked %u behind %u dropped %u resub %u\n", f, w->job[f].kind,
             w->final_[f], w->kicked[f], w->kicked_behind[f], w->repd[f], w->resub[f]);
  }
  for (f = 1; f <= w->nsub; ++f) if (!w->final_[f]) violate(w, "W5 fence never ended", f);
  if (w->win.Count) violate(w, "window not empty at the end", w->win.Entry[0].Fence);
  if (in_firmware(w)) violate(w, "firmware not empty at the end", 0);
  for (f = 1; f <= w->nsub; ++f)
    if (w->kicked[f] && w->resub[f]) violate(w, "W5 kicked job resubmitted", f);
  return w->violations;
}

/* ---- scenarios ---------------------------------------------------------- */
static void start(world *w, const char *name) { init(w, name); retarget(w); }
static unsigned s_order(void) {
  world *w = &W; unsigned i;
  start(w, "order");
  /* a first render starts the queues; then pairs of every class */
  add(w, K_R, OUT_OK, 7);
  for (i = 0; i < 15; ++i) add(w, (i % 3) == 2 ? K_R : K_C, OUT_OK, 7);
  run(w, 400);
  finish(w);
  if (w->max_fw != 2 || w->rec.Kicks < 4) { printf("order: max in flight %u kicks %u\n", w->max_fw, w->rec.Kicks); ++w->violations; }
  if (w->max_batch != 2 || !w->rec.Reported[2]) { printf("order: no batched pair (B1)\n"); ++w->violations; }
  if (!w->rec.Barriers[ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE] ||
      !w->rec.Barriers[ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D])
    { printf("order: barrier classes missing\n"); ++w->violations; }
  return w->violations;
}
static unsigned s_depth1(void) {
  world *w = &W; unsigned i;
  start(w, "depth1");
  w->depth2 = 0;
  for (i = 0; i < 8; ++i) add(w, i & 1 ? K_RP : K_C, OUT_OK, 7);
  run(w, 300);
  finish(w);
  if (w->max_fw != 1 || w->rec.Kicks) { puts("depth1: kicked behind"); ++w->violations; }
  return w->violations;
}
static unsigned s_prelude(void) {
  world *w = &W; unsigned i;
  start(w, "prelude");
  add(w, K_RP, OUT_OK, 7);
  for (i = 0; i < 14; ++i) add(w, (i % 3) == 2 ? K_RP : K_C, OUT_OK, 7);
  run(w, 400);
  finish(w);
  if (!w->rec.Barriers[ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE] ||
      !w->rec.Barriers[ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D])
    { puts("prelude: compute-queue barriers missing"); ++w->violations; }
  return w->violations;
}
static unsigned s_pairs(void) {
  world *w = &W;
  start(w, "pairs");
  add(w, K_R, OUT_OK, 7); add(w, K_R, OUT_OK, 7);   /* render after render: refused */
  add(w, K_C, OUT_OK, 7); add(w, K_C, OUT_OK, 9);   /* another process: refused */
  add(w, K_R, OUT_OK, 9);
  run(w, 300);
  finish(w);
  if (w->kicked_behind[2] || w->kicked_behind[4]) { puts("pairs: refused pair kicked"); ++w->violations; }
  return w->violations;
}
/* A fault in the running job with its successor kicked (Head) or in the
 * successor itself; both U5 variants. */
static unsigned s_fault(int runs_after, int head) {
  world *w = &W;
  start(w, head ? (runs_after ? "fault-head-runs" : "fault-head-killed")
                : (runs_after ? "fault-next-runs" : "fault-next-killed"));
  w->runs_after_drop = runs_after;
  add(w, K_R, OUT_OK, 7); add(w, K_C, OUT_OK, 7);         /* 1, 2 behind 1 */
  add(w, K_C, head ? OUT_FAULT : OUT_OK, 7);              /* 3 */
  add(w, K_R, OUT_OK, 7);                                 /* 4 behind 3 */
  add(w, K_C, head ? OUT_OK : OUT_FAULT, 7);              /* 5 */
  add(w, K_R, OUT_OK, 7); add(w, K_C, OUT_OK, 7); add(w, K_R, OUT_OK, 7);
  run(w, 400);
  finish(w);
  if (head) {
    if (!w->repd[3] || !w->kicked_behind[4]) { puts("fault-head: drop not reported or no successor"); ++w->violations; }
    if (!runs_after && !w->repd[4]) { puts("fault-head: killed successor reported as success (B2)"); ++w->violations; }
  } else if (!w->repd[5]) {
    puts("fault-next: the faulting job not reported dropped"); ++w->violations;
  }
  return w->violations;
}
/* A compute job faults with a compute job kicked behind it on the same
 * queue: the recovery must wait for the successor's entries too. */
static unsigned s_fault_cc(int runs_after) {
  world *w = &W;
  start(w, runs_after ? "fault-cc-runs" : "fault-cc-killed");
  w->runs_after_drop = runs_after;
  add(w, K_R, OUT_OK, 7); add(w, K_C, OUT_OK, 7);
  add(w, K_C, OUT_FAULT, 7); add(w, K_C, OUT_OK, 7);
  add(w, K_R, OUT_OK, 7); add(w, K_C, OUT_OK, 7);
  run(w, 300);
  finish(w);
  if (!w->repd[3] || !w->kicked_behind[4]) { puts("fault-cc: drop or successor missing"); ++w->violations; }
  if (!runs_after && !w->repd[4]) { puts("fault-cc: killed successor reported as success (B2)"); ++w->violations; }
  return w->violations;
}
/* The first render of a queue lifetime (new queues, InitBM) never goes
 * behind a running job (refusal 61). */
static unsigned s_first_render(void) {
  world *w = &W;
  start(w, "first-render");
  add(w, K_C, OUT_OK, 7); add(w, K_R, OUT_OK, 7); add(w, K_C, OUT_OK, 7); add(w, K_R, OUT_OK, 7);
  run(w, 200);
  finish(w);
  if (w->kicked_behind[2]) { puts("first-render: kicked behind"); ++w->violations; }
  return w->violations;
}
/* A hung running job with its successor kicked: the TDR reset quiesces
 * until the firmware drops it (its Timeout) and both jobs end. */
static unsigned s_tdr(int runs_after) {
  world *w = &W; unsigned f;
  start(w, runs_after ? "tdr-runs" : "tdr-killed");
  w->runs_after_drop = runs_after;
  add(w, K_R, OUT_OK, 7); add(w, K_C, OUT_OK, 7);
  add(w, K_C, OUT_HANG, 7); add(w, K_R, OUT_OK, 7);
  add(w, K_C, OUT_OK, 7); add(w, K_R, OUT_OK, 7);
  run(w, 400);
  finish(w);
  if (w->resets != 1 || !w->kicked_behind[4] || !w->repd[3]) { printf("tdr: resets %u\n", w->resets); ++w->violations; }
  for (f = 3; f <= 4; ++f) if (!w->final_[f] || w->resub[f]) ++w->violations;
  return w->violations;
}
static void ev_preempt(world *w) {
  if (AppleAgxSchedulerKickedFence(&w->sch) && !w->preempt && !w->resets && w->now < 60) begin_preempt(w);
}
static unsigned s_preempt(void) {
  world *w = &W;
  start(w, "preempt");
  w->event = ev_preempt;
  w->gate = 3; /* a never-kicked packet waits behind the kicked one */
  add(w, K_R, OUT_OK, 7); add(w, K_C, OUT_OK, 7); add(w, K_C, OUT_OK, 7); add(w, K_R, OUT_OK, 7);
  run(w, 300);
  finish(w);
  if (!w->kicked_behind[2] || w->repd[2] || !w->final_[2] || w->resub[2]) { puts("preempt: kicked fence not completed"); ++w->violations; }
  if (!w->resub[3]) { puts("preempt: the never-kicked packet was not handed back"); ++w->violations; }
  return w->violations;
}
static void ev_fault(world *w) {
  if (AppleAgxSchedulerKickedFence(&w->sch) && !w->resets && !w->faulted) w->faulted = 1;
}
static unsigned s_rc63(void) {
  world *w = &W;
  start(w, "rc63");
  w->event = ev_fault;
  add(w, K_R, OUT_OK, 7); add(w, K_C, OUT_OK, 7); add(w, K_C, OUT_OK, 7); add(w, K_R, OUT_OK, 7);
  run(w, 300);
  return finish(w);
}
static void ev_paging(world *w) { w->paging = w->now < 40; }
static unsigned s_paging(void) {
  world *w = &W;
  start(w, "paging");
  w->event = ev_paging;
  add(w, K_R, OUT_OK, 7); add(w, K_C, OUT_OK, 7);
  run(w, 200);
  finish(w);
  if (w->rec.Kicks) { puts("paging: kicked behind a paging packet"); ++w->violations; }
  return w->violations;
}
/* The rc97 drill: the worker leaves the drilled job (no poll; the scheduler
 * faults) with its successor queued (never kicked: VidSch resubmits it) or
 * already kicked (both end inside the reset's quiescence). */
static void ev_drill(world *w) {
  unsigned want_kicked = w->caps & 0x100u ? 1u : 0u;
  if (w->resets || w->faulted || !AppleAgxSchedulerActiveFence(&w->sch, 0u, 0u)) return;
  if (want_kicked ? AppleAgxSchedulerKickedFence(&w->sch) != 0u
                  : AppleAgxSchedulerQueuedFence(&w->sch, 0u, 0u) != 0u) {
    w->faulted = 1;
  }
}
static unsigned s_drill(int kicked) {
  world *w = &W;
  start(w, kicked ? "drill-kicked" : "drill-queued");
  if (kicked) w->caps |= 0x100u;
  else w->depth2 = 0;
  w->event = ev_drill;
  add(w, K_R, OUT_OK, 7); add(w, K_C, OUT_OK, 7); add(w, K_R, OUT_OK, 7);
  run(w, 3);
  w->event = NULL;
  reset_engine(w);
  run(w, 200);
  finish(w);
  if (w->resets != 1 || (!kicked && !w->resub[2]) || (kicked && (!w->final_[2] || w->resub[2] || w->repd[2])))
    { printf("drill: resets %u\n", w->resets); ++w->violations; }
  return w->violations;
}

static int unit_scheduler(void) {
  APPLE_AGX_SCHEDULER s; APPLE_AGX_U32 last = 0;
  AppleAgxSchedulerInitialize(&s);
  if (!AppleAgxSchedulerQueueFence(&s, 0, 0, 5) || !AppleAgxSchedulerQueueFence(&s, 0, 0, 6) ||
      !AppleAgxSchedulerQueueFence(&s, 0, 0, 7)) return 1;
  if (AppleAgxSchedulerKickQueuedFence(&s, 0, 0, 5)) return 2;       /* nothing active */
  if (!AppleAgxSchedulerActivateFence(&s, 0, 0, 5)) return 3;
  if (AppleAgxSchedulerKickQueuedFence(&s, 0, 0, 7)) return 4;       /* not the head */
  if (!AppleAgxSchedulerKickQueuedFence(&s, 0, 0, 6) ||
      AppleAgxSchedulerKickQueuedFence(&s, 0, 0, 6)) return 5;       /* one */
  if (!AppleAgxSchedulerResetEngine(&s, 0, 0, &last) || last != 6u ||
      s.LastAbortedFence != 6u || s.KickedFence) return 6;            /* newest aborted */
  if (s.PreemptedFenceCount != 1u || s.PreemptedFenceQueue[0] != 7u) return 7;
  return 0;
}

int main(void) {
  unsigned bad = 0;
  int unit = unit_scheduler();
  if (unit) { printf("FAIL scheduler unit %d\n", unit); return 1; }
  bad += s_order();
  bad += s_depth1();
  bad += s_prelude();
  bad += s_pairs();
  bad += s_fault(1, 1);
  bad += s_fault(0, 1);
  bad += s_fault(1, 0);
  bad += s_fault(0, 0);
  bad += s_fault_cc(1);
  bad += s_fault_cc(0);
  bad += s_first_render();
  bad += s_tdr(1);
  bad += s_tdr(0);
  bad += s_preempt();
  bad += s_rc63();
  bad += s_paging();
  bad += s_drill(0);
  bad += s_drill(1);
  if (bad) { printf("FAIL %u\n", bad); return 1; }
  puts("PASS");
  return 0;
}
