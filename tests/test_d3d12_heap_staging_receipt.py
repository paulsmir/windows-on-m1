"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Per-Execute staging byte accounting must preserve 64-bit totals across intermediate submissions."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_umd_staging_write_watch import function
ROOT = Path(__file__).resolve().parents[1]

class HeapStagingReceipt(unittest.TestCase):
    def test_execute_measurement_covers_intermediate_submissions(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Dirty-byte receipts must count the complete Execute, retain 64-bit values and reset between Executes instead of leaking previous Map traffic. RC64: copy-escape class counters must reset the same way and saturate without carrying a count into the microsecond field."""
        from test_umd_deallocated_slot_no_evict import function as extract
        owner=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        ddi=(ROOT/'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text()
        body=function(owner.replace('extern "C" ', 'static '),'AgxD3d12OwnerMeasureHeapStaging')
        replay=extract(ddi,'ExecuteCommandLists')
        self.assertLess(replay.index('AgxD3d12OwnerLock('),replay.index('AgxD3d12OwnerMeasureHeapStaging(owner, TRUE)'))
        self.assertLess(replay.index('AgxD3d12OwnerMeasureHeapStaging(owner, TRUE)'),replay.index('ReplayAndSubmit('))
        self.assertLess(replay.index('ReplayAndSubmit('),replay.index('AgxD3d12OwnerMeasureHeapStaging(owner, FALSE)'))
        self.assertLess(replay.index('AgxD3d12OwnerMeasureHeapStaging(owner, FALSE)'),replay.index('AgxD3d12OwnerUnlock('))
        harness=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#define APPLE_AGX_GPUVA_WINSYS 1
#define S_OK 0
using UINT=unsigned;using BOOL=int;
struct AGX_D3D12_OWNER {struct {bool HeapStaging;uint64_t HeapStagingDirtyBytes,HeapStagingUploadBytes,HeapStagingDownloadBytes;UINT CopyEscapeCount[13];uint64_t CopyEscapeMicros[13];} Runtime;};
struct OwnerLock {explicit OwnerLock(AGX_D3D12_OWNER*){}};
static UINT words[7],classes[16];static unsigned receipts,class_receipts;
static void AdmissionUmdDiagnostic(const char *name,int,const UINT *v,UINT n) {
 if(!strcmp(name,"measure-copy-escape-class")) {assert(n==16 && receipts);++class_receipts;memcpy(classes,v,sizeof(classes));return;}
 assert(!strcmp(name,"measure-d3d12-heap-staging") && n==7);++receipts;memcpy(words,v,sizeof(words));
}
@@BODY@@
int main() {
 AGX_D3D12_OWNER o={};AgxD3d12OwnerMeasureHeapStaging(&o,0);assert(!receipts);
 o.Runtime.HeapStaging=true;o.Runtime.HeapStagingUploadBytes=999;
 AgxD3d12OwnerMeasureHeapStaging(&o,1);assert(!o.Runtime.HeapStagingUploadBytes);
 o.Runtime.HeapStagingDirtyBytes=(3ull<<32)+4;o.Runtime.HeapStagingUploadBytes=(5ull<<32)+6;
 o.Runtime.HeapStagingDownloadBytes=(7ull<<32)+8;
 AgxD3d12OwnerMeasureHeapStaging(&o,0);
 assert(receipts==1 && words[0]==1 && words[1]==4 && words[2]==3 && words[3]==6 && words[4]==5 && words[5]==8 && words[6]==7);
 assert(!o.Runtime.HeapStagingDirtyBytes && !o.Runtime.HeapStagingUploadBytes && !o.Runtime.HeapStagingDownloadBytes);
 /* RC64: no escapes, no class row; Map-time escapes before Execute reset. */
 assert(!class_receipts);
 o.Runtime.CopyEscapeCount[5]=9;AgxD3d12OwnerMeasureHeapStaging(&o,1);assert(!o.Runtime.CopyEscapeCount[5]);
 o.Runtime.CopyEscapeCount[0]=3;o.Runtime.CopyEscapeMicros[0]=210;      /* query, <=64 KiB */
 o.Runtime.CopyEscapeCount[5]=5000;o.Runtime.CopyEscapeMicros[5]=(1ull<<21); /* upload, <=1 MiB */
 o.Runtime.CopyEscapeCount[12]=1;o.Runtime.CopyEscapeMicros[12]=(5ull<<32);  /* outside a slot */
 AgxD3d12OwnerMeasureHeapStaging(&o,0);
 assert(class_receipts==1 && classes[0]==1);
 assert(classes[1]==((3u<<20)|210u) && classes[6]==((0xfffu<<20)|0xfffffu) && classes[13]==((1u<<20)|0xfffffu));
 assert(classes[2]==0 && classes[14]==5004u && classes[15]==0xffffffffu);
 for(unsigned i=0;i<13;++i) assert(!o.Runtime.CopyEscapeCount[i] && !o.Runtime.CopyEscapeMicros[i]);
}
'''.replace('@@BODY@@',body)
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'receipt.cpp';exe=Path(tmp)/'receipt';src.write_text(harness)
            b=subprocess.run(['clang++','-std=c++17',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(b.returncode,0,b.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
