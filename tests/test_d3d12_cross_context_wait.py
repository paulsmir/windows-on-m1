"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A runtime queue signal must not overtake owner work. Exercise the production
callback helper and submit body with a controlled WDK boundary, including
errors, full-width values and CPU retirement fallback.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_queue_sync.h'


class CrossContextWait(unittest.TestCase):
    def test_callback_protocol(self):
        self.assertTrue(HEADER.exists(), 'cross-context completion bridge is missing')
        source = r'''
#include <cassert>
#include <cstdint>
#include <vector>
using UINT = unsigned; using UINT64 = uint64_t;
using HANDLE = void *; using D3DKMT_HANDLE = unsigned; using HRESULT = int32_t;
constexpr HRESULT S_OK=0, S_FALSE=1, E_FAIL=-1;
struct D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU {
 HANDLE hContext; UINT ObjectCount; const D3DKMT_HANDLE *ObjectHandleArray;
 const UINT64 *MonitoredFenceValueArray;
};
struct D3DDDI_DEVICECALLBACKS {
 HRESULT (*pfnWaitForSynchronizationObjectFromGpuCb)(HANDLE,const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU*);
};
static HANDLE device=(HANDLE)1, context=(HANDLE)3;
static unsigned calls;
static UINT64 value; static unsigned object;
static HRESULT result=S_OK;
static HRESULT gpu(HANDLE h,const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU *p) {
 assert(h==device && p->ObjectCount==1 && p->hContext==context);
 value=*p->MonitoredFenceValueArray;object=*p->ObjectHandleArray;++calls;return result;
}
#include "agx_d3d12_queue_sync.h"
int main() {
 D3DDDI_DEVICECALLBACKS k={gpu},missing={};
 assert(AgxD3d12QueueWaitForOwner(&k,device,context,99,0x100000002ull)==S_OK);
 assert(calls==1 && object==99 && value==0x100000002ull);
 assert(AgxD3d12QueueWaitForOwner(&k,device,context,99,UINT64_MAX)==S_OK && value==UINT64_MAX);
 assert(AgxD3d12QueueWaitForOwner(&k,device,context,99,1)==S_OK && value==1);
 // No private increment/wrap or global dedup: every queue waits on the supplied fence.
 assert(calls==3);
 assert(AgxD3d12QueueWaitForOwner(&k,device,context,99,0)==S_FALSE && calls==3);
 assert(AgxD3d12QueueWaitForOwner(&k,device,context,0,3)==S_FALSE && calls==3);
 assert(AgxD3d12QueueWaitForOwner(&missing,device,context,99,3)==S_FALSE && calls==3);
 assert(AgxD3d12QueueWaitForOwner(nullptr,device,context,99,3)==S_FALSE && calls==3);
 assert(AgxD3d12QueueWaitForOwner(&k,device,nullptr,99,3)==S_FALSE && calls==3);
 assert(AgxD3d12QueueWaitForOwner(&k,nullptr,context,99,3)==S_FALSE && calls==3);
 result=E_FAIL;assert(AgxD3d12QueueWaitForOwner(&k,device,context,99,3)==E_FAIL && calls==4);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(source)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(HEADER.parent), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_wait_uses_the_signalled_completion_timeline(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A queue must wait on the accepted render fence after staging downloads,
        not a receipt counter or the pre-download internal fence.
        """
        from test_d3d12_native_owner import body
        path = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'
        source = path.read_text()
        submit = body(source, 'static int submit(void *context,')
        self.assertLess(submit.index('pfnSubmitCommandCb('), submit.index('signal_render(device,internal)'))
        self.assertLess(submit.index('signal_render(device,internal)'), submit.index('finish_submission('))
        finish = body(source, 'static int finish_submission(')
        self.assertIn('*fence=internal;', finish)
        self.assertIn('*fence=internal+1;', finish)
        self.assertLess(finish.index('transfer_held(device,true)'), finish.index('signal_render(device,internal+1)'))
        owner = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        submit = body(owner, 'static HRESULT submit_owner(')
        self.assertIn('AgxWin32AsahiContextNewestFence(owner->Context)', submit)
        self.assertNotIn('ReportedFence +', submit)

    def test_actual_submit_retire_decision(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Execute's actual submit body must flush before arming the queue wait,
        skip retire only on S_OK, and retire/report faults on every fallback.
        """
        from test_d3d12_native_owner import body
        owner = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        function = body(owner, 'static HRESULT submit_owner(')
        source = r'''
