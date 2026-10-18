from pathlib import Path
import os,json,subprocess,hashlib,sys
root=Path(r'C:\Users\pauls\AD04-persistent-dwm-next')
base=Path(r'C:\Users\pauls\R149-offline')
phase=sys.argv[1]
out=base/phase;out.mkdir(exist_ok=True)
x=json.loads(Path(r'C:\Users\pauls\EXP862-r148-pools\native-gpuva-arm64\result.json').read_text(encoding='utf-8-sig'))
vc=Path(r'C:\VS2022Community\VC\Tools\MSVC\14.44.35207');sdk=Path(r'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0')
env=os.environ.copy();env['INCLUDE']=';'.join(map(str,[vc/'include',sdk/'shared',sdk/'ucrt',sdk/'um',sdk/'km']))
records=[]
for name in ('agx_win32_gpuva','agx_win32_asahi_bo','agx_win32_gpuva_batch'):
 u=next(u for u in x['units'] if u['name'].endswith('-'+name))
 argv=[a for a in u['argv'] if not a.lower().startswith(('/fo','/fd')) and a not in ('/W2','/Zc:enumTypes','/Zc:preprocessor')]
 argv=[('/imsvc'+a[2:]) if a.startswith('/I') and 'AD04-persistent-dwm-next' not in a else a for a in argv]
 argv+=['/W4','/WX','/Fo'+str(out/(name+'.obj'))]
 if phase!='baseline' and name=='agx_win32_gpuva_batch':
  argv+=['/clang:-Wno-incompatible-pointer-types','/clang:-Wno-sign-compare']
 r=subprocess.run(argv,env=env,capture_output=True,text=True)
 (out/(name+'.log')).write_text(r.stdout+r.stderr)
 print(name,'compile',r.returncode,r.stdout+r.stderr,flush=True)
 record={'name':name,'argv':argv,'exit':r.returncode}
 if not r.returncode and phase!='baseline':
  aa=[a for a in argv if not a.lower().startswith('/fo') and a!='/c']+['/clang:--analyze','/clang:-Xanalyzer','/clang:-analyzer-output=text']
  a=subprocess.run(aa,env=env,capture_output=True,text=True);(out/(name+'-analyze.log')).write_text(a.stdout+a.stderr)
  record.update(analysis_argv=aa,analysis_exit=a.returncode,object_sha256=hashlib.sha256((out/(name+'.obj')).read_bytes()).hexdigest())
  print(name,'analyze',a.returncode,a.stdout+a.stderr,flush=True)
 records.append(record)
(base/(phase+'-result.json')).write_text(json.dumps(records,indent=2))

if phase!='baseline':
 for relative in ('drivers/apple-agx/mesa/winsys/agx_win32_gpuva.c','drivers/apple-agx/shared/src/apple_agx_g4_submit.c','drivers/apple-agx/shared/src/apple_agx_g4_builder.c'):
  name=Path(relative).stem
  argv=[str(vc/'bin/HostX64/arm64/cl.exe'),'/nologo','/c','/TC','/std:c11','/W4','/WX','/analyze','/I'+str(root/'drivers/apple-agx/shared/include'),'/I'+str(root/'drivers/apple-agx/mesa/winsys'),'/Fo'+str(out/(name+'-msvc.obj')),str(root/relative)]
  r=subprocess.run(argv,env=env,capture_output=True,text=True);(out/(name+'-msvc.log')).write_text(r.stdout+r.stderr)
  records.append({'name':name+'-msvc','argv':argv,'exit':r.returncode,'source_sha256':hashlib.sha256((root/relative).read_bytes()).hexdigest(),'object_sha256':hashlib.sha256((out/(name+'-msvc.obj')).read_bytes()).hexdigest() if r.returncode==0 else None})
  print(name,'MSVC',r.returncode,r.stdout+r.stderr,flush=True)
 (base/(phase+'-result.json')).write_text(json.dumps(records,indent=2))
 sys.exit(1 if any(r['exit'] or r.get('analysis_exit',0) for r in records) else 0)
