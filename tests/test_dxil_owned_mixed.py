"""EXP1398 opt-in actual C API + actual NIR test; all dependencies enforced.

No external build, install or fetch. Requires parent-approved source manifest.
"""
import hashlib,json,os,subprocess,unittest
from pathlib import Path
from test_d3d12_view_mapping import block
ROOT=Path(__file__).resolve().parents[1]
PINS=ROOT/'tests/dxil_owned_mixed_pins.json'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
class DxilOwnedMixed(unittest.TestCase):
 def test_actual_mixed_namespace_lifetime_and_nir(self):
  receipt=os.environ.get('AGX_C15_MIXED_RECEIPTS')
  if not receipt:self.skipTest('EXP1398 compiler/run requires parent BEFORE and explicit approved manifest')
  phase=os.environ.get('AGX_C15_MIXED_PHASE','')
  self.assertIn(phase,('red','green'))
  approved=Path(os.environ['AGX_C15_MIXED_APPROVED_MANIFEST'])
  self.assertEqual(sha(approved),os.environ['AGX_C15_MIXED_APPROVED_SHA256'])
  approval=json.loads(approved.read_text());self.assertEqual(approval['phase'],phase)
  self.assertEqual(sha(PINS),approval['pins_sha256'])
  for name,expected in approval['sources'].items():self.assertEqual(sha(ROOT/name),expected,'approved original source changed: '+name)
  pins=json.loads(PINS.read_text())
  for name,entry in pins['files'].items():
   p=Path(name);self.assertEqual(p.stat().st_size,entry['bytes'],name)
   self.assertEqual(str(p.resolve()),entry['resolved_path'],name)
   self.assertEqual(sha(p),entry['sha256'],'pinned dependency changed: '+name)
  for name,expected in pins['repository_files'].items():self.assertEqual(sha(ROOT/name),expected,'pinned repository input changed: '+name)
  if phase=='red':self.assertEqual(sha(ROOT/'drivers/apple-agx/mesa/dxil/agx_dxil_owned.c'),pins['red_adapter_sha256'])
  out=Path(receipt)/phase;out.mkdir(parents=True,exist_ok=True)
  # Refuse to overwrite evidence for a prior attempt; retry uses a new receipt root.
  self.assertFalse((out/'results.json').exists(),'preserve prior receipts; preregister a new receipt root for retry')
  (out/'approved-inputs.json').write_bytes(approved.read_bytes())
  (out/'dependency-pins.json').write_bytes(PINS.read_bytes())
  driver=ROOT/'drivers/apple-agx/mesa/dxil';external=Path(pins['external_root'])
  owned=(driver/'agx_dxil_owned.c').read_text()
  actual=block(owned,'struct conversion_job {')+';\n'+block(owned,'static int earlier(')+'\n'+block(owned,'static int freeze_map(')
  generated=(ROOT/'tests/dxil_owned_permutation_fixture.c').read_text().replace('/* ACTUAL_FREEZE_HELPERS */',actual)
  (out/'actual-permutation.c').write_text(generated)
  flags=['-g','-O1','-fsanitize=address,undefined']
  includes=['-I',str(driver),'-I',str(external/'source')]
  libs=[external/'build'/p for p in pins['external_archives']]
  fixture=ROOT/'tests/fixtures/dxil/exp1397_spaces_cs60.dxil'
  commands=[]
  def compile_c(source,target):commands.append(['/usr/bin/clang','-std=c11','-Wall','-Wextra','-Werror',*flags,*includes,'-c',str(source),'-o',str(target)])
  compile_c(out/'actual-permutation.c',out/'permutation.o')
  commands.append(['/usr/bin/clang++',*flags,str(out/'permutation.o'),*map(str,libs),'-o',str(out/'permutation')])
  commands.append([str(out/'permutation'),str(fixture)])
  objs=[]
  for source,name in [(driver/'agx_dxil_program.c','program.o'),(driver/'agx_dxil_owned.c','owned.o'),(ROOT/'tests/dxil_owned_mixed_fixture.c','mixed.o')]:
   objs.append(out/name);compile_c(source,out/name)
  commands.append(['/usr/bin/clang++',*flags,*map(str,objs),*map(str,libs),'-o',str(out/'mixed-converter')])
  commands.append([str(out/'mixed-converter'),str(fixture),str(out/'mixed.spv'),str(out/'map.json')])
  nir=[]
  for arg in pins['nir_command_1389']:
   if arg=='clang':arg='/usr/bin/clang'
   elif arg.endswith('/nir-check.c'):arg=str(ROOT/'tests/dxil_owned_mixed_nir_fixture.c')
   elif arg.endswith('/nir-check'):arg=str(out/'mixed-nir-check')
   nir.append(arg)
  commands.append(nir)
  commands.append([str(out/'mixed-nir-check'),str(out/'mixed.spv'),str(out/'pre.nir'),str(out/'post.nir')])
  (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  (out/'generated-inputs.json').write_text(json.dumps({'actual-permutation.c':sha(out/'actual-permutation.c')},indent=2)+'\n')
  results=[]
  for i,command in enumerate(commands):
   r=subprocess.run(command,capture_output=True,text=True,timeout=30,cwd=out)
   (out/f'{i}-stdout.txt').write_text(r.stdout);(out/f'{i}-stderr.txt').write_text(r.stderr)
   results.append({'step':i,'returncode':r.returncode});(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
   self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  self.assertIn('C15_MIXED_NIR_PASS',r.stdout)
  self.assertTrue((out/'mixed.spv').exists());self.assertTrue((out/'map.json').exists())
  artifacts={p.name:{'bytes':p.stat().st_size,'sha256':sha(p)} for p in out.iterdir() if p.is_file()}
  (out/'artifacts.json').write_text(json.dumps(artifacts,indent=2)+'\n')
  print(r.stdout.strip())
