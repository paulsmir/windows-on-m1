#!/usr/bin/env python3
"""Reproduce EXP861 memory accounting from saved UMD/ETL; no hardware access."""
import argparse,collections,hashlib,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--experiment',type=Path,default=Path('/Users/pavel/public_windows/.local/experiments/EXP861-r147-pagingroot'));p.add_argument('--etl-jsonl',type=Path);p.add_argument('--output',type=Path,default=Path(__file__).with_name('summary.json'));a=p.parse_args()
e=a.experiment; evidence=e/'hardware-evidence'; out={'inputs':{},'caveats':['UMD normal trace is capped at128 records per loaded module instance; log lacks time/device ID and no destroy receipt exists. Counts are observed lower bounds, not live allocation counts.','ETL ending without destroy does not establish a leak; pair identities and report trace window.']}
for f in [e/'heartbeat.jsonl',evidence/'state.json',evidence/'umd-final-after-loss.log',evidence/'post-recovery-events.json']:
 out['inputs'][str(f)]={'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
c=collections.defaultdict(collections.Counter);sizes=collections.defaultdict(collections.Counter)
for line in (evidence/'umd-final-after-loss.log').read_text().splitlines():
 m=re.match(r'(\S+) hr=(\S+) pid=(\d+) tid=(\d+)(.*)',line)
 if not m:continue
 stage,hr,pid,tid,values=m.groups();c[pid][stage]+=1
 if stage=='g4-native-allocate-cb' and hr=='0x00000000':
  v=[int(x,16) for x in values.split()];sizes[pid][f'class{v[0]}/bytes{v[1]}']+=1
out['umd_per_pid']={pid:{'devices_observed':x['g4-create-device-enter'],'canonical_allocates_observed':x['g4-native-allocate-cb'],'canonical_requested_bytes_observed':sum(int(k.split('bytes')[1])*n for k,n in sizes[pid].items()),'canonical_size_classes':sizes[pid]} for pid,x in c.items()}
s=json.loads((evidence/'state.json').read_text(encoding='utf-8-sig'));out['physical_snapshot']={k:s[k] for k in ['UTC','TotalPhysicalMemory','FreePhysicalMemoryKB']}
r=json.loads((evidence/'post-recovery-events.json').read_text(encoding='utf-8-sig'));out['resource2004']=r['Resource2004']
if a.etl_jsonl:
 out['inputs'][str(a.etl_jsonl)]={'bytes':a.etl_jsonl.stat().st_size,'sha256':hashlib.sha256(a.etl_jsonl.read_bytes()).hexdigest()}
 live={};devices={};counts=collections.Counter();unmatched=collections.Counter();totals=collections.defaultdict(collections.Counter);device_totals=collections.defaultdict(collections.Counter);windows={};completed=[]
 for line in a.etl_jsonl.read_text(encoding='utf-8-sig').splitlines():
  x=json.loads(line);i=x['id'];d=x['data'];t=x['t'];counts[str(i)]+=1
  if not windows:windows['first']=t
  windows['last']=t
  adapter=d.get('pDxgAdapter','');dev=d.get('hDevice','');pid=str(int(d.get('hProcessId',str(x['pid'])),0));key=(adapter,d.get('hVidMmGlobalAlloc',''))
  if i==27:devices[(adapter,dev)]={'pid':pid,'t':t};device_totals[pid]['creates']+=1
  elif i==28:
   old=devices.pop((adapter,dev),None);device_totals[(old or {}).get('pid',pid)]['destroys']+=1
  elif i==33:
   if key in live:unmatched['reused_live_global_handle']+=1
   live[key]={'pid':pid,'device':dev,'adapter':adapter,'t':t,**d};totals[pid]['created_bytes']+=int(d['allocSize']);totals[pid]['creates']+=1
  elif i==34:
   old=live.pop(key,None)
   if old:
    totals[old['pid']]['destroyed_bytes']+=int(old['allocSize']);totals[old['pid']]['destroys']+=1
   else:unmatched['destroy_without_create']+=1
 by_shape=collections.defaultdict(collections.Counter)
 for v in live.values():
  pid=v['pid'];n=int(v['allocSize']);totals[pid]['end_unretired_bytes']+=n;totals[pid]['end_unretired_count']+=1
  by_shape[pid][f"{v['adapter']}/bytes{n}/read{v['dwReadSegment']}/write{v['dwWriteSegment']}/flags{v['Flags']}/pt{v['PageTableOrDirectory']}"]+=1
 out['etl']={'window':windows,'counts':counts,'unmatched':unmatched,'per_pid':totals,'devices_per_pid':device_totals,'end_unretired_shapes':by_shape,'end_unretired_device_count':len(devices)}
a.output.write_text(json.dumps(out,indent=2)+'\n')
print(a.output)
