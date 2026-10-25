"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The KMD rejects the defined Src64KBPages/Dst64KBPages paging flags as invalid.
Exercise its real encoder: all three legal directions and page-size hints must
produce exact byte ranges; reserved flags must not emit a paging record.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'

class PagingTransferFlags(unittest.TestCase):
    def test_page_size_hints_preserve_transfer_ranges(self):
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "render_paging.h"
typedef unsigned UINT; typedef unsigned long long ULONGLONG;
typedef int NTSTATUS, BOOLEAN; typedef unsigned char *PUCHAR;
#define TRUE 1
#define FALSE 0
#define MAXULONG 0xffffffffu
#define NT_SUCCESS(x) ((x)>=0)
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER -1
#define STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER -2
#define ADMISSION_OBJECT_CONTEXT_MAGIC 123
#define ADMISSION_MAX_PAGING_RECORDS 64
#define DXGK_OPERATION_VIRTUAL_FILL 9
#define DXGK_OPERATION_VIRTUAL_TRANSFER 8
#define DXGK_OPERATION_SIGNAL_MONITORED_FENCE 10
#define DXGK_MEMORY_TRANSFER_LOCAL_TO_SYSTEM 0
#define DXGK_MEMORY_TRANSFER_SYSTEM_TO_LOCAL 1
#define DXGK_MEMORY_TRANSFER_LOCAL_TO_LOCAL 2
#define RtlZeroMemory(p,n) memset(p,0,n)
#define RtlCopyMemory memcpy
typedef struct {int Lock;} ADMISSION_G3_STATE;
typedef struct {int unused;} ADMISSION_G3_PROCESS;
typedef struct {void *Adapter;} DEVICE;
typedef struct {struct {UINT Magic;DEVICE *Device;} Object;
 ADMISSION_G3_PROCESS *GpuvaG3Process;int GpuvaG3Poisoned;ULONGLONG GpuvaG3RootIpa;
} ADMISSION_RENDER_CONTEXT;
typedef struct {void *GpuvaG3State;int ObjectAdapter;} ADMISSION_CONTEXT;
typedef struct {UINT Operation;void *hSystemContext;UINT MultipassOffset,DmaSize,DmaBufferPrivateDataSize,DmaBufferWriteOffset;
 void *pDmaBuffer,*pDmaBufferPrivateData;
 struct {ULONGLONG DestinationVirtualAddress,FillSizeInBytes;UINT FillPattern;} FillVirtual;
 struct {ULONGLONG MonitoredFenceGpuVa,MonitoredFenceValue;} SignalMonitoredFence;
 struct {ULONGLONG SourceVirtualAddress,DestinationVirtualAddress,TransferSizeInBytes;
 UINT TransferDirection;union {UINT Flags;struct {UINT Src64KBPages:1,Dst64KBPages:1,Reserved:30;};} Flags;} TransferVirtual;
} DXGKARG_BUILDPAGINGBUFFER;
static unsigned encoded;
static void ExAcquireFastMutex(int *p){assert(!*p);*p=1;}
static void ExReleaseFastMutex(int *p){assert(*p);*p=0;}
static NTSTATUS AdmissionG3ResolveLogicalVa(ADMISSION_G3_PROCESS *p,ULONGLONG root,
 ULONGLONG va,int write,ULONGLONG *ipa,UINT *segment){
 (void)p;(void)root;(void)write;
 assert(va>=0x10000 && va<0x40000);*ipa=va+0x900000000ULL;
 *segment=va>=0x30000?0:2;return 0;
}
static NTSTATUS AdmissionG3SnapshotAperture(ADMISSION_CONTEXT *a,ULONGLONG *i,UINT *s){(void)a;(void)i;(void)s;return 0;}
static void AdmissionMemoryPoolNoteRecord(ADMISSION_CONTEXT *a,ADMISSION_PAGING_RECORD *r){(void)a;(void)r;}
static void AdmissionPagingNoteEncoded(ADMISSION_CONTEXT *a,UINT n){(void)a;encoded+=n;}
@@FUNCTION@@
int main(void){
 ADMISSION_G3_STATE state={0};ADMISSION_CONTEXT a={&state,0};DEVICE d={&a.ObjectAdapter};
 ADMISSION_G3_PROCESS p={0};ADMISSION_RENDER_CONTEXT c={{123,&d},&p,0,1};
 unsigned char dma[4096],priv[8192];
 for(unsigned direction=0;direction<3;++direction)for(unsigned flags=0;flags<8;++flags){
  DXGKARG_BUILDPAGINGBUFFER x={0};encoded=0;
  x.Operation=8;x.hSystemContext=&c;x.DmaSize=sizeof(dma);x.DmaBufferPrivateDataSize=sizeof(priv);
  x.pDmaBuffer=dma;x.pDmaBufferPrivateData=priv;x.TransferVirtual.Flags.Flags=flags;
  x.TransferVirtual.SourceVirtualAddress=direction==1?0x30000:0x10000;
  x.TransferVirtual.DestinationVirtualAddress=direction==0?0x30000:0x20000;
  x.TransferVirtual.TransferSizeInBytes=0x10000;x.TransferVirtual.TransferDirection=direction;
  NTSTATUS status=AdmissionG3EncodeVirtualPaging(&a,&x);
  if(flags&4){assert(status==STATUS_INVALID_PARAMETER && !encoded && !x.MultipassOffset);continue;}
  assert(status==STATUS_SUCCESS && x.MultipassOffset==0x10000);
  assert(encoded==(direction==2?1u:16u));
  ADMISSION_PAGING_RECORD *r=(ADMISSION_PAGING_RECORD*)priv;unsigned bytes=0;
  for(unsigned j=0;j<encoded;++j){
   assert(r[j].SourceIpa==x.TransferVirtual.SourceVirtualAddress+0x900000000ULL+bytes);
   assert(r[j].DestinationIpa==x.TransferVirtual.DestinationVirtualAddress+0x900000000ULL+bytes);
   assert(r[j].Bytes==(direction==2?0x10000u:0x1000u));bytes+=r[j].Bytes;
  }
  assert(bytes==65536 && !state.Lock);
 }
}
'''
        code = code.replace('@@FUNCTION@@', body((RENDER/'src/gpuva_g3_paging_windows.c').read_text(), 'AdmissionG3EncodeVirtualPaging'))
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'t.c').write_text(code)
            run=subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                '-I'+str(RENDER/'include'),'-I'+str(ROOT/'drivers/apple-agx/shared/include'),
                str(p/'t.c'),str(RENDER/'src/render_paging.c'),'-o',str(p/'t')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            run=subprocess.run([str(p/'t')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
