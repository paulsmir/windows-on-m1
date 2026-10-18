from pathlib import Path
import subprocess,base64
b=Path(__file__).parent
ssh=['ssh','-i',str(Path.home()/'.ssh/windows_builder'),'-o','BatchMode=yes','-o','ConnectTimeout=7','pauls@192.168.1.24']
def ps(s):
 r=subprocess.run(ssh+['powershell.exe -NoProfile -ExecutionPolicy Bypass -EncodedCommand '+base64.b64encode(s.encode('utf-16le')).decode()],capture_output=True,timeout=240)
 print(r.stdout.decode(errors='replace'),r.stderr.decode(errors='replace'),flush=True)
 r.check_returncode()
ps("New-Item -ItemType Directory -Force C:\\agx\\r154-epoch | Out-Null")
subprocess.run(['scp','-q','-i',str(Path.home()/'.ssh/windows_builder'),*[str(b/n) for n in ['baseline-hashes.json','source-hashes.json','changed.tar','compile.py']],'pauls@192.168.1.24:C:/agx/r154-epoch/'],check=True,timeout=90)
ps("python C:\\agx\\r154-epoch\\compile.py; exit $LASTEXITCODE")
for n in ['compile-result.json','builder-before.json','compile/gpuva_g3_windows.c.log']:
 subprocess.run(['scp','-q','-i',str(Path.home()/'.ssh/windows_builder'),'pauls@192.168.1.24:C:/agx/r154-epoch/'+n,str(b/Path(n).name)],check=True,timeout=60)
