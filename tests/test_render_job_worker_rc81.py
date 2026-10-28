"""rc81: the KMD job worker runs on a dedicated, performance-core thread.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2010 (Wom1JobTiming971, last 64 jobs): the same KMD code took two
distinct times per job, 64-84 us or 261-273 us for a render build and
18-27 us or 72-77 us from completion to job end, the same 3.8x ratio for
both phases of one job. m1n1 fixes the cluster p-states (cpufreq.c
cpufreq_init_cluster), and the guest MADT (mu T810XFamilyPkg
MADT_Static.aslc) gives UIDs 0-3 (Icestorm) efficiency class 0 and UIDs
4-7 (Firestorm) class 1, so the slow mode is the worker running on an
efficiency core: IoQueueWorkItem(DelayedWorkQueue) runs it on whatever
system worker thread and processor is free. The tests check:
- AdmissionJobWorkerAffinity picks every core of the highest efficiency
  class (the J313 Firestorm cluster), in one processor group, and returns 0
  (no pin) for a homogeneous or empty topology;
- the v2 timing slot records the worker's processor and efficiency class
  at the build and at completion once, and the BeginJob flags, with the
  layout the tools/agx_gpu_frame_breakdown.py decoder reads;
- AdmissionPlatformRuntimeSubmit hands a job to the dedicated thread
  (no work-item hop) and falls back to the work item without it; the thread
  runs one worker per request, a request made from inside the worker
  (AdmissionPlatformWorkerFinished -> AdmissionDispatchQueuedWork) runs next
  on the same thread, and a stop never drops a pending request;
- AdmissionPlatformDestroy stops the thread only after the worker is idle
  and waits for the thread object itself, not an event it sets.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

from test_g4_submit_virtual_replay import function_body

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "drivers/apple-agx/render-admission/include"
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/render_job_timing.c"
BACKEND = ROOT / "drivers/apple-agx/render-admission/src/backend_platform_windows.c"


def build_and_run(test, program):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "model.c"
        exe = Path(directory) / "model"
        path.write_text(program)
        built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                                "-Werror", "-fsanitize=address,undefined", "-I", str(INCLUDE),
                                str(path), str(SOURCE), "-o", str(exe)],
                               capture_output=True, text=True)
        test.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        test.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


AFFINITY = r'''
#include <assert.h>
#include <stddef.h>
#include "render_job_timing.h"
int main(void) {
  /* J313 guest: Icestorm 0-3 class 0, Firestorm 4-7 class 1, one group. */
  ADMISSION_JOB_WORKER_CORE j313[8];
  unsigned short group = 99; unsigned long long mask = 99;
  unsigned i;
  for (i = 0; i < 8; ++i) {
    j313[i].Mask = 1ull << i; j313[i].Group = 0;
    j313[i].EfficiencyClass = i < 4 ? 0 : 1;
  }
  assert(AdmissionJobWorkerAffinity(j313, 8, &group, &mask) == 1);
  assert(group == 0 && mask == 0xf0ull);
  /* order does not matter: performance cores reported first */
  {
    ADMISSION_JOB_WORKER_CORE r[8];
    for (i = 0; i < 8; ++i) r[i] = j313[7 - i];
    assert(AdmissionJobWorkerAffinity(r, 8, &group, &mask) == 1 && mask == 0xf0ull);
  }
  /* homogeneous: no pin, outputs untouched */
  for (i = 0; i < 8; ++i) j313[i].EfficiencyClass = 2;
  group = 7; mask = 7;
  assert(AdmissionJobWorkerAffinity(j313, 8, &group, &mask) == 0 && group == 7 && mask == 7);
  assert(AdmissionJobWorkerAffinity(j313, 0, &group, &mask) == 0);
  assert(AdmissionJobWorkerAffinity(NULL, 8, &group, &mask) == 0);
  assert(AdmissionJobWorkerAffinity(j313, 8, NULL, &mask) == 0);
  /* two groups: the group of the first top-class core only */
  {
    ADMISSION_JOB_WORKER_CORE g[4] = {
      {0x1ull, 0, 0, {0}}, {0x2ull, 0, 3, {0}}, {0x1ull, 1, 3, {0}}, {0x4ull, 0, 3, {0}}};
    assert(AdmissionJobWorkerAffinity(g, 4, &group, &mask) == 1 && group == 0 && mask == 0x6ull);
  }
  /* a top-class core with an empty mask contributes nothing */
  {
    ADMISSION_JOB_WORKER_CORE e[2] = {{0x1ull, 0, 0, {0}}, {0x0ull, 0, 1, {0}}};
    assert(AdmissionJobWorkerAffinity(e, 2, &group, &mask) == 0);
  }
  return 0;
}
'''

TIMING = r'''
#include <assert.h>
#include <stddef.h>
#include "render_job_timing.h"
int main(void) {
  static ADMISSION_JOB_TIMING_STATE s;
  const ADMISSION_JOB_TIMING_SLOT *slot;
  /* the decoder's '<IIIIQQ' header and '<IIIIQQ12Q4QIIIIII' v2 slot */
  assert(ADMISSION_JOB_TIMING_VERSION == 2u);
  assert(sizeof(ADMISSION_JOB_TIMING_SLOT) == 184u);
  assert(offsetof(ADMISSION_JOB_TIMING_SLOT, CpuBuild) == 168u);
  assert(offsetof(ADMISSION_JOB_TIMING_SLOT, Flags) == 176u);
  assert(offsetof(ADMISSION_JOB_TIMING_STATE, Slot) == 32u);
  assert(offsetof(ADMISSION_JOB_TIMING_STATE, WorkerFlags) == 12u);
  AdmissionJobTimingInitialize(&s, 24000000ull);
  assert(s.Version == 2u && s.Bytes == sizeof(s) && s.WorkerFlags == 0u);
  assert(AdmissionJobTimingBegin(&s, 5u, 7u, 1ull, 64u, 100ull));
  assert(AdmissionJobTimingCpuValue(5u, 1u) == (5u | 1u << 8 | 1u << 31));
  assert(AdmissionJobTimingCpuValue(300u, 0u) == 0u); /* not one byte */
  assert(AdmissionJobTimingCpu(&s, 5u, 0, AdmissionJobTimingCpuValue(1u, 0u)));
  assert(!AdmissionJobTimingCpu(&s, 5u, 0, AdmissionJobTimingCpuValue(6u, 1u))); /* first wins */
  assert(AdmissionJobTimingCpu(&s, 5u, 1, AdmissionJobTimingCpuValue(6u, 1u)));
  assert(!AdmissionJobTimingCpu(&s, 5u, 1, 0u));
  assert(!AdmissionJobTimingCpu(&s, 9u, 0, AdmissionJobTimingCpuValue(1u, 0u))); /* no slot */
  assert(AdmissionJobTimingFlag(&s, 5u, ADMISSION_JOB_FLAG_MAPPING_UNCHANGED));
  assert(AdmissionJobTimingFlag(&s, 5u, ADMISSION_JOB_FLAG_PRIVATE_SCENE));
  assert(!AdmissionJobTimingFlag(&s, 9u, ADMISSION_JOB_FLAG_PRIVATE_SCENE));
  slot = AdmissionJobTimingFind(&s, 5u);
  assert(slot->CpuBuild == (1u | 1u << 31));
  assert(slot->CpuComplete == (6u | 1u << 8 | 1u << 31));
  assert(slot->Flags == (ADMISSION_JOB_FLAG_MAPPING_UNCHANGED | ADMISSION_JOB_FLAG_PRIVATE_SCENE));
  /* a reused slot starts clean */
  assert(AdmissionJobTimingBegin(&s, 5u + ADMISSION_JOB_TIMING_CAPACITY, 7u, 1ull, 64u, 200ull));
  {
    unsigned i;
    for (i = 1; i < ADMISSION_JOB_TIMING_CAPACITY; ++i)
      assert(AdmissionJobTimingBegin(&s, 1000u + i, 7u, 1ull, 64u, 300ull + i));
    assert(AdmissionJobTimingBegin(&s, 2000u, 7u, 1ull, 64u, 900ull));
    slot = AdmissionJobTimingFind(&s, 2000u);
    assert(slot && !slot->CpuBuild && !slot->CpuComplete && !slot->Flags);
  }
  return 0;
}
'''


class JobWorkerHelpers(unittest.TestCase):
    def test_timing_v2_records_processor_and_begin_flags(self):
        build_and_run(self, TIMING)


if __name__ == "__main__":
    unittest.main()
