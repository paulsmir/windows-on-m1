"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1932's rolling omission history lost the faulting VA. Capture must retain
the first valid fault's exact current four-page group, not another process,
fence, VM slot, neighbouring group, or a later fault. Busy/uncertain state is
unknown, never evidence of an absent leaf. EXP1933: another process must not
consume the only snapshot and hide the later valid fault; persist first and
latest separately, including an unknown latest result. Exercise production capture bodies
with the real graph lookup and decode the actual C wire layout.
EXP1935: zero current PTEs must not discard the paging operations that could
explain them. Preserve only same-process, overlapping-group history in newest
order; table-only clears and adjacent subpages matter, unrelated records do not.
Truncation and missing history must remain explicit, including old v1 input.
"""
from pathlib import Path
import os
import re
import base64
import json
import subprocess
import struct
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
#include <wchar.h>
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
@@LEAF_TYPES@@
typedef struct { int Lock; ADMISSION_G3_PROCESS *ActiveProcess; ULONG ActiveFence;
  ADMISSION_G3_LEAF_HISTORY LeafHistory[ADMISSION_G3_LEAF_RING];
  ULONG LeafHistoryNext;
} ADMISSION_G3_STATE;
typedef struct { ADMISSION_G3_STATE *GpuvaG3State; void *PhysicalDeviceObject; } ADMISSION_CONTEXT;
@@FUNCTION@@
static APPLE_AGX_GPUVA_G3_NODE root, middle, table, re, me, leaf;
static APPLE_AGX_GPUVA_G3_NODE *rs[8], *ms[2048], *ls[2048];
static APPLE_AGX_GPUVA_G3_LOGICAL_PTE ptes[8192];
@@RECOVERY@@
int main(int argc, char **argv) {
  ADMISSION_G3_PROCESS p={0}; ADMISSION_RENDER_CONTEXT ctx={0};
  static ADMISSION_G3_STATE state; state.ActiveProcess=&p;state.ActiveFence=71;
  ADMISSION_CONTEXT adapter={&state,0};
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
  @@HISTORY_CASES@@
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
  unsigned char regionC[ADMISSION_REGIONC_PENDING_STAMPS_OFFSET +
                        ADMISSION_REGIONC_PENDING_STAMPS_BYTES] = {0};
  runtime.Initdata.DataObjects[1].CpuAddress=regionC;
  runtime.Initdata.DataObjects[1].Length=sizeof(regionC);
  runtime.Adapter=&adapter; runtime.Provider.QueueProvider.PendingFence=71;
  runtime.Initdata.DataObjects[0].CpuAddress=status; capture_event=e;
  status[8]=1; assert(AdmissionTransportRecover(&runtime,71,4));
  assert(status[8]==0 && status[12]==1 && runtime.GpuFaultMapping.Status==7);
  assert(runtime.GpuFaultUnpublished && runtime.FirmwareRecoveries==1);
  /* Non-halted recovery still preserves and publishes the captured evidence. */
  memset(&runtime.GpuFaultMapping,0,sizeof(runtime.GpuFaultMapping)); status[12]=0;
  assert(!AdmissionTransportRecover(&runtime,71,4));
  assert(runtime.GpuFaultMapping.Status==7 && status[12]==0);
  ADMISSION_GPU_FAULT_MAPPING first_mapping=runtime.GpuFaultMapping;
  /* A second process/fence/VA must not be hidden by the first valid fault. */
  ++state.ActiveFence; ++capture_event.Fence;
  capture_event.FaultInfo+=1ULL<<30; p.OsProcessId=9002;
  runtime.Provider.QueueProvider.PendingFence=capture_event.Fence;
  status[8]=1; status[12]=0;
  assert(AdmissionTransportRecover(&runtime,capture_event.Fence,4));
  assert(!memcmp(&first_mapping,&runtime.GpuFaultMapping,sizeof(first_mapping)));
  assert(runtime.GpuFaultMappingLast.Fence==capture_event.Fence);
  assert(runtime.GpuFaultMappingLast.OsProcessId==9002);
  assert(runtime.GpuFaultMappingLast.FaultVa==va+64);
  assert(runtime.GpuFaultMappingLast.Status==AdmissionGpuFaultMappingCaptured);
  AdmissionPublishFirmwareRecovery(&runtime);
  assert(first_written && last_written && flushed);
  assert(!memcmp(&first_written_value,&first_mapping,sizeof(first_mapping)));
  assert(!memcmp(&last_written_value,&runtime.GpuFaultMappingLast,sizeof(first_mapping)));
  /* No valid register read: preserve both, never erase known evidence. */
  ADMISSION_GPU_FAULT_MAPPING last_mapping=runtime.GpuFaultMappingLast;
  capture_event.Flags=0; status[8]=1; status[12]=0;
  assert(AdmissionTransportRecover(&runtime,capture_event.Fence,4));
  assert(!memcmp(&last_mapping,&runtime.GpuFaultMappingLast,sizeof(last_mapping)));
  /* Unknown latest must replace the old captured result, not misattribute it. */
  capture_event.Flags=14; busy=1; ++capture_event.Fence;
  runtime.Provider.QueueProvider.PendingFence=capture_event.Fence;
  status[8]=1; status[12]=0;
  assert(AdmissionTransportRecover(&runtime,capture_event.Fence,4));
  assert(runtime.GpuFaultMappingLast.Status==AdmissionGpuFaultMappingBusy);
  assert(runtime.GpuFaultMappingLast.Fence==capture_event.Fence);
  assert(runtime.GpuFaultMappingLast.NativeIpa==0 && runtime.GpuFaultMappingLast.OsProcessId==0);
  assert(!memcmp(&first_mapping,&runtime.GpuFaultMapping,sizeof(first_mapping)));
  AdmissionPublishFirmwareRecovery(&runtime);
  assert(first_written==2 && last_written==2 && flushed==2);
  assert(!memcmp(&first_written_value,&first_mapping,sizeof(first_mapping)));
  assert(last_written_value.Status==AdmissionGpuFaultMappingBusy);
  assert(last_written_value.Fence==capture_event.Fence);
  puts("PASS"); return 0;
}
'''


HISTORY_CASES = r"""
#ifdef ADMISSION_GPU_FAULT_HISTORY_COUNT
  /* Exercise real capture, not a test copy of its matcher. No current PTE
   * mutation is needed: provenance is historical and independent of validity. */
  const ULONGLONG base=va&~0x3fffULL;
  ADMISSION_G3_LEAF_HISTORY h={0};
  h.ProcessId=p.Graph.ProcessId;h.TableIpa=tableIpa;h.FirstVa=base;
  h.First=group;h.Count=4;h.ValidCount=4;h.Allocation=0xaaa;
  h.MappingGeneration=1220;h.Qpc=100;h.FirstSegment=3;
  state.LeafHistory[0]=h;state.LeafHistoryNext=1;
  #define CAPTURE() do {memset(&r,0,sizeof(r));AdmissionGpuvaG3CaptureFaultMapping(&adapter,&e,&r);} while(0)
  CAPTURE();assert(r.HistoryScanned==1 && r.HistoryMatches==1 && r.HistoryCount==1);
  assert(r.History[0].Match==3 && r.History[0].Sequence==1);
  /* A newer same-VA foreign process or disjoint 16 KiB group cannot match. */
  state.LeafHistory[1]=h;state.LeafHistory[1].ProcessId++;
  state.LeafHistory[2]=h;state.LeafHistory[2].FirstVa+=0x4000;state.LeafHistory[2].First+=4;
  state.LeafHistoryNext=3;CAPTURE();assert(r.HistoryMatches==1);
  /* Neighbor within the native group may invalidate the complete leaf. */
  state.LeafHistory[3]=h;state.LeafHistory[3].First=group+3;
  state.LeafHistory[3].FirstVa=base+0x3000;state.LeafHistory[3].Count=1;
  state.LeafHistory[3].ValidCount=0;state.LeafHistory[3].Flags=4;
  state.LeafHistoryNext=4;CAPTURE();assert(r.HistoryMatches==2);
  assert(r.History[0].ValidCount==0 && r.History[0].Flags==4);
  /* Whole-table clear has no VA; match by authenticated table/index. */
  state.LeafHistory[4]=h;state.LeafHistory[4].FirstVa=0;
  state.LeafHistory[4].First=0;state.LeafHistory[4].Count=8192;
  state.LeafHistory[4].Flags=ADMISSION_G3_LEAF_EVENT_RESET;
  state.LeafHistory[4].ValidCount=0;state.LeafHistory[4].Allocation=0;
  state.LeafHistoryNext=5;CAPTURE();assert(r.HistoryMatches==3);
  assert(r.History[0].Match==2 && r.History[0].Flags==16);
  state.LeafHistory[4].Flags=ADMISSION_G3_LEAF_EVENT_SYSTEM_RETIRE;
  CAPTURE();assert(r.History[0].Flags==32);
  /* Replaced table still matches via VA; failed updates stay failed. */
  state.LeafHistory[5]=h;state.LeafHistory[5].TableIpa+=0x4000;
  state.LeafHistory[5].Status=0xc000000d;state.LeafHistoryNext=6;
  CAPTURE();assert(r.HistoryMatches==4 && r.History[0].Match==1);
  assert(r.History[0].Status==0xc000000d);
  /* Missing current table must not erase historical VA evidence. */
  ms[(va>>25)&2047u]=NULL;CAPTURE();
  assert(r.Status==AdmissionGpuFaultMappingNoTable && r.HistoryMatches==3);
  assert(r.History[0].Match==1);ms[(va>>25)&2047u]=&me;
  /* Malformed/out-of-range index cannot wrap into a false table match. */
  state.LeafHistory[6]=h;state.LeafHistory[6].FirstVa=0;
  state.LeafHistory[6].First=0xffffffffu;state.LeafHistory[6].Count=8;
  state.LeafHistoryNext=7;CAPTURE();assert(r.HistoryMatches==4);
  busy=1;CAPTURE();assert(r.Status==AdmissionGpuFaultMappingBusy && !r.HistoryScanned);busy=0;
  p.Graph.Uncertain=1;CAPTURE();assert(!r.HistoryScanned);p.Graph.Uncertain=0;
  /* Empty ring is known to be scanned empty, not proof of never mapped. */
  state.LeafHistoryNext=0;CAPTURE();assert(!r.HistoryCount && !r.HistoryMatches);
  /* Wrap the physical ring. Keep newest 8 of 10, report truncation. */
  memset(state.LeafHistory,0,sizeof(state.LeafHistory));
  for(unsigned n=0;n<10;++n) {
    h.Qpc=100+n;h.Allocation=0xaaa0+n;
    state.LeafHistory[(8190+n)%ADMISSION_G3_LEAF_RING]=h;
  }
  state.LeafHistoryNext=8200;CAPTURE();
  assert(r.HistoryNext==8200 && r.HistoryScanned==8192);
  assert(r.HistoryMatches==10 && r.HistoryCount==8);
  assert(r.History[0].Sequence==8200 && r.History[7].Sequence==8193);
  assert(r.History[0].Qpc==109 && r.History[7].Qpc==102);
  assert(p.Graph.MappingGeneration==1234 && p.Graph.LeaseToken==7 && !locked);
  #undef CAPTURE
