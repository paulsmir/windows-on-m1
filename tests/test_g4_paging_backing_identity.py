"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
CPU paging must retain original backing after native identity separation. A
queued transfer must see earlier executed fills, never a build-time byte copy.
Runs actual resolver, encoder, mapper and executor; OS mappings are host arrays.
A real atomic private range supplies two distinct native identities. Their bytes stay
untouched until an activation path exists. This does not implement or emulate
shadow activation, GPU execution or eviction.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
SHARED = ROOT / 'drivers/apple-agx/shared'

class PagingBackingIdentity(unittest.TestCase):
    def test_original_endpoints_and_ordered_contents(self):
        source = (RENDER / 'src/gpuva_g3_paging_windows.c').read_text()
        funcs = '\n'.join(body(source, name) for name in (
            'AdmissionG3FindPagingEdge', 'AdmissionG3ResolveLogicalVa',
            'AdmissionG3EncodeVirtualPaging', 'AdmissionG3MapPagingIpa',
            'AdmissionG3FillPattern', 'AdmissionG3ExecuteVirtualPaging'))
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p/'test.c').write_text(FIXTURE.replace('@@FUNCS@@', funcs))
            r = subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-I'+str(RENDER/'include'),
                '-I'+str(SHARED/'include'), str(p/'test.c'),
                str(RENDER/'src/render_paging.c'),
                str(RENDER/'src/render_pool_slab.c'), '-o', str(p/'test')],
                capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
            r = subprocess.run([str(p/'test')], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout+r.stderr)

FIXTURE = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "render_paging.h"
#include "render_pool_slab.h"
#include "apple_agx_gpuva_g3_translation.h"
typedef unsigned UINT; typedef unsigned long long ULONGLONG;
typedef long long LONGLONG; typedef uintptr_t ULONG_PTR;
typedef int NTSTATUS, BOOLEAN; typedef unsigned char UCHAR, *PUCHAR;
typedef void VOID, *PVOID; typedef size_t SIZE_T;
typedef struct {LONGLONG QuadPart;} PHYSICAL_ADDRESS;
#define TRUE 1
#define FALSE 0
#define MAXULONG 0xffffffffu
#define MAXULONGLONG (~0ULL)
#define NT_SUCCESS(x) ((x)>=0)
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER -1
#define STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER -2
#define STATUS_INVALID_ADDRESS -3
#define STATUS_INSUFFICIENT_RESOURCES -4
#define ADMISSION_OBJECT_CONTEXT_MAGIC 123
#define ADMISSION_MAX_PAGING_RECORDS 64
#define DXGK_OPERATION_VIRTUAL_FILL 9
#define DXGK_OPERATION_VIRTUAL_TRANSFER 8
#define DXGK_OPERATION_SIGNAL_MONITORED_FENCE 10
#define DXGK_MEMORY_TRANSFER_LOCAL_TO_SYSTEM 0
#define DXGK_MEMORY_TRANSFER_SYSTEM_TO_LOCAL 1
#define DXGK_MEMORY_TRANSFER_LOCAL_TO_LOCAL 2
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2
#define ADMISSION_MEMORY_POOL_SEGMENT 3
#define ADMISSION_MEMORY_APERTURE_SEGMENT 1
#define RtlZeroMemory(p,n) memset(p,0,n)
#define RtlCopyMemory memcpy
#define RtlMoveMemory memmove
#define UNREFERENCED_PARAMETER(p) ((void)(p))
#define PASSIVE_LEVEL 0
#define APC_LEVEL 1
#define MmCached 1
static int KeGetCurrentIrql(void) {return 0;}
static void KeMemoryBarrier(void) {}
typedef struct {int Lock;} ADMISSION_G3_STATE;
typedef struct NODE {struct NODE *Next;ULONGLONG Ipa,AuxIpa;UINT Index;} APPLE_AGX_GPUVA_G3_NODE;
typedef struct SHADOW {struct SHADOW *Next;ULONGLONG BrokerIpa;
 APPLE_AGX_GPUVA_G3_LOGICAL_PTE *LogicalPtes;} ADMISSION_G3_TABLE_SHADOW;
