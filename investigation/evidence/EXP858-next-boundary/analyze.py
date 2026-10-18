"""Reproduce EXP858 offline counts from the saved, decoded evidence (no machine I/O)."""
import argparse
from collections import Counter, defaultdict
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path, help='main repository .local/experiments')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
raw = args.root / 'EXP858-r145-staging'
decoded = args.root / 'EXP858-offline-analysis'
rows = []
for name in ['allocation-events.json', 'paging-events.json', 'context-events.json']:
    rows += json.loads((decoded / name).read_text())
rows.sort(key=lambda r: (r['t'], r['record']))
assert len({(r['record'], r['id'], r['t']) for r in rows}) == len(rows)
apple = '0xffffdc0895be0000'
devices, contexts, live, allocations = {}, {}, {}, {}
closed, packets, unmatched_stops, overwritten_starts = [], Counter(), 0, 0
for row in rows:
    d, event = row['data'], row['id']
    if event == 27:
        devices[d['hDevice']] = {'start': row, 'stop': None}
    elif event == 28 and d['hDevice'] in devices:
        devices[d['hDevice']]['stop'] = row
    elif event == 30:
        device = devices.get(d['hDevice'])
        contexts[d['hContext']] = device['start']['data']['pDxgAdapter'] if device else 'unknown'
    elif event in [175, 178]:
        packets[(event, contexts.get(d['hContext'], 'unknown'), d['PacketType'])] += 1
    if event == 33 and d['pDxgAdapter'] == apple:
        key = d['hVidMmGlobalAlloc']
        if key in live:
            overwritten_starts += 1
        live[key] = {'start': row, 'device': devices.get(d['hDevice']), 'terminate': None}
    elif event == 36 and d['hVidMmGlobalAlloc'] in live:
        allocations[d['hVidMmAlloc']] = live[d['hVidMmGlobalAlloc']]
    elif event == 39 and d['hVidMmAlloc'] in allocations:
        allocations[d['hVidMmAlloc']]['terminate'] = row
    elif event == 34 and d['pDxgAdapter'] == apple:
        old = live.pop(d['hVidMmGlobalAlloc'], None)
        if old is None:
            unmatched_stops += 1
        else:
            closed.append((old, row))
shapes = defaultdict(lambda: {'count': 0, 'bytes': 0, 'samples': []})
for allocation in live.values():
    r = allocation['start']; d = r['data']
    key = f"flags={int(d['Flags']):#x},stopped={bool(allocation['device'] and allocation['device']['stop'])},termination={bool(allocation['terminate'])}"
    v = shapes[key]; v['count'] += 1; v['bytes'] += int(d['allocSize'])
    if len(v['samples']) < 2:
        v['samples'].append({'create': r, 'device_stop': allocation['device']['stop'] if allocation['device'] else None, 'terminate': allocation['terminate']})
trace = (raw / 'hardware-evidence/umd.log').read_text().splitlines()
by_pid = defaultdict(Counter)
for line in trace:
    by_pid[re.search(r'pid=(\d+)', line)[1]][line.split()[0]] += 1
blocks = []
for line in [x for x in trace if 'pid=8148 ' in x]:
    if not blocks or (line.startswith('g4-open-adapter-enter') and len(blocks[-1]) == 128):
        blocks.append([])
    blocks[-1].append(line)
assert len(blocks) == 405 and all(len(b) == 128 for b in blocks)
assert by_pid['1232']['g4-create-device-enter'] == 2
assert sum(by_pid['1232'].values()) == 128
latencies = sorted((datetime.fromisoformat(b['t']) - datetime.fromisoformat(a['start']['t'])).total_seconds() for a, b in closed)
# Native decoder counts cross-check every selected event, independently of XML queries.
native_text = (decoded / 'raw-stacks.txt').read_text(encoding='utf-8-sig')
native_counts = {int(a): int(b) for a, b in re.findall(r'^ID (\d+) (\d+)', native_text, re.M)}
counts = Counter(r['id'] for r in rows)
for event, count in counts.items():
    assert native_counts[event] == count, (event, count, native_counts[event])
