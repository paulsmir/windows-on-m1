"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Opt-in must require the explicit sysmem ABI, reject old/tainted/stale replies,
and preserve default startup. This executes the real client and KMD readers.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_apple_agx_pool_headroom import function
import test_g4_pool_residency_limit as policy
RENDER = policy.RENDER

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'


def run(source, client=False):
    with tempfile.TemporaryDirectory(prefix='sysmem-client-') as tmp:
        c = Path(tmp) / 'test.c'
        binary = Path(tmp) / 'test'
        c.write_text(source)
        cmd = [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
               '-Werror', '-fsanitize=address,undefined', '-I', str(SHARED / 'include'), str(c)]
        if client:
            cmd += [str(SHARED / 'src/apple_agx_gpuva_broker_v5_client.c')]
        subprocess.run(cmd + ['-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)


class SysmemBroker(unittest.TestCase):
    def test_capability_reply_contract(self):
        run(r'''
#include <assert.h>
#include <string.h>
#include "apple_agx_gpuva_broker_v5_client.h"
struct fixture { AGX_GPUVA_V5_REQUEST q; AGX_GPUVA_V5_RESPONSE r; unsigned calls, mode; };
static bool w64(void *opaque,unsigned offset,unsigned long long value) {
  struct fixture *f=opaque;
  assert(offset>=AGX_GPUVA_V5_OFFSET && offset<AGX_GPUVA_V5_OFFSET+sizeof(f->q));
  memcpy((char *)&f->q+offset-AGX_GPUVA_V5_OFFSET,&value,8);return true;
}
static bool w32(void *opaque,unsigned offset,unsigned value) {
  struct fixture *f=opaque; AGX_GPUVA_V5_REQUEST expected={0};
  assert(offset==AGX_GPUVA_V5_OFFSET+AGX_GPUVA_V5_DOORBELL && value==1);
  expected.Version=6;expected.Bytes=128;expected.Command=AGX_GPUVA_V5_QUERY_CAPS;
  expected.Sequence=f->q.Sequence;expected.Epoch=7;
  assert(!memcmp(&expected,&f->q,sizeof(expected)));++f->calls;
  memset(&f->r,0,sizeof(f->r));f->r.Receipt=f->q.Sequence;f->r.Epoch=7;
  f->r.Reserved[0]=AGX_GPUVA_V5_CAP_SYSMEM;f->r.Reserved[1]=AGX_GPUVA_V5_SYSMEM_ABI;
  if(f->mode==1) f->r.Status=1; /* Old broker rejects command. */
  if(f->mode==2) f->r.Flags=1;
  if(f->mode==3) f->r.Reserved[0]=0;
  if(f->mode==4) f->r.Reserved[1]=2;
  if(f->mode==5) f->r.Epoch=8;
  if(f->mode==6) --f->r.Receipt;
  if(f->mode==7) return false;
  return true;
}
static bool r64(void *opaque,unsigned offset,unsigned long long *value) {
  struct fixture *f=opaque;
  assert(offset>=AGX_GPUVA_V5_OFFSET+AGX_GPUVA_V5_RESPONSE_OFFSET);
  memcpy(value,(char *)&f->r+offset-AGX_GPUVA_V5_OFFSET-AGX_GPUVA_V5_RESPONSE_OFFSET,8);
  return true;
}
static void barrier(void *opaque) {(void)opaque;}
int main(void) {
  struct fixture f={0};APPLE_AGX_GPUVA_V5_CLIENT c;
  APPLE_AGX_GPUVA_V5_IO io={&f,w64,r64,w32,barrier,0};
  assert(AppleAgxGpuvaV5ClientInit(&c,&io));
  assert(!AppleAgxGpuvaV5ClientHasSysmem(&c) && !f.calls);
  c.Epoch=7;
  for(f.mode=0;f.mode<8;++f.mode)
    assert(AppleAgxGpuvaV5ClientHasSysmem(&c)==(f.mode==0));
  assert(c.Epoch==7 && f.calls==8);
  assert(!AppleAgxGpuvaV5ClientHasSysmem(NULL));
  return 0;
}
''', client=True)

    def test_only_dword_one_enables_probe(self):
        policy.PoolResidencyLimit.check_registry_reader(
            self, 'AdmissionSysmemGpuMapRequested', 'Wom1SysmemGpuMap',
            source=(RENDER / 'src/gpuva_g3_windows.c').read_text())

    def test_live_switch_only_dword_one_enables_probe(self):
        policy.PoolResidencyLimit.check_registry_reader(
            self, 'AdmissionLiveAdditivePagingRequested', 'Wom1LiveAdditivePaging',
            source=(RENDER / 'src/gpuva_g3_windows.c').read_text())

    def test_switch_is_read_inside_g3_start_before_negotiation(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Pool startup is
        later than G3 startup; reading this switch there silently skips it."""
        start = function((RENDER / 'src/gpuva_g3_windows.c').read_text(), 'AdmissionGpuvaG3Start')
        self.assertLess(start.index('KeGetCurrentIrql() != PASSIVE_LEVEL'),
                        start.index('AdmissionSysmemGpuMapRequested(context)'))
        self.assertLess(start.index('AdmissionSysmemGpuMapRequested(context)'),
                        start.index('AppleAgxGpuvaV5ClientHasSysmem'))

    def test_real_start_defaults_and_failure_cleanup(self):
        start = function((RENDER / 'src/gpuva_g3_windows.c').read_text(), 'AdmissionGpuvaG3Start')
        run(r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "apple_agx_gpuva_broker_v5_client.h"
typedef int NTSTATUS, BOOLEAN;
typedef struct { void *GpuvaG3State; int SysmemGpuMap,LiveRequested; unsigned LiveAdditivePaging; struct {unsigned PagingNode;} ObjectAdapter; } ADMISSION_CONTEXT;
typedef struct { ADMISSION_CONTEXT *Adapter; int Lock,JobEvent,Processes;
 APPLE_AGX_GPUVA_V5_CLIENT Client; } ADMISSION_G3_STATE;
#define PASSIVE_LEVEL 0
#define STATUS_INVALID_DEVICE_STATE -1
#define STATUS_INSUFFICIENT_RESOURCES -2
#define STATUS_DEVICE_HARDWARE_ERROR -3
#define STATUS_NOT_SUPPORTED -4
#define STATUS_SUCCESS 0
#define POOL_FLAG_NON_PAGED 0
#define ADMISSION_POOL_TAG 0
#define NotificationEvent 0
#define FALSE 0
#define RtlZeroMemory(p,n) memset(p,0,n)
static int irql, allocated, freed, queries, attached, supported,live_supported,live_queries;
static int KeGetCurrentIrql(void) {return irql;}
static int AdmissionLiveAdditivePagingRequested(ADMISSION_CONTEXT *c) {assert(irql==0);return c->LiveRequested;}
static int AdmissionSysmemGpuMapRequested(ADMISSION_CONTEXT *c) {assert(irql==0);return c->SysmemGpuMap;}
static void *ExAllocatePool2(int flags,size_t bytes,int tag) {(void)flags;(void)tag;++allocated;return malloc(bytes);}
static void ExFreePoolWithTag(void *p,int tag) {(void)tag;++freed;free(p);}
static void ExInitializeFastMutex(int *p) {*p=1;}
static void KeInitializeEvent(int *p,int type,int state) {(void)type;*p=state;}
static void InitializeListHead(int *p) {*p=1;}
static int AdmissionGpuvaV5ClientOpen(ADMISSION_CONTEXT *c,APPLE_AGX_GPUVA_V5_CLIENT *v) {(void)c;(void)v;return 1;}
bool AppleAgxGpuvaV5ClientCall(APPLE_AGX_GPUVA_V5_CLIENT *c,const AGX_GPUVA_V5_REQUEST *q,AGX_GPUVA_V5_RESPONSE *r)
{(void)c;assert(q->Command==AGX_GPUVA_V5_CREATE);r->Status=2;r->Epoch=7;return true;}
bool AppleAgxGpuvaV5ClientHasSysmem(APPLE_AGX_GPUVA_V5_CLIENT *c) {(void)c;++queries;return supported;}
bool AppleAgxGpuvaV5ClientHasLiveAdditive(APPLE_AGX_GPUVA_V5_CLIENT *c) {(void)c;++live_queries;return live_supported;}
static void AdmissionG3AttachMailbox(ADMISSION_G3_STATE *s) {(void)s;++attached;}
static void AdmissionG3RecordLiveAdditiveState(ADMISSION_CONTEXT *a,int requested,
 const APPLE_AGX_GPUVA_V5_CLIENT *c) {(void)a;(void)requested;(void)c;}
''' + start + r'''
int main(void) {
 ADMISSION_CONTEXT c={0};
 assert(AdmissionGpuvaG3Start(&c)==0 && !queries && attached==1);
 assert(!live_queries&&!c.LiveAdditivePaging&&!c.ObjectAdapter.PagingNode);
 free(c.GpuvaG3State);c.GpuvaG3State=NULL;c.SysmemGpuMap=1;
 assert(AdmissionGpuvaG3Start(&c)==STATUS_NOT_SUPPORTED);
 assert(queries==1 && attached==1 && freed==1 && !c.GpuvaG3State);
 supported=1;
 assert(AdmissionGpuvaG3Start(&c)==0 && queries==2 && attached==2);
 free(c.GpuvaG3State);c.GpuvaG3State=NULL;irql=1;
 assert(AdmissionGpuvaG3Start(&c)==STATUS_INVALID_DEVICE_STATE && allocated==3);
 irql=0;c.LiveRequested=1;
 assert(AdmissionGpuvaG3Start(&c)==0 && live_queries==1 && !c.LiveAdditivePaging && !c.ObjectAdapter.PagingNode);
 free(c.GpuvaG3State);c.GpuvaG3State=NULL;live_supported=1;
 assert(AdmissionGpuvaG3Start(&c)==0 && live_queries==2 && c.LiveAdditivePaging==1 && c.ObjectAdapter.PagingNode==1);
 free(c.GpuvaG3State);
 return 0;
}
''')
