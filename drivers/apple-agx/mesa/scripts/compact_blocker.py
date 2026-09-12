"""Bounded context extraction, with no model, build or hardware authority."""
import argparse
from collections import deque
import json
from pathlib import Path
import re
import subprocess

def first_diagnostic(lines):
    chain = deque(maxlen=8)
    for line in lines:
        if 'In file included from ' in line:
            chain.append(line.strip())
        if re.search(r'\b(?:fatal )?error(?: [A-Z]+\d+)?:', line):
            return {'first_error': line.strip()[:1500], 'include_chain': list(chain)}
    return {'first_error': None, 'include_chain': list(chain)}

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--context', type=Path, required=True)
    p.add_argument('--log', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    root = Path(__file__).resolve().parents[4]
    context = json.loads(args.context.read_text())
    packet = {'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
              'last_pass': context['last_pass'], 'first_error': context.get('first_error'),
              'include_or_call_chain': context['include_or_call_chain'],
              'definitions': context['definitions'], 'overlays': context['overlays'],
              'constraints': context['constraints'], 'source': []}
    if args.log:
        with args.log.open(errors='replace') as stream:
            result = first_diagnostic(stream)
        packet['first_error'] = result['first_error']
        packet['include_or_call_chain'] = result['include_chain']
    total = 0
    for excerpt in context['source']:
        path = Path(excerpt['path'])
        if not path.is_absolute():
            path = root / path
        start, end = excerpt['start'], excerpt['end']
        if not (1 <= start <= end) or total + end - start + 1 > 100:
            raise SystemExit('Source context exceeds 100 lines or has invalid bounds')
        total += end - start + 1
        lines = path.read_text().splitlines()
        if end > len(lines):
            raise SystemExit('Stale source excerpt bounds')
        packet['source'].append({'path': str(path), 'start': start,
                                 'text': '\n'.join(lines[start-1:end])})
    if total < 30:
        raise SystemExit('At least 30 relevant source lines required')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(packet, indent=2) + '\n')
    print(json.dumps({'packet': str(args.output), 'head': packet['head'],
                      'first_error': packet['first_error'], 'source_lines': total}))

if __name__ == '__main__':
    main()
