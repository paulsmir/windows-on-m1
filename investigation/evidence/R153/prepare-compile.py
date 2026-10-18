from pathlib import Path
import hashlib,json,tarfile,subprocess
root=Path.cwd();out=Path(__file__).parent
baseline=json.loads((out/'previous-source-hashes.json').read_text())
baseline={n:h for n,h in baseline.items() if n.startswith(('drivers/apple-agx/render-admission/src/','drivers/apple-agx/render-admission/include/','drivers/apple-agx/shared/'))}
final={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in baseline}
new='drivers/apple-agx/shared/include/apple_agx_render_manager.h'
final[new]=hashlib.sha256((root/new).read_bytes()).hexdigest()
changed=[n for n in final if final[n]!=baseline.get(n)]
(out/'baseline-hashes.json').write_text(json.dumps(baseline,indent=2))
(out/'source-hashes.json').write_text(json.dumps(final,indent=2))
with tarfile.open(out/'changed.tar','w') as t:
 for n in changed:t.add(root/n,arcname=n)
s=(root/'investigation/evidence/R151/compile.py').read_text().replace("r151-dump","r153-offline")
start=s.index("lines=tlog.read_text")
s=s.replace('before={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in baseline}', 'before={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in baseline}\n(base/\"builder-before.json\").write_text(json.dumps(before,indent=2))')
s=s.replace("if before[n]!=final[n]:","if before.get(n)!=final[n]:")
s=s[:s.index("lines=tlog.read_text")]+r'''
lines=tlog.read_text(encoding='utf-16').splitlines()
parse=ctypes.windll.shell32.CommandLineToArgvW;parse.restype=ctypes.POINTER(ctypes.c_wchar_p);parse.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_int)]
vc=Path(r'C:\VS2022Community\VC\Tools\MSVC\14.44.35207');sdk=Path(r'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0')
env=os.environ.copy();env['INCLUDE']=';'.join(map(str,[vc/'include',sdk/'km',sdk/'shared',sdk/'ucrt',sdk/'um']))
exe=vc/'bin/HostX86/arm64/cl.exe'
records=[]
for name in ['apple_agx_exp208_dynamic.c','apple_agx_render_shared_memory.c','apple_agx_g13_queue_provider.c','gpuva_g3_windows.c','backend_platform_windows.c','render_backend_image.c']:
 line=next(lines[i+1] for i,l in enumerate(lines) if l.startswith('^') and l.upper().endswith('\\'+name.upper()))
 count=ctypes.c_int();ptr=parse('cl '+line,ctypes.byref(count));argv=[ptr[i] for i in range(1,count.value)];ctypes.windll.kernel32.LocalFree(ptr)
 argv=[x for x in argv if not x.lower().startswith(('/fo','/fd'))]
 assert '/W4' in argv and '/WX' in argv and '/analyze' in argv
 argv.extend(['/Fo'+str(out/(name+'.obj')),'/Fd'+str(out/(name+'.pdb'))])
 r=subprocess.run([str(exe),*argv],cwd=root/'drivers/apple-agx/render-admission',env=env,capture_output=True,text=True)
 (out/(name+'.log')).write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,flush=True)
 records.append({'source':name,'argv':[str(exe),*argv],'exit':r.returncode,'object_sha256':hashlib.sha256((out/(name+'.obj')).read_bytes()).hexdigest() if r.returncode==0 else None})
(base/'compile-result.json').write_text(json.dumps({'records':records,'source_count':len(final),'changed_files':names},indent=2))
sys.exit(1 if any(r['exit'] for r in records) else 0)
'''
(out/'compile.py').write_text(s)
s=(Path('/Users/pavel/public_windows/.local/experiments/R151-dump/run-compile.py')).read_text().replace('r151-dump','r153-offline')
s=s.replace("['compile-result.json','original-compile-command.txt','builder-before.json','compile/render_backend_image.c.log']", "['compile-result.json','builder-before.json',*[ 'compile/'+n+'.log' for n in ['apple_agx_exp208_dynamic.c','apple_agx_render_shared_memory.c','apple_agx_g13_queue_provider.c','gpuva_g3_windows.c','backend_platform_windows.c','render_backend_image.c']]]")
(out/'run-compile.py').write_text(s)
print('changed builder inputs',len(changed),changed)