#include <cassert>
#include <cstdint>
#include <vector>
using HRESULT=int; using UINT=unsigned; using HANDLE=void*; using LONGLONG=long long;
constexpr HRESULT S_OK=0,S_FALSE=1,E_FAIL=-1,E_INVALIDARG=-2,DXGI_ERROR_DEVICE_HUNG=-3;
struct pipe_framebuffer_state {};
struct pipe_context {
 void (*set_framebuffer_state)(pipe_context*,pipe_framebuffer_state*);
 void (*set_vertex_buffers)(pipe_context*,unsigned,void*);
 void (*flush)(pipe_context*,void*,unsigned);
};
struct AGX_D3D12_OWNER {
 pipe_context *Context; unsigned PendingDraws,PendingCopies,PendingDispatches;
 bool DrawFramebufferValid; unsigned TimedDraws,SlowDraws;
 LONGLONG TimeBind,TimeDrawVbo,TimeUnbind,TimeDrawVboIndirect,MaxDrawVbo;
 unsigned long long ReportedFence;
 void *Kernel,*RuntimeDevice;
 struct {unsigned RenderSyncObject;} Runtime;
};
struct OwnerLock { explicit OwnerLock(AGX_D3D12_OWNER*){} };
static void AgxD3d12OwnerRecordError(AGX_D3D12_OWNER *, HRESULT) {} // receipt sink
static std::vector<int> calls;
static HRESULT wait_result=S_OK;
static int retired=1,faulted=0;
static unsigned long long newest=0x100000003ull;
static LONGLONG qpc_now(){return 1;}
static UINT qpc_us(LONGLONG x){return (UINT)x;}
static void AdmissionUmdDiagnostic(const char*,HRESULT,const UINT*,UINT){}
static void receipt(const char*,HRESULT,UINT,UINT,UINT){}
static unsigned long long AgxWin32AsahiContextNewestFence(pipe_context*){return newest;}
static int AgxWin32AsahiContextFaulted(pipe_context*){return faulted;}
static int AgxWin32AsahiContextRetire(pipe_context*,unsigned ms){assert(ms==5000);calls.push_back(3);return retired;}
static HRESULT AgxD3d12QueueWaitForOwner(void*,HANDLE device,HANDLE context,unsigned object,unsigned long long value){
 assert(device==(HANDLE)1 && context==(HANDLE)2 && object==7 && value==newest);
 calls.push_back(2);return wait_result;
}
static void flush(pipe_context*,void*,unsigned){calls.push_back(1);}
''' + function + r'''
int main(){
 pipe_context pipe={nullptr,nullptr,flush};
 AGX_D3D12_OWNER owner={};owner.Context=&pipe;owner.RuntimeDevice=(HANDLE)1;owner.Runtime.RenderSyncObject=7;
 assert(submit_owner(&owner,(HANDLE)2)==S_OK);
 assert((calls==std::vector<int>{1,2}) && owner.ReportedFence==newest);
 calls.clear();wait_result=E_FAIL;
 assert(submit_owner(&owner,(HANDLE)2)==S_OK && (calls==std::vector<int>{1,2,3}));
 calls.clear();wait_result=S_FALSE;
 assert(submit_owner(&owner,(HANDLE)2)==S_OK && (calls==std::vector<int>{1,2,3}));
 calls.clear();wait_result=S_OK;
 assert(submit_owner(&owner,nullptr)==S_OK && (calls==std::vector<int>{1,3}));
 calls.clear();faulted=1;
 assert(submit_owner(&owner,(HANDLE)2)==DXGI_ERROR_DEVICE_HUNG && (calls==std::vector<int>{1,3}));
 calls.clear();faulted=0;wait_result=E_FAIL;retired=0;
 assert(submit_owner(&owner,(HANDLE)2)==DXGI_ERROR_DEVICE_HUNG && (calls==std::vector<int>{1,2,3}));
 assert(submit_owner(nullptr,(HANDLE)2)==E_INVALIDARG);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(source)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-DAPPLE_AGX_GPUVA_WINSYS',
                            str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)
            # RenderSyncObject is absent from ADMISSION_UMD_DEVICE without
            # GPUVA winsys: compile the exact fallback with that field absent.
            fallback = source[:source.index('int main()')].replace(
                'struct {unsigned RenderSyncObject;} Runtime;', '') + r'''
int main() {
 pipe_context pipe={nullptr,nullptr,flush};
 AGX_D3D12_OWNER owner={};owner.Context=&pipe;wait_result=E_FAIL;
 assert(submit_owner(&owner,(HANDLE)2)==S_OK && (calls==std::vector<int>{1,3}));
}
'''
            (path / 'fallback.cpp').write_text(fallback)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(path / 'fallback.cpp'), '-o', str(path / 'fallback')], check=True)
            subprocess.run([str(path / 'fallback')], check=True)

    def test_execute_and_retirement_integration(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        The outgoing queue context must reach the owner under its lock.
        The helper must not introduce a blocking ingress CPU wait.
        """
        from test_d3d12_native_owner import body
        ddi = (HEADER.parent / 'agx_d3d12_ddi.cpp').read_text()
        execute = body(ddi, 'static VOID APIENTRY ExecuteCommandLists(')
        self.assertNotIn('WaitForSingleObject', HEADER.read_text())
        self.assertNotIn('WaitForSynchronizationObjectFromCpu', HEADER.read_text())
        self.assertIn('AgxD3d12OwnerSubmitForQueue(owner, queue->context)', ddi)
        self.assertIn('option[0] == \'1\'', ddi)
        self.assertIn('ReplayAndSubmit(queue, count, lists)', execute)
        owner = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        submit = body(owner, 'static HRESULT submit_owner(')
        self.assertLess(submit.index('->flush('), submit.index('AgxD3d12QueueWaitForOwner('))
        self.assertIn('owner->Runtime.RenderSyncObject, fence', submit)
        self.assertIn('ordered ? 1 : AgxWin32AsahiContextRetire(', submit)
        destroy = body(owner, 'extern "C" HRESULT AgxD3d12OwnerDestroy(')
        self.assertIn('if (owner->Context)', destroy)
        self.assertLess(destroy.index('AgxWin32AsahiContextRetire('), destroy.index('AgxWin32AsahiContextDestroy('))


if __name__ == '__main__':
    unittest.main()
