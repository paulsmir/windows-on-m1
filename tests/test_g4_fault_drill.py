"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The deliberate CDM fault must be one-shot, confined to the arming process's
currently leased graph, and injected only into a proven unmapped range. Unknown
or unpublished mappings must never be treated as a safe fault target.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'


class FaultDrill(unittest.TestCase):
    def test_injection_uses_real_graph_absence_proof(self):
        graph = (ROOT / 'drivers/apple-agx/shared/src/apple_agx_gpuva_g3_graph.c').read_text()
        pieces = []
        for pattern in (r'static APPLE_AGX_GPUVA_G3_NODE \*slot\(.*?^}',
                        r'static bool missing_range\(.*?^}',
                        r'bool AppleAgxGpuvaG3GraphInspectRangeAccess\(.*?^}'):
            pieces.append(re.search(pattern, graph, re.S | re.M)[0])
        shim = re.sub(r'typedef struct \{ unsigned Created,.*?enum \{.*?};',
                      '#include "apple_agx_gpuva_g3_graph.h"\ntypedef APPLE_AGX_GPUVA_G3_GRAPH GRAPH;',
                      SHIM, flags=re.S)
        shim = shim.replace('irql, pid, lock_depth, mapped, reason, inspected', 'irql, pid, lock_depth')
        shim = re.sub(r'static int AppleAgxGpuvaG3GraphInspectRangeAccess\(.*?^}',
                      '#define LOOKUP_VISIT() ((void)0)\n#define G3_PAGE 0x4000ULL\n' +
                      '\n'.join(pieces), shim, flags=re.S | re.M)
        self.run_drill(shim, REAL_GRAPH_CASES)

    def test_actual_arm_query_and_injection_boundaries(self):
        self.run_drill(SHIM, CASES)

    def run_drill(self, shim, cases):
        source = (RENDER / 'src/gpuva_g3_windows.c').read_text()
        functions = []
        for name in ('AdmissionGpuvaG3FaultDrillEscape', 'AdmissionGpuvaG3FaultDrillApply'):
            match = re.search(r'(?:NTSTATUS|BOOLEAN) ' + name + r'\(.*?^}', source, re.S | re.M)
            self.assertIsNotNone(match, 'missing real fault drill entry point: ' + name)
            functions.append(match[0])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'drill.c'
            path.write_text(shim + '\n'.join(functions) + cases)
            exe = Path(tmp) / 'drill'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-fsanitize=address,undefined', '-I', str(RENDER / 'include'),
                            '-I', str(ROOT / 'drivers/apple-agx/shared/include'),
                            str(path), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


