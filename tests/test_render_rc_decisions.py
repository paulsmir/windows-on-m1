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
static unsigned calls,fail;
static void RtlInitUnicodeString(UNICODE_STRING *n,const wchar_t *v){*n=v;}
static int ZwSetValueKey(HANDLE k,UNICODE_STRING *n,unsigned z,unsigned t,void *p,unsigned long bytes){
 (void)k;(void)z;assert(t==3);++calls;
 unsigned *h=p;assert(h[0]==1 && h[1]==bytes);
 if(!wcscmp(*n,L"Wom1RcProgress")) assert(h[2]==0 && bytes==18448);
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
 assert(r.Histogram[66]==5 && r.Count==1 && r.Records[0].Count==5);
 assert(r.Records[0].RFence==99 && r.Records[0].CFence==0 && r.Records[0].ProcessId==123);
 AdmissionRcDecision(&r,68,29,99,100,123,20);
 assert(r.Count==2 && r.Records[1].Detail==29);
 for(unsigned i=0;i<128;++i) AdmissionRcDecision(&r,65,0,200+i,201+i,124,30+i);
 assert(r.Count==128 && r.Overwritten==2);
 r.Histogram[65]=~0u;AdmissionRcDecision(&r,65,0,327,328,124,200);
 assert(r.Histogram[65]==~0u);
 AdmissionRcDecision(&r,999,0,1,2,1,201);assert(r.Histogram[71]==1);
}
''')

    def test_skip_and_refusal_wiring_preserves_admission(self):
        text=WORKER.read_text()
        self.assertIn('AdmissionRcSkippedKickWindows(adapter, runtime,',text)
        kick=text[text.index('static BOOLEAN AdmissionJobWindowKickWindows('):]
        kick=kick[:kick.index('\n}')]
        self.assertIn('AdmissionRcDecisionWindows(runtime, refusal,',kick)
        self.assertLess(kick.index('AdmissionRcDecisionWindows(runtime, refusal,'),
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
        self.assertIn('AdmissionRcDecisionWindows(runtime, refusal, prebindRefusal,',text)
        self.assertIn('description.Fence, prebindRefusal);',text)
