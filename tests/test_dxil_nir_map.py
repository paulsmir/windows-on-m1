"""Opt-in CURRENT ROOT original descriptor pass + actual Mesa NIR semantic gate."""
import datetime,json,os,signal,subprocess,unittest
from pathlib import Path
class DxilNirMap(unittest.TestCase):
 def test_current_map_pass_on_actual_nir(self):
  target=os.environ.get('AGX_C15_NIR_MAP_RECEIPTS')
  if not target:self.skipTest('CURRENT-source CPU compiler/run requires parent BEFORE and explicit approved pins')
  # No helper import, external metadata or tool access happens before default skip.
  from dxil_nir_map_source import commands,direct_inputs,metadata,entry,sha,dependency_inputs
  approved_path=Path(os.environ['AGX_C15_NIR_MAP_APPROVED_MANIFEST'])
  self.assertEqual(sha(approved_path),os.environ['AGX_C15_NIR_MAP_APPROVED_SHA256'])
  approved=json.loads(approved_path.read_text());self.assertEqual(approved['schema'],1)
  before=os.environ['AGX_C15_NIR_MAP_AUTHORIZED_BEFORE'];self.assertTrue(before and not before.startswith('PENDING'));self.assertEqual(approved['approved_before'],before)
  out=Path(target).resolve();self.assertEqual(str(out),approved['receipt_root']);self.assertFalse(out.exists(),'fresh receipts only')
  meta=metadata();self.assertEqual(meta['schema'],1);expected=commands(out);self.assertEqual(len(expected),4);self.assertEqual(expected,approved['commands'])
  header_path=Path(approved['header_inventory']);self.assertEqual(sha(header_path),meta['header_inventory_sha256']);headers=json.loads(header_path.read_text())['headers'];self.assertEqual(len(headers),meta['header_count'])
  current=direct_inputs();current[str(header_path.resolve())]=entry(header_path);self.assertEqual(current,approved['inputs'])
  allowed={**headers,**current}
  def pins():
   actual={name:entry(name)for name in allowed}
   if actual!=allowed:raise RuntimeError('CURRENT_DIRECT_OR_HEADER_PIN_DRIFT')
   return actual
  pins();out.mkdir(parents=True);(out/'approval.json').write_bytes(approved_path.read_bytes());results=[];closures={};error=None;stdout=None
  env=os.environ.copy()
  for n in ['CL','_CL_','CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','SDKROOT','DEVELOPER_DIR','DYLD_LIBRARY_PATH','DYLD_INSERT_LIBRARIES','ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS']:env.pop(n,None)
  try:
   for step,argv in enumerate(expected,1):
    pins();(out/f'{step}.argv.json').write_text(json.dumps(argv,indent=2)+'\n');start=datetime.datetime.now(datetime.timezone.utc).isoformat();timed=False
    with (out/f'{step}.stdout').open('xb')as log,(out/f'{step}.stderr').open('xb')as err:
     child=subprocess.Popen(argv,cwd=out,env=env,stdout=log,stderr=err,start_new_session=True)
     try:rc=child.wait(timeout=60)
     except subprocess.TimeoutExpired:
      timed=True
      try:os.killpg(child.pid,signal.SIGKILL)
      except ProcessLookupError:pass
      rc=child.wait(timeout=10)
    results.append({'step':step,'argv':argv,'start_utc':start,'end_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'rc':rc,'timeout60':timed})
    if timed or rc or(out/f'{step}.stderr').stat().st_size:raise RuntimeError('RC_STDERR_TIMEOUT_STOP step='+str(step))
    pins()
    if step<=2:
     deps=dependency_inputs(Path(argv[argv.index('-MF')+1]));missing=[n for n in deps if n not in allowed]
     closures[str(step)]={'dependencies':deps,'missing':missing}
     if missing:raise RuntimeError('MMD_UNPINNED_INPUTS '+repr(missing))
    if step==4:
     stdout=(out/'4.stdout').read_text()
     for marker in meta['run_markers']:
      if marker not in stdout:raise RuntimeError('ACTUAL_MARKER_MISSING '+marker)
     for pair in meta['expected_vectors']:
      if(out/pair['actual']).read_bytes()!=Path(pair['expected_input']).read_bytes():raise RuntimeError('SEMANTIC_VECTOR_MISMATCH '+pair['actual'])
  except Exception as exc:error=str(exc)
  finally:
   (out/'results.json').write_text(json.dumps(results,indent=2)+'\n');(out/'dependency-closure.json').write_text(json.dumps(closures,indent=2)+'\n')
   try:post=pins();(out/'post-input-hashes.json').write_text(json.dumps(post,indent=2)+'\n')
   except Exception as exc:error=error or str(exc)
   (out/'verdict.json').write_text(json.dumps({'status':'STOP'if error else'PASS','before':before,'error':error,'actual_stdout':stdout,'scope':'CURRENT_ROOT_UNLINKED_ACTUAL_NIR_MAP_CPU; no translator/backend/Windows admission/OOM/guest/GPUclaim'},indent=2)+'\n')
   (out/'output-manifest.json').write_text(json.dumps({p.name:entry(p)for p in out.iterdir()if p.is_file()},indent=2)+'\n')
  self.assertIsNone(error,error)
