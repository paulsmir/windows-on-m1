"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

CopyResource supports the same integer/BC reinterpretation as CopySubresourceRegion.
Every mip must have the same number of stored blocks on both sides; comparing
pixel dimensions rejects legal GPU-compression output. A bad later mip must be
refused before writing any earlier mip. Microsoft D3D10.1 format-conversion rules.
"""
import unittest
from test_d3d10_copy_bc_reinterpret import SCRIPT, function, compile_run


class WholeBlockCopy(unittest.TestCase):
    def test_each_mip_has_equal_block_counts(self):
        text = SCRIPT.read_text()
        self.assertIn('\nAgxD3d10WholeBlockCopy(', text)
        helper = function(text, 'AgxD3d10WholeBlockCopy')
        compile_run(r'''
#include <cassert>
struct pipe_resource { unsigned width0,height0,last_level,format; };
static unsigned util_format_get_blockwidth(unsigned f) { return f; }
static unsigned util_format_get_blockheight(unsigned f) { return f; }
static unsigned u_minify(unsigned n,unsigned l) { return n>>l ? n>>l : 1; }
''' + helper + r'''
int main() {
 pipe_resource a={128,64,7,1},b={512,256,7,4};
 assert(AgxD3d10WholeBlockCopy(&a,&b));
 assert(AgxD3d10WholeBlockCopy(&b,&a));
 a={1,1,0,1};b={2,2,0,4};assert(AgxD3d10WholeBlockCopy(&a,&b));
 a={3,3,1,1};b={12,12,1,4};
 // Equal top-level block counts, unequal at level 1: 1 vs ceil(6/4)=2.
 assert(!AgxD3d10WholeBlockCopy(&a,&b));
 a={2,2,0,1};b={4,4,0,4};assert(!AgxD3d10WholeBlockCopy(&a,&b));
}
''')

    def test_whole_copy_preflights_then_uses_independent_mip_boxes(self):
        text = SCRIPT.read_text()
        body = text[text.index("'ResourceCopy','''"):]
        body = body[:body.index("''')")]
        self.assertIn('AgxD3d10BlockCopyCompatible(source->Format, destination->Format)', body)
        self.assertIn('AgxD3d10WholeBlockCopy(src, dst)', body)
        self.assertLess(body.index('AgxD3d10WholeBlockCopy'), body.index('AgxD3d10CopyBlocks'))
        block = body[body.index('if (blockCopy) {'):body.index('if (compressed) {')]
        for side in ('src','dst'):
            for dim in ('width0','height0','depth0'):
                self.assertIn('u_minify('+side+'->'+dim+',level)',block)
        self.assertIn('if (FAILED(status)) { SetError(hDevice, status); return; }',block)


if __name__ == '__main__':
    unittest.main()
