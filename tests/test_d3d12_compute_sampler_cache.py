"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Repeated dispatches must reuse immutable sampler objects, still bind/unbind on
every call, and free cached state once on owner teardown. Changed keys and failed
creation must never return stale state. Batch-relative heaps remain uncached.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_owner_allocation_failure import OWNER, PRELUDE, function

class ComputeSamplerCache(unittest.TestCase):
    def test_dispatch_reuses_state_and_cleans_up(self):
        source=OWNER.read_text()
        self.assertIn('compute_sampler(',source,'Dispatch still allocates/deletes every sampler')
        header=OWNER.with_suffix('.h').read_text()
        end=header.index('} AGX_D3D12_SAMPLER;')+len('} AGX_D3D12_SAMPLER;')
        sampler_type=header[header.rfind('typedef struct {',0,end):end]
        pre=PRELUDE.replace('struct AGX_D3D12_SAMPLER {unsigned Valid;};',sampler_type)
        code=pre+function(source,'static void *compute_sampler(')+function(source,'static void release_compute_samplers(')+function(source,'extern "C" HRESULT AgxD3d12OwnerDispatch(')+r'''
int main(){
 pipe_context pipe;AGX_D3D12_OWNER owner={&pipe,0};
 AGX_D3D12_COMPUTE compute={&pipe,{1,1,1},{},{}};
 AGX_D3D12_SAMPLER samplers[4]={{1,1},{0,0},{1,3},{0,0}};
 AGX_D3D12_UAV uavs[4];AGX_D3D12_CBV cbvs[4];AGX_D3D12_SRV srvs[4];
 for(unsigned i=0;i<149;i++)
  assert(AgxD3d12OwnerDispatch(&owner,&compute,uavs,cbvs,srvs,samplers,1,1,1,nullptr)==S_OK);
 assert(attempts==2 && live==2 && encoded==149 && bound==298);
 samplers[0].Filter=2;
 assert(AgxD3d12OwnerDispatch(&owner,&compute,uavs,cbvs,srvs,samplers,1,1,1,nullptr)==S_OK);
 assert(attempts==3 && live==2);
 nth=4;samplers[0].Filter=4;
 assert(AgxD3d12OwnerDispatch(&owner,&compute,uavs,cbvs,srvs,samplers,1,1,1,nullptr)==E_OUTOFMEMORY);
 assert(encoded==150 && live==2);
 nth=0;
 assert(AgxD3d12OwnerDispatch(&owner,&compute,uavs,cbvs,srvs,samplers,1,1,1,nullptr)==S_OK);
 samplers[0].Valid=samplers[2].Valid=0;
 assert(AgxD3d12OwnerDispatch(&owner,&compute,uavs,cbvs,srvs,samplers,1,1,1,nullptr)==S_OK);
 release_compute_samplers(&owner);assert(!live);
 release_compute_samplers(&owner);assert(!live);
 // Every descriptor byte participates: no untested field may yield a false hit.
 AGX_D3D12_SAMPLER key={};key.Valid=1;
 for(unsigned byte=0;byte<sizeof(key);byte++) {
  void *original=compute_sampler(&owner,0,&key);assert(original);
  unsigned before=attempts;
  ((unsigned char *)&key)[byte]^=1;
  assert(compute_sampler(&owner,0,&key));assert(attempts==before+1);
 }
 release_compute_samplers(&owner);assert(!live);

}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.cpp').write_text(code)
            z=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)

if __name__=='__main__': unittest.main()
