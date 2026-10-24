"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1932's rolling omission history lost the faulting VA. Capture must retain
the first valid fault's exact current four-page group, not another process,
fence, VM slot, neighbouring group, or a later fault. Busy/uncertain state is
unknown, never evidence of an absent leaf. Exercise production capture bodies
with the real graph lookup and decode the actual C wire layout.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from g3_vidmm_replay import body
from test_wom1_gpu_fault_receipt import DECODER

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
SHARED = ROOT / 'drivers/apple-agx/shared'

PROGRAM = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "render_gpu_fault.h"
#include "apple_agx_gpuva_g3_graph.h"
#include "apple_agx_gpuva_g3_translation.h"
typedef void VOID;
typedef unsigned ULONG, UINT;
typedef unsigned long long ULONGLONG;
typedef uintptr_t ULONG_PTR;
#define RtlZeroMemory(p,n) memset(p,0,n)
#define PASSIVE_LEVEL 0
static int irql, locked, busy;
static int KeGetCurrentIrql(void) { return irql; }
static int ExTryToAcquireFastMutex(int *m) {
  (void)m; assert(!locked); if(busy) return 0; locked=1; return 1;
}
static void ExReleaseFastMutex(int *m) { (void)m; assert(locked); locked=0; }
typedef struct _ADMISSION_RENDER_CONTEXT {
  struct _ADMISSION_RENDER_CONTEXT *GpuvaG3NextContext;
} ADMISSION_RENDER_CONTEXT;
typedef struct _ADMISSION_G3_TABLE_SHADOW {
  struct _ADMISSION_G3_TABLE_SHADOW *NextBroker;
  ULONGLONG BrokerIpa;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *LogicalPtes;
} ADMISSION_G3_TABLE_SHADOW;
typedef struct {
  APPLE_AGX_GPUVA_G3_GRAPH Graph;
  ADMISSION_RENDER_CONTEXT *Contexts;
  ADMISSION_G3_TABLE_SHADOW *TableShadowBrokerBuckets[256];
  ULONG OsProcessId, CpuOnlyMappings;
} ADMISSION_G3_PROCESS;
typedef struct { int Lock; ADMISSION_G3_PROCESS *ActiveProcess; ULONG ActiveFence; } ADMISSION_G3_STATE;
typedef struct { ADMISSION_G3_STATE *GpuvaG3State; } ADMISSION_CONTEXT;
@@FUNCTION@@
static APPLE_AGX_GPUVA_G3_NODE root, middle, table, re, me, leaf;
static APPLE_AGX_GPUVA_G3_NODE *rs[8], *ms[2048], *ls[2048];
static APPLE_AGX_GPUVA_G3_LOGICAL_PTE ptes[8192];
@@RECOVERY@@
int main(int argc, char **argv) {
  ADMISSION_G3_PROCESS p={0}; ADMISSION_RENDER_CONTEXT ctx={0};
  ADMISSION_G3_STATE state={0,&p,71}; ADMISSION_CONTEXT adapter={&state};
  ADMISSION_G3_TABLE_SHADOW shadow={0};
  ADMISSION_GPU_FAULT_EVENT e={0}; ADMISSION_GPU_FAULT_MAPPING r={0};
  const ULONGLONG va=0x1112681040ULL, tableIpa=0x900004000ULL;
  unsigned index=(va>>12)&8191u, group=index&~3u;
  assert(argc==2);
  root.Slots=rs; middle.Slots=ms; table.Slots=ls;
  re.ChildTable=&middle; me.ChildTable=&table; me.AuxIpa=tableIpa;
  rs[(va>>36)&7u]=&re; ms[(va>>25)&2047u]=&me;
  p.Graph.Created=1; p.Graph.RootTable=&root; p.Graph.RootIpa=0x900000000ULL;
  p.Graph.Tables=&table; table.Ipa=tableIpa; table.Level=2;
  p.Graph.ProcessId=9; p.Graph.ProcessGeneration=3;
  p.Graph.MappingGeneration=1234; p.Graph.Slot=1; p.Graph.JobInFlight=1;
  p.Graph.LeaseToken=7; p.Contexts=&ctx; p.OsProcessId=4104;
  shadow.BrokerIpa=tableIpa; shadow.LogicalPtes=ptes;
  p.TableShadowBrokerBuckets[((tableIpa>>14)^(tableIpa>>22)^(tableIpa>>30))&255]=&shadow;
  for(unsigned i=0;i<4;++i) {
    ptes[group+i].GuestIpa=0xa00000000ULL+i*0x2000ULL; /* scattered */
    ptes[group+i].Flags=3; ptes[group+i].Allocation=0xabc000ULL+i;
    ptes[group+i].AllocationOffset=0x10000ULL+i*4096;
  }
  e.Fence=71; e.ContextToken=(ULONG_PTR)&ctx;
  e.Flags=ADMISSION_GPU_FAULT_JOB_KNOWN|ADMISSION_GPU_FAULT_INFO_VALID|ADMISSION_GPU_FAULT_INFO_READ;
  e.FaultInfo=((va>>6)<<30)|(1ULL<<17)|0x11;
  /* Invalid reads do not consume the first-valid capture. */
  e.Flags=0; AdmissionGpuvaG3CaptureFaultMapping(&adapter,&e,&r); assert(!r.Version);
  e.Flags=14; busy=1; AdmissionGpuvaG3CaptureFaultMapping(&adapter,&e,&r);
  assert(r.Status==AdmissionGpuFaultMappingBusy); busy=0; memset(&r,0,sizeof(r));
  irql=1; AdmissionGpuvaG3CaptureFaultMapping(&adapter,&e,&r);
  assert(r.Status==AdmissionGpuFaultMappingUnavailable); irql=0; memset(&r,0,sizeof(r));
  ++e.Fence; AdmissionGpuvaG3CaptureFaultMapping(&adapter,&e,&r);
  assert(r.Status==AdmissionGpuFaultMappingIdentity); --e.Fence; memset(&r,0,sizeof(r));
  e.ContextToken=1; AdmissionGpuvaG3CaptureFaultMapping(&adapter,&e,&r);
  assert(r.Status==AdmissionGpuFaultMappingIdentity); e.ContextToken=(ULONG_PTR)&ctx;
  memset(&r,0,sizeof(r)); p.Graph.Slot=2;
  AdmissionGpuvaG3CaptureFaultMapping(&adapter,&e,&r);
  assert(r.Status==AdmissionGpuFaultMappingVm); p.Graph.Slot=1;
  memset(&r,0,sizeof(r)); p.Graph.Uncertain=1;
  AdmissionGpuvaG3CaptureFaultMapping(&adapter,&e,&r);
  assert(r.Status==AdmissionGpuFaultMappingUncertain); p.Graph.Uncertain=0;
  memset(&r,0,sizeof(r)); ms[(va>>25)&2047u]=NULL;
  AdmissionGpuvaG3CaptureFaultMapping(&adapter,&e,&r);
  assert(r.Status==AdmissionGpuFaultMappingNoTable); ms[(va>>25)&2047u]=&me;
  memset(&r,0,sizeof(r)); shadow.LogicalPtes=NULL;
  AdmissionGpuvaG3CaptureFaultMapping(&adapter,&e,&r);
  assert(r.Status==AdmissionGpuFaultMappingNoShadow); shadow.LogicalPtes=ptes;
  memset(&r,0,sizeof(r)); AdmissionGpuvaG3CaptureFaultMapping(&adapter,&e,&r);
  assert(r.Status==AdmissionGpuFaultMappingCaptured && !r.NativePresent);
  assert(r.FaultVa==va && r.Ptes[1].Allocation==0xabc001 && !locked);
  assert(r.Ptes[3].GuestIpa==0xa00006000ULL && r.MappingGeneration==1234);
  FILE *f=fopen(argv[1],"wb"); assert(f && fwrite(&r,sizeof(r),1,f)==1); fclose(f);
  ADMISSION_GPU_FAULT_MAPPING saved=r;
  ptes[group+1].GuestIpa=0; ++e.Fence;
  AdmissionGpuvaG3CaptureFaultMapping(&adapter,&e,&r);
  assert(!memcmp(&saved,&r,sizeof(r))); --e.Fence;
  /* Complete mapping: native and logical are independent observations. */
  leaf.AuxIpa=0xb00000000ULL; leaf.Writable=1; leaf.Kind=AppleAgxGpuvaG3SystemBacking;
  ls[(va>>14)&2047u]=&leaf;
  for(unsigned i=0;i<4;++i) ptes[group+i].GuestIpa=leaf.AuxIpa+i*4096;
  memset(&r,0,sizeof(r)); AdmissionGpuvaG3CaptureFaultMapping(&adapter,&e,&r);
  assert(r.NativePresent && r.NativeIpa==0xb00000000ULL && r.NativeWritable==1);
  assert(r.Ptes[1].GuestIpa==0xb00001000ULL);
  assert(p.Graph.MappingGeneration==1234 && p.Graph.LeaseToken==7);
  /* Actual recovery callback must capture before its firmware resume write. */
  ADMISSION_PLATFORM_RUNTIME runtime={0}; unsigned status[16]={0};
  runtime.Adapter=&adapter; runtime.Provider.QueueProvider.PendingFence=71;
  runtime.Initdata.DataObjects[0].CpuAddress=status; capture_event=e;
  status[8]=1; assert(AdmissionTransportRecover(&runtime,71,4));
  assert(status[8]==0 && status[12]==1 && runtime.GpuFaultMapping.Status==7);
  assert(runtime.GpuFaultUnpublished && runtime.FirmwareRecoveries==1);
  /* Non-halted recovery still preserves and publishes the captured evidence. */
  memset(&runtime.GpuFaultMapping,0,sizeof(runtime.GpuFaultMapping)); status[12]=0;
  assert(!AdmissionTransportRecover(&runtime,71,4));
  assert(runtime.GpuFaultMapping.Status==7 && status[12]==0);
  puts("PASS"); return 0;
}
'''

RECOVERY = r'''
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define APPLE_AGX_BACKEND_FALSE 0
#define APPLE_AGX_BACKEND_TRUE 1
#define TRUE 1
#define AppleAgxInitdataMemoryFirmwareStatus 0
typedef unsigned APPLE_AGX_BACKEND_U32;
typedef int APPLE_AGX_BACKEND_BOOL;
typedef struct {
  ADMISSION_CONTEXT *Adapter;
  struct { struct { unsigned PendingFence; } QueueProvider; } Provider;
  struct { struct { void *CpuAddress; } DataObjects[1]; } Initdata;
  ADMISSION_GPU_FAULT_RECEIPT GpuFault;
  ADMISSION_GPU_FAULT_MAPPING GpuFaultMapping;
  int GpuFaultUnpublished, FirmwareRecoveries, FirmwareRecoveryUnpublished;
  unsigned LastFirmwareRecoveryKind, LastFirmwareRecoveryFence;
  unsigned LastFirmwareRecoveryMs, MaxFirmwareRecoveryMs;
  ULONGLONG JobSubmitMs;
} ADMISSION_PLATFORM_RUNTIME;
static ADMISSION_GPU_FAULT_EVENT capture_event;
static void AdmissionGpuFaultCapture(ADMISSION_PLATFORM_RUNTIME *r, unsigned f,
    unsigned k, const volatile unsigned *s, ADMISSION_GPU_FAULT_EVENT *e) {
  (void)r; (void)f; (void)k; assert(s[12]==0); *e=capture_event;
}
static ULONGLONG AdmissionPlatformNowMs(void) { static ULONGLONG ms; return ++ms; }
static void KeMemoryBarrier(void) { }
static void KeStallExecutionProcessor(unsigned us) { (void)us; }
static void InterlockedIncrement(int *p) { ++*p; }
@@BODY@@
'''


class FaultMapping(unittest.TestCase):
    def test_current_mapping_and_first_fault_retention(self):
        text = (RENDER/'src/gpuva_g3_windows.c').read_text()
        self.assertTrue('VOID AdmissionGpuvaG3CaptureFaultMapping(' in text,
                      'fault VA cannot be correlated with the current graph')
        source = PROGRAM.replace('@@FUNCTION@@', body(text, 'AdmissionGpuvaG3CaptureFaultMapping'))
        backend = (RENDER/'src/backend_platform_windows.c').read_text()
        source = source.replace('@@RECOVERY@@', RECOVERY.replace('@@BODY@@',
            body(backend, 'AdmissionTransportRecover')))
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp/'capture.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-fsanitize=address,undefined', '-I', str(RENDER/'include'),
                '-I', str(SHARED/'include'), str(tmp/'capture.c'),
                str(SHARED/'src/apple_agx_gpuva_g3_graph.c'),
                str(SHARED/'src/apple_agx_gpuva_broker_v5_client.c'),
                str(RENDER/'src/render_gpu_fault.c'), '-o', str(tmp/'capture')], check=True)
            path = tmp/'Wom1GpuFaultMapping.bin'
            subprocess.run([str(tmp/'capture'), str(path)], check=True)
            data = path.read_bytes()
            result = DECODER.decode_fault_mapping(data)
            self.assertEqual(result['status'], 'captured')
            self.assertEqual(result['fault_va'], '0x1112681040')
            self.assertFalse(result['native_present'])
            self.assertEqual(result['ptes'][1]['allocation'], '0xabc001')
            self.assertEqual(result['ptes'][3]['ipa'], '0xa00006000')
            self.assertEqual(DECODER.read_receipts(path), {'Wom1GpuFaultMapping': data})
            for invalid in (b'', data[:-1], bytes(len(data)), data+b'\0'):
                with self.assertRaises(ValueError): DECODER.decode_fault_mapping(invalid)


if __name__ == '__main__':
    unittest.main()
