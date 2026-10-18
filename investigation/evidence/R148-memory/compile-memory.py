from pathlib import Path
import os,sys,json,hashlib,subprocess,ctypes,tarfile
root=Path(r'C:\Users\pauls\AD04-persistent-dwm-next');base=Path(r'C:\Users\pauls\R148-memory');exp=Path(r'C:\Users\pauls\EXP861-r147-pagingroot');out=base/'build';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
changed=json.loads((base/'changed-hashes.json').read_text());pool=json.loads(Path(r'C:\Users\pauls\R148-offline\pool-hashes.json').read_text());baseline=json.loads((exp/'source-manifest.json').read_text(encoding='utf-8-sig'))
for f in ([] if '--continue-after-transfer' in sys.argv else baseline['files']):assert sha(root/f['path']) in (f['sha256'],pool.get(f['path']),changed.get(f['path'])),f['path']
with tarfile.open(base/'changed.tar') as t:t.extractall(root)
for n,h in changed.items():assert sha(root/n)==h,n
native_result=json.loads((exp/'native-gpuva-arm64/result.json').read_text(encoding='utf-8-sig'))
unit=next(u for u in native_result['units'] if any(a.endswith('\\Device.cpp') for a in u['argv']))
generated=Path(next(a for a in unit['argv'] if a.endswith('\\Device.cpp')))
expected=next(f['sha256'] for f in native_result['inputs'] if f['path']==str(generated))
text=generated.read_text();insert='   if (SUCCEEDED(result)) result = AgxD3d10WindowsFlushDeferredResources(pDevice->windows);\n'
assert sha(generated)==expected,'generated Device.cpp baseline mismatch'
start=text.index('\nFlush(D3D10DDI_HDEVICE');end=text.index('\n}',start)
body=text[start:end];needle='   if (SUCCEEDED(result)) result = AgxD3d10WindowsQueryCollect(pDevice->windows);'
assert body.count(needle)==1
body=body.replace(needle,insert+needle);generated.write_text(text[:start]+body+text[end:])
vc=Path(r'C:\VS2022Community\VC\Tools\MSVC\14.44.35207');sdk=Path(r'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0')
env=os.environ.copy();env['INCLUDE']=';'.join(map(str,[vc/'include',sdk/'km',sdk/'shared',sdk/'ucrt',sdk/'um']))
lines=Path(r'C:\Users\pauls\R145-offline\umd-CL.command.1.tlog').read_text(encoding='utf-16').splitlines()
parse=ctypes.windll.shell32.CommandLineToArgvW;parse.restype=ctypes.POINTER(ctypes.c_wchar_p);parse.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_int)]
name='agx_d3d10_windows.cpp';line=next(lines[i+1] for i,l in enumerate(lines) if l.startswith('^') and l.upper().endswith('\\'+name.upper()))
count=ctypes.c_int();ptr=parse('cl '+line,ctypes.byref(count));argv=[ptr[i] for i in range(1,count.value)];ctypes.windll.kernel32.LocalFree(ptr)
argv=[x for x in argv if not x.lower().startswith(('/fo','/fd'))];argv+=['/W4','/WX','/analyze','/Fo'+str(out/(name+'.obj')),'/Fd'+str(out/(name+'.pdb'))]
records=[]
def run(label,argv,cwd=None):
 r=subprocess.run(argv,cwd=cwd,env=env,capture_output=True,text=True);(out/(label+'.log')).write_text(r.stdout+r.stderr);print(label,r.returncode,r.stdout+r.stderr)
 records.append({'name':label,'argv':argv,'exit':r.returncode});(out/'result.json').write_text(json.dumps({'verified_inputs':len(baseline['files']),'changed':changed,'generated_before':expected,'generated_after':sha(generated),'records':records},indent=2));return r.returncode
status=run('winsys-arm64',[str(vc/'bin/HostX86/arm64/cl.exe'),*argv],root/'drivers/apple-agx/render-admission/umd')
if status:sys.exit(status)
argv=[x for x in unit['argv'] if not x.lower().startswith(('/fo','/fd')) and x not in ('/W2','/Zc:enumTypes','/Zc:preprocessor')]
argv=[('/imsvc'+x[2:]) if x.startswith('/I') and 'AD04-persistent-dwm-next' not in x else x for x in argv]
argv+=['/W4','/WX','/Fo'+str(out/'Device.obj')]
status=run('device-arm64',argv)
if status:sys.exit(status)
argv=[x for x in argv if x!='/c' and not x.lower().startswith('/fo')]+['/clang:--analyze','/clang:-Xanalyzer','/clang:-analyzer-output=text']
status=run('device-analyze',argv)
(out/'artifact-hashes.json').write_text(json.dumps({p.name:sha(p)for p in out.glob('*.obj')},indent=2));sys.exit(status)
