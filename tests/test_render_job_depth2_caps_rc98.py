"""rc98: Wom1JobDepth2Caps -- the depth-2 decision with the rc98 capabilities.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
rc95 refuses every C<->R pair of the GravityMark frame (CRCRCRCR, renders
with a v5 compute prelude) with 57 (both use the compute sidecar's one
job-word set) and every R->C pair of a render without a prelude with 60
(no compute-queue Barrier). rc98 builds the sidecar's set B and the
Barrier encoders and declares SECOND_SIDECAR | TA_ON_3D | COMPUTE_BARRIER
only under the new device value Wom1JobDepth2Caps = 1 (on top of
Wom1JobDepth2 and Wom1JobBuildAhead). The test catches:
  C1 the capabilities declared without both switches, or LIVE_MANAGER
     declared (U1 is not built: R->R pairs must still refuse 58/59);
  C2 a C<->R or R->C pair still refused 57/60 with the capabilities, or
     eligible without them (the rc95 receipts must stay comparable);
  C3 an eligible pair whose successor's job words or leading Barrier
     overwrite a byte of the running job's job-word set, built with the
     real builder, the real set choice and the real Barrier encoder; a
     naive policy (SECOND_SIDECAR declared, every build in set A) must
     break it;
  C4 every Barrier class an eligible pair needs has an encoder, on the
     right queue's firmware stamp;
  C5 the KMD wiring: the switch, the decision's Capabilities, the set
     choice of the envelope compute and v5 prelude builds (never a present
     job, never without a job-owned preemption buffer, set A without the
     capabilities), the prelude's TA Barrier in its compute's set, no
     second kick, and the timing flags/decoder for set B and caps.
"""
from pathlib import Path
import importlib.util
import os
import re
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
SHARED = ROOT / 'drivers/apple-agx/shared'
WORKER = RENDER / 'src/backend_platform_windows.c'
TOOL = ROOT / 'tools/agx_gpu_frame_breakdown.py'

SHARED_SOURCES = [
    'apple_agx_render_shared_memory.c', 'apple_agx_g13_compute_work.c',
    'apple_agx_render_template.generated.c', 'apple_agx_render_template_rebase.c',
    'apple_agx_exp208_adapter.c', 'apple_agx_exp208_gdi.c',
    'apple_agx_exp208_framebuffer.c', 'apple_agx_gdi.c', 'apple_agx_relocation.c',
    'apple_agx_memory.c', 'apple_agx_exp208_dynamic.c',
]

