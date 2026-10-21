"""C31: the actual CreateCommandSignature layout against the pinned WDK.

WHAT REAL BUG OR INVARIANT: ExecuteIndirect reads each record's arguments at
the offsets the signature computes. D3D12 packs arguments tightly in
declaration order, so a wrong size or order reads another argument's bytes.
The ExecuteIndirect sample's root-CBV + draw record (stride 0x18) must be
admitted; unsupported argument kinds and short strides are refused.
"""
from pathlib import Path
import subprocess,tempfile,unittest
from test_d3d12_view_mapping import block
from d3d12_texture1d_source import declaration
ROOT=Path(__file__).resolve().parents[1]
DDI=ROOT/'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
FIXTURE=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#define APIENTRY
using UINT=unsigned;using SIZE_T=unsigned long;using HRESULT=int;
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_NOTIMPL (-2)
#define SUCCEEDED(x) ((x)>=0)
static constexpr UINT kRootArguments=64,kVertexBuffers=32;
struct D3D12DDI_HDEVICE{void*pDrvPrivate;};struct D3D12DDI_HCOMMANDSIGNATURE{void*pDrvPrivate;};struct D3D12DDI_HROOTSIGNATURE{void*pDrvPrivate;};
/* WDK */
static UINT last_refusal;
static void AdmissionUmdDiagnostic(const char*,HRESULT,const UINT*r,UINT n){last_refusal=n>3?r[3]:0;}
/* ACTUAL */
static HRESULT make(UINT stride,std::initializer_list<D3D12DDI_INDIRECT_ARGUMENT_DESC> args,AgxD3d12CommandSignature*sig){
 D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 a={};a.ByteStride=stride;a.NumArgumentDescs=static_cast<UINT>(args.size());a.pArgumentDescs=args.begin();
 return CreateCommandSignature(D3D12DDI_HDEVICE{},&a,D3D12DDI_HCOMMANDSIGNATURE{sig});
}
static D3D12DDI_INDIRECT_ARGUMENT_DESC arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE t,UINT p=0,UINT dest=0,UINT n=0){
 D3D12DDI_INDIRECT_ARGUMENT_DESC d={};d.Type=t;if(t==D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT){d.Constant.RootParameterIndex=p;d.Constant.DestOffsetIn32BitValues=dest;d.Constant.Num32BitValuesToSet=n;}
 else if(t==D3D12DDI_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW)d.VertexBuffer.Slot=p;else d.ConstantBufferView.RootParameterIndex=p;return d;
}
int main(){
 AgxD3d12CommandSignature s;
 // the sample: CBV root parameter 0, then a draw; 0x18 bytes
 assert(make(0x18,{arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW,0),arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW)},&s)==S_OK);
 assert(s.kind==1&&s.arguments==2&&s.record_bytes==24&&s.argument[0].offset==0&&s.argument[0].param==0&&s.argument[1].offset==8);
 // constants (3 DWORDs) pack at 4-byte granularity before a view and an indexed draw
 assert(make(56,{arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT,2,1,3),arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW,4),
                arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW),arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED)},&s)==S_OK);
 assert(s.kind==2&&s.argument[0].count==3&&s.argument[0].dest==1&&s.argument[1].offset==12&&s.argument[2].offset==20);
 assert(s.argument[3].offset==36&&s.record_bytes==56);
 // a plain draw stays the Gallium indirect path (one argument)
 assert(make(16,{arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW)},&s)==S_OK&&s.arguments==1&&s.record_bytes==16);
 // refusals: short stride, vertex buffers for a dispatch, incrementing constants, two draws, nothing
 assert(make(0x14,{arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW,0),arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW)},&s)==E_NOTIMPL&&last_refusal==5&&!s.kind);
 assert(make(64,{arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW,0),arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_DISPATCH)},&s)==E_NOTIMPL&&last_refusal==3);
 assert(make(64,{arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT),arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW)},&s)==E_NOTIMPL&&last_refusal==4);
 assert(make(64,{arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW),arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW)},&s)==E_NOTIMPL&&last_refusal==4);
 assert(make(64,{arg(D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW,0)},&s)==E_NOTIMPL&&last_refusal==1);
 std::puts("PASS C31 signature layout and refusals");
}
'''
class CommandSignature(unittest.TestCase):
    def test_actual_layout_packs_arguments_in_order(self):
        s=DDI.read_text()
        wdk='\n'.join(declaration(n) for n in ['D3D12DDI_INDIRECT_ARGUMENT_TYPE','D3D12DDI_INDIRECT_ARGUMENT_DESC','D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001'])
        actual='\n'.join([next(l for l in s.splitlines() if l.startswith('static constexpr UINT kSignatureArguments')),
                          block(s,'struct AgxD3d12SignatureArgument {')+';',block(s,'struct AgxD3d12CommandSignature {')+';',
                          block(s,'static HRESULT APIENTRY CreateCommandSignature(')])
        source=FIXTURE.replace('/* WDK */',wdk.replace('_In_reads_(NumArgumentDescs)','')).replace('/* ACTUAL */',actual)
        source='#include <initializer_list>\n'+source
        with tempfile.TemporaryDirectory(prefix='d3d12-signature-') as t:
            p=Path(t)/'sig.cpp';p.write_text(source);exe=Path(t)/'sig'
            r=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(p),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertIn('PASS C31',r.stdout)
if __name__=='__main__':unittest.main()
