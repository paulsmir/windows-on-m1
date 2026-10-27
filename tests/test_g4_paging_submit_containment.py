"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
RC59: four saved GravityMark dumps (EXP1969/1976/1978/1980) stop in
dxgmms2!VidSchiSubmitPagingCommand -> VidSchiSendToExecutionQueue with
0x119/2: the KMD refused a well-formed system paging SubmitCommandVirtual
(one VirtualFill/Discard record) right after a firmware Timeout. dxgmms2
submits paging with allow-failure clear, so the refusal itself is the
bugcheck. The only refusal left for such a packet is AdmissionCpuQueueSubmit
returning STATUS_DEVICE_BUSY (EXP1969's P2 shows it before RC43 renamed it
INVALID_PARAMETER): a frozen scheduler (SchedulerFaulted) or an unclaimed
boundary preemption (AppleAgxSchedulerQueueFence admission block).

The test runs the real AdmissionCpuQueueSubmit, AdmissionDispatchQueuedWork,
AdmissionPagingQuiescent, the real paging-record validator and the real
shared scheduler. It fails if a valid paging packet is refused in either
state, if a held packet is dispatched while the scheduler is faulted, if a
packet admitted during a preemption is stored/executed instead of being
resubmittable after the DMA_PREEMPTED boundary or an engine reset, if present
work stops being a fallible refusal, or if the EXP1980 dumped record stops
being a valid record (which would move the attribution).
"""
from pathlib import Path
import os
import re
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
SHARED = ROOT / 'drivers/apple-agx/shared'

# EXP1980 rec1980h/102626-14046-01.dmp, data block at the 0x119 P3 argument
# (DXGKARG_SUBMITCOMMANDVIRTUAL.pDmaBufferPrivateData = ffffb987a5cffa00):
# the 0x80-byte ADMISSION_PAGING_RECORD of fence 0xbf2f.
EXP1980_RECORD = bytes.fromhex(
    '5241 4d50 0100 0000 8000 0000 0000 0000'
    '0000 0000 0000 0000 0000 0000 0000 0000'
    '0000 0000 0000 0000 0000 0000 0000 0000'
    '0000 0000 0000 0000 0000 0000 0000 0000'
    '0000 0000 0000 0000 0100 0000 0000 0000'
    '0300 0000 0000 0000 0000 0000 0000 0000'
    '0000 48b9 0d00 0000 0000 0800 0000 0000'
    '0000 0000 0000 0000 fc2d 0000 0000 0000'.replace(' ', ''))

SHIM = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "apple_agx_scheduler.h"
#include "render_submission.h"
#include "render_paging.h"
#include "render_present.h"
#define _Use_decl_annotations_
#define ADMISSION_MAX_PAGING_RECORDS 64u
#define ADMISSION_PRESENT_BLT_DMA_MAX 4096u
#define ADMISSION_CPU_PACKET_PAGING 1u
#define ADMISSION_CPU_PACKET_PRESENT 2u
#define ADMISSION_CPU_PACKET_NOP 3u
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_PENDING 0x103
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000d)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011)
#define RtlCopyMemory memcpy
typedef void VOID;
typedef uint32_t UINT, ULONG;
typedef int32_t LONG, NTSTATUS;
typedef long long LONGLONG, LONG64;
typedef unsigned long long ULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef unsigned char UCHAR, BOOLEAN;
typedef int KIRQL;
typedef union {LONGLONG QuadPart;} LARGE_INTEGER;
typedef struct _ADMISSION_RENDER_CONTEXT {struct {unsigned FenceOutstanding;} Object;} ADMISSION_RENDER_CONTEXT;
/*PACKET*/
typedef enum {AdmissionG4PendingFree, AdmissionG4PendingFilling, AdmissionG4PendingQueued} ADMISSION_G4_PENDING_STATE;
typedef struct {ADMISSION_G4_PENDING_STATE State; ULONG Fence;} ADMISSION_G4_PENDING;
typedef struct {volatile LONG64 QueueToDispatch[3];} ADMISSION_PAGING_PROFILE;
typedef struct _ADMISSION_CONTEXT {
 int Started, PagingLock, SchedulerLock, PagingIdle;
 volatile LONG SchedulerInitialized, SchedulerFaulted, PagingStopping, PagingPending;
 volatile LONG PagingWorkersActive, PagingRecordsUnsubmitted;
 void *PagingWorkItem;
 ULONG CpuQueueHead, CpuQueueCount, DispatchedFence, PresentCopyBytes, PagingRecordCount;
 ULONG PagingFence, G4PendingCount, G4PendingHead;
 NTSTATUS PagingCompletionStatus;
 LONGLONG PagingDispatchQpc;
 ADMISSION_RENDER_CONTEXT *PresentCopyContext;
 ADMISSION_PAGING_PROFILE PagingProfile;
 APPLE_AGX_SCHEDULER Scheduler;
 ADMISSION_RENDER_PACKET RenderPacket;
 ADMISSION_G4_PENDING G4Pending[2];
 ADMISSION_CPU_PACKET CpuQueue[APPLE_AGX_SCHEDULER_QUEUE_CAPACITY];
 UCHAR PresentCopyCommand[4096];
 ADMISSION_PAGING_RECORD PagingRecords[64];
} ADMISSION_CONTEXT;
typedef struct {void *hContext; unsigned SubmissionFenceId; struct {unsigned Resubmission:1;} Flags;} DXGKARG_SUBMITCOMMAND;
static int lock_depth, cpu_launches;
static LONG InterlockedCompareExchange(volatile LONG *p, LONG value, LONG expected){LONG old=*p;if(old==expected)*p=value;return old;}
static LONG InterlockedExchange(volatile LONG *p, LONG value){LONG old=*p;*p=value;return old;}
static LONG InterlockedExchangeAdd(volatile LONG *p, LONG value){LONG old=*p;*p+=value;return old;}
static void KeAcquireSpinLock(int *p, KIRQL *irql){(void)p;*irql=2;++lock_depth;}
static void KeReleaseSpinLock(int *p, KIRQL irql){(void)p;(void)irql;--lock_depth;}
static void KeAcquireSpinLockAtDpcLevel(int *p){(void)p;++lock_depth;}
static void KeReleaseSpinLockFromDpcLevel(int *p){(void)p;--lock_depth;}
static void KeClearEvent(int *p){*p=0;}
static LARGE_INTEGER KeQueryPerformanceCounter(void *f){LARGE_INTEGER v={1};(void)f;return v;}
static void AdmissionPagingProfileAdd(volatile LONG64 *t, LONGLONG ticks){(void)t;(void)ticks;}
static void AdmissionPagingQueueActive(ADMISSION_CONTEXT *c){assert(!lock_depth&&c->PagingPending);++cpu_launches;}
static BOOLEAN AdmissionPlatformRuntimeSubmit(ADMISSION_CONTEXT *c){(void)c;assert(0);return FALSE;}
static BOOLEAN AdmissionPlatformRuntimeReady(ADMISSION_CONTEXT *c){(void)c;return FALSE;}
void AdmissionDispatchQueuedWork(ADMISSION_CONTEXT *Context);
'''

CASES = r'''
static ADMISSION_CONTEXT *fresh(void){
 ADMISSION_CONTEXT *c=calloc(1,sizeof(*c));assert(c);
 c->Started=c->SchedulerInitialized=1;c->PagingWorkItem=c;c->PagingIdle=1;
 AppleAgxSchedulerInitialize(&c->Scheduler);AdmissionRenderPacketInitialize(&c->RenderPacket);
 return c;
}
static NTSTATUS paging(ADMISSION_CONTEXT *c,unsigned fence,unsigned resubmission){
 DXGKARG_SUBMITCOMMAND a={NULL,fence,{0}};a.Flags.Resubmission=resubmission;
 NTSTATUS s=AdmissionCpuQueueSubmit(c,&a,ADMISSION_CPU_PACKET_PAGING,record,sizeof(record));
 assert(!lock_depth);return s;
}
static void complete_cpu(ADMISSION_CONTEXT *c,unsigned fence){
 assert(c->PagingPending&&c->PagingFence==fence&&c->DispatchedFence==fence);
 assert(AppleAgxSchedulerCompleteActiveFence(&c->Scheduler,0,0,fence));
 c->PagingPending=0;c->DispatchedFence=0;
}
int main(void){
 /* The dumped packet itself is valid: refusal must come from the queue. */
 assert(sizeof(record)==sizeof(ADMISSION_PAGING_RECORD));
 assert(AdmissionPagingRecordsValid((const ADMISSION_PAGING_RECORD *)record,1,64,sizeof(ADMISSION_PAGING_MARKER)));
 assert(((const ADMISSION_PAGING_RECORD *)record)->Kind==AdmissionPagingVirtualFill);

 /* 1. Firmware-timeout aftermath with a frozen scheduler (EXP996 shape). */
 ADMISSION_CONTEXT *c=fresh();
 assert(AppleAgxSchedulerQueueFence(&c->Scheduler,0,0,0xbf2e));
 assert(AppleAgxSchedulerActivateFence(&c->Scheduler,0,0,0xbf2e));
 assert(AppleAgxSchedulerFaultActiveFence(&c->Scheduler,0,0,0xbf2e));
 c->SchedulerFaulted=0x40000|4303;c->PagingRecordsUnsubmitted=1;
 assert(paging(c,0xbf2f,0)==STATUS_SUCCESS);
 assert(c->CpuQueueCount==1&&c->Scheduler.QueuedFence==0xbf2f&&cpu_launches==0);
 assert(c->PagingRecordsUnsubmitted==0&&c->PagingIdle==0);
 assert(!AdmissionPagingQuiescent(c));
 AdmissionDispatchQueuedWork(c);assert(cpu_launches==0&&c->CpuQueueCount==1);
 { /* Present stays a fallible device refusal while frozen. */
   unsigned char blt[4]={1};DXGKARG_SUBMITCOMMAND p={NULL,0xbf30,{0}};
   assert(AdmissionCpuQueueSubmit(c,&p,ADMISSION_CPU_PACKET_PRESENT,blt,sizeof(blt))==STATUS_DEVICE_BUSY);
   assert(c->CpuQueueCount==1&&c->Scheduler.LastSubmittedFence==0xbf2f); }
 /* TDR engine reset keeps the held fence resubmittable with its old ID. */
 { unsigned aborted=0;assert(AppleAgxSchedulerResetEngine(&c->Scheduler,0,0,&aborted));
   assert(aborted==0xbf2e);c->CpuQueueHead=c->CpuQueueCount=0;c->SchedulerFaulted=0;
   assert(paging(c,0xbf2f,1)==STATUS_SUCCESS);
   assert(c->CpuQueueCount==0&&cpu_launches==1);complete_cpu(c,0xbf2f);
   AdmissionDispatchQueuedWork(c);assert(cpu_launches==1); }
 /* A frozen scheduler still refuses a fence that is not newer. */
 c->SchedulerFaulted=1;assert(paging(c,0xbf2f,0)==STATUS_DEVICE_BUSY);
 c->PagingStopping=1;c->SchedulerFaulted=0;assert(paging(c,0xbf31,0)==STATUS_DEVICE_BUSY);
 free(c);

 /* 2. An unclaimed boundary preemption: the active job ended (dropped)
  * but DMA_PREEMPTED is not yet reported when the paging packet arrives. */
 cpu_launches=0;c=fresh();
 assert(AppleAgxSchedulerQueueFence(&c->Scheduler,0,0,0xb499));
 assert(AppleAgxSchedulerActivateFence(&c->Scheduler,0,0,0xb499));
 assert(AppleAgxSchedulerBeginBoundaryPreemption(&c->Scheduler,0,0,0x300,0xb499,0xb499));
 assert(AppleAgxSchedulerPreemptionPhase(&c->Scheduler)==AppleAgxPreemptionWaitCurrentBoundary);
 c->PagingRecordsUnsubmitted=1;
 assert(paging(c,0xb49a,0)==STATUS_SUCCESS);   /* while the job still runs */
 assert(c->CpuQueueCount==0&&c->Scheduler.QueueCount==0&&cpu_launches==0);
 assert(c->Scheduler.PreemptedFenceCount==1&&c->Scheduler.PreemptedFenceQueue[0]==0xb49a);
 assert(c->Scheduler.LastSubmittedFence==0xb49a);
 assert(c->PagingRecordsUnsubmitted==1&&!AdmissionPagingQuiescent(c));
 assert(paging(c,0xb49a,1)==STATUS_SUCCESS&&c->Scheduler.PreemptedFenceCount==1);
 assert(AppleAgxSchedulerFaultActiveFence(&c->Scheduler,0,0,0xb499));
 assert(AppleAgxSchedulerObserveBoundaryCompletion(&c->Scheduler,0,0,0xb499));
 assert(AppleAgxSchedulerPreemptionPhase(&c->Scheduler)==AppleAgxPreemptionReadyToNotify);
 assert(paging(c,0xb49b,0)==STATUS_SUCCESS);   /* before the notification */
 assert(paging(c,0xb49b,0)==STATUS_DEVICE_BUSY); /* never twice */
 assert(paging(c,0xb490,0)==STATUS_DEVICE_BUSY); /* never an old fence */
 assert(c->Scheduler.PreemptedFenceCount==2&&c->CpuQueueCount==0);
 { APPLE_AGX_PREEMPTION p;assert(AppleAgxSchedulerClaimBoundaryPreemption(&c->Scheduler,&p));
   assert(p.PreemptionFence==0x300&&p.LastCompletedFence==0xb499);
   assert(AppleAgxSchedulerCommitBoundaryPreemption(&c->Scheduler,0x300)); }
 /* Windows resubmits everything after LastCompletedFence, in order. */
 assert(paging(c,0xb49a,1)==STATUS_SUCCESS&&c->PagingRecordsUnsubmitted==0);
 assert(cpu_launches==1);complete_cpu(c,0xb49a);
 assert(paging(c,0xb49b,1)==STATUS_SUCCESS);
 AdmissionDispatchQueuedWork(c);assert(cpu_launches==2);complete_cpu(c,0xb49b);
 assert(c->Scheduler.PreemptedFenceCount==0&&c->Scheduler.CompletedFence==0xb49b);
 /* Outside a pending preemption the admission rule is unchanged: an
  * unknown resubmission falls back to ordinary in-order queueing. */
 assert(paging(c,0xb49c,1)==STATUS_SUCCESS&&cpu_launches==3);complete_cpu(c,0xb49c);
 free(c);

 /* 3. Without a reset, a preempted-admitted fence survives an engine reset. */
 cpu_launches=0;c=fresh();
 assert(AppleAgxSchedulerQueueFence(&c->Scheduler,0,0,10));
 assert(AppleAgxSchedulerActivateFence(&c->Scheduler,0,0,10));
 assert(AppleAgxSchedulerBeginBoundaryPreemption(&c->Scheduler,0,0,0x400,10,10));
 assert(paging(c,11,0)==STATUS_SUCCESS);
 { unsigned aborted=0;assert(AppleAgxSchedulerResetEngine(&c->Scheduler,0,0,&aborted));
   assert(aborted==10&&c->Scheduler.PreemptedFenceCount==1&&c->Scheduler.PreemptedFenceQueue[0]==11); }
 assert(paging(c,11,1)==STATUS_SUCCESS&&cpu_launches==1);
 free(c);
 return 0;
}
'''


def function(source, name):
    match = re.search(r'_Use_decl_annotations_ (?:NTSTATUS|BOOLEAN|VOID|void) ' + name +
                      r'\(.*?^}', source, re.S | re.M)
    if match is None:
        raise AssertionError(name)
    return match.group(0)


def build_and_run(queue_source):
    header = (RENDER / 'include/render_admission.h').read_text()
    packet = re.search(r'typedef struct _ADMISSION_CPU_PACKET.*?} ADMISSION_CPU_PACKET;',
                       header, re.S).group(0)
    record = ','.join('0x%02x' % b for b in EXP1980_RECORD)
    body = (SHIM.replace('/*PACKET*/', packet) +
            'static const unsigned char record[] = {' + record + '};\n' +
            function(queue_source, 'AdmissionCpuQueueSubmit') + '\n' +
            function(queue_source, 'AdmissionDispatchQueuedWork') + '\n' +
            function(queue_source, 'AdmissionPagingQuiescent') + '\n' + CASES)
    with tempfile.TemporaryDirectory() as tmp:
        program = Path(tmp) / 'containment.c'
        binary = Path(tmp) / 'containment'
        program.write_text(body)
        compiled = subprocess.run(
            [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
             '-Wno-unused-function', '-fsanitize=address,undefined',
             '-I', str(RENDER / 'include'), '-I', str(SHARED / 'include'),
             str(program), str(SHARED / 'src/apple_agx_scheduler.c'),
             str(RENDER / 'src/render_submission.c'), str(RENDER / 'src/render_paging.c'),
             '-o', str(binary)], capture_output=True, text=True)
        if compiled.returncode:
            return compiled.returncode, compiled.stderr
        ran = subprocess.run([str(binary)], capture_output=True, text=True)
        return ran.returncode, ran.stdout + ran.stderr


class PagingSubmitContainment(unittest.TestCase):
    def test_dumped_record_matches_the_kmd_record_layout(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The attribution
        reads the dumped private data as one VirtualFill record (magic RAMP,
        512 KiB, pool segment 3); a layout change would invalidate it."""
        magic, version, size, reserved = struct.unpack_from('<IIII', EXP1980_RECORD)
        self.assertEqual((magic, version, size, reserved), (0x504d4152, 1, 0x80, 0))
        kind, source, destination = struct.unpack_from('<III', EXP1980_RECORD, 0x48)
        self.assertEqual((kind, source, destination), (1, 0, 3))
        ipa, nbytes = struct.unpack_from('<QI', EXP1980_RECORD, 0x60)
        self.assertEqual((ipa, nbytes), (0xdb9480000, 0x80000))

    def test_valid_paging_packet_is_never_refused_by_scheduler_state(self):
        queue = (RENDER / 'src/work_queue_windows.c').read_text()
        code, output = build_and_run(queue)
        self.assertEqual(code, 0, output)


if __name__ == '__main__':
    unittest.main()
