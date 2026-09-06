"""Catch loss of internal latch completion when Windows disables VSync reports.

Execute the production start publication, ControlInterrupt and ISR against the
real portable scanout client. Only Windows primitives and broker MMIO are shims.
"""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'


class VsyncOwnershipTests(unittest.TestCase):
    def test_initial_modeset_and_disabled_notifications_still_retire_latches(self):
        source = (ROOT / 'drivers/apple-agx/render-admission/src/scanout_windows.c').read_text()
        def function(name):
            match = re.search(r'(?:static |._Use_decl_annotations_ )?[^\n]*\b' +
                              name + r'\(.*?^}', source, re.S | re.M)
            self.assertIsNotNone(match, name)
            return match.group(0)
        runtime = re.search(r'typedef struct _ADMISSION_SCANOUT_RUNTIME.*?} ADMISSION_SCANOUT_RUNTIME;', source, re.S).group(0)
        start = function('AdmissionScanoutStart')
        publication = '  Context->ScanoutRuntime = runtime;' + start.rsplit('  Context->ScanoutRuntime = runtime;', 1)[1]
        shim = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "apple_agx_fixed_panel.h"
#define _Use_decl_annotations_
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_NOT_READY (-1)
#define STATUS_DEVICE_HARDWARE_ERROR (-2)
#define RtlZeroMemory(p,n) memset(p,0,n)
#define DXGK_INTERRUPT_CRTC_VSYNC 1
typedef int32_t LONG, NTSTATUS;
typedef int64_t LONG64;
typedef uint32_t ULONG;
typedef int BOOLEAN;
typedef unsigned long long APPLE_AGX_U64;
typedef struct { int InterruptType; struct {unsigned VidPnTargetId;
 struct {int64_t QuadPart;} PhysicalAddress;} CrtcVsync; }
 DXGKARGCB_NOTIFY_INTERRUPT_DATA;
typedef struct _ADMISSION_CONTEXT ADMISSION_CONTEXT;
struct _ADMISSION_CONTEXT {
 void *ScanoutRuntime;
 int InterfaceValid;
 struct {void *DeviceHandle;
 void (*DxgkCbNotifyInterrupt)(void *,DXGKARGCB_NOTIFY_INTERRUPT_DATA *);
 int (*DxgkCbQueueDpc)(void *);} Interface;
 volatile LONG LastInterruptStatus,InterruptCount,InterruptAckCount,SchedulerDpcPending;
};
static LONG InterlockedExchange(volatile LONG *p,LONG v){LONG old=*p;*p=v;return old;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG expected){LONG old=*p;if(old==expected)*p=v;return old;}
static LONG InterlockedIncrement(volatile LONG *p){return ++*p;}
static LONG64 InterlockedExchange64(volatile LONG64 *p,LONG64 v){LONG64 old=*p;*p=v;return old;}
static LONG64 InterlockedCompareExchange64(volatile LONG64 *p,LONG64 v,LONG64 expected){LONG64 old=*p;if(old==expected)*p=v;return old;}
static unsigned irq_enable,irq_status,notify_count,dpc_count;
static unsigned long long latch,notified_address;
static APPLE_AGX_SCANOUT_BOOL read32(void *ctx,unsigned offset,unsigned *value){
 (void)ctx; assert(offset==0x414); *value=irq_status; return 1;}
static APPLE_AGX_SCANOUT_BOOL read64(void *ctx,unsigned offset,unsigned long long *value){
 (void)ctx; assert(offset==0x430); *value=latch; return 1;}
static APPLE_AGX_SCANOUT_BOOL write32(void *ctx,unsigned offset,unsigned value){
 (void)ctx; if(offset==0x418)irq_enable=value;
 else {assert(offset==0x414);irq_status &= ~value;} return 1;}
static void notify(void *ctx,DXGKARGCB_NOTIFY_INTERRUPT_DATA *data){
 (void)ctx; assert(data->InterruptType==1 && data->CrtcVsync.VidPnTargetId==0);
 notified_address=(unsigned long long)data->CrtcVsync.PhysicalAddress.QuadPart; ++notify_count;}
static int dpc(void *ctx){(void)ctx;++dpc_count;return 1;}
'''
        harness = r'''
static void pending(ADMISSION_SCANOUT_RUNTIME *r,unsigned long long sequence){
 r->PresentGate=1;r->PendingValid=1;r->PendingSequence=(LONG64)sequence;
 r->PendingPhysicalAddress=0x1500000000LL;
 r->Panel.Scanout.PresentPending=1;r->Panel.Scanout.PendingPresentSequence=sequence;
 latch=sequence;irq_status=1;
}
int main(void){
 ADMISSION_CONTEXT c={0}; ADMISSION_SCANOUT_RUNTIME r={0};
 r.Adapter=&c;r.Panel.Started=1;r.Panel.Scanout.Qualified=1;
 r.Panel.Scanout.AbiVersion=2;r.Panel.Scanout.Capabilities=0x7f;
 r.Panel.Scanout.Io.Read32=read32;r.Panel.Scanout.Io.Read64=read64;
 r.Panel.Scanout.Io.Write32=write32;
 c.InterfaceValid=1;c.Interface.DxgkCbNotifyInterrupt=notify;c.Interface.DxgkCbQueueDpc=dpc;
 assert(finish_start(&c,&r)==0);
 assert(r.IrqEnabled==1 && irq_enable==3); /* EXP503 had zero here. */
 assert(AdmissionScanoutControlInterrupt(&c,FALSE)==0);
 assert(r.IrqEnabled==1 && irq_enable==3);
 pending(&r,2);assert(AdmissionScanoutInterrupt(&c));
 assert(!r.PendingValid && !r.PresentGate && !r.Panel.Scanout.PresentPending);
 assert(!notify_count && !dpc_count && !irq_status && !r.Faulted);
 assert(!AdmissionScanoutInterrupt(&c)); /* no duplicate completion */
 assert(AdmissionScanoutControlInterrupt(&c,TRUE)==0);
 pending(&r,3);assert(AdmissionScanoutInterrupt(&c));
 assert(notify_count==1 && dpc_count==1 && notified_address==0x1500000000ULL);
 assert(!r.PendingValid && !r.PresentGate && !r.Faulted);
 assert(AdmissionScanoutControlInterrupt(&c,FALSE)==0);
 pending(&r,4);assert(AdmissionScanoutInterrupt(&c));
 assert(notify_count==1 && dpc_count==1 && !r.PendingValid && !r.PresentGate);
 /* Stale physical receipt must not notify or retire a new pending request. */
 pending(&r,5);latch=4;assert(AdmissionScanoutInterrupt(&c));
 assert(r.Faulted && r.PendingValid && notify_count==1 && dpc_count==1);
 return 0;
}
'''
        body = (shim + runtime + '\n' + function('AdmissionScanoutGet') +
                '\nstatic NTSTATUS finish_start(ADMISSION_CONTEXT *Context, ADMISSION_SCANOUT_RUNTIME *runtime) {\n' + publication +
                '\n' + function('AdmissionScanoutControlInterrupt') +
                '\n' + function('AdmissionScanoutInterrupt') + harness)
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / 'vsync.c'
            program.write_text(body)
            binary = Path(tmp) / 'vsync'
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-I',str(SHARED/'include'),str(program),
                            str(SHARED/'src/apple_agx_scanout.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
