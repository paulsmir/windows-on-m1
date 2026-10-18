from pathlib import Path
import os,sys,json,hashlib,subprocess,ctypes,tarfile
root=Path(r'C:\Users\pauls\AD04-persistent-dwm-next');base=Path(r'C:\Users\pauls\R148-memory');exp=Path(r'C:\Users\pauls\EXP861-r147-pagingroot');out=base/'build';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
vc=Path(r'C:\VS2022Community\VC\Tools\MSVC\14.44.35207');sdk=Path(r'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0')
env=os.environ.copy();env['INCLUDE']=';'.join(map(str,[vc/'include',sdk/'km',sdk/'shared',sdk/'ucrt',sdk/'um']))
lines=Path(r'C:\Users\pauls\R145-offline\umd-CL.command.1.tlog').read_text(encoding='utf-16').splitlines()
parse=ctypes.windll.shell32.CommandLineToArgvW;parse.restype=ctypes.POINTER(ctypes.c_wchar_p);parse.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_int)]
name='agx_d3d10_windows.cpp';line=next(lines[i+1] for i,l in enumerate(lines) if l.startswith('^') and l.upper().endswith('\\'+name.upper()))
count=ctypes.c_int();ptr=parse('cl '+line,ctypes.byref(count));argv=[ptr[i] for i in range(1,count.value)];ctypes.windll.kernel32.LocalFree(ptr)
argv=[x for x in argv if not x.lower().startswith(('/fo','/fd')) and x!='/analyze-'];argv+=['/W4','/WX','/analyze','/Fo'+str(out/(name+'.obj')),'/Fd'+str(out/(name+'.pdb'))]
records=[]
def run(label,argv,cwd=None):
 r=subprocess.run(argv,cwd=cwd,env=env,capture_output=True,text=True);(out/(label+'.log')).write_text(r.stdout+r.stderr);print(label,r.returncode,r.stdout+r.stderr)
 records.append({'name':label,'argv':argv,'exit':r.returncode});(out/'winsys-result.json').write_text(json.dumps({'source_sha256':sha(root/'drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp'),'records':records},indent=2));return r.returncode
status=run('winsys-arm64',[str(vc/'bin/HostX86/arm64/cl.exe'),*argv],root/'drivers/apple-agx/render-admission/umd')
if status:sys.exit(status)
