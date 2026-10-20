"""Render actual current DDI root Calc/Create and private layout into a fixture."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
FIXTURE = ROOT / 'tests/d3d12_r1_root_preflight_fixture.cpp'
TYPES = ('AgxD3d12RootParameter', 'AgxD3d12Range', 'AgxD3d12RootSignature')
FUNCTIONS = (
    'static AgxD3d12Range *RangesOf(',
    'static D3D12DDI_STATIC_SAMPLER *StaticSamplersOf(',
    'static UINT RootParameterDwords(',
    'static SIZE_T APIENTRY CalcPrivateRootSignatureSize(',
    'static HRESULT APIENTRY CreateRootSignature(',
)


def struct_source(text, name):
    marker = 'struct ' + name + ' {'
    if text.count(marker) != 1:
        raise RuntimeError('ambiguous private root type: ' + name)
    start = text.index(marker)
    return text[start:text.index('};', start) + 2]


def function(text, marker):
    if text.count(marker) != 1:
        raise RuntimeError('ambiguous actual root callback: ' + marker)
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
    types = '\n'.join(struct_source(text, name) for name in TYPES)
    policy_marker = 'static constexpr AgxR1RootLayoutPolicy kCurrentRootLayoutPolicy = '
    if text.count(policy_marker) != 1:
        raise RuntimeError('actual root policy absent or ambiguous')
    at = text.index(policy_marker)
    policy = text[at:text.index(';', at) + 1]
    callbacks = policy + '\n\n' + '\n\n'.join(function(text, marker) for marker in FUNCTIONS)
    fixture = FIXTURE.read_text()
    assert fixture.count('/* ACTUAL_ROOT_PRIVATE_TYPES */') == 1
    assert fixture.count('/* ACTUAL_ROOT_CALLBACKS */') == 1
    generated = fixture.replace('/* ACTUAL_ROOT_PRIVATE_TYPES */', types).replace(
        '/* ACTUAL_ROOT_CALLBACKS */', callbacks)
    unit = destination / 'actual-root-preflight.cpp'
    unit.write_text(generated)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    receipt = {'source_sha256': sha(SOURCE), 'fixture_sha256': sha(FIXTURE),
               'types_sha256': hashlib.sha256(types.encode()).hexdigest(),
               'callbacks_sha256': hashlib.sha256(callbacks.encode()).hexdigest(),
               'generated_sha256': sha(unit), 'actual_source_only': True,
               'host_type_model_not_windows_abi': True}
    (destination / 'generated-inputs.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return unit
