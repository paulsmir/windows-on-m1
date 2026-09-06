"""Execute production paging encoder and Patch routing against EXP499 arguments."""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
SHARED = ROOT / 'drivers/apple-agx/shared'


class PagingPatchTests(unittest.TestCase):
    def test_paging_dispatch_validation_and_idempotence(self):
        source = (RENDER / 'src/gdi_windows.c').read_text()
        paging = (RENDER / 'src/paging_windows.c').read_text()
        encoder = re.search(r'static NTSTATUS AdmissionEncodePaging\(.*?^}', paging, re.S | re.M).group(0)
        helper = re.search(r'static NTSTATUS AdmissionPatchPaging\(.*?^}', source, re.S | re.M)
        start = source.index('PAGED_CODE();', source.index('NTSTATUS AdmissionDdiPatch('))
        end = source.index('  context = (ADMISSION_RENDER_CONTEXT *)Args->hContext;', start)
        route = source[start:end]
        shim = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "render_paging.h"
#include "render_objects.h"
#define _Inout_
#define _In_
#define _In_opt_
#define PAGED_CODE() ((void)0)
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((int32_t)0xc000000d)
#define STATUS_INVALID_HANDLE ((int32_t)0xc0000008)
#define STATUS_NOT_SUPPORTED ((int32_t)0xc00000bb)
#define STATUS_INVALID_DEVICE_STATE ((int32_t)0xc0000184)
#define STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER ((int32_t)0xc01e0001)
#define ADMISSION_GDI_DMA_PRIVATE_SIZE 8192u
#define ADMISSION_GDI_COLOR_FILL_PATCH_COUNT 1u
#define ADMISSION_MAX_PAGING_RECORDS 64u
#define MAXULONG UINT32_MAX
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define RtlCopyMemory memcpy
#define RtlCompareMemory(a,b,n) (memcmp((a),(b),(n))==0?(n):0)
typedef int32_t NTSTATUS;
typedef void *HANDLE,*PMDL;
typedef unsigned int UINT,ULONG;
typedef uintptr_t ULONG_PTR;
typedef size_t SIZE_T;
typedef unsigned char UCHAR,*PUCHAR;
typedef struct {ADMISSION_OBJECT_ADAPTER ObjectAdapter;int Started;} ADMISSION_CONTEXT;
typedef struct {ADMISSION_OBJECT_CONTEXT Object;} ADMISSION_RENDER_CONTEXT;
typedef struct {void *pDmaBuffer;UINT DmaSize;void *pDmaBufferPrivateData;
 UINT DmaBufferPrivateDataSize;} DXGKARG_BUILDPAGINGBUFFER;
typedef struct {HANDLE hContext;UINT DmaBufferSegmentId;
 union {int64_t QuadPart;} DmaBufferPhysicalAddress;void *pDmaBuffer;
 UINT DmaBufferSize,DmaBufferSubmissionStartOffset,DmaBufferSubmissionEndOffset;
 void *pDmaBufferPrivateData;UINT DmaBufferPrivateDataSize;
 UINT DmaBufferPrivateDataSubmissionStartOffset,DmaBufferPrivateDataSubmissionEndOffset;
 const void *pAllocationList;UINT AllocationListSize;const void *pPatchLocationList;
 UINT PatchLocationListSize,PatchLocationListSubmissionStart,PatchLocationListSubmissionLength;
 UINT SubmissionFenceId;union {UINT Value;struct {UINT Paging:1;UINT Other:31;};} Flags;
 UINT EngineOrdinal;} DXGKARG_PATCH;
/* This fixture contains paging records, never a Present private packet.
 * The actual Present classifier/translator is exercised by its own suite. */
static int AdmissionPresentIsBltPrivate(void *data,UINT bytes){
 assert(data!=NULL && bytes>=sizeof(ADMISSION_PAGING_RECORD));return 0;
}
static NTSTATUS AdmissionPresentPatch(ADMISSION_CONTEXT *adapter,const DXGKARG_PATCH *args){
 (void)adapter;(void)args;assert(0 && "paging record routed as Present");return STATUS_NOT_SUPPORTED;
}
'''
        wrapper = '\nstatic NTSTATUS dispatch(ADMISSION_CONTEXT *adapter,const DXGKARG_PATCH *Args) {\n' + route + '\nreturn STATUS_NOT_SUPPORTED; /* Unchanged GDI implementation beyond test boundary. */\n}\n'
        cases = r'''
