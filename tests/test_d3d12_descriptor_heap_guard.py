"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Already cached private descriptor mappings must not allocate a redundant CPU
shadow; destruction must free their storage once. The opt-in heap-end guard must be in the same allocation, initialized in the
GPU mirror, and excluded from logical descriptor counts. Removing the extra
backing or initializing only the cached CPU shadow exposes a neighbour again.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_root_signature_identity import block

ROOT = Path(__file__).resolve().parents[1]
DDI = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'

class DescriptorHeapGuard(unittest.TestCase):
    def test_actual_create_guard_on_off_and_failure(self):
        source = DDI.read_text()
        actual = block(source, 'static UINT64 DescriptorHeapBytes(') + '\n' + block(source, 'static HRESULT APIENTRY CreateDescriptorHeap(') + '\n' + block(source, 'static VOID APIENTRY DestroyDescriptorHeap(') + '\n' + block(source, 'static void MirrorHeapForms(')
        fixture = (ROOT / 'tests/d3d12_descriptor_heap_guard_fixture.cpp').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = Path(tmp) / 'guard.cpp', Path(tmp) / 'guard'
            cpp.write_text(fixture.replace('/* CREATE */', actual))
            build = subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_guard_is_not_an_extra_logical_descriptor(self):
        source = DDI.read_text()
        walk = block(source, 'static HRESULT SetStageHeapWalk(AGX_D3D12_OWNER *owner, unsigned stage, const AgxD3d12Dispatch *b, AgxD3d12HeapScratch **scratch,\n                                bool *minmax, JobRangeSink table) {')
        self.assertIn('DescriptorHeapBytes(b->descriptors)', walk)
        self.assertIn('DescriptorHeapBytes(b->sampler_descriptors)', walk)
        for marker in ('static AgxD3d12Heap *HeapAt(', 'static AgxD3d12Heap *MirroredHeapAt(', 'static void MirrorHeapForms('):
            self.assertIn('DescriptorHeapBytes(heap)', block(source, marker))

    def test_private_map_identity_and_fallback(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A WC, stale,
        interior, unmapped or foreign pointer must keep its cached shadow;
        only the actual live T13 map can safely own descriptors directly.
        """
        source = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        actual = block(source, 'extern "C" BOOL AgxD3d12OwnerCpuMapIsPrivate(')
        fixture = r'''
#include <cassert>
using BOOL=bool;using UINT=unsigned;
#define TRUE true
#define FALSE false
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(r) 3
struct Slot {bool Active,Transition,HeapStaging,Mapped;void *PrivateStaging,*LockedBase;};
struct AGX_D3D12_OWNER {struct {int ScreenBufferLock; Slot ScreenBuffers[3];} Runtime;};
struct OwnerLock {explicit OwnerLock(AGX_D3D12_OWNER *){}};
static int locks;
static void AcquireSRWLockShared(int *){assert(!locks);++locks;}
static void ReleaseSRWLockShared(int *){assert(locks==1);--locks;}
/* QUERY */
int main(){
 char cpu[2]={};AGX_D3D12_OWNER o={};
 assert(!AgxD3d12OwnerCpuMapIsPrivate(nullptr,cpu));
 assert(!AgxD3d12OwnerCpuMapIsPrivate(&o,nullptr));
 assert(!AgxD3d12OwnerCpuMapIsPrivate(&o,cpu));
 auto &s=o.Runtime.ScreenBuffers[2];s={true,false,true,true,cpu,cpu};
#ifdef APPLE_AGX_GPUVA_WINSYS
 assert(AgxD3d12OwnerCpuMapIsPrivate(&o,cpu));
 assert(!AgxD3d12OwnerCpuMapIsPrivate(&o,cpu+1));
 s.Active=false;assert(!AgxD3d12OwnerCpuMapIsPrivate(&o,cpu));s.Active=true;
 s.Transition=true;assert(!AgxD3d12OwnerCpuMapIsPrivate(&o,cpu));s.Transition=false;
 s.HeapStaging=false;assert(!AgxD3d12OwnerCpuMapIsPrivate(&o,cpu));s.HeapStaging=true;
 s.Mapped=false;assert(!AgxD3d12OwnerCpuMapIsPrivate(&o,cpu));s.Mapped=true;
 s.PrivateStaging=nullptr;assert(!AgxD3d12OwnerCpuMapIsPrivate(&o,cpu));s.PrivateStaging=cpu;
 s.LockedBase=cpu+1;assert(!AgxD3d12OwnerCpuMapIsPrivate(&o,cpu));
#else
 assert(!AgxD3d12OwnerCpuMapIsPrivate(&o,cpu));
#endif
 assert(!locks);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = Path(tmp)/'query.cpp', Path(tmp)/'query'
            cpp.write_text(fixture.replace('/* QUERY */', actual))
            for defines in ([], ['-DAPPLE_AGX_GPUVA_WINSYS']):
                build = subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined', *defines, str(cpp), '-o', str(exe)], capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                run = subprocess.run([str(exe)], capture_output=True, text=True)
                self.assertEqual(run.returncode, 0, run.stderr)
