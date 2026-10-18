from pathlib import Path
import subprocess,base64
b=Path(__file__).parent
ssh=['ssh','-i',str(Path.home()/'.ssh/windows_builder'),'-o','BatchMode=yes','-o','ConnectTimeout=7','pauls@192.168.1.24']
def ps(s):
 r=subprocess.run(ssh+['powershell.exe -NoProfile -ExecutionPolicy Bypass -EncodedCommand '+base64.b64encode(s.encode('utf-16le')).decode()],capture_output=True,timeout=240)
 print(r.stdout.decode(errors='replace'),r.stderr.decode(errors='replace'),flush=True)
 r.check_returncode()
ps("New-Item -ItemType Directory -Force C:\\agx\\r153-offline | Out-Null")
subprocess.run(['scp','-q','-i',str(Path.home()/'.ssh/windows_builder'),*[str(b/n) for n in ['baseline-hashes.json','source-hashes.json','changed.tar','compile.py']],'pauls@192.168.1.24:C:/agx/r153-offline/'],check=True,timeout=90)
ps("python C:\\agx\\r153-offline\\compile.py; exit $LASTEXITCODE")
for n in ['compile-result.json','builder-before.json',*[ 'compile/'+n+'.log' for n in ['apple_agx_exp208_dynamic.c','apple_agx_render_shared_memory.c','apple_agx_g13_queue_provider.c','gpuva_g3_windows.c','backend_platform_windows.c','render_backend_image.c']]]:
 subprocess.run(['scp','-q','-i',str(Path.home()/'.ssh/windows_builder'),'pauls@192.168.1.24:C:/agx/r153-offline/'+n,str(b/Path(n).name)],check=True,timeout=60)
