"""Original explicit-length DXIL framing helper against original EXP1389 bytes."""
import hashlib,os,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class DxilProgram(unittest.TestCase):
 def test_original_compute_container_and_adversarial_framing(self):
  fixture=ROOT/'tests/fixtures/dxil/exp1389_cs60.dxil'
  self.assertEqual(hashlib.sha256(fixture.read_bytes()).hexdigest(),'f3934fccef3b1428d62ed6a675540cab1bf674faaf85c67320e3ed83c6c62bb4')
  self.assertEqual(hashlib.sha256(fixture.with_suffix('.hlsl').read_bytes()).hexdigest(),'d9493d63709a0fc4a546f97cfa5033f6a7f14c90360d56aa7d119ddc188356e0')
  self.assertEqual(hashlib.sha256(fixture.read_bytes()[1548:2824]).hexdigest(),'b2c39e1fbc25150750de33858b45f7fb17091013f799c3e68eea3c017c164df7')
  with tempfile.TemporaryDirectory(prefix='agx-dxil-input-') as t:
   exe=Path(t)/'parser'
   r=subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(ROOT/'drivers/apple-agx/mesa/dxil'),str(ROOT/'tests/dxil_program_fixture.c'),str(ROOT/'drivers/apple-agx/mesa/dxil/agx_dxil_program.c'),'-o',str(exe)],capture_output=True,text=True,timeout=30)
   self.assertEqual(r.returncode,0,r.stderr)
   r=subprocess.run([str(exe),str(fixture)],capture_output=True,text=True,timeout=30);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   self.assertIn('AGX_DXIL_INPUT_PASS',r.stdout);print(r.stdout.strip())
