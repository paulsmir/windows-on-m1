"""Opt-in CURRENT ROOT checked adapter; skip before any external file/tool access."""
import datetime,hashlib,json,os,shlex,signal,subprocess,unittest
from pathlib import Path
class CheckedOwned(unittest.TestCase):
 def test_actual_current_checked_adapter_nominal(self):
  target=os.environ.get('AGX_C15_CHECKED_RECEIPTS')
  if not target:self.skipTest('parent BEFORE/current-source/composite approval required')
  from dxil_checked_owned_source import sha,expected_inputs,commands
  approved=Path(os.environ['AGX_C15_CHECKED_APPROVED_MANIFEST']);self.assertEqual(sha(approved),os.environ['AGX_C15_CHECKED_APPROVED_SHA256'])
  a=json.loads(approved.read_text());self.assertEqual(a['schema'],1)
  before=os.environ['AGX_C15_CHECKED_AUTHORIZED_BEFORE'];self.assertTrue(before and not before.startswith('PENDING'));self.assertEqual(a['approved_before'],before)
  out=Path(target).resolve();self.assertEqual(str(out),a['receipt_root']);self.assertFalse(out.exists(),'fresh output required')
  m,pins=expected_inputs();self.assertEqual(a['inputs'],pins);cs=commands(out);self.assertEqual(a['commands'],cs);self.assertEqual(len(cs),9)
  def verify():
   actual={n:{'bytes':Path(n).stat().st_size,'sha256':sha(n)}for n in pins}
   if actual!=pins:raise RuntimeError('INPUT_PIN_DRIFT')
   return actual
  verify();allowed={str(Path(n).resolve())for n in pins};out.mkdir(parents=True);(out/'approval.json').write_bytes(approved.read_bytes());results=[];error=None;dependencies={}
  env=os.environ.copy()
  for n in ['CL','_CL_','CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','SDKROOT','DEVELOPER_DIR','DYLD_LIBRARY_PATH','DYLD_INSERT_LIBRARIES','ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS']:env.pop(n,None)
  try:
   for c in cs:
    verify();name=c['label'];argv=c['argv'];(out/(name+'.argv.json')).write_text(json.dumps(argv,indent=2)+'\n');start=datetime.datetime.now(datetime.timezone.utc).isoformat();timed=False
    with (out/(name+'.stdout')).open('xb')as stdout,(out/(name+'.stderr')).open('xb')as stderr:
     child=subprocess.Popen(argv,cwd=out,env=env,stdout=stdout,stderr=stderr,start_new_session=True)
     try:rc=child.wait(timeout=120)
     except subprocess.TimeoutExpired:
      timed=True
      try:os.killpg(child.pid,signal.SIGKILL)
      except ProcessLookupError:pass
      rc=child.wait(timeout=10)
    results.append({'label':name,'argv':argv,'rc':rc,'timeout120':timed,'start_utc':start,'end_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
    if rc or timed:raise RuntimeError('COMMAND_STOP '+name)
    err=out/(name+'.stderr');want=m['expected_stderr'].get(name,{'bytes':0,'sha256':hashlib.sha256(b'').hexdigest()})
    if err.stat().st_size!=want['bytes']or sha(err)!=want['sha256']:raise RuntimeError('UNEXPECTED_STDERR_STOP '+name)
    if '-MF'in argv:
     dep=Path(argv[argv.index('-MF')+1]);text=dep.read_text().replace('\\\n','');actual=sorted({str(Path(n).resolve())for n in shlex.split(text.split(':',1)[1])})
     if not set(actual)<=allowed:raise RuntimeError('UNREGISTERED_DEPENDENCY '+repr(set(actual)-allowed))
     dependencies[name]=actual
    verify()
   if 'C15_OWNED_CONVERTER_PASS 'not in(out/'one-nominal.stdout').read_text():raise RuntimeError('ONE_ORACLE_MISSING')
   if 'C15_MIXED_OWNED_PASS keys=5 'not in(out/'mixed-nominal.stdout').read_text():raise RuntimeError('MIXED_ORACLE_MISSING')
   for name,want in m['expected_mixed'].items():
    f=out/name
    if f.stat().st_size!=want['bytes']or sha(f)!=want['sha256']:raise RuntimeError('MIXED_ARTIFACT_NOT_ACCEPTED_BYTES '+name)
  except Exception as e:error=str(e)
  finally:
   (out/'results.json').write_text(json.dumps(results,indent=2)+'\n');(out/'dependencies.json').write_text(json.dumps(dependencies,indent=2)+'\n')
   try:(out/'post-pins.json').write_text(json.dumps(verify(),indent=2)+'\n')
   except Exception as e:error=error or str(e)
   (out/'verdict.json').write_text(json.dumps({'status':'STOP'if error else'PASS','before':before,'error':error,'scope':'actual CURRENT ROOT nominal checked adapter/composite; exact accepted diagnostics; no fault/fullOOM/native/DDI/caps/runtime-lifetime claim'},indent=2)+'\n')
   (out/'artifact-hashes.json').write_text(json.dumps({p.name:{'bytes':p.stat().st_size,'sha256':sha(p)}for p in out.iterdir()if p.is_file()},indent=2)+'\n')
  self.assertIsNone(error,error)
