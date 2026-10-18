from pathlib import Path
import os,sys,json,hashlib,subprocess,ctypes
root=Path(r'C:\Users\pauls\AD04-persistent-dwm-next')
base=Path(r'C:\Users\pauls\R148-offline');out=base/'kmd';out.mkdir(exist_ok=True)
manifest=json.loads((base/'current-hashes.json').read_text(encoding='utf-8-sig'))
baseline=json.loads((Path(r'C:\Users\pauls\EXP861-r147-pagingroot\source-manifest.json')).read_text(encoding='utf-8-sig'))
for item in baseline['files']:
 name=item['path'];actual=hashlib.sha256((root/name).read_bytes()).hexdigest()
 assert actual in (item['sha256'],manifest.get(name)), ('unexpected builder input',name,actual)
import tarfile
with tarfile.open(base/'kmd.tar') as archive: archive.extractall(root)
print('Verified536 persistent inputs; apply2 QUERY source files; rebuild all KMD TUs for ADMISSION_CONTEXT offsets',flush=True)
for name,h in manifest.items(): assert hashlib.sha256((root/name).read_bytes()).hexdigest()==h,name
lines=(Path(r'C:\Users\pauls\R145-offline')/'kmd-CL.command.1.tlog').read_text(encoding='utf-16').splitlines()
parse=ctypes.windll.shell32.CommandLineToArgvW;parse.restype=ctypes.POINTER(ctypes.c_wchar_p)
parse.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_int)]
vc=Path(r'C:\VS2022Community\VC\Tools\MSVC\14.44.35207');sdk=Path(r'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0')
env=os.environ.copy();env['INCLUDE']=';'.join(map(str,[vc/'include',sdk/'km',sdk/'shared',sdk/'ucrt',sdk/'um']))
names=[Path(l[1:].replace('\\','/')).name.lower() for l in lines if l.startswith('^')]
assert len(names)==len(set(names)),names
records=[]
for name in names:
 source_name=name
 line=next(lines[i+1] for i,l in enumerate(lines) if l.startswith('^') and l.upper().endswith('\\'+source_name.upper()))
 count=ctypes.c_int();ptr=parse('cl '+line,ctypes.byref(count));argv=[ptr[i] for i in range(1,count.value)];ctypes.windll.kernel32.LocalFree(ptr)
 argv=[x for x in argv if not x.lower().startswith(('/fo','/fd'))]
 if source_name!=name:
  argv=[str(root/'drivers/apple-agx/shared/src'/name) if x.upper().endswith('\\'+source_name.upper()) else x for x in argv]
 assert '/W4' in argv and '/WX' in argv and '/analyze' in argv
 argv.extend(['/Fo'+str(out/(name+'.obj')),'/Fd'+str(out/(name+'.pdb'))])
 exe=vc/'bin/HostX86/arm64/cl.exe'
 r=subprocess.run([str(exe),*argv],cwd=root/'drivers/apple-agx/render-admission',env=env,capture_output=True,text=True)
 (out/(name+'.log')).write_text(r.stdout+r.stderr);print(name,r.returncode,r.stdout+r.stderr,flush=True)
 records.append({'source':name,'argv':[str(exe),*argv],'exit':r.returncode,'object_sha256':hashlib.sha256((out/(name+'.obj')).read_bytes()).hexdigest() if r.returncode==0 else None})
 if r.returncode:break
(base/'kmd-compile-result.json').write_text(json.dumps(records,indent=2));sys.exit(records[-1]['exit'])
