from pathlib import Path
import os,subprocess,json
base=Path(r'C:\Users\pauls\R148-offline');u=json.loads((base/'native-command.json').read_text(encoding='utf-8-sig'))
argv=[str(base/'baseline-pool.c') if x.lower().endswith('agx_win32_gpuva_batch.c') else x for x in u['argv'] if not x.lower().startswith(('/fo','/fd')) and x not in ('/W2','/Zc:enumTypes','/Zc:preprocessor')]
argv=[('/imsvc'+x[2:]) if x.startswith('/I') and 'AD04-persistent-dwm-next' not in x else x for x in argv]+['/W4','/WX','/Fo'+str(base/'baseline-pool.obj')]
vc=Path(r'C:\VS2022Community\VC\Tools\MSVC\14.44.35207');sdk=Path(r'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0')
env=os.environ.copy();env['INCLUDE']=';'.join(map(str,[vc/'include',sdk/'shared',sdk/'ucrt',sdk/'um',sdk/'km']))
r=subprocess.run(argv,env=env,capture_output=True,text=True);print('BASELINE',r.returncode,r.stdout+r.stderr)
(base/'baseline-pool-result.json').write_text(json.dumps({'argv':argv,'exit':r.returncode},indent=2))
