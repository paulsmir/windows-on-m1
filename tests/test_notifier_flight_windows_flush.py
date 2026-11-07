"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Object 12's staged CPU copy is not a DMA mapping. Passing it to the Windows
transport flush rejects every render before commit. Execute the actual Windows
object-flush loop against a transport that accepts only mapped storage.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_render_job_depth2_caps_rc98 import ROOT, SHARED, WORKER

class WindowsFlightFlush(unittest.TestCase):
    def test_staged_threshold_is_not_dma_flushed(self):
        text=WORKER.read_text();start=text.index('  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;',text.index('static APPLE_AGX_BACKEND_BOOL AdmissionExternalBuildJob('))
        end=text.index('  if (runtime->Adapter->BackendImage.Binding.Framebuffer.Active',start)
        loop=text[start:end]
        source=r'''
#include "apple_agx_render_shared_memory.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>
static unsigned char mapped[40][16];
static unsigned calls;
typedef struct { struct { APPLE_AGX_RENDER_SHARED_MEMORY_OWNER RenderSharedMemory; } Initdata;
 APPLE_AGX_EXP208_RELOCATION_OBJECT QueueObjects[40];
 struct { int (*FlushForDevice)(void *, const void *,unsigned); } TransportIo; } R;
static int flush(void *ctx,const void *address,unsigned bytes) {
 unsigned i;(void)ctx;(void)bytes;++calls;
 for(i=0;i<40;++i) if(address==mapped[i]) return 1;
 return 0;
}
static int run(R *runtime) { unsigned index;
'''+loop+r'''
 return 1; BuildFailure: return 0;
}
int main(void) {
 R r;unsigned i,staged=9; memset(&r,0,sizeof(r)); r.TransportIo.FlushForDevice=flush;
 for(i=0;i<40;++i) { r.QueueObjects[i].Data=mapped[i];r.QueueObjects[i].Size=4; }
 assert(run(&r) && calls==40);calls=0;
 r.Initdata.RenderSharedMemory.NotifierFlight=1;
 r.QueueObjects[12].Data=(unsigned char *)&staged;
 assert(run(&r) && calls==39 && staged==9);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'test.c';c.write_text(source);b=Path(d)/'test'
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                '-ftrivial-auto-var-init=pattern','-I',str(SHARED/'include'),str(c),'-o',str(b)],check=True)
            subprocess.run([str(b)],check=True)
