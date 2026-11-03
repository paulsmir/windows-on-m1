"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2085 could not distinguish an empty receipt from failed export, and the
prebind gate hid decisions before the kick function. Exercise real diagnostic
helpers, including saturation, ring wrap, registry failures and empty export.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
INC = ROOT/'drivers/apple-agx/render-admission/include'
WORKER = ROOT/'drivers/apple-agx/render-admission/src/backend_platform_windows.c'

class RcDecisions(unittest.TestCase):
    def build(self, code):
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'t.c';b=Path(d)/'t';c.write_text(code)
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                '-Wconversion','-ftrivial-auto-var-init=pattern',
                '-fsanitize=address,undefined','-I',str(INC),str(c),'-o',str(b)],check=True)
            subprocess.run([str(b)],check=True)

    def test_empty_export_and_failed_write_retry(self):
        text=WORKER.read_text()
        self.assertIn('static VOID AdmissionRcExportWindows(',text)
        start=text.index('static VOID AdmissionRcExportWindows(')
        body=text[start:text.index('\n}',start)+2]
        self.build(r'''
#include "render_rc_progress.h"
#include <assert.h>
#include <wchar.h>
typedef void VOID;typedef int HANDLE,NTSTATUS;typedef const wchar_t *UNICODE_STRING;
#define FALSE 0
#define REG_BINARY 3
#define NT_SUCCESS(x) ((x)>=0)
typedef struct {unsigned RcReceiptEnabled,RcExportPending;
 ADMISSION_RC_RECEIPT RcReceipt;ADMISSION_RC_DECISIONS RcDecisions;} ADMISSION_PLATFORM_RUNTIME;
/* The independent drill exporter is covered by test_g4_depth2_drill_windows.
 * This fixture has no armed drill and exercises only RC registry writes. */
static void AdmissionDepth2DrillExportWindows(ADMISSION_PLATFORM_RUNTIME *r,HANDLE k) {
 (void)r;(void)k;
}
static unsigned calls,fail;
static void RtlInitUnicodeString(UNICODE_STRING *n,const wchar_t *v){*n=v;}
static int ZwSetValueKey(HANDLE k,UNICODE_STRING *n,unsigned z,unsigned t,void *p,unsigned long bytes){
 (void)k;(void)z;assert(t==3);++calls;
 unsigned *h=p;assert(h[0]==(!wcscmp(*n,L"Wom1RcProgress")?2u:4u) && h[1]==bytes);
 if(!wcscmp(*n,L"Wom1RcProgress")) assert(h[2]==0 && bytes==19984);
 return calls==fail ? -1:0;
}
'''+body+r'''
int main(void){ADMISSION_PLATFORM_RUNTIME r={0};
 AdmissionRcInitialize(&r.RcReceipt);AdmissionRcDecisionsInitialize(&r.RcDecisions);
 r.RcExportPending=1;AdmissionRcExportWindows(&r,1);assert(!calls && r.RcExportPending);
 r.RcReceiptEnabled=1;fail=1;AdmissionRcExportWindows(&r,1);assert(calls==2 && r.RcExportPending);
 calls=0;fail=2;AdmissionRcExportWindows(&r,1);assert(calls==2 && r.RcExportPending);
 calls=0;fail=0;AdmissionRcExportWindows(&r,1);assert(calls==2 && !r.RcExportPending);
}
''')

    def test_bounded_histogram_and_recent_exact_pair_records(self):
        self.assertIn('AdmissionRcDecision(', (INC/'render_rc_progress.h').read_text())
        self.build(r'''
#include "render_rc_progress.h"
#include <assert.h>
int main(void){ADMISSION_RC_DECISIONS r={0};
 AdmissionRcDecisionsInitialize(&r);
 for(unsigned i=0;i<5;++i) AdmissionRcDecision(&r,66,0,99,0,123,10+i);
 assert(r.Histogram[66]==5 && r.IdleCount==1 && r.IdleRecords[0].Count==5);
 assert(r.IdleRecords[0].RFence==99 && r.IdleRecords[0].CFence==0 && r.IdleRecords[0].ProcessId==123);
 AdmissionRcDecision(&r,68,29,99,100,123,20);
 assert(r.Count==1 && r.Records[0].Detail==29);
 for(unsigned i=0;i<128;++i) AdmissionRcDecision(&r,65,0,200+i,201+i,124+i,30+i);
 assert(r.Count==128 && r.Overwritten==1);
 r.Histogram[65]=~0u;AdmissionRcDecision(&r,65,0,327,328,251,200);
 assert(r.Histogram[65]==~0u);
 AdmissionRcDecision(&r,999,0,1,2,1,201);assert(r.Histogram[71]==1);
}
''')

    def test_skip_and_refusal_wiring_preserves_admission(self):
        text=WORKER.read_text()
        self.assertIn('AdmissionRcSkippedKickWindows(adapter, runtime,',text)
        kick=text[text.index('static BOOLEAN AdmissionJobWindowKickWindows('):]
        kick=kick[:kick.index('\n}')]
        self.assertIn('AdmissionRcDecisionTyped(&runtime->RcDecisions,',kick)
        self.assertLess(kick.index('AdmissionRcDecisionTyped(&runtime->RcDecisions,'),
                        kick.index('if (refusal != 0u)'))
        self.assertIn('AdmissionRcInitialize(&runtime->RcReceipt)',text)
        self.assertNotIn('runtime->RcReceiptEnabled && runtime->RcReceipt.Count',text)

    def test_running_pid_captured_before_release_can_fail(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A refused image release must retain the running PID even though the
        normal Depth2Running description is filled only after release succeeds.
        """
        text=WORKER.read_text()
        start=text.index('static VOID AdmissionRcDecisionWindows(')
        body=text[start:text.index('\n}',start)+2]
        self.assertIn('runtime->RcDecisionPid',body)
        self.assertNotIn('Depth2Running.Process',body)
        adopted=text[text.index('AdoptedJob:'):text.index('AdoptedKicked:')]
        self.assertIn('runtime->RcDecisionPid =',adopted)
        self.assertIn('description.ContextToken',adopted)

    def test_terminal_prebind_detail_survives_done_true(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Prebind refusals 28/29 set Done despite no bound successor. The caller
        must retain that reason even when it proceeds to the kick decision.
        """
        text=WORKER.read_text()
        self.assertIn('(unsigned int)prebindRefusal,(unsigned int)fence,s.Next.Fence,',text)
        self.assertIn('description.Fence, prebindRefusal);',text)

    def test_typed_decision_coalescing_requires_identical_classes(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Equal fence IDs with a different pair class cannot be merged into the
        same diagnostic observation; zero-initialized classes stay unknown.
        """
        self.build(r'''#include "render_rc_progress.h"
#include <assert.h>
int main(void) {
 ADMISSION_RC_DECISIONS r={0};AdmissionRcDecisionsInitialize(&r);
 AdmissionRcDecisionTyped(&r,0,0,99,100,42,10,1,3,3);
 AdmissionRcDecisionTyped(&r,0,0,99,100,42,11,1,3,3);
 assert(r.Count==1 && r.Records[0].Count==2);
 AdmissionRcDecisionTyped(&r,0,0,99,100,42,12,3,1,1);
 assert(r.Count==2 && r.Records[1].RunningKind==3 && r.Records[1].NextKind==1);
 assert(r.Records[1].Barrier==1 && r.Histogram[0]==3);
 AdmissionRcDecision(&r,0,0,99,100,42,13);
 assert(r.Count==3 && !r.Records[2].RunningKind && !r.Records[2].Barrier);
}
''')

    def test_probe_decisions_survive_foreign_idle_flood(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        EXP2091 lost every probe decision to reason-66 DWM polls. Changing
        background fences must not evict a probe admission or typed refusal.
        """
        self.build(r'''#include "render_rc_progress.h"
#include <assert.h>
int main(void) {
 ADMISSION_RC_DECISIONS r={0};AdmissionRcDecisionsInitialize(&r);
 AdmissionRcDecisionTyped(&r,64,7,99,100,42,1,3,1,1);
 AdmissionRcDecisionTyped(&r,0,0,101,102,42,2,3,1,1);
 for(unsigned i=0;i<30000;++i) AdmissionRcDecision(&r,66,0,200+i,0,123,3+i);
 unsigned found=0;
 for(unsigned i=0;i<r.Count;++i) if(r.Records[i].ProcessId==42) ++found;
 assert(found==2 && r.Histogram[66]==30000 && r.Histogram[64]==1 && r.Histogram[0]==1);
 assert(r.Records[0].Reason==64 && r.Records[0].Barrier==1 && r.Records[1].Reason==0);
}
''')

    def test_all_reasons_survive_interleaved_changing_fence_flood(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        EXP2093 lost probe decisions to changing-fence reason-64 DWM records.
        Stress 30000 repeats: every reason must aggregate by identity even when
        another identity intervenes, retaining first/last fences and counts.
        """
        self.build(r'''#include "render_rc_progress.h"
#include <assert.h>
int main(void) {
 ADMISSION_RC_DECISIONS r={0};AdmissionRcDecisionsInitialize(&r);
 AdmissionRcDecisionTyped(&r,0,0,99,100,42,1,3,1,1);
 for(unsigned i=0;i<30000;++i) {
   AdmissionRcDecisionTyped(&r,64,0,200+i,201+i,123,2+2*i,1,1,2);
   AdmissionRcDecisionTyped(&r,4,0,200+i,201+i,123,3+2*i,1,1,2);
 }
 assert(r.Count==3 && !r.Overwritten && r.Records[0].ProcessId==42);
 assert(r.Histogram[64]==30000 && r.Histogram[4]==30000);
 assert(r.Records[1].RFence==200 && r.Records[1].CFence==201);
 assert(r.Records[1].Count==30000 && r.Records[1].FirstLow==2);
 assert(r.Records[1].LastLow==60000);
 assert(r.Records[1].LastRFence==30199 && r.Records[1].LastCFence==30200);
 /* Each component of the key must separate records. */
 AdmissionRcDecisionTyped(&r,64,1,1,2,123,60002,1,1,2);
 AdmissionRcDecisionTyped(&r,64,0,1,2,124,60003,1,1,2);
 AdmissionRcDecisionTyped(&r,64,0,1,2,123,60004,3,1,2);
 AdmissionRcDecisionTyped(&r,64,0,1,2,123,60005,1,3,2);
 AdmissionRcDecisionTyped(&r,64,0,1,2,123,60006,1,1,1);
 assert(r.Count==8);
 for(unsigned reason=0;reason<72;++reason) {
   ADMISSION_RC_DECISIONS all={0};AdmissionRcDecisionsInitialize(&all);
   AdmissionRcDecisionTyped(&all,reason,7,10,11,42,1,3,1,1);
   AdmissionRcDecisionTyped(&all,reason,8,20,21,42,2,3,1,1);
   AdmissionRcDecisionTyped(&all,reason,7,30,31,42,3,3,1,1);
   ADMISSION_RC_DECISION *s=reason==66 ? all.IdleRecords:all.Records;
   assert(s[0].Count==2 && s[0].RFence==10 && s[0].CFence==11);
   assert(s[0].LastLow==3 && all.Histogram[reason]==3);
   assert(s[0].LastRFence==30 && s[0].LastCFence==31);
 }
}
''')

    def test_idle_ring_wrap_coalescing_and_saturation(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Reserving idle storage must preserve semantic identity, every histogram
        increment, bounded wrap and saturated counters rather than wrap to zero.
        """
        self.build(r'''#include "render_rc_progress.h"
#include <assert.h>
int main(void) {
 ADMISSION_RC_DECISIONS r={0};AdmissionRcDecisionsInitialize(&r);
 assert(r.Version==4 && r.Bytes==9920);
 for(unsigned i=0;i<40;++i) AdmissionRcDecision(&r,66,0,i+1,0,43+i,i+1);
 assert(r.IdleCount==32 && r.IdleNext==8 && r.IdleOverwritten==8);
 assert(!r.Count && !r.Overwritten && r.Histogram[66]==40);
 AdmissionRcDecision(&r,66,0,40,0,82,41);
 assert(r.IdleRecords[7].Count==2 && r.IdleRecords[7].LastLow==41);
 r.IdleRecords[7].Count=~0u;r.Histogram[66]=~0u;
 AdmissionRcDecision(&r,66,0,40,0,82,42);
 assert(r.IdleRecords[7].Count==~0u && r.Histogram[66]==~0u);
 r.IdleOverwritten=~0u;AdmissionRcDecision(&r,66,0,41,0,83,43);
 assert(r.IdleOverwritten==~0u && r.IdleNext==9);
}
''')
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'abi.c'
            c.write_text('#include "render_rc_progress.h"\n'
                '_Static_assert(sizeof(ADMISSION_RC_DECISIONS)==9920,"size");\n'
                '_Static_assert(__builtin_offsetof(ADMISSION_RC_DECISIONS,IdleCount)==7988,"append");\n'
                '_Static_assert(__builtin_offsetof(ADMISSION_RC_DECISIONS,IdleRecords)==8000,"records");\n')
            for target in ['i686-pc-windows-msvc','x86_64-pc-windows-msvc','aarch64-pc-windows-msvc']:
                subprocess.run(['clang','-target',target,'-std=c11','-Werror','-Wconversion',
                    '-fsyntax-only','-I',str(INC),str(c)],check=True)
