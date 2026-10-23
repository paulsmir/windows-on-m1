extern "C" unsigned c21_native_bo_calls(void);
extern "C" unsigned c21_native_device_calls(void);
extern "C" void c24_check_native(pipe_resource*,const AGX_WIN32_ASAHI_RESOURCE_LAYOUT*);
int main(){
 test_owner.Screen=c23_native_screen();AgxD3d12Device device;unsigned cases=0;
 for(UINT width:{1u,37u,16384u})for(UINT format:{42u,28u})for(UINT layers:{1u,3u,2048u})for(UINT levels:{1u,2u,6u,15u}){
  if(levels>1 && (width>>(levels-1))==0)continue;
  D3D12DDIARG_CREATERESOURCE_0003 arg={};arg.ResourceType=D3D12DDI_RT_TEXTURE1D;arg.Width=width;arg.Height=1;arg.DepthOrArraySize=layers;
  arg.MipLevels=levels;arg.SampleDesc.Count=1;arg.Format=format;arg.Flags=16;
  D3D12DDI_RESOURCE_ALLOCATION_INFO a={};unsigned before=ensure_calls,bo=c21_native_bo_calls(),dev=c21_native_device_calls();
  CheckResourceAllocationInfo({},&arg,static_cast<D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS>(0),65536,1,&a);
  assert(a.ResourceDataSize!=~0ull&&a.Layout==0x117&&a.ResourceDataAlignment==65536&&before==ensure_calls);
  assert(c21_native_bo_calls()==bo&&c21_native_device_calls()==dev);
  D3D12DDIARG_CREATEHEAP_0001 h={};h.CPUPageProperty=D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
  AgxD3d12Heap heap={};AgxD3d12Resource resource={};
  assert(CreateLineResource(&device,&h,&heap,&arg,&resource)==E_NOTIMPL&&ensure_calls==before);
  arg.Layout=a.Layout;assert(CreateLineResource(&device,&h,&heap,&arg,&resource)==S_OK&&ensure_calls==before+1);
  assert(resource.type==D3D12DDI_RT_TEXTURE1D&&resource.layers==layers&&resource.levels==levels&&resource.bytes==a.ResourceDataSize&&resource.gpuva==0);
  auto*n=static_cast<pipe_resource*>(heap.native);assert(n->target==PIPE_TEXTURE_1D_ARRAY&&n->width0==width&&n->array_size==layers&&n->last_level==levels-1);
  AGX_WIN32_ASAHI_RESOURCE_LAYOUT raw={};bo=c21_native_bo_calls();dev=c21_native_device_calls();assert(AgxWin32AsahiQueryUncompressedResourceLayout(n,&raw));
  assert(c21_native_bo_calls()==bo&&c21_native_device_calls()==dev);c24_check_native(n,&raw);
  if(width==37&&layers==3&&levels==2)assert(raw.LayerStrideBytes==16384&&raw.SizeBytes==49152&&a.ResourceDataSize==65536);
  D3D12DDI_RESOURCE_ALLOCATION_INFO reflected={};D3DKMT_HANDLE allocation=0;CheckExistingResourceAllocationInfo({}, {&resource},&reflected,&allocation);assert(!memcmp(&a,&reflected,sizeof(a))&&allocation==77);
  for(UINT sub=0;sub<levels*layers;sub++){
   D3D12DDI_SUBRESOURCE_INFO out={};CheckSubresourceInfo({}, {&resource},sub,&out);UINT mip=sub%levels,layer=sub/levels;
   assert(out.Offset==raw.LayerStrideBytes*layer+raw.LevelOffsets[mip]&&out.RowStride==4ull*(width>>mip)&&out.DepthStride==raw.LayerStrideBytes);
  }
  D3D12DDI_SUBRESOURCE_INFO out;memset(&out,0xcc,sizeof(out));CheckSubresourceInfo({}, {&resource},levels*layers,&out);
  assert(!out.Offset&&!out.RowStride&&!out.DepthStride);
  c23_native_release(n);before=ensure_calls;
  for(UINT bad=0;bad<13;bad++){auto v=arg;auto hh=h;
   switch(bad){case 0:v.Height=2;break;case 1:v.DepthOrArraySize=2049;break;case 2:v.Width=16385;break;case 3:v.MipLevels=0;break;case 4:v.MipLevels=16;break;case 5:v.Format=99;break;case 6:v.Flags=1;break;case 7:v.Layout=1;break;case 8:hh.Alignment=4096;break;case 9:hh.CPUPageProperty=D3D12DDI_CPU_PAGE_PROPERTY_WRITE_COMBINE;break;case 10:hh.ByteSize=1;break;case 11:v.SampleDesc.Count=2;break;case 12:v.DepthOrArraySize=0;break;}
   assert(CreateLineResource(&device,&hh,&heap,&v,&resource)==E_NOTIMPL&&ensure_calls==before);
  }cases++;
 }
 puts("PASS C24 actual query/create/reflection/subresource bounds + same initializer/AIL/native layout +query0BO/device; canonical ARRAY1 and ARRAY2048");printf("cases=%u\n",cases);
}
