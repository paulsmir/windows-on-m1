"""Original actual-module renderer; data only, never invokes a compiler/tool."""
from pathlib import Path
import hashlib,json,re
from d3d12_zero_cache_library_source import render as render_library, WDK as LIBRARY_WDK
ROOT=Path(__file__).resolve().parents[1]
WDK=ROOT/'.local/reference/wdk26100/d3d12umddi.h'
WDK_SHA='d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28'
PROGRAM_SHA='b22b71c9856cd3f2514a98609331b9ee4ab2b5e83aa8086d148243c277d52de5'
TOOL_SHA='1590ac950a3d627817d09ade5cb60b2115f17a72182a3141e010b4bcc482a0c9'
TOOL=Path('/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin')
SDK=Path('/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def source_inputs():
 return [ROOT/x for x in [
 'drivers/apple-agx/d3d12/agx_d3d12_shader_owned_core.cpp','drivers/apple-agx/d3d12/agx_d3d12_shader_owned_core.h',
 'drivers/apple-agx/d3d12/agx_d3d12_zero_cache_library.cpp','drivers/apple-agx/d3d12/agx_d3d12_zero_cache_library.h',
 'drivers/apple-agx/mesa/dxil/agx_dxil_program.c','drivers/apple-agx/mesa/dxil/agx_dxil_program.h',
 'tests/d3d12_shader_owned_fixture.cpp','tests/d3d12_shader_owned_source.py','tests/test_d3d12_shader_owned.py',
 'tests/d3d12_zero_cache_library_source.py','tests/d3d12_zero_cache_host_windows.h',
 'tests/fixtures/dxil/exp1397_spaces_cs60.dxil']]+[WDK,LIBRARY_WDK]
def commands(receipts,generated):
 r,g=Path(receipts),Path(generated);flags=['-isysroot',str(SDK),'-fsanitize=address,undefined','-fno-omit-frame-pointer']
 d=ROOT/'drivers/apple-agx/d3d12';p=ROOT/'drivers/apple-agx/mesa/dxil'
 return [[str(TOOL/'clang'),'-std=c11',*flags,'-c',str(p/'agx_dxil_program.c'),'-o',str(r/'parser.o')],
 [str(TOOL/'clang++'),'-std=c++17',*flags,'-I',str(g/'host-sdk'),'-I',str(d),'-I',str(p),str(d/'agx_d3d12_shader_owned_core.cpp'),str(d/'agx_d3d12_zero_cache_library.cpp'),str(ROOT/'tests/d3d12_shader_owned_fixture.cpp'),str(r/'parser.o'),'-o',str(r/'fixture')],
 [str(r/'fixture'),str(g/'exp1397.program')]]
def render(out):
 out=Path(out);assert sha(WDK)==WDK_SHA
 # Existing library helper emits actual WDK library declarations and original shim.
 render_library(out)
 text=WDK.read_text();names=['D3D12DDI_CREATE_SHADER_FLAGS','D3D12DDIARG_SIGNATURE_ENTRY','D3D12DDIARG_STAGE_IO_SIGNATURES','D3D12DDIARG_CREATE_SHADER_0010'];records={}
 for name in names:
  m=re.search(r'typedef (?:struct|enum) '+name+r'\s*\{.*?\}\s*'+name+r'\s*;',text,re.S)
  if not m:raise RuntimeError('actual WDK declaration missing '+name)
  records[name]=m.group()
 sdk=out/'host-sdk';q=sdk/'windows.h';q.write_text(q.read_text()+'''\nusing UINT8=uint8_t;using BYTE=uint8_t;
using D3D10_SB_NAME=unsigned;using D3D10_SB_REGISTER_COMPONENT_TYPE=unsigned;using D3D11_SB_OPERAND_MIN_PRECISION=unsigned;
struct D3D12DDI_HROOTSIGNATURE {void *pDrvPrivate;};struct D3D12DDI_HSHADER {void *pDrvPrivate;};
struct D3D12DDIARG_SIGNATURE_ENTRY_0012;struct D3D12DDIARG_TESSELLATION_IO_SIGNATURES;
''')
 q=sdk/'d3d12umddi.h';q.write_text(q.read_text()+'\n'+'\n'.join(records.values()))
 base=out/'generated-inputs.json';base_meta=json.loads(base.read_text());base_meta['scope']='Actual WDK library + shader declarations with original host aliases; NOT Windows ABI';base_meta['generated_files']={str(p.relative_to(out)):sha(p)for p in sorted(sdk.iterdir())};base.write_text(json.dumps(base_meta,indent=2)+'\n')
 fixture=ROOT/'tests/fixtures/dxil/exp1397_spaces_cs60.dxil';assert sha(fixture)==PROGRAM_SHA;data=fixture.read_bytes();u=lambda n:int.from_bytes(data[n:n+4],'little');programs=[]
 for i in range(u(28)):
  at=u(32+4*i)
  if data[at:at+4]==b'DXIL':programs.append(data[at+8:at+8+u(at+4)])
 assert len(programs)==1 and len(programs[0])==1680;(out/'exp1397.program').write_bytes(programs[0])
 (out/'actual-shader-wdk-records.json').write_text(json.dumps(records,indent=2)+'\n')
 manifest={'scope':'actual current modules/WDK records; host aliases NOT Windows ABI; framing/storage NOT IR validity',
 'inputs':{str(p):{'bytes':p.stat().st_size,'sha256':sha(p)}for p in source_inputs()},
 'generated':{str(p.relative_to(out)):{'bytes':p.stat().st_size,'sha256':sha(p)}for p in sorted(out.rglob('*'))if p.is_file()and p.name!='shader-generated-inputs.json'}}
 (out/'shader-generated-inputs.json').write_text(json.dumps(manifest,indent=2)+'\n');return manifest
