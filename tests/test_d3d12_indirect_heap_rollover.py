"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Asahi's CDM-overfull flush can end any launch in a multi-record indirect
Dispatch. Every next record must bind its resources and batch-relative sampler
handles to the newly selected batch before encoding; stop on a failed rebind.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_owner_allocation_failure import PRELUDE, OWNER, function, dispatch_helpers

class IndirectHeapRollover(unittest.TestCase):
    def test_each_launch_rebinds_after_cdm_flush(self):
        pre=PRELUDE.replace('static bool faulted, launch_fault;',
            'static bool faulted, launch_fault;static unsigned batch,heap_batch,binds,fail_bind;')
        pre=pre.replace('++encoded;if(launch_fault)faulted=true;',
            'assert(heap_batch==batch);++encoded;++batch;if(launch_fault)faulted=true;')
        pre=pre.replace('return faulted?DXGI_ERROR_DEVICE_HUNG:S_OK;',
            'if(++binds==fail_bind)return E_OUTOFMEMORY;heap_batch=batch;return faulted?DXGI_ERROR_DEVICE_HUNG:S_OK;')
        code=pre+dispatch_helpers(OWNER.read_text())+function(OWNER.read_text(),'extern "C" HRESULT AgxD3d12OwnerDispatch(')+r'''
int main(){
 for(fail_bind=0;fail_bind<4;++fail_bind){
  batch=1;heap_batch=binds=encoded=0;
  pipe_context pipe;AGX_D3D12_OWNER owner={&pipe,0};
  AGX_D3D12_COMPUTE compute={&pipe,{1,1,1},{},{}};
  AGX_D3D12_SAMPLER samplers[4]={};AGX_D3D12_UAV uavs[4];AGX_D3D12_CBV cbvs[4];AGX_D3D12_SRV srvs[4];
  pipe_resource resource;AGX_D3D12_BUFFER buffer={&resource,64};
  AGX_D3D12_INDIRECT indirect={&buffer,nullptr,4,12,0};
  HRESULT hr=AgxD3d12OwnerDispatch(&owner,&compute,uavs,cbvs,srvs,samplers,0,0,0,&indirect);
  assert(hr==(fail_bind?E_OUTOFMEMORY:S_OK));
  assert(encoded==(fail_bind?fail_bind-1:4));assert(!live);
 }
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.cpp').write_text(code)
            z=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',
                              str(p/'test.cpp'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
