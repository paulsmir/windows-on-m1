"""Original CURRENT ROOT checked-facade recipe. No external build/fetch/fallback."""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
METADATA=ROOT/'tests/dxil_checked_composite_pins.json'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def source_inputs():
 names=['drivers/apple-agx/mesa/dxil/'+n for n in ['agx_dxil_owned.c','agx_dxil_owned.h','agx_dxil_checked.cpp','agx_dxil_checked.h','agx_dxil_program.c','agx_dxil_program.h']]
 names+=['drivers/apple-agx/mesa/dxbc/dxbc_bindings.h','tests/dxil_owned_fixture.c','tests/dxil_owned_mixed_fixture.c','tests/dxil_profile_agreement_fixture.c','tests/fixtures/dxil/exp1389_cs60.dxil','tests/fixtures/dxil/exp1397_spaces_cs60.dxil','tests/fixtures/dxil/exp1532_cs61.dxil','tests/dxil_checked_composite_pins.json','tests/dxil_checked_owned_source.py','tests/test_dxil_checked_owned.py']
 return [ROOT/n for n in names]
def expected_inputs():
 m=json.loads(METADATA.read_text())
 for n,h in m['project_expected'].items():
  if sha(ROOT/n)!=h:raise RuntimeError('EXACT_REVIEWED_PROJECT_SOURCE_REQUIRED '+n)
 pins=dict(m['external_pins'])
 for n,e in pins.items():
  p=Path(n)
  if p.stat().st_size!=e['bytes'] or sha(p)!=e['sha256']:raise RuntimeError('COMPOSITE_DEPENDENCY_DRIFT '+n)
 headers=json.loads(Path(m['header_inventory']).read_text())['headers']
 pins.update(headers)
 for p in source_inputs():pins[str(p)]={'bytes':p.stat().st_size,'sha256':sha(p)}
 return m,pins
def commands(out):
 m=json.loads(METADATA.read_text());out=Path(out).resolve();d=ROOT/'drivers/apple-agx/mesa/dxil';flags=['-arch','arm64','-isysroot',m['sdk'],'-g','-O1','-fsanitize=address,undefined'];args=[]
 units=[('program',d/'agx_dxil_program.c',False),('owned',d/'agx_dxil_owned.c',False),('facade',d/'agx_dxil_checked.cpp',True),('one',ROOT/'tests/dxil_owned_fixture.c',False),('mixed',ROOT/'tests/dxil_owned_mixed_fixture.c',False),('profile',ROOT/'tests/dxil_profile_agreement_fixture.c',False)]
 for name,path,cpp in units:
  argv=[m['cxx']if cpp else m['cc'],'-std=c++14'if cpp else'-std=c11',*flags]
  if cpp:argv+=['-fexceptions']
  if name=='profile':argv+=['-DAGX_PROFILE_QUERY_AVAILABLE=1']
  argv+=['-I',str(d),'-I',m['external_source'],'-MD','-MF',str(out/(name+'.d')),'-c',str(path),'-o',str(out/(name+'.o'))]
  args.append({'label':name+'-compile','argv':argv})
 for name,fixture in [('one','exp1389_cs60.dxil'),('mixed','exp1397_spaces_cs60.dxil')]:
  args.append({'label':name+'-link','argv':[m['cxx'],'-arch','arm64','-isysroot',m['sdk'],'-fexceptions','-fsanitize=address,undefined',*[str(out/(x+'.o'))for x in ['program','owned','facade',name]],*m['archives'],'-o',str(out/name)]})
  argv=[str(out/name),str(ROOT/'tests/fixtures/dxil'/fixture)]
  if name=='mixed':argv +=[str(out/'mixed.spv'),str(out/'mixed-map.json')]
  args.append({'label':name+'-nominal','argv':argv})
 args.append({'label':'profile-link','argv':[m['cxx'],'-arch','arm64','-isysroot',m['sdk'],'-fexceptions','-fsanitize=address,undefined',*[str(out/(x+'.o'))for x in ['program','owned','facade','profile']],*m['archives'],'-o',str(out/'profile')]})
 args.append({'label':'profile-nominal','argv':[str(out/'profile'),str(ROOT/'tests/fixtures/dxil/exp1389_cs60.dxil'),str(ROOT/'tests/fixtures/dxil/exp1532_cs61.dxil')]})
 return args
