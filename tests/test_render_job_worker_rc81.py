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
  /* the decoder's '<IIIIQQ' header and '<IIIIQQ12Q4QIIIIII' v2 slot
   * (rc83 v3 appends the 8Q Sub stamps, rc95 v4 five more) */
  assert(ADMISSION_JOB_TIMING_VERSION == 4u);
  assert(sizeof(ADMISSION_JOB_TIMING_SLOT) == 288u);
  assert(offsetof(ADMISSION_JOB_TIMING_SLOT, Sub) == 184u);
  assert(offsetof(ADMISSION_JOB_TIMING_SLOT, CpuBuild) == 168u);
  assert(offsetof(ADMISSION_JOB_TIMING_SLOT, Flags) == 176u);
  assert(offsetof(ADMISSION_JOB_TIMING_STATE, Slot) == 32u);
  assert(offsetof(ADMISSION_JOB_TIMING_STATE, WorkerFlags) == 12u);
  AdmissionJobTimingInitialize(&s, 24000000ull);
  assert(s.Version == 4u && s.Bytes == sizeof(s) && s.WorkerFlags == 0u);
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


THREAD_SHIM = r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#define _Use_decl_annotations_
#define _In_
#define _In_opt_
#define TRUE 1
#define FALSE 0
#define IO_NO_INCREMENT 0
#define DelayedWorkQueue 1
#define Executive 0
#define KernelMode 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER 1
#define AppleAgxBackendRuntimeReady 2
#define ADMISSION_JOB_THREAD_PRIORITY_EXPECTED 12
#define VOID void
typedef int BOOLEAN; typedef int LONG; typedef int KIRQL; typedef int NTSTATUS;
typedef unsigned long long KAFFINITY; typedef void *PVOID;
typedef struct { int Signaled; } KEVENT;
typedef struct { KAFFINITY Mask; unsigned short Group; } GROUP_AFFINITY;
typedef struct { int Unused; } DEVICE_OBJECT, *PDEVICE_OBJECT;
typedef struct _ADMISSION_CONTEXT {
  int SchedulerLock; void *PlatformRuntime; PDEVICE_OBJECT PhysicalDeviceObject;
} ADMISSION_CONTEXT;
typedef struct {
  ADMISSION_CONTEXT *Adapter; struct { int Phase; } Backend; void *WorkItem;
  KEVENT WorkIdle; volatile LONG WorkScheduled, Stopping, Resetting;
  PVOID JobThreadObject; KEVENT JobThreadWake, JobThreadExited;
  volatile LONG JobThreadRequest, JobThreadStop; GROUP_AFFINITY JobThreadAffinity;
} ADMISSION_PLATFORM_RUNTIME;
static int locks, work_items, workers, pinned_calls, reverted, terminated, priority, waits;
static int pinned_now, stop_on_block, nested_submits;
static ADMISSION_PLATFORM_RUNTIME *rt;
static void KeAcquireSpinLock(int *l, KIRQL *i) { (void)l; *i = 2; assert(!locks); ++locks; }
static void KeReleaseSpinLock(int *l, KIRQL i) { (void)l; (void)i; assert(locks == 1); --locks; }
static LONG InterlockedCompareExchange(volatile LONG *p, LONG v, LONG e) { LONG o = *p; if (o == e) *p = v; return o; }
static LONG InterlockedExchange(volatile LONG *p, LONG v) { LONG o = *p; *p = v; return o; }
static void KeClearEvent(KEVENT *e) { e->Signaled = 0; }
static LONG KeSetEvent(KEVENT *e, int n, BOOLEAN w) {
  (void)n; (void)w; assert(!locks);
  /* no lost wakeup: a wake of the job thread is preceded by its request or stop */
  if (e == &rt->JobThreadWake) assert(rt->JobThreadRequest || rt->JobThreadStop);
  if (e == &rt->JobThreadExited) assert(!pinned_now && !terminated);
  e->Signaled = 1; return 0;
}
static NTSTATUS KeWaitForSingleObject(void *o, int r, int m, BOOLEAN a, void *t) {
  KEVENT *e = (KEVENT *)o; (void)r; (void)m; (void)a; (void)t; ++waits;
  assert(e == &rt->JobThreadWake && !locks);
  if (!e->Signaled) {
    /* the thread would block: the model's Destroy stops it now */
    assert(stop_on_block);
    rt->JobThreadStop = 1; e->Signaled = 1;
  }
  e->Signaled = 0; /* SynchronizationEvent */
  return STATUS_SUCCESS;
}
static void IoQueueWorkItem(void *w, void *f, int q, void *c) { (void)w; (void)f; assert(q == DelayedWorkQueue && c == rt->Adapter); ++work_items; }
static void *KeGetCurrentThread(void) { return &priority; }
static LONG KeSetPriorityThread(void *t, LONG p) { assert(t == &priority); priority = p; return 8; }
static void KeSetSystemGroupAffinityThread(GROUP_AFFINITY *a, GROUP_AFFINITY *p) {
  assert(a == &rt->JobThreadAffinity && a->Mask == 0xf0u && p); ++pinned_calls; pinned_now = 1; p->Mask = 0xffu; p->Group = 0; }
