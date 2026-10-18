from pathlib import Path
import os,sys,json,hashlib,subprocess
base=Path(r'C:\Users\pauls\R148-memory');out=base/'build';exp=Path(r'C:\Users\pauls\EXP861-r147-pagingroot');j=json.loads((exp/'native-gpuva-arm64/result.json').read_text(encoding='utf-8-sig'));unit=next(u for u in j['units'] if any(a.endswith('\\Device.cpp')for a in u['argv']))
source=Path(next(x for x in unit['argv']if x.endswith('\\Device.cpp')));text=source.read_text();insert='   if (SUCCEEDED(result)) result = AgxD3d10WindowsFlushDeferredResources(pDevice->windows);\n';assert text.count(insert)==1
baseline=out/'Device-baseline.cpp';baseline.write_text(text.replace(insert,''));expected=next(x['sha256']for x in j['inputs']if x['path']==str(source));assert hashlib.sha256(baseline.read_bytes()).hexdigest()==expected
vc=Path(r'C:\VS2022Community\VC\Tools\MSVC\14.44.35207');sdk=Path(r'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0');env=os.environ.copy();env['INCLUDE']=';'.join(map(str,[vc/'include',sdk/'shared',sdk/'ucrt',sdk/'um',sdk/'km']))
argv=[x for x in unit['argv'] if not x.lower().startswith(('/fo','/fd')) and x not in ('/W2','/Zc:enumTypes','/Zc:preprocessor')];argv=[('/imsvc'+x[2:])if x.startswith('/I')and'AD04-persistent-dwm-next'not in x else x for x in argv];argv+=['/I'+str(source.parent),'/W4','/WX'];records=[]
def run(name,args):
 r=subprocess.run(args,env=env,capture_output=True,text=True);(out/(name+'.log')).write_text(r.stdout+r.stderr);print(name,r.returncode,r.stdout+r.stderr);records.append({'name':name,'argv':args,'exit':r.returncode});return r
r=run('device-baseline-w4',[str(baseline)if x==str(source)else x for x in argv]+['/Fo'+str(out/'Device-baseline.obj')])
assert r.returncode and r.stderr.count('error: unused parameter')==7 and r.stderr.count('error:')==7,'unexpected baseline warnings'
argv+=['/clang:-Wno-unused-parameter']
r=run('device-arm64',argv+['/Fo'+str(out/'Device.obj')]);assert r.returncode==0
r=run('device-analyze',[x for x in argv if x!='/c']+['/clang:--analyze','/clang:-Xanalyzer','/clang:-analyzer-output=text']);assert r.returncode==0
(out/'device-result.json').write_text(json.dumps({'records':records,'baseline_sha256':expected,'generated_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'object_sha256':hashlib.sha256((out/'Device.obj').read_bytes()).hexdigest()},indent=2))
