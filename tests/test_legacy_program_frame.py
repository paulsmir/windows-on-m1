"""Opt-in bounded legacy PROGRAM contract using actual current modules."""
from pathlib import Path
import os,json,re,subprocess,unittest
from legacy_program_frame_source import inputs,commands,CC,CC_SHA,NM,SDK,sha
class LegacyProgramFrame(unittest.TestCase):
 def test_actual_framer_and_full_parser_fixture(self):
  target=os.environ.get('AGX_LEGACY_FRAME_RECEIPTS')
  if not target:self.skipTest('parent BEFORE/source-tool approval and exact four argv required')
  a=Path(os.environ['AGX_LEGACY_FRAME_APPROVAL']);self.assertEqual(sha(a),os.environ['AGX_LEGACY_FRAME_APPROVAL_SHA256']);d=json.loads(a.read_text());before=os.environ['AGX_LEGACY_FRAME_BEFORE'];self.assertTrue(before and not before.startswith('PENDING'));self.assertEqual(before,d['before'])
  out=Path(target).resolve();g=Path(d['generated_root']).resolve();self.assertEqual(str(out),d['receipt_root']);self.assertFalse(out.exists());self.assertEqual(sha(CC),CC_SHA);self.assertTrue(SDK.is_dir())
  m=json.loads((g/'generated-inputs.json').read_text());self.assertEqual({str(Path(p).resolve())for p in m['source_inputs']},{str(p.resolve())for p in inputs()})
  for p,v in m['source_inputs'].items():self.assertEqual(sha(p),v['sha256']);self.assertEqual(Path(p).stat().st_size,v['bytes'])
  for n,v in m['generated'].items():self.assertEqual(sha(g/n),v['sha256']);self.assertEqual((g/n).stat().st_size,v['bytes'])
  required={str(p.resolve())for p in inputs()}|{str((g/n).resolve())for n in m['generated']}|{str((g/'generated-inputs.json').resolve()),str(CC.resolve()),str(NM.resolve())}
  self.assertEqual({str(Path(p).resolve())for p in d['inputs']},required)
  for p,v in d['inputs'].items():self.assertEqual(sha(p),v['sha256']);self.assertEqual(Path(p).stat().st_size,v['bytes'])
  expected=commands(out,g);self.assertEqual(d['commands'],expected);out.mkdir(parents=True);(out/'approval.json').write_bytes(a.read_bytes());results=[];error=None
  try:
   for n,argv in enumerate(expected,1):
    (out/f'{n}.argv.json').write_text(json.dumps(argv,indent=2)+'\n')
    try:r=subprocess.run(argv,capture_output=True,timeout=60)
    except subprocess.TimeoutExpired as e:
     (out/f'{n}.stdout').write_bytes(e.stdout or b'');(out/f'{n}.stderr').write_bytes(e.stderr or b'');results.append({'ordinal':n,'rc':None,'timeout60':True});raise RuntimeError('TIMEOUT_STOP')
    (out/f'{n}.stdout').write_bytes(r.stdout);(out/f'{n}.stderr').write_bytes(r.stderr);results.append({'ordinal':n,'rc':r.returncode})
    if r.returncode or r.stderr:raise RuntimeError('NONZERO_RC_OR_STDERR_STOP '+str(n))
    if n==2 and re.search(rb'\b_?(?:malloc|calloc|realloc|free)\b',r.stdout):raise RuntimeError('DIRECT_ALLOCATOR_REFERENCE')
    if n==4:self.assertIn(b'LEGACY_PROGRAM_FRAME_PASS checks=3664 actual_FXC_positives=12 actual_parser_compare=YES nonallocating_framer=YES IR_validation=NO',r.stdout)
  except Exception as e:error=str(e)
  finally:
   post={p:{'sha256':sha(p),'bytes':Path(p).stat().st_size}for p in d['inputs']}
   if post!=d['inputs']:error=error or'POST_INPUT_DRIFT'
   (out/'post-pins.json').write_text(json.dumps(post,indent=2)+'\n');(out/'verdict.json').write_text(json.dumps({'before':before,'status':'STOP'if error else'PASS','error':error,'results':results},indent=2)+'\n');(out/'artifact-manifest.json').write_text(json.dumps({p.name:{'sha256':sha(p),'bytes':p.stat().st_size}for p in out.iterdir()if p.is_file()},indent=2)+'\n')
  self.assertIsNone(error,error)
