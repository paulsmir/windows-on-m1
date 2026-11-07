static pipe_sampler_view captured;static unsigned images_calls;static pipe_resource *bound_buffer,*bound_image;
static pipe_sampler_view *capture(pipe_context*,pipe_resource*,const pipe_sampler_view*v){captured=*v;return &captured;}
static void cb(pipe_context*,mesa_shader_stage,unsigned,const pipe_constant_buffer*){}
static void sv(pipe_context*,mesa_shader_stage,unsigned,unsigned,unsigned,pipe_sampler_view**){}
static void sb(pipe_context*,mesa_shader_stage,unsigned,unsigned,const pipe_shader_buffer*b,unsigned){bound_buffer=b?b[0].buffer:nullptr;}
static void si(pipe_context*,mesa_shader_stage,unsigned,unsigned,unsigned,const pipe_image_view*i){++images_calls;bound_image=i?i[0].resource:nullptr;}
int main(){
 pipe_resource native={};native.target=PIPE_TEXTURE_1D;native.width0=37;native.height0=native.depth0=native.array_size=1;native.last_level=1;
 AgxD3d12Heap heap={&native,0x10000,65536};AgxD3d12Resource resource={&heap,0x10000,65536,D3D12DDI_RT_TEXTURE1D,DXGI_FORMAT_R32_UINT,37,1,1,2};
 AgxD3d12Descriptor d={};D3D12DDI_CPU_DESCRIPTOR_HANDLE at={reinterpret_cast<uintptr_t>(&d)};
 D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 srv={};srv.hDrvResource.pDrvPrivate=&resource;srv.Format=resource.format;srv.ResourceDimension=D3D12DDI_RD_TEXTURE1D;srv.Shader4ComponentMapping=AGX_D3D12_COMPONENT_MAPPING_IDENTITY;srv.Tex1D={1,0,1,1,0};
 CreateShaderResourceView(nullptr,&srv,at);assert(!last_reason&&d.kind==kDescriptorSrvTexture&&d.level==1&&d.levels==1&&d.layers==1);
 for(unsigned bad=0;bad<7;bad++){auto v=srv;switch(bad){case 0:v.Tex1D.FirstArraySlice=1;break;case 1:v.Tex1D.ArraySize=2;break;case 2:v.Tex1D.MostDetailedMip=2;break;case 3:v.Tex1D.MipLevels=2;break;case 4:v.Tex1D.ResourceMinLODClamp=1;break;case 5:v.Format=99;break;case 6:v.ResourceDimension=D3D12DDI_RD_TEXTURE2D;break;}CreateShaderResourceView(nullptr,&v,at);assert(last_reason&&!d.kind);}
 D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002 uv={};uv.hDrvResource.pDrvPrivate=&resource;uv.Format=resource.format;uv.ResourceDimension=D3D12DDI_RD_TEXTURE1D;uv.Tex1D={1,0,1};
 CreateUnorderedAccessView(nullptr,&uv,at);assert(!last_reason&&d.kind==kDescriptorUavTexture&&d.level==1&&d.layers==1);
 for(unsigned bad=0;bad<5;bad++){auto v=uv;switch(bad){case 0:v.Tex1D.FirstArraySlice=1;break;case 1:v.Tex1D.ArraySize=2;break;case 2:v.Tex1D.MipSlice=2;break;case 3:v.Format=99;break;case 4:v.ResourceDimension=D3D12DDI_RD_TEXTURE2D;break;}CreateUnorderedAccessView(nullptr,&v,at);assert(last_reason&&!d.kind);}
 pipe_context pipe={};pipe.create_sampler_view=capture;pipe.set_constant_buffer=cb;pipe.set_sampler_views=sv;pipe.set_shader_buffers=sb;pipe.set_shader_images=si;
 AGX_D3D12_BUFFER buffer={&native,0,65536};AGX_D3D12_CBV cbvs[14]={};AGX_D3D12_UAV uavs[8]={};AGX_D3D12_SRV srvs[128]={};
 srvs[0].Buffer=&buffer;srvs[0].Texture=1;srvs[0].Format=resource.format;srvs[0].FirstLevel=1;srvs[0].Levels=1;srvs[0].Layers=1;srvs[0].ComponentMapping=AGX_D3D12_COMPONENT_MAPPING_IDENTITY;
 uint8_t dims[128]={};dims[0]=DXBC_SRV_1D;uint8_t uav_dims[8]={};pipe_sampler_view *views[128]={};unsigned count;
 assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dims,uav_dims)==S_OK&&captured.target==PIPE_TEXTURE_1D&&captured.u.tex.first_level==1);
 dims[0]=DXBC_SRV_OTHER;assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dims,uav_dims)==E_INVALIDARG);
 dims[0]=DXBC_SRV_1D;native.target=PIPE_TEXTURE_2D;assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dims,uav_dims)==E_INVALIDARG);native.target=PIPE_TEXTURE_1D;
 AGX_D3D12_UAV u={};u.Buffer=&buffer;u.Texture=1;u.Format=resource.format;u.Level=u.Layers=1;pipe_image_view image={};assert(uav_image(&u,&image)&&image.u.tex.level==1&&image.u.tex.first_layer==0&&image.u.tex.last_layer==0);
 srvs[0]={};dims[0]=DXBC_SRV_UNUSED;uavs[0]=u;uavs[0].Texture=0;uavs[0].Format=0;uav_dims[0]=DXBC_UAV_UNUSED;
 bound_buffer=bound_image=&native;assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dims,uav_dims)==S_OK&&!bound_buffer&&!bound_image);
 uav_dims[0]=DXBC_UAV_OTHER;assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dims,uav_dims)==E_INVALIDARG);
 uavs[0]=u;uav_dims[0]=DXBC_UAV_1D;assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dims,uav_dims)==S_OK&&bound_image==&native&&!bound_buffer);
 puts("PASS C23 actualDDI1D SRV/UAV selectedmip/12refusals/SRVcrossdimension/ownerimageview/unused1Dnullbindings");
}