static void KeRevertToUserGroupAffinityThread(GROUP_AFFINITY *p) { assert(p->Mask == 0xffu && pinned_now); pinned_now = 0; ++reverted; }
static void PsTerminateSystemThread(NTSTATUS s) { assert(s == STATUS_SUCCESS); ++terminated; }
static void RtlZeroMemory(void *p, size_t n) { memset(p, 0, n); }
static BOOLEAN AdmissionPlatformRuntimeSubmit(ADMISSION_CONTEXT *Context);
/* The worker: its finished path clears WorkScheduled and dispatches the
 * queued successor (AdmissionPlatformWorkerFinished -> AdmissionDispatchQueuedWork). */
static void AdmissionPlatformWorker(PDEVICE_OBJECT d, PVOID c) {
  assert(d == rt->Adapter->PhysicalDeviceObject && c == rt->Adapter);
  assert(rt->WorkScheduled == 1 && !rt->JobThreadRequest);
  assert(pinned_now == (rt->JobThreadAffinity.Mask != 0) && priority == ADMISSION_JOB_THREAD_PRIORITY_EXPECTED);
  ++workers;
  rt->WorkScheduled = 0;
  if (nested_submits) { --nested_submits; assert(AdmissionPlatformRuntimeSubmit(rt->Adapter)); }
}
'''

THREAD_CASES = r'''
static void reset(ADMISSION_CONTEXT *a, ADMISSION_PLATFORM_RUNTIME *r, int thread, KAFFINITY mask) {
  static int object;
  memset(a, 0, sizeof(*a)); memset(r, 0, sizeof(*r)); rt = r;
  static DEVICE_OBJECT device; a->PhysicalDeviceObject = &device;
  a->PlatformRuntime = r; r->Adapter = a; r->WorkItem = r; r->WorkIdle.Signaled = 1;
  r->Backend.Phase = AppleAgxBackendRuntimeReady; r->JobThreadObject = thread ? &object : NULL;
  r->JobThreadAffinity.Mask = mask;
  work_items = workers = pinned_calls = reverted = terminated = priority = waits = 0;
  pinned_now = stop_on_block = nested_submits = 0;
}
int main(void) {
  ADMISSION_CONTEXT a; ADMISSION_PLATFORM_RUNTIME r;
  /* no dedicated thread: the work item stays the worker */
  reset(&a, &r, 0, 0);
  assert(AdmissionPlatformRuntimeSubmit(&a) && work_items == 1 && !r.JobThreadWake.Signaled);
  assert(r.WorkScheduled == 1 && !r.WorkIdle.Signaled);
  assert(!AdmissionPlatformRuntimeSubmit(&a) && work_items == 1); /* one at a time */
  /* dedicated thread: no work item; request then wake */
  reset(&a, &r, 1, 0xf0u);
  assert(AdmissionPlatformRuntimeSubmit(&a) && !work_items);
  assert(r.JobThreadRequest == 1 && r.JobThreadWake.Signaled && r.WorkScheduled == 1);
  assert(!AdmissionPlatformRuntimeSubmit(&a));
  /* the successor dispatched from inside the worker runs next on the same
   * thread; three jobs, then the thread blocks and is stopped */
  nested_submits = 2; stop_on_block = 1;
  AdmissionJobThread(&r);
  assert(workers == 3 && !work_items && terminated == 1 && r.JobThreadExited.Signaled);
  assert(pinned_calls == 1 && reverted == 1 && !pinned_now);
  assert(!r.JobThreadRequest && waits == 4);
  /* a stop raced with a pending request: the request still runs once */
  reset(&a, &r, 1, 0);
  assert(AdmissionPlatformRuntimeSubmit(&a));
  r.JobThreadStop = 1;
  AdmissionJobThread(&r);
  assert(workers == 1 && terminated == 1 && !pinned_calls && !reverted && waits == 1);
  /* refused while stopping or resetting, or not ready */
  reset(&a, &r, 1, 0); r.Stopping = 1; assert(!AdmissionPlatformRuntimeSubmit(&a));
  reset(&a, &r, 1, 0); r.Resetting = 1; assert(!AdmissionPlatformRuntimeSubmit(&a));
  reset(&a, &r, 1, 0); r.WorkItem = NULL; assert(!AdmissionPlatformRuntimeSubmit(&a));
  assert(!r.JobThreadWake.Signaled && !r.JobThreadRequest);
  return 0;
}
'''


class JobWorkerThread(unittest.TestCase):
    def test_thread_runs_each_request_once_without_work_item_hop(self):
        source = BACKEND.read_text()
        priority = re.search(r"#define ADMISSION_JOB_THREAD_PRIORITY (\d+)", source)
        self.assertIsNotNone(priority, "missing ADMISSION_JOB_THREAD_PRIORITY")
        # DelayedWorkQueue threads, which ran the worker before, run at 12.
        self.assertEqual(priority.group(1), "12")
        functions = "\n".join(function_body(source, name) for name in
                              ("AdmissionPlatformRuntimeSubmit", "AdmissionJobThread"))
        functions = functions.replace("ADMISSION_JOB_THREAD_PRIORITY",
                                      "ADMISSION_JOB_THREAD_PRIORITY_EXPECTED")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "thread.c"
            exe = Path(directory) / "thread"
            path.write_text(THREAD_SHIM + functions + THREAD_CASES)
            built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                                    "-Werror", "-fsanitize=address,undefined", str(path), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_destroy_stops_the_idle_thread_and_waits_for_the_thread_object(self):
        destroy = function_body(BACKEND.read_text(), "AdmissionPlatformDestroy")
        idle = destroy.index("KeWaitForSingleObject(&Runtime->WorkIdle")
        stop = destroy.index("InterlockedExchange(&Runtime->JobThreadStop, 1)")
        self.assertLess(idle, stop)
        tail = destroy[stop:]
        self.assertRegex(tail, r"KeSetEvent\(&Runtime->JobThreadWake")
        self.assertRegex(tail, r"KeWaitForSingleObject\(Runtime->JobThreadObject")
        self.assertIn("ObDereferenceObject(Runtime->JobThreadObject)", tail)
        # the busy check guards the stop (a retried Destroy must not strand a job)
        block = destroy[destroy.rindex("if (Runtime->JobThreadObject != NULL)", 0, stop):stop]
        self.assertIn("WorkScheduled", block)
        self.assertIn("STATUS_DEVICE_BUSY", block)

    def test_start_pins_only_from_the_topology_and_falls_back_to_the_work_item(self):
        start = function_body(BACKEND.read_text(), "AdmissionJobThreadStart")
        self.assertIn('L"Wom1JobThreadOff"', start)
        self.assertIn('L"Wom1JobThreadAnyCore"', start)
        self.assertIn("AdmissionJobWorkerAffinity(", start)
        self.assertIn("PsCreateSystemThread(", start)
        self.assertIn("ObReferenceObjectByHandle(", start)
        # a thread that cannot be referenced is stopped and waited for, never leaked
        failed = start[start.index("ObReferenceObjectByHandle("):]
        self.assertIn("KeWaitForSingleObject(&runtime->JobThreadExited", failed)
        self.assertIn("ZwClose(", failed)


class JobWorkerHelpers(unittest.TestCase):
    def test_affinity_selects_the_highest_efficiency_class(self):
        build_and_run(self, AFFINITY)

    def test_timing_v2_records_processor_and_begin_flags(self):
        build_and_run(self, TIMING)


if __name__ == "__main__":
    unittest.main()
