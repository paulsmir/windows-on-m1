"""Original CURRENT ROOT exact-field key data renderer/argv; no fallback."""
from pathlib import Path
import json,re
from d3d12_shader_owned_source import render as render_owned,TOOL,TOOL_SHA,SDK,WDK,WDK_SHA,sha
from d3d12_shader_provider_source import source_inputs as provider_inputs
ROOT=Path(__file__).resolve().parents[1]
ROOT_RECORDS=['D3D12DDI_FILTER','D3D12DDI_TEXTURE_ADDRESS_MODE','D3D12DDI_COMPARISON_FUNC','D3D12DDI_DESCRIPTOR_RANGE_TYPE','D3D12DDI_DESCRIPTOR_RANGE','D3D12DDI_ROOT_DESCRIPTOR_TABLE','D3D12DDI_ROOT_CONSTANTS','D3D12DDI_ROOT_DESCRIPTOR','D3D12DDI_SHADER_VISIBILITY','D3D12DDI_ROOT_PARAMETER_TYPE','D3D12DDI_ROOT_PARAMETER','D3D12DDI_ROOT_SIGNATURE_FLAGS','D3D12DDI_STATIC_BORDER_COLOR','D3D12DDI_STATIC_SAMPLER','D3D12DDI_ROOT_SIGNATURE','D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001']
def source_inputs():
 return list(dict.fromkeys(provider_inputs()+[ROOT/n for n in ['drivers/apple-agx/d3d12/agx_root_description_key.cpp','drivers/apple-agx/d3d12/agx_root_description_key.h','tests/data/d3d12_root_key_fixture.cpp','tests/d3d12_root_key_source.py','tests/test_d3d12_root_key.py']]))
def commands(receipts,generated):
 r,g=Path(receipts).resolve(),Path(generated).resolve();d=ROOT/'drivers/apple-agx/d3d12';x=ROOT/'drivers/apple-agx/mesa/dxil';l=ROOT/'drivers/apple-agx/mesa/dxbc';flags=['-isysroot',str(SDK),'-fsanitize=address,undefined','-fno-omit-frame-pointer']
 return [[str(TOOL/'clang'),'-std=c11',*flags,'-c',str(l/'agx_legacy_program_frame.c'),'-o',str(r/'legacy.o')],
 [str(TOOL/'clang'),'-std=c11',*flags,'-c',str(x/'agx_dxil_program.c'),'-o',str(r/'dxil.o')],
 [str(TOOL/'clang++'),'-std=c++17',*flags,'-I',str(g/'host-sdk'),'-I',str(d),'-I',str(x),'-I',str(l),str(d/'agx_root_description_key.cpp'),str(d/'agx_shader_input_provider.cpp'),str(d/'agx_d3d12_shader_owned_core.cpp'),str(d/'agx_d3d12_zero_cache_library.cpp'),str(ROOT/'tests/data/d3d12_root_key_fixture.cpp'),str(r/'legacy.o'),str(r/'dxil.o'),'-o',str(r/'key-fixture')],
 [str(r/'key-fixture'),str(g/'exp1397.program')]]
def render(out):
 out=Path(out);render_owned(out);text=WDK.read_text();records={}
 for n in ROOT_RECORDS:
  m=re.search(r'typedef (?:struct|enum) '+n+r'\s*\{.*?\}\s*'+n+r'\s*;',text,re.S)
  if not m:raise RuntimeError('actual WDK root declaration missing '+n)
  records[n]=m.group()
 p=out/'host-sdk/windows.h';p.write_text(p.read_text()+'\nusing FLOAT=float;\n');p=out/'host-sdk/d3d12umddi.h';p.write_text(p.read_text()+'\n'+'\n'.join(records.values())+'\n')
 (out/'actual-root-records.json').write_text(json.dumps({'WDK':str(WDK),'sha256':sha(WDK),'records':records,'scope':'Actual16WDKdeclarations+originalhostaliasesNOTnativeABI'},indent=2)+'\n')
 # Every required source comes from current checked-out ROOT; missing files fail.
 inputs={str(p.resolve()):{'bytes':p.stat().st_size,'sha256':sha(p)}for p in source_inputs()}
 generated={str(p.relative_to(out)):{'bytes':p.stat().st_size,'sha256':sha(p)}for p in sorted(out.rglob('*'))if p.is_file()}
 m={'scope':'CURRENT ROOT structural key only; no admission/APPEND policy/Microsoft serialization','inputs':inputs,'generated':generated}
 (out/'root-key-generated-inputs.json').write_text(json.dumps(m,indent=2)+'\n');return m
