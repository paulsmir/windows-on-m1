"""Execute the production D3D12 stream-output declaration translation (C39).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The owner turns the DDI's
D3D12DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY list into the translator's xfb
outputs. A wrong component run, a hole that emits an output, offsets that
do not accumulate per output slot, or a stride smaller than the slot's
data would capture the wrong varyings or overlap vertices in the buffer;
declarations the translator cannot express (streams other than 0, a
non-contiguous mask, unaligned strides) must be refused, not truncated.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from test_d3d12_view_mapping import block

ROOT = Path(__file__).resolve().parents[1]
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
HEADER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h'
DXBC = ROOT / 'drivers/apple-agx/mesa/dxbc/dxbc_to_nir.h'

FIXTURE = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
/* TYPES */
/* PRODUCTION */
static AGX_D3D12_SO_DECL decl(unsigned entries) { AGX_D3D12_SO_DECL d; memset(&d, 0, sizeof(d)); d.Entries = entries; return d; }
int main() {
    struct dxbc_nir_xfb x;
    /* position.xyzw to slot 0, a 2-component hole, color.yz to slot 0, a
     * texcoord.xy to slot 1: offsets accumulate per slot, holes only advance */
    AGX_D3D12_SO_DECL d = decl(4);
    d.Entry[0] = {0, 0, 0, 0xf};
    d.Entry[1] = {0, 0, 0xffffffffu, 0x3};
    d.Entry[2] = {0, 0, 1, 0x6};
    d.Entry[3] = {0, 1, 2, 0x3};
    assert(so_xfb(&d, &x));
    assert(x.num_outputs == 3);
    assert(x.outputs[0].reg == 0 && x.outputs[0].start_component == 0 && x.outputs[0].num_components == 4 &&
           x.outputs[0].buffer == 0 && x.outputs[0].dst_offset == 0);
    assert(x.outputs[1].reg == 1 && x.outputs[1].start_component == 1 && x.outputs[1].num_components == 2 &&
           x.outputs[1].buffer == 0 && x.outputs[1].dst_offset == 6);
    assert(x.outputs[2].reg == 2 && x.outputs[2].num_components == 2 && x.outputs[2].buffer == 1 &&
           x.outputs[2].dst_offset == 0);
    assert(x.stride[0] == 8 && x.stride[1] == 2 && x.stride[2] == 0);
    /* DDI byte strides win where given */
    d.NumStrides = 2; d.Strides[0] = 48; d.Strides[1] = 8;
    assert(so_xfb(&d, &x) && x.stride[0] == 12 && x.stride[1] == 2);
    /* a stride smaller than its slot's data, or unaligned, is refused */
    d.Strides[0] = 16; assert(!so_xfb(&d, &x));
    d.Strides[0] = 50; assert(!so_xfb(&d, &x));
    d.Strides[0] = 48;
    /* streams other than 0, slots past 3, non-contiguous masks, an empty
     * non-hole component run and too many entries are refused */
    AGX_D3D12_SO_DECL e = d; e.Entry[0].Stream = 1; assert(!so_xfb(&e, &x));
    e = d; e.Entry[0].Slot = 4; assert(!so_xfb(&e, &x));
    e = d; e.Entry[0].Mask = 0x5; assert(!so_xfb(&e, &x));
    e = d; e.Entry[0].Mask = 0; assert(!so_xfb(&e, &x));
    e = d; e.Entries = AGX_D3D12_SO_ENTRIES + 1; assert(!so_xfb(&e, &x));
    /* the rasterized stream is 0 or none */
    e = d; e.Rasterized = 0xffffffffu; assert(so_xfb(&e, &x));
    e = d; e.Rasterized = 1; assert(!so_xfb(&e, &x));
    /* only holes: nothing to capture */
    e = decl(1); e.Entry[0] = {0, 0, 0xffffffffu, 0xf}; assert(!so_xfb(&e, &x));
    return 0;
}
'''


class StreamOutput(unittest.TestCase):
    def test_declaration_translation(self):
        header, dxbc, owner = HEADER.read_text(), DXBC.read_text(), OWNER.read_text()
        types = re.search(r'#define AGX_D3D12_SO_ENTRIES \d+', header)[0] + '\n'
        types += block(header, 'typedef struct AGX_D3D12_SO_DECL {') + ' AGX_D3D12_SO_DECL;\n'
        types += re.search(r'#define DXBC_NIR_MAX_XFB_OUTPUTS \d+', dxbc)[0] + '\n'
        types += re.search(r'#define DXBC_NIR_MAX_XFB_BUFFERS \d+', dxbc)[0] + '\n'
        types += block(dxbc, 'struct dxbc_nir_xfb_output {') + ';\n' + block(dxbc, 'struct dxbc_nir_xfb {') + ';\n'
        code = block(owner, 'static bool so_xfb(')
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'C++ compiler required')
        with tempfile.TemporaryDirectory(prefix='d3d12-so-') as tmp:
            source, binary = Path(tmp) / 'so.cpp', Path(tmp) / 'so'
            source.write_text(FIXTURE.replace('/* TYPES */', types).replace('/* PRODUCTION */', code))
            build = subprocess.run([compiler, '-std=c++17', '-fsanitize=address,undefined', str(source), '-o', str(binary)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
