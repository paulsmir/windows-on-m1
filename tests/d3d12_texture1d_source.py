"""Original actual-function renderers for the bounded Texture1D regressions."""
from pathlib import Path
import re
from test_dxbc_to_nir import ROOT
from test_d3d12_view_mapping import block
from test_d3d12_volume_native_owner import actual_native_source

def sources():
    return ((ROOT/'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text(),
            (ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text(),
            (ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h').read_text())

def declaration(name):
    h=(ROOT/'.local/reference/wdk26100/d3d12umddi.h').read_text()
    m=re.search(r'typedef (?:struct|enum) '+name+r'\s*\{.*?\}\s*'+name+r';',h,re.S)
    if not m:raise AssertionError('pinned WDK declaration absent '+name)
    return m[0]

def native_source():
    return actual_native_source()+'\n'+(ROOT/'tests/d3d12_texture1d_native_fixture.c').read_text()

def query_base():
    ddi,owner,header=sources()
    s=(ROOT/'tests/d3d12_volume_query_fixture.cpp').read_text().split('static UINT query_calls=')[0]
    s=s.replace('#include "asahi/layout/layout.h"\n','')
    s=s.replace('#define E_INVALIDARG (-1)','#define E_INVALIDARG static_cast<HRESULT>(0x80070057u)')
    s=s.replace('#define E_NOTIMPL (-2)','#define E_NOTIMPL static_cast<HRESULT>(0x80004001u)')
    s=s.replace('#define D3D12DDI_TL_UNDEFINED 0','#define D3D12DDI_TL_UNDEFINED 0\n#define D3D12DDI_TL_ROW_MAJOR 1')
    end=header.index('} AGX_D3D12_VOLUME_LAYOUT;')+len('} AGX_D3D12_VOLUME_LAYOUT;')
    s=s.replace('/* VOLUME_LAYOUT_RECORD */',header[header.rfind('typedef struct {',0,end):end])
    if 'D3D12DDI_RT_TEXTURE1D' not in s:s+='\nenum{D3D12DDI_RT_TEXTURE1D=2};\n'
    s+='\nusing BOOL=int;using UINT16=unsigned short;using UINT32=unsigned;\n'
    for n in ['D3D12DDICAPS_TYPE','D3D12DDI_SWIZZLE_PATTERN','D3D12DDI_SWIZZLE_BIT_ENTRY','D3D12DDI_SWIZZLE_PATTERN_DESC_0003','D3D12DDI_SWIZZLE_PATTERN_FLAGS','D3D12DDI_SWIZZLE_PATTERN_DESC_0022','D3D12DDI_TEXTURE_LAYOUT_CAPS_0026','D3D12DDI_TEXTURE_LAYOUT_CAPS','D3D12DDI_TEXTURE_LAYOUT_CAPS_0001','D3D12DDIARG_GETCAPS','D3D12DDI_RESOURCE_ALLOCATION_INFO','D3D12DDI_SUBRESOURCE_INFO','D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS']:
        s+=declaration(n)+'\n'
    s+='#include "windows_swizzle_reference.h"\n'
    s+='#include "%s"\n' % (ROOT/'drivers/apple-agx/d3d12/agx_d3d12_volume_registry.h')
    for m in ['static unsigned block_bytes(','extern "C" unsigned AgxD3d12FormatBlockBytes(', 'static unsigned texture_block(', 'extern "C" unsigned AgxD3d12FormatBlock(', 'extern "C" int AgxD3d12FormatMultisample(', 'static bool texture3d_template(', 'static bool texture3d_layout(', 'extern "C" HRESULT AgxD3d12QueryTexture3DLayout(', 'static bool texture1d_template(', 'extern "C" HRESULT AgxD3d12QueryTexture1DLayout(', 'static bool texture1d_array_template(', 'extern "C" HRESULT AgxD3d12QueryTexture1DArrayLayout(']:s+=block(owner,m)+'\n'
    wdk=(ROOT/'.local/reference/wdk26100/d3d12umddi.h').read_text()
    for name in ('D3D12_REQ_SUBRESOURCES','D3D12_REQ_TEXTURE1D_ARRAY_AXIS_DIMENSION','D3D12_REQ_TEXTURE1D_U_DIMENSION'):
        match=re.search(r'const UINT '+name+r'\s*=.*?;',wdk)
        if not match:raise AssertionError('pinned actual WDK constant missing: '+name)
        s+=match[0]+'\n'
    for m in ['static bool VolumeLayoutInfo(','static bool LineLayoutInfo(','static UINT64 TextureBytes(','static HRESULT VolumeLayoutCaps(']:s+=block(ddi,m)+'\n'
    s+='\nstruct D3D12DDI_HDEVICE{void*pDrvPrivate;};struct D3D12DDI_HRESOURCE{void*pDrvPrivate;};using D3DKMT_HANDLE=unsigned;enum{D3D12DDI_RT_BUFFER=1};static constexpr UINT64 kVaMinimum=0x10000ULL,kVaLimit=1ULL<<39;\nstatic void AdmissionUmdDiagnostic(const char*,HRESULT,const UINT*,UINT){}\n'
    s+=block(ddi,'struct AgxD3d12Heap {')+';\n'+block(ddi,'struct AgxD3d12Resource {')+';\n'
    for m in ['static UINT64 VolumeAllocationQuery(','static VOID APIENTRY CheckResourceAllocationInfo(','static VOID APIENTRY CheckSubresourceInfo(','static VOID APIENTRY CheckExistingResourceAllocationInfo(']:s+=block(ddi,m)+'\n'
    return s

def query_source():
    return '#define C23_CANDIDATE 1\n'+query_base()+(ROOT/'tests/d3d12_texture1d_query_fixture.cpp').read_text()

def create_source():
    ddi,owner,_=sources();s=query_base()+'\n#include <initializer_list>\n#define E_FAIL static_cast<HRESULT>(0x80004005u)\n'
    for n in ['D3D12DDI_CPU_PAGE_PROPERTY','D3D12DDI_HEAP_FLAGS','D3D12DDI_MEMORY_POOL','D3D12DDIARG_CREATEHEAP_0001']:s+=declaration(n)+'\n'
    s+='''extern "C" pipe_screen *c23_native_screen(void);
extern "C" void c23_native_release(pipe_resource*);
static UINT64 AgxWin32AsahiBufferGpuAddress(pipe_resource*p){assert(p);return 0x1000000;}
static void AgxWin32AsahiResourceRelease(pipe_resource**p){c23_native_release(*p);*p=nullptr;}
struct AgxD3d12Device{};static AGX_D3D12_OWNER test_owner;static unsigned ensure_calls;
static AGX_D3D12_OWNER *EnsureOwner(AgxD3d12Device*){++ensure_calls;return &test_owner;}
static D3DKMT_HANDLE AgxD3d12OwnerBufferAllocation(AGX_D3D12_OWNER*,AGX_D3D12_BUFFER*){return 77;}
'''
    return s+block(owner,'extern "C" HRESULT AgxD3d12OwnerCreateTexture1DArray(')+'\n'+block(owner,'extern "C" HRESULT AgxD3d12OwnerCreateTexture1D(')+'\n'+block(ddi,'static HRESULT CreateLineResource(')+'\n'+(ROOT/'tests/d3d12_texture1d_create_fixture.cpp').read_text()

def copy_source():
    ddi,_,_=sources();s=(ROOT/'tests/d3d12_volume_record_fixture.cpp').read_text().split('int main()')[0]
    if 'D3D12DDI_RT_TEXTURE1D' not in s:s=s.replace('D3D12DDI_RT_BUFFER=1,','D3D12DDI_RT_TEXTURE1D=2,D3D12DDI_RT_BUFFER=1,')
    s=s.replace('/* ACTUAL_RECORD_TYPES */',block(ddi,'struct AgxD3d12TextureCopy {')+';\n'+block(ddi,'struct AgxD3d12TextureRegionCopy {')+';')
    code=block(ddi,'struct AgxD3d12CopySide {')+';\n'+'\n'.join(block(ddi,m) for m in ['static bool CopySide(','static bool LineSubresource(','static void CopyLineTextureRegion(','static void CopyVolumeTextureRegion(','static VOID APIENTRY CopyTextureRegion('])
    return s.replace('/* ACTUAL_RECORDING */',code)+(ROOT/'tests/d3d12_texture1d_copy_fixture.cpp').read_text()

def view_source():
    ddi,owner,header=sources();s=(ROOT/'tests/d3d12_view_mapping_fixture.cpp').read_text().split('/* PRODUCTION_VIEW_MAPPING */')[0]
    s=s.replace('DXGI_FORMAT_R32_UINT=1,DXGI_FORMAT_R8G8B8A8_UNORM=2','DXGI_FORMAT_R32_UINT=42,DXGI_FORMAT_R8G8B8A8_UNORM=28')
    if 'D3D12DDI_RT_TEXTURE1D' not in s:s=s.replace('D3D12DDI_RT_BUFFER=1,','D3D12DDI_RT_TEXTURE1D=2,D3D12DDI_RD_TEXTURE1D=2,D3D12DDI_RT_BUFFER=1,')
    end=header.index('} AGX_D3D12_SRV;')+len('} AGX_D3D12_SRV;');s=s.replace('/* PRODUCTION_OWNER_SRV */',header[header.rfind('typedef struct {',0,end):end])
    if 'Tex1D;' not in s:s=s.replace(' D3D12DDIARG_TEX2D_SHADER_RESOURCE_VIEW_0002 Tex2D;',' struct{UINT MostDetailedMip,FirstArraySlice,MipLevels,ArraySize;float ResourceMinLODClamp;}Tex1D;\n D3D12DDIARG_TEX2D_SHADER_RESOURCE_VIEW_0002 Tex2D;')
    s=s.replace('static bool uav_image(const AGX_D3D12_UAV *,pipe_image_view *) {return false;}','')
    s+='''
struct D3D12DDIARG_TEX2D_UNORDERED_ACCESS_VIEW_0002{UINT MipSlice,FirstArraySlice,ArraySize,PlaneSlice;};
enum{kDescriptorUavTexture=8,kDescriptorUavTypedBuffer=9,kDescriptorUavBuffer=2};
struct D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002{
 struct{void*pDrvPrivate;}hDrvResource;DXGI_FORMAT Format;UINT ResourceDimension;
 struct{UINT MipSlice,FirstArraySlice,ArraySize;}Tex1D;
 D3D12DDIARG_TEX2D_UNORDERED_ACCESS_VIEW_0002 Tex2D;
 struct{UINT MipSlice,FirstW,WSize;}Tex3D;
 struct{UINT64 FirstElement;UINT NumElements,StructureByteStride,Flags;UINT64 CounterOffsetInBytes;struct{void*pDrvPrivate;}hDrvCounterResource;}Buffer;
};
#define D3D12DDI_BUFFER_UAV_FLAG_RAW 1
'''
    s+=block(ddi,'struct AgxD3d12Descriptor {')+';\n'+block(ddi,'static void SetUavCounter(')+'\n'+block(ddi,'static bool VolumeViewRange(')+'\n'
    for m in ['static VOID APIENTRY CreateShaderResourceView(','static VOID APIENTRY CreateUnorderedAccessView(']:s+=block(ddi,m)+'\n'
    for m in ['static unsigned level_extent(unsigned extent, unsigned level) {','static unsigned texture_layer_extent(const struct pipe_resource *resource,unsigned level) {','static bool uav_dimension_matches(','static bool uav_image(','static bool srv_component_mapping(','static int srv_texture_template(','static HRESULT bind_stage(']:s+=block(owner,m)+'\n'
    return s+(ROOT/'tests/d3d12_texture1d_view_fixture.cpp').read_text()