MODEL = r'''
#include "apple_agx_render_shared_memory.h"
#include "render_job_window.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define K_R ADMISSION_JOB_WINDOW_KIND_RENDER
#define K_RP ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE
#define K_C ADMISSION_JOB_WINDOW_KIND_COMPUTE
#define SIDE 39u
static unsigned char store[40][0x10000];
static APPLE_AGX_RENDER_SHARED_MEMORY_OWNER o;

static void setup(void) {
  unsigned i;
  memset(&o, 0, sizeof(o)); memset(store, 0xcd, sizeof(store));
  o.ObjectCount = 40u;
  for (i = 0; i < 40u; ++i) {
    o.Objects[i].CpuAddress = store[i]; o.Objects[i].Length = 0x10000u;
    o.Objects[i].State = AppleAgxMemoryGpuMapped;
    o.VirtualAddresses[i] = 0x1500000000ULL + (unsigned long long)i * 0x10000ULL;
    o.Objects[i].GpuVirtualAddress = o.VirtualAddresses[i];
  }
  memset(store[11], 0, 0x10000);
}

static unsigned refusal(unsigned caps, unsigned nk, unsigned mk, unsigned long long ns,
                        unsigned long long ms) {
  ADMISSION_JOB_WINDOW_KICK_STATE s;
  memset(&s, 0, sizeof(s));
  s.Enabled = 1u; s.InFlight = 1u; s.NextPrebound = 1u; s.Capabilities = caps;
  s.Running.Fence = 100u; s.Running.Kind = nk; s.Running.Process = 7u; s.Running.SceneId = ns;
  s.Next.Fence = 101u; s.Next.Kind = mk; s.Next.Process = 7u; s.Next.SceneId = ms;
  s.QueuedFence = 101u;
  return AdmissionJobWindowKickRefusal(&s);
}

/* C2: (running, next, refusal without caps, refusal with the rc98 caps) */
static const struct { unsigned n, m, before, after; unsigned long long ns, ms; } pairs[] = {
  {K_C, K_RP, 57u, 0u, 0, 1}, {K_RP, K_C, 57u, 0u, 1, 0}, {K_C, K_C, 57u, 0u, 0, 0},
  {K_C, K_R, 0u, 0u, 0, 1},   {K_R, K_C, 60u, 0u, 1, 0},
  {K_R, K_R, 58u, 58u, 1, 2}, {K_R, K_R, 59u, 59u, 1, 1}, {K_RP, K_R, 59u, 59u, 1, 1},
  {K_RP, K_RP, 57u, 58u, 1, 2},
};

/* N's live job words: its set (when it uses the sidecar). */
static int live(unsigned set, unsigned offset) {
  unsigned base = set ? APPLE_AGX_COMPUTE_JOB_WORD_SET_B_DELTA : 0u;
  offset -= base;
  return (offset >= APPLE_AGX_COMPUTE_WORK_OFFSET && offset < APPLE_AGX_COMPUTE_STATISTICS_OFFSET) ||
         (offset >= APPLE_AGX_COMPUTE_TA_BARRIER_OFFSET &&
          offset < APPLE_AGX_COMPUTE_QUEUE_BARRIER_OFFSET + APPLE_AGX_G13_BARRIER_BYTES) ||
         (offset >= APPLE_AGX_COMPUTE_TIMESTAMP_MICROSEQUENCE_OFFSET &&
          offset < APPLE_AGX_COMPUTE_TIMESTAMP_OFFSET + APPLE_AGX_G13_TIMESTAMP_BLOCK_BYTES);
}

/* A job's own sidecar build at depth 1 (what the KMD does at kick time). */
static unsigned build(unsigned kind, unsigned stamp, unsigned caps, int naive) {
  APPLE_AGX_RENDER_COMPUTE_INPUT in;
  APPLE_AGX_RENDER_COMPUTE_OUTPUT out;
  APPLE_AGX_RENDER_BARRIER_INPUT b;
  APPLE_AGX_U64 address;
  if (!AdmissionJobWindowUsesSidecar(kind)) return 9u;
  memset(&in, 0, sizeof(in));
  in.CdmStreamBase = 0x1500400000ULL; in.CdmStreamBytes = 72u; in.Counter = stamp;
  in.UscExecutionBase = 0x1100000000ULL; in.VmSlot = 2u; in.EventNumber = 2u;
  in.StampValue = in.EventSequence = stamp; in.PreemptionGpuAddress = 0x1500600000ULL;
  in.Timestamps = 1u;
  in.JobWordSet = naive ? 0u : AppleAgxRenderSharedMemoryNextJobWordSet(&o, stamp,
      AdmissionJobWindowSecondSet(kind, caps) ? APPLE_AGX_TRUE : APPLE_AGX_FALSE);
  assert(AppleAgxRenderSharedMemoryBuildCompute(&o, &in, &out));
  if (kind == K_RP) { /* D3D-G1b: the prelude's TA Barrier, in its set */
    memset(&b, 0, sizeof(b));
    b.WaitStampGpuAddress = out.FirmwareStampGpuAddress; b.WaitValue = stamp;
    b.WaitSlot = 2u; b.StampSelf = 0x7a000000u | stamp; b.JobWordSet = out.JobWordSet;
    assert(AppleAgxRenderSharedMemoryBuildComputeBarrier(&o, &b, &address));
  }
  return out.JobWordSet;
}

/* C4: the successor's leading Barrier on N's last firmware stamp. */
static void leading(unsigned nk, unsigned mk, unsigned stamp, int naive) {
  APPLE_AGX_RENDER_BARRIER_INPUT b;
  APPLE_AGX_U64 address = 0u;
  unsigned barrier = AdmissionJobWindowBarrier(nk, mk), queue, on;
  memset(&b, 0, sizeof(b));
  switch (barrier) {
    case ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE:
      queue = APPLE_AGX_RENDER_BARRIER_QUEUE_TA; on = APPLE_AGX_RENDER_STAMP_QUEUE_COMPUTE; break;
    case ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D:
      queue = APPLE_AGX_RENDER_BARRIER_QUEUE_TA; on = APPLE_AGX_RENDER_STAMP_QUEUE_3D; break;
    case ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D:
      queue = APPLE_AGX_RENDER_BARRIER_QUEUE_COMPUTE; on = APPLE_AGX_RENDER_STAMP_QUEUE_3D; break;
    case ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE:
      queue = APPLE_AGX_RENDER_BARRIER_QUEUE_COMPUTE; on = APPLE_AGX_RENDER_STAMP_QUEUE_COMPUTE; break;
    default: printf("RED C4 no Barrier for %u->%u\n", nk, mk); exit(1);
  }
  b.WaitStampGpuAddress = AppleAgxRenderSharedMemoryFirmwareStampAddress(&o, on);
  b.WaitValue = stamp - 1u; b.WaitSlot = on == APPLE_AGX_RENDER_STAMP_QUEUE_3D ? 1u : 2u;
  b.StampSelf = stamp;
  b.JobWordSet = naive ? 0u : AppleAgxRenderSharedMemoryNextJobWordSet(&o, stamp, APPLE_AGX_TRUE);
  if (!AppleAgxRenderSharedMemoryBuildLeadingBarrier(&o, &b, queue, &address)) {
    printf("RED C4 Barrier %u not built\n", barrier); exit(1);
  }
  assert(address >= o.VirtualAddresses[SIDE] && address < o.VirtualAddresses[SIDE] + 0xc000u);
}

/* C3: kick N (depth 1 build), then while N runs build N+1's words and its
 * leading Barrier; no byte of N's live set may change. */
static int frame(const unsigned *kinds, unsigned count, unsigned caps, int naive) {
  static unsigned char before[0x10000];
  unsigned i, stamp = 0x200u, nset = 9u, bad = 0u;
  setup();
  for (i = 0; i + 1u < count; ++i, ++stamp) {
    unsigned n = kinds[i], m = kinds[i + 1], off;
    if (i == 0u) nset = build(n, stamp, caps, naive);
    if (refusal(caps, n, m, n == K_C ? 0 : 1, m == K_C ? 0 : 1) != 0u) {
      nset = build(m, stamp + 1u, caps, naive); /* N ended first: depth 1 */
      continue;
    }
    memcpy(before, store[SIDE], sizeof(before));
    {
      unsigned mset = build(m, stamp + 1u, caps, naive);
      leading(n, m, stamp + 1u, naive);
      if (AdmissionJobWindowUsesSidecar(n))
        for (off = 0x8000u; off < 0xc000u; ++off)
          if (live(nset, off) && before[off] != store[SIDE][off]) {
            if (!naive) printf("RED C3 %u->%u wrote N's set %u at %#x\n", n, m, nset, off);
            ++bad; break;
          }
      nset = mset;
    }
  }
  return (int)bad;
}

int main(void) {
  static const unsigned gm[] = {K_C, K_RP, K_C, K_RP, K_C, K_RP, K_C, K_RP, K_C};
  static const unsigned mixed[] = {K_C, K_R, K_C, K_C, K_RP, K_R, K_C, K_RP, K_RP, K_C};
  unsigned i;
  /* C1 */
  assert(AdmissionJobWindowCapabilities(0u, 0u) == 0u);
  assert(AdmissionJobWindowCapabilities(0u, 1u) == 0u);
  assert(AdmissionJobWindowCapabilities(1u, 0u) == 0u);
  assert(AdmissionJobWindowCapabilities(1u, 1u) == ADMISSION_JOB_WINDOW_CAPS_RC98);
  if (ADMISSION_JOB_WINDOW_CAPS_RC98 & ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER) { puts("RED C1 LIVE_MANAGER"); return 1; }
  assert(AdmissionJobWindowSecondSet(K_C, ADMISSION_JOB_WINDOW_CAPS_RC98) &&
         AdmissionJobWindowSecondSet(K_RP, ADMISSION_JOB_WINDOW_CAPS_RC98) &&
         !AdmissionJobWindowSecondSet(K_R, ADMISSION_JOB_WINDOW_CAPS_RC98) &&
         !AdmissionJobWindowSecondSet(ADMISSION_JOB_WINDOW_KIND_PRESENT, ADMISSION_JOB_WINDOW_CAPS_RC98) &&
         !AdmissionJobWindowSecondSet(K_C, 0u));
  /* C2 */
  for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
    unsigned before = refusal(0u, pairs[i].n, pairs[i].m, pairs[i].ns, pairs[i].ms);
    unsigned after = refusal(AdmissionJobWindowCapabilities(1u, 1u), pairs[i].n, pairs[i].m,
                             pairs[i].ns, pairs[i].ms);
    if (before != pairs[i].before || after != pairs[i].after) {
      printf("RED C2 pair %u->%u: %u/%u, expected %u/%u\n", pairs[i].n, pairs[i].m,
             before, after, pairs[i].before, pairs[i].after);
      return 1;
    }
  }
  /* C3/C4 with the real builder and set choice */
  if (frame(gm, 9u, ADMISSION_JOB_WINDOW_CAPS_RC98, 0) ||
      frame(mixed, 10u, ADMISSION_JOB_WINDOW_CAPS_RC98, 0)) return 1;
  /* without caps nothing is eligible across the sidecar (depth 1 builds only) */
  if (frame(gm, 9u, 0u, 0)) return 1;
  /* naive: SECOND_SIDECAR declared but every build in set A */
  if (!frame(gm, 9u, ADMISSION_JOB_WINDOW_CAPS_RC98, 1)) { puts("FAIL naive survived"); return 2; }
  puts("CAPS_OK");
  return 0;
}
'''


