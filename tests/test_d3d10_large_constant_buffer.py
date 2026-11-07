"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

D3D11.1 allows constant-buffer allocations larger than the 64-KiB shader
window. The generated CreateResource previously rejected them before Asahi
allocation. Exclusive CONSTANT_BUFFER binding and 16-byte size alignment
remain required; resource-wide size limits still apply.
"""
import unittest
from test_d3d10_copy_bc_reinterpret import SCRIPT, compile_run


class LargeConstantBuffer(unittest.TestCase):
    def test_actual_creation_predicate_separates_storage_from_window(self):
        text = SCRIPT.read_text()
        start = text.index('(!wantsConstant || (pCreateResource->BindFlags ==')
        end = text.index('AgxD3d10WindowsIdentity', start)
        predicate = text[start:end].rstrip().removesuffix('&&').rstrip()
        compile_run(r'''
#include <cassert>
typedef unsigned UINT;
#define D3D10_DDI_BIND_CONSTANT_BUFFER 4u
struct Create { UINT BindFlags; };
struct Mip { UINT TexelWidth; };
static bool admit(UINT width,UINT binds) {
 Create desc={binds};Create *pCreateResource=&desc;
 Mip mip={width};Mip *resourceMip=&mip;
 bool wantsConstant=(binds & D3D10_DDI_BIND_CONSTANT_BUFFER)!=0;
 return ''' + predicate + r''';
}
int main() {
 assert(admit(16,4));assert(admit(65536,4));
 assert(admit(65552,4));assert(admit(1024*1024,4));
 assert(!admit(65537,4));assert(!admit(65536,4|8));
}
''')
        create = text[text.index('AgxD3d10WindowsDiagnosticResource("frontend-create"'):]
        self.assertLess(create.index('AgxD3d10ResourceWithinRequiredLimits(pCreateResource)'),
                        create.index('bool bufferShape'))


if __name__ == '__main__': unittest.main()
