"""rc90: build-ahead job window (Wom1JobBuildAhead) -- host model.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The KMD runs one firmware job at a time. rc90 lets the worker that ran job N
(a) restore N's template arena right after N's kick, (b) bind the next
pending G4 submission N+1 into it while N runs and (c) claim N+1 itself
after N's completion instead of returning through WorkerFinished, the DPC
dispatch and the job-thread wake. The decisions are
render_job_ahead.c (AdmissionJobAhead*Refusal). This model drives them
through the scheduler/packet/pending/image state machine of
backend_platform_windows.c, gpuva_g3_windows.c and scheduler_windows.c and
checks, for every scenario:
  I1 jobs are kicked in fence order;
  I2 job N is reported to Windows (DMA_COMPLETED / DMA_PAGE_FAULTED) or
     aborted by a reset before N+1 is kicked (one job in the firmware);
  I3 a job is kicked with its own image bound, and nothing binds over an
     image that a job still owns;
  I4 nothing is kicked while a preemption is pending or the scheduler is
     faulted (rc63: a faulted scheduler waits for the TDR reset and does
     not report DMA_PREEMPTED);
  I5 at the end every accepted fence is exactly once reported, aborted by
     the reset or dropped for resubmission, and the image, the prebound
     entry, the early-release mark and the packet are all free;
  I6 a TDR (or fault) with one job prebound accounts for both: the running
     job is aborted, the prebound one is unbound and dropped for VidSch's
     resubmission, never kicked.
A naive policy (chain/prebind whenever a pending head exists) violates I3
and I4 in this model, so the model is discriminating.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RA = ROOT / "drivers/apple-agx/render-admission"
INCLUDE = RA / "include"
SOURCE = RA / "src/render_job_ahead.c"
WORKER = RA / "src/backend_platform_windows.c"
G3 = RA / "src/gpuva_g3_windows.c"
ADMISSION_H = INCLUDE / "render_admission.h"
VCXPROJ = RA / "AppleAgxRenderAdmission.vcxproj"

MODEL = r'''
#include <stdio.h>
#include <string.h>
#include "render_job_ahead.h"

enum { KIND_RENDER = 1, KIND_CPU = 2 };
enum { P_IDLE = 0, P_WAIT = 1, P_READY = 2 };
enum { OUT_OK, OUT_DROPPED, OUT_HANG };
#define Q_QUEUED ADMISSION_JOB_AHEAD_PENDING_QUEUED
#define Q_PREBOUND ADMISSION_JOB_AHEAD_PENDING_PREBOUND
#define PK_EMPTY 0u
#define PK_QUEUED 2u
#define PK_ACTIVE 3u
#define MAXF 128

typedef struct { unsigned fence, kind, state, slow; } item;
typedef struct world world;
typedef void (*during_fn)(world *, unsigned fence);
struct world {
  int naive;
  unsigned next_fence;
  item q[64]; unsigned qn;            /* scheduler queue, fence order */
  unsigned active, preempt, faulted, resetting, dispatched;
  unsigned packet_state, packet_fence;
  unsigned image_bound, image_slow, released;
  unsigned pb_fence, pb_dropped;       /* the prebound pending entry */
  unsigned finished, finished_dropped;
  unsigned outcome[MAXF];              /* per fence: OUT_* */
  unsigned slow[MAXF];
  unsigned kicked[MAXF], nk, chained_kick[MAXF];
  unsigned reported[MAXF], nr, aborted[MAXF], na, dropped[MAXF], nd;
  unsigned accepted[MAXF], nacc;
  unsigned chains, prebinds, unbinds, releases, notified, notified_last;
  unsigned last_completed, violations;
  during_fn during;
  int in_worker;
};

static void violate(world *w, const char *what, unsigned fence) {
  printf("VIOLATION %s fence %u\n", what, fence);
  ++w->violations;
}
static int in(const unsigned *a, unsigned n, unsigned f) {
  unsigned i; for (i = 0; i < n; ++i) if (a[i] == f) return 1; return 0;
}
static unsigned submit(world *w, unsigned kind, unsigned outcome, unsigned slow) {
  unsigned f = w->next_fence++;
  w->q[w->qn].fence = f; w->q[w->qn].kind = kind;
  w->q[w->qn].state = Q_QUEUED; w->q[w->qn].slow = slow; ++w->qn;
  w->outcome[f] = outcome; w->slow[f] = slow;
  w->accepted[w->nacc++] = f;
  return f;
}
static void pop(world *w) { memmove(w->q, w->q + 1, --w->qn * sizeof(item)); }
static item *render_head(world *w) {
  unsigned i; for (i = 0; i < w->qn; ++i) if (w->q[i].kind == KIND_RENDER) return &w->q[i];
  return NULL;
}

static void fill(world *w, ADMISSION_JOB_AHEAD_STATE *s, unsigned job) {
  item *h = render_head(w);
  unsigned i, renders = 0;
  memset(s, 0, sizeof(*s));
  for (i = 0; i < w->qn; ++i) renders += w->q[i].kind == KIND_RENDER;
  s->Enabled = 1; s->Resetting = w->resetting; s->Faulted = w->faulted;
  s->PreemptionPhase = w->preempt; s->BackendReady = w->active == 0;
  s->QueuedFence = w->qn ? w->q[0].fence : 0;
  s->CpuHeadFence = w->qn && w->q[0].kind == KIND_CPU ? w->q[0].fence : 0;
  s->ActiveFence = w->active; s->DispatchedFence = w->dispatched;
  s->PacketState = w->packet_state; s->PacketFence = w->packet_fence;
  s->PendingCount = renders + (w->pb_fence && w->pb_dropped);
  if (w->pb_fence && w->pb_dropped) {
    s->PendingHeadState = ADMISSION_JOB_AHEAD_PENDING_DROPPED;
    s->PendingHeadFence = w->pb_fence;
  } else if (h) {
    s->PendingHeadState = h->state; s->PendingHeadFence = h->fence;
  }
  s->ImageKind = w->image_bound ? (w->image_slow ? ADMISSION_JOB_AHEAD_IMAGE_OTHER
                                                 : ADMISSION_JOB_AHEAD_IMAGE_G4_RENDER)
                                : ADMISSION_JOB_AHEAD_IMAGE_NONE;
  s->ImageBoundFence = w->image_bound; s->ImageFastRelease = !w->image_slow;
  s->ReleasedFence = w->released; s->JobFence = job;
  s->FinishedFence = w->finished; s->FinishedDropped = w->finished_dropped;
}

static void bind_image(world *w, unsigned fence) {
  if (w->image_bound) violate(w, "bind over a bound image", fence);
  w->image_bound = fence; w->image_slow = w->slow[fence];
}
static void release_image(world *w, unsigned fence) {
  if (w->image_bound != fence) violate(w, "release of a foreign image", fence);
  w->image_bound = 0; w->image_slow = 0;
}

static void kick(world *w, unsigned fence, unsigned chained) {
  unsigned i;
  if (w->nk && fence <= w->kicked[w->nk - 1]) violate(w, "I1 order", fence);
  for (i = 0; i < w->nk; ++i)
    if (!in(w->reported, w->nr, w->kicked[i]) && !in(w->aborted, w->na, w->kicked[i]))
      violate(w, "I2 previous job still in the firmware", fence);
  if (w->image_bound != fence) violate(w, "I3 kicked without its image", fence);
  if (w->preempt != P_IDLE || w->faulted || w->resetting)
    violate(w, "I4 kick while preempting/faulted/resetting", fence);
  w->chained_kick[fence] = chained;
  w->kicked[w->nk++] = fence;
}

/* Preemption request (PreemptCommand): boundary, queue and pending dropped. */
static void preempt_request(world *w) {
  unsigned i;
  w->preempt = w->active ? P_WAIT : P_READY;
  for (i = 0; i < w->qn; ++i) {
    w->dropped[w->nd++] = w->q[i].fence;
    if (w->q[i].fence == w->pb_fence) w->pb_dropped = 1;
  }
  w->qn = 0;
  /* a Queued packet (bound, not active) is discarded as well */
  if (w->packet_state == PK_QUEUED) {
    w->dropped[w->nd++] = w->packet_fence;
    release_image(w, w->packet_fence);
    w->packet_state = PK_EMPTY; w->packet_fence = 0;
    if (w->dispatched && !w->active) w->dispatched = 0;
  }
}

static void prebind_try(world *w, unsigned job) {
  ADMISSION_JOB_AHEAD_STATE s;
  item *h = render_head(w);
  unsigned refusal;
  if (w->pb_fence) return;
  fill(w, &s, job);
  refusal = w->naive ? !(h && h->state == Q_QUEUED) : AdmissionJobAheadPrebindRefusal(&s);
  if (refusal) return;
  bind_image(w, h->fence);
  h->state = Q_PREBOUND; w->pb_fence = h->fence; w->pb_dropped = 0;
  ++w->prebinds;
}

static void unprebind(world *w) {
  item *h;
  if (!w->pb_fence) return;
  release_image(w, w->pb_fence);
  if (!w->pb_dropped && (h = render_head(w)) != NULL && h->fence == w->pb_fence)
    h->state = Q_QUEUED;
  w->pb_fence = 0; w->pb_dropped = 0; ++w->unbinds;
}

static void complete(world *w, unsigned fence, int dropped) {
  w->active = 0;
  w->packet_state = PK_EMPTY; w->packet_fence = 0;
  if (w->released == fence) w->released = 0; else release_image(w, fence);
  w->reported[w->nr++] = fence;
  if (w->nr > 1 && w->reported[w->nr - 2] > fence) violate(w, "report order", fence);
  w->finished = fence; w->finished_dropped = dropped;
  w->last_completed = fence;
  if (w->preempt == P_WAIT) w->preempt = P_READY;
}

static void reset_engine(world *w);

/* The job worker for one invocation (AdmissionPlatformWorker): bind head,
 * activate, kick, early release, poll (prebind), complete, chain. */
static void worker_run(world *w) {
  unsigned chained = 0;
  w->in_worker = 1;
  for (;;) {
    ADMISSION_JOB_AHEAD_STATE s;
    unsigned fence, refusal;
    item *h = w->qn ? &w->q[0] : NULL;
    /* AdmissionG4PendingBindHead */
    if (w->packet_state == PK_EMPTY && h && h->kind == KIND_RENDER &&
        h->state == Q_QUEUED) {
      bind_image(w, h->fence);
      w->packet_state = PK_QUEUED; w->packet_fence = h->fence;
    }
    /* activation */
    if (w->packet_state != PK_QUEUED || w->active || !w->qn ||
        w->q[0].fence != w->packet_fence ||
        (w->dispatched && w->dispatched != w->packet_fence) ||
        /* AppleAgxSchedulerActivateFence refuses while dispatch is blocked
         * by a preemption; the worker does not test SchedulerFaulted. */
        w->resetting || w->preempt != P_IDLE)
      break;
    fence = w->packet_fence; pop(w);
    w->packet_state = PK_ACTIVE; w->active = fence; w->dispatched = fence;
    kick(w, fence, chained != 0);
    fill(w, &s, fence);
    if (!AdmissionJobAheadReleaseRefusal(&s)) {
      release_image(w, fence); w->released = fence; ++w->releases;
    }
    prebind_try(w, fence);
    if (w->during) w->during(w, fence);
    prebind_try(w, fence);
    if (w->outcome[fence] == OUT_HANG) {
      w->resetting = 1; /* TDR: the poll loop leaves, the job stays */
      break;
    }
    complete(w, fence, w->outcome[fence] == OUT_DROPPED);
    /* chain */
    fill(w, &s, fence);
    s.Chained = chained;
    h = render_head(w);
    refusal = w->naive ? !(h && w->packet_state == PK_EMPTY && w->q[0].fence == h->fence)
                       : AdmissionJobAheadChainRefusal(&s);
    if (refusal) break;
    if (w->pb_fence) {
      if (w->pb_fence != w->q[0].fence) violate(w, "promote of a non-head", w->pb_fence);
      w->packet_state = PK_QUEUED; w->packet_fence = w->pb_fence;
      w->q[0].state = Q_QUEUED; w->pb_fence = 0;
    }
    w->dispatched = w->q[0].fence;
    ++w->chains; ++chained;
  }
  unprebind(w);   /* the worker resolves its prebound entry before leaving */
  w->in_worker = 0;
}

/* VidSch resubmits what a preemption or reset dropped, with new fences. */
static void resubmit(world *w) {
  unsigned i, n = w->nd;
  for (i = 0; i < n; ++i) {
    unsigned old = w->dropped[i];
    submit(w, KIND_RENDER, w->outcome[old] == OUT_HANG ? OUT_OK : w->outcome[old],
           w->slow[old]);
  }
  w->nd = 0; /* the dropped list now lives on as the new fences */
}

/* DPC + WorkerFinished: preemption notify, then dispatch (normal path). */
static void dispatch(world *w) {
  for (;;) {
    if (w->resetting) return;
    if (w->preempt == P_READY && !w->active && w->packet_state == PK_EMPTY) {
      if (w->faulted) return; /* rc63: deferred to the TDR reset */
      ++w->notified; w->notified_last = w->last_completed; w->preempt = P_IDLE;
      w->dispatched = 0;
      resubmit(w);
      continue;
    }
    if (w->faulted || w->preempt != P_IDLE || !w->qn || w->active) return;
    w->dispatched = 0;
    if (w->q[0].kind == KIND_CPU) {
      unsigned f = w->q[0].fence;
      pop(w);
      w->reported[w->nr++] = f; w->last_completed = f;
      continue;
    }
    w->dispatched = w->q[0].fence;
    worker_run(w);
    if (w->resetting) reset_engine(w);
  }
}

/* ResetFromTimeout: quiesce (worker idle), abort the active job, drop the
 * rest, clear the fault and the boundary. */
static void reset_engine(world *w) {
  unsigned i;
  if (w->in_worker) violate(w, "reset before the worker left", 0);
  if (w->pb_fence) violate(w, "I6 prebound entry outlived the worker", w->pb_fence);
  if (w->active) {
    unsigned f = w->active;
    w->aborted[w->na++] = f; w->active = 0;
    w->packet_state = PK_EMPTY; w->packet_fence = 0;
    if (w->released == f) w->released = 0; else release_image(w, f);
  }
  if (w->packet_state == PK_QUEUED) {
    w->dropped[w->nd++] = w->packet_fence; release_image(w, w->packet_fence);
    w->packet_state = PK_EMPTY; w->packet_fence = 0;
  }
  for (i = 0; i < w->qn; ++i) w->dropped[w->nd++] = w->q[i].fence;
  w->qn = 0; w->dispatched = 0; w->faulted = 0; w->preempt = P_IDLE;
  w->resetting = 0;
  resubmit(w);
}

static void finish(world *w, const char *name) {
  unsigned i;
  for (i = 0; i < w->nacc; ++i) {
    unsigned f = w->accepted[i], c = 0;
    c += in(w->reported, w->nr, f) + in(w->aborted, w->na, f);
    if (c != 1 && !in(w->dropped, w->nd, f)) {
      /* a dropped fence was resubmitted as a later fence */
      unsigned resub = 0, k;
      for (k = 0; k < w->nacc; ++k) if (w->accepted[k] > f) resub = 1;
      if (!(c == 0 && resub)) violate(w, "I5 fence accounting", f);
    }
  }
  if (w->image_bound) violate(w, "I5 image still bound", w->image_bound);
  if (w->pb_fence || w->released || w->packet_state != PK_EMPTY || w->qn)
    violate(w, "I5 state not free", 0);
  printf("%s: kicked %u chains %u prebinds %u unbinds %u releases %u "
         "aborted %u notified %u violations %u\n", name, w->nk, w->chains,
         w->prebinds, w->unbinds, w->releases, w->na, w->notified, w->violations);
}

static void init(world *w, int naive) { memset(w, 0, sizeof(*w)); w->naive = naive; w->next_fence = 1; }

/* ---- scenarios ---- */
static unsigned s_extra;
static void during_submit_next(world *w, unsigned fence) {
  if (fence == 1) s_extra = submit(w, KIND_RENDER, OUT_OK, 0);
}
static unsigned scenario_order(int naive) {
  world w; unsigned i;
  init(&w, naive);
  submit(&w, KIND_RENDER, OUT_OK, 0);   /* 1 */
  submit(&w, KIND_RENDER, OUT_OK, 0);   /* 2 */
  submit(&w, KIND_CPU, OUT_OK, 0);      /* 3 paging between renders */
  submit(&w, KIND_RENDER, OUT_OK, 0);   /* 4 */
  submit(&w, KIND_RENDER, OUT_OK, 0);   /* 5 */
  submit(&w, KIND_RENDER, OUT_OK, 0);   /* 6 */
  dispatch(&w);
  finish(&w, naive ? "order/naive" : "order");
  if (!naive) {
    if (w.nk != 5 || w.chains < 3 || w.prebinds < 3) { puts("order: no overlap"); return 1; }
    if (w.chained_kick[1] || !w.chained_kick[2] || w.chained_kick[4] || !w.chained_kick[5])
      { puts("order: chain across the paging packet"); return 1; }
    for (i = 1; i < w.nr; ++i) if (w.reported[i] < w.reported[i - 1]) return 1;
  }
  return w.violations;
}
static void during_hang_prebound(world *w, unsigned fence) {
  if (fence == 1) submit(w, KIND_RENDER, OUT_OK, 0);
}
static unsigned scenario_tdr(int naive) {
  world w;
  init(&w, naive);
  submit(&w, KIND_RENDER, OUT_HANG, 0);  /* 1 hangs */
  w.during = during_hang_prebound;        /* 2 arrives and is prebound */
  dispatch(&w);
  w.during = NULL;
  dispatch(&w);
  finish(&w, naive ? "tdr/naive" : "tdr");
  if (!naive && (w.prebinds < 1 || w.na != 1 || w.aborted[0] != 1 ||
                 in(w.kicked, w.nk, 2) || w.unbinds < 1 || !in(w.kicked, w.nk, 3)))
    { puts("tdr: running and prebound job not both accounted"); return 1; }
  return w.violations;
}
static unsigned scenario_fault(int naive) {
  world w;
  init(&w, naive);
  submit(&w, KIND_RENDER, OUT_DROPPED, 0); /* 1 dropped by the firmware */
  w.during = during_submit_next;            /* 2 prebound */
  dispatch(&w);
  finish(&w, naive ? "fault/naive" : "fault");
  if (!naive && (w.chained_kick[2] || !in(w.kicked, w.nk, 2) || w.unbinds < 1))
    { puts("fault: a job chained after a dropped job"); return 1; }
  return w.violations;
}
static void during_preempt(world *w, unsigned fence) {
  if (fence == 1) { submit(w, KIND_RENDER, OUT_OK, 0); w->during = NULL;
                    prebind_try(w, fence); preempt_request(w); }
}
static unsigned scenario_preempt(int naive) {
  world w;
  init(&w, naive);
  submit(&w, KIND_RENDER, OUT_OK, 0);  /* 1 runs; 2 prebound; then preempt */
  w.during = during_preempt;
  dispatch(&w);
  finish(&w, naive ? "preempt/naive" : "preempt");
  if (!naive && (w.notified != 1 || w.notified_last != 1 || in(w.kicked, w.nk, 2) ||
                 !in(w.kicked, w.nk, 3)))
    { puts("preempt: boundary not after job 1 or job 2 not resubmitted"); return 1; }
  return w.violations;
}
static void during_fault_preempt(world *w, unsigned fence) {
  if (fence == 1) { submit(w, KIND_RENDER, OUT_OK, 0); w->during = NULL;
                    prebind_try(w, fence); w->faulted = 1; }
}
static unsigned scenario_rc63(int naive) {
  world w;
  init(&w, naive);
  submit(&w, KIND_RENDER, OUT_OK, 0);  /* 1 runs, 2 prebound, scheduler faults */
  w.during = during_fault_preempt;
  dispatch(&w);
  if (!naive && (w.nk != 1 || w.notified)) { puts("rc63: dispatch while faulted"); return 1; }
  preempt_request(&w);                  /* VidSch preempts the frozen engine */
  dispatch(&w);
  if (!naive && w.notified) { puts("rc63: DMA_PREEMPTED while faulted"); return 1; }
  w.resetting = 1; reset_engine(&w);    /* the TDR fires */
  dispatch(&w);
  finish(&w, naive ? "rc63/naive" : "rc63");
  return w.violations;
}
static void during_submit_slow(world *w, unsigned fence) {
  if (fence == 1) submit(w, KIND_RENDER, OUT_OK, 0);
}
static unsigned scenario_slow_image(int naive) {
  world w;
  init(&w, naive);
  submit(&w, KIND_RENDER, OUT_OK, 1);  /* 1: an image that cannot release early */
  w.during = during_submit_slow;
  dispatch(&w);
  finish(&w, naive ? "slow/naive" : "slow");
  /* only job 2 releases early; it is chained, then bound after job 1 */
  if (!naive && (w.releases != 1 || w.prebinds != 0 || !w.chained_kick[2]))
    { puts("slow: early release of a non-G4 image"); return 1; }
  return w.violations;
}

int main(void) {
  unsigned bad = 0, naive_bad = 0;
  bad += scenario_order(0);
  bad += scenario_tdr(0);
  bad += scenario_fault(0);
  bad += scenario_preempt(0);
  bad += scenario_rc63(0);
  bad += scenario_slow_image(0);
  /* each naive scenario must break an invariant */
  naive_bad += scenario_rc63(1) != 0;
  naive_bad += scenario_slow_image(1) != 0;
  if (bad) { printf("FAIL %u\n", bad); return 1; }
  if (naive_bad != 2) { puts("FAIL: the naive policy passed (model not discriminating)"); return 2; }
  puts("PASS");
  return 0;
}
'''


def _build_and_run(tmp: Path) -> subprocess.CompletedProcess:
    path = tmp / "ahead_model.c"
    exe = tmp / "ahead_model"
    path.write_text(MODEL)
    built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                            "-Werror", "-I", str(INCLUDE), str(path), str(SOURCE),
                            "-o", str(exe)], capture_output=True, text=True)
    if built.returncode != 0:
        return built
    return subprocess.run([str(exe)], capture_output=True, text=True)


class JobAheadModel(unittest.TestCase):
    def test_model_invariants_hold_and_naive_policy_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            ran = _build_and_run(Path(directory))
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("PASS", ran.stdout)


class JobAheadWiring(unittest.TestCase):
    """The KMD uses the same decisions, gated by a default-off switch."""

    def assertIn(self, member, container, msg=None):  # keep failures short
        self.assertTrue(member in container, msg or f"missing: {member!r}")

    def assertNotIn(self, member, container, msg=None):
        self.assertTrue(member not in container, msg or f"unexpected: {member!r}")

    def assertRegex(self, text, regex, msg=None):
        self.assertTrue(re.search(regex, text), msg or f"no match: {regex!r}")

    def test_switch_is_default_off_and_gates_every_step(self):
        text = WORKER.read_text()
        self.assertIn('AdmissionPlatformSwitchEnabled(Context, L"Wom1JobBuildAhead")', text)
        self.assertNotIn('AdmissionPlatformSwitchDisabled(Context, L"Wom1JobBuildAhead")', text)
        for call in ("AdmissionJobAheadReleaseRefusal(", "AdmissionJobAheadChainRefusal("):
            self.assertIn(call, text)
        self.assertIn("AdmissionJobAheadPrebindRefusal(", G3.read_text())
        # every decision input starts from the switch; the KMD fills the
        # adapter side under SchedulerLock
        self.assertRegex(text, r"->Enabled\s*=\s*runtime->JobAhead")
        g3 = G3.read_text()
        prebind = g3[g3.index("BOOLEAN AdmissionG4PendingPrebindHead("):]
        prebind = prebind[:prebind.index("\n}\n")]
        fill = prebind.index("AdmissionJobAheadFillLocked(adapter, State)")
        self.assertLess(prebind.index("KeAcquireSpinLock(&adapter->SchedulerLock"), fill)
        self.assertLess(fill, prebind.index("AdmissionJobAheadPrebindRefusal(State)"))
        worker = text[text.index("static VOID AdmissionPlatformWorker(\n    _In_ PDEVICE_OBJECT DeviceObject, _In_opt_ PVOID Context) {"):]
        worker = worker[:worker.index("\n}\n")]
        for step in ("AdmissionJobAheadReleaseWindows(", "AdmissionJobAheadPrebindWindows(",
                     "AdmissionJobAheadChainWindows("):
            self.assertIn(step, worker)
            for match in re.finditer(re.escape(step), worker):
                at = match.start()
                self.assertIn("runtime->JobAhead", worker[max(0, at - 500):at], step)
        # the prebind tries right after the kick (before a TDR drill leaves
        # the job) and at every poll
        self.assertEqual(worker.count("AdmissionJobAheadPrebindWindows("), 2)
        self.assertLess(worker.index("AdmissionJobAheadPrebindWindows("),
                        worker.index("AdmissionTdrDrillTake("))

    def test_completion_and_retire_honour_the_early_release(self):
        text = WORKER.read_text()
        complete = text[text.index("static APPLE_AGX_BACKEND_BOOL AdmissionBackendComplete("):]
        complete = complete[:complete.index("\n}\n")]
        self.assertIn("AheadReleasedFence == Fence", complete)
        retire = text[text.index("static APPLE_AGX_BACKEND_BOOL AdmissionBackendRetire("):]
        retire = retire[:retire.index("\n}\n")]
        self.assertIn("AheadReleasedFence == Fence", retire)
        save = text[text.index("static APPLE_AGX_BACKEND_BOOL AdmissionSaveG4Manager("):]
        save = save[:save.index("\n}\n")]
        self.assertIn("AheadManager", save)

    def test_preempt_and_reset_drop_a_prebound_entry_like_a_binding_one(self):
        g3 = G3.read_text()
        drop = g3[g3.index("VOID AdmissionG4PendingDropLocked("):]
        drop = drop[:drop.index("\n}\n")]
        self.assertRegex(drop, r"AdmissionG4PendingBinding\s*\|\|\s*\n?\s*entry->State == AdmissionG4PendingPrebound")
        header = ADMISSION_H.read_text()
        self.assertRegex(header, r"AdmissionG4PendingPrebound\s*=\s*5")
        self.assertIn("AdmissionG4PendingDropped = 4", header)

    def test_worker_resolves_a_prebound_entry_before_it_finishes(self):
        text = WORKER.read_text()
        worker = text[text.index("static VOID AdmissionPlatformWorker(\n    _In_ PDEVICE_OBJECT DeviceObject, _In_opt_ PVOID Context) {"):]
        worker = worker[:worker.index("\n}\n")]
        self.assertIn("AdmissionG4PendingAheadUnbind(adapter)", worker)
        last_unbind = worker.rindex("AdmissionG4PendingAheadUnbind(adapter)")
        last_finish = worker.rindex("AdmissionPlatformWorkerFinished(runtime);")
        chain = worker.rindex("AdmissionJobAheadChainWindows(")
        self.assertLess(chain, last_unbind)
        self.assertLess(last_unbind, last_finish)
        # the chain jumps back before the pending bind, after the job's
        # completion (the poll loop) and the receipts
        self.assertLess(worker.index("ChainNext:"), worker.index("AdmissionG4PendingBindHead(adapter)"))
        self.assertLess(worker.index("AppleAgxPlatformProviderPoll("), chain)

    def test_chain_claims_with_the_dispatch_lock_order(self):
        text = WORKER.read_text()
        chain = text[text.index("static BOOLEAN AdmissionJobAheadChainWindows("):]
        chain = chain[:chain.index("\n}\n")]
        paging = chain.index("KeAcquireSpinLock(&adapter->PagingLock")
        sched = chain.index("KeAcquireSpinLockAtDpcLevel(&adapter->SchedulerLock)")
        self.assertLess(paging, sched)
        self.assertLess(sched, chain.index("AdmissionJobAheadChainRefusal(&s)"))
        self.assertIn("adapter->DispatchedFence = next", chain)
        self.assertIn("KeSetEvent(&runtime->SlotEvent", chain)

    def test_new_source_is_in_the_kmd_project(self):
        self.assertIn(r'<ClCompile Include="src\render_job_ahead.c" />', VCXPROJ.read_text())


if __name__ == "__main__":
    unittest.main()
