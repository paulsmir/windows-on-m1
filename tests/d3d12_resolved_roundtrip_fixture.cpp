using BOOL=int;using D3DKMT_HANDLE=unsigned;
#define E_FAIL (-4)
using D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS=unsigned;
static const UINT64 kVaLimit=1ull<<39,kVaMinimum=65536;
/* PINNED_ALLOCATION_ABI */
struct D3D12DDI_HDEVICE {void *pDrvPrivate;};struct D3D12DDI_HRESOURCE {void *pDrvPrivate;};
struct AgxD3d12Device {};
static AGX_D3D12_OWNER test_owner;static unsigned ensure_calls;
static AGX_D3D12_OWNER *EnsureOwner(AgxD3d12Device *){++ensure_calls;return &test_owner;}
static D3DKMT_HANDLE AgxD3d12OwnerBufferAllocation(AGX_D3D12_OWNER *,AGX_D3D12_BUFFER *){return 77;}
static void AdmissionUmdDiagnostic(const char *,HRESULT,const UINT *,UINT){}
/* ACTUAL_RESOURCE_FUNCTIONS */
int main(){
 static_assert(sizeof(D3D12DDI_RESOURCE_ALLOCATION_INFO)==56);
 static_assert(sizeof(D3D12DDI_SUBRESOURCE_INFO)==32);
 pipe_screen screen={};screen.is_format_supported=supported;screen.resource_create_with_modifiers=create;test_owner.Screen=&screen;
 AgxD3d12Device device;
 for(UINT side=1;side<=32;side*=2)for(UINT levels=1;levels<=2;levels++)for(UINT fmt: {28u,42u}) {
  D3D12DDIARG_CREATERESOURCE_0003 r={};r.ResourceType=D3D12DDI_RT_TEXTURE3D;r.SampleDesc.Count=1;
  r.Width=r.Height=side;r.DepthOrArraySize=8;r.MipLevels=levels;r.Format=fmt;
  unsigned before=ensure_calls;D3D12DDI_RESOURCE_ALLOCATION_INFO alloc={};
  CheckResourceAllocationInfo({},&r,0,0,0,&alloc);
  assert(alloc.ResourceDataSize!=~0ull && alloc.Layout>=0x100 && alloc.Layout<=0x105 && ensure_calls==before);
  assert(alloc.MipLevelSwizzleTransition==((levels>1&&side>1)?1:255));
  assert(!alloc.AdditionalDataHeaderAlignment&&!alloc.AdditionalDataAlignment);
  AgxD3d12Heap heap={};AgxD3d12Resource resource={};D3D12DDIARG_CREATEHEAP_0001 h={};h.CPUPageProperty=D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
  assert(CreateVolumeResource(&device,&h,&heap,&r,&resource)==E_NOTIMPL && ensure_calls==before);
  r.Layout=alloc.Layout;
  assert(CreateVolumeResource(&device,&h,&heap,&r,&resource)==S_OK && ensure_calls==before+1);
  D3D12DDI_RESOURCE_ALLOCATION_INFO existing={};D3DKMT_HANDLE handle=0;
  CheckExistingResourceAllocationInfo({}, {&resource},&existing,&handle);
  assert(!memcmp(&alloc,&existing,sizeof(alloc)) && handle==77 && resource.heap==&heap);
  for(UINT mip=0;mip<levels;mip++){
   D3D12DDI_SUBRESOURCE_INFO si={};CheckSubresourceInfo({}, {&resource},mip,&si);
   UINT s=(side>>mip)?side>>mip:1;assert(si.Offset==resource.volume_offsets[mip]&&si.RowStride==4ull*s*s&&si.DepthStride==resource.volume_depth_stride);
   assert(!si.RowBytePreSwizzleOffset&&!si.ColumnPreSwizzleOffset&&!si.DepthPreSwizzleOffset);
  }
  D3D12DDI_SUBRESOURCE_INFO bad;memset(&bad,0xa5,sizeof(bad));CheckSubresourceInfo({}, {&resource},levels,&bad);
  D3D12DDI_SUBRESOURCE_INFO zero={};assert(!memcmp(&bad,&zero,sizeof(bad)));
  before=ensure_calls;
  for(UINT fault=0;fault<9;fault++){
   auto v=r;auto hh=h;
   switch(fault){case 0:v.Layout=D3D12DDI_TL_ROW_MAJOR;break;case 1:v.Layout=static_cast<D3D12DDI_TEXTURE_LAYOUT>(0x106);break;case 2:v.Height=33;break;case 3:v.MipLevels=3;break;case 4:v.Format=99;break;case 5:v.Flags=2;break;case 6:hh.CPUPageProperty=D3D12DDI_CPU_PAGE_PROPERTY_WRITE_COMBINE;break;case 7:hh.ByteSize=1;break;case 8:hh.Alignment=4096;break;}
   assert(CreateVolumeResource(&device,&hh,&heap,&v,&resource)==E_NOTIMPL&&ensure_calls==before);
  }
  for(UINT restriction: {1u,4096u,32768u,65537u}){
   CheckResourceAllocationInfo({},&r,0,restriction,0,&existing);assert(existing.ResourceDataSize==~0ull&&ensure_calls==before);
  }
  CheckResourceAllocationInfo({},&r,0,65536,0,&existing);assert(existing.ResourceDataSize==alloc.ResourceDataSize);
 }
 puts("PASS WDK allocation56/subresource32 exact query-resolvedcreate-existing roundtrip/zeroOwnerQuery/refusals");
}
