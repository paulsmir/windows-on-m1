"""K1 (Wom1CrossProcessRenderPairs): the kick decision and its receipt.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
K1 lets a render of another process be kicked behind a running plain render
(capability bit 0x40). The switch is opt-in, so the first defect it must not
have is a changed decision with the bit clear: every pair the bench runs
today has to get exactly today's refusal. The second is a wider admission
than the one that was reasoned about: a pair kicked into the running job's
VM slot (one translation for two address spaces), or kicked while its
buffer manager is not provably live in its own firmware slot (a manager
restored, evicted or shared under a running job). So:
  T1 with the bit clear the decision equals a frozen copy of the decision
     before K1 for every state of a product space over kinds, processes,
     VM slots, the capabilities 0x01-0x20, the modes and every later guard;
  T2 with the bit set, every state outside the K1 class still equals the
     frozen decision, and a state of the class is decided exactly like the
     same-process render pair whose manager-slot proof is OtherManagerSlot,
     after the VM slot rule (zero or equal slot: 6). Nothing else differs:
     the lead Barrier classes, the kick classes, the sidecar sets, the
     idle-queue need and the prelude switch refuse as they do today;
  T3 the pair classes recorded by the xproc receipt of EXP2318 x, replayed
     with the bit clear, reproduce the recorded counters word for word and
     leave every K1 counter zero; with the bit set only the two
     render -> render classes change, by the manager-slot proof;
  T4 the lead waivers (OV2, OV5 waiver, C2a private lead) never fire for a
     K1 pair, with their switches on or off, and the two unset switches
     named by the run (Wom1CrossProcessPrivateLead, Wom1Ov2NoHazardLead)
     are not inputs of the decision;
  T5 the receipt counts the new class in exactly one place per decision and
     the v3 words keep their offsets, so a 20 s arm can prove kicked > 0 and
     failed == 0 and name the gate that refused;
  T6 the KMD wiring cannot turn the class on by accident or resolve what the
     rule could not prove: the bit has one writer, under its own registry
     value read once at adapter start on top of C1 and manager slots; the
     lead switches and the K1 switch never appear in one statement; the
     second-slot proof is a read on the binding of the Prebound head behind
     the running job's live manager, defaults to "unproven", and is repeated
     on the binding BeginSuccessorJob gave before anything is built or
     submitted; every undo of an unpublished build also names the second
     manager's fence (else a failed kick leaves a pending fence that refuses
     the class until the next start).
"""
import importlib.util
import os
import re
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
SHARED = ROOT / 'drivers/apple-agx/shared'

# Wom1CrossProcessPairsReceipt after arm x of EXP2318 (hardware observation,
# .local/experiments/EXP2318-n4c-switch-unset/evidence/EXP2318/
# kmd-receipts-post-x.txt): receipt v3, 240 bytes.
EXP2318_X = (
    '03000000f0000000f0370000002100000021000000000000000000003700000006000000'
    '000000000000000000000000000000000000000000000000e11600000f00000000000000'
    '000000000000000000000000000000000000000000000000c7780200c678020002000000'
    '0300000037000000bf780200000000000000000000000000000000000000000000000000'
    '00000000000000000000000000000000000000000000000000000000000000006c000000'
    '741600000000000000000000010000000000000000000000000000000000000000000000'
    '000000000000000000000000000000000000000000000000')


def build_run(code, optimise=False):
    with tempfile.TemporaryDirectory(prefix='xproc-render-') as tmp:
        src = Path(tmp) / 'test.c'
        src.write_text(code)
        exe = Path(tmp) / 'test'
        flags = ['-O2'] if optimise else ['-fsanitize=address,undefined']
        cmd = [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
               *flags, f'-I{RENDER / "include"}', f'-I{SHARED / "include"}', str(src),
               str(RENDER / 'src/render_job_window.c'), '-o', str(exe)]
        built = subprocess.run(cmd, capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)
        return subprocess.run([str(exe)], capture_output=True, text=True, timeout=300)


