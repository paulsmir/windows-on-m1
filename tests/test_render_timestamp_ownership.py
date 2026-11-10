"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A render successor rebinds QueueObjects to its parity before the predecessor
completes. Actual Windows capture/publication must still read the predecessor's
own TA and 3D pairs. Compile the real builder and real Windows readers, with
only OS clock/lock calls replaced; no model of timestamp selection.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_render_job_depth2_exp2047 import SHARED_SOURCES

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'
RENDER = ROOT / 'drivers/apple-agx/render-admission'


def function(text, name):
    start = text.index(name)
    start = text.rfind('\n', 0, start) + 1
    end = text.index('\n}', start) + 2
    return text[start:end]


SHIM = r'''
#include "render_job_timing.h"
typedef unsigned int ULONG;
typedef unsigned char UCHAR, BOOLEAN, KIRQL;
typedef unsigned long long ULONGLONG;
typedef void VOID;
typedef struct { long long QuadPart; } LARGE_INTEGER;
#define TRUE 1
#define FALSE 0
#define RtlCopyMemory memcpy
#define RtlZeroMemory(p,n) memset(p,0,n)
#define KeAcquireSpinLock(lock,irql) ((void)(lock), *(irql)=0)
#define KeReleaseSpinLock(lock,irql) ((void)(lock), (void)(irql))
#define ADMISSION_QUEUE_OBJECT_TA_TIMESTAMP_START 30u
#define ADMISSION_QUEUE_OBJECT_TA_TIMESTAMP_END 31u
#define ADMISSION_QUEUE_OBJECT_BUFFER_MANAGER_MISC 22u
static LARGE_INTEGER KeQueryPerformanceCounter(LARGE_INTEGER *frequency) {
    LARGE_INTEGER now = {24000000};
    if (frequency) frequency->QuadPart = 24000000;
    return now;
}
typedef struct {
    APPLE_AGX_BACKEND_RUNTIME Backend;
    APPLE_AGX_EXP208_RELOCATION_OBJECT QueueObjects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
    ADMISSION_JOB_TIMING_STATE JobTiming;
    ADMISSION_GPU_TIMESTAMP_RING GpuTimestamps;
    ADMISSION_GPU_COMPLETION_RING GpuCompletions;
    ULONG JobTimingLock, ComputeTsFence, ComputeTsRequested;
    ULONGLONG ComputeTsCommandVa, ComputeTsWords[4];
    /* fields the sliced KMD functions read since EXP2205/T1 */
    BOOLEAN JobDiagnostics, PresentTsEnabled;
    ULONG SavedStatsFence; unsigned char *SavedStatsData; /* K1: unset here */
    struct {
      ULONG Version, Bytes, Count, Head;
      struct { ULONG ProcessId, Fence; ULONGLONG Start, End; } Ring[512];
    } PresentTs;
} ADMISSION_PLATFORM_RUNTIME;
#define RTL_NUMBER_OF(a) (sizeof(a) / sizeof((a)[0]))
/* Replace only the hardware pstate read; retain the actual timing writer. */
static ULONGLONG AdmissionJobTimingPstateWord(ADMISSION_PLATFORM_RUNTIME *runtime) {
    (void)runtime;return 0x80010203ULL;
}
static VOID AdmissionFwTimingCaptureWindows(ADMISSION_PLATFORM_RUNTIME *runtime,ULONG fence) {
    (void)runtime;(void)fence;
}
'''

