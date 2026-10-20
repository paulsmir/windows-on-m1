"""Render only actual current DDI R1 element-layout wrappers into a CPU fixture."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
FIXTURE = ROOT / 'tests/d3d12_r1_element_layout_fixture.cpp'
NAMES = ('CalcPrivateElementLayoutSize0010', 'CreateElementLayout0010')


def function(text, name):
    marker = 'static ' + ('SIZE_T' if name.startswith('Calc') else 'VOID') + ' APIENTRY ' + name + '('
    if text.count(marker) != 1:
        raise RuntimeError('actual DDI wrapper absent or ambiguous: ' + name)
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def render(destination):
    destination = Path(destination)
    destination.mkdir(exist_ok=False)
    text = SOURCE.read_text()
    snippets = [function(text, name) for name in NAMES]
    fixture = FIXTURE.read_text()
    assert fixture.count('/* ACTUAL_R1_ELEMENT_WRAPPERS */') == 1
    generated = fixture.replace('/* ACTUAL_R1_ELEMENT_WRAPPERS */', '\n\n'.join(snippets))
    unit = destination / 'actual-r1-element-wrappers.cpp'
    unit.write_text(generated)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    receipt = {'source': str(SOURCE), 'source_sha256': sha(SOURCE),
               'fixture_sha256': sha(FIXTURE),
               'functions': {name: hashlib.sha256(code.encode()).hexdigest()
                             for name, code in zip(NAMES, snippets)},
               'generated_sha256': sha(unit), 'actual_source_only': True,
               'host_type_model_not_windows_abi': True}
    (destination / 'generated-inputs.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return unit