# The decision before K1 (render_job_window.c at a2b769fe9), with its three
# file-local helpers. The public helpers it calls are not changed by K1.
FROZEN = r'''
#include <stdio.h>
#include <string.h>
#include "render_job_window.h"

#define K_N ADMISSION_JOB_WINDOW_KIND_NONE
#define K_R ADMISSION_JOB_WINDOW_KIND_RENDER
#define K_RP ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE
#define K_C ADMISSION_JOB_WINDOW_KIND_COMPUTE
#define K_P ADMISSION_JOB_WINDOW_KIND_PRESENT
#define K_O ADMISSION_JOB_WINDOW_KIND_OTHER
#define XP ADMISSION_JOB_WINDOW_CAP_CROSS_PROCESS
#define XR ADMISSION_JOB_WINDOW_CAP_XPROC_RENDER

static unsigned int frozen_kind(unsigned int Kind, unsigned int Capabilities) {
  return Kind == ADMISSION_JOB_WINDOW_KIND_PRESENT &&
         (Capabilities & ADMISSION_JOB_WINDOW_CAP_PRESENT_PAIRS)
      ? ADMISSION_JOB_WINDOW_KIND_COMPUTE : Kind;
}
static unsigned int frozen_g4(unsigned int Kind) {
  return Kind == ADMISSION_JOB_WINDOW_KIND_RENDER ||
         Kind == ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE ||
         Kind == ADMISSION_JOB_WINDOW_KIND_COMPUTE ? 1u : 0u;
}
static unsigned int frozen_barrier_available(unsigned int Barrier,
                                             unsigned int Capabilities) {
  switch (Barrier) {
    case ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE:
      return 1u;
    case ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D:
      return (Capabilities & ADMISSION_JOB_WINDOW_CAP_TA_ON_3D) ? 1u : 0u;
    case ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D:
    case ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE:
      return (Capabilities & ADMISSION_JOB_WINDOW_CAP_COMPUTE_BARRIER) ? 1u : 0u;
    default:
      return 0u;
  }
}
__attribute__((unused))
static unsigned int frozen(const ADMISSION_JOB_WINDOW_KICK_STATE *s) {
  const ADMISSION_JOB_WINDOW_JOB *n, *m;
  unsigned int nk, mk;
  if (!s) return 99u;
  if (!s->Enabled) return 1u;
  if (s->Stopping || s->Resetting) return 2u;
  if (s->Faulted) return 3u;
  if (s->PreemptionPhase != 0u) return 4u;
  n = &s->Running;
  m = &s->Next;
  nk = frozen_kind(n->Kind, s->Capabilities);
  mk = frozen_kind(m->Kind, s->Capabilities);
  if (s->InFlight != 1u || n->Fence == 0u) return 50u;
  if (s->WindowPoisoned) return 51u;
  if (!s->NextPrebound || m->Fence == 0u || m->Fence == n->Fence) return 52u;
  if (s->QueuedFence == 0u || s->QueuedFence != m->Fence) return 53u;
  if (s->PagingPending ||
      (s->CpuHeadFence != 0u && s->CpuHeadFence == m->Fence)) return 54u;
  if (n->Process == 0ULL || m->Process != n->Process) {
    if (!(s->Capabilities & ADMISSION_JOB_WINDOW_CAP_CROSS_PROCESS) ||
        n->Process == 0ULL || m->Process == 0ULL ||
        n->Kind != ADMISSION_JOB_WINDOW_KIND_RENDER ||
        m->Kind != ADMISSION_JOB_WINDOW_KIND_COMPUTE)
      return 55u;
    if (n->VmSlot == 0u || m->VmSlot == 0u || n->VmSlot == m->VmSlot)
      return ADMISSION_JOB_WINDOW_REFUSAL_CROSS_PROCESS_INELIGIBLE;
  }
  if (!frozen_g4(nk) || !frozen_g4(mk)) return 56u;
  if (n->Kind == ADMISSION_JOB_WINDOW_KIND_PRESENT &&
      m->Kind == ADMISSION_JOB_WINDOW_KIND_PRESENT) return 56u;
  if (s->General && !s->Prelude &&
      (n->Kind == ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE ||
       m->Kind == ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE))
    return ADMISSION_JOB_WINDOW_REFUSAL_PRELUDE_UNPROVEN;
  if (AdmissionJobWindowUsesSidecar(nk) &&
      AdmissionJobWindowUsesSidecar(mk) &&
      !(s->Capabilities & ADMISSION_JOB_WINDOW_CAP_SECOND_SIDECAR))
    return 57u;
  if (AdmissionJobWindowUsesManager(n->Kind) &&
      AdmissionJobWindowUsesManager(m->Kind)) {
    if (s->General ? !s->SameManagerSlot :
        (n->SceneId == 0ULL || m->SceneId != n->SceneId)) return 58u;
    if (!(s->Capabilities & ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER))
      return 59u;
  }
  if (!frozen_barrier_available(AdmissionJobWindowBarrier(nk, mk),
                                s->Capabilities))
    return 60u;
  if (s->KickClasses != 0u &&
      !(s->KickClasses & (1u << AdmissionJobWindowBarrier(nk, mk))))
    return 64u;
  if (s->NextNeedsIdle) return 61u;
  if (s->SetsValid && s->RunningSet == s->NextSet) return 62u;
  if (s->PreludeNotifyGate &&
      n->Kind == ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE &&
      !s->RunningPreludeComplete && !s->PreludeGateExpired &&
      mk == ADMISSION_JOB_WINDOW_KIND_COMPUTE)
    return ADMISSION_JOB_WINDOW_REFUSAL_PRELUDE_NOTIFY_GATE;
  return 0u;
}

__attribute__((unused))
static ADMISSION_JOB_WINDOW_KICK_STATE base(unsigned rk, unsigned nk,
    unsigned long long rp, unsigned long long np, unsigned rs, unsigned ns,
    unsigned caps) {
  ADMISSION_JOB_WINDOW_KICK_STATE s;
  memset(&s, 0, sizeof(s));
  s.Enabled = 1u; s.InFlight = 1u; s.NextPrebound = 1u; s.QueuedFence = 11u;
  s.Capabilities = caps;
  s.General = 1u; s.Prelude = 1u;
  s.Running.Fence = 10u; s.Running.Kind = rk; s.Running.Process = rp;
  s.Running.SceneId = 7ULL; s.Running.Context = rp * 16ULL; s.Running.VmSlot = rs;
  s.Next.Fence = 11u; s.Next.Kind = nk; s.Next.Process = np;
  s.Next.SceneId = 7ULL; s.Next.Context = np * 16ULL; s.Next.VmSlot = ns;
  return s;
}

/* The K1 class, written again here from the specification. */
__attribute__((unused))
static unsigned k1_class(const ADMISSION_JOB_WINDOW_KICK_STATE *s) {
  return (s->Capabilities & XR) && s->Running.Process != 0ULL &&
         s->Next.Process != 0ULL && s->Running.Process != s->Next.Process &&
         s->Running.Kind == K_R && (s->Next.Kind == K_R || s->Next.Kind == K_RP);
}

/* T2: what a state must be decided as. */
__attribute__((unused))
static unsigned want(const ADMISSION_JOB_WINDOW_KICK_STATE *s) {
  ADMISSION_JOB_WINDOW_KICK_STATE same;
  unsigned e;
  if (!k1_class(s)) return frozen(s);
  same = *s;
  same.Next.Process = same.Running.Process;
  same.SameManagerSlot = s->OtherManagerSlot;
  e = frozen(&same);
  /* the guards before the process term keep their order */
  if (e == 1u || e == 2u || e == 3u || e == 4u || (e >= 50u && e <= 54u)) return e;
  if (s->Running.VmSlot == 0u || s->Next.VmSlot == 0u ||
      s->Running.VmSlot == s->Next.VmSlot)
    return 6u;
  return e;
}
'''

