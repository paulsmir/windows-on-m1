"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The one-shot registry arm must be durably consumed before injection, including
write/flush failures; the KMD must stop polling only AFTER a paired kick and
release the stall only AFTER worker idle. Pinned terminal reports cannot be
silently overwritten or accept duplicate reports. Compile real helper bodies.
"""
from pathlib import Path
import subprocess
import os
import tempfile
import unittest
from test_g4_tdr_drill_poisoned_completion import extract
from test_render_job_window import INCLUDE, SHARED, RC102_SOURCES
from test_g4_rr_event_drain import model_source
from test_g4_depth2_recovery_drill import MAIN

SRC=INCLUDE.parent/'src/backend_platform_windows.c'

KMD = r'''
#include "render_depth2_drill.h"
#include "render_rc_progress.h"
#include "apple_agx_render_shared_memory.h"
typedef unsigned long ULONG;
typedef unsigned long long ULONGLONG;
typedef unsigned char BOOLEAN;
typedef void VOID;
#define FALSE 0
#define TRUE 1
typedef struct {unsigned TdrDrillAllowed;} ADMISSION_CONTEXT;
typedef struct {
 ADMISSION_DEPTH2_DRILL_RECEIPT Depth2Drill;
 APPLE_AGX_PLATFORM_PROVIDER Provider;
 APPLE_AGX_BACKEND_RUNTIME Backend;
 struct {APPLE_AGX_RENDER_SHARED_MEMORY_OWNER RenderSharedMemory;} Initdata;
 ADMISSION_RC_SAMPLE RcCurrent;
 unsigned Depth2KickedFence,Depth2General,RcExportPending,RcReceiptEnabled;
 ADMISSION_CONTEXT *Adapter;
} ADMISSION_PLATFORM_RUNTIME;
static ULONGLONG AdmissionJobQpc(void){return 0x100000002ULL;}
'''

SELECTOR = r'''
static void kmd_selector(void) {
  world *w=&W;setup_pair(K_R);
  static ADMISSION_PLATFORM_RUNTIME r;
  ADMISSION_CONTEXT a={1};memset(&r,0,sizeof(r));
  r.Adapter=&a;r.Depth2General=1;r.Depth2KickedFence=3;
  r.Provider=w->platform;r.Backend=w->be;
  r.Depth2Drill.Mode=1;r.RcCurrent.ProcessId=123;
  r.RcCurrent.RunningKind=K_R;r.RcCurrent.NextKind=K_R;r.RcCurrent.Barrier=2;
  APPLE_AGX_NOTIFIER_FLIGHT *f=r.Initdata.RenderSharedMemory.Flights;
  f[0].Fence=2;f[1].Fence=3;f[0].Published=f[1].Published=1;
  f[0].Visible=f[1].Visible=1;
  r.Depth2Drill.Mode=0;assert(!AdmissionDepth2DrillTakeWindows(&r));
  r.Depth2Drill.Mode=1;a.TdrDrillAllowed=0;assert(!AdmissionDepth2DrillTakeWindows(&r));
  a.TdrDrillAllowed=1;r.Depth2General=0;assert(!AdmissionDepth2DrillTakeWindows(&r));
  r.Depth2General=1;r.Backend.SuccessorPending=0;assert(!AdmissionDepth2DrillTakeWindows(&r));
  r.Backend.SuccessorPending=1;f[1].Visible=0;assert(!AdmissionDepth2DrillTakeWindows(&r));
  f[1].Visible=1;f[0].Published=0;assert(!AdmissionDepth2DrillTakeWindows(&r));
  f[0].Published=1;r.Depth2KickedFence=4;assert(!AdmissionDepth2DrillTakeWindows(&r));
  r.Depth2KickedFence=3;r.Depth2Drill.Attempts=64;assert(!AdmissionDepth2DrillTakeWindows(&r));
  r.Depth2Drill.Attempts=63;assert(AdmissionDepth2DrillTakeWindows(&r));
  assert(r.Depth2Drill.Attempts==64 && r.Depth2Drill.Queue.HeadFence==2 &&
      r.Depth2Drill.Queue.NextFence==3 && r.Depth2Drill.HeadFlight==1 &&
      r.Depth2Drill.NextFlight==2 && r.Depth2Drill.ProcessId==123 &&
      r.Depth2Drill.QpcLow==2 && r.Depth2Drill.QpcHigh==1);
  assert(!AdmissionDepth2DrillTakeWindows(&r));
}
'''

class Depth2DrillWindows(unittest.TestCase):
    def test_selector_with_real_runtime(self):
        source=model_source()+KMD+extract(SRC.read_text(), 'AdmissionRcExportRequest')
        source+=extract(SRC.read_text(), 'AdmissionDepth2DrillTakeWindows')
        source+=MAIN.replace('int main(void)', 'static int runtime_main(void)')
        source+=SELECTOR+'\nint main(void) {kmd_selector();return 0;}\n'
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'selector.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra',
                '-Werror','-Wno-unused-function','-ftrivial-auto-var-init=pattern',
                '-fsanitize=address,undefined','-I',str(SHARED/'include'),'-I',str(INCLUDE),
                str(p/'selector.c'),*map(str,RC102_SOURCES.values()),'-o',str(p/'selector')],check=True)
            subprocess.run([str(p/'selector')],check=True,timeout=30)

    def test_arm_consumption(self):
        src=SRC.read_text()
        self.assertIn('AdmissionDepth2DrillConsumeWindows',src)
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
typedef uint32_t ULONG;typedef uint64_t ULONGLONG;typedef unsigned char UCHAR;
typedef void *HANDLE;typedef const wchar_t *UNICODE_STRING;
typedef struct {void *PhysicalDeviceObject;} ADMISSION_CONTEXT;
typedef struct {ULONG Type,DataLength;UCHAR Data[4];} KEY_VALUE_PARTIAL_INFORMATION;
typedef KEY_VALUE_PARTIAL_INFORMATION *PKEY_VALUE_PARTIAL_INFORMATION;
#define REG_DWORD 4u
#define PLUGPLAY_REGKEY_DEVICE 1u
#define KEY_QUERY_VALUE 2u
#define KEY_SET_VALUE 4u
#define KeyValuePartialInformation 1u
#define NT_SUCCESS(x) ((x)>=0)
static unsigned fail,regvalue,regtype,length,opened,closed,writes,flushes;
static int IoOpenDeviceRegistryKey(void *p,unsigned a,unsigned b,HANDLE *k) {
 (void)p;(void)a;(void)b;++opened;*k=(void *)1;return fail==1?-1:0;}
static void RtlInitUnicodeString(UNICODE_STRING *n,const wchar_t *s){*n=s;}
static int ZwQueryValueKey(HANDLE k,UNICODE_STRING *n,unsigned c,void *p,ULONG size,ULONG *bytes){
 (void)k;(void)c;assert(wcscmp(*n,L"Wom1Depth2Drill")==0);assert(size>=sizeof(KEY_VALUE_PARTIAL_INFORMATION));
 KEY_VALUE_PARTIAL_INFORMATION *v=p;v->Type=regtype;v->DataLength=length;
 memcpy(v->Data,&regvalue,4);*bytes=sizeof(*v);return fail==2?-1:0;}
static int ZwSetValueKey(HANDLE k,UNICODE_STRING *n,unsigned a,unsigned type,void *data,ULONG size){
 (void)k;(void)n;(void)a;assert(type==REG_DWORD && size==4);++writes;
 if(fail==3)return -1;memcpy(&regvalue,data,4);return 0;}
static int ZwFlushKey(HANDLE k){(void)k;++flushes;return fail==4?-1:0;}
static void ZwClose(HANDLE k){(void)k;++closed;}
'''+extract(src,'AdmissionDepth2DrillConsumeWindows')+r'''
int main(void){ADMISSION_CONTEXT a={(void *)2};
 for(unsigned mode=0;mode<4;++mode)for(fail=0;fail<5;++fail){
  regvalue=mode;regtype=REG_DWORD;length=4;opened=closed=writes=flushes=0;
  ULONG got=AdmissionDepth2DrillConsumeWindows(&a);
  assert(got==((mode==1 || mode==2) && !fail?mode:0));
  assert(opened==1 && closed==(fail!=1));
  if(got){assert(!regvalue && writes==1 && flushes==1);assert(!AdmissionDepth2DrillConsumeWindows(&a));}
 }
 fail=0;regvalue=1;regtype=3;assert(!AdmissionDepth2DrillConsumeWindows(&a));
 regtype=REG_DWORD;length=3;assert(!AdmissionDepth2DrillConsumeWindows(&a));
 a.PhysicalDeviceObject=NULL;opened=0;assert(!AdmissionDepth2DrillConsumeWindows(&a) && !opened);
}
'''
        self.compile_run(code)

    @staticmethod
    def compile_run(code):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'t.c').write_text(code)
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wconversion',
                '-Wshorten-64-to-32','-ftrivial-auto-var-init=pattern','-fsanitize=address,undefined',
                '-I',str(INCLUDE),'-I',str(SHARED/'include'),str(p/'t.c'),'-o',str(p/'t')],check=True)
            subprocess.run([str(p/'t')],check=True)

    def test_export_keeps_the_selected_pair(self):
        src=SRC.read_text()
        self.compile_run(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "render_depth2_drill.h"
typedef void VOID;typedef void *HANDLE;typedef uint32_t ULONG;
typedef const wchar_t *UNICODE_STRING;
typedef struct {
 unsigned RcReceiptEnabled,RcExportPending;
 ADMISSION_DEPTH2_DRILL_RECEIPT Depth2Drill;
 struct {struct {APPLE_AGX_G13_QUEUE_RUNTIME Runtime;} QueueProvider;} Provider;
} ADMISSION_PLATFORM_RUNTIME;
static ADMISSION_DEPTH2_DRILL_RECEIPT saved;static unsigned writes,fail;
#define NT_SUCCESS(s) ((s)>=0)
#define FALSE 0
static void RtlInitUnicodeString(UNICODE_STRING *n,const wchar_t *s){*n=s;}
#define REG_BINARY 3u
static int ZwSetValueKey(HANDLE k,UNICODE_STRING *n,unsigned a,unsigned type,void *data,ULONG size){
 (void)k;(void)a;assert(type==REG_BINARY && size==164);
 assert(wcscmp(*n,L"Wom1Depth2DrillReceipt")==0);
 memcpy(&saved,data,size);++writes;return fail ? -1:0;
}
'''+extract(src,'AdmissionDepth2DrillExportWindows')+r'''
int main(void) {
 ADMISSION_PLATFORM_RUNTIME r={0};
 _Static_assert(sizeof(ADMISSION_DEPTH2_DRILL_RECEIPT)==164,"receipt ABI");
 _Static_assert(sizeof(APPLE_AGX_G13_DEPTH2_DRILL)==92,"queue receipt ABI");
 AdmissionDepth2DrillExportWindows(&r,(void *)1);assert(!writes);
 r.Depth2Drill.Mode=1;r.Depth2Drill.ReportedCount=2;
 r.Depth2Drill.ReportFence[0]=20;r.Depth2Drill.ReportFence[1]=21;
 r.Provider.QueueProvider.Runtime.Drill.HeadFence=20;
 r.Provider.QueueProvider.Runtime.Drill.NextFence=21;
 r.Provider.QueueProvider.Runtime.Drill.TerminalCount=2;
 /* Later DWM work changes live pending IDs, not the pinned record. */
 r.Provider.QueueProvider.Runtime.PendingFence=70;
 r.Provider.QueueProvider.Runtime.Successor.Fence=71;
 r.RcExportPending=1;fail=1;
 AdmissionDepth2DrillExportWindows(&r,(void *)1);assert(r.RcExportPending);
 fail=0;writes=0;
 AdmissionDepth2DrillExportWindows(&r,(void *)1);assert(!r.RcExportPending);
 assert(writes==1 && saved.Queue.HeadFence==20 && saved.Queue.NextFence==21 &&
        saved.ReportFence[0]==20 && saved.ReportFence[1]==21 && saved.Queue.TerminalCount==2);
}
''')

    def test_pinned_reports_and_wiring(self):
        self.assertTrue((INCLUDE/'render_depth2_drill.h').exists())
        self.compile_run(r'''
#include <assert.h>
#include "render_depth2_drill.h"
int main(void) {
 ADMISSION_DEPTH2_DRILL_RECEIPT r={0};r.Queue.HeadFence=20;r.Queue.NextFence=21;
 AdmissionDepth2DrillReport(&r,19,1);assert(!r.ReportedCount);
 AdmissionDepth2DrillReport(&r,20,1);AdmissionDepth2DrillReport(&r,21,5);
 assert(r.ReportedCount==2 && r.ReportFence[0]==20 && r.ReportFence[1]==21 && r.ReportStatus[1]==5);
 AdmissionDepth2DrillReport(&r,21,5);assert(r.ReportOverflow==1 && r.ReportedCount==2);
}
''')
        src=SRC.read_text()
        worker=extract(src,'AdmissionPlatformWorker')
        kick=worker.index('kickDone = AdmissionJobWindowKickWindows')
        inject=worker.index('AdmissionDepth2DrillTakeWindows')
        poll=worker.index('if (!AppleAgxPlatformProviderPoll(')
        self.assertLess(kick,inject);self.assertLess(inject,poll)
        quiesce=extract(src,'AdmissionPlatformRuntimeQuiesceActive')
        self.assertLess(quiesce.index('KeWaitForSingleObject'),quiesce.index('AppleAgxG13QueueRuntimeDrillRelease'))
        sample=extract(src,'AdmissionRcSampleWindows')
        self.assertIn('ADMISSION_RC_REPORTED',sample)
        self.assertIn('AdmissionDepth2DrillReport',sample)
