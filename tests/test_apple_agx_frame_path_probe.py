import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"


class FramePathProbeTests(unittest.TestCase):
    def test_context_fence_present_and_tdr_snapshot(self):
        source = (RENDER / "src/dwm_ddi_probe_windows.c").read_text()
        source = source.replace('#include "render_admission.h"', '')
        shim = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "render_qualification.h"
#define _Use_decl_annotations_
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define APPLE_AGX_EXP907_FRAME_RECEIPT 1
#define APPLE_AGX_VERSION_BUILD 907u
#define STATUS_SUCCESS 0
#define STATUS_PENDING 0x103
#define STATUS_INVALID_PARAMETER (-1)
#define STATUS_DEVICE_BUSY (-2)
#define STATUS_DEVICE_HARDWARE_ERROR (-3)
#define NT_SUCCESS(v) ((v)>=0)
#define MAXULONG 0xffffffffu
#define PASSIVE_LEVEL 0
#define TRUE 1
#define FALSE 0
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define RtlCopyMemory(p,s,n) memcpy((p),(s),(n))
#define KeMemoryBarrier() __sync_synchronize()
typedef int NTSTATUS;
typedef long LONG;
typedef unsigned int ULONG;
typedef unsigned int UINT;
typedef unsigned long long ULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef int BOOLEAN;
typedef int KIRQL;
typedef void VOID;
typedef void *PVOID;
typedef struct _ADMISSION_CONTEXT {
  int Started;
  unsigned int Win32BootGeneration;
  ADMISSION_DWM_DDI_PROBE DwmDdiProbe;
  ADMISSION_DWM_FRAME_PROBE DwmFrameProbe;
  int SchedulerLock;
  struct { unsigned int CompletedFence, LastSubmittedFence, ActiveFence; } Scheduler;
  struct { int State; struct { unsigned long long ContextToken, Fence; } Description; } RenderPacket;
  volatile LONG PagingPending;
} ADMISSION_CONTEXT;
static LONG InterlockedIncrement(volatile LONG *value){return __atomic_add_fetch(value,1,__ATOMIC_SEQ_CST);}
static LONG InterlockedCompareExchange(volatile LONG *value,LONG replacement,LONG comparison){
  __atomic_compare_exchange_n(value,&comparison,replacement,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
  return comparison;
}
static LONG InterlockedExchange(volatile LONG *value,LONG replacement){return __atomic_exchange_n(value,replacement,__ATOMIC_SEQ_CST);}
static int KeGetCurrentIrql(void){return PASSIVE_LEVEL;}
static void KeAcquireSpinLock(int *lock,KIRQL *oldIrql){(void)lock;*oldIrql=0;}
static void KeReleaseSpinLock(int *lock,KIRQL oldIrql){(void)lock;(void)oldIrql;}
static int AdmissionRenderPacketState(const void *packet){return ((const struct {int State;} *)packet)->State;}
static unsigned int AppleAgxSchedulerActiveFence(const void *scheduler,unsigned int node,unsigned int engine){
  (void)node;(void)engine;return ((const struct {unsigned int CompletedFence,LastSubmittedFence,ActiveFence;} *)scheduler)->ActiveFence;
}
'''
        replay = r'''
int main(void) {
  ADMISSION_CONTEXT adapter={0};
  ADMISSION_DWM_FRAME_PROBE query={0};
  void *context=(void *)(uintptr_t)0x12340000u;
  adapter.Started=1;adapter.Win32BootGeneration=42;
  assert(AdmissionDwmFrameArmWindows(&adapter,context,1244,3,0xc0000a80,0x7d0000));
  AdmissionDwmFrameRecordQuery(&adapter,context,53,STATUS_INVALID_PARAMETER,15);
  AdmissionDwmFrameRecordSubmit(&adapter,context,0x900000,77,0,STATUS_SUCCESS,FALSE);
  AdmissionDwmFrameRecordCompletion(&adapter,context,77);
  AdmissionDwmFrameRecordPresent(&adapter,context,STATUS_PENDING,TRUE);
  AdmissionDwmFrameRecordBlt(&adapter,context,0x10,0x20,0x7d0000,0xb0000);
  AdmissionDwmFrameRecordCopy(&adapter,context,0x8e0110000ULL,4096,STATUS_SUCCESS);
  AdmissionDwmFrameRecordPresent(&adapter,context,STATUS_SUCCESS,FALSE);
  AdmissionDwmFrameRecordSubmit(&adapter,(void *)(uintptr_t)0x56780000u,0,99,0,STATUS_INVALID_PARAMETER,FALSE);
  adapter.RenderPacket.State=2;adapter.RenderPacket.Description.ContextToken=(unsigned long long)(uintptr_t)context;
  adapter.RenderPacket.Description.Fence=78;
  adapter.Scheduler.CompletedFence=77;adapter.Scheduler.LastSubmittedFence=78;adapter.Scheduler.ActiveFence=78;
  AdmissionDwmFrameRecordTdr(&adapter,STATUS_PENDING,FALSE);
  AdmissionDwmFrameRecordPrivateReset(&adapter,FALSE);
  AdmissionDwmFrameRecordTdr(&adapter,STATUS_DEVICE_HARDWARE_ERROR,TRUE);
  query.Magic=ADMISSION_DWM_FRAME_PROBE_MAGIC;query.Version=ADMISSION_DWM_FRAME_VERSION;query.Bytes=sizeof(query);
  assert(AdmissionDwmFrameProbeQueryWindows(&adapter,&query)==STATUS_SUCCESS);
  assert(query.BootGeneration==42 && query.CandidateBuild==907 && query.ArmedCount==1);
  assert(query.Entries[0].Context==(unsigned long long)(uintptr_t)context);
  assert(query.Entries[0].OsProcessId==1244 && query.Entries[0].CanonicalGpuVa==0x7d0000);
  assert(query.Entries[0].QueryCount==1 && query.Entries[0].QueryPredicate==53 && query.Entries[0].QueryResidentPages==15);
  assert(query.Entries[0].SubmitCount==1 && query.Entries[0].SubmittedFence==77);
  assert(query.Entries[0].CompleteCount==1 && query.Entries[0].CompletedFence==77);
  assert(query.Entries[0].PresentCount==1 && query.Entries[0].DestinationGuestIpa==0x8e0110000ULL);
  assert(query.Entries[0].PresentStatus==STATUS_SUCCESS && query.Entries[0].CopiedBytes==4096);
  assert(query.Tdr.Captured==1 && query.Tdr.PacketContext==(unsigned long long)(uintptr_t)context);
  assert(query.Tdr.PacketFence==78 && query.Tdr.SchedulerCompletedFence==77);
  assert(query.Tdr.PrivateResetStatus==(unsigned int)STATUS_DEVICE_HARDWARE_ERROR);
  assert(query.Tdr.ResetStatus==(unsigned int)STATUS_DEVICE_HARDWARE_ERROR);
  query.Version++;
  assert(AdmissionDwmFrameProbeQueryWindows(&adapter,&query)==STATUS_INVALID_PARAMETER);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source_path = Path(directory) / "probe.c"
            executable = Path(directory) / "probe"
            source_path.write_text(shim + source + replay)
            compile_result = subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I", str(RENDER / "include"),
                str(source_path), "-o", str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            subprocess.run([str(executable)], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