def load():
    spec = importlib.util.spec_from_file_location('agx_gpu_frame_breakdown', TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def section(text, start):
    body = text[text.index(start):]
    return body[:body.index('\n}\n')]


class Depth2CapsModel(unittest.TestCase):
    def test_pairs_sets_and_barriers(self):
        with tempfile.TemporaryDirectory(prefix='rc98-caps-') as tmp:
            path = Path(tmp) / 'caps.c'
            path.write_text(MODEL)
            exe = Path(tmp) / 'caps'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-fsanitize=address,undefined',
                            '-I', str(SHARED / 'include'), '-I', str(RENDER / 'include'),
                            str(path), str(RENDER / 'src/render_job_window.c')] +
                           [str(SHARED / 'src' / s) for s in SHARED_SOURCES] +
                           ['-o', str(exe)], check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn('CAPS_OK', ran.stdout)


class Depth2CapsWiring(unittest.TestCase):
    def test_switch_is_default_on_and_needs_depth2(self):
        text = WORKER.read_text()
        self.assertEqual(text.count('L"Wom1JobDepth2Caps"'), 1)
        self.assertRegex(text, r'runtime->Depth2Capabilities = AdmissionJobWindowCapabilities\(\s*'
                               r'runtime->JobDepth2 \? 1u : 0u,\s*runtime->JobDepth2 &&\s*'
                               r'!AdmissionPlatformSwitchDisabled\(Context, L"Wom1JobDepth2Caps"\)')
        # rc102: Wom1JobDepth2Kick (the wired window) is cleared with them
        self.assertRegex(text, r'runtime->JobDepth2 = FALSE;\s*\n\s*runtime->Depth2Capabilities = 0u;\s*\n'
                               r'\s*runtime->JobDepth2Kick = FALSE;\s*\n#endif')
        # the switch is read after (below) Wom1JobDepth2 in the build-ahead branch
        self.assertLess(text.index('L"Wom1JobDepth2")'), text.index('L"Wom1JobDepth2Caps"'))

    def test_decision_uses_the_capabilities_and_kicks_nothing(self):
        note = section(WORKER.read_text(), 'static VOID AdmissionJobWindowNoteWindows(')
        self.assertLess(note.index('s.Capabilities = runtime->Depth2Capabilities;'),
                        note.index('AdmissionJobWindowKickRefusal(&s)'))
        self.assertIn('ADMISSION_JOB_FLAG_DEPTH2_CAPS', note)
        for forbidden in ('AppleAgxBackendRuntimeSubmit', 'Doorbell', 'BuildLeadingBarrier',
                          'AdmissionG4PendingAheadPromoteLocked', 'AppleAgxSchedulerActivateFence'):
            self.assertNotIn(forbidden, note)
        # rc102 (step 3/4): only a successor build publishes a leading
        # Barrier (AdmissionBuildLeadBarrier, called under Plan->Successor)
        text = WORKER.read_text()
        self.assertEqual(text.count('AppleAgxRenderSharedMemoryBuildLeadingBarrier('), 1)
        self.assertIn('AppleAgxRenderSharedMemoryBuildLeadingBarrier(',
                      section(text, 'static APPLE_AGX_BACKEND_BOOL AdmissionBuildLeadBarrier('))
        for call in re.finditer(r'AdmissionBuildLeadBarrier\(runtime', text):
            self.assertIn('Plan->Successor', text[max(0, call.start() - 700):call.start()])

    def test_set_choice_of_the_compute_builds(self):
        text = WORKER.read_text()
        compute = section(text, 'static APPLE_AGX_BACKEND_BOOL AdmissionExternalBuildComputeJob(')
        self.assertRegex(compute, r'input\.JobWordSet = AppleAgxRenderSharedMemoryNextJobWordSet\(\s*'
                                  r'&runtime->Initdata\.RenderSharedMemory, fence,\s*'
                                  r'preemption != 0ULL &&\s*AdmissionJobWindowSecondSet\(image->PresentCompute \?\s*'
                                  r'ADMISSION_JOB_WINDOW_KIND_PRESENT : ADMISSION_JOB_WINDOW_KIND_COMPUTE,\s*'
                                  r'runtime->Depth2Capabilities\)')
        self.assertLess(compute.index('input.PreemptionGpuAddress = preemption;'),
                        compute.index('input.JobWordSet ='))
        self.assertIn('ADMISSION_JOB_FLAG_SIDECAR_SET_B', compute)
        build = section(text, 'static APPLE_AGX_BACKEND_BOOL AdmissionExternalBuildJob(')
        # the v5 prelude: guarded by its own preemption buffer
        self.assertRegex(build, r'input\.PreemptionGpuAddress = image->G4PreludePreemption\.Va;\s*/\*[^*]*\*/\s*'
                                r'input\.JobWordSet = AppleAgxRenderSharedMemoryNextJobWordSet\(\s*'
                                r'&runtime->Initdata\.RenderSharedMemory, Submission->Submission\.Fence,\s*'
                                r'input\.PreemptionGpuAddress != 0ULL &&\s*'
                                r'AdmissionJobWindowSecondSet\(ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE,')
        # the legacy mixed batch has no job-owned preemption buffer: set A
        mixed = build[build.index('APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH) {'):
                      build.index('D3D-G1: a v5 render')]
        self.assertNotIn('JobWordSet', mixed)
        self.assertEqual(build.count('input.JobWordSet ='), 1)
        barrier = section(text, 'static APPLE_AGX_BACKEND_BOOL AdmissionBuildTaComputeBarrier(')
        self.assertLess(barrier.index('barrier.JobWordSet = Output->JobWordSet;'),
                        barrier.index('AppleAgxRenderSharedMemoryBuildComputeBarrier('))

    def test_kmd_headers_stay_crt_free(self):
        for path in (RENDER / 'include/render_job_window.h', RENDER / 'src/render_job_window.c',
                     SHARED / 'include/apple_agx_render_shared_memory.h'):
            self.assertIsNone(re.search(r'#include\s*<', path.read_text()), path.name)


