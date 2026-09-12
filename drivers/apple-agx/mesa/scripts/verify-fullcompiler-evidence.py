"""Verify the saved compiler-only milestone, not GPU execution or conformance."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[4] / 'investigation/evidence/AD04-fullcompiler-001'
# scripts -> mesa -> apple-agx -> drivers -> repository
results = {}
for arch, count in [('x64', 48), ('arm64', 46)]:
    entries = json.loads((root / arch / 'result.json').read_text())
    assert len(entries) == count, (arch, len(entries))
    assert len({e['name'] for e in entries}) == count
    assert all(e['exit'] == 0 for e in entries), arch
    assert 'agx_compile' in {e['name'] for e in entries}
    assert 'agx_spill' in {e['name'] for e in entries}
    assert 'libagx' in {e['name'] for e in entries}
    assert 'link' in {e['name'] for e in entries}
    results[arch] = {'compile_units': 45, 'link': 'PASS',
                     'execution': 'PASS' if arch == 'x64' else 'NOT_RUN'}
for variant in (0, 1):
    native = (root / f'native-v{variant}/compute.bin').read_bytes()
    windows = (root / f'windows-v{variant}/compute.bin').read_bytes()
    assert len(native) == 160 and windows == native
    metadata = json.loads((root / f'x64/execute-{variant}.log').read_text())
    assert metadata['variant'] == variant
    assert metadata['compute']['binary_bytes'] == len(windows)
    fnv = 0xcbf29ce484222325
    for byte in windows:
        fnv = ((fnv ^ byte) * 0x100000001b3) & ((1 << 64) - 1)
    assert metadata['compute']['fnv1a64'] == f'0x{fnv:016x}'
    results[f'variant{variant}'] = {'bytes': len(windows), 'native_byte_exact': True,
                                  'sha256': hashlib.sha256(windows).hexdigest()}
assert results['variant0']['sha256'] != results['variant1']['sha256']
results['hardware'] = 'NOT_RUN'
results['scope'] = 'Full compiler build/link; two compute NIR fixtures, not full shader conformance'
(root / 'verified.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
