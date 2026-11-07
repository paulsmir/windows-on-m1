"""Original current-ROOT actual-function C24 renderers; no private candidate fallback."""
from pathlib import Path
import re
import d3d12_texture1d_source as line
from test_dxbc_to_nir import ROOT
from test_d3d12_view_mapping import block


def definition(text, marker):
    pattern = re.escape(marker) if marker.endswith('{') else re.escape(marker) + r'[^;{]*\{'
    found = list(re.finditer(pattern, text))
    if len(found) != 1:
        raise AssertionError('unique actual definition required: ' + marker)
    return block(text[found[0].start():], marker)


def current():
    return ((ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text(),
            (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text())


def native_source():
    return line.native_source() + '\n' + (ROOT / 'tests/d3d12_texture1d_array_native_fixture.c').read_text()


def create_source():
    # C23 base now owns all actual common/array query+constructor dependencies.
    # Keep only the distinct C24 oracle and finite native-buffer stand-in policy.
    text = line.create_source().split('int main()')[0]
    text = text.replace('assert(p);return 0x1000000;', 'assert(p);return p->target==PIPE_BUFFER?0x1000000:0;')
    return text + (ROOT / 'tests/d3d12_texture1d_array_create_fixture.cpp').read_text()


def view_source():
    # The established C23 renderer already extracts the actual current DDI,
    # owner and named WDK-shaped shim, including definition-qualified helpers.
    text = line.view_source().split('static pipe_sampler_view captured;')[0]
    return '#include <initializer_list>\n' + text + (ROOT / 'tests/d3d12_texture1d_array_view_fixture.cpp').read_text()


def copy_source():
    ddi, _ = current()
    text = line.copy_source().split('int main()')[0]
    text = text.replace('struct AgxD3d12Command {', definition(ddi, 'struct AgxD3d12Copy {') + ';\nstruct AgxD3d12Command {', 1)
    text = text.replace('UINT kind;AgxD3d12TextureCopy', 'UINT kind;AgxD3d12Copy copy;AgxD3d12TextureCopy', 1)
    text = text.replace('kCommandTextureCopy=4,', 'kCommandCopy=1,kCommandTextureCopy=4,', 1)
    text += '\nstruct D3D12DDI_HRESOURCE {void*pDrvPrivate;};\n' + definition(ddi, 'static VOID APIENTRY ResourceCopy(') + '\n'
    return text + (ROOT / 'tests/d3d12_texture1d_array_copy_fixture.cpp').read_text()
