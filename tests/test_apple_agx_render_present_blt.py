"""Reproduce EXP505's real CDD buffered BLT against the production callback."""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT/'drivers/apple-agx/render-admission'
SHARED = ROOT/'drivers/apple-agx/shared'


class PresentBltTests(unittest.TestCase):
    def test_copy_codec_overlap_clipping_and_read_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary=Path(tmp)/'codec'
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-I',str(RENDER/'include'),'-I',str(SHARED/'include'),
                            str(RENDER/'tests/render_present_test.c'),str(RENDER/'src/render_present.c'),
                            str(RENDER/'src/render_allocation.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_exp505_buffered_blt_generates_copy_and_both_references(self):
        source = (RENDER/'src/callbacks.c').read_text()
        callback = re.search(r'_Use_decl_annotations_ NTSTATUS AdmissionDdiPresent\(.*?^}',source,re.S|re.M).group(0)
        shim = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "render_objects.h"
#include "render_allocation.h"
#include "render_present.h"
#include "render_submission.h"
#include "render_submit_trace.h"
#include "apple_agx_dma_shadow.h"
#define _Use_decl_annotations_
#define C_ASSERT(x) _Static_assert(x,#x)
#define TRUE 1
#define FALSE 0
#define RtlZeroMemory(p,n) memset(p,0,n)
#define RtlCopyMemory(p,q,n) memcpy(p,q,n)
#define FIELD_OFFSET(t,f) ((unsigned)offsetof(t,f))
#define STATUS_SUCCESS 0
#define NT_SUCCESS(s) ((s)>=0)
#define STATUS_INVALID_PARAMETER (-1)
#define STATUS_INVALID_HANDLE (-2)
#define STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE (-3)
#define STATUS_INVALID_ADDRESS (-4)
#define STATUS_INVALID_USER_BUFFER (-5)
#define STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER (-6)
#define D3DDDIFMT_A8R8G8B8 21
#define APPLE_AGX_SCANOUT_J313_SURFACE_SIZE 0xfa0000ULL
#define ADMISSION_OPEN_ALLOCATION_MAGIC 0x4f504152u
#define DXGK_PRESENT_SOURCE_INDEX 1
#define DXGK_PRESENT_DESTINATION_INDEX 2
#define CONTAINING_RECORD(p,t,f) ((t *)((char *)(p)-offsetof(t,f)))
typedef void *HANDLE;
typedef void *PVOID;
typedef void VOID;
typedef unsigned UINT;
typedef unsigned ULONG;
typedef int NTSTATUS;
typedef int BOOLEAN;
typedef unsigned long long ULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef unsigned char *PUCHAR;
typedef struct {int32_t left,top,right,bottom;} RECT;
typedef struct {int64_t QuadPart;} PHYSICAL_ADDRESS;
typedef struct {HANDLE hDeviceSpecificAllocation; unsigned WriteOperation:1,SegmentId:5,Reserved:26;
 PHYSICAL_ADDRESS PhysicalAddress;} DXGK_ALLOCATIONLIST;
typedef struct {unsigned AllocationIndex,SlotId,DriverId,AllocationOffset,PatchOffset,SplitOffset;} D3DDDI_PATCHLOCATIONLIST;
typedef struct {ADMISSION_OBJECT_DEVICE Object;} ADMISSION_DEVICE;
typedef struct {ADMISSION_OBJECT_CONTEXT Object;struct {int Active;} SchedulerContext;} ADMISSION_RENDER_CONTEXT;
typedef struct {ADMISSION_OBJECT_ADAPTER ObjectAdapter;int Started;} ADMISSION_CONTEXT;
typedef struct {unsigned Magic;ADMISSION_DEVICE *Device;unsigned RuntimeAllocation;
 ADMISSION_ALLOCATION_OBJECT *Allocation;int ReadOnly;} ADMISSION_OPEN_ALLOCATION;
typedef struct {void *pDmaBuffer;unsigned DmaSize;void *pDmaBufferPrivateData;
 unsigned DmaBufferPrivateDataSize;
 union {DXGK_ALLOCATIONLIST *pAllocationList;DXGK_ALLOCATIONLIST *pAllocationInfo;};
 D3DDDI_PATCHLOCATIONLIST *pPatchLocationListOut;unsigned PatchLocationListOutSize;
 unsigned MultipassOffset,Color;RECT DstRect,SrcRect;unsigned SubRectCnt;
 const RECT *pDstSubRects;unsigned FlipInterval;union {unsigned Value;} Flags;
 unsigned DmaBufferSegmentId;PHYSICAL_ADDRESS DmaBufferPhysicalAddress;
 unsigned Reserved;uint64_t DmaBufferGpuVirtualAddress;
 unsigned NumSrcAllocations,NumDstAllocations,PrivateDriverDataSize;void *pPrivateDriverData;
} DXGKARG_PRESENT;
typedef struct {HANDLE hContext;union {unsigned Value;} Flags;unsigned EngineOrdinal;
 void *pDmaBuffer;unsigned DmaBufferSize;void *pDmaBufferPrivateData;unsigned DmaBufferPrivateDataSize;
 DXGK_ALLOCATIONLIST *pAllocationList;unsigned AllocationListSize;
 D3DDDI_PATCHLOCATIONLIST *pPatchLocationList;unsigned PatchLocationListSize;
 unsigned PatchLocationListSubmissionStart,PatchLocationListSubmissionLength;
 unsigned DmaBufferSubmissionStartOffset,DmaBufferSubmissionEndOffset;
 unsigned DmaBufferPrivateDataSubmissionStartOffset,DmaBufferPrivateDataSubmissionEndOffset;
 unsigned SubmissionFenceId;
} DXGKARG_PATCH;
typedef struct {HANDLE hContext;union {unsigned Value;struct {unsigned Paging:1,Present:1,Other:30;};} Flags;
 unsigned EngineOrdinal,NodeOrdinal,SubmissionFenceId;
 void *pDmaBufferPrivateData;unsigned DmaBufferPrivateDataSize,DmaBufferSize;
 unsigned DmaBufferSubmissionStartOffset,DmaBufferSubmissionEndOffset;
 unsigned DmaBufferPrivateDataSubmissionStartOffset,DmaBufferPrivateDataSubmissionEndOffset;
} DXGKARG_SUBMITCOMMAND;
static unsigned traced_field,traced_value;
static void AdmissionSubmitTraceValueWindows(ADMISSION_CONTEXT *c,BOOLEAN enabled,
 unsigned field,unsigned value){(void)c;if(enabled){traced_field=field;traced_value=value;}}
static unsigned queue_calls,queued_fence,queued_bytes;
static unsigned char queued_copy[4096];
static NTSTATUS AdmissionPagingSubmitPresent(ADMISSION_CONTEXT *c,const DXGKARG_SUBMITCOMMAND *a,
 const VOID *data,UINT bytes){assert(c->Started);assert(bytes<=4096);++queue_calls;
 queued_fence=a->SubmissionFenceId;queued_bytes=bytes;memcpy(queued_copy,data,bytes);return 0;}
static size_t RtlCompareMemory(const void *a,const void *b,size_t n){return memcmp(a,b,n)==0?n:0;}
static void AdmissionRecordPresent(ADMISSION_DEVICE *d,const DXGKARG_PRESENT *p,unsigned b,NTSTATUS s){(void)d;(void)p;(void)b;(void)s;}
static void AdmissionFlushPresentTransfer(ADMISSION_CONTEXT *c){(void)c;}
'''
        cases = r'''
int main(void){
 ADMISSION_CONTEXT adapter={{ADMISSION_OBJECT_ADAPTER_MAGIC,1,1},1};
 ADMISSION_DEVICE device={{ADMISSION_OBJECT_DEVICE_MAGIC,2,&adapter.ObjectAdapter,0,1,2}};
 ADMISSION_RENDER_CONTEXT context={{ADMISSION_OBJECT_CONTEXT_MAGIC,2,&device.Object,0,0,1,0},{1}};
 ADMISSION_ALLOCATION_OBJECT src={0},dst={0};
 assert(AdmissionAllocationDescribe(2560,1600,4,1,21,1,&src.Description));
 assert(AdmissionAllocationDescribe(2560,1600,4,1,21,0,&dst.Description));
 src.Magic=dst.Magic=ADMISSION_ALLOCATION_OBJECT_MAGIC;
 ADMISSION_OPEN_ALLOCATION os={ADMISSION_OPEN_ALLOCATION_MAGIC,&device,1,&src,1};
 ADMISSION_OPEN_ALLOCATION od={ADMISSION_OPEN_ALLOCATION_MAGIC,&device,2,&dst,0};
 DXGK_ALLOCATIONLIST a[3]={0};a[1].hDeviceSpecificAllocation=&os;a[1].SegmentId=2;
 a[1].PhysicalAddress.QuadPart=0x1501000000LL;a[2].hDeviceSpecificAllocation=&od;
 a[2].SegmentId=2;a[2].WriteOperation=1;a[2].PhysicalAddress.QuadPart=0x1500000000LL;
 unsigned char dma[4096]={0},private_data[8192]={0};D3DDDI_PATCHLOCATIONLIST patches[256]={0};
 RECT rect={0,0,2560,1600};DXGKARG_PRESENT p={0};
 p.pDmaBuffer=dma;p.DmaSize=4096;p.pDmaBufferPrivateData=private_data;p.DmaBufferPrivateDataSize=8192;
 p.pAllocationList=a;p.pPatchLocationListOut=patches;p.PatchLocationListOutSize=256;
 p.SrcRect=p.DstRect=rect;p.SubRectCnt=1;p.pDstSubRects=&rect;p.Flags.Value=1;
 assert(AdmissionDdiPresent(&context,&p)==STATUS_SUCCESS);
 assert((unsigned char *)p.pDmaBuffer>dma && (unsigned char *)p.pDmaBuffer<=dma+4096);
 assert(p.pPatchLocationListOut==patches+2);
 assert(patches[0].AllocationIndex==1 && patches[1].AllocationIndex==2);
 assert(patches[0].PatchOffset==144 && patches[1].PatchOffset==152);
 assert(p.MultipassOffset==1);
 APPLE_AGX_DMA_SHADOW shadow;APPLE_AGX_DMA_SHADOW_VIEW view;ADMISSION_PRESENT_BLT_COMMAND command;
 assert(AppleAgxDmaShadowOpen(&shadow,private_data,8192));
 assert(AppleAgxDmaShadowFind(private_data,shadow.BytesUsed,0,184,&view));
 assert(AdmissionPresentBltValidate(view.Bytes,184,1,&command));
 assert(command.SourceLocation==0x0200001501000000ULL && command.DestinationLocation==0x0200001500000000ULL);
 DXGKARG_SUBMITCOMMAND submit={0};submit.hContext=&context;submit.Flags.Value=2;submit.SubmissionFenceId=37;
 submit.pDmaBufferPrivateData=private_data;submit.DmaBufferPrivateDataSize=8192;submit.DmaBufferSize=4096;
 submit.DmaBufferSubmissionEndOffset=184;submit.DmaBufferPrivateDataSubmissionEndOffset=shadow.BytesUsed;
 assert(AdmissionPresentSubmit(&adapter,&submit)==0); /* no Patch call */
 assert(queue_calls==1 && queued_fence==37 && queued_bytes==184);
 submit.SubmissionFenceId=38;submit.DmaBufferPrivateDataSubmissionEndOffset=0;
 assert(AdmissionPresentSubmit(&adapter,&submit)==0); /* nonpaging zero private subrange */
 assert(queue_calls==2 && queued_fence==38 && queued_bytes==184);
 submit.DmaBufferPrivateDataSubmissionEndOffset=shadow.BytesUsed-1;
 traced_field=traced_value=0;
 assert(AdmissionPresentSubmitTraced(&adapter,&submit,TRUE)==STATUS_INVALID_PARAMETER);
 assert(traced_field==AdmissionSubmitTracePresentGuard);
 assert(traced_value==AdmissionPresentSubmitPrivateEndLow);
 assert(queue_calls==2);
 submit.DmaBufferPrivateDataSubmissionEndOffset=shadow.BytesUsed;
 DXGKARG_PATCH patch={0};patch.hContext=&context;patch.Flags.Value=2;
 patch.pDmaBuffer=dma;patch.DmaBufferSize=4096;patch.pDmaBufferPrivateData=private_data;
 patch.DmaBufferPrivateDataSize=8192;patch.pAllocationList=a;patch.AllocationListSize=3;
 patch.pPatchLocationList=patches;patch.PatchLocationListSize=2;patch.PatchLocationListSubmissionLength=2;
 patch.DmaBufferSubmissionEndOffset=184;patch.DmaBufferPrivateDataSubmissionEndOffset=shadow.BytesUsed;
 patch.SubmissionFenceId=39;a[1].PhysicalAddress.QuadPart=0x1501800000LL;
 assert(AdmissionPresentPatch(&adapter,&patch)==0);
 assert(AdmissionPresentPatch(&adapter,&patch)==0); /* idempotent relocation */
 assert(AdmissionPresentBltValidate(view.Bytes,184,1,&command));
 assert(command.SourceLocation==0x0200001501800000ULL);
 assert(AdmissionPresentBltValidate(queued_copy,queued_bytes,1,&command));
 assert(command.SourceLocation==0x0200001501000000ULL); /* prior queued snapshot immutable */
 submit.SubmissionFenceId=39;assert(AdmissionPresentSubmit(&adapter,&submit)==0);
 assert(queue_calls==3 && queued_fence==39);
 assert(AppleAgxDmaShadowPatchU64(private_data,shadow.BytesUsed,144,0));
 assert(AdmissionPresentSubmit(&adapter,&submit)==STATUS_INVALID_PARAMETER);
 assert(queue_calls==3); /* cannot submit unresolved source */
 traced_field=traced_value=0;
 assert(AdmissionPresentSubmitTraced(&adapter,&submit,TRUE)==STATUS_INVALID_PARAMETER);
 assert(traced_field==AdmissionSubmitTracePresentGuard);
 assert(traced_value==AdmissionPresentSubmitResidency);
 assert(AppleAgxDmaShadowPatchU64(private_data,shadow.BytesUsed,144,0x0200001501800000ULL));
 submit.DmaBufferSubmissionStartOffset=1;traced_field=traced_value=0;
 assert(AdmissionPresentSubmitTraced(&adapter,&submit,TRUE)==STATUS_INVALID_PARAMETER);
 assert(traced_field==AdmissionSubmitTracePresentGuard);
 assert(traced_value==AdmissionPresentSubmitDmaStart);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            frontend=(RENDER/'src/present_windows.c').read_text()
            frontend=frontend.replace('#include "render_admission.h"','')
            program=Path(tmp)/'present.c'; program.write_text(shim+frontend+callback+cases)
            binary=Path(tmp)/'present'
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-I',str(RENDER/'include'),'-I',str(SHARED/'include'),str(program),
                            str(RENDER/'src/render_allocation.c'),str(RENDER/'src/render_present.c'),
                            str(RENDER/'src/render_submission.c'),
                            str(SHARED/'src/apple_agx_dma_shadow.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
