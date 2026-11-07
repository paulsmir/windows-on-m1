static pipe_sampler_view captured;static pipe_image_view image;static pipe_resource*bound_buffer,*bound_image;static unsigned sampler_calls;
static pipe_sampler_view*capture(pipe_context*,pipe_resource*,const pipe_sampler_view*v){sampler_calls++;captured=*v;return &captured;}
static void cb(pipe_context*,mesa_shader_stage,unsigned,const pipe_constant_buffer*){}
static void sv(pipe_context*,mesa_shader_stage,unsigned,unsigned,unsigned,pipe_sampler_view**){}
static void sb(pipe_context*,mesa_shader_stage,unsigned,unsigned,const pipe_shader_buffer*b,unsigned){bound_buffer=b?b[0].buffer:nullptr;}
static void si(pipe_context*,mesa_shader_stage,unsigned,unsigned,unsigned,const pipe_image_view*i){bound_image=i?i[0].resource:nullptr;if(i)image=i[0];}
int main(){
 pipe_resource native={};native.target=PIPE_TEXTURE_1D_ARRAY;native.width0=37;native.height0=native.depth0=1;native.array_size=3;native.last_level=1;
 AgxD3d12Heap heap={&native,0,65536};AgxD3d12Resource resource={&heap,0,65536,D3D12DDI_RT_TEXTURE1D,DXGI_FORMAT_R32_UINT,37,1,3,2};
 AgxD3d12Descriptor desc={};D3D12DDI_CPU_DESCRIPTOR_HANDLE at={reinterpret_cast<uintptr_t>(&desc)};
 D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 srv={};srv.hDrvResource.pDrvPrivate=&resource;srv.Format=resource.format;srv.ResourceDimension=D3D12DDI_RD_TEXTURE1D;
 srv.Shader4ComponentMapping=AGX_D3D12_COMPONENT_MAPPING_IDENTITY;srv.Tex1D={1,1,1,2,0};
 CreateShaderResourceView(nullptr,&srv,at);assert(!last_reason&&desc.kind==kDescriptorSrvTexture&&desc.first_layer==1&&desc.layers==2&&desc.level==1&&desc.levels==1);
 for(unsigned bad=0;bad<9;bad++){auto s=srv;switch(bad){case 0:s.Tex1D.FirstArraySlice=3;break;case 1:s.Tex1D.ArraySize=3;break;case 2:s.Tex1D.ArraySize=0;break;case 3:s.Tex1D.ArraySize=~0u;break;case 4:s.Tex1D.MostDetailedMip=2;break;case 5:s.Tex1D.MipLevels=2;break;case 6:s.Tex1D.ResourceMinLODClamp=1;break;case 7:s.Tex1D.ResourceMinLODClamp=std::numeric_limits<float>::quiet_NaN();break;case 8:s.Tex1D.FirstArraySlice=~0u;break;}
  CreateShaderResourceView(nullptr,&s,at);assert(last_reason&&!desc.kind);}
 D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002 uv={};uv.hDrvResource.pDrvPrivate=&resource;uv.Format=resource.format;uv.ResourceDimension=D3D12DDI_RD_TEXTURE1D;uv.Tex1D={1,1,1};
 CreateUnorderedAccessView(nullptr,&uv,at);assert(!last_reason&&desc.kind==kDescriptorUavTexture&&desc.first_layer==1&&desc.layers==1&&desc.level==1);
 for(unsigned bad=0;bad<6;bad++){auto u=uv;switch(bad){case 0:u.Tex1D.FirstArraySlice=3;break;case 1:u.Tex1D.ArraySize=3;break;case 2:u.Tex1D.ArraySize=0;break;case 3:u.Tex1D.ArraySize=~0u;break;case 4:u.Tex1D.MipSlice=2;break;case 5:u.Tex1D.FirstArraySlice=~0u;break;}
  CreateUnorderedAccessView(nullptr,&u,at);assert(last_reason&&!desc.kind);}
 pipe_context pipe={};pipe.create_sampler_view=capture;pipe.set_constant_buffer=cb;pipe.set_sampler_views=sv;pipe.set_shader_buffers=sb;pipe.set_shader_images=si;
 AGX_D3D12_BUFFER buffer={&native,0,65536};AGX_D3D12_CBV cbvs[14]={};AGX_D3D12_UAV uavs[8]={};AGX_D3D12_SRV srvs[128]={};
 srvs[0].Buffer=&buffer;srvs[0].Texture=1;srvs[0].Format=resource.format;srvs[0].FirstLevel=1;srvs[0].Levels=1;srvs[0].FirstLayer=1;srvs[0].Layers=1;srvs[0].ComponentMapping=AGX_D3D12_COMPONENT_MAPPING_IDENTITY;
 uint8_t dims[128]={},uav_dims[8]={};pipe_sampler_view*views[128]={};unsigned count;
 for(auto stage:{MESA_SHADER_COMPUTE,MESA_SHADER_VERTEX,MESA_SHADER_FRAGMENT}){
  for(unsigned countLayers:{1u,2u}){srvs[0].Layers=countLayers;dims[0]=DXBC_SRV_1D_ARRAY;
   assert(bind_stage(&pipe,stage,cbvs,srvs,uavs,views,&count,dims,uav_dims)==S_OK&&captured.target==PIPE_TEXTURE_1D_ARRAY&&captured.u.tex.first_layer==1&&captured.u.tex.last_layer==countLayers);
   dims[0]=DXBC_SRV_1D;assert(bind_stage(&pipe,stage,cbvs,srvs,uavs,views,&count,dims,uav_dims)==(countLayers==1?S_OK:E_INVALIDARG));if(countLayers==1)assert(captured.target==PIPE_TEXTURE_1D);
  }
 }
 srvs[0].Layers=1;dims[0]=DXBC_SRV_OTHER;assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dims,uav_dims)==E_INVALIDARG);
 dims[0]=DXBC_SRV_1D_ARRAY;
 for(auto target:{PIPE_BUFFER,PIPE_TEXTURE_2D,PIPE_TEXTURE_3D}){native.target=target;
  assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dims,uav_dims)==E_INVALIDARG);}
 native.target=PIPE_TEXTURE_1D_ARRAY;dims[0]=DXBC_SRV_UNUSED;unsigned before=sampler_calls;
 assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dims,uav_dims)==S_OK&&sampler_calls==before);
 srvs[0]={};dims[0]=DXBC_SRV_UNUSED;AGX_D3D12_UAV u={};u.Buffer=&buffer;u.Texture=1;u.Format=resource.format;u.Level=1;u.FirstLayer=1;u.Layers=1;uavs[0]=u;
 for(auto stage:{MESA_SHADER_COMPUTE,MESA_SHADER_VERTEX,MESA_SHADER_FRAGMENT}){
  for(unsigned countLayers:{1u,2u}){uavs[0].Layers=countLayers;uav_dims[0]=DXBC_UAV_1D_ARRAY;
   assert(bind_stage(&pipe,stage,cbvs,srvs,uavs,views,&count,dims,uav_dims)==S_OK&&bound_image==&native&&!image.u.tex.single_layer_view&&image.u.tex.first_layer==1&&image.u.tex.last_layer==countLayers);
   uav_dims[0]=DXBC_UAV_1D;assert(bind_stage(&pipe,stage,cbvs,srvs,uavs,views,&count,dims,uav_dims)==(countLayers==1?S_OK:E_INVALIDARG));if(countLayers==1)assert(image.u.tex.single_layer_view);
  }
 }
 uavs[0]=u;uavs[0].Texture=0;uavs[0].Format=0;uav_dims[0]=DXBC_UAV_UNUSED;bound_buffer=bound_image=&native;
 assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dims,uav_dims)==S_OK&&!bound_buffer&&!bound_image);
 uavs[0]=u;uav_dims[0]=DXBC_UAV_OTHER;assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dims,uav_dims)==E_INVALIDARG);
 for(auto target:{PIPE_BUFFER,PIPE_TEXTURE_2D,PIPE_TEXTURE_3D}){native.target=target;uav_dims[0]=DXBC_UAV_1D_ARRAY;
  assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dims,uav_dims)==E_INVALIDARG);}
 /* Count1 backing still has explicit ARRAY declaration; no count heuristic. */
 native.target=PIPE_TEXTURE_1D_ARRAY;native.array_size=1;uavs[0]=u;uavs[0].FirstLayer=0;uav_dims[0]=DXBC_UAV_1D_ARRAY;
 assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dims,uav_dims)==S_OK&&!image.u.tex.single_layer_view);
 puts("PASS C24 actualDDI views15refusals and ownerCS/VS/PS count1/2 shader-dimension/unused/crossdimension controls");
}