manifest = {}
for name in ['hardware-evidence/umd.log', 'hardware-evidence/EXP801DxgBoot.etl', 'hardware-evidence/WER-explorer.exe.2788.dmp', 'AppleAgxRenderAdmissionUmd.dll', 'AppleAgxRenderAdmissionUmd.pdb', 'source-manifest.json']:
    file = raw / name
    digest = hashlib.file_digest(file.open('rb'), 'sha256').hexdigest()
    manifest[str(file.relative_to(args.root))] = {'sha256': digest, 'bytes': file.stat().st_size}
for name in ['allocation-events.json', 'paging-events.json', 'context-events.json', 'focused-events0.xml', 'focused-events1.xml', 'paging-events.xml', 'context-events.xml', 'raw-stacks.txt', 'boundary-stacks.txt', 'dump.txt', 'dump-detail.txt', 'dump-runtime.txt', 'dump-owners.txt', 'dump-boundary.txt', 'copy-call-stacks.txt', 'readetl.cpp', 'read-boundary.cpp', 'read-copy-calls.cpp', 'etl-focused0.ps1', 'etl-focused1.ps1', 'etl-paging.ps1', 'etl-context.ps1', 'parse_etl.py']:
    file = decoded / name
    manifest[str(file.relative_to(args.root))] = {'sha256': hashlib.file_digest(file.open('rb'), 'sha256').hexdigest(), 'bytes': file.stat().st_size}
copy_stacks = (decoded / 'copy-call-stacks.txt').read_text(encoding='utf-8-sig').splitlines()
copy_pids, copy_sites = Counter(), Counter()
for index, line in enumerate(copy_stacks):
    if ' id=105 ' not in line:
        continue
    copy_pids[int(re.search(r'pid=(\d+)', line)[1])] += 1
    stack = copy_stacks[index+1].split('data=')[1].split(',')
    copy_sites[stack[stack.index('00007ffd7200af60')+1]] += 1
assert sum(copy_pids.values()) == 93
assert dict(copy_sites) == {'00007ffd7200be04': 93}
result = {
    'copy_calls_by_pid': dict(copy_pids),
    'copy_call_parent_sites': dict(copy_sites),
    'scope': 'Offline saved EXP858 evidence; no driver edits, package or Air operation',
    'trace_counts_by_pid': dict(by_pid),
    'pid8148_blocks': {'blocks': len(blocks), 'lines_each': 128},
    'etl_header': native_text.splitlines()[0],
    'etl_native_summary': next(x for x in native_text.splitlines() if x.startswith('SUMMARY')),
    'selected_event_counts': dict(counts),
    'selected_first_utc': rows[0]['t'], 'selected_last_utc': rows[-1]['t'],
    'packet_counts': [{'id': i, 'adapter': a, 'packet_type': t, 'count': n} for (i, a, t), n in sorted(packets.items())],
    'apple_allocation_replay': {
        'closed_pairs': len(closed), 'overwritten_starts': overwritten_starts,
        'unmatched_stops': unmatched_stops,
        'live_count': len(live), 'live_bytes': sum(int(v['start']['data']['allocSize']) for v in live.values()),
        'closed_median_seconds': latencies[len(latencies)//2], 'closed_max_seconds': max(latencies),
        'closed_flags_counts': dict(Counter(a['start']['data']['Flags'] for a, b in closed)),
        'live_shapes': dict(shapes),
        'limitations': 'Pointer-plus-lifetime joins, not PID totals: destruction often runs on PID4. ETL reports six lost buffers; seven overwritten starts and no unmatched stops bound the census, so it is not a complete accounting of the 600-second counters.'
    },
    'make_resident_statuses': dict(Counter(r['data']['Status'] for r in rows if r['id'] == 339)),
    'reserve_resource_statuses': dict(Counter(r['data']['Status'] for r in rows if r['id'] == 68)),
    'device_error_events': [r for r in rows if r['id'] == 467],
    'representative_boundary_events': [r for r in rows if r['pid']==1224 and r['tid']==5908 and '2026-09-28T04:30:43.34' < r['t'] < '2026-09-28T04:30:43.60' and r['id'] in [338,339,297,367,42,39,182]],
    'manifest': manifest,
}
args.output.write_text(json.dumps(result, indent=2, sort_keys=True)+'\n')
print(json.dumps({'paired_destructions':len(closed),'overwritten_starts':overwritten_starts,'unmatched_stops':unmatched_stops,'live_bytes':result['apple_allocation_replay']['live_bytes'],'packet_counts':result['packet_counts']},indent=2))
