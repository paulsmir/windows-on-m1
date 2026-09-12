#!/usr/bin/env python3
"""Request bounded structured data from loopback Ollama; never execute it."""
import argparse
import hashlib
import json
import time
import urllib.request
import fcntl
import os
from pathlib import Path

MODEL = 'devstral-small-2:24b-instruct-2512-q4_K_M'
PREFIX = ('You are a bounded Tier C patch proposal worker. You have no shell authority. '
          'Return JSON only. Do not choose the next project action. '
          'Do not invent tools or execution results. Follow the supplied contract.')
REVIEW_SCHEMA = {
    'type':'object', 'additionalProperties':False,
    'required':['review','scope','semantics','test_coverage','unauthorized_expansion','architecture_risk'],
    'properties': {'review': {'type':'string', 'enum':['PASS','FAIL','QUESTION']},
                   **{k:{'type':'string'} for k in ['scope','semantics','test_coverage',
                                                   'unauthorized_expansion','architecture_risk']}}
}

def gpu_record():
    with urllib.request.urlopen('http://127.0.0.1:11434/api/ps', timeout=5) as response:
        models = json.load(response)['models']
    exact = [m for m in models if m.get('name') == MODEL]
    if len(exact) != 1:
        raise ValueError('GPU_RESIDENCY_FAILED: intended model not loaded')
    m = exact[0]
    total, vram = m.get('size', 0), m.get('size_vram', 0)
    ratio = vram / total if total > 0 else 0
    if ratio < 0.95 or m.get('context_length') != 4096:
        raise ValueError(f'GPU_RESIDENCY_FAILED: total={total} vram={vram} ratio={ratio}')
    return {'local_model': MODEL, 'gpu_preflight': 'PASS', 'size_total':total,
            'size_vram':vram, 'gpu_residency_ratio':ratio, 'num_ctx':4096,
            'processor':'GPU-resident per api/ps', 'keep_alive':'10m'}

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--review', action='store_true')
    p.add_argument('--warm-only', action='store_true')
    p.add_argument('--max-output', type=int, default=384)
    a = p.parse_args()
    if not 16 <= a.max_output <= 2048:
        p.error('max-output must be between 16 and 2048')
    if a.output.exists():
        p.error('preserve existing evidence; choose a fresh output directory')
    context = a.input.read_text()
    if len(context.encode()) > 16000:
        p.error('context exceeds 16KB')
    a.output.mkdir(parents=True)
    (a.output / 'input.json').write_text(context)
    lock_fd = os.open('/private/tmp/apple-agx-devstral-session.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    lock = os.fdopen(lock_fd, 'a+')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    if not a.warm_only:
        try:
            record = gpu_record()
        except Exception as error:
            record = {'local_model':MODEL, 'gpu_preflight':'FAIL',
                      'result':'LOCAL_WORKER_BLOCKED', 'reason':'GPU_RESIDENCY_FAILED',
                      'detail':str(error)}
            (a.output / 'gpu-preflight.json').write_text(json.dumps(record, indent=2)+'\n')
            print(json.dumps(record))
            raise SystemExit(2)
        (a.output / 'gpu-preflight.json').write_text(json.dumps(record, indent=2)+'\n')
    request = {'model': MODEL, 'stream': True, 'format': 'json', 'keep_alive': '10m',
               'options': {'num_ctx': 4096, 'num_predict': a.max_output,
                           'num_thread': 2, 'temperature': 0},
               'messages': [{'role': 'system', 'content': PREFIX +
                 (' You are a fresh read-only reviewer. Do not return edits.' if a.review else '')},
                            {'role': 'user', 'content': context}]}
    if a.warm_only:
        request['messages'] = [{'role':'user', 'content':'Reply 1'}]
        request['options']['num_predict'] = 1
        request.pop('format')
    elif a.review:
        request['format'] = REVIEW_SCHEMA
    data = json.dumps(request).encode()
    req = urllib.request.Request('http://127.0.0.1:11434/api/chat', data=data,
                                 headers={'Content-Type': 'application/json'})
    start = time.monotonic()
    try:
        parts = []
        raw_size = 0
        with urllib.request.urlopen(req, timeout=150) as response, (a.output / 'response.ndjson').open('wb') as log:
            for line in response:
                log.write(line)
                log.flush()
                raw_size += len(line)
                if raw_size > 1_000_000 or time.monotonic()-start > 360:
                    raise ValueError('response exceeds byte/time cap')
                result = json.loads(line)
                if 'error' in result:
                    raise ValueError(result['error'])
                parts.append(result.get('message', {}).get('content', ''))
                if result.get('done'):
                    break
        if not result.get('done'):
            raise ValueError('stream ended without completion')
        if a.warm_only:
            record = gpu_record()
            (a.output / 'gpu-preflight.json').write_text(json.dumps(record, indent=2)+'\n')
            print(json.dumps(record))
            return
        (a.output / 'gpu-postflight.json').write_text(json.dumps(gpu_record(), indent=2)+'\n')
        parsed = json.loads(''.join(parts))
        (a.output / 'proposal.json').write_text(json.dumps(parsed, indent=2)+'\n')
        meta = {k: result.get(k) for k in ['model', 'done', 'done_reason',
                                         'prompt_eval_count', 'eval_count', 'load_duration']}
        meta.update(elapsed_seconds=round(time.monotonic()-start, 2),
                    raw_bytes=raw_size, context_bytes=len(context.encode()),
                    input_sha256=hashlib.sha256(context.encode()).hexdigest(),
                    execution_authority='NONE')
        (a.output / 'usage.json').write_text(json.dumps(meta, indent=2)+'\n')
        print(json.dumps(meta))
    except Exception as error:
        (a.output / 'failure.json').write_text(json.dumps({'error':str(error),
                         'elapsed_seconds':time.monotonic()-start})+'\n')
        raise

if __name__ == '__main__':
    main()
