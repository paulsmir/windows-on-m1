"""EXP1401 opt-in execution of exact actual EXP1398 NIR, not HLSL emulation."""
import hashlib,json,os,subprocess,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PINS=ROOT/'tests/dxil_nir_semantic_pins.json'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
class DxilNirSemantic(unittest.TestCase):
 def test_exact_actual_nir_against_independent_1397_vectors(self):
  receipt=os.environ.get('AGX_C15_SEMANTIC_RECEIPTS')
  if not receipt:self.skipTest('EXP1401 requires parent BEFORE plus explicit approved manifest')
  approval=Path(os.environ['AGX_C15_SEMANTIC_APPROVED_MANIFEST']);self.assertEqual(sha(approval),os.environ['AGX_C15_SEMANTIC_APPROVED_SHA256'])
  approved=json.loads(approval.read_text());self.assertEqual(sha(PINS),approved['pins_sha256'])
  for name,expected in approved['sources'].items():self.assertEqual(sha(ROOT/name),expected,name)
  pins=json.loads(PINS.read_text())
  for name,entry in pins['files'].items():
   p=Path(name);self.assertEqual(str(p.resolve()),entry['resolved_path'],name);self.assertEqual(p.stat().st_size,entry['bytes'],name);self.assertEqual(sha(p),entry['sha256'],name)
  out=Path(receipt);out.mkdir(parents=True,exist_ok=True);self.assertFalse((out/'results.json').exists(),'never overwrite checkpoint receipts')
  (out/'approved-inputs.json').write_bytes(approval.read_bytes());(out/'pins.json').write_bytes(PINS.read_bytes())
  base=(ROOT/'tests/dxil_owned_mixed_nir_fixture.c').read_text()
  self.assertEqual(base.count('assert(argc==4)'),1);self.assertEqual(base.count('final_contract(s);dump(s,argv[3]);'),1)
  generated=base.replace('static unsigned ubo_indices,', '#include "'+str(ROOT/'tests/dxil_nir_semantic_oracle.h')+'"\nstatic unsigned ubo_indices,',1).replace('assert(argc==4)','assert(argc==5)',1).replace('final_contract(s);dump(s,argv[3]);','final_contract(s);semantic_oracle(s,argv[4]);dump(s,argv[3]);',1)
  source=out/'actual-semantic-nir.c';source.write_text(generated)
  command=[]
  for arg in pins['nir_command_1398']:
   if arg.endswith('/dxil_owned_mixed_nir_fixture.c'):arg=str(source)
   elif arg.endswith('/mixed-nir-check'):arg=str(out/'semantic-nir-check')
   command.append(arg)
  commands=[command,[str(out/'semantic-nir-check'),pins['spirv'],str(out/'pre.nir'),str(out/'post.nir'),pins['vectors_dir']]]
  (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/'generated-input.json').write_text(json.dumps({'actual-semantic-nir.c':sha(source)},indent=2)+'\n')
  results=[]
  for i,cmd in enumerate(commands):
   r=subprocess.run(cmd,cwd=out,capture_output=True,text=True,timeout=30);(out/f'{i}-stdout.txt').write_text(r.stdout);(out/f'{i}-stderr.txt').write_text(r.stderr);results.append({'step':i,'returncode':r.returncode});(out/'results.json').write_text(json.dumps(results,indent=2)+'\n');self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  self.assertIn('C15_NIR_SEMANTIC_PASS lanes=192 active=173 tail=19 compared_words=384 stores=346',r.stdout)
  self.assertEqual(sha(out/'post.nir'),pins['post_nir_sha256'],'read-only interpreter must not alter original NIR')
  vectors=Path(pins['vectors_dir'])
  for actual,expected in [('actual_u0_space1.bin','expected_u0_space1.bin'),('actual_u0_space9.bin','expected_u0_space9.bin')]:self.assertEqual((out/actual).read_bytes(),(vectors/expected).read_bytes(),actual)
  artifacts={p.name:{'bytes':p.stat().st_size,'sha256':sha(p)} for p in out.iterdir() if p.is_file()};(out/'artifacts.json').write_text(json.dumps(artifacts,indent=2)+'\n');print(r.stdout.strip())
