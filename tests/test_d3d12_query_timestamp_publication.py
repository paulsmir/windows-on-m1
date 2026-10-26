"""RC40 completion-published timestamp sources for GPU query resolution.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A GPU query resolve cannot read a timestamp that exists only in a CPU escape.
Production parsing must admit only an owned writable eight-byte destination;
completion must publish it before residency/fence release, including retries,
and must refuse stale, foreign, invalid, or unsupported mappings without stores.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_g4_submit_virtual_replay import function_body
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers/apple-agx/shared"
RENDER = ROOT / "drivers/apple-agx/render-admission"


def run_c(source, parser=False):
    with tempfile.TemporaryDirectory(prefix="rc40-timestamp-") as tmp:
        tmp = Path(tmp)
        path = tmp / "test.c"
        path.write_text(source)
        command = [os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-I", str(SHARED / "include"),
                   "-I", str(RENDER / "include"),
                   "-I", str(ROOT / "tests"), str(path)]
        if parser:
            command.append(str(SHARED / "src/apple_agx_g4_submit.c"))
        subprocess.run(command + ["-o", str(tmp / "test")], check=True,
                       capture_output=True, text=True)
        result = subprocess.run([str(tmp / "test")], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr or f"production replay returned {result.returncode}")


class TimestampPublicationTests(unittest.TestCase):
    def test_compute_parser_accepts_only_writable_publication_target(self):
        run_c(r'''
#define main existing_compute_tests
#include "apple_agx_g4_compute_submit_test.c"
#undef main
int main(void) {
  ENVELOPE e; SPACE s; APPLE_AGX_G4_COMPUTE_VIEW v; APPLE_AGX_G4_FAILURE f;
  make(&e, &s);
  s.Map[4] = (MAPPING){0x1234500000ULL, 0x10000u, 1}; s.Count = 5;
  e.Compute.Timestamps = (APPLE_AGX_G4_TIMESTAMPS){2u, 0u, 0x34500000u, 0x12u};
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseOk);
  s.Map[4].Writable = 0;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseUnmapped && f.Write && f.Bytes == 8u);
  s.Map[4].Writable = 1; s.Map[4].Bytes = 4u;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseUnmapped);
  s.Map[4].Bytes = 0x10000u; e.Compute.Timestamps.EndHandle += 1u;
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  e.Compute.Timestamps = (APPLE_AGX_G4_TIMESTAMPS){2u, 1u, 0x34500000u, 0x12u};
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  e.Compute.Timestamps = (APPLE_AGX_G4_TIMESTAMPS){2u, 0u, 0u, 0x80u};
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  e.Compute.Timestamps = (APPLE_AGX_G4_TIMESTAMPS){3u, 0u, 0x34500000u, 0x12u};
  assert(parse(&e, &s, &v, &f) == AppleAgxG4ParseInvalid);
  return existing_compute_tests();
}
''', parser=True)

    def test_render_publishes_fragment_end_only_and_prelude_cannot_publish(self):
        run_c(r'''
#define main existing_render_compute_tests
#include "apple_agx_g4_render_compute_submit_test.c"
#undef main
int main(void) {
  PACKET p; SPACE s; APPLE_AGX_G4_SUBMIT_VIEW v;
  make(&p, &s);
  s.Map[s.Count++] = (MAPPING){0x1234500000ULL, 0x10000u, 1};
  p.Render.TimestampsFragment = (APPLE_AGX_G4_TIMESTAMPS){2u, 0u, 0x34500000u, 0x12u};
  assert(parse(&p, &s, &v) == AppleAgxG4ParseOk);
  s.Map[s.Count - 1u].Writable = 0;
  assert(parse(&p, &s, &v) == AppleAgxG4ParseUnmapped);
  s.Map[s.Count - 1u].Writable = 1;
  p.Compute.Timestamps = p.Render.TimestampsFragment;
  assert(parse(&p, &s, &v) != AppleAgxG4ParseOk);
  memset(&p.Compute.Timestamps, 0, sizeof(p.Compute.Timestamps));
  p.Render.TimestampsVertex = p.Render.TimestampsFragment;
  assert(parse(&p, &s, &v) != AppleAgxG4ParseOk);
  return existing_render_compute_tests();
}
''', parser=True)

    def test_publication_encode_decode_has_no_host_layout_dependency(self):
        header = (SHARED / "include/apple_agx_g4_submit.h").read_text()
        self.assertIn("AppleAgxG4TimestampPublishVa", header)
        run_c(r'''
#include "apple_agx_g4_submit.h"
#include <assert.h>
int main(void) {
  APPLE_AGX_G4_TIMESTAMPS ts = {0};
  assert(AppleAgxG4TimestampPublishVa(&ts) == 0);
  AppleAgxG4TimestampPublish(&ts, 0x1234500008ULL);
  assert(ts.StartHandle == APPLE_AGX_G4_TIMESTAMP_PUBLISH && !ts.StartOffset);
  assert(ts.EndHandle == 0x34500008u && ts.EndOffset == 0x12u);
  assert(AppleAgxG4TimestampPublishVa(&ts) == 0x1234500008ULL);
  ts.StartOffset = 1; assert(!AppleAgxG4TimestampPublishVa(&ts));
  AppleAgxG4TimestampPublish(&ts, (1ULL << 39));
  assert(!AppleAgxG4TimestampPublishVa(&ts));
  AppleAgxG4TimestampPublish(&ts, 0x1234500001ULL);
  assert(!AppleAgxG4TimestampPublishVa(&ts));
  AppleAgxG4TimestampPublish(&ts, 0u); assert(!AppleAgxG4TimestampPublishVa(&ts));
  return 0;
}
''')

    def test_actual_completion_publishes_before_release_and_fails_closed(self):
        source = (RENDER / "src/gpuva_g3_windows.c").read_text()
        functions = "\n".join(function_body(source, name) for name in (
            "AdmissionG3PublishTimestampLocked", "AdmissionGpuvaG3CompleteJobTimestamp",
            "AdmissionGpuvaG3CompleteJob"))
        run_c(COMPLETION_TYPES + functions + COMPLETION_CASES)

    def test_image_binding_and_relocate_preserve_target_and_reset_retry_cache(self):
        image = (RENDER / "src/render_backend_image.c").read_text()
        platform = (RENDER / "src/backend_platform_windows.c").read_text()
        functions = "\n".join(body(image, name) for name in (
            "AdmissionBackendBindingZero", "AdmissionBackendImageBindG4Compute"))
        functions += "\n" + body(platform, "AdmissionBackendRelocate")
        run_c(IMAGE_TYPES + functions + IMAGE_CASES)
        render = body(image, "AdmissionBackendImageBindG4Submission")
        self.assertIn("AppleAgxG4TimestampPublishVa(&render.TimestampsFragment)", render)

    def test_firmware_end_conversion_and_completion_wiring(self):
        source = (RENDER / "src/backend_platform_windows.c").read_text()
        extract = function_body(source, "AdmissionGpuTimestampEndNanoseconds")
        run_c(CONVERSION_TYPES + extract + CONVERSION_CASES)
        # This is a wiring invariant; the executed completion test above checks stores.
        complete = body(source, "AdmissionBackendComplete")
        self.assertLess(complete.index("AdmissionGpuTimestampEndNanoseconds"),
                        complete.index("AdmissionGpuvaG3CompleteJobTimestamp"))
        self.assertLess(complete.index("AdmissionGpuvaG3CompleteJobTimestamp"),
                        complete.index("DxgkCbNotifyInterrupt"))
        self.assertIn("Status == AppleAgxBackendCompletionSuccess", complete)
        relocate = body(source, "AdmissionBackendRelocate")
        self.assertIn("Job->TimestampPublishVa = image != NULL ? image->TimestampPublishVa : 0ULL", relocate)


COMPLETION_TYPES = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "apple_agx_g4_submit.h"
typedef unsigned long long ULONGLONG;
typedef unsigned long ULONG;
typedef unsigned char BOOLEAN;
typedef unsigned char *PUCHAR;
typedef size_t SIZE_T;
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
#define IO_NO_INCREMENT 0
#define APPLE_AGX_GPUVA_G3_VALID 1u
#define APPLE_AGX_GPUVA_G3_WRITE 2u
#define ADMISSION_MEMORY_LOCAL_SEGMENT 1u
#define ADMISSION_MEMORY_POOL_SEGMENT 3u
#define NT_SUCCESS(s) ((s) == 0)
#define ADMISSION_G3_POISON(p,n) ((p)->Poisoned = 1)
#define RtlCopyMemory memcpy
#define RtlZeroMemory(p,n) memset(p,0,n)
typedef struct { ULONGLONG GuestIpa, Allocation; unsigned Flags, SegmentId; } APPLE_AGX_GPUVA_G3_LOGICAL_PTE;
typedef struct { void *CpuAddress; ULONGLONG GuestIpaAddress, Bytes; } ADMISSION_SCANOUT_MEMORY_VIEW;
typedef struct { int JobInFlight, Uncertain; ULONGLONG LeaseToken; } GRAPH;
typedef struct _ADMISSION_RENDER_CONTEXT { void *GpuvaG3Process; int GpuvaG3Poisoned; } ADMISSION_RENDER_CONTEXT;
typedef struct _ADMISSION_G3_PRIVATE_SCENE { struct _ADMISSION_G3_PRIVATE_SCENE *Next; unsigned Queued, Started, Fence, GpuDone; } ADMISSION_G3_PRIVATE_SCENE;
typedef struct { GRAPH Graph; int Poisoned, CpuOnlyMappings; ADMISSION_G3_PRIVATE_SCENE *PrivateScenes; } ADMISSION_G3_PROCESS;
typedef struct { int Lock, JobEvent; ADMISSION_G3_PROCESS *ActiveProcess; ULONG ActiveFence, LastCompletedFence, PrivateCompletionFence; } ADMISSION_G3_STATE;
typedef struct { void *GpuvaG3State; } ADMISSION_CONTEXT;
static APPLE_AGX_GPUVA_G3_LOGICAL_PTE pte;
static unsigned char local_bytes[0x2000], pool_bytes[0x2000];
static unsigned irql, end_calls, barriers, wakes, access_ok, translate_ok, pte_ok, end_ok;
static ULONGLONG translated, expected;
static ADMISSION_G3_STATE *live_state;
static unsigned KeGetCurrentIrql(void) { return irql; }
static void ExAcquireFastMutex(int *lock) { assert(!*lock); *lock = 1; }
static void ExReleaseFastMutex(int *lock) { assert(*lock); *lock = 0; }
static void KeMemoryBarrier(void) { assert(live_state->Lock); ++barriers; }
static void KeSetEvent(int *event, int increment, int wait) { (void)event;(void)increment;(void)wait; ++wakes; }
static int AdmissionMemoryRuntimeLocalView(ADMISSION_CONTEXT *a, ADMISSION_SCANOUT_MEMORY_VIEW *v) { (void)a; *v = (ADMISSION_SCANOUT_MEMORY_VIEW){local_bytes,0x100000, sizeof(local_bytes)}; return 0; }
static int AdmissionMemoryPoolView(ADMISSION_CONTEXT *a, ADMISSION_SCANOUT_MEMORY_VIEW *v) { (void)a; *v = (ADMISSION_SCANOUT_MEMORY_VIEW){pool_bytes,0x200000, sizeof(pool_bytes)}; return 0; }
static const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *AdmissionG3CopyPte(const ADMISSION_G3_PROCESS *p, ULONGLONG va) { (void)p;(void)va; assert(live_state->Lock); return pte_ok ? &pte : NULL; }
static int AppleAgxGpuvaG3GraphContainsRangeAccess(GRAPH *g, ULONGLONG va, unsigned bytes, int write) { assert(g->JobInFlight && va && bytes == 8u && write); return access_ok; }
static int AppleAgxGpuvaG3GraphTranslateVa(GRAPH *g, ULONGLONG va, ULONGLONG *ipa) { (void)g;(void)va; *ipa = translated; return translate_ok; }
static int AppleAgxGpuvaG3TableSpanWithinLocal(ULONGLONG base, ULONGLONG bytes, ULONGLONG at, ULONGLONG count) { return at >= base && at - base <= bytes && count <= bytes - (at - base); }
static int AppleAgxGpuvaG3GraphEndJob(GRAPH *g) { ULONGLONG value; assert(live_state->Lock); memcpy(&value,pte.SegmentId == 3u ? pool_bytes + 8 : local_bytes + 8,8); assert(value == expected); ++end_calls; if(end_ok) g->JobInFlight = 0; return end_ok; }
'''

COMPLETION_CASES = r'''
static void setup(ADMISSION_CONTEXT *a, ADMISSION_G3_STATE *s, ADMISSION_G3_PROCESS *p, ADMISSION_RENDER_CONTEXT *c) {
 memset(s,0,sizeof(*s));memset(p,0,sizeof(*p)); memset(c,0,sizeof(*c));
 memset(local_bytes,0,sizeof(local_bytes));memset(pool_bytes,0,sizeof(pool_bytes));
 a->GpuvaG3State=s; live_state=s;s->ActiveProcess=p;s->ActiveFence=7;
 c->GpuvaG3Process=p;p->Graph.JobInFlight=1;p->Graph.LeaseToken=1;
 pte=(APPLE_AGX_GPUVA_G3_LOGICAL_PTE){0x100000,1,3,1};
 translated=0x100008; expected=123456789; irql=end_calls=barriers=wakes=0;
 access_ok=translate_ok=pte_ok=end_ok=1;
}
int main(void) {
 ADMISSION_CONTEXT a; ADMISSION_G3_STATE s; ADMISSION_G3_PROCESS p, foreign; ADMISSION_RENDER_CONTEXT c;
 setup(&a,&s,&p,&c);
 assert(AdmissionGpuvaG3CompleteJobTimestamp(&a,&c,7,0x1234500008ULL,expected));
 assert(end_calls==1 && barriers>=1 && wakes==1 && !s.ActiveProcess && s.LastCompletedFence==7);
 /* A completion retry must not reread mappings or republish an already freed BO. */
 pte_ok=access_ok=translate_ok=0;
 assert(AdmissionGpuvaG3CompleteJobTimestamp(&a,&c,7,0x1234500008ULL,expected));
 assert(end_calls==1 && wakes==1);
 for (unsigned which=0;which<14;which++) {
   setup(&a,&s,&p,&c); ULONGLONG va=0x1234500008ULL, value=expected; ULONG fence=7;
   switch(which) {
    case 0: va++;break; case 1: va=1ULL<<39;break;
    case 2: c.GpuvaG3Process=&foreign;break; case 3: fence++;break;
    case 4: pte.Flags=1;break; case 5: pte.SegmentId=0;break;
    case 6: pte.GuestIpa=0x102000; translated=0x102008;break;
    case 7: access_ok=0;break; case 8: translated++;break;
    case 9: pte_ok=0;break; case 10: p.Poisoned=1;break;
    case 11: p.Graph.JobInFlight=0;break; case 12: value=0;break;
    case 13: pte.Allocation=0;break;
   }
   assert(!AdmissionGpuvaG3CompleteJobTimestamp(&a,&c,fence,va,value));
   assert(!end_calls && !wakes && s.ActiveProcess==&p && !s.LastCompletedFence);
   ULONGLONG observed=0;memcpy(&observed,local_bytes+8,8);assert(!observed);
 }
 setup(&a,&s,&p,&c);pte.SegmentId=3;pte.GuestIpa=0x200000;translated=0x200008;
 assert(AdmissionGpuvaG3CompleteJobTimestamp(&a,&c,7,0x1234500008ULL,expected));
 setup(&a,&s,&p,&c);expected=0;
 assert(AdmissionGpuvaG3CompleteJob(&a,7)); assert(end_calls==1);
 setup(&a,&s,&p,&c);end_ok=0;
 assert(!AdmissionGpuvaG3CompleteJobTimestamp(&a,&c,7,0x1234500008ULL,expected));
 assert(p.Poisoned && s.ActiveProcess==&p && !s.LastCompletedFence);
 return 0;
}
'''

CONVERSION_TYPES = r'''
#include <assert.h>
#include <string.h>
typedef unsigned long long ULONGLONG;
typedef unsigned char UCHAR;
typedef int BOOLEAN;
typedef struct { long long QuadPart; } LARGE_INTEGER;
#define TRUE 1
#define FALSE 0
#define RtlCopyMemory memcpy
#define AppleAgxBackendJobCompute 1
#define AppleAgxBackendJobRender 2
typedef struct { int Kind; const void *ComputeTimestampBlock; ULONGLONG TimestampEndNs; } APPLE_AGX_BACKEND_JOB_IMAGE;
typedef struct { const void *Data; unsigned Size; } APPLE_AGX_EXP208_RELOCATION_OBJECT;
typedef struct { struct { APPLE_AGX_BACKEND_JOB_IMAGE PendingJob; } Backend; APPLE_AGX_EXP208_RELOCATION_OBJECT QueueObjects[30]; } ADMISSION_PLATFORM_RUNTIME;
static long long clock_frequency = 24000000;
static LARGE_INTEGER KeQueryPerformanceCounter(LARGE_INTEGER *f) { if(f) f->QuadPart=clock_frequency; return (LARGE_INTEGER){1}; }
'''
CONVERSION_CASES = r'''
int main(void) {
 ADMISSION_PLATFORM_RUNTIME r={0}; ULONGLONG ticks[2]={0,24000001}, ns=0;
 r.Backend.PendingJob.Kind=AppleAgxBackendJobCompute;r.Backend.PendingJob.ComputeTimestampBlock=ticks;
 assert(AdmissionGpuTimestampEndNanoseconds(&r,&ns) && ns==1000000041ULL);
 /* Image release may restore the block before completion notification retries. */
 ticks[1]=0;
 assert(AdmissionGpuTimestampEndNanoseconds(&r,&ns) && ns==1000000041ULL);
 r.Backend.PendingJob.TimestampEndNs=0;ticks[1]=24000001;
 r.Backend.PendingJob.Kind=AppleAgxBackendJobRender;r.QueueObjects[29].Data=&ticks[1];r.QueueObjects[29].Size=8;
 assert(AdmissionGpuTimestampEndNanoseconds(&r,&ns) && ns==1000000041ULL);
 r.Backend.PendingJob.TimestampEndNs=0;
 r.QueueObjects[29].Size=4;assert(!AdmissionGpuTimestampEndNanoseconds(&r,&ns));
 r.QueueObjects[29].Size=8;clock_frequency=0;assert(!AdmissionGpuTimestampEndNanoseconds(&r,&ns));
 clock_frequency=24000000;ticks[1]=0;assert(!AdmissionGpuTimestampEndNanoseconds(&r,&ns));
 ticks[1]=~0ULL;assert(!AdmissionGpuTimestampEndNanoseconds(&r,&ns));
 return 0;
}
'''

IMAGE_TYPES = r'''
#include <assert.h>
#include <string.h>
#include "render_backend_image.h"
typedef struct { ADMISSION_BACKEND_IMAGE BackendImage; } ADMISSION_CONTEXT;
typedef struct {
 ADMISSION_CONTEXT *Adapter;
 struct {
   void *Context;
   struct {
    APPLE_AGX_BACKEND_BOOL (*Relocate)(void *, void *, APPLE_AGX_BACKEND_U32,
     const APPLE_AGX_RENDER_TEMPLATE_ROOTS *, const unsigned char *,
     APPLE_AGX_BACKEND_U32, const APPLE_AGX_BACKEND_SUBMISSION *, APPLE_AGX_BACKEND_JOB_IMAGE *);
   } Image;
 } PlatformIo;
} ADMISSION_PLATFORM_RUNTIME;
static APPLE_AGX_BACKEND_BOOL relocate(void *c, void *arena, APPLE_AGX_BACKEND_U32 bytes,
 const APPLE_AGX_RENDER_TEMPLATE_ROOTS *roots, const unsigned char *sub, APPLE_AGX_BACKEND_U32 count,
 const APPLE_AGX_BACKEND_SUBMISSION *submission, APPLE_AGX_BACKEND_JOB_IMAGE *job) {
 (void)c;(void)arena;(void)bytes;(void)roots;(void)sub;(void)count;(void)submission;
 job->TimestampEndNs=99; return APPLE_AGX_BACKEND_TRUE;
}
'''
IMAGE_CASES = r'''
int main(void) {
 ADMISSION_CONTEXT adapter={0};ADMISSION_PLATFORM_RUNTIME runtime={0};
 APPLE_AGX_BACKEND_JOB_IMAGE job={0};APPLE_AGX_G4_COMPUTE_ENVELOPE envelope={0};
 adapter.BackendImage.Ready=1;
 envelope.Header.Base.Version=APPLE_AGX_G4_PRIVATE_VERSION_COMPUTE;
 envelope.Header.Base.CommandVa=0x1234000000ULL;envelope.Native.Type=APPLE_AGX_G4_COMPUTE;
 AppleAgxG4TimestampPublish(&envelope.Compute.Timestamps,0x1234500008ULL);
 assert(AdmissionBackendImageBindG4Compute(&adapter.BackendImage,7,&envelope));
 assert(adapter.BackendImage.TimestampPublishVa==0x1234500008ULL);
 runtime.Adapter=&adapter;runtime.PlatformIo.Image.Relocate=relocate;
 assert(AdmissionBackendRelocate(&runtime,NULL,0,NULL,NULL,0,NULL,&job));
 assert(job.TimestampPublishVa==0x1234500008ULL && !job.TimestampEndNs);
 assert(job.CommandVa==envelope.Header.Base.CommandVa);
 runtime.Adapter=NULL;
 assert(AdmissionBackendRelocate(&runtime,NULL,0,NULL,NULL,0,NULL,&job));
 assert(!job.TimestampPublishVa && !job.TimestampEndNs);
 return 0;
}
'''

if __name__ == "__main__":
    unittest.main()