TRUTH = FROZEN + r'''
static unsigned long long states, differ, k1_states, k1_kicked, mismatches;

static void check(const ADMISSION_JOB_WINDOW_KICK_STATE *s) {
  unsigned got = AdmissionJobWindowKickRefusal(s), expected = want(s);
  ++states;
  if (k1_class(s)) { ++k1_states; if (got == 0u) ++k1_kicked; }
  if (expected != frozen(s)) ++differ;
  if (got != expected && mismatches++ < 12u)
    printf("MISMATCH got %u want %u frozen %u: kinds %u>%u procs %llu>%llu slots %u>%u "
           "caps %#x general %u prelude %u same %u other %u idle %u sets %u classes %#x\n",
           got, expected, frozen(s), s->Running.Kind, s->Next.Kind, s->Running.Process,
           s->Next.Process, s->Running.VmSlot, s->Next.VmSlot, s->Capabilities, s->General,
           s->Prelude, s->SameManagerSlot, s->OtherManagerSlot, s->NextNeedsIdle,
           s->SetsValid, s->KickClasses);
}

int main(void) {
  static const unsigned kinds[] = {K_N, K_R, K_RP, K_C, K_P, K_O};
  static const unsigned long long procs[][2] =
      {{100, 100}, {100, 200}, {0, 200}, {100, 0}, {0, 0}};
  static const unsigned slots[][2] = {{0, 3}, {2, 0}, {2, 2}, {2, 3}};
  /* general with the scene equal; legacy with the scene equal, other, zero */
  static const unsigned long long modes[][3] = {{1, 7, 7}, {0, 7, 7}, {0, 7, 8}, {0, 0, 0}};
  static const unsigned classes[] = {0u, 1u << ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D,
      1u << ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D};
  unsigned a, b, p, sl, caps, mode, cl, flags, bit, early;
  ADMISSION_JOB_WINDOW_KICK_STATE s;
  for (bit = 0; bit < 2; ++bit) {
    states = differ = k1_states = k1_kicked = 0ULL;
    for (caps = 0; caps < 64u; ++caps)
     for (a = 0; a < 6; ++a) for (b = 0; b < 6; ++b)
      for (p = 0; p < 5; ++p) for (sl = 0; sl < 4; ++sl)
       for (mode = 0; mode < 4; ++mode) for (cl = 0; cl < 3; ++cl)
        for (flags = 0; flags < 64u; ++flags) {
          s = base(kinds[a], kinds[b], procs[p][0], procs[p][1], slots[sl][0],
                   slots[sl][1], caps | (bit ? XR : 0u));
          s.General = (unsigned)modes[mode][0];
          s.Running.SceneId = modes[mode][1]; s.Next.SceneId = modes[mode][2];
          s.KickClasses = classes[cl];
          s.Prelude = flags & 1u; s.SameManagerSlot = (flags >> 1) & 1u;
          s.OtherManagerSlot = (flags >> 2) & 1u; s.NextNeedsIdle = (flags >> 3) & 1u;
          s.SetsValid = (flags >> 4) & 1u; s.RunningSet = s.NextSet = 1u;
          s.PreludeNotifyGate = (flags >> 5) & 1u;
          check(&s);
        }
    /* every guard before the process term, one at a time */
    for (caps = 0; caps < 64u; ++caps)
     for (a = 0; a < 6; ++a) for (b = 0; b < 6; ++b)
      for (p = 0; p < 5; ++p) for (early = 0; early < 13u; ++early)
       for (flags = 0; flags < 4u; ++flags) {
         s = base(kinds[a], kinds[b], procs[p][0], procs[p][1], 2u, 3u,
                  caps | (bit ? XR : 0u));
         s.SameManagerSlot = flags & 1u; s.OtherManagerSlot = (flags >> 1) & 1u;
         switch (early) {
           case 0: s.Enabled = 0u; break;
           case 1: s.Stopping = 1u; break;
           case 2: s.Resetting = 1u; break;
           case 3: s.Faulted = 1u; break;
           case 4: s.PreemptionPhase = 2u; break;
           case 5: s.InFlight = 2u; break;
           case 6: s.Running.Fence = 0u; break;
           case 7: s.WindowPoisoned = 1u; break;
           case 8: s.NextPrebound = 0u; break;
           case 9: s.Next.Fence = 10u; s.QueuedFence = 10u; break;
           case 10: s.QueuedFence = 12u; break;
           case 11: s.PagingPending = 1u; break;
           default: s.CpuHeadFence = 11u; break;
         }
         check(&s);
       }
    printf("bit %u states %llu differ-from-frozen %llu k1 %llu kicked %llu mismatches %llu\n",
           bit, states, differ, k1_states, k1_kicked, mismatches);
  }
  expect_null: printf("null %u\n", AdmissionJobWindowKickRefusal(0));
  return mismatches != 0ULL;
}
'''.replace('  expect_null: ', '  ')

