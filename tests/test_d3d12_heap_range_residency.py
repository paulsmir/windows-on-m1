"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The resource heap walk must track the union of reachable root ranges, not
holes, sampler ranges, or unrelated heap tail entries. Dynamic indexing stays
conservative inside every declared range; overlap does not duplicate entries.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_root_signature_identity import block
from test_d3d12_descriptor_heap_guard import DDI

class HeapRangeResidency(unittest.TestCase):
    def test_production_range_union(self):
        s=DDI.read_text()
        marker='static bool HeapSlotReachable('
        self.assertTrue(marker in s)
        fn=block(s,marker)
        walk=block(s,'static HRESULT SetStageHeapWalk(AGX_D3D12_OWNER *owner, unsigned stage, const AgxD3d12Dispatch *b, AgxD3d12HeapScratch **scratch,\n                                bool *minmax) {')
        self.assertLess(walk.index('if (!HeapSlotReachable(b, i)) continue;'),walk.index('auto *view'))
        code=r'''
#include <cassert>
#include <climits>
using UINT=unsigned;
struct AgxD3d12Dispatch {unsigned heap_ranges;unsigned long long heap_sampler_mask;UINT heap_first[8],heap_count[8];};
'''+fn+r'''
int main(){
 AgxD3d12Dispatch b={};b.heap_ranges=5;b.heap_sampler_mask=8;
 b.heap_first[0]=3;b.heap_count[0]=2;
 b.heap_first[2]=8;b.heap_count[2]=3; // slot 1 is an empty map hole
 b.heap_first[3]=0;b.heap_count[3]=30; // sampler range is not a resource
 b.heap_first[4]=9;b.heap_count[4]=1; // overlap
 for(unsigned i=0;i<32;++i)assert(HeapSlotReachable(&b,i)==(i==3 || i==4 || (i>=8 && i<11)));
 b.heap_first[0]=UINT_MAX-1;b.heap_count[0]=2;
 assert(HeapSlotReachable(&b,UINT_MAX));assert(!HeapSlotReachable(&b,0));
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'t.cpp').write_text(code)
            z=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'t')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
