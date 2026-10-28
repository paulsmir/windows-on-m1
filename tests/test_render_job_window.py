"""rc95: firmware depth-2 job window (Wom1JobDepth2) -- host model.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Depth 2 kicks job N+1 to the firmware while N runs. The decisions and the
in-flight window are render_job_window.c (AdmissionJobWindowKickRefusal,
AdmissionJobWindow{Kick,Ended,Reportable,Reported,QuiesceFence,
ResetAbort}). This model drives them with a firmware that runs its queued
jobs strictly one after another (N+1 leads with a Barrier on N's last
stamp), drops a faulting job when resumed (Asahi gpu.rs handle_fault /
recover), times a hung one out, and with VidSch's 2-deep submission
(sub(N) ~ cmp(N-2), rc94). For every scenario it checks:
  W1 kicks and reports in fence order (DMA_COMPLETED order), also when the
     firmware's completion notification for N is deferred until N+1 ends
     (the notifier threshold counts every kicked job, Asahi queue/mod.rs
     run(); U4);
  W2 the firmware never holds more than two jobs; a second job is kicked
     only with a legal, available Barrier for the pair;
  W3 two jobs in the firmware never share the compute sidecar, a buffer
     manager binding (scene) or the VM slot (process);
  W4 nothing is kicked while a preemption is pending, the scheduler is
     faulted or a reset runs, and nothing is reported while faulted (rc63);
  W5 every accepted fence ends exactly once: reported, aborted by a reset or
     (never kicked) resubmitted by VidSch; a kicked job is never resubmitted
     (it may have run: no double execution);
  W6 a reset returns only once the firmware holds none of the window's
     jobs, and its LastAbortedFenceId covers every unreported kicked job;
  W7 after a firmware-dropped job nothing new is kicked until the window
     drained.
Naive policies (kick whenever a successor is bound; report a job when its
own end is seen; quiesce and abort only the oldest job) each break an
invariant in this model, so the model is discriminating.
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
SOURCE = RA / "src/render_job_window.c"
HEADER = INCLUDE / "render_job_window.h"
VCXPROJ = RA / "AppleAgxRenderAdmission.vcxproj"

MODEL = r'''
#include <stdio.h>
#include <string.h>
#include "render_job_window.h"

#define K_R ADMISSION_JOB_WINDOW_KIND_RENDER
#define K_RP ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE
#define K_C ADMISSION_JOB_WINDOW_KIND_COMPUTE
#define K_P ADMISSION_JOB_WINDOW_KIND_PRESENT
enum { OUT_OK, OUT_FAULT, OUT_HANG };
#define MAXF 96
#define FW_TIMEOUT 40u   /* the firmware's own Timeout of a hung job */
#define TDR_TICKS 25u    /* Windows TdrDelay, shorter */
#define RESET_BOUND 200u /* the reset's bounded quiescence wait */

typedef struct { unsigned kind, outcome, dur; unsigned long long proc, scene; } jd;

typedef struct world {
  const char *name;
  int naive_kick, naive_report, naive_reset;
  unsigned caps;
  int fw_runs_after_drop; /* U5: the firmware runs N+1 after dropping N */
  int deferred;           /* U4: N's end is seen only once N+1 ended */
  jd plan[MAXF]; unsigned nplan, nexplan;
  jd job[MAXF + 1];       /* by fence */
  unsigned nsub;          /* fences 1..nsub submitted by VidSch */
  unsigned bindnext;      /* lowest submitted fence not yet kicked */
  ADMISSION_JOB_WINDOW win;
  unsigned fwq[4], fwn;   /* kicked fences the firmware holds, in order */
  unsigned start[MAXF + 1], started[MAXF + 1], ended[MAXF + 1], dropped[MAXF + 1];
  unsigned seen[MAXF + 1];/* KMD saw the firmware verdict */
  unsigned drop_seen_at[MAXF + 1];
  unsigned now, prebound, preempt, faulted, resetting, paging;
  unsigned kicked[MAXF + 1], kick_order[MAXF], nk;
  unsigned rep[MAXF], nr, repd[MAXF + 1], aborted[MAXF + 1], na, resub[MAXF + 1], nres;
  unsigned final_[MAXF + 1];
  unsigned depth2, max_fw, last_aborted, resets;
  void (*event)(struct world *);
  unsigned violations;
} world;

static void violate(world *w, const char *what, unsigned f) {
  printf("VIOLATION [%s] %s fence %u\n", w->name, what, f);
  ++w->violations;
}

static void init(world *w, const char *name) {
  memset(w, 0, sizeof(*w));
  w->name = name; w->bindnext = 1; w->fw_runs_after_drop = 1;
  AdmissionJobWindowInitialize(&w->win);
}
static void add(world *w, unsigned kind, unsigned outcome, unsigned long long proc,
                unsigned long long scene) {
  jd *j = &w->plan[w->nplan++];
  j->kind = kind; j->outcome = outcome; j->proc = proc; j->scene = scene;
  j->dur = kind == K_C ? 3u : 5u;
}
static unsigned finished_count(world *w) {
  unsigned f, n = 0;
  for (f = 1; f <= w->nsub; ++f) n += w->final_[f] != 0;
  return n;
}
/* VidSch: fence n is submitted once n-2 jobs ended (sub(N) ~ cmp(N-2)). */
static void vidsch(world *w) {
  while (w->nexplan < w->nplan && w->nsub + 1 <= finished_count(w) + 2 &&
         !w->preempt && !w->resetting) {
    w->job[++w->nsub] = w->plan[w->nexplan++];
  }
}
static void resubmit(world *w, unsigned f) {
  if (w->kicked[f]) violate(w, "W5 a kicked job is resubmitted", f);
  if (w->final_[f]) violate(w, "W5 resubmit of an ended fence", f);
  w->resub[f] = 1; w->final_[f] = 3; ++w->nres;
  /* VidSch resubmits the packet with a new fence */
  if (w->nplan < MAXF) w->plan[w->nplan++] = w->job[f];
}

static void check_pair(world *w, unsigned a, unsigned b) {
  jd *x = &w->job[a], *y = &w->job[b];
  if (x->proc != y->proc) violate(w, "W3 two processes in the firmware", b);
  if (AdmissionJobWindowUsesSidecar(x->kind) && AdmissionJobWindowUsesSidecar(y->kind) &&
      !(w->caps & ADMISSION_JOB_WINDOW_CAP_SECOND_SIDECAR))
    violate(w, "W3 two jobs in the compute sidecar", b);
  if (AdmissionJobWindowUsesManager(x->kind) && AdmissionJobWindowUsesManager(y->kind) &&
      (x->scene != y->scene || !(w->caps & ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER)))
    violate(w, "W3 two manager bindings live", b);
  if (x->kind == K_P || y->kind == K_P) violate(w, "W3 present job in a window", b);
}
static void kick(world *w, unsigned f) {
  if (w->preempt || w->faulted || w->resetting) violate(w, "W4 kick while preempting/faulted/resetting", f);
  if (w->nk && f <= w->kick_order[w->nk - 1]) violate(w, "W1 kick order", f);
  if (w->win.Poisoned) violate(w, "W7 kick into a window with a dropped job", f);
  if (w->fwn) {
    unsigned prev = w->fwq[w->fwn - 1];
    if (w->paging) violate(w, "W4 second kick while a paging packet runs", f);
    unsigned b = AdmissionJobWindowBarrier(w->job[prev].kind, w->job[f].kind);
    if (b == ADMISSION_JOB_WINDOW_BARRIER_NONE) violate(w, "W2 no barrier for the pair", f);
    if ((b == ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D && !(w->caps & ADMISSION_JOB_WINDOW_CAP_TA_ON_3D)) ||
        ((b == ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D ||
          b == ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE) &&
         !(w->caps & ADMISSION_JOB_WINDOW_CAP_COMPUTE_BARRIER)))
      violate(w, "W2 barrier not available", f);
    check_pair(w, prev, f);
    ++w->depth2;
  }
  if (!AdmissionJobWindowKick(&w->win, f) && !w->naive_kick) violate(w, "window refused a kick", f);
  w->kicked[f] = 1; w->kick_order[w->nk++] = f;
  w->fwq[w->fwn++] = f;
  if (w->fwn > w->max_fw) w->max_fw = w->fwn;
  if (w->fwn > ADMISSION_JOB_WINDOW_DEPTH) violate(w, "W2 more than two jobs in the firmware", f);
  if (w->prebound == f) w->prebound = 0;
  if (w->bindnext == f) ++w->bindnext;
}

static void fill(world *w, ADMISSION_JOB_WINDOW_KICK_STATE *s) {
  unsigned n = w->win.Count ? w->win.Entry[w->win.Count - 1].Fence : 0, m = w->prebound;
  memset(s, 0, sizeof(*s));
  s->Enabled = 1; s->Resetting = w->resetting; s->Faulted = w->faulted;
  s->PreemptionPhase = w->preempt; s->PagingPending = w->paging;
  s->QueuedFence = w->bindnext <= w->nsub ? w->bindnext : 0;
  s->InFlight = w->win.Count; s->WindowPoisoned = w->win.Poisoned;
  s->Capabilities = w->caps; s->NextPrebound = m != 0;
  if (n) { s->Running.Fence = n; s->Running.Kind = w->job[n].kind;
           s->Running.Process = w->job[n].proc; s->Running.SceneId = w->job[n].scene; }
  if (m) { s->Next.Fence = m; s->Next.Kind = w->job[m].kind;
           s->Next.Process = w->job[m].proc; s->Next.SceneId = w->job[m].scene; }
}

static void report(world *w, unsigned f, int dropped) {
  if (w->faulted) violate(w, "W4 report while faulted", f);
  if (w->final_[f]) violate(w, "W5 fence ends twice", f);
  if (w->nr && f < w->rep[w->nr - 1]) violate(w, "W1 report order", f);
  if (!w->ended[f] && !w->dropped[f]) violate(w, "report before the firmware ended it", f);
  w->rep[w->nr++] = f; w->repd[f] = dropped; w->final_[f] = 1;
}

/* KMD worker: depth-1 path, prebind, the depth-2 decision, completions. */
static void kmd(world *w) {
  unsigned f;
  int dropped;
  /* completions: the firmware verdicts the KMD can see */
  for (f = 1; f <= w->nsub; ++f) {
    unsigned visible = (w->ended[f] || w->dropped[f]) && !w->seen[f] && w->kicked[f];
    unsigned i, all = 1;
    for (i = 0; i < w->win.Count; ++i) {
      unsigned g = w->win.Entry[i].Fence;
      if (!w->ended[g] && !w->dropped[g]) all = 0;
    }
    /* U4: the notifier event (and the stamp, if deferred) arrives only when
     * every kicked job reached the threshold */
    if (visible && w->deferred && w->ended[f] && !all) visible = 0;
    /* A drop is proven only once every published ring entry of the window
     * retired (PollRecovery: done == write), a little after the resume. */
    if (visible && w->dropped[f]) {
      if (!all) visible = 0;
      else if (!w->drop_seen_at[f]) { w->drop_seen_at[f] = w->now + 2u; visible = 0; }
      else if (w->now < w->drop_seen_at[f]) visible = 0;
    }
    if (!visible) continue;
    w->seen[f] = 1;
    (void)AdmissionJobWindowEnded(&w->win, f, w->dropped[f] != 0);
    if (w->naive_report && !w->faulted) {
      report(w, f, w->dropped[f] != 0);
      (void)AdmissionJobWindowReported(&w->win, f);
    }
  }
  if (!w->naive_report && !w->faulted) {
    while ((f = AdmissionJobWindowReportable(&w->win, &dropped)) != 0) {
      report(w, f, dropped);
      if (!AdmissionJobWindowReported(&w->win, f)) { violate(w, "window refused a report", f); break; }
    }
  }
  if (w->resetting) return;
  /* rc95: the depth-2 decision while N runs and N+1 is prebound */
  if (w->win.Count == 1 && w->fwn == 1 && w->prebound) {
    ADMISSION_JOB_WINDOW_KICK_STATE s;
    unsigned refusal;
    fill(w, &s);
    refusal = w->naive_kick ? 0u : AdmissionJobWindowKickRefusal(&s);
    if (refusal == 0) { kick(w, w->prebound); return; }
  }
  if (w->faulted || w->preempt) return;
  /* depth 1: an empty window kicks the head (bound now or prebound) */
  if (w->win.Count == 0 && w->fwn == 0 && w->bindnext <= w->nsub) {
    kick(w, w->bindnext);
    return;
  }
  /* rc90 prebind: the successor binds while N runs */
  if (w->win.Count == 1 && !w->prebound && w->bindnext <= w->nsub) w->prebound = w->bindnext;
}

/* Firmware: strict order (N+1's Barrier waits for N's end or drop). */
static void firmware(world *w) {
  unsigned i;
  if (!w->fwn) return;
  {
    unsigned h = w->fwq[0];
    jd *j = &w->job[h];
    if (!w->started[h]) { w->started[h] = 1; w->start[h] = w->now; }
    if (j->outcome == OUT_OK && w->now - w->start[h] >= j->dur) w->ended[h] = 1;
    else if (j->outcome == OUT_FAULT && w->now - w->start[h] >= 1u) w->dropped[h] = 1;
    else if (j->outcome == OUT_HANG && w->now - w->start[h] >= FW_TIMEOUT) w->dropped[h] = 1;
    if (w->ended[h] || w->dropped[h]) {
      int was_dropped = w->dropped[h] != 0;
      for (i = 1; i < w->fwn; ++i) w->fwq[i - 1] = w->fwq[i];
      --w->fwn;
      /* U5 variant: a dropped job's successor may be killed with it */
      if (was_dropped && !w->fw_runs_after_drop && w->fwn) {
        unsigned k = w->fwq[0];
        w->started[k] = 1; w->dropped[k] = 1;
        for (i = 1; i < w->fwn; ++i) w->fwq[i - 1] = w->fwq[i];
        --w->fwn;
      }
    }
  }
}

/* DxgkDdiResetEngine (G4-r3): fault the context, wait bounded for the
 * firmware to end the window's jobs, abort them, drop the queue. */
static void reset_engine(world *w) {
  unsigned q, t, aborted = 0, last, f;
  ++w->resets;
  w->faulted = 1; w->resetting = 1;
  q = w->naive_reset ? (w->win.Count ? w->win.Entry[0].Fence : 0)
                     : AdmissionJobWindowQuiesceFence(&w->win);
  for (t = 0; q && t < RESET_BOUND; ++t) {
    unsigned i, done = 1;
    for (i = 0; i < w->fwn; ++i) if (w->fwq[i] <= q) done = 0;
    if (done) break;
    ++w->now; firmware(w);
  }
  if (w->fwn) violate(w, "W6 reset returned with a job in the firmware", w->fwq[0]);
  if (w->naive_reset) {
    last = w->win.Count ? w->win.Entry[0].Fence : 0;
    AdmissionJobWindowInitialize(&w->win);
  } else {
    last = AdmissionJobWindowResetAbort(&w->win, &aborted);
  }
  w->last_aborted = last;
  for (f = 1; f <= w->nsub; ++f) {
    if (w->final_[f]) continue;
    if (w->kicked[f] && f <= last) { w->aborted[f] = 1; w->final_[f] = 2; ++w->na; continue; }
    if (w->kicked[f]) violate(w, "W6 kicked job above LastAbortedFenceId", f);
    resubmit(w, f);
  }
  w->prebound = 0; w->bindnext = w->nsub + 1;
  w->fwn = 0;
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
    if (w->event) w->event(w);
    kmd(w);
    if (w->event) w->event(w);
    kmd(w);
    /* Windows TDR: the oldest kicked job ran too long */
    if (w->win.Count && !w->resetting) {
      unsigned h = w->win.Entry[0].Fence;
      if (w->started[h] && w->now - w->start[h] >= TDR_TICKS && !w->ended[h] && !w->dropped[h])
        reset_engine(w);
    }
    /* a pending preemption completes at an empty window: VidSch takes back
     * every queued (never kicked) packet */
    if (w->preempt && w->win.Count == 0 && w->fwn == 0) {
      unsigned f;
      for (f = 1; f <= w->nsub; ++f) if (!w->final_[f] && !w->kicked[f]) resubmit(w, f);
      w->prebound = 0; w->bindnext = w->nsub + 1; w->preempt = 0;
    }
  }
}

static unsigned finish(world *w) {
  unsigned f;
  for (f = 1; f <= w->nsub; ++f)
    if (!w->final_[f]) violate(w, "W5 fence never ended", f);
  if (w->win.Count || w->fwn) violate(w, "window not empty at the end", 0);
  return w->violations;
}

/* S1: alternating compute/render of one process (the GravityMark pairs
 * after rc94, without preludes): C->R pairs go depth 2. */
static unsigned s_order(int nk, int nr, int deferred) {
  world w; unsigned i;
  init(&w, deferred ? "order-deferred" : "order");
  w.naive_kick = nk; w.naive_report = nr; w.deferred = deferred;
  for (i = 0; i < 16; ++i) add(&w, i & 1 ? K_R : K_C, OUT_OK, 7, 100 + i);
  run(&w, 400);
  finish(&w);
  if (!nk && !nr && (!w.depth2 || w.max_fw != 2)) { printf("order: no depth-2 kick (%u)\n", w.depth2); ++w.violations; }
  return w.violations;
}
/* S2: pairs that must not share the firmware today. */
static unsigned s_pairs(int nk, unsigned caps) {
  world w;
  init(&w, caps ? "pairs-caps" : "pairs");
  w.naive_kick = nk; w.caps = caps;
  add(&w, K_C, OUT_OK, 7, 0);  add(&w, K_RP, OUT_OK, 7, 1);  /* sidecar */
  add(&w, K_R, OUT_OK, 7, 2);  add(&w, K_R, OUT_OK, 7, 3);   /* other scene */
  add(&w, K_R, OUT_OK, 7, 3);                                 /* same scene */
  add(&w, K_C, OUT_OK, 7, 0);  add(&w, K_C, OUT_OK, 7, 0);   /* sidecar */
  add(&w, K_R, OUT_OK, 9, 4);                                 /* other process */
  add(&w, K_P, OUT_OK, 9, 0);  add(&w, K_C, OUT_OK, 9, 0);   /* present */
  run(&w, 400);
  return finish(&w);
}
/* S3: N faults with N+1 kicked behind it (both U5 variants). */
static unsigned s_fault(int nk, int nr, int runs_after) {
  world w;
  init(&w, runs_after ? "fault-runs" : "fault-killed");
  w.naive_kick = nk; w.naive_report = nr; w.fw_runs_after_drop = runs_after;
  add(&w, K_C, OUT_OK, 7, 0); add(&w, K_R, OUT_OK, 7, 1);
  add(&w, K_C, OUT_FAULT, 7, 0); add(&w, K_R, OUT_OK, 7, 2);
  add(&w, K_C, OUT_OK, 7, 0); add(&w, K_R, OUT_OK, 7, 3);
  add(&w, K_C, OUT_OK, 7, 0);
  run(&w, 300);
  finish(&w);
  if (!nk && !nr && (!w.repd[3] || (!runs_after && !w.repd[4]) || w.depth2 < 2))
    { puts("fault: drop not reported or no depth-2 kick"); ++w.violations; }
  return w.violations;
}
/* S4: a hung N with N+1 kicked: the TDR reset quiesces and aborts both. */
static unsigned s_tdr(int nreset, int runs_after) {
  world w; unsigned f;
  init(&w, runs_after ? "tdr-runs" : "tdr-killed");
  w.naive_reset = nreset; w.fw_runs_after_drop = runs_after;
  add(&w, K_C, OUT_HANG, 7, 0); add(&w, K_R, OUT_OK, 7, 1);
  add(&w, K_C, OUT_OK, 7, 0); add(&w, K_R, OUT_OK, 7, 2);
  run(&w, 400);
  finish(&w);
  if (!nreset) {
    if (w.resets != 1 || w.last_aborted != 2 || !w.aborted[1] || !w.aborted[2])
      { printf("tdr: both jobs not aborted (last %u)\n", w.last_aborted); ++w.violations; }
    for (f = 1; f <= w.nsub; ++f) if (w.kicked[f] && w.resub[f]) ++w.violations;
  }
  return w.violations;
}
/* S5: a preemption request while N runs and N+1 could be kicked. */
static void ev_preempt(world *w) {
  if (w->win.Count == 1 && w->prebound && w->nk == 1) w->preempt = 1;
}
static unsigned s_preempt(int nk) {
  world w;
  init(&w, "preempt");
  w.naive_kick = nk; w.event = ev_preempt;
  add(&w, K_C, OUT_OK, 7, 0); add(&w, K_R, OUT_OK, 7, 1);
  add(&w, K_C, OUT_OK, 7, 0);
  run(&w, 200);
  return finish(&w);
}
/* S6 (rc63): the scheduler faults while N runs with N+1 kicked; nothing is
 * reported or kicked until the TDR reset aborts both. */
static void ev_fault(world *w) {
  if (w->win.Count == 1 && w->prebound && w->nk == 1 && !w->resets) w->faulted = 1;
}
static unsigned s_rc63(int nk) {
  world w;
  init(&w, "rc63");
  w.naive_kick = nk; w.event = ev_fault;
  add(&w, K_C, OUT_OK, 7, 0); add(&w, K_R, OUT_OK, 7, 1);
  add(&w, K_C, OUT_OK, 7, 0); add(&w, K_R, OUT_OK, 7, 2);
  run(&w, 6);
  if (w.faulted) reset_engine(&w);
  w.event = NULL;
  run(&w, 200);
  return finish(&w);
}
/* S7: a paging packet heads the queue between N and N+1. */
static void ev_paging(world *w) { w->paging = w->win.Count == 1 && w->nk == 1; }
static unsigned s_paging(int nk) {
  world w;
  init(&w, "paging");
  w.naive_kick = nk; w.event = ev_paging;
  add(&w, K_C, OUT_OK, 7, 0); add(&w, K_R, OUT_OK, 7, 1);
  run(&w, 100);
  return finish(&w);
}

static int unit_checks(void) {
  ADMISSION_JOB_WINDOW win; int d = 0; unsigned n = 0;
  AdmissionJobWindowInitialize(&win);
  if (!AdmissionJobWindowKick(&win, 0xfffffffeu) || !AdmissionJobWindowKick(&win, 1u) ||
      AdmissionJobWindowKick(&win, 2u)) return 1;              /* wrap ok, depth 2 */
  if (AdmissionJobWindowReportable(&win, &d) != 0u) return 2;
  if (!AdmissionJobWindowEnded(&win, 1u, 0) || AdmissionJobWindowEnded(&win, 1u, 0)) return 3;
  if (AdmissionJobWindowReportable(&win, &d) != 0u) return 4;  /* newest first: held */
  if (!AdmissionJobWindowEnded(&win, 0xfffffffeu, 1) || !win.Poisoned) return 5;
  if (AdmissionJobWindowReportable(&win, &d) != 0xfffffffeu || !d) return 6;
  if (AdmissionJobWindowReported(&win, 1u)) return 7;          /* not the oldest */
  if (!AdmissionJobWindowReported(&win, 0xfffffffeu) || win.Count != 1u || !win.Poisoned) return 8;
  if (AdmissionJobWindowKick(&win, 3u)) return 9;              /* poisoned */
  if (AdmissionJobWindowQuiesceFence(&win) != 1u) return 10;
  if (AdmissionJobWindowResetAbort(&win, &n) != 1u || n != 1u || win.Count || win.Poisoned) return 11;
  if (AdmissionJobWindowKick(&win, 5u) != 1 || AdmissionJobWindowKick(&win, 5u)) return 12;
  if (!AdmissionJobWindowStampReached(0x8300u, 0x8300u) ||
      !AdmissionJobWindowStampReached(0x100u, 0xffffff00u) ||
      AdmissionJobWindowStampReached(0x8200u, 0x8300u) ||
      AdmissionJobWindowStampReached(5u, 0u)) return 13;
  if (AdmissionJobWindowBarrier(K_C, K_R) != ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE ||
      AdmissionJobWindowBarrier(K_R, K_R) != ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D ||
      AdmissionJobWindowBarrier(K_RP, K_C) != ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D ||
      AdmissionJobWindowBarrier(K_C, K_RP) != ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE ||
      AdmissionJobWindowBarrier(K_P, K_R) != ADMISSION_JOB_WINDOW_BARRIER_NONE) return 14;
  if (AdmissionJobWindowKickRefusal(NULL) != 99u) return 15;
  return 0;
}

int main(void) {
  unsigned bad = 0, naive = 0;
  int unit = unit_checks();
  if (unit) { printf("FAIL unit %d\n", unit); return 1; }
  bad += s_order(0, 0, 0);
  bad += s_order(0, 0, 1);
  bad += s_pairs(0, 0);
  bad += s_pairs(0, ADMISSION_JOB_WINDOW_CAP_SECOND_SIDECAR | ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER |
                    ADMISSION_JOB_WINDOW_CAP_TA_ON_3D | ADMISSION_JOB_WINDOW_CAP_COMPUTE_BARRIER);
  bad += s_fault(0, 0, 1);
  bad += s_fault(0, 0, 0);
  bad += s_tdr(0, 1);
  bad += s_tdr(0, 0);
  bad += s_preempt(0);
  bad += s_rc63(0);
  bad += s_paging(0);
  if (bad) { printf("FAIL %u\n", bad); return 1; }
  puts("--- naive policies (each must break an invariant) ---");
  naive += s_pairs(1, 0) != 0;
  naive += s_fault(1, 0, 1) != 0;
  naive += s_fault(0, 1, 1) != 0;
  naive += s_tdr(1, 1) != 0;
  naive += s_preempt(1) != 0;
  naive += s_rc63(1) != 0;
  naive += s_paging(1) != 0;
  if (naive != 7) { printf("FAIL: %u of 7 naive policies broke an invariant\n", naive); return 2; }
  puts("PASS");
  return 0;
}
'''


def _build_and_run(tmp: Path, source: Path = SOURCE) -> subprocess.CompletedProcess:
    path = tmp / "window_model.c"
    exe = tmp / "window_model"
    path.write_text(MODEL)
    built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                            "-Werror", "-I", str(INCLUDE), str(path), str(source),
                            "-o", str(exe)], capture_output=True, text=True)
    if built.returncode != 0:
        return built
    return subprocess.run([str(exe)], capture_output=True, text=True)


class JobWindowModel(unittest.TestCase):
    def test_model_invariants_hold_and_naive_policies_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            ran = _build_and_run(Path(directory))
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("PASS", ran.stdout)

    def test_dropping_a_refusal_breaks_the_model(self):
        """RED evidence: each guard is load-bearing (mutation check)."""
        text = SOURCE.read_text()
        mutations = {
            "sidecar": ("    return 57u;\n", "    return 0u;\n"),
            "scene": ("return 58u;", "return 0u;"),
            "process": ("return 55u;", "return 0u;"),
            "preempt": ("if (s->PreemptionPhase != 0u) return 4u;", ""),
            "quiesce-newest": ("return Window->Entry[Window->Count - 1u].Fence;\n}",
                               "return Window->Entry[0].Fence;\n}"),
        }
        for name, (old, new) in mutations.items():
            with self.subTest(name), tempfile.TemporaryDirectory() as directory:
                self.assertIn(old, text, name)
                mutated = Path(directory) / "render_job_window.c"
                mutated.write_text(text.replace(old, new, 1))
                ran = _build_and_run(Path(directory), mutated)
                self.assertTrue(ran.stdout, name + ": did not build\n" + ran.stderr)
                self.assertNotEqual(ran.returncode, 0, name + ": mutation survived\n" + ran.stdout)


SHARED = ROOT / "drivers/apple-agx/shared"
RC102_MODEL = Path(__file__).resolve().parent / "render_job_window_rc102_model.c"
RC102_SOURCES = {
    "codec": SHARED / "src/apple_agx_g13_codec.c",
    "runtime": SHARED / "src/apple_agx_g13_queue_runtime.c",
    "provider": SHARED / "src/apple_agx_g13_queue_provider.c",
    "backend": SHARED / "src/apple_agx_backend_runtime.c",
    "submission": SHARED / "src/apple_agx_submission.c",
    "firmware": SHARED / "src/apple_agx_firmware.c",
    "rtkit": SHARED / "src/apple_agx_rtkit.c",
    "scheduler": SHARED / "src/apple_agx_scheduler.c",
    "window": SOURCE,
}


def _build_and_run_rc102(tmp: Path, mutated=None) -> subprocess.CompletedProcess:
    sources = dict(RC102_SOURCES)
    if mutated:
        name, old, new = mutated
        text = sources[name].read_text()
        if old not in text:
            raise AssertionError(f"{name}: mutation anchor missing: {old!r}")
        path = tmp / sources[name].name
        path.write_text(text.replace(old, new, 1))
        sources[name] = path
    exe = tmp / "rc102_model"
    built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                            "-Werror", "-fsanitize=address,undefined",
                            "-I", str(SHARED / "include"), "-I", str(INCLUDE),
                            str(RC102_MODEL), *map(str, sources.values()), "-o", str(exe)],
                           capture_output=True, text=True)
    if built.returncode != 0:
        return built
    return subprocess.run([str(exe)], capture_output=True, text=True)


class JobWindowRealStateMachine(unittest.TestCase):
    """rc102: two jobs in flight through the real queue runtime, queue
    provider, backend runtime, scheduler and window
    (render_job_window_rc102_model.c).

    WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A kicked successor
    that is lost, reported out of order or twice, handed back to VidSch by
    a preemption or reset, or completed without its own stamps; a reset
    that returns while the firmware still holds a job of the window; a
    batched (U4) pair that is not reported in one poll. Scenarios: order
    (every pair class), depth 1 (switch off), preludes, refused pairs,
    faults in either job (both U5 variants), a TDR with two outstanding,
    preemption with a never-kicked packet behind, the rc63 scheduler fault,
    paging, and the rc97 drill with the job behind queued or kicked."""

    def test_two_in_flight_invariants(self):
        with tempfile.TemporaryDirectory() as directory:
            ran = _build_and_run_rc102(Path(directory))
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("PASS", ran.stdout)

    def test_each_depth2_step_is_load_bearing(self):
        """RED evidence: removing one step of the real code breaks the model."""
        mutations = {
            "successor not observed": ("runtime",
                "      !AppleAgxG13ObserveSuccessor(Runtime, &event))",
                "      0)"),
            "no promotion": ("runtime",
                "       status == AppleAgxG13QueueCompletionRecovered) &&\n      Runtime->Successor.Fence != 0u) {",
                "       status == AppleAgxG13QueueCompletionRecovered) &&\n      0) {"),
            "recovery bounded by the running job": ("runtime",
                "        write != newest[index]->ExpectedDonePointer)\n      return AppleAgxG13QueueRuntimeResultInvalidState;\n    if (done != write)\n      retired",
                "        (head[index] && write != head[index]->ExpectedDonePointer))\n      return AppleAgxG13QueueRuntimeResultInvalidState;\n    if (done != write)\n      retired"),
            "batched successor not taken": ("provider",
                "  if (!AppleAgxG13ProviderTake(Provider, Batch, &next))",
                "  if (1)"),
            "backend keeps no successor": ("backend",
                "  if (Runtime->SuccessorPending && nextPhase == AppleAgxBackendRuntimeReady)",
                "  if (Runtime->SuccessorPending && nextPhase == AppleAgxBackendRuntimeStopping)"),
            "preemption hands the kicked fence back": ("scheduler",
                "  kicked = AppleAgxSchedulerTakeKicked(Scheduler);\n  if (Scheduler->KickedFence != 0u && kicked == 0u)",
                "  kicked = 0u;\n  Scheduler->KickedFence = 0u;\n  if (0)"),
            "no idle-queue refusal": ("window", "if (s->NextNeedsIdle) return 61u;", ""),
            "successor verdict without stamps": ("runtime",
                "        reached = APPLE_AGX_BACKEND_FALSE;",
                "        reached = APPLE_AGX_BACKEND_TRUE;"),
        }
        for name, mutation in mutations.items():
            with self.subTest(name), tempfile.TemporaryDirectory() as directory:
                ran = _build_and_run_rc102(Path(directory), mutation)
                self.assertTrue(ran.stdout, name + ": did not build\n" + ran.stderr)
                self.assertNotEqual(ran.returncode, 0, name + ": mutation survived\n" + ran.stdout[-2000:])


class JobWindowSource(unittest.TestCase):
    def test_kmd_clean_and_in_the_project(self):
        for path in (SOURCE, HEADER):
            text = path.read_text()
            self.assertIsNone(re.search(r"#include\s*<", text), path.name)
        self.assertIn(r'<ClCompile Include="src\render_job_window.c" />', VCXPROJ.read_text())


if __name__ == "__main__":
    unittest.main()
