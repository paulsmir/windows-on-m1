import copy,json,subprocess,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CONTRACT=ROOT/'drivers/apple-agx/mesa/d3d10umd/required-formats-fl10.json'
VALIDATOR=ROOT/'tools/verify_apple_agx_required_formats.py'
class RequiredFormatsTests(unittest.TestCase):
 def run_value(self,value):
  import tempfile
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'formats.json';p.write_text(json.dumps(value))
   return subprocess.run([sys.executable,'-B',str(VALIDATOR),'--contract',str(p),'--root',str(ROOT)],text=True,capture_output=True)
 def test_repository_contract(self):
  r=subprocess.run([sys.executable,'-B',str(VALIDATOR),'--contract',str(CONTRACT),'--root',str(ROOT)],text=True,capture_output=True)
  self.assertEqual(r.returncode,0,r.stderr);self.assertEqual(json.loads(r.stdout)['formats'],93)
 def test_missing_format_rejected(self):
  v=json.loads(CONTRACT.read_text());v['entries'].pop();r=self.run_value(v)
  self.assertNotEqual(r.returncode,0);self.assertIn('format set',r.stderr)
 def test_optional_policy_rejected(self):
  v=json.loads(CONTRACT.read_text());v['optional_msaa']=True;r=self.run_value(v)
  self.assertNotEqual(r.returncode,0);self.assertIn('optional policy',r.stderr)
 def test_unproven_lowering_rejected(self):
  v=json.loads(CONTRACT.read_text());v['entries'][1]['representations']=['invented-lowered'];r=self.run_value(v)
  self.assertNotEqual(r.returncode,0);self.assertIn('lowered set',r.stderr)
if __name__=='__main__':unittest.main()
