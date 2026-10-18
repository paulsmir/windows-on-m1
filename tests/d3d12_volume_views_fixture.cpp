static pipe_sampler_view captured;
static pipe_framebuffer_state framebuffer;
static unsigned clear_calls;
static pipe_sampler_view *capture(pipe_context *,pipe_resource *,const pipe_sampler_view *v){captured=*v;return &captured;}
static void cb(pipe_context *,mesa_shader_stage,unsigned,const pipe_constant_buffer *){}
static void sv(pipe_context *,mesa_shader_stage,unsigned,unsigned,unsigned,pipe_sampler_view **){}
static void sb(pipe_context *,mesa_shader_stage,unsigned,unsigned,const pipe_shader_buffer *,unsigned){}
static void si(pipe_context *,mesa_shader_stage,unsigned,unsigned,unsigned,const pipe_image_view *){}
static void fb(pipe_context *,const pipe_framebuffer_state *s){framebuffer=*s;}
static void clear(pipe_context *,unsigned,unsigned,uint8_t,const pipe_scissor_state *,const pipe_color_union *,double,unsigned){++clear_calls;}
int main() {
 pipe_resource native={};native.target=PIPE_TEXTURE_3D;native.width0=native.height0=4;native.depth0=8;native.array_size=1;native.last_level=1;
 AgxD3d12Heap heap={&native,0x10000,65536};AgxD3d12Resource resource={&heap,0x10000,65536,D3D12DDI_RT_TEXTURE3D,DXGI_FORMAT_R32_UINT,4,4,8,2};
 AgxD3d12Descriptor view={};D3D12DDI_CPU_DESCRIPTOR_HANDLE output={reinterpret_cast<uintptr_t>(&view)};
 D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 srv={};srv.hDrvResource.pDrvPrivate=&resource;srv.Format=resource.format;srv.ResourceDimension=D3D12DDI_RD_TEXTURE3D;
 srv.Shader4ComponentMapping=AGX_D3D12_COMPONENT_MAPPING_IDENTITY;srv.Tex3D={1,1,0};
 CreateShaderResourceView(nullptr,&srv,output);std::printf("volume SRV reason=%u kind=%u\n",last_reason,view.kind);std::fflush(stdout);
 assert(!last_reason && view.kind==kDescriptorSrvTexture && view.level==1 && view.levels==1 && view.layers==1 && view.first_layer==0);
 srv.Tex3D.MipLevels=2;CreateShaderResourceView(nullptr,&srv,output);assert(last_reason && !view.kind);
 srv.Tex3D.MipLevels=UINT_MAX;CreateShaderResourceView(nullptr,&srv,output);assert(!last_reason && view.levels==1);
 srv.Tex3D.ResourceMinLODClamp=1;CreateShaderResourceView(nullptr,&srv,output);assert(last_reason && !view.kind);
 D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002 uav={};uav.hDrvResource.pDrvPrivate=&resource;uav.Format=resource.format;uav.ResourceDimension=D3D12DDI_RD_TEXTURE3D;uav.Tex3D={1,0,UINT_MAX};
 CreateUnorderedAccessView(nullptr,&uav,output);assert(!last_reason && view.kind==kDescriptorUavTexture && view.level==1 &&view.layers==4);
 uav.Tex3D.WSize=4;CreateUnorderedAccessView(nullptr,&uav,output);assert(!last_reason && view.layers==4);
 uav.Tex3D.FirstW=1;uav.Tex3D.WSize=3;CreateUnorderedAccessView(nullptr,&uav,output);assert(last_reason && !view.kind);
 D3D12DDIARG_CREATE_RENDER_TARGET_VIEW_0002 rtv={};rtv.hDrvResource.pDrvPrivate=&resource;rtv.Format=resource.format;rtv.ResourceDimension=D3D12DDI_RD_TEXTURE3D;rtv.Tex3D={1,3,1};
 CreateRenderTargetView(nullptr,&rtv,output);assert(!last_reason && view.kind==kDescriptorRtv && view.level==1 &&view.first_layer==3 &&view.layers==1);
 rtv.Tex3D.FirstW=4;CreateRenderTargetView(nullptr,&rtv,output);assert(last_reason && !view.kind);
 rtv.Tex3D.FirstW=0;rtv.Tex3D.WSize=2;CreateRenderTargetView(nullptr,&rtv,output);assert(last_reason && !view.kind);
 rtv.Tex3D.FirstW=3;rtv.Tex3D.WSize=UINT_MAX;CreateRenderTargetView(nullptr,&rtv,output);assert(!last_reason && view.layers==1);
 pipe_context pipe={};pipe.create_sampler_view=capture;pipe.set_constant_buffer=cb;pipe.set_sampler_views=sv;
 pipe.set_shader_buffers=sb;pipe.set_shader_images=si;pipe.set_framebuffer_state=fb;pipe.clear=clear;
 AGX_D3D12_BUFFER buffer={&native,0,65536};AGX_D3D12_CBV cbvs[14]={};AGX_D3D12_UAV uavs[8]={};AGX_D3D12_SRV srvs[128]={};
 srvs[0].Buffer=&buffer;srvs[0].Texture=1;srvs[0].Format=resource.format;srvs[0].FirstLevel=1;srvs[0].Levels=1;srvs[0].Layers=1;srvs[0].ComponentMapping=AGX_D3D12_COMPONENT_MAPPING_IDENTITY;
 uint8_t dimensions[128]={};dimensions[0]=DXBC_SRV_3D;pipe_sampler_view *views[128]={};unsigned count=0;
 assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dimensions)==S_OK);
 assert(captured.target==PIPE_TEXTURE_3D &&captured.u.tex.first_level==1 &&captured.u.tex.last_level==1 &&count==1);
 dimensions[0]=DXBC_SRV_OTHER;assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&count,dimensions)==E_INVALIDARG);
 AGX_D3D12_UAV uv={};uv.Buffer=&buffer;uv.Texture=1;uv.Format=resource.format;uv.Level=1;uv.Layers=4;pipe_image_view image={};
 assert(uav_image(&uv,&image) &&image.resource==&native &&image.u.tex.last_layer==3);
 uv.FirstLayer=1;uv.Layers=3;assert(!uav_image(&uv,&image));
 AGX_D3D12_OWNER owner={&pipe,0};float color[4]={1,0,0,1};
 assert(AgxD3d12OwnerClearRenderTarget(&owner,&buffer,resource.format,1,3,1,color,nullptr,0)==S_OK);
 assert(clear_calls==1 &&framebuffer.layers==1 &&framebuffer.width==2 &&framebuffer.height==2 &&framebuffer.cbufs[0].level==1 &&framebuffer.cbufs[0].first_layer==3 &&framebuffer.cbufs[0].last_layer==3);
 assert(AgxD3d12OwnerClearRenderTarget(&owner,&buffer,resource.format,1,4,1,color,nullptr,0)==E_INVALIDARG &&clear_calls==1);
 assert(AgxD3d12OwnerClearRenderTarget(&owner,&buffer,resource.format,1,1,2,color,nullptr,0)==E_INVALIDARG &&clear_calls==1);
 puts("PASS volume SRV/UAV/RTV ranges");
}
