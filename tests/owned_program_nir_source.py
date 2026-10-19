"""Original CURRENT-source owned PROGRAM/NIR CPU recipes. No private fallback."""
import hashlib,json,pathlib
from pathlib import Path
import d3d12_shader_concurrent_provider_source as provider
import dxil_checked_owned_source as checked
import dxil_nir_map_source as nir_recipe
ROOT=Path(__file__).resolve().parents[1]
MODULE=ROOT/'drivers/apple-agx/mesa/dxil'
NAMES=['drivers/apple-agx/mesa/dxil/'+n for n in ['agx_owned_program_nir.h','agx_owned_program_nir.cpp','agx_owned_program_nir_native.h','agx_owned_program_nir_native.c','agx_owned_nir_pipe.h','agx_owned_nir_pipe.c']]+['tests/'+n for n in ['d3d12_owned_program_nir_fixture.cpp','owned_program_nir_inspect.c','owned_program_nir_source.py','test_owned_program_nir.py']]
LINK_STDERR=b"ld: warning: ignoring duplicate libraries: '-lc++'\n"
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def entry(p):p=Path(p);return {'bytes':p.stat().st_size,'sha256':sha(p)}
def source_inputs():
 return list(dict.fromkeys([ROOT/n for n in NAMES]+[checked.ROOT/'drivers/apple-agx/mesa/dxbc'/n for n in ['dxbc_to_nir.c','dxbc_parse.c']]+provider.source_inputs()+checked.source_inputs()+nir_recipe.source_inputs()))
def render(generated):
 # Existing CURRENT renderer owns fresh-directory creation and actual WDK/PROGRAM generation.
 return provider.render(generated)
def commands(receipts,generated):
 out,g=Path(receipts).resolve(),Path(generated).resolve();external=json.loads(checked.METADATA.read_text())
 template=nir_recipe.commands(out)[0];cflags=template[1:template.index('-MMD')]+['-I',str(MODULE)]
 common=['-arch','arm64','-isysroot',external['sdk'],'-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 current=checked.ROOT/'drivers/apple-agx';dxil=current/'mesa/dxil';dxbc=current/'mesa/dxbc';d3d=current/'d3d12'
 cs=[];objects=[]
 def unit(name,source,flags,cpp=False):
  obj=out/(name+'.o');objects.append(str(obj));cs.append({'label':name,'argv':[external['cxx']if cpp else external['cc'],*flags,'-MD','-MF',str(out/(name+'.d')),'-c',str(source),'-o',str(obj)]})
 for name,source in [('frontend',MODULE/'agx_owned_program_nir_native.c'),('inspect',ROOT/'tests/owned_program_nir_inspect.c'),('map',dxil/'agx_dxil_nir_map.c'),('dxbc',dxbc/'dxbc_to_nir.c'),('parse',dxbc/'dxbc_parse.c')]:unit(name,source,cflags)
 # The opaque wrapper has a strong helper reference; every bridge link must
 # include the real clone/consume helper, even without callback-fixture calls.
 _,external_pins=checked.expected_inputs()
 contexts=[Path(n)for n in external_pins if n.endswith('/pipe/p_context.h')]
 if len(contexts)!=1:raise RuntimeError('EXACT_PINNED_GALLIUM_CONTEXT_HEADER_REQUIRED')
 unit('handoff',MODULE/'agx_owned_nir_pipe.c',cflags+['-I',str(contexts[0].parent.parent)])
 small=['-std=c11',*common,'-I',str(dxil),'-I',str(dxbc),'-I',external['external_source']]
 for name,source in [('program',dxil/'agx_dxil_program.c'),('legacy',dxbc/'agx_legacy_program_frame.c'),('owned',dxil/'agx_dxil_owned.c')]:unit(name,source,small)
 cpp=['-std=c++17',*common,'-fexceptions','-I',str(g/'host-sdk'),'-I',str(d3d),'-I',str(dxil),'-I',str(dxbc),'-I',str(MODULE)]
 for name,source in [('core',d3d/'agx_d3d12_shader_owned_core.cpp'),('library',d3d/'agx_d3d12_zero_cache_library.cpp'),('provider',d3d/'agx_shader_input_provider.cpp'),('bridge',MODULE/'agx_owned_program_nir.cpp'),('fixture',ROOT/'tests/d3d12_owned_program_nir_fixture.cpp')]:unit(name,source,cpp,True)
 unit('facade',dxil/'agx_dxil_checked.cpp',['-std=c++14',*common,'-fexceptions','-I',str(dxil),'-I',external['external_source']],True)
 link=nir_recipe.commands(out)[2];libs=link[link.index('-o')+2:]
 cs.append({'label':'link','argv':[external['cxx'],*common,'-fexceptions',*objects,*external['archives'],*libs,'-o',str(out/'owned-program-nir-fixture')]})
 cs.append({'label':'run','argv':[str(out/'owned-program-nir-fixture'),str(g/'exp1397.program'),str(g/'legacy-programs/cs50.program'),str(g/'legacy-programs/vs40.program'),str(g/'legacy-programs/ps40.program'),nir_recipe.commands(out)[3][-1]]})
 return cs

def expected_inputs(generated):
 _,pins=checked.expected_inputs();pins.update(nir_recipe.direct_inputs())
 for q in source_inputs():pins[str(q.resolve())]=entry(q)
 for directory in [checked.ROOT/'drivers/apple-agx/mesa/dxil',checked.ROOT/'drivers/apple-agx/mesa/dxbc',checked.ROOT/'drivers/apple-agx/d3d12']:
  for q in directory.glob('*.h'):pins[str(q.resolve())]=entry(q)
 for q in Path(generated).rglob('*'):
  if q.is_file():pins[str(q.resolve())]=entry(q)
 for q in Path(nir_recipe.commands(Path(generated))[3][-1]).iterdir():
  if q.is_file():pins[str(q.resolve())]=entry(q)
 canonical={}
 for name,value in pins.items():
  key=str(Path(name).resolve())
  if key in canonical and canonical[key]!=value:raise RuntimeError('CONFLICTING_CANONICAL_PINS '+key)
  canonical[key]=value
 return canonical