typedef struct {struct {int Created,Uncertain;APPLE_AGX_GPUVA_G3_NODE *Parents;} Graph;
 ADMISSION_G3_TABLE_SHADOW *TableShadows;} ADMISSION_G3_PROCESS;
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
typedef struct {PUCHAR CpuAddress;ULONGLONG GuestIpaAddress,Bytes;} ADMISSION_SCANOUT_MEMORY_VIEW;
static unsigned char local[0x10000], system_pages[4][4096], private_bytes[0x20000];
static ADMISSION_POOL_SLAB private_slab;
static ADMISSION_POOL_SLAB *NewPrivateSlab(void *ctx) {
 (void)ctx; for(unsigned i=0;i<32;++i) private_slab.Pa[i]=0x300000+i*0x10000;
 return &private_slab;
}
static void FreePrivateSlab(void *ctx,ADMISSION_POOL_SLAB *slab){
 (void)ctx;assert(slab==&private_slab);
}
static unsigned encoded;
static void ExAcquireFastMutex(int *p){assert(!*p);*p=1;}
static void ExReleaseFastMutex(int *p){assert(*p);*p=0;}
static NTSTATUS AdmissionG3SnapshotAperture(ADMISSION_CONTEXT *a,ULONGLONG *i,UINT *s){
 (void)a;(void)i;assert(*s==0 || *s==2);return 0;}
static void AdmissionMemoryPoolNoteRecord(ADMISSION_CONTEXT *a,ADMISSION_PAGING_RECORD *r){(void)a;(void)r;}
static void AdmissionPagingNoteEncoded(ADMISSION_CONTEXT *a,UINT n){(void)a;encoded+=n;}
static NTSTATUS AdmissionMemoryRuntimeLocalView(ADMISSION_CONTEXT *a,ADMISSION_SCANOUT_MEMORY_VIEW *v){
 (void)a;v->CpuAddress=local;v->GuestIpaAddress=0x100000;v->Bytes=sizeof(local);return 0;}
static int AdmissionMemoryPoolView(ADMISSION_CONTEXT *a,ADMISSION_SCANOUT_MEMORY_VIEW *v){(void)a;(void)v;return 0;}
static void *MmMapIoSpace(PHYSICAL_ADDRESS pa,UINT n,UINT cache){
 assert(n==4096 && cache==MmCached);
 for(unsigned i=0;i<4;++i) if(pa.QuadPart==0x200000+(LONGLONG)i*0x9000) return system_pages[i];
 if(pa.QuadPart>=0x300000 && pa.QuadPart<0x320000)
  return private_bytes+(pa.QuadPart-0x300000);
 return NULL;}