int main(void) {
 _Alignas(8) UCHAR dma[4096]={0},priv[4096]={0};
 UCHAR dmaBefore[4096],privBefore[4096];
 ADMISSION_CONTEXT adapter={0};adapter.Started=1;
 ADMISSION_OBJECT_DEVICE device={0};device.Adapter=&adapter.ObjectAdapter;
 ADMISSION_RENDER_CONTEXT context={0};context.Object.Magic=ADMISSION_OBJECT_CONTEXT_MAGIC;
 context.Object.Device=&device;context.Object.Flags=ADMISSION_CONTEXT_SYSTEM;
 APPLE_AGX_PHYSICAL_PAGING_PLAN plan={0};plan.Kind=AppleAgxPhysicalPagingFill;
 plan.Bytes=16384000;plan.FillPattern=0;
 DXGKARG_BUILDPAGINGBUFFER encode={dma,4096,priv,4096};
 assert(AdmissionEncodePaging(&encode,&plan,NULL)==0);
 assert((UCHAR *)encode.pDmaBuffer-dma==16);
 assert((UCHAR *)encode.pDmaBufferPrivateData-priv==72);
 DXGKARG_PATCH a={0};a.hContext=&context;a.DmaBufferSegmentId=1;
 a.DmaBufferPhysicalAddress.QuadPart=0x160fffe000LL;
 a.pDmaBuffer=dma;a.DmaBufferSize=4096;a.DmaBufferSubmissionEndOffset=16;
 a.pDmaBufferPrivateData=priv;a.DmaBufferPrivateDataSize=4096;
 a.DmaBufferPrivateDataSubmissionEndOffset=72;a.SubmissionFenceId=1;a.Flags.Value=1;
 memcpy(dmaBefore,dma,4096);memcpy(privBefore,priv,4096);
 assert(dispatch(&adapter,&a)==0);
 assert(dispatch(&adapter,&a)==0);
 assert(memcmp(dmaBefore,dma,4096)==0 && memcmp(privBefore,priv,4096)==0);
 a.hContext=NULL;assert(dispatch(&adapter,&a)==0);a.hContext=&context;
 a.Flags.Value=3;assert(dispatch(&adapter,&a)!=0);a.Flags.Value=1;
 a.pAllocationList=&device;assert(dispatch(&adapter,&a)!=0);a.pAllocationList=NULL;
 a.PatchLocationListSubmissionLength=1;assert(dispatch(&adapter,&a)!=0);
 a.PatchLocationListSubmissionLength=0;
 a.DmaBufferSubmissionEndOffset=4097;assert(dispatch(&adapter,&a)!=0);
 a.DmaBufferSubmissionEndOffset=16;
 a.DmaBufferPrivateDataSubmissionStartOffset=1;assert(dispatch(&adapter,&a)!=0);
 a.DmaBufferPrivateDataSubmissionStartOffset=0;
 priv[0]^=1;assert(dispatch(&adapter,&a)!=0);priv[0]^=1;
 dma[0]^=1;assert(dispatch(&adapter,&a)!=0);dma[0]^=1;
 device.Adapter=NULL;assert(dispatch(&adapter,&a)!=0);device.Adapter=&adapter.ObjectAdapter;
 /* A later independent submission may start inside both buffers. */
 assert(AdmissionEncodePaging(&encode,&plan,NULL)==0);
 a.DmaBufferSubmissionStartOffset=16;a.DmaBufferSubmissionEndOffset=32;
 a.DmaBufferPrivateDataSubmissionStartOffset=72;
 a.DmaBufferPrivateDataSubmissionEndOffset=144;
 memcpy(dmaBefore,dma,4096);memcpy(privBefore,priv,4096);
 assert(dispatch(&adapter,&a)==0);
 assert(memcmp(dmaBefore,dma,4096)==0 && memcmp(privBefore,priv,4096)==0);
 a.DmaBufferPrivateDataSubmissionEndOffset=145;assert(dispatch(&adapter,&a)!=0);
 a.Flags.Value=0;assert(dispatch(&adapter,&a)!=0); /* no GDI guard weakening */
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / 'patch.c'
            program.write_text(shim + encoder + ('\n'+helper.group(0) if helper else '') + wrapper + cases)
            binary = Path(tmp) / 'patch'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-fsanitize=address,undefined', '-I', str(RENDER / 'include'),
                            '-I', str(SHARED / 'include'), str(program),
                            str(RENDER / 'src/render_paging.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
