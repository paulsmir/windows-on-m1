#!/usr/bin/env python3
"""Render actual source functions/records only. Never compile or execute a gate."""
import hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HERE=Path(__file__).resolve().parent

def block(text,marker):
 start=text.index(marker);end=text.index('{',start)+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]

def declaration(text,name):
 found=re.search(r'typedef struct '+re.escape(name)+r'\s*\{.*?\}\s*'+re.escape(name)+r'\s*;',text,re.S)
 if not found:raise ValueError('pinned declaration missing: '+name)
 return found.group()

def owner_record(header,name):
 end=header.index('} '+name+';')+len('} '+name+';')
 return header[header.rfind('typedef struct {',0,end):end]

def render(destination):
 ddi=ROOT/'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
 owner=ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h'
 wdk=ROOT/'.local/reference/wdk26100/d3d12umddi.h'
 source,header,wdk_source=ddi.read_text(),owner.read_text(),wdk.read_text()
 structs=['D3D12DDI_RENDER_TARGET_BLEND_DESC','D3D12DDI_BLEND_DESC',
 'D3D12DDI_DEPTH_STENCILOP_DESC','D3D12DDI_DEPTH_STENCIL_DESC',
 'D3D12DDI_RASTERIZER_DESC','D3D12DDIARG_INPUT_ELEMENT_DESC',
 'D3D12DDIARG_CREATE_PIPELINE_STATE_0001']
 enums=['D3D12DDI_BLEND','D3D12DDI_BLEND_OP','D3D12DDI_LOGIC_OP',
 'D3D12DDI_DEPTH_WRITE_MASK','D3D12DDI_COMPARISON_FUNC','D3D12DDI_STENCIL_OP',
 'D3D12DDI_FILL_MODE','D3D12DDI_CULL_MODE','D3D12DDI_CONSERVATIVE_RASTERIZATION_MODE',
 'D3D12DDI_INPUT_CLASSIFICATION','D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE',
 'D3D12DDI_PRIMITIVE_TOPOLOGY_TYPE']
 # Enum numeric values are not evaluated here; these aliases support actual WDK
 # records on the host. This gate tests publication, not Windows ABI layout.
 records='\n'.join('using '+name+'=unsigned;'for name in enums)+'\n'
 records+='static constexpr UINT D3D12DDI_INPUT_CLASSIFICIATION_PER_INSTANCE_DATA=1;\n'
 records+='\n'.join(declaration(wdk_source,name)for name in structs)
 actual_records='\n'.join(owner_record(header,name)for name in
 ['AGX_D3D12_COMPUTE','AGX_D3D12_GRAPHICS_DESC','AGX_D3D12_GRAPHICS'])
 functions={name:block(source,marker)for name,marker in [
 ('private_PSO','struct AgxD3d12PipelineState {'),
 ('CompileCompute','static HRESULT CompileCompute('),
 ('CompileGraphics','static HRESULT CompileGraphics('),
 ('DestroyPipelineState','static VOID APIENTRY DestroyPipelineState(D3D12DDI_HDEVICE handle,')]}
 limit=re.search(r'static constexpr UINT kPipelineElements\s*=\s*\d+\s*;',source)
 if not limit:raise ValueError('actual pipeline limit missing')
 actual=limit.group()+'\n'+functions['private_PSO']+';\n'
 actual+='static void RetirePsoIdentity(AgxD3d12PipelineState *) {}\n'
 actual+='\n'.join(functions[n]for n in ['CompileCompute','CompileGraphics','DestroyPipelineState'])
 text=(HERE/'d3d12_pso_publication_fixture.cpp').read_text().replace('/* PINNED_WDK_TYPES */',records)
 text=text.replace('/* ACTUAL_OWNER_TYPES */',actual_records).replace('/* ACTUAL_PSO_FUNCTIONS */',actual)
 destination.mkdir(parents=True,exist_ok=True);path=destination/'rendered.cpp';path.write_text(text)
 files=[ddi,owner,wdk,HERE/'d3d12_pso_publication_fixture.cpp',Path(__file__).resolve(),
 ROOT/'drivers/apple-agx/mesa/dxbc/dxbc_bindings.h',ROOT/'drivers/apple-agx/mesa/dxbc/dxbc_resources.h']
 receipt={'scope':'PREPARE_ONLY no compiler or native execution','input_files':{str(p):hashlib.sha256(p.read_bytes()).hexdigest()for p in files},'actual_functions':{n:hashlib.sha256(t.encode()).hexdigest()for n,t in functions.items()},'rendered_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
 (destination/'inputs.json').write_text(json.dumps(receipt,indent=2)+'\n')
 return path

if __name__=='__main__':
 import argparse
 ap=argparse.ArgumentParser();ap.add_argument('directory',type=Path);args=ap.parse_args()
 print(render(args.directory))
