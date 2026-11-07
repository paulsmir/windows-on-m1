"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2116's node1 metadata overwrote Wom1G3Node0, falsely suggesting a missing
paging node. Execute the real receipt writer in the observed node0/node1
sequence, with only Windows registry/IRQL services modeled.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

from test_g4_sysmem_broker import run, RENDER
from test_apple_agx_pool_headroom import function

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'


REGISTRY = r'''
#include <assert.h>
#include <string.h>
#include <wchar.h>
typedef unsigned int ULONG, UINT;
typedef int NTSTATUS;
typedef void *HANDLE, *PDEVICE_OBJECT;
typedef struct { unsigned EngineType, GpuMmuSupported, IoMmuSupported;
  struct { unsigned Value; } Flags; } DXGKARG_GETNODEMETADATA;
#define _Use_decl_annotations_
#define PASSIVE_LEVEL 0
#define PLUGPLAY_REGKEY_DEVICE 1
#define KEY_SET_VALUE 2
#define NT_SUCCESS(s) ((s) >= 0)
#define RtlZeroMemory(p,n) memset(p,0,n)
static int irql, opens, closes, writes, open_result;
static int KeGetCurrentIrql(void) {return irql;}
static int IoOpenDeviceRegistryKey(void *pdo,int kind,int access,HANDLE *key) {
  assert(pdo && kind==1 && access==2);++opens;
  if (!open_result) *key=pdo;
  return open_result;
}
static void ZwClose(HANDLE key) {assert(key);++closes;}
static int ZwFlushKey(HANDLE key) {assert(key);return 0;}
static ULONG nodes[2][8];
static void WriteBinary(HANDLE key,const wchar_t *name,const void *data,ULONG bytes) {
  unsigned node;
  assert(key && bytes==32);
  if (!wcscmp(name,L"Wom1G3Node0")) node=0;
  else {assert(!wcscmp(name,L"Wom1G3Node1"));node=1;}
  memcpy(nodes[node],data,bytes);++writes;
}
'''


class LiveActivation(unittest.TestCase):
    def test_real_startup_reports_discovery_before_process_negotiation(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Accepted capability must be distinguishable from request-off, absent,
        malformed or stale replies. Actual broker dispatch, client, readers
        and G3 startup must agree without adding a second capability query.
        """
        m1n1 = Path(os.environ.get('Q22_M1N1_SOURCE', str(ROOT / 'm1n1_windows')))
        with tempfile.TemporaryDirectory(prefix='q22c-start-') as tmp:
            d = Path(tmp)
            shutil.copytree(m1n1 / 'src', d / 'm1n1/src')
            shutil.copytree(m1n1 / 'tests', d / 'm1n1/tests')
            (d / 'drivers/apple-agx').mkdir(parents=True)
            (d / 'drivers/apple-agx/shared').symlink_to(SHARED.resolve())
            platform = (m1n1 / 'src/hv_agx_retained_platform.c').read_text()
            source = '''#include <stdio.h>
#define main broker_selftest
#include "hv_agx_gpuva_v5_test.c"
#undef main
#include "hv_agx_gpuva_v5_mmio.h"
static bool request_powered=true, gpuva_capacity_reported;
static struct hv_agx_gpuva_v5_wire gpuva_v5_wire;
#define gpuva_v5 (((struct fixture *)context)->broker)
'''
            source += function(platform, 'gpuva_execute') + '\n#undef gpuva_v5\n'
            kmd = (RENDER / 'src/gpuva_g3_windows.c').read_text()
            names = ['AdmissionSysmemGpuMapRequested', 'AdmissionLiveAdditivePagingRequested']
            if 'static BOOLEAN AdmissionGpuPrivateCachedRequested(' in kmd:
                names.append('AdmissionGpuPrivateCachedRequested')  # GC experiment switch
            if 'static VOID AdmissionG3RecordLiveAdditiveState(' in kmd:
                names.append('AdmissionG3RecordLiveAdditiveState')
            names.append('AdmissionGpuvaG3Start')
            actual = '\n'.join(function(kmd, n) for n in names)
            source += (ROOT / 'tests/g4_live_activation_start.c').read_text().replace(
                '/* ACTUAL_KMD_START */', actual)
            (d / 'case.c').write_text(source)
            cmd = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-function', '-ftrivial-auto-var-init=pattern',
                   '-fsanitize=address,undefined',
                   '-I'+str(d/'m1n1/src'), '-I'+str(d/'m1n1/tests'),
                   '-I'+str(SHARED/'include'), str(d/'case.c'),
                   str(d/'m1n1/src/hv_agx_gpuva_v5.c'),
                   str(d/'m1n1/src/hv_agx_gpuva_v5_mmio.c'),
                   str(SHARED/'src/apple_agx_uat.c'),
                   str(SHARED/'src/apple_agx_gpuva_broker_v5_client.c'),
                   '-o', str(d/'case')]
            subprocess.run(cmd, check=True)
            subprocess.run([str(d/'case')], check=True)

    def test_node_metadata_keeps_both_ordinals(self):
        source = (RENDER / 'src/receipts.c').read_text()
        record = re.search(r'typedef struct _ADMISSION_G3_NODE_RECEIPT.*?'
                           r'} ADMISSION_G3_NODE_RECEIPT;', source, re.S).group(0)
        run(REGISTRY + record + function(source, 'AdmissionRecordGpuvaG3Node') + r'''
int main(void) {
  int device;
  DXGKARG_GETNODEMETADATA render={1,1,0,{0}}, paging={6,1,0,{0}};
  AdmissionRecordGpuvaG3Node(&device,0,&render,0);
  AdmissionRecordGpuvaG3Node(&device,1,&paging,0);
  assert(nodes[0][2]==0 && nodes[0][4]==1);
  assert(nodes[1][2]==1 && nodes[1][4]==6 && nodes[1][5]==1);
  assert(nodes[0][0]==1 && nodes[0][1]==32 && nodes[0][3]==0);
  assert(nodes[1][6]==0 && nodes[1][7]==0);
  AdmissionRecordGpuvaG3Node(&device,1,&paging,-1);
  assert(nodes[1][3]==~0u && nodes[1][4]==0 && nodes[1][5]==0);
  assert(nodes[0][4]==1);
  irql=1;AdmissionRecordGpuvaG3Node(&device,0,&render,0);irql=0;
  AdmissionRecordGpuvaG3Node(NULL,0,&render,0);
  AdmissionRecordGpuvaG3Node(&device,0,NULL,0);
  open_result=-1;AdmissionRecordGpuvaG3Node(&device,0,&render,0);
  assert(writes==3 && opens==4 && closes==3);
  return 0;
}
''')
