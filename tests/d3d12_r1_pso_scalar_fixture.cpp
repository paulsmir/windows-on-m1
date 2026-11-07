#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
struct Handle{void*pDrvPrivate;};
struct Sample{std::uint32_t Count,Quality;};
struct Reference{Handle hLibrary;std::uint32_t PipelineIndex;};
struct D3D12DDIARG_CREATE_PIPELINE_STATE_0010{
 Handle hComputeShader,hVertexShader,hPixelShader,hDomainShader,hHullShader,hGeometryShader;
 Handle hRootSignature,hBlendState;std::uint32_t SampleMask;
 Handle hRasterizerState,hDepthStencilState,hElementLayout;
 std::uint32_t IBStripCutValue,PrimitiveTopologyType,NumRenderTargets,RTVFormats[8],DSVFormat;
 Sample SampleDesc;std::uint32_t NodeMask;Reference LibraryReference;
};
#include "agx_d3d12_r1_pso_scalar_canonical.h"
static void fill(D3D12DDIARG_CREATE_PIPELINE_STATE_0010 &x,std::uintptr_t base,unsigned char pad){
 std::memset(&x,pad,sizeof x);
 x.hComputeShader={};x.hVertexShader={reinterpret_cast<void*>(base+1)};
 x.hPixelShader={reinterpret_cast<void*>(base+2)};x.hDomainShader={};x.hHullShader={};x.hGeometryShader={};
 x.hRootSignature={reinterpret_cast<void*>(base+3)};x.hBlendState={reinterpret_cast<void*>(base+4)};
 x.hRasterizerState={reinterpret_cast<void*>(base+5)};x.hDepthStencilState={reinterpret_cast<void*>(base+6)};
 x.hElementLayout={reinterpret_cast<void*>(base+7)};
 x.SampleMask=0xabcdef01;x.IBStripCutValue=2;x.PrimitiveTopologyType=3;x.NumRenderTargets=2;
 x.RTVFormats[0]=28;x.RTVFormats[1]=87;for(int i=2;i<8;++i)x.RTVFormats[i]=std::uint32_t(base+i);
 x.DSVFormat=45;x.SampleDesc={1,0};x.NodeMask=1;
 x.LibraryReference.hLibrary={};x.LibraryReference.PipelineIndex=std::uint32_t(base);
}
static unsigned checks=0;
static void check(bool b,const char*n){++checks;if(!b){std::fprintf(stderr,"PSO_SCALAR_FAIL %s\n",n);std::exit(1);}}
int main(){
 D3D12DDIARG_CREATE_PIPELINE_STATE_0010 a,b;fill(a,0x1000,0);fill(b,0x2000,0xa5);
 unsigned char x[96],y[96];std::memset(x,0x5a,sizeof x);std::memset(y,0xc3,sizeof y);
 std::size_t nx=0,ny=0;
 check(AgxD3D12CanonicalPsoScalars0010(&a,x,sizeof x,&nx)&&nx==80,"encode first");
 check(AgxD3D12CanonicalPsoScalars0010(&b,y,sizeof y,&ny)&&ny==80,"encode second");
 check(std::memcmp(x,y,80)==0,"pointer and padding independence");
 check(x[80]==0x5a&&y[80]==0xc3,"canary");
 b.SampleMask^=1;check(AgxD3D12CanonicalPsoScalars0010(&b,y,sizeof y,&ny)&&std::memcmp(x,y,80)!=0,"sample mask matters");
 fill(b,0x2000,0xa5);b.NodeMask=2;check(AgxD3D12CanonicalPsoScalars0010(&b,y,sizeof y,&ny)&&std::memcmp(x,y,80)!=0,"node mask matters");
 fill(b,0x2000,0xa5);b.RTVFormats[1]=88;check(AgxD3D12CanonicalPsoScalars0010(&b,y,sizeof y,&ny)&&std::memcmp(x,y,80)!=0,"active RT matters");
 fill(b,0x2000,0xa5);b.NumRenderTargets=9;ny=777;check(!AgxD3D12CanonicalPsoScalars0010(&b,y,sizeof y,&ny)&&ny==0,"RT count bounds");
 fill(b,0x2000,0xa5);b.LibraryReference.hLibrary={reinterpret_cast<void*>(0x1234)};ny=777;
 check(!AgxD3D12CanonicalPsoScalars0010(&b,y,sizeof y,&ny)&&ny==0,"nonzero library remains gated");
 fill(b,0x2000,0xa5);ny=777;check(!AgxD3D12CanonicalPsoScalars0010(&b,y,79,&ny)&&ny==0,"short output");
 ny=777;check(!AgxD3D12CanonicalPsoScalars0010(&b,reinterpret_cast<unsigned char*>(&b),sizeof b,&ny)&&ny==0,"overlap refused");
 std::printf("PSO_SCALAR_PASS checks=%u\n",checks);
}
