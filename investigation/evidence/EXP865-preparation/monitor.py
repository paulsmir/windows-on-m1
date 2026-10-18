from pathlib import Path
import subprocess,base64,time,json,datetime
b=Path(__file__).parent
enc=base64.b64encode((b/'sample.ps1').read_text().encode('utf-16le')).decode()
cmd=['ssh','-i',str(Path.home()/'.ssh/air'),'-o','BatchMode=yes','-o','ConnectTimeout=5','-o','ConnectionAttempts=1','-o','StrictHostKeyChecking=yes','-o','UserKnownHostsFile=/Users/pavel/public_windows/.local/experiments/EXP641-standard-present/air_known_hosts','pavel@192.168.1.37','powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand '+enc]
first=None;start=time.monotonic();last_good=None
with (b/'heartbeat.jsonl').open('a',buffering=1) as f:
 while time.monotonic()-start<900:
  try:
   r=subprocess.run(cmd,capture_output=True,timeout=25)
   if r.returncode:raise RuntimeError('SSH exit '+str(r.returncode))
   s=json.loads(r.stdout.decode('utf-8-sig'));now=datetime.datetime.fromisoformat(s['Utc'].replace('Z','+00:00'));boot=datetime.datetime.fromisoformat(s['Boot'].replace('Z','+00:00'));s['ElapsedBootSeconds']=(now-boot).total_seconds()
   if s.get('Problem') != 0 or not s.get('Present') or s.get('Arm'):
    f.write(json.dumps({'ExcludedNonOriginalSample':s})+'\n');raise SystemExit(4)
   if first is None:first=s['Boot'];(b/'window-state-first.json').write_text(json.dumps(s,indent=2)+'\n')
   if s['Boot']!=first:raise RuntimeError('BOOT_CHANGED')
   last_good=time.monotonic();f.write(json.dumps(s)+'\n');print(json.dumps(s),flush=True)
   (b/'window-state-final.json').write_text(json.dumps(s,indent=2)+'\n')
   if s['ElapsedBootSeconds']>=600:print('WINDOW_600_SECONDS_REACHED',flush=True);break
  except Exception as e:
   v={'Utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'Error':str(e)};f.write(json.dumps(v)+'\n');print(json.dumps(v),flush=True)
   if 'BOOT_CHANGED' in str(e) or (last_good and time.monotonic()-last_good>90):raise SystemExit(2)
  time.sleep(30)
 else:raise SystemExit(3)