EXERCISE = r'''
  {
    static ADMISSION_PLATFORM_RUNTIME runtime;
    APPLE_AGX_BACKEND_JOB_IMAGE first, second;
    ADMISSION_GPU_TIMESTAMP ts;
    ADMISSION_GPU_COMPLETION_QUERY query = {0};
    ADMISSION_JOB_TIMING_SLOT *timing;
    unsigned char *first_words[8];
    ULONGLONG end_ns = 0, word;
    unsigned n;
    /* per-job firmware timing is recorded only with Wom1JobDiagnostics */
    runtime.JobDiagnostics = TRUE;
    owner.NotifierFlight = FLIGHT_MODE;
    if (owner.NotifierFlight) assert(AppleAgxRenderFlightBegin(&owner,80));
#define BUILD(init) AppleAgxRenderSharedMemoryBuildActiveG4Job(&owner,arena,APPLE_AGX_EXP208_ARENA_BYTES,source_objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,init,&bindings,&staged_job,runtime.QueueObjects,&active_job)
    assert(BUILD(1));
    first=active_job; first.CommandVa=0x1500010000ULL;
    for(n=0;n<8;++n) first_words[n]=runtime.QueueObjects[28+n].Data;
    if (owner.NotifierFlight) {
      assert(AppleAgxRenderFlightPublish(&owner,80));
      AppleAgxRenderFlightVisible(&owner,80);
      assert(AppleAgxRenderFlightBegin(&owner,81));
    }
    staged_job.TaExpectedStamp+=0x100u; staged_job.D3ExpectedStamp+=0x100u;
    assert(BUILD(0));
    second=active_job; second.CommandVa=0x1500020000ULL;
    /* The actual second build changed both parity pointers. Firmware finishes
     * N after N+1 was built; an early snapshot at build time is wrong too. */
    for(n=0;n<8;++n) {
      assert(first_words[n]!=runtime.QueueObjects[28+n].Data);
      static const ULONGLONG ticks[8]={24000000ULL,26400000ULL,21600000ULL,24000000ULL,1,2,3,4};
      word=ticks[n];
      memcpy(first_words[n],&word,sizeof(word));
      word=ticks[n]+24000000ULL;
      memcpy(runtime.QueueObjects[28+n].Data,&word,sizeof(word));
    }
    if (owner.NotifierFlight) {
      assert(AppleAgxRenderFlightPublish(&owner,81));
      AppleAgxRenderFlightVisible(&owner,81);
    }
    for(n=0;n<2;++n) {
      runtime.Backend.PendingJob=n ? second:first;
      timing=&runtime.JobTiming.Slot[(80+n)%ADMISSION_JOB_TIMING_CAPACITY];
      memset(timing,0,sizeof(*timing));timing->Fence=80+n;timing->ProcessId=42;
      timing->Qpc[AdmissionJobPhaseSubmit]=11;
      timing->Qpc[AdmissionJobPhaseComplete]=22;
      AdmissionGpuTimestampCaptureWindows(&runtime,80+n);
      assert(AdmissionGpuTimestampFind(&runtime.GpuTimestamps,42,
          runtime.Backend.PendingJob.CommandVa,&ts));
      assert(ts.Start==(n ? 48000000ULL:24000000ULL));
      assert(ts.End==(n ? 50400000ULL:26400000ULL));
      assert(AdmissionGpuTimestampEndNanoseconds(&runtime,&end_ns));
      assert(end_ns==(n ? 2100000000ULL:1100000000ULL));
      AdmissionGpuCompletionCaptureWindows(&runtime,80+n,AppleAgxBackendCompletionSuccess);
      AdmissionGpuCompletionRead(&runtime.GpuCompletions,42,&query);
      assert(query.Count==1 && query.Record[0].Fence==80+n);
      assert(query.Record[0].Start==ts.Start && query.Record[0].End==ts.End);
      assert(query.Record[0].TaStart==(n ? 45600000ULL:21600000ULL));
      assert(query.Record[0].TaEnd==(n ? 48000000ULL:24000000ULL));
      AdmissionJobTimingFirmwareWindows(&runtime,80+n);
      assert(timing->FirmwareTaStart==query.Record[0].TaStart &&
             timing->FirmwareTaEnd==query.Record[0].TaEnd && timing->FirmwareValid==3u);
      assert(timing->Firmware3dEnd==0x80010203ULL);
      /* K2 (ring v5): the slot of any process keeps the job's own 3D pair
       * (the predecessor's parity, not the successor's rebound objects);
       * a plain render has no block. The v4 fields above are unchanged. */
      assert(timing->FirmwareStamp3dStart==query.Record[0].Start &&
             timing->FirmwareStamp3dEnd==query.Record[0].End);
      assert(timing->FirmwareStamps==ADMISSION_JOB_FW_STAMP_3D &&
             !timing->FirmwareStampBlockStart && !timing->FirmwareStampBlockEnd);
      /* Retry uses retained end even if firmware storage is no longer read. */
      runtime.Backend.PendingJob.TimestampEndNs=123456789ULL;
      assert(AdmissionGpuTimestampEndNanoseconds(&runtime,&end_ns) && end_ns==123456789ULL);
      /* Completion retry must not emit a second record. */
      AdmissionGpuCompletionCaptureWindows(&runtime,80+n,AppleAgxBackendCompletionSuccess);
      AdmissionGpuCompletionRead(&runtime.GpuCompletions,42,&query);
      assert(query.Count==0);
      AppleAgxRenderFlightRetire(&owner,80+n);
      if(n==0) {
        /* Build N+2 into the now free first parity, while N+1 still owns
         * the second. Test promotion independently of the latest build. */
        if(owner.NotifierFlight) assert(AppleAgxRenderFlightBegin(&owner,82));
        staged_job.TaExpectedStamp+=0x100u;staged_job.D3ExpectedStamp+=0x100u;
        assert(BUILD(0));
        word=72000000ULL;memcpy(runtime.QueueObjects[28].Data,&word,sizeof(word));
        word=74400000ULL;memcpy(runtime.QueueObjects[29].Data,&word,sizeof(word));
      }
    }
    if(owner.NotifierFlight) assert(AppleAgxRenderFlightCancel(&owner,82));
    /* No half-pair or fallback to current QueueObjects on a missing end. */
    runtime.Backend.PendingJob=first;
    runtime.Backend.PendingJob.CommandVa=0x1500030000ULL;
    runtime.Backend.PendingJob.RenderTimestampPointers[1]=NULL;
    timing=&runtime.JobTiming.Slot[83%ADMISSION_JOB_TIMING_CAPACITY];
    memset(timing,0,sizeof(*timing));timing->Fence=83;timing->ProcessId=42;
    AdmissionGpuTimestampCaptureWindows(&runtime,83);
    assert(!AdmissionGpuTimestampFind(&runtime.GpuTimestamps,42,
                                     runtime.Backend.PendingJob.CommandVa,&ts));
    assert(!AdmissionGpuTimestampEndNanoseconds(&runtime,&end_ns));
    AdmissionGpuCompletionCaptureWindows(&runtime,83,AppleAgxBackendCompletionSuccess);
    AdmissionGpuCompletionRead(&runtime.GpuCompletions,42,&query);
    assert(query.Count==1 && !query.Record[0].Start && !query.Record[0].End);
    /* Compute still uses its own block, regardless of render pointers. */
    {
      ULONGLONG block[4]={96000000ULL,98400000ULL,0,0};
      memset(&runtime.Backend.PendingJob,0,sizeof(runtime.Backend.PendingJob));
      runtime.Backend.PendingJob.Kind=AppleAgxBackendJobCompute;
      runtime.Backend.PendingJob.CommandVa=0x1500040000ULL;
      runtime.Backend.PendingJob.ComputeTimestampBlock=block;
      timing=&runtime.JobTiming.Slot[84%ADMISSION_JOB_TIMING_CAPACITY];
      memset(timing,0,sizeof(*timing));timing->Fence=84;timing->ProcessId=42;
      AdmissionGpuTimestampCaptureWindows(&runtime,84);
      assert(AdmissionGpuTimestampFind(&runtime.GpuTimestamps,42,
                                     runtime.Backend.PendingJob.CommandVa,&ts));
      assert(ts.Start==96000000ULL && ts.End==98400000ULL);
      assert(AdmissionGpuTimestampEndNanoseconds(&runtime,&end_ns) && end_ns==4100000000ULL);
      AdmissionGpuCompletionCaptureWindows(&runtime,84,AppleAgxBackendCompletionSuccess);
      AdmissionGpuCompletionRead(&runtime.GpuCompletions,42,&query);
      assert(query.Count==1 && query.Record[0].Start==ts.Start && query.Record[0].End==ts.End);
      assert(!query.Record[0].TaStart && !query.Record[0].TaEnd);
      AdmissionJobTimingFirmwareWindows(&runtime,84);
      assert(timing->Firmware3dEnd==0x80010203ULL);
      assert(!timing->FirmwareValid && !timing->FirmwareTaStart && !timing->FirmwareTaEnd);
      /* K2: a compute job's own block start/end, no 3D pair. */
      assert(timing->FirmwareStamps==(ADMISSION_JOB_FW_STAMP_BLOCK|ADMISSION_JOB_FW_STAMP_COMPUTE));
      assert(timing->FirmwareStampBlockStart==96000000ULL &&
             timing->FirmwareStampBlockEnd==98400000ULL);
      assert(!timing->FirmwareStamp3dStart && !timing->FirmwareStamp3dEnd);
    }
    /* K2: a render with a compute prelude keeps both; a missing 3D end
     * leaves no half pair and no mask bit. */
    {
      ULONGLONG block[4]={120000000ULL,120000600ULL,0,0};
      runtime.Backend.PendingJob=first;
      runtime.Backend.PendingJob.CommandVa=0x1500050000ULL;
      runtime.Backend.PendingJob.ComputeTimestampBlock=block;
      timing=&runtime.JobTiming.Slot[85%ADMISSION_JOB_TIMING_CAPACITY];
      memset(timing,0,sizeof(*timing));timing->Fence=85;timing->ProcessId=77;
      AdmissionJobTimingFirmwareWindows(&runtime,85);
      assert(timing->FirmwareStamps==(ADMISSION_JOB_FW_STAMP_3D|ADMISSION_JOB_FW_STAMP_BLOCK));
      assert(timing->FirmwareStampBlockStart==120000000ULL &&
             timing->FirmwareStampBlockEnd==120000600ULL);
      /* first's parity was rebuilt as N+2 above: its own objects' words */
      assert(timing->FirmwareStamp3dStart==72000000ULL && timing->FirmwareStamp3dEnd==74400000ULL);
      assert(timing->FirmwareValid==3u && timing->ProcessId==77);
      runtime.Backend.PendingJob.RenderTimestampPointers[1]=NULL;
      runtime.Backend.PendingJob.ComputeTimestampBlock=NULL;
      timing=&runtime.JobTiming.Slot[86%ADMISSION_JOB_TIMING_CAPACITY];
      memset(timing,0,sizeof(*timing));timing->Fence=86;timing->ProcessId=77;
      AdmissionJobTimingFirmwareWindows(&runtime,86);
      assert(!(timing->FirmwareStamps&ADMISSION_JOB_FW_STAMP_3D) &&
             !timing->FirmwareStamp3dStart && !timing->FirmwareStamp3dEnd);
      assert(timing->FirmwareValid==3u);
    }
#undef BUILD
  }
  for(index=0;index<fake.Allocated;++index) free(fake.Allocations[index].Storage);
  free(arena);
  return 0;
}
'''


