"""Opt-in CURRENT original owned-PROGRAM/NIR composition; no default external access."""
import datetime,hashlib,json,os,shlex,signal,subprocess,unittest
from pathlib import Path
class OwnedNirPipe(unittest.TestCase):
 def test_actual_current_nir_clone_callback(self):
  target=os.environ.get('AGX_C15_NIR_PIPE_RECEIPTS')
  if not target:self.skipTest('explicit CURRENT-source CPU approval required')
  # No helper import, metadata/header/tool access occurs before the default skip.
  from owned_nir_pipe_source import commands,expected_inputs,sha,entry,LINK_STDERR
  approved=Path(os.environ['AGX_C15_NIR_PIPE_APPROVED_MANIFEST']);self.assertEqual(sha(approved),os.environ['AGX_C15_NIR_PIPE_APPROVED_SHA256'])
  a=json.loads(approved.read_text());before=os.environ['AGX_C15_NIR_PIPE_AUTHORIZED_BEFORE'];self.assertTrue(before and not before.startswith('PENDING'));self.assertEqual(a['approved_before'],before)
  out=Path(target).resolve();generated=Path(a['generated_root']);self.assertEqual(str(out),a['receipt_root']);self.assertFalse(out.exists(),'fresh receipt root required')
  pins=expected_inputs(generated);self.assertEqual(a['inputs'],pins);cs=commands(out,generated);self.assertEqual(a['commands'],cs);self.assertEqual(len(cs),19)
  def verify():
   for name,want in pins.items():
    if entry(name)!=want:raise RuntimeError('INPUT_DRIFT '+name)
  verify();out.mkdir(parents=True);(out/'approval.json').write_bytes(approved.read_bytes());results=[];deps={};error=None
  env=os.environ.copy()
  for n in ['CL','_CL_','CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','SDKROOT','DEVELOPER_DIR','DYLD_LIBRARY_PATH','DYLD_INSERT_LIBRARIES','ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS']:env.pop(n,None)
  try:
   for c in cs:
    verify();label=c['label'];argv=c['argv'];(out/(label+'.argv.json')).write_text(json.dumps(argv,indent=2)+'\n');start=datetime.datetime.now(datetime.timezone.utc).isoformat();timed=False
    with(out/(label+'.stdout')).open('xb')as so,(out/(label+'.stderr')).open('xb')as se:
     child=subprocess.Popen(argv,cwd=out,env=env,stdout=so,stderr=se,start_new_session=True)
     try:rc=child.wait(timeout=120)
     except subprocess.TimeoutExpired:
      timed=True
      try:os.killpg(child.pid,signal.SIGKILL)
      except ProcessLookupError:pass
      rc=child.wait(timeout=10)
    results.append({'label':label,'argv':argv,'rc':rc,'timeout':timed,'start':start,'end':datetime.datetime.now(datetime.timezone.utc).isoformat()});(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    if rc or timed:raise RuntimeError('FIRST_COMMAND_STOP '+label)
    stderr=(out/(label+'.stderr')).read_bytes();expected=LINK_STDERR if label=='link'else b''
    if stderr!=expected:raise RuntimeError('UNEXPECTED_STDERR '+label)
    if '-MF'in argv:
     text=Path(argv[argv.index('-MF')+1]).read_text().replace('\\\n','');actual=sorted({str(Path(n).resolve())for n in shlex.split(text.split(':',1)[1])});deps[label]=actual
     if not set(actual)<=pins.keys():raise RuntimeError('UNPINNED_DEPENDENCY '+repr(sorted(set(actual)-pins.keys())))
    verify()
   so=(out/'run.stdout').read_text()
   for line in ['C15_OWNED_NIR_BRIDGE_PASS checks=79 accepted=5 refused=6','C15_OWNED_NIR_NORMALIZATION_PASS base_global=0 base_workgroup=0','C15_NIR_SEMANTIC_PASS lanes=192 active=173 tail=19 compared_words=384','srv_swap_bad=346 seed_loss_bad=346 wrong_slot_bad=346','C15_NIR_PIPE_CALLBACK_PASS token_stage=5','C15_NIR_PIPE_CALLBACK_PASS token_stage=1','C15_NIR_PIPE_CALLBACK_PASS token_stage=0','C15_NIR_PIPE_FS_SERIALIZATION_PASS ORIGINAL_AND_TRANSFERRED_CLONES_UNCHANGED','C15_NIR_PIPE_FIXTURE_PAIR_ROLLBACK_PASS NO_PRODUCTION_GRAPHICS_CLAIM','C15_NIR_PIPE_CALLBACK_FINAL_PASS checks=199 retained_refs=0','C15_NIR_OPAQUE_NULL_CASES_PASS','C15_NIR_OPAQUE_WRAPPER_PASS valid_calls=20 null_calls=8']:
    if line not in so:raise RuntimeError('ORACLE_MARKER_MISSING '+line)
   for n in ['1','9']:
    q=out/('actual_u0_space'+n+'.bin');gold=Path(cs[-1]['argv'][-1])/('expected_u0_space'+n+'.bin')
    if q.stat().st_size!=768 or q.read_bytes()!=gold.read_bytes():raise RuntimeError('GOLD384_WORD_MISMATCH')
  except BaseException as e:error={'type':type(e).__qualname__,'message':str(e)}
  finally:
   try:verify();post=True
   except BaseException as e:post=False;error=error or {'post':repr(e)}
   (out/'verdict.json').write_text(json.dumps({'status':'STOP'if error else'PASS','error':error,'post_pins_match':post,'scope':'CURRENT-source CPU actual NIR clone/metadata/publication with fixture callbacks only; no actual Asahi constructor/compiler/BO/DDI/GPU or allocation-failure claim'},indent=2)+'\n');(out/'dependencies.json').write_text(json.dumps(deps,indent=2)+'\n');(out/'artifact-hashes.json').write_text(json.dumps({str(q.relative_to(out)):entry(q)for q in out.rglob('*')if q.is_file()},indent=2)+'\n')
  self.assertIsNone(error,error)
