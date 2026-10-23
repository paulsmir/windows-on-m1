"""Original actual-function source renderer. No compiler invoked on import."""
from pathlib import Path
import re,hashlib

def block(source,marker):
 start=source.index(marker);brace=source.index('{',start);depth=1;i=brace+1
 while depth:
  if source[i]=='{':depth+=1
  elif source[i]=='}':depth-=1
  i+=1
 return source[start:i]

def render(ddi,wdk,fixture):
 assert hashlib.sha256(wdk.read_bytes()).hexdigest()=='d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28'
 header=wdk.read_text();source=ddi.read_text();decls=[]
 names=['D3D12DDI_RESOURCE_FLAGS_0003','D3D12DDI_RESOURCE_TYPE','D3D12DDI_TEXTURE_LAYOUT','D3D12DDI_RESOURCE_STATES','D3D12DDIARG_HRESOURCE_PLACEMENT','D3D12DDIARG_BUFFER_PLACEMENT','D3D12DDIARG_ROW_MAJOR_RESOURCE_LAYOUT','D3D12DDIARG_CREATERESOURCE_0003','D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS','D3D12DDI_RESOURCE_ALLOCATION_INFO']
 for name in names:
  m=re.search(r'typedef (?:struct|enum) '+name+r'\s*\{.*?\}\s*'+name+r'\s*;',header,re.S)
  if not m:raise ValueError(name)
  decls.append(m.group())
 functions='\n'.join(block(source,n)+';' for n in ['struct AgxD3d12Heap {','struct AgxD3d12Resource {'])+'\n'
 functions+='\n'.join(block(source,n) for n in ['static UINT64 VolumeAllocationQuery(', 'static VOID APIENTRY CheckResourceAllocationInfo(', 'static VOID APIENTRY CheckExistingResourceAllocationInfo('])
 return fixture.read_text().replace('HAS_LINE_BRANCH',str(int('if(arg && arg->ResourceType==D3D12DDI_RT_TEXTURE1D)' in source))).replace('/* PINNED_WDK */','\n'.join(decls)).replace('/* ACTUAL_FUNCTIONS */',functions)