class Depth2CapsDecoder(unittest.TestCase):
    def test_set_b_and_caps_flags(self):
        b = load()
        self.assertEqual((b.FLAG_SIDECAR_SET_B, b.FLAG_DEPTH2_CAPS), (0x1000, 0x2000))
        header = (RENDER / 'include/render_job_timing.h').read_text()
        self.assertIn('#define ADMISSION_JOB_FLAG_SIDECAR_SET_B 0x1000u', header)
        self.assertIn('#define ADMISSION_JOB_FLAG_DEPTH2_CAPS 0x2000u', header)
        data = bytearray(struct.pack('<IIIIQQ', 4, 32 + 64 * 288, 4, 0, 1000000, 0))
        rows = [(0x2000 | 0x800, 0), (0x2000 | 0x20 | 0x1000 | 0x800, 0),
                (0x2000 | 0x800, 0), (0x2000 | 0x20, 59 << 24)]
        for i in range(64):
            flags, ahead = rows[i] if i < 4 else (0, 0)
            q = {p: 1000 * (i + 1) + 10 * n for n, p in enumerate(b.KMD_PHASES)} if i < 4 else {}
            data += struct.pack('<IIIIQQ12Q4QII', i + 1 if i < 4 else 0, 9452 if i < 4 else 0,
                                0, 0, 0, 0, *[q.get(p, 0) for p in b.KMD_PHASES], 0, 0, 0, 0, 0, 0)
            data += struct.pack('<IIII', 0, 0, flags, ahead)
            data += struct.pack('<13Q', *([0] * 13))
        d = b.kmd_job_timing(bytes(data))['depth2']
        self.assertEqual((d['with_caps'], d['sidecar_set_b'], d['sidecar_set_a']), (4, 1, 1))
        self.assertEqual((d['eligible'], d['refusals']), (3, {59: 1}))


if __name__ == '__main__':
    unittest.main()
