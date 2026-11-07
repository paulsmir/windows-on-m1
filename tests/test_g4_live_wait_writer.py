"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The live-wait ring must retain every counted refusal across wrap and concurrent
staging, and serialize coherent snapshots without registry I/O in a paging DDI.
Run the real stage/worker; OS locks/queue/registry and unrelated receipt types
are host services. The new receipt ABI is the production header.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_paging_ddi_no_registry_io import function
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'drivers/apple-agx/render-admission/src'
INC=SRC.parent/'include'

class LiveWaitWriter(unittest.TestCase):
    def test_actual_stage_worker_ring_and_concurrent_snapshots(self):
        source=(SRC/'receipts.c').read_text()
        types=source[source.index('enum {\n  AdmissionPagingReceiptDdiStatus'):source.index('static IO_WORKITEM_ROUTINE AdmissionPagingReceiptWorker;')]
        bodies='\n'.join(function(source,n) for n in ('AdmissionPagingReceiptStage','AdmissionPagingReceiptWorker','AdmissionRecordG3LiveWait'))
        if os.environ.get('Q22D_NEGATIVE')=='writer':
            bodies=bodies.replace('++r->Histogram[entry->Reason]', '(void)r->Histogram[entry->Reason]')
        code=(ROOT/'tests/g4_live_wait_writer_shim.h').read_text()+types+'\n'+r'''
static IO_WORKITEM_ROUTINE AdmissionPagingReceiptWorker;
static void AdmissionRecordG3LiveWait(ADMISSION_CONTEXT *,const ADMISSION_G3_LIVE_WAIT_ENTRY *);
static void AdmissionPagingReceiptWrite(ADMISSION_CONTEXT *,PCWSTR,const VOID *,ULONG,BOOLEAN);
'''+bodies+'\n'+(ROOT/'tests/g4_live_wait_writer_case.c').read_text()
        with tempfile.TemporaryDirectory(prefix='q22d-writer-') as d:
            d=Path(d);(d/'case.c').write_text(code)
            r=subprocess.run(['clang','-std=gnu11','-Wall','-Wextra','-Werror',
                '-ftrivial-auto-var-init=pattern','-fsanitize=address,undefined','-pthread',
                '-I',str(INC),str(d/'case.c'),'-o',str(d/'case')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            r=subprocess.run([str(d/'case')],capture_output=True,text=True,timeout=15)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