SHIM = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "render_gpu_fault.h"
typedef int32_t NTSTATUS;
typedef uint32_t ULONG;
typedef uint64_t ULONGLONG;
typedef int BOOLEAN;
typedef void *HANDLE;
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000du)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xc00000bbu)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xc0000184u)
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xc0000008u)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011u)
#define ADMISSION_G3_PROCESS_MAGIC 123
#define ADMISSION_GPUVA_G3_JOB_VM_SLOT 1
#define RtlCopyMemory memcpy
#define RtlZeroMemory(p,n) memset(p,0,n)
#define HandleToULong(p) ((ULONG)(uintptr_t)(p))
#define CONTAINING_RECORD(p,t,f) ((t *)(void *)(p))
typedef struct LIST { struct LIST *Flink; } LIST_ENTRY, *PLIST_ENTRY;
typedef struct { unsigned Created, Uncertain, JobInFlight, Slot; void *RootTable; ULONGLONG RootIpa, LeaseToken; } GRAPH;
typedef struct { unsigned Reason; } APPLE_AGX_GPUVA_G3_WALK_FAILURE;
enum { AppleAgxG3WalkNoRoot=1, AppleAgxG3WalkNoTable=2, AppleAgxG3WalkLeafAbsent=3, AppleAgxG3WalkLeafNotPublished=4 };
typedef struct PROCESS {
  LIST_ENTRY Link;
  unsigned Magic, OsProcessId, Poisoned, CpuOnlyMappings;
  GRAPH Graph;
  ADMISSION_GPU_FAULT_DRILL FaultDrill;
} ADMISSION_G3_PROCESS;
typedef struct { LIST_ENTRY Processes; unsigned Lock, ActiveFence; ADMISSION_G3_PROCESS *ActiveProcess; } ADMISSION_G3_STATE;
typedef struct { void *GpuvaG3State; unsigned GpuFaultDrillAllowed; } ADMISSION_CONTEXT;
typedef struct { void *pPrivateDriverData; unsigned PrivateDriverDataSize; } DXGKARG_ESCAPE;
static unsigned irql, pid, lock_depth, mapped, reason, inspected;
static unsigned KeGetCurrentIrql(void) { return irql; }
static HANDLE PsGetCurrentProcessId(void) { return (HANDLE)(uintptr_t)pid; }
static void ExAcquireFastMutex(unsigned *lock) { (void)lock; assert(!lock_depth++); }
static void ExReleaseFastMutex(unsigned *lock) { (void)lock; assert(lock_depth--==1); }
static int AppleAgxGpuvaG3GraphInspectRangeAccess(GRAPH *g, ULONGLONG va,
    unsigned bytes, int write, APPLE_AGX_GPUVA_G3_WALK_FAILURE *failure) {
  assert(g->JobInFlight && lock_depth && va==0x10000 && bytes==64 && !write);
  ++inspected; failure->Reason=reason; return mapped;
}
'''
CASES = r'''
int main(void) {
  ADMISSION_CONTEXT a={0}; ADMISSION_G3_STATE s={0};
  ADMISSION_G3_PROCESS own={0}, other={0};
  ADMISSION_GPU_FAULT_DRILL q={0}; DXGKARG_ESCAPE args={&q,sizeof(q)};
  ULONGLONG base; unsigned bytes, mode;
  a.GpuvaG3State=&s; s.Processes.Flink=&own.Link;
  own.Link.Flink=&other.Link; other.Link.Flink=&s.Processes;
  own.Magic=other.Magic=ADMISSION_G3_PROCESS_MAGIC;
  own.OsProcessId=pid=42; other.OsProcessId=99;
  own.Graph.Created=own.Graph.JobInFlight=1; own.Graph.Slot=1; own.Graph.RootTable=&s; own.Graph.RootIpa=0x900000000ULL; own.Graph.LeaseToken=7;
  q.Magic=ADMISSION_GPU_FAULT_DRILL_MAGIC; q.Version=1; q.Bytes=sizeof(q); q.Operation=1;
  assert(AdmissionGpuvaG3FaultDrillEscape(&a,&args)==STATUS_NOT_SUPPORTED);
  a.GpuFaultDrillAllowed=1;
  assert(AdmissionGpuvaG3FaultDrillEscape(&a,&args)==0 && q.Armed);
  s.ActiveProcess=&other; s.ActiveFence=101; base=0x1100000000ULL; bytes=4096;
  assert(!AdmissionGpuvaG3FaultDrillApply(&a,101,&base,&bytes));
  assert(base==0x1100000000ULL && bytes==4096 && own.FaultDrill.Armed && !inspected);
  s.ActiveProcess=&own;
  assert(!AdmissionGpuvaG3FaultDrillApply(&a,102,&base,&bytes) && own.FaultDrill.Armed);
  for (mode=0;mode<8;++mode) {
    memset(&q,0,sizeof(q)); q.Magic=ADMISSION_GPU_FAULT_DRILL_MAGIC;
    q.Version=1; q.Bytes=sizeof(q); q.Operation=1;
    assert(AdmissionGpuvaG3FaultDrillEscape(&a,&args)==0);
    base=0x1100000000ULL; bytes=4096; inspected=0;
    mapped=mode==0; reason=mode==1 ? 4 : mode==2 ? 0 : mode==6 ? 3 : mode==7 ? 1 : 2;
    own.Graph.Uncertain=mode==3; own.Graph.JobInFlight=mode!=4;
    assert(AdmissionGpuvaG3FaultDrillApply(&a,101,&base,&bytes)==(mode>=5));
    assert(!own.FaultDrill.Armed); /* consumed even when proof fails */
    assert(base==(mode>=5 ? 0x10000ULL : 0x1100000000ULL));
    assert(bytes==(mode>=5 ? 64u : 4096u));
    assert(!AdmissionGpuvaG3FaultDrillApply(&a,101,&base,&bytes));
    own.Graph.Uncertain=0; own.Graph.JobInFlight=1;
  }
  q.Operation=0;
  assert(AdmissionGpuvaG3FaultDrillEscape(&a,&args)==0);
  assert(q.Fence==101 && q.GpuVa==0x10000 && !q.Armed && !q.FaultInfo);
  pid=99;
  assert(AdmissionGpuvaG3FaultDrillEscape(&a,&args)==0 && !q.Fence && !q.GpuVa);
  pid=43;
  assert(AdmissionGpuvaG3FaultDrillEscape(&a,&args)==STATUS_INVALID_HANDLE);
  assert(!lock_depth);
  return 0;
}
'''

REAL_GRAPH_CASES = r'''
int main(void) {
  ADMISSION_CONTEXT a={0}; ADMISSION_G3_STATE s={0}; ADMISSION_G3_PROCESS p={0};
  APPLE_AGX_GPUVA_G3_NODE root={0}, re={0}, middle={0}, me={0}, table={0}, leaf={0};
  APPLE_AGX_GPUVA_G3_NODE *rs[8]={0}, *ms[2048]={0}, *ls[2048]={0};
  unsigned mode, bytes; unsigned long long base;
  root.Slots=rs; middle.Slots=ms; table.Slots=ls;
  re.ChildTable=&middle; me.ChildTable=&table;
  a.GpuFaultDrillAllowed=1; a.GpuvaG3State=&s; s.ActiveProcess=&p; s.ActiveFence=101;
  p.Graph.Created=1; p.Graph.JobInFlight=1; p.Graph.RootIpa=0x900000000ULL;
  p.Graph.RootTable=&root; p.Graph.LeaseToken=7; p.Graph.Slot=1;
  /* Each absent edge is an unmapped VA. Existing-but-unpublished leaves
   * and mapped pages must never be chosen, despite the same probe address. */
  for (mode=0;mode<5;++mode) {
    rs[0]=mode>=1 ? &re : NULL; ms[0]=mode>=2 ? &me : NULL;
    ls[4]=mode>=3 ? &leaf : NULL; leaf.AuxIpa=mode==4 ? 0x900004000ULL : 0;
    p.FaultDrill.Armed=1; base=0x1100000000ULL; bytes=4096;
    assert(AdmissionGpuvaG3FaultDrillApply(&a,101,&base,&bytes)==(mode<3));
    assert(base==(mode<3 ? 0x10000ULL : 0x1100000000ULL));
    assert(!p.FaultDrill.Armed && !lock_depth);
  }
  /* Keep the escape in this translation-unit replay as a real caller. */
  assert(AdmissionGpuvaG3FaultDrillEscape(NULL,NULL)==STATUS_INVALID_PARAMETER);
  return 0;
}
'''
