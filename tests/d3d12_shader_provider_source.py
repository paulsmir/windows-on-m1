"""Original CURRENT ROOT provider renderer/argv. No private source fallback."""
from pathlib import Path
import json
from d3d12_shader_owned_source import render as render_owned,source_inputs as owned_inputs,TOOL,TOOL_SHA,SDK,WDK,WDK_SHA,sha
ROOT=Path(__file__).resolve().parents[1]
def source_inputs():
 return list(dict.fromkeys(owned_inputs()+[ROOT/n for n in ['drivers/apple-agx/d3d12/agx_shader_input_provider.h','drivers/apple-agx/d3d12/agx_shader_input_provider.cpp','tests/d3d12_shader_provider_fixture.cpp','tests/d3d12_shader_provider_source.py','tests/test_d3d12_shader_provider.py']]))
def commands(receipts,generated):
 r,g=Path(receipts).resolve(),Path(generated).resolve();d=ROOT/'drivers/apple-agx/d3d12';x=ROOT/'drivers/apple-agx/mesa/dxil';l=ROOT/'drivers/apple-agx/mesa/dxbc'
 flags=['-isysroot',str(SDK),'-fsanitize=address,undefined','-fno-omit-frame-pointer']
 return [[str(TOOL/'clang'),'-std=c11',*flags,'-c',str(l/'agx_legacy_program_frame.c'),'-o',str(r/'legacy.o')],
 [str(TOOL/'clang'),'-std=c11',*flags,'-c',str(x/'agx_dxil_program.c'),'-o',str(r/'dxil.o')],
 [str(TOOL/'clang++'),'-std=c++17',*flags,'-I',str(g/'host-sdk'),'-I',str(d),'-I',str(x),'-I',str(l),str(d/'agx_d3d12_shader_owned_core.cpp'),str(d/'agx_d3d12_zero_cache_library.cpp'),str(d/'agx_shader_input_provider.cpp'),str(ROOT/'tests/d3d12_shader_provider_fixture.cpp'),str(r/'legacy.o'),str(r/'dxil.o'),'-o',str(r/'provider-fixture')],
 [str(r/'provider-fixture'),str(g/'exp1397.program'),str(g/'legacy-programs/cs50.program')]]
def render(out):
 out=Path(out);render_owned(out)
 # All inputs resolved from current checked-out ROOT. Missing provider/module/data
 # is an error; never import a prepared candidate or archived generated fallback.
 inputs={str(p.resolve()):{'bytes':p.stat().st_size,'sha256':sha(p)}for p in source_inputs()}
 generated={str(p.relative_to(out)):{'bytes':p.stat().st_size,'sha256':sha(p)}for p in sorted(out.rglob('*'))if p.is_file()}
 manifest={'scope':'CURRENT ROOT actual modules+existing actual WDK data renderer; host aliases NOT native ABI; opaque-root caller-serialized CPU only','inputs':inputs,'generated':generated}
 (out/'provider-generated-inputs.json').write_text(json.dumps(manifest,indent=2)+'\n');return manifest
