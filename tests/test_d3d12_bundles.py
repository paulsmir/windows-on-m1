"""C30: the actual D3D12 bundle call log (ExecuteBundle replay) on the host.

WHAT REAL BUG OR INVARIANT: a bundle must copy what its pointer arguments
name at record time (applications reuse those arrays) and replay its calls
on the calling list's handle, in order, every time it is executed; a bundle
cannot execute a bundle and an open bundle cannot be executed.
"""
from pathlib import Path
import subprocess,tempfile,unittest
from test_d3d12_view_mapping import block
ROOT=Path(__file__).resolve().parents[1]
DDI=ROOT/'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
def actual():
    s=DDI.read_text()
    parts=[next(l for l in s.splitlines() if l.startswith('typedef void (*AgxD3d12BundleReplay)')),
           block(s,'struct AgxD3d12BundleCall {')+';',
           next(l for l in s.splitlines() if l.startswith('static constexpr SIZE_T kBundleHeader')),
           block(s,'static bool IsBundle('),block(s,'static unsigned char *BundleAppend('),
           'template<auto Fn> struct Bundled;',
           block(s,'template<class... A, VOID (APIENTRY *Fn)(D3D12DDI_HCOMMANDLIST, A...)> struct Bundled<Fn> {')+';',
           block(s,'template<class T> static const T *BundleArray('),
           block(s,'template<class T> static unsigned char *BundleAppendArray('),
           block(s,'static VOID APIENTRY BundledVertexBuffers('),
           block(s,'template<bool Graphics> static VOID APIENTRY BundledRootConstants('),
           block(s,'static VOID APIENTRY ExecuteBundle(')]
    return '\n'.join(parts)
class Bundles(unittest.TestCase):
    def test_actual_call_log_copies_and_replays_on_the_caller(self):
        source=(ROOT/'tests/d3d12_bundle_fixture.cpp').read_text().replace('/* ACTUAL_BUNDLE */',actual())
        with tempfile.TemporaryDirectory(prefix='d3d12-bundle-') as t:
            p=Path(t)/'bundle.cpp';p.write_text(source);exe=Path(t)/'bundle'
            r=subprocess.run(['clang++','-std=c++17','-Wall','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertIn('PASS C30',r.stdout)
    def test_table_routes_bundle_legal_ddis_through_the_log(self):
        s=DDI.read_text();start=s.index('static void FillD3D12DDI_COMMAND_LIST_FUNCS_3D_0003(');table=s[start:s.index('\n}\n',start)]
        for slot in ('pfnDrawInstanced','pfnDrawIndexedInstanced','pfnDispatch','pfnExecuteIndirect','pfnIaSetTopology',
                     'pfnOmSetStencilRef','pfnSetPipelineState','pfnSetGraphicsRootSignature','pfnSetGraphicsRootDescriptorTable',
                     'pfnSetGraphicsRootConstantBufferView','pfnSetComputeRootSignature','pfnSetComputeRootDescriptorTable'):
            line=next(l for l in table.splitlines() if slot+',' in l)
            self.assertIn('Bundled<',line,slot)
        for slot,fn in (('pfnIASetVertexBuffers','BundledVertexBuffers'),('pfnIASetIndexBuffer','BundledIndexBuffer'),
                        ('pfnOmSetBlendFactor','BundledBlendFactor'),('pfnSetDescriptorHeaps','BundledDescriptorHeaps'),
                        ('pfnSetGraphicsRoot32BitConstants','BundledRootConstants<true>'),('pfnExecuteBundle','ExecuteBundle')):
            self.assertIn(slot+', '+fn+')',table)
        # state a bundle inherits from its caller is never logged into it
        for slot in ('pfnOMSetRenderTargets','pfnRsSetViewports','pfnRsSetScissorRects','pfnClearRootArguments'):
            line=next(l for l in table.splitlines() if slot+',' in l)
            self.assertNotIn('Bundled',line,slot)
if __name__=='__main__':unittest.main()
