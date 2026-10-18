"""Opt-in actual shader-owned CPU modules; parent BEFORE and frozen argv required."""
import datetime,json,os,subprocess,unittest
from pathlib import Path
from d3d12_shader_owned_source import ROOT,WDK,WDK_SHA,TOOL,TOOL_SHA,SDK,commands,sha,source_inputs
class ShaderOwned(unittest.TestCase):
 def test_actual_owned_storage_contract(self):
  target=os.environ.get('AGX_SHADER_OWNED_RECEIPTS')
  if not target:self.skipTest('parent BEFORE and exact source/tool/argv approval required')
  approval=Path(os.environ['AGX_SHADER_OWNED_APPROVED_MANIFEST'])
  self.assertEqual(sha(approval),os.environ['AGX_SHADER_OWNED_APPROVED_SHA256'])
  approved=json.loads(approval.read_text());self.assertEqual(approved['schema'],1)
  before=os.environ['AGX_SHADER_OWNED_AUTHORIZED_BEFORE'];self.assertTrue(before and not before.startswith('PENDING'))
  self.assertEqual(approved['approved_before'],before)
  out=Path(target).resolve();generated=Path(approved['generated_root']).resolve()
  self.assertEqual(str(out),approved['receipt_root']);self.assertFalse(out.exists(),'fresh attempt required')
  self.assertEqual(sha(WDK),WDK_SHA);self.assertTrue(SDK.is_dir())
  manifest=generated/'shader-generated-inputs.json';meta=json.loads(manifest.read_text())
  required={str(p.resolve())for p in source_inputs()}
  self.assertEqual({str(Path(p).resolve())for p in meta['inputs']},required)
  for p,item in meta['inputs'].items():
   self.assertEqual(sha(p),item['sha256'],p);self.assertEqual(Path(p).stat().st_size,item['bytes'],p)
  generated_paths=[]
  for name,item in meta['generated'].items():
   p=generated/name;self.assertEqual(sha(p),item['sha256'],name);self.assertEqual(p.stat().st_size,item['bytes'],name);generated_paths.append(p)
  required|={str(p.resolve())for p in generated_paths+[manifest]}
  required|={str((TOOL/name).resolve())for name in ['clang','clang++']}
  self.assertEqual({str(Path(p).resolve())for p in approved['inputs']},required)
  for p,item in approved['inputs'].items():
   self.assertEqual(sha(p),item['sha256'],p);self.assertEqual(Path(p).stat().st_size,item['bytes'],p)
  for name in ['clang','clang++']:
   self.assertEqual(sha(TOOL/name),TOOL_SHA)
   self.assertEqual(str((TOOL/name).resolve()),approved['tool_resolved'][name])
  expected=commands(out,generated);self.assertEqual(approved['commands'],expected)
  out.mkdir(parents=True);(out/'approval.json').write_bytes(approval.read_bytes())
  results=[];error=None
  try:
   for n,argv in enumerate(expected,1):
    (out/f'{n}.argv.json').write_text(json.dumps(argv,indent=2)+'\n');started=datetime.datetime.now(datetime.timezone.utc).isoformat()
    try:r=subprocess.run(argv,cwd=out,capture_output=True,timeout=60)
    except subprocess.TimeoutExpired as e:
     (out/f'{n}.stdout').write_bytes(e.stdout or b'');(out/f'{n}.stderr').write_bytes(e.stderr or b'');results.append({'step':n,'rc':None,'timeout60':True});raise RuntimeError('BOUNDED_HOST_TIMEOUT')
    (out/f'{n}.stdout').write_bytes(r.stdout);(out/f'{n}.stderr').write_bytes(r.stderr)
    results.append({'step':n,'start_utc':started,'end_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'rc':r.returncode})
    if r.returncode:raise RuntimeError('COMPILE_OR_FUNCTIONAL_STOP step='+str(n)+' rc='+str(r.returncode))
    if n==3:self.assertIn(b'SHADER_OWNED_CORE_PASS checks=114467 actual_modules=YES native_ABI=NO framing_only=YES no_registration=YES',r.stdout)
  except Exception as e:error=str(e)
  finally:
   (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
   post={p:{'bytes':Path(p).stat().st_size,'sha256':sha(p)}for p in approved['inputs']}
   (out/'post-input-hashes.json').write_text(json.dumps(post,indent=2)+'\n')
   if post!=approved['inputs']:error=error or'POST_INPUT_HASH_DRIFT'
   (out/'verdict.json').write_text(json.dumps({'status':'STOP'if error else'PASS','before':before,'error':error},indent=2)+'\n')
   (out/'output-manifest.json').write_text(json.dumps({p.name:{'bytes':p.stat().st_size,'sha256':sha(p)}for p in out.iterdir()if p.is_file()},indent=2)+'\n')
  self.assertIsNone(error,error)