static void MmUnmapIoSpace(void *p,UINT n){assert(p && n==4096);}
@@FUNCS@@
int main(void){
 ADMISSION_G3_STATE state={0};ADMISSION_CONTEXT a={&state,0};DEVICE d={&a.ObjectAdapter};
 APPLE_AGX_GPUVA_G3_NODE middle={NULL,2,3,0},root={&middle,1,2,0};
 static APPLE_AGX_GPUVA_G3_LOGICAL_PTE ptes[8192];
 ADMISSION_G3_TABLE_SHADOW table={NULL,3,ptes};
 ADMISSION_G3_PROCESS p={{1,0,&root},&table};
 ADMISSION_RENDER_CONTEXT c={{123,&d},&p,0,1};
 unsigned char dma[4096];ADMISSION_PAGING_RECORD records[16],fill;
 ADMISSION_POOL_SLABS slabs={0};ADMISSION_POOL_PRIVATE leases[2]={{0}};
 slabs.NewSlab=NewPrivateSlab;slabs.FreeSlab=FreePrivateSlab;slabs.PrivateCap=2;
 assert(AdmissionPoolSlabTakePrivateRange(&slabs,2,leases));
 memset(private_bytes,0xa5,sizeof(private_bytes));
 for(unsigned i=0;i<4;++i){
  ptes[16+i].Flags=3;ptes[16+i].SegmentId=2;ptes[16+i].GuestIpa=0x100000+i*4096;
  ptes[16+i].NativeIpa=leases[0].Slab->Pa[leases[0].Slot]+i*4096; /* deliberately different: never a CPU endpoint */
  ptes[32+i].Flags=3;ptes[32+i].GuestIpa=0x200000+i*0x9000;
  ptes[32+i].NativeIpa=leases[1].Slab->Pa[leases[1].Slot]+i*4096;
 }
 DXGKARG_BUILDPAGINGBUFFER x={0};
 #define PREP() do{x.hSystemContext=&c;x.pDmaBuffer=dma;x.DmaSize=sizeof(dma);\
 x.pDmaBufferPrivateData=records;x.DmaBufferPrivateDataSize=sizeof(records);encoded=0;}while(0)
 memset(local,0xee,sizeof(local));memset(system_pages,0x22,sizeof(system_pages));
 x.Operation=9;x.FillVirtual.DestinationVirtualAddress=0x10000;
 x.FillVirtual.FillSizeInBytes=0x4000;x.FillVirtual.FillPattern=0x76543210;PREP();
 assert(!AdmissionG3EncodeVirtualPaging(&a,&x) && encoded==1);fill=records[0];
 assert(fill.DestinationIpa==0x100000 && local[0]==0xee);
 memset(&x,0,sizeof(x));x.Operation=8;x.TransferVirtual.SourceVirtualAddress=0x10000;
 x.TransferVirtual.DestinationVirtualAddress=0x20000;x.TransferVirtual.TransferSizeInBytes=0x4000;
 x.TransferVirtual.TransferDirection=0;PREP();
 assert(!AdmissionG3EncodeVirtualPaging(&a,&x) && encoded==4);
 assert(local[0]==0xee && system_pages[0][0]==0x22);
 assert(!AdmissionG3ExecuteVirtualPaging(&a,&fill));
 for(unsigned i=0;i<4;++i){
  assert(records[i].SourceIpa==0x100000+i*4096);
  assert(records[i].DestinationIpa==0x200000+i*0x9000);
  assert(!AdmissionG3ExecuteVirtualPaging(&a,&records[i]));
  for(unsigned j=0;j<4096;++j) assert(system_pages[i][j]==((UCHAR*)&fill.FillPattern)[j&3]);
 }
 for(unsigned i=0x8000;i<0xc000;++i) assert(local[i]==0xee);
 /* Reverse transfer consumes current original system bytes at execution. */
 memset(&x,0,sizeof(x));x.Operation=8;x.TransferVirtual.SourceVirtualAddress=0x20000;
 x.TransferVirtual.DestinationVirtualAddress=0x10000;x.TransferVirtual.TransferSizeInBytes=0x4000;
 x.TransferVirtual.TransferDirection=1;PREP();
 assert(!AdmissionG3EncodeVirtualPaging(&a,&x) && encoded==4);
 memset(system_pages,0x73,sizeof(system_pages));
 for(unsigned i=0;i<4;++i) assert(!AdmissionG3ExecuteVirtualPaging(&a,&records[i]));
 for(unsigned i=0;i<0x4000;++i) assert(local[i]==0x73);
 for(unsigned i=0;i<sizeof(private_bytes);++i) assert(private_bytes[i]==0xa5);
 assert(AdmissionPoolSlabGivePrivate(&slabs,&leases[0]));
 assert(slabs.PrivateCount==1);
 assert(AdmissionPoolSlabGivePrivate(&slabs,&leases[1]));
 assert(slabs.PrivateCount==0 && slabs.Stats.Slabs==0);
 assert(!state.Lock);
}
'''
