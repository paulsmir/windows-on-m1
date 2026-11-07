"""Opt-in CURRENT original owned-PROGRAM/NIR composition; no default external access."""
import datetime,hashlib,json,os,shlex,signal,subprocess,unittest
from pathlib import Path
class LegacyPrepared(unittest.TestCase):
 def test_current_legacy_constructor_no_core(self):
  target=os.environ.get('AGX_C15_LEGACY_PREPARED_RECEIPTS')
  if not target:self.skipTest('explicit CURRENT-source CPU approval required')
  # No helper import, metadata/header/tool access occurs before the default skip.
  from legacy_prepared_source import commands,expected_inputs,sha,entry,LINK_STDERR
  approved=Path(os.environ['AGX_C15_LEGACY_PREPARED_APPROVED_MANIFEST']);self.assertEqual(sha(approved),os.environ['AGX_C15_LEGACY_PREPARED_APPROVED_SHA256'])
  a=json.loads(approved.read_text());before=os.environ['AGX_C15_LEGACY_PREPARED_AUTHORIZED_BEFORE'];self.assertTrue(before and not before.startswith('PENDING'));self.assertEqual(a['approved_before'],before)
  out=Path(target).resolve();generated=Path(a['generated_root']);self.assertEqual(str(out),a['receipt_root']);self.assertFalse(out.exists(),'fresh receipt root required')
  pins=expected_inputs(generated);self.assertEqual(a['inputs'],pins);cs=commands(out,generated);self.assertEqual(a['commands'],cs);self.assertEqual(len(cs),14)
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
   if 'C15_LEGACY_PREPARED_NO_CORE_PASS checks='not in so or 'ROOT_UNSPECIFIED NO_PROVIDER_NO_CORE_NO_GPU'not in so:raise RuntimeError('LEGACY_FACTORY_MARKER_MISSING')
  except BaseException as e:error={'type':type(e).__qualname__,'message':str(e)}
  finally:
   try:verify();post=True
   except BaseException as e:post=False;error=error or {'post':repr(e)}
   (out/'verdict.json').write_text(json.dumps({'status':'STOP'if error else'PASS','error':error,'post_pins_match':post,'scope':'CURRENT true legacy factory, no ownedcore/provider/resolver objects; actualNIR/lifetime checks, no DDI/nativeconstructor/GPU claim'},indent=2)+'\n');(out/'dependencies.json').write_text(json.dumps(deps,indent=2)+'\n');(out/'artifact-hashes.json').write_text(json.dumps({str(q.relative_to(out)):entry(q)for q in out.rglob('*')if q.is_file()},indent=2)+'\n')
  self.assertIsNone(error,error)