class RenderTimestampOwnership(unittest.TestCase):
    def test_actual_capture_after_successor_switches_slot(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        QueueObjects must not substitute the successor's pair for a completed
        render, with legacy parity storage or q2 pinned per-flight storage.
        """
        windows = (RENDER / 'src/backend_platform_windows.c').read_text()
        helpers = ''
        if 'static BOOLEAN AdmissionRenderTimestampPair(' in windows:
            helpers = function(windows, 'static BOOLEAN AdmissionRenderTimestampPair(')
        capture = '\n'.join(function(windows, name) for name in (
            'static VOID AdmissionJobTimingPstateWindows(',
            'static BOOLEAN AdmissionGpuTimestampEndNanoseconds(',
            'static VOID AdmissionGpuTimestampCaptureWindows(',
            'static VOID AdmissionGpuCompletionCaptureWindows(',
            'static VOID AdmissionJobTimingFirmwareWindows('))
        fixture = (SHARED / 'tests/apple_agx_render_shared_memory_test.c').read_text()
        fixture = fixture[:fixture.index('  arena[layouts[0].ArenaOffset] = 0x5au;')]
        fixture = fixture.replace('int main(void)', SHIM + helpers + capture + '\nint main(void)')
        for flight in (0, 1):
            with self.subTest(notifier_flight=flight), tempfile.TemporaryDirectory() as d:
                source = Path(d) / 'test.c'
                binary = Path(d) / 'test'
                source.write_text(fixture + EXERCISE.replace('FLIGHT_MODE', str(flight)))
                built = subprocess.run([
                    os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-variable', '-Wno-unused-function',
                    '-Wshorten-64-to-32', '-Wimplicit-int-conversion',
                    '-fsanitize=address,undefined', '-ftrivial-auto-var-init=pattern',
                    '-I', str(SHARED / 'include'), '-I', str(RENDER / 'include'), str(source),
                    *[str(SHARED / 'src' / n) for n in SHARED_SOURCES],
                    str(RENDER / 'src/render_job_timing.c'), '-o', str(binary)],
                    capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr)
                ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(ran.returncode, 0, ran.stderr)


if __name__ == '__main__':
    unittest.main()
