int main(){
 test_owner.Screen=c23_native_screen();AgxD3d12Device device;
 for(UINT width: {1u,37u,16384u})for(UINT format: {42u,28u})for(UINT requested: {1u,2u}){
  if(width==1&&requested==2)continue;
  D3D12DDIARG_CREATERESOURCE_0003 arg={};arg.ResourceType=D3D12DDI_RT_TEXTURE1D;arg.Width=width;arg.Height=arg.DepthOrArraySize=1;arg.MipLevels=requested;arg.SampleDesc.Count=1;arg.Format=format;arg.Flags=16;
  D3D12DDI_RESOURCE_ALLOCATION_INFO a={};unsigned before=ensure_calls;
  CheckResourceAllocationInfo({},&arg,static_cast<D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS>(0),65536,1,&a);assert(a.ResourceDataSize!=~0ull&&a.Layout==0x105&&before==ensure_calls);
  D3D12DDIARG_CREATEHEAP_0001 h={};h.CPUPageProperty=D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
  AgxD3d12Heap heap={};AgxD3d12Resource resource={};
  assert(CreateLineResource(&device,&h,&heap,&arg,&resource)==E_NOTIMPL&&ensure_calls==before);
  arg.Layout=a.Layout;assert(CreateLineResource(&device,&h,&heap,&arg,&resource)==S_OK&&ensure_calls==before+1);
  assert(resource.type==D3D12DDI_RT_TEXTURE1D&&resource.width==width&&resource.height==1&&resource.layers==1&&resource.levels==requested&&resource.bytes==a.ResourceDataSize&&heap.volume);
  auto*n=static_cast<pipe_resource*>(heap.native);assert(n->target==PIPE_TEXTURE_1D&&n->width0==width&&n->height0==1&&n->array_size==1&&n->last_level==requested-1);
  D3D12DDI_RESOURCE_ALLOCATION_INFO reflected={};D3DKMT_HANDLE allocation=0;CheckExistingResourceAllocationInfo({}, {&resource},&reflected,&allocation);assert(!memcmp(&a,&reflected,sizeof(a))&&allocation==77);
  c23_native_release(n);before=ensure_calls;
  for(UINT bad=0;bad<12;bad++){auto v=arg;auto hh=h;
   switch(bad){case 0:v.Height=2;break;case 1:v.DepthOrArraySize=2;break;case 2:v.Width=16385;break;case 3:v.MipLevels=0;break;case 4:v.MipLevels=16;break;case 5:v.Format=99;break;case 6:v.Flags=1;break;case 7:v.Layout=1;break;case 8:hh.Alignment=4096;break;case 9:hh.CPUPageProperty=D3D12DDI_CPU_PAGE_PROPERTY_WRITE_COMBINE;break;case 10:hh.ByteSize=1;break;case 11:v.SampleDesc.Count=2;break;}
   assert(CreateLineResource(&device,&hh,&heap,&v,&resource)==E_NOTIMPL&&ensure_calls==before);
  }
 }
 puts("PASS C23 actual query/create/existing default nonarray domain and12refusals beforeEnsureOwner");
}