#endif
"""

RECOVERY = r'''
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define APPLE_AGX_BACKEND_FALSE 0
#define APPLE_AGX_BACKEND_TRUE 1
#define TRUE 1
#define AppleAgxInitdataMemoryFirmwareStatus 0
#define AppleAgxInitdataMemoryRegionC 1
@@REGION_DEFINES@@
typedef unsigned char UCHAR;
typedef struct { void *CpuAddress; ULONGLONG Length; } APPLE_AGX_MEMORY_OBJECT;
typedef unsigned APPLE_AGX_BACKEND_U32;
typedef int APPLE_AGX_BACKEND_BOOL;
typedef struct {
  ADMISSION_CONTEXT *Adapter;
  struct { struct { unsigned PendingFence; struct { unsigned DeadlineExpiries; } Runtime; } QueueProvider; } Provider;
  struct { APPLE_AGX_MEMORY_OBJECT DataObjects[2]; } Initdata;
  ADMISSION_GPU_FAULT_RECEIPT GpuFault;
  ADMISSION_GPU_FAULT_MAPPING GpuFaultMapping, GpuFaultMappingLast;
  int GpuFaultUnpublished, FirmwareRecoveries, FirmwareRecoveryUnpublished;
  unsigned LastFirmwareRecoveryKind, LastFirmwareRecoveryFence;
  unsigned LastFirmwareRecoveryMs, MaxFirmwareRecoveryMs;
  unsigned PublishedDeadlineExpiries, LongestJobMs, LongestJobFence;
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
typedef int BOOLEAN;
typedef void *HANDLE;
typedef const wchar_t *UNICODE_STRING;
#define FALSE 0
#define PLUGPLAY_REGKEY_DEVICE 1
#define KEY_SET_VALUE 2
#define REG_BINARY 3
#define NT_SUCCESS(s) ((s)>=0)
static int first_written,last_written,flushed;
static ADMISSION_GPU_FAULT_MAPPING first_written_value,last_written_value;
static int InterlockedCompareExchange(int *p,int v,int cmp) {
  int before=*p; if(before==cmp) *p=v; return before;
}
static int IoOpenDeviceRegistryKey(void *a,int b,int c,HANDLE *key) {
  (void)a;(void)b;(void)c;*key=(HANDLE)1; return 0;
}
static void RtlInitUnicodeString(UNICODE_STRING *s,const wchar_t *value) {*s=value;}
static int ZwSetValueKey(HANDLE key,UNICODE_STRING *name,int title,int type,
    const void *value,unsigned bytes) {
  (void)key;(void)title;assert(type==REG_BINARY);
  if(!wcscmp(*name,L"Wom1GpuFaultMapping")) {
    assert(bytes==sizeof(ADMISSION_GPU_FAULT_MAPPING));memcpy(&first_written_value,value,bytes);++first_written;
  } else if(!wcscmp(*name,L"Wom1GpuFaultMappingLast")) {
    assert(bytes==sizeof(ADMISSION_GPU_FAULT_MAPPING));memcpy(&last_written_value,value,bytes);++last_written;
  }
  return 0;
}
static int ZwFlushKey(HANDLE key) {(void)key;++flushed;return 0;}
static void ZwClose(HANDLE key) {(void)key;}
@@PUBLISH@@
'''


class FaultMapping(unittest.TestCase):
    def test_current_mapping_and_first_fault_retention(self):
        text = (RENDER/'src/gpuva_g3_windows.c').read_text()
        self.assertTrue('VOID AdmissionGpuvaG3CaptureFaultMapping(' in text,
                      'fault VA cannot be correlated with the current graph')
        source = PROGRAM.replace('@@FUNCTION@@', body(text, 'AdmissionGpuvaG3CaptureFaultMapping'))
        types = (RENDER/'include/render_admission.h').read_text()
        types = types[types.index('#define ADMISSION_G3_LEAF_RING '):]
        types = types[:types.index('} ADMISSION_G3_LEAF_HISTORY;')+len('} ADMISSION_G3_LEAF_HISTORY;')]
        source = source.replace('@@LEAF_TYPES@@', types).replace('@@HISTORY_CASES@@', HISTORY_CASES)
        backend = (RENDER/'src/backend_platform_windows.c').read_text()
        source = source.replace('@@RECOVERY@@', RECOVERY.replace('@@BODY@@',
            body(backend, 'AdmissionTransportRecover')).replace('@@PUBLISH@@',
            body(backend, 'AdmissionPublishFirmwareRecovery')))
        header = (RENDER/'include/render_admission.h').read_text()
        source = source.replace('@@REGION_DEFINES@@', '\n'.join(
            re.search(r'^#define ' + name + r' .+$', text, re.M)[0]
            for name, text in (
                ('ADMISSION_REGIONC_PENDING_STAMPS_OFFSET', backend),
                ('ADMISSION_REGIONC_PENDING_STAMPS_BYTES', header))))
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
            self.assertEqual(result['version'], 2, 'fault capture loses the paging history')
            self.assertEqual(result['status'], 'captured')
            history = result['history']
            self.assertEqual(history['matched'], 10)
            self.assertEqual(history['scanned'], 8192)
            self.assertTrue(history['truncated'])
            self.assertEqual(len(history['events']), 8)
            self.assertEqual([e['qpc'] for e in history['events']], list(range(109, 101, -1)))
            self.assertEqual(history['events'][0]['match'], ['va', 'table-index'])
            self.assertEqual(history['events'][0]['allocation'], '0xaaa9')
            # Real v1 prefix still decodes, without fabricated history.
            old = bytearray(data[:256]); struct.pack_into('<II', old, 0, 1, 256)
            prior = DECODER.decode_fault_mapping(old)
            self.assertEqual(prior['version'], 1)
            self.assertIsNone(prior['history'])
            self.assertEqual(prior['ptes'], result['ptes'])
            for offset, value in ((256+4, 8193), (256+8, 8193), (256+12, 9)):
                bad = bytearray(data); struct.pack_into('<I', bad, offset, value)
                with self.assertRaises(ValueError): DECODER.decode_fault_mapping(bad)
            self.assertEqual(result['fault_va'], '0x1112681040')
            self.assertFalse(result['native_present'])
            self.assertEqual(result['ptes'][1]['allocation'], '0xabc001')
            self.assertEqual(result['ptes'][3]['ipa'], '0xa00006000')
            self.assertEqual(DECODER.read_receipts(path), {'Wom1GpuFaultMapping': data})
            latest = tmp/'Wom1GpuFaultMappingLast.bin'
            latest.write_bytes(data)
            self.assertEqual(DECODER.read_receipts(latest), {'Wom1GpuFaultMappingLast': data})
            self.assertEqual(DECODER.DECODERS['Wom1GpuFaultMappingLast'](data), result)
            for suffix, content in [
                ('.json', json.dumps({'Receipts': {'Wom1GpuFaultMappingLast':
                    base64.b64encode(data).decode()}})),
                ('.reg', '"Wom1GpuFaultMappingLast"=hex:' + ','.join(f'{v:02x}' for v in data))]:
                saved = tmp/('receipts'+suffix); saved.write_text(content)
                self.assertEqual(DECODER.read_receipts(saved), {'Wom1GpuFaultMappingLast': data})
            for invalid in (b'', data[:-1], bytes(len(data)), data+b'\0'):
                with self.assertRaises(ValueError): DECODER.decode_fault_mapping(invalid)


if __name__ == '__main__':
    unittest.main()
