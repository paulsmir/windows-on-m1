"""Opt-in CURRENT ROOT provider CPU contract. Default skips before tool access."""
import datetime,json,os,signal,subprocess,unittest
from pathlib import Path
class ShaderProvider(unittest.TestCase):
 def test_actual_current_provider(self):
  target=os.environ.get('AGX_SHADER_PROVIDER_RECEIPTS')
  if not target:self.skipTest('parent BEFORE and current source/tool/argv approval required')
  from d3d12_shader_provider_source import ROOT,WDK,WDK_SHA,TOOL,TOOL_SHA,SDK,commands,sha,source_inputs
  approval=Path(os.environ['AGX_SHADER_PROVIDER_APPROVED_MANIFEST']);self.assertEqual(sha(approval),os.environ['AGX_SHADER_PROVIDER_APPROVED_SHA256'])
  approved=json.loads(approval.read_text());self.assertEqual(approved['schema'],1)
  before=os.environ['AGX_SHADER_PROVIDER_AUTHORIZED_BEFORE'];self.assertTrue(before and not before.startswith('PENDING'));self.assertEqual(approved['approved_before'],before)
  out=Path(target).resolve();g=Path(approved['generated_root']).resolve();self.assertEqual(str(out),approved['receipt_root']);self.assertFalse(out.exists(),'fresh output required')
  self.assertEqual(sha(WDK),WDK_SHA);self.assertTrue(SDK.is_dir());manifest=g/'provider-generated-inputs.json';meta=json.loads(manifest.read_text())
  required={str(p.resolve())for p in source_inputs()};self.assertEqual(set(meta['inputs']),required)
  for p,e in meta['inputs'].items():self.assertEqual(sha(p),e['sha256'],p);self.assertEqual(Path(p).stat().st_size,e['bytes'],p)
  gen=[]
  for n,e in meta['generated'].items():
   p=g/n;self.assertEqual(sha(p),e['sha256'],n);self.assertEqual(p.stat().st_size,e['bytes'],n);gen.append(p)
  required|={str(p.resolve())for p in gen+[manifest]};required|={str((TOOL/n).resolve())for n in ['clang','clang++']}
  self.assertEqual({str(Path(p).resolve())for p in approved['inputs']},required)
  for n in ['clang','clang++']:self.assertEqual(sha(TOOL/n),TOOL_SHA);self.assertEqual(str((TOOL/n).resolve()),approved['tool_resolved'][n])
  expected=commands(out,g);self.assertEqual(len(expected),4);self.assertEqual(approved['commands'],expected)
  def pins():
   post={p:{'bytes':Path(p).stat().st_size,'sha256':sha(p)}for p in approved['inputs']}
   if post!=approved['inputs']:raise RuntimeError('CURRENT_INPUT_HASH_DRIFT')
   return post
  pins();out.mkdir(parents=True);(out/'approval.json').write_bytes(approval.read_bytes());results=[];error=None;marker=None
  env=os.environ.copy()
  for n in ['CL','_CL_','CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','SDKROOT','DEVELOPER_DIR','DYLD_LIBRARY_PATH','DYLD_INSERT_LIBRARIES','ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS']:env.pop(n,None)
  try:
   for n,argv in enumerate(expected,1):
    pins();(out/f'{n}.argv.json').write_text(json.dumps(argv,indent=2)+'\n');start=datetime.datetime.now(datetime.timezone.utc).isoformat();timed=False
    with (out/f'{n}.stdout').open('xb')as o,(out/f'{n}.stderr').open('xb')as e:
     child=subprocess.Popen(argv,cwd=out,env=env,stdout=o,stderr=e,start_new_session=True)
     try:rc=child.wait(timeout=60)
     except subprocess.TimeoutExpired:
      timed=True
      try:os.killpg(child.pid,signal.SIGKILL)
      except ProcessLookupError:pass
      rc=child.wait(timeout=10)
    results.append({'step':n,'argv':argv,'start_utc':start,'end_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'rc':rc,'timeout60':timed})
    if timed or rc or (out/f'{n}.stderr').stat().st_size:raise RuntimeError('NONZERO_RC_STDERR_OR_TIMEOUT_STOP step='+str(n))
    pins()
    if n==4:
     marker=(out/f'{n}.stdout').read_text();expected_marker='PASS provider caller-serialized checks=79 exact-core legacy+DXIL/ownedopaque-root/leases/generations/sinks/publishlast noDDIpolicy\n'
     if marker!=expected_marker:raise RuntimeError('ACTUAL_PROVIDER_79_MARKER_MISMATCH')
  except Exception as e:error=str(e)
  finally:
   (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
   try:post=pins();(out/'post-input-hashes.json').write_text(json.dumps(post,indent=2)+'\n')
   except Exception as e:error=error or str(e)
   (out/'verdict.json').write_text(json.dumps({'status':'STOP'if error else'PASS','before':before,'error':error,'actual_marker':marker,'scope':'UNLINKEDcaller-serializedCPU; ownedopaque roots/borrowedstablePROGRAM-signatures/typedCPUsink; noWindows/HRESULT/thread-safety/OOMrecoveryclaim'},indent=2)+'\n')
   (out/'output-manifest.json').write_text(json.dumps({p.name:{'bytes':p.stat().st_size,'sha256':sha(p)}for p in out.iterdir()if p.is_file()},indent=2)+'\n')
  self.assertIsNone(error,error)
