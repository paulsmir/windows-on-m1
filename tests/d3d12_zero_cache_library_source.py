"""Original WDK-declaration extraction; writes data only, never invokes tools."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
WDK = Path('/Users/pavel/public_windows/.local/reference/winsdk26100/um/d3d12umddi.h')
WDK_SHA256 = 'd338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def render(out):
    out = Path(out)
    out.mkdir(exist_ok=False)
    data = WDK.read_bytes()
    if hashlib.sha256(data).hexdigest() != WDK_SHA256:
        raise RuntimeError('pinned actual WDK changed')
    text = data.decode('utf-8-sig')
    first = 'typedef struct D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010'
    last = 'typedef struct D3D12DDI_DEVICE_FUNCS_CORE_0010'
    if text.count(first) != 1 or text.count(last) != 1:
        raise RuntimeError('ambiguous actual WDK library declarations')
    declarations = text[text.index(first):text.index(last)]
    names = [
        'CALC_PRIVATE_PIPELINE_LIBRARY_SIZE', 'CREATE_PIPELINE_LIBRARY',
        'DESTROY_PIPELINE_LIBRARY', 'ADD_PIPELINE_STATE_TO_LIBRARY',
        'CALC_SERIALIZED_LIBRARY_SIZE', 'SERIALIZE_LIBRARY',
    ]
    for name in names:
        if declarations.count('PFND3D12DDI_' + name + '_0010') != 1:
            raise RuntimeError('missing/duplicate actual callback typedef: ' + name)
    sdk = out / 'host-sdk'
    sdk.mkdir()
    (sdk / 'windows.h').write_bytes((ROOT / 'tests/d3d12_zero_cache_host_windows.h').read_bytes())
    (sdk / 'd3d12umddi.h').write_text(
        '#pragma once\n#include <windows.h>\n' + declarations)
    result = {
        'scope': 'Actual WDK records/typedefs plus original host shim; NOT Windows ABI',
        'WDK': str(WDK), 'WDK_sha256': WDK_SHA256,
        'declarations_sha256': hashlib.sha256(declarations.encode()).hexdigest(),
        'generated_files': {str(p.relative_to(out)): sha(p) for p in sorted(sdk.iterdir())},
    }
    (out / 'generated-inputs.json').write_text(json.dumps(result, indent=2) + '\n')
    return result