CASES = FROZEN + r'''
static unsigned failures;
static void expect(const char *what, unsigned got, unsigned wanted) {
  if (got != wanted) { printf("FAIL %s: got %u want %u\n", what, got, wanted); ++failures; }
}
#define CAPS (ADMISSION_JOB_WINDOW_CAPS_RC98 | ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER | XP)

static ADMISSION_JOB_WINDOW_KICK_STATE k1(unsigned nk, unsigned caps) {
  ADMISSION_JOB_WINDOW_KICK_STATE s = base(K_R, nk, 100, 200, 2, 3, caps);
  s.OtherManagerSlot = 1u;
  return s;
}

int main(void) {
  ADMISSION_JOB_WINDOW_KICK_STATE s;
  unsigned i, on;
  static const unsigned nexts[] = {K_R, K_RP};

  expect("bit value", XR, 0x40u);
  expect("bit is new", XR & (ADMISSION_JOB_WINDOW_CAPS_RC98 | ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER |
                              ADMISSION_JOB_WINDOW_CAP_PRESENT_PAIRS | XP), 0u);
  for (i = 0; i < 2; ++i) {
    /* the admitted pair, and its Barrier class (the lead Barrier is built from it) */
    s = k1(nexts[i], CAPS | XR);
    expect("admitted", AdmissionJobWindowKickRefusal(&s), 0u);
    expect("class", AdmissionJobWindowXprocRenderClass(s.Capabilities, &s.Running, &s.Next), 1u);
    expect("barrier", AdmissionJobWindowBarrierEx(s.Running.Kind, s.Next.Kind, s.Capabilities),
           nexts[i] == K_R ? ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D
                           : ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D);
    /* 0x40 does not need 0x20, and 0x20 alone admits nothing of the class */
    s = k1(nexts[i], (CAPS & ~XP) | XR);
    expect("without 0x20", AdmissionJobWindowKickRefusal(&s), 0u);
    s = k1(nexts[i], CAPS);
    expect("bit clear", AdmissionJobWindowKickRefusal(&s), 55u);
    expect("class off", AdmissionJobWindowXprocRenderClass(s.Capabilities, &s.Running, &s.Next), 0u);
    /* VM slots */
    s = k1(nexts[i], CAPS | XR); s.Next.VmSlot = 2u;
    expect("equal slot", AdmissionJobWindowKickRefusal(&s), 6u);
    s = k1(nexts[i], CAPS | XR); s.Next.VmSlot = 0u;
    expect("next slot 0", AdmissionJobWindowKickRefusal(&s), 6u);
    s = k1(nexts[i], CAPS | XR); s.Running.VmSlot = 0u;
    expect("running slot 0", AdmissionJobWindowKickRefusal(&s), 6u);
    /* the manager-slot proof; the same-slot flag is not a substitute */
    s = k1(nexts[i], CAPS | XR); s.OtherManagerSlot = 0u;
    expect("slot unproven", AdmissionJobWindowKickRefusal(&s), 58u);
    s.SameManagerSlot = 1u;
    expect("same-slot flag", AdmissionJobWindowKickRefusal(&s), 58u);
    s = k1(nexts[i], CAPS | XR); s.General = 0u;
    expect("legacy same scene", AdmissionJobWindowKickRefusal(&s), 0u);
    s.Next.SceneId = 8ULL;
    expect("legacy other scene", AdmissionJobWindowKickRefusal(&s), 58u);
    s = k1(nexts[i], (CAPS & ~ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER) | XR);
    expect("no live manager", AdmissionJobWindowKickRefusal(&s), 59u);
    /* every later guard */
    s = k1(nexts[i], CAPS | XR); s.NextNeedsIdle = 1u;
    expect("needs idle", AdmissionJobWindowKickRefusal(&s), 61u);
    s = k1(nexts[i], CAPS | XR); s.SetsValid = 1u; s.RunningSet = s.NextSet = 1u;
    expect("one sidecar set", AdmissionJobWindowKickRefusal(&s), 62u);
    s = k1(nexts[i], CAPS | XR);
    s.KickClasses = 1u << ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE;
    expect("class not kicked", AdmissionJobWindowKickRefusal(&s), 64u);
    s = k1(nexts[i], (CAPS & ~(nexts[i] == K_R ? ADMISSION_JOB_WINDOW_CAP_TA_ON_3D
                                               : ADMISSION_JOB_WINDOW_CAP_COMPUTE_BARRIER)) | XR);
    expect("no barrier", AdmissionJobWindowKickRefusal(&s), 60u);
    s = k1(nexts[i], CAPS | XR); s.PagingPending = 1u;
    expect("paging", AdmissionJobWindowKickRefusal(&s), 54u);
    s = k1(nexts[i], CAPS | XR); s.PreludeNotifyGate = 1u;
    expect("R14 gate", AdmissionJobWindowKickRefusal(&s), 0u); /* N is a plain render */
    /* T4: no lead waiver, switches on or off */
    for (on = 0; on < 2; ++on) {
      unsigned barrier;
      s = k1(nexts[i], CAPS | XR);
      s.Next.Independent = s.Next.TaIndependent = s.Next.PrivateOnly = 1u;
      barrier = AdmissionJobWindowBarrierEx(s.Running.Kind, s.Next.Kind, s.Capabilities);
      expect("ov2", AdmissionJobWindowOv2Refusal(on, barrier, &s.Running, &s.Next) ==
                    ADMISSION_JOB_WINDOW_OV2_WAIVE, 0u);
      expect("ov5w", AdmissionJobWindowOv5WaiverRefusal(on, barrier, &s.Running, &s.Next) ==
                     ADMISSION_JOB_WINDOW_OV5W_WAIVE, 0u);
      expect("c2a", AdmissionJobWindowXprocLeadRefusal(on, barrier, &s.Running, &s.Next) ==
                    ADMISSION_JOB_WINDOW_XPL_WAIVE, 0u);
      /* and the job flags the waivers read do not reach the decision */
      expect("flags", AdmissionJobWindowKickRefusal(&s), 0u);
    }
  }
  /* the prelude switch covers the prelude successor as it does today */
  s = k1(K_RP, CAPS | XR); s.Prelude = 0u;
  expect("prelude off", AdmissionJobWindowKickRefusal(&s), 5u);
  s = k1(K_R, CAPS | XR); s.Prelude = 0u;
  expect("prelude off, plain", AdmissionJobWindowKickRefusal(&s), 0u);
  /* classes K1 does not add */
  s = base(K_RP, K_R, 100, 200, 2, 3, CAPS | XR); s.OtherManagerSlot = 1u;
  expect("prelude running", AdmissionJobWindowKickRefusal(&s), 55u);
  s = base(K_RP, K_RP, 100, 200, 2, 3, CAPS | XR); s.OtherManagerSlot = 1u;
  expect("prelude -> prelude", AdmissionJobWindowKickRefusal(&s), 55u);
  s = base(K_C, K_R, 100, 200, 2, 3, CAPS | XR); s.OtherManagerSlot = 1u;
  expect("compute running", AdmissionJobWindowKickRefusal(&s), 55u);
  s = base(K_R, K_P, 100, 200, 2, 3, CAPS | XR | ADMISSION_JOB_WINDOW_CAP_PRESENT_PAIRS);
  expect("present", AdmissionJobWindowKickRefusal(&s), 55u);
  s = base(K_R, K_R, 0, 200, 2, 3, CAPS | XR); s.OtherManagerSlot = 1u;
  expect("no process", AdmissionJobWindowKickRefusal(&s), 55u);
  expect("no process class", AdmissionJobWindowXprocRenderClass(s.Capabilities, &s.Running, &s.Next), 0u);
  expect("null class", AdmissionJobWindowXprocRenderClass(CAPS | XR, 0, &s.Next) +
                       AdmissionJobWindowXprocRenderClass(CAPS | XR, &s.Running, 0), 0u);
  /* C1's class is untouched by the new bit and by the new flag */
  s = base(K_R, K_C, 100, 200, 2, 3, CAPS | XR);
  expect("C1 with the bit", AdmissionJobWindowKickRefusal(&s), 0u);
  s = base(K_R, K_C, 100, 200, 2, 3, XR | (CAPS & ~XP));
  expect("C1 needs 0x20", AdmissionJobWindowKickRefusal(&s), 55u);
  /* a same-process render pair never reads the new flag */
  s = base(K_R, K_R, 100, 100, 2, 2, CAPS | XR); s.OtherManagerSlot = 1u;
  expect("same process, other slot", AdmissionJobWindowKickRefusal(&s), 58u);
  printf("failures %u\n", failures);
  return failures != 0u;
}
'''

