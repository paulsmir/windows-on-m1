"""Original CURRENT ROOT recipes for the opt-in actual-NIR map CPU gate."""
import hashlib,json,shlex
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
METADATA=ROOT/'tests/data/dxil_nir_map_host.json'
SOURCE_NAMES=[
 'drivers/apple-agx/mesa/dxil/agx_dxil_nir_map.c',
 'drivers/apple-agx/mesa/dxil/agx_dxil_nir_map.h',
 'drivers/apple-agx/mesa/dxbc/dxbc_bindings.h',
 'tests/data/dxil_nir_map_fixture.c','tests/data/dxil_nir_map_host.json',
 'tests/dxil_nir_semantic_oracle.h','tests/dxil_nir_map_source.py','tests/test_dxil_nir_map.py']
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def entry(path):
 p=Path(path);return {'bytes':p.stat().st_size,'sha256':sha(p)}
def metadata():return json.loads(METADATA.read_text())
def source_inputs():return [ROOT/n for n in SOURCE_NAMES]
def commands(out):
 return [[a.replace('@ROOT@',str(ROOT)).replace('@OUT@',str(out))for a in cmd]for cmd in metadata()['commands']]
def direct_inputs():
 result={str(p.resolve()):entry(p)for p in source_inputs()}
 for name,e in metadata()['external_inputs'].items():
  p=Path(name)
  if str(p.resolve())!=name or entry(p)!=e:raise RuntimeError('EXTERNAL_PIN_DRIFT '+name)
  result[name]=e
 return result

def dependency_inputs(path):
 text=Path(path).read_text().replace('\\\n','')
 target,colon,rest=text.partition(':')
 if not colon or not target.strip()or not rest.strip():raise RuntimeError('BAD_MMD_FILE '+str(path))
 return sorted({str(Path(n).resolve())for n in shlex.split(rest)})
