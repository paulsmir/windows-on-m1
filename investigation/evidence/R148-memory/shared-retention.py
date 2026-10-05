#!/usr/bin/env python3
"""Pair creator device epochs and live per-process references; avoids pointer reuse."""
import collections,json
from pathlib import Path
base=Path(__file__).parent
live={};refs={};devices={};closed={};create_device={};rows=[];first=None;last=None
for line in (base/'lifetime-events.jsonl').read_text().splitlines():
 e=json.loads(line);d=e['data'];i=e['id'];t=e['t'];first=first or t;last=t
 adapter=d.get('pDxgAdapter');key=(adapter,d.get('hVidMmGlobalAlloc'));dk=(adapter,d.get('hDevice'))
 if i==27:devices[dk]=e
 elif i==28:
  old=devices.pop(dk,None)
  if old:closed[(dk,old['t'])]=e
 elif i==33:live[key]=e;create_device[key]=(dk,devices.get(dk,{}).get('t'))
 elif i==34:live.pop(key,None)
 elif i==36:refs[(adapter,d['hVidMmAlloc'])]=e
 elif i==37:refs.pop((adapter,d['hVidMmAlloc']),None)
byglobal=collections.defaultdict(list)
for r in refs.values():byglobal[(r['data']['pDxgAdapter'],r['data']['hVidMmGlobalAlloc'])].append(r)
consumer='0xffff8b0968cfa520'
summary=collections.defaultdict(collections.Counter)
for k,e in live.items():
 rr=byglobal[k]
 if not any(r['data']['hDevice']==consumer for r in rr):continue
 destroyed=closed.get(create_device[k]);pid=str(e['pid']);sz=int(e['data']['allocSize'])
 summary[pid]['count']+=1;summary[pid]['bytes']+=sz
 if destroyed:summary[pid]['creator_destroyed_count']+=1;summary[pid]['creator_destroyed_bytes']+=sz
 rows.append({'global_create':e,'creator_destroy':destroyed,'live_process_references':rr})
result={'window':[first,last],'consumer_device':consumer,'consumer_pid':1260,'per_creator_pid':summary,'rows':rows}
(base/'shared-retention.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(summary,indent=2))
