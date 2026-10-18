"""Replay the EXP911 WDDM paging resubmission through the actual KMD entry."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_g4_submit_virtual_replay import function_body

ROOT = Path(__file__).resolve().parents[1]
KMD = ROOT / 'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c'
SHARED = ROOT / 'drivers/apple-agx/shared'
RENDER = ROOT / 'drivers/apple-agx/render-admission'


class PagingResubmissionReplay(unittest.TestCase):
    def test_real_paging_subroutine_accepts_windows_resubmission_after_preemption(self):
        source = function_body(KMD.read_text(), 'AdmissionGpuvaG3SubmitVirtualPaging')
        fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "render_paging.h"
#include "apple_agx_scheduler.h"
#define _Use_decl_annotations_
#define MAXULONG 0xffffffffu
#define PASSIVE_LEVEL 0
#define DISPATCH_LEVEL 2
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((int)0xc000000d)
#define STATUS_DEVICE_BUSY ((int)0xc00000e8)
#define NT_SUCCESS(x) ((int)(x)>=0)
#define ADMISSION_CONTEXT_SYSTEM 1u
#define ADMISSION_CPU_PACKET_PAGING 1u
#define ADMISSION_MAX_PAGING_RECORDS 64u
#define RtlZeroMemory(p,n) memset((p),0,(n))
typedef unsigned int UINT,ULONG;
typedef unsigned long long ULONGLONG;
typedef int NTSTATUS;
typedef void *HANDLE;
typedef unsigned char BOOLEAN;
typedef union {unsigned Value; struct {unsigned Paging:1,Present:1,RedirectedPresent:1,
  NullRendering:1,Flip:1,FlipWithNoWait:1,ContextSwitch:1,Resubmission:1,
  VirtualMachineData:1,Reserved:23;};} DXGK_SUBMITCOMMANDFLAGS;
typedef struct _ADMISSION_CONTEXT ADMISSION_CONTEXT;
typedef struct {int Token;} ADMISSION_OBJECT_ADAPTER;
typedef struct {ADMISSION_OBJECT_ADAPTER *Adapter;} DEVICE;
typedef struct {unsigned Magic,Flags; DEVICE *Device;} OBJECT_CONTEXT;
typedef struct {OBJECT_CONTEXT Object; void *GpuvaG3Process; unsigned GpuvaG3Poisoned;
  ULONGLONG GpuvaG3RootIpa;} ADMISSION_RENDER_CONTEXT;
typedef struct {HANDLE hContext;ULONGLONG DmaBufferVirtualAddress;UINT DmaBufferSize;
  void *pDmaBufferPrivateData;UINT DmaBufferPrivateDataSize,DmaBufferUmdPrivateDataSize;
  UINT SubmissionFenceId,NodeOrdinal,EngineOrdinal;DXGK_SUBMITCOMMANDFLAGS Flags;}
  DXGKARG_SUBMITCOMMANDVIRTUAL;
typedef DXGKARG_SUBMITCOMMANDVIRTUAL DXGKARG_SUBMITCOMMAND;
struct _ADMISSION_CONTEXT {int Started;ADMISSION_OBJECT_ADAPTER ObjectAdapter;APPLE_AGX_SCHEDULER Scheduler;
  ULONG LastBranch;UINT QueueCalls;DXGK_SUBMITCOMMANDFLAGS LastQueueFlags;};
#define ADMISSION_OBJECT_CONTEXT_MAGIC 0x434f4152u
static unsigned KeGetCurrentIrql(void) {return PASSIVE_LEVEL;}
static NTSTATUS AdmissionG4SubmitReject(ADMISSION_CONTEXT *a, ADMISSION_RENDER_CONTEXT *c,
  const DXGKARG_SUBMITCOMMANDVIRTUAL *args,ULONG branch,NTSTATUS result,ULONG unused,BOOLEAN valid) {
  (void)c;(void)args;(void)unused;(void)valid;a->LastBranch=branch;return result;
}
enum {AdmissionG4RejectPagingInput=3,AdmissionG4RejectPagingShape=4,
      AdmissionG4RejectPagingRecords=5,AdmissionG4RejectPagingQueue=6};
static NTSTATUS AdmissionCpuQueueSubmit(ADMISSION_CONTEXT *a,const DXGKARG_SUBMITCOMMAND *p,
  ULONG kind,const void *records,UINT bytes) {
  assert(kind==ADMISSION_CPU_PACKET_PAGING && records && bytes==4*sizeof(ADMISSION_PAGING_RECORD));
  ++a->QueueCalls;a->LastQueueFlags=p->Flags;
  int queued=p->Flags.Resubmission ?
    AppleAgxSchedulerQueueResubmittedPagingFence(&a->Scheduler,0,0,p->SubmissionFenceId):0;
  if(!queued) queued=AppleAgxSchedulerQueueFence(&a->Scheduler,0,0,p->SubmissionFenceId);
  return queued ? STATUS_SUCCESS:STATUS_DEVICE_BUSY;
}
/* Extracted unmodified production function follows. */
#include "paging.inc"
int main(void) {
 ADMISSION_CONTEXT adapter={0};DEVICE device={0};ADMISSION_RENDER_CONTEXT context={0};
 ADMISSION_PAGING_RECORD records[4]={0};DXGKARG_SUBMITCOMMANDVIRTUAL args={0};
 device.Adapter=&adapter.ObjectAdapter;adapter.Started=1;AppleAgxSchedulerInitialize(&adapter.Scheduler);
 assert(AppleAgxSchedulerQueueFence(&adapter.Scheduler,0,0,0x1d3eu));
 assert(AppleAgxSchedulerActivateFence(&adapter.Scheduler,0,0,0x1d3eu));
 assert(AppleAgxSchedulerCompleteActiveFence(&adapter.Scheduler,0,0,0x1d3eu));
 context.Object.Magic=ADMISSION_OBJECT_CONTEXT_MAGIC;
 context.Object.Flags=ADMISSION_CONTEXT_SYSTEM;context.Object.Device=&device;
 context.GpuvaG3Process=&adapter;context.GpuvaG3RootIpa=0x9d4be8000ULL;
 for(unsigned i=0;i<4;++i){records[i].Header.Magic=ADMISSION_PAGING_MAGIC;
  records[i].Header.Version=ADMISSION_PAGING_VERSION;
  records[i].Header.RecordBytes=sizeof(records[i]);
  records[i].Kind=AdmissionPagingVirtualTransfer;
  records[i].SourceSegment=0;records[i].DestinationSegment=2;
  records[i].SourceIpa=0x95b3a6000ULL+i*0x1000;
  records[i].DestinationIpa=0x8e124c000ULL+i*0x1000;
  records[i].Bytes=0x1000;
 }
 args.hContext=&context;args.DmaBufferVirtualAddress=0x4101000;
 args.DmaBufferSize=4*sizeof(ADMISSION_PAGING_MARKER);
 args.pDmaBufferPrivateData=records;args.DmaBufferPrivateDataSize=sizeof(records);
 args.SubmissionFenceId=0x1d3fu;args.Flags.Value=0x81u;
 fprintf(stderr,"before started=%d magic=%x ptr=%p/%p flags=%x paging=%u resub=%u sizeof=%zu marker=%zu pmask=%x\n",adapter.Started,context.Object.Magic,(void*)device.Adapter,(void*)&adapter.ObjectAdapter,args.Flags.Value,args.Flags.Paging,args.Flags.Resubmission,sizeof(ADMISSION_PAGING_RECORD),sizeof(ADMISSION_PAGING_MARKER),context.Object.Flags);
 NTSTATUS first=AdmissionGpuvaG3SubmitVirtualPaging(&adapter,&context,&args);
 fprintf(stderr,"first=%08x branch=%u queue=%u bytes=%zu dma=%u\n",(unsigned)first,adapter.LastBranch,adapter.QueueCalls,sizeof(records),args.DmaBufferSize);
 assert(first==STATUS_SUCCESS);
 assert(adapter.QueueCalls==1 && adapter.LastQueueFlags.Paging && adapter.LastQueueFlags.Resubmission);
 assert(AppleAgxSchedulerLastSubmittedFence(&adapter.Scheduler,0,0)==0x1d3fu);
 args.Flags.Value=1u;args.SubmissionFenceId=0x1d40u;
 assert(AdmissionGpuvaG3SubmitVirtualPaging(&adapter,&context,&args)==STATUS_SUCCESS);
 APPLE_AGX_PREEMPTION preempt={0};
 assert(AppleAgxSchedulerBeginBoundaryPreemption(&adapter.Scheduler,0,0,0x1d50u,0x1d40u,0u));
 assert(AppleAgxSchedulerClaimBoundaryPreemption(&adapter.Scheduler,&preempt));
 assert(preempt.LastCompletedFence==0x1d3eu);
 assert(AppleAgxSchedulerCommitBoundaryPreemption(&adapter.Scheduler,0x1d50u));
 args.Flags.Value=0x81u;args.SubmissionFenceId=0x1d3fu;
 assert(AdmissionGpuvaG3SubmitVirtualPaging(&adapter,&context,&args)==STATUS_SUCCESS);
 assert(AppleAgxSchedulerQueuedFence(&adapter.Scheduler,0,0)==0x1d3fu);
 args.Flags.Value=0x101u;args.SubmissionFenceId=0x1d41u;
 assert(AdmissionGpuvaG3SubmitVirtualPaging(&adapter,&context,&args)==STATUS_INVALID_PARAMETER);
 puts("EXP911 paging flags81 actual KMD function: admitted exact four-record successor, reserved bits rejected");return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='g3-paging-resubmit-') as tmp:
            tmp = Path(tmp)
            (tmp/'paging.inc').write_text(source)
            src = tmp/'replay.c';src.write_text(fixture)
            exe = tmp/'replay'
            result = subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra',
                '-Werror','-fsanitize=address,undefined','-I',str(tmp),'-I',str(RENDER/'include'),
                '-I',str(SHARED/'include'),str(src),str(RENDER/'src/render_paging.c'),
                str(SHARED/'src/apple_agx_scheduler.c'),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            run = subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)


if __name__ == '__main__':
    unittest.main()
