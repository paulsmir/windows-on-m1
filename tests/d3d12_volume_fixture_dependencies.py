"""Original source-only dependency closure for legacy volume fixture renderers."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]


def definition(text, marker):
    # Unique definition only; never consume a forward declaration's next body.
    pattern=re.escape(marker)+r'[^;{]*\{'
    found=list(re.finditer(pattern,text))
    if len(found)!=1:raise AssertionError('unique actual definition required: '+marker)
    start=found[0].start();opening=text.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]


def line_query_dependencies(owner):
    header=(ROOT/'.local/reference/wdk26100/d3d12umddi.h').read_text()
    constants=[]
    for name in ('D3D12_REQ_SUBRESOURCES','D3D12_REQ_TEXTURE1D_ARRAY_AXIS_DIMENSION','D3D12_REQ_TEXTURE1D_U_DIMENSION'):
        match=re.search(r'const UINT '+name+r'\s*=.*?;',header)
        if not match:raise AssertionError('pinned WDK constant missing: '+name)
        constants.append(match[0])
    # Existing renderers already provide actual texture1d_template before here.
    functions=[definition(owner,marker) for marker in (
        'static bool texture1d_array_template(',
        'extern "C" HRESULT AgxD3d12QueryTexture1DArrayLayout(')]
    return '\n'.join(constants+functions)+'\n'


def volume_record():
    """C21G: the actual AGX_D3D12_VOLUME_LAYOUT record and the layout registry
    (agx_d3d12_volume_registry.h) that VolumeLayoutInfo selects from."""
    header=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h').read_text()
    end=header.index('} AGX_D3D12_VOLUME_LAYOUT;')+len('} AGX_D3D12_VOLUME_LAYOUT;')
    registry=ROOT/'drivers/apple-agx/d3d12/agx_d3d12_volume_registry.h'
    return '#include "%s"\n' % registry+header[header.rfind('typedef struct {',0,end):end]
