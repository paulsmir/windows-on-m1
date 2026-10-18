from pathlib import Path
import os,sys,json,hashlib,subprocess,ctypes,tarfile
root=Path(r'C:\Users\pauls\AD04-persistent-dwm-next');base=Path(r'C:\agx\r150-dump');out=base/'compile';out.mkdir(exist_ok=True)
baseline=json.loads((base/'baseline-hashes.json').read_text());final=json.loads((base/'source-hashes.json').read_text())
before={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in baseline}
for n,h in before.items():assert h in (baseline[n],final[n]),('unexpected input',n,h)
(base/'builder-before.json').write_text(json.dumps(before,indent=2))
with tarfile.open(base/'changed.tar') as t:
 names=t.getnames();assert all(n in final and not Path(n).is_absolute() and '..' not in Path(n).parts for n in names)
 for n in names:
  if before[n]!=final[n]:t.extract(n,root)
for n,h in final.items():assert hashlib.sha256((root/n).read_bytes()).hexdigest()==h,n
print('Verified536 sources; applied only5 changed integration files including1 KMD fix',flush=True)
tlog=root/'drivers/apple-agx/render-admission/ARM64/Release/AppleAgx.C62B1A75.tlog/CL.command.1.tlog'
lines=tlog.read_text(encoding='utf-16').splitlines();name='backend_platform_windows.c'
line=next(lines[i+1] for i,l in enumerate(lines) if l.startswith('^') and l.upper().endswith('\\'+name.upper()))
(base/'original-compile-command.txt').write_text(line)
parse=ctypes.windll.shell32.CommandLineToArgvW;parse.restype=ctypes.POINTER(ctypes.c_wchar_p);parse.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_int)]
count=ctypes.c_int();ptr=parse('cl '+line,ctypes.byref(count));argv=[ptr[i] for i in range(1,count.value)];ctypes.windll.kernel32.LocalFree(ptr)
argv=[x for x in argv if not x.lower().startswith(('/fo','/fd'))]
assert '/W4' in argv and '/WX' in argv and '/analyze' in argv
assert any('APPLE_AGX_GPUVA_G3_QUALIFICATION' in x for x in argv)
argv.extend(['/Fo'+str(out/(name+'.obj')),'/Fd'+str(out/(name+'.pdb'))])
vc=Path(r'C:\VS2022Community\VC\Tools\MSVC\14.44.35207');sdk=Path(r'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0')
env=os.environ.copy();env['INCLUDE']=';'.join(map(str,[vc/'include',sdk/'km',sdk/'shared',sdk/'ucrt',sdk/'um']))
exe=vc/'bin/HostX86/arm64/cl.exe'
r=subprocess.run([str(exe),*argv],cwd=root/'drivers/apple-agx/render-admission',env=env,capture_output=True,text=True)
(out/(name+'.log')).write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,flush=True)
record={'source':name,'argv':[str(exe),*argv],'exit':r.returncode,'source_count':len(final),'changed_files':names,'object_sha256':hashlib.sha256((out/(name+'.obj')).read_bytes()).hexdigest() if r.returncode==0 else None}
(base/'compile-result.json').write_text(json.dumps(record,indent=2));sys.exit(r.returncode)
