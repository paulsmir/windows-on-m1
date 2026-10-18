from pathlib import Path
import os,json,hashlib,subprocess,tarfile,sys
root=Path(r'C:\Users\pauls\AD04-persistent-dwm-next');base=Path(r'C:\Users\pauls\R148-offline');out=base/'pool';out.mkdir(exist_ok=True)
manifest=json.loads((base/'pool-hashes.json').read_text());baseline=json.loads(Path(r'C:\Users\pauls\EXP861-r147-pagingroot\source-manifest.json').read_text(encoding='utf-8-sig'))
for f in baseline['files']:
 actual=hashlib.sha256((root/f['path']).read_bytes()).hexdigest()
 assert actual in (f['sha256'],manifest.get(f['path'])),f['path']
with tarfile.open(base/'pool.tar') as t:t.extractall(root)
for n,h in manifest.items():assert hashlib.sha256((root/n).read_bytes()).hexdigest()==h
print('Verified',len(baseline['files']),'inputs; applied',len(manifest),'changed file')
u=json.loads((base/'native-command.json').read_text(encoding='utf-8-sig'))
argv=[x for x in u['argv'] if not x.lower().startswith(('/fo','/fd')) and x not in ('/W2','/Zc:enumTypes','/Zc:preprocessor')]
# Third-party inline definitions are system headers; project sources retain W4.
argv=[('/imsvc'+x[2:]) if x.startswith('/I') and 'AD04-persistent-dwm-next' not in x else x for x in argv]
argv+=['/W4','/WX','/clang:-Wno-incompatible-pointer-types','/clang:-Wno-sign-compare','/Fo'+str(out/'agx_win32_gpuva_batch.obj')]
vc=Path(r'C:\VS2022Community\VC\Tools\MSVC\14.44.35207');sdk=Path(r'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0')
env=os.environ.copy();env['INCLUDE']=';'.join(map(str,[vc/'include',sdk/'shared',sdk/'ucrt',sdk/'um',sdk/'km']))
r=subprocess.run(argv,env=env,capture_output=True,text=True)
(out/'compile.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr)
if r.returncode==0:
 analysis_argv=[x for x in argv if not x.lower().startswith('/fo') and x!='/c']+['/clang:--analyze','/clang:-Xanalyzer','/clang:-analyzer-output=text']
 a=subprocess.run(analysis_argv,env=env,capture_output=True,text=True)
 (out/'analyze.log').write_text(a.stdout+a.stderr);print('ANALYZE',a.returncode,a.stdout+a.stderr)
else: a=None;analysis_argv=[]
(base/'pool-compile-result.json').write_text(json.dumps({'argv':argv,'exit':r.returncode,'analysis_exit':a.returncode if a else None,'analysis_argv':analysis_argv,'source_sha256':manifest,'verified_inputs':len(baseline['files']),'object_sha256':hashlib.sha256((out/'agx_win32_gpuva_batch.obj').read_bytes()).hexdigest() if (out/'agx_win32_gpuva_batch.obj').exists() else None},indent=2));sys.exit(r.returncode or (a.returncode if a else 0))