# T3/T5: replays `counts` decisions per class through the calls the KMD
# makes (decision, Note55, Note, RenderNote, Kicked) and dumps the receipt.
REPLAY = FROZEN + r'''
#define CAPS (ADMISSION_JOB_WINDOW_CAPS_RC98 | ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER | XP)
static ADMISSION_JOB_WINDOW_XPROC_RECEIPT r;
static unsigned fence = 100u;

/* reason: the owner query's result for this decision (0 = proven) */
static void decide(unsigned rk, unsigned nk, unsigned rs, unsigned ns, unsigned caps,
                   unsigned reason, unsigned undone) {
  ADMISSION_JOB_WINDOW_KICK_STATE s = base(rk, nk, 100, 200, rs, ns, caps);
  unsigned refusal, render;
  s.Running.Fence = fence; s.Next.Fence = s.QueuedFence = fence + 1u; fence += 2u;
  render = AdmissionJobWindowXprocRenderClass(caps, &s.Running, &s.Next);
  s.OtherManagerSlot = render && reason == 0u ? 1u : 0u;
  refusal = AdmissionJobWindowKickRefusal(&s);
  if (refusal == 55u) AdmissionJobWindowXprocNote55(&r, caps, &s.Running, &s.Next);
  (void)AdmissionJobWindowXprocNote(&r, refusal, &s.Running, &s.Next);
  if (render) AdmissionJobWindowXprocRenderNote(&r, refusal, reason, &s.Running, &s.Next);
  if (refusal != 0u) return;
  if (undone) {
    AdmissionJobWindowXprocUndone(&r, undone == 1u, s.Next.Fence, 0u);
    if (render) AdmissionJobWindowXprocRenderUndone(&r, undone == 1u);
    return;
  }
  AdmissionJobWindowXprocKicked(&r, &s.Running, &s.Next);
  if (render) AdmissionJobWindowXprocRenderKicked(&r, &s.Running, &s.Next, 1u, 2u);
}

int main(int argc, char **argv) {
  unsigned caps = CAPS | (argc > 1 ? XR : 0u), i;
  const unsigned char *bytes = (const unsigned char *)&r;
  (void)argv;
  AdmissionJobWindowXprocReceiptInitialize(&r);
  /* EXP2318 x: 5857 x 55 (render -> prelude render 5748, render -> render
   * 108, prelude render -> render 1), 15 x 6 and 8448 kicked render ->
   * compute pairs. With the bit set, of the 5748: 5000 proven, the rest by
   * manager reason; of the 108: 100 proven (2 fall back, 1 fails), 5 in one
   * VM slot, 3 that need idle queues. */
  for (i = 0; i < 5748u; ++i)
    decide(K_R, K_RP, 2, 3, caps,
           i < 5000u ? 0u : i < 5400u ? ADMISSION_JOB_WINDOW_XPR_MANAGER_PENDING :
           i < 5600u ? ADMISSION_JOB_WINDOW_XPR_MANAGER_UNBOUND :
           i < 5700u ? ADMISSION_JOB_WINDOW_XPR_MANAGER_ALL_BOUND :
           i < 5740u ? ADMISSION_JOB_WINDOW_XPR_MANAGER_POISONED :
           i < 5747u ? ADMISSION_JOB_WINDOW_XPR_MANAGER_UNPROVEN : 77u, 0u);
  for (i = 0; i < 100u; ++i) decide(K_R, K_R, 2, 3, caps, 0u, i < 2u ? 1u : i == 2u ? 2u : 0u);
  for (i = 0; i < 5u; ++i) decide(K_R, K_R, 3, 3, caps, 0u, 0u);
  for (i = 0; i < 3u; ++i) {
    ADMISSION_JOB_WINDOW_KICK_STATE s = base(K_R, K_R, 100, 200, 2, 3, caps);
    unsigned refusal;
    s.OtherManagerSlot = 1u; s.NextNeedsIdle = 1u;
    refusal = AdmissionJobWindowKickRefusal(&s);
    if (refusal == 55u) AdmissionJobWindowXprocNote55(&r, caps, &s.Running, &s.Next);
    (void)AdmissionJobWindowXprocNote(&r, refusal, &s.Running, &s.Next);
    if (AdmissionJobWindowXprocRenderClass(caps, &s.Running, &s.Next))
      AdmissionJobWindowXprocRenderNote(&r, refusal, 0u, &s.Running, &s.Next);
  }
  for (i = 0; i < 15u; ++i) decide(K_R, K_C, 3, 3, caps, 0u, 0u);
  for (i = 0; i < 8448u; ++i) decide(K_R, K_C, 2, 3, caps, 0u, 0u);
  decide(K_RP, K_R, 2, 3, caps, 0u, 0u);   /* the last refusal recorded was a 55 */
  /* null receipts and jobs are ignored */
  AdmissionJobWindowXprocRenderNote(0, 0u, 0u, &r_null_job(), &r_null_job());
  AdmissionJobWindowXprocRenderNote(&r, 0u, 0u, 0, 0);
  AdmissionJobWindowXprocRenderKicked(0, 0, 0, 0u, 0u);
  AdmissionJobWindowXprocRenderKicked(&r, 0, 0, 0u, 0u);
  AdmissionJobWindowXprocRenderUndone(0, 1u);
  for (i = 0; i < (unsigned)sizeof(r); ++i) printf("%02x", bytes[i]);
  printf("\n");
  return 0;
}
'''.replace('&r_null_job(), &r_null_job()', '&(ADMISSION_JOB_WINDOW_JOB){0}, &(ADMISSION_JOB_WINDOW_JOB){0}')

ABI = r'''
#include "render_job_window.h"
#include "apple_agx_render_shared_memory.h"
#define AT(f) __builtin_offsetof(ADMISSION_JOB_WINDOW_XPROC_RECEIPT, f)
_Static_assert(ADMISSION_JOB_WINDOW_XPROC_RECEIPT_VERSION == 4u, "version");
_Static_assert(sizeof(ADMISSION_JOB_WINDOW_XPROC_RECEIPT) == 328, "size");
_Static_assert(ADMISSION_JOB_WINDOW_XPROC_RECEIPT_V3_BYTES == 240u, "v3 size");
/* v3 words keep their place */
_Static_assert(AT(Kicked) == 16 && AT(Failed) == 24 && AT(RefusalCode) == 28, "v1");
_Static_assert(AT(LastFence) == 96 && AT(LeadConsidered) == 128, "v2");
_Static_assert(AT(Refused55CapabilityOff) == 168 && AT(Refused55Kinds) == 176, "v3");
/* v4 starts where v3 ended */
_Static_assert(AT(RenderConsidered) == 240 && AT(RenderKicked) == 248, "v4");
_Static_assert(AT(RenderFailed) == 256 && AT(RenderRefusedSameSlot) == 260, "v4");
_Static_assert(AT(RenderRefusedManager) == 264 && AT(RenderRefusedOther) == 288, "v4");
_Static_assert(AT(RenderLastRefusal) == 292 && AT(RenderLastFence) == 304, "v4");
_Static_assert(AT(RenderLastNextManagerSlot) == 324, "v4");
/* the manager reasons are the owner query's values */
_Static_assert(APPLE_AGX_RENDER_MANAGER_OTHER_OK == 0u, "ok");
_Static_assert(ADMISSION_JOB_WINDOW_XPR_MANAGER_UNPROVEN == APPLE_AGX_RENDER_MANAGER_OTHER_UNPROVEN, "1");
_Static_assert(ADMISSION_JOB_WINDOW_XPR_MANAGER_UNBOUND == APPLE_AGX_RENDER_MANAGER_OTHER_UNBOUND, "2");
_Static_assert(ADMISSION_JOB_WINDOW_XPR_MANAGER_ALL_BOUND == APPLE_AGX_RENDER_MANAGER_OTHER_ALL_BOUND, "3");
_Static_assert(ADMISSION_JOB_WINDOW_XPR_MANAGER_POISONED == APPLE_AGX_RENDER_MANAGER_OTHER_POISONED, "4");
_Static_assert(ADMISSION_JOB_WINDOW_XPR_MANAGER_PENDING == APPLE_AGX_RENDER_MANAGER_OTHER_PENDING, "5");
_Static_assert(ADMISSION_JOB_WINDOW_XPR_MANAGER_REASONS == APPLE_AGX_RENDER_MANAGER_OTHER_REASONS, "n");
/* the new kick-state field is the last one: zero-initialised callers and
 * positional readers of the older fields are unaffected */
_Static_assert(__builtin_offsetof(ADMISSION_JOB_WINDOW_KICK_STATE, OtherManagerSlot) +
               sizeof(unsigned int) == sizeof(ADMISSION_JOB_WINDOW_KICK_STATE), "last");
int main(void) { return 0; }
'''


