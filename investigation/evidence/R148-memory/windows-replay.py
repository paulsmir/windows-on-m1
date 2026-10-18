from pathlib import Path
import os,subprocess,json,hashlib,sys
base=Path(r'C:\Users\pauls\R148-memory');out=base/'build';vc=Path(r'C:\VS2022Community\VC\Tools\MSVC\14.44.35207');sdk=Path(r'C:\Program Files (x86)\Windows Kits\10');env=os.environ.copy();env['INCLUDE']=';'.join(map(str,[vc/'include',*(sdk/'Include/10.0.26100.0'/s for s in ['um','shared','ucrt','km'])]));env['LIB']=';'.join(map(str,[vc/'lib/x64',sdk/'Lib/10.0.26100.0/um/x64',sdk/'Lib/10.0.26100.0/ucrt/x64']))
exe=out/'windows-replay.exe';argv=[str(vc/'bin/HostX64/x64/cl.exe'),'/nologo','/std:c++17','/EHsc','/W4','/WX','/analyze','/MD','/Fo'+str(out/'windows-replay.obj'),'/Fe'+str(exe),str(base/'windows-replay.cpp')]
r=subprocess.run(argv,env=env,capture_output=True,text=True);(out/'windows-replay-compile.log').write_text(r.stdout+r.stderr);print('COMPILE',r.returncode,r.stdout+r.stderr)
a=None
if not r.returncode:
 a=subprocess.run([str(exe)],env=env,capture_output=True,text=True);(out/'windows-replay-run.log').write_text(a.stdout+a.stderr);print('RUN',a.returncode,a.stdout+a.stderr)
(out/'windows-replay-result.json').write_text(json.dumps({'argv':argv,'compile_exit':r.returncode,'run_exit':a.returncode if a else None,'source_sha256':hashlib.sha256((base/'windows-replay.cpp').read_bytes()).hexdigest(),'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()if exe.exists()else None},indent=2));sys.exit(r.returncode or (a.returncode if a else 0))