def tool():
    spec = importlib.util.spec_from_file_location('agx_xproc_receipt',
                                                  ROOT / 'tools/agx_xproc_receipt.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CrossProcessRenderDecision(unittest.TestCase):
    def test_truth_table_bit_clear_is_the_frozen_decision_and_bit_set_only_adds_the_class(self):
        run = build_run(TRUTH, optimise=True)
        lines = run.stdout.strip().split('\n')
        self.assertEqual(run.returncode, 0, '\n'.join(lines[:14]) + run.stderr)
        clear, on, null = (line.split() for line in lines)
        # bit clear: the same states, none decided differently from the frozen copy
        self.assertEqual(clear[:6], ['bit', '0', 'states', clear[3], 'differ-from-frozen', '0'])
        self.assertEqual(clear[6:], ['k1', '0', 'kicked', '0', 'mismatches', '0'])
        self.assertGreater(int(clear[3]), 30_000_000)
        # bit set: only states of the class differ, and some of them are kicked
        self.assertEqual(on[3], clear[3])
        self.assertGreater(int(on[5]), 0)
        self.assertGreater(int(on[7]), int(on[5]))   # class states that still equal 55 -> other code
        self.assertGreater(int(on[9]), 0)
        self.assertEqual(on[10:], ['mismatches', '0'])
        self.assertEqual(null, ['null', '99'])

    def test_named_cases_of_the_class(self):
        run = build_run(CASES)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(run.stdout.strip(), 'failures 0')

    def test_frozen_copy_is_the_decision_before_k1(self):
        """The frozen copy must be the committed pre-K1 text, not a paraphrase:
        every line of it that is code appears in today's function or was
        replaced by K1 in the two terms K1 changes (55, 58). When the
        decision is changed on purpose later, the frozen copy moves with it
        in the same commit: it is the decision without K1."""
        source = (RENDER / 'src/render_job_window.c').read_text()
        body = source[source.index('unsigned int AdmissionJobWindowKickRefusal('):]
        body = body[:body.index('\n}\n')]
        frozen = FROZEN[FROZEN.index('static unsigned int frozen(const'):]
        frozen = frozen[:frozen.index('\n}\n')]

        def code(text):
            out = []
            for line in text.split('\n'):
                line = line.split('/*')[0].strip()
                if line and not line.startswith('*') and '*/' not in line:
                    out.append(line.replace('frozen_kind', 'window_kind').replace(
                        'frozen_g4', 'window_g4').replace('frozen_barrier_available',
                                                           'barrier_available'))
            return out
        now = ' '.join(code(body))
        missing = [line for line in code(frozen)[1:] if line not in now]
        self.assertEqual(missing, [
            'unsigned int nk, mk;',
            'if (!(s->Capabilities & ADMISSION_JOB_WINDOW_CAP_CROSS_PROCESS) ||',
            'if (s->General ? !s->SameManagerSlot :',
        ])
        # and K1 put exactly this in their place
        for line in ('unsigned int nk, mk, xr;',
                     'xr = AdmissionJobWindowXprocRenderClass(s->Capabilities, n, m);',
                     'if (!xr && (!(s->Capabilities & ADMISSION_JOB_WINDOW_CAP_CROSS_PROCESS) ||',
                     'if (s->General ? !(xr ? s->OtherManagerSlot : s->SameManagerSlot) :'):
            self.assertEqual(now.count(line), 1, line)
        self.assertEqual(now.count('xr'), 4)


class CrossProcessRenderReceipt(unittest.TestCase):
    @staticmethod
    def replay(bit):
        with tempfile.TemporaryDirectory(prefix='xproc-render-') as tmp:
            src = Path(tmp) / 'test.c'
            src.write_text(REPLAY)
            exe = Path(tmp) / 'test'
            built = subprocess.run(
                [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                 '-fsanitize=address,undefined', f'-I{RENDER / "include"}', str(src),
                 str(RENDER / 'src/render_job_window.c'), '-o', str(exe)],
                capture_output=True, text=True)
            if built.returncode:
                raise AssertionError(built.stdout + built.stderr)
            run = subprocess.run([str(exe)] + (['set'] if bit else []), capture_output=True,
                                 text=True, timeout=120)
            if run.returncode:
                raise AssertionError(run.stdout + run.stderr)
            return bytes.fromhex(run.stdout.strip())

    def test_exp2318_x_replayed_with_the_bit_clear_reproduces_the_recorded_receipt(self):
        recorded = struct.unpack('<60I', bytes.fromhex(EXP2318_X))
        data = self.replay(False)
        self.assertEqual(len(data), 328)
        now = struct.unpack('<82I', data)
        # every counter word of v3; words 24-31 and 39-41 are fences/slots of the last pair
        counters = [i for i in range(2, 60) if not 24 <= i <= 31 and not 39 <= i <= 41]
        self.assertEqual([now[i] for i in counters], [recorded[i] for i in counters])
        self.assertEqual(now[28], recorded[28])   # the last refusal: 55
        self.assertEqual((now[26], now[27]), (recorded[26], recorded[27]))  # VM slots 2 -> 3
        self.assertEqual(now[60:], (0,) * 22)     # no K1 counter moves with the bit clear
        self.assertEqual((recorded[2], recorded[3], recorded[4], recorded[6]),
                         (14320, 8448, 8448, 0))

    def test_exp2318_x_replayed_with_the_bit_set_changes_only_the_render_classes(self):
        t = tool()
        before = t.decode(bytes.fromhex(EXP2318_X))
        after = t.decode(self.replay(True))
        self.assertEqual(before['version'], 3)
        self.assertNotIn('render_pairs', before)
        self.assertEqual(after['version'], 4)
        # the compute class and the class K1 does not add are as recorded
        self.assertEqual(after['considered'], before['considered'])
        self.assertEqual(after['refused_55'], dict(capability_off=0, no_process=0,
                                                   kinds={'render+prelude -> render': 1}))
        self.assertEqual(before['refused_55']['kinds'], {
            'render -> render': 108, 'render -> render+prelude': 5748,
            'render+prelude -> render': 1})
        self.assertEqual(after['refusals'], {'58': 748, '6': 20, '61': 3, '55': 1})
        self.assertEqual(after['allowed'], 8448 + 5000 + 100)
        self.assertEqual((after['kicked'], after['fell_back'], after['failed']),
                         (8448 + 5000 + 97, 2, 1))
        self.assertEqual(after['render_pairs'], dict(
            considered=5856, allowed=5100, kicked=5097, fell_back=2, failed=1,
            refused_same_vm_slot=5,
            refused_manager_58={'pending': 400, 'unbound': 200, 'all_bound': 100,
                                'poisoned': 40, 'unproven': 8},
            refused_other=3,
            last_refusal=dict(code=61, fence=11, manager_reason='unproven'),
            last_kicked=dict(fence=after['render_pairs']['last_kicked']['fence'],
                             running_fence=after['render_pairs']['last_kicked']['fence'] - 1,
                             running_vm_slot=2, next_vm_slot=3,
                             running_manager_slot=1, next_manager_slot=2)))
        pairs = after['render_pairs']
        self.assertEqual(pairs['considered'], pairs['allowed'] + pairs['refused_same_vm_slot'] +
                         sum(pairs['refused_manager_58'].values()) + pairs['refused_other'])
        self.assertEqual(pairs['allowed'], pairs['kicked'] + pairs['fell_back'] + pairs['failed'])

    def test_decoder_reads_v3_and_v4_and_refuses_anything_else(self):
        t = tool()
        v3 = bytes.fromhex(EXP2318_X)
        self.assertEqual(t.decode(v3)['refusals'], {'55': 5857, '6': 15})
        v4 = self.replay(False)
        out = t.decode(v4)
        self.assertEqual(out['refusals'], {'55': 5857, '6': 15})
        self.assertEqual(out['render_pairs']['considered'], 0)
        with self.assertRaises(ValueError):
            t.decode(v4[:240])                       # v4 header, v3 length
        with self.assertRaises(ValueError):
            t.decode(struct.pack('<2I', 4, 240) + v4[8:])
        with self.assertRaises(ValueError):
            t.decode(struct.pack('<2I', 5, 328) + v4[8:])
        with self.assertRaises(ValueError):
            t.decode(struct.pack('<2I', 3, 328) + v4[8:240])

    def test_receipt_and_kick_state_abi(self):
        with tempfile.TemporaryDirectory(prefix='xproc-render-abi-') as tmp:
            src = Path(tmp) / 'abi.c'
            src.write_text(ABI)
            for target in ('x86_64-pc-windows-msvc', 'aarch64-pc-windows-msvc'):
                for unit in (src, RENDER / 'src/render_job_window.c'):
                    done = subprocess.run(
                        ['clang', '-target', target, '-std=c11', '-Wall', '-Wextra', '-Werror',
                         '-Wconversion', '-Wsign-compare', '-fsyntax-only',
                         '-I', str(RENDER / 'include'), '-I', str(SHARED / 'include'), str(unit)],
                        capture_output=True, text=True)
                    self.assertEqual(done.returncode, 0, done.stderr)


PLATFORM = RENDER / 'src/backend_platform_windows.c'
LEAD_NAMES = ('XprocLead', 'PrivateLead', 'Ov2')
K1_NAMES = ('CrossProcessRenderPairs', 'XprocRenderKick', 'CAP_XPROC_RENDER',
            'ManagerOtherSlot', 'OtherManagerSlot', 'otherReason')


def c_function(text, name):
    start = [m.start() for m in re.finditer(
        r'(?m)^static [A-Za-z_ ]+\b' + name + r'\(', text)][-1]
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def no_comments(text):
    return re.sub(r'/\*.*?\*/', '', text, flags=re.S)


class CrossProcessRenderWiring(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = PLATFORM.read_text()
        cls.code = no_comments(cls.text)
        cls.kick = no_comments(c_function(cls.text, 'AdmissionJobWindowKickWindows'))

    def test_switch_has_one_reader_one_bit_and_its_prerequisites(self):
        code = self.code
        self.assertEqual(code.count('L"Wom1CrossProcessRenderPairs"'), 1)
        self.assertEqual(code.count('L"Wom1CrossProcessPairs"'), 1)
        read = ('  runtime->CrossProcessRenderPairs = runtime->CrossProcessPairs &&\n'
                '      runtime->ManagerSlots &&\n'
                '      AdmissionPlatformSwitchEnabled(Context, L"Wom1CrossProcessRenderPairs")'
                ' ? TRUE : FALSE;\n'
                '  if (runtime->CrossProcessRenderPairs)\n'
                '    runtime->Depth2Capabilities |= ADMISSION_JOB_WINDOW_CAP_XPROC_RENDER;\n')
        self.assertEqual(code.count(read), 1)
        # one writer of the bit, one writer of the switch besides its reset
        self.assertEqual(code.count('ADMISSION_JOB_WINDOW_CAP_XPROC_RENDER'), 1)
        self.assertEqual(code.count('|= ADMISSION_JOB_WINDOW_CAP_CROSS_PROCESS'), 1)
        self.assertEqual(len(re.findall(r'runtime->CrossProcessRenderPairs\s*=[^=]', code)), 2)
        start = code[code.index('NTSTATUS AdmissionPlatformRuntimeStart('):]
        reset = start.index('runtime->CrossProcessRenderPairs = FALSE;')
        c1 = start.index('L"Wom1CrossProcessPairs"')
        k1 = start.index(read)
        self.assertLess(reset, c1)
        self.assertLess(c1, k1)
        # in the same build configuration as C1; the G3 gate and the owner
        # switch follow the decision on every start, whatever was compiled
        self.assertNotIn('#', start[c1:k1 + len(read)])
        after = start[k1:]
        g3 = '  Context->G3CrossProcessRenderPairs = runtime->CrossProcessRenderPairs ? 1u : 0u;\n'
        owner = ('  runtime->Initdata.RenderSharedMemory.ManagerOtherSlot =\n'
                 '      runtime->CrossProcessRenderPairs ? APPLE_AGX_TRUE : APPLE_AGX_FALSE;\n')
        self.assertEqual(code.count(g3), 1)
        self.assertEqual(code.count(owner), 1)
        self.assertEqual(len(re.findall(r'\bManagerOtherSlot\b', code)), 1)
        self.assertLess(after.index('#endif'), after.index(g3))
        self.assertLess(after.index(g3), after.index(owner))
        self.assertLess(after.index(owner), after.index('AppleAgxBackendRuntimeStart('))

    def test_lead_switches_and_the_class_switch_never_share_a_statement(self):
        mixed = [s for s in self.code.split(';')
                 if any(n in s for n in K1_NAMES) and any(n in s for n in LEAD_NAMES)]
        self.assertEqual(mixed, [])
        gpuva = no_comments((RENDER / 'src/gpuva_g3_windows.c').read_text())
        self.assertEqual(gpuva.count('G3CrossProcessRenderPairs'), 1)
        self.assertIn('else if (cross && (process->Graph.SlotTable == NULL ||\n'
                      '                     (!(adapter->G3CrossProcessRenderPairs != 0u &&\n'
                      '                        adapter->BackendImage.G4Native &&\n'
                      '                        private_scene != NULL) &&\n'
                      '                      !adapter->BackendImage.G4Compute)))\n'
                      '    refusal = 10u;', gpuva)

    def test_second_slot_proof_is_a_read_before_the_decision_and_again_after_begin(self):
        kick = self.kick
        self.assertIn('ULONG otherReason = APPLE_AGX_RENDER_MANAGER_OTHER_UNPROVEN;', kick)
        proof = ('  if (runtime->CrossProcessRenderPairs && pendingManagerValid && head != NULL &&\n'
                 '      head->Fence == pendingFence && head->Context == pendingContext &&\n'
                 '      owner->ManagerFence == fence)\n'
                 '    otherReason = (ULONG)AppleAgxRenderManagerOtherSlotRefusal(owner,\n'
                 '        pendingManager, &pendingKey);\n'
                 '  s.OtherManagerSlot = otherReason == APPLE_AGX_RENDER_MANAGER_OTHER_OK ? 1u : 0u;\n')
        self.assertEqual(kick.count(proof), 1)
        self.assertEqual(len(re.findall(r'otherReason\s*=[^=]', kick)), 2)
        self.assertEqual(len(re.findall(r's\.OtherManagerSlot\s*=[^=]', kick)), 1)
        decision = kick.index('refusal = AdmissionJobWindowKickRefusal(&s);')
        self.assertLess(kick.index(proof), decision)
        # 61 is waived only for a pair of the class whose second slot is proven
        idle = kick[kick.index('s.NextNeedsIdle'):decision]
        self.assertIn('!(s.OtherManagerSlot != 0u &&\n'
                      '         AdmissionJobWindowXprocRenderClass(s.Capabilities, &s.Running,\n'
                      '                                            &s.Next) != 0u) &&\n'
                      '       AdmissionJobWindowManagerNeedsIdle(owner,', idle.replace('\n       \n', '\n'))
        # the kick marks the class only on an allowed, settled decision
        note = kick.index('runtime->XprocRenderKick = refusal == 0u ? TRUE : FALSE;')
        self.assertLess(decision, kick.index('runtime->XprocRenderKick = FALSE;'))
        self.assertLess(kick.index('runtime->XprocRenderKick = FALSE;'), note)
        begin = kick.index('AdmissionGpuvaG3BeginSuccessorJob(adapter, context, next,')
        again = kick.index('(runtime->XprocRenderKick\n'
                           '           ? AppleAgxRenderManagerOtherSlotRefusal(owner, image->G4Manager,\n'
                           '                 &image->G4ManagerKey) != APPLE_AGX_RENDER_MANAGER_OTHER_OK\n'
                           '           : AdmissionJobWindowManagerNeedsIdle(owner, TRUE, FALSE, NULL, NULL,')
        submit = kick.index('AppleAgxBackendRuntimeSubmitSuccessor(&runtime->Backend, &submission)')
        self.assertLess(note, begin)
        self.assertLess(begin, again)
        self.assertLess(again, submit)
        # failing it ends the G3 job before anything is built
        self.assertLess(again, kick.index('AdmissionGpuvaG3AbortJob(adapter, next);', again))
        self.assertLess(kick.index('AdmissionGpuvaG3AbortJob(adapter, next);', again), submit)
        # nothing in the kick resolves a manager: no drain, bind, restore
        for name in ('AppleAgxRenderManagerDrain', 'AdmissionPrepareG4Manager(',
                     'AdmissionEvictG4Manager', 'AppleAgxRenderManagerSave('):
            self.assertNotIn(name, kick)
        self.assertEqual(self.code.count('AppleAgxRenderManagerOtherSlotRefusal('), 2)

    def test_every_undo_of_an_unpublished_build_names_the_second_manager(self):
        kick, code = self.kick, self.code
        self.assertIn('(owner->ManagerFence != next && owner->ManagerSecondFence != next &&\n'
                      '          owner->ManagerOtherFence != next) ||\n'
                      '         AppleAgxRenderManagerCancelBuild(owner,image->G4Manager,next,', kick)
        undone = no_comments(c_function(self.text, 'AdmissionJobWindowKickBuildUndoneWindows'))
        self.assertIn('owner->ManagerFence == fence || owner->ManagerSecondFence == fence ||\n'
                      '      owner->ManagerOtherFence == fence ||', undone)
        build = no_comments(c_function(self.text, 'AdmissionExternalBuildJob'))
        failure = build[build.index('BuildFailure:'):]
        self.assertIn('ManagerSecondFence==Submission->Submission.Fence ||\n'
                      '       runtime->Initdata.RenderSharedMemory.ManagerOtherFence=='
                      'Submission->Submission.Fence))\n'
                      '    (void)AppleAgxRenderManagerCancelBuild(', failure)
        # the receipt of the class: one exit per outcome
        helper = no_comments(c_function(self.text, 'AdmissionXprocUndoneWindows'))
        self.assertIn('if (runtime->XprocRenderKick) {\n'
                      '    runtime->XprocRenderKick = FALSE;\n'
                      '    AdmissionJobWindowXprocRenderUndone(&runtime->XprocReceipt,', helper)
        self.assertEqual(kick.count('AdmissionXprocUndoneWindows(runtime, cross, fallbacksBefore, next,'), 6)
        self.assertEqual(code.count('AdmissionJobWindowXprocRenderUndone('), 1)
        self.assertEqual(code.count('AdmissionJobWindowXprocRenderKicked('), 1)
        self.assertEqual(code.count('AdmissionJobWindowXprocRenderNote('), 1)
        kicked = kick.index('AdmissionJobWindowXprocRenderKicked(')
        self.assertLess(kick.index('AppleAgxBackendRuntimeSubmitSuccessor('), kicked)
        self.assertIn('owner->ManagerOtherFence == next ? (unsigned int)owner->ManagerOtherIndex : 0u',
                      kick[kicked:])


if __name__ == '__main__':
    unittest.main()
