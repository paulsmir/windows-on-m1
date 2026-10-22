#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
using VOID=void;using UINT=unsigned;using UINT8=unsigned char;using UINT16=unsigned short;using UINT32=unsigned;using UINT64=unsigned long long;using BOOL=int;using HRESULT=int;
#define APIENTRY
#define APPLE_AGX_D3D12_NATIVE_OWNER 1
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_NOTIMPL (-2)
#define FALSE 0
#define TRUE 1
struct D3D12DDI_HADAPTER {void *pDrvPrivate;};
/* PINNED_WDK */
// Unrelated capability declarations are compile-only stubs; tested layout ABI
// declarations above are extracted unmodified from the pinned WDK.
using D3D12DDI_3DPIPELINELEVEL=int;
static const int kProfileLevel=1,D3D12DDI_RESOURCE_BINDING_TIER_1=1,D3D12DDI_RESOURCE_BINDING_TIER_2=2,D3D12DDI_RESOURCE_HEAP_TIER_1=1,
 D3D12DDI_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED=0,D3D12DDI_TILED_RESOURCES_TIER_NOT_SUPPORTED=0,
 D3D12DDI_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED=0,D3D12DDI_SHADER_MIN_PRECISION_NONE=0;
struct D3D12DDI_3DPIPELINESUPPORT1_DATA_0081 {int HighestRuntimeSupportedFeatureLevel,MaximumDriverSupportedFeatureLevel;};
struct D3D12DDI_D3D12_OPTIONS_DATA_0003 {int ResourceBindingTier,ConservativeRasterizationTier,TiledResourcesTier,CrossNodeSharingTier,VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation,OutputMergerLogicOp,ResourceHeapTier;};
struct D3D12DDI_MEMORY_ARCHITECTURE_CAPS {int UMA,IOCoherent,CacheCoherent;};
struct D3D12DDI_SHADER_CAPS_0003 {int MinPrecision,Doubles,ShaderSpecifiedStencilRef,TypedUAVLoadAdditionalFormats,ROVs;};
struct D3D12DDI_SHADER_CAPS_0012 {int MinPrecision,Doubles,ShaderSpecifiedStencilRef,TypedUAVLoadAdditionalFormats,ROVs,WaveOps;UINT WaveLaneCountMin,WaveLaneCountMax,TotalLaneCount;};
struct AGX_WIN32_DEVICE_INFO {UINT CoresPerCluster,CoreMask;};
static struct {AGX_WIN32_DEVICE_INFO info;int info_valid;} adapter_object;
struct D3D12DDI_ARCHITECTURE_INFO_DATA {int TileBasedDeferredRenderer;};
struct D3D12DDI_GPUVA_CAPS_0004 {int MaxGPUVirtualAddressBitsPerResource;};
struct D3D12DDICAPS_HARDWARE_SCHEDULING_CAPS_0050 {UINT ComputeQueuesPer3DQueue;};
enum D3D12DDI_SHADER_MODEL {D3D12DDI_SHADER_MODEL_5_1_RELEASE_0011=0x50015,D3D12DDI_SHADER_MODEL_6_0_RELEASE_0011=0x60005,D3D12DDI_SHADER_MODEL_6_1_RELEASE_0033=0x60015,D3D12DDI_SHADER_MODEL_6_2_RELEASE_0042=0x60025,D3D12DDI_SHADER_MODEL_6_3_RELEASE_0054=0x60035,D3D12DDI_SHADER_MODEL_6_4_RELEASE_0062=0x60045,D3D12DDI_SHADER_MODEL_6_5_RELEASE_0071=0x60055};
struct D3D12DDI_D3D12_SHADER_MODELS_DATA_0011 {UINT *pNumShaderModelsSupported;D3D12DDI_SHADER_MODEL *pShaderModelsSupported;};
static bool ShaderModel6Enabled(){return false;}
static bool Negotiated0033(){return false;}
static bool Negotiated0042(){return false;}
static bool Negotiated0071(){return false;}
static bool r2=false;
static bool NegotiatedR2(){return r2;}
enum D3D12DDI_COMMAND_QUEUE_FLAGS {D3D12DDI_COMMAND_QUEUE_FLAG_NONE=0};
struct D3D12DDICAPS_UMD_BASED_COMMAND_QUEUE_PRIORITY_DATA_0023 {D3D12DDI_COMMAND_QUEUE_FLAGS SupportedQueueFlagsForGlobalRealtimeQueues;};
enum D3D12DDI_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAGS_0030 {D3D12DDI_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAG_0030_NONE=0};
struct D3D12DDI_PROTECTED_RESOURCE_SESSION_SUPPORT_DATA_0030 {UINT NodeIndex;D3D12DDI_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAGS_0030 Support;};
static constexpr int kTypedUavLoads = 1;
#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif
static void AdmissionUmdDiagnostic(const char *,HRESULT,const UINT *,UINT){}
/* ACTUAL_CAPS */
int main(){
 static_assert(sizeof(D3D12DDI_SWIZZLE_BIT_ENTRY)==1);
 static_assert(sizeof(D3D12DDI_SWIZZLE_PATTERN_DESC_0003)==100);
 static_assert(sizeof(D3D12DDI_SWIZZLE_PATTERN_DESC_0004)==104);
 static_assert(sizeof(D3D12DDI_TEXTURE_LAYOUT_CAPS_0001)==16);
 struct {unsigned pre;unsigned char data[256];unsigned post;} out;
 auto query=[&](D3D12DDICAPS_TYPE type,void *index,unsigned size){
  memset(&out,0xa5,sizeof(out));D3D12DDIARG_GETCAPS q={type,index,out.data,size};
  HRESULT hr=GetCaps({},&q);assert(out.pre==0xa5a5a5a5&&out.post==0xa5a5a5a5);
  for(unsigned n=size;n<256;n++)assert(out.data[n]==0xa5);return hr;
 };
 for(unsigned size: {12u,16u}){
  assert(query(D3D12DDICAPS_TYPE_TEXTURE_LAYOUT,nullptr,size)==S_OK);
  auto *c=reinterpret_cast<D3D12DDI_TEXTURE_LAYOUT_CAPS_0001 *>(out.data);
  assert(c->DeviceDependentLayoutCount==6&&c->DeviceDependentSwizzleCount==6&&!c->Supports64KStandardSwizzle);
  if(size==16)assert(!c->SupportsRowMajorTexture);
 }
 for(UINT i=0;i<6;i++){
  assert(query(D3D12DDICAPS_TYPE_TEXTURE_LAYOUT,&i,8)==S_OK);
  UINT ids[2];memcpy(ids,out.data,8);assert(ids[0]==0x100+i&&ids[1]==0x100+(i<5?i+1:5));
  assert(query(D3D12DDICAPS_TYPE_SWIZZLE_PATTERN,&i,100)==S_OK);
  D3D12DDI_SWIZZLE_PATTERN_DESC_0003 p;memcpy(&p,out.data,100);assert(!p.StackDepthSlices);
  unsigned bits=2+2*(5-i);
  for(unsigned n=0;n<32;n++){
   auto v=p.InterleavePatternSourceBits[n];assert(v.Valid==(n<bits));
   assert(!p.InterleavePatternXORSourceBits[n].Valid&&!p.InterleavePatternXOR2SourceBits[n].Valid);
   if(n<bits){assert(v.ChannelIndex==(n<2?0:(n-2)%2));assert(v.SourceBitIndex==(n<2?n:((n-2)%2?(n-3)/2:2+(n-2)/2)));}
  }
  // Evaluate actual packed descriptor bytes for every byte of its FULL block.
  const unsigned side=32u>>i;bool seen[4096]={};
  for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++)for(unsigned byte=0;byte<4;byte++){
   UINT source[4]={4*x+byte,y,0,0},address=0,expected=byte;
   for(unsigned n=0;n<bits;n++){auto v=p.InterleavePatternSourceBits[n];address|=((source[v.ChannelIndex]>>v.SourceBitIndex)&1u)<<n;}
   for(unsigned k=0;k<5;k++)expected|=((x>>k)&1u)<<(2+2*k),expected|=((y>>k)&1u)<<(3+2*k);
   assert(address==expected&&address<4*side*side&&!seen[address]);seen[address]=true;
  }
  assert(query(D3D12DDICAPS_TYPE_SWIZZLE_PATTERN,&i,104)==E_INVALIDARG);
  assert(query(D3D12DDICAPS_TYPE_TEXTURE_LAYOUT1,&i,24)==E_NOTIMPL);
 }
 UINT bad=6;assert(query(D3D12DDICAPS_TYPE_TEXTURE_LAYOUT,&bad,8)==E_INVALIDARG);
 assert(query(D3D12DDICAPS_TYPE_SWIZZLE_PATTERN,&bad,100)==E_INVALIDARG);
 assert(query(D3D12DDICAPS_TYPE_SWIZZLE_PATTERN,nullptr,100)==E_INVALIDARG);
 assert(query(D3D12DDICAPS_TYPE_TEXTURE_LAYOUT,nullptr,8)==E_INVALIDARG);
 D3D12DDIARG_GETCAPS q={D3D12DDICAPS_TYPE_TEXTURE_LAYOUT,nullptr,nullptr,16};assert(GetCaps({},&q)==E_NOTIMPL);
 assert(GetCaps({},nullptr)==E_NOTIMPL);
 // EXP1870: R1 runtimes keep E_NOTIMPL for the 0022 forms; R2+ runtimes get
 // the same registry (caps _0026, 3x6 patterns, the 0003 interleave bits)
 assert(query(D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT,nullptr,20)==E_NOTIMPL);
 r2=true;
 static_assert(sizeof(D3D12DDI_TEXTURE_LAYOUT_CAPS_0026)==20);
 assert(query(D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT,nullptr,20)==S_OK);
 {auto *c=reinterpret_cast<D3D12DDI_TEXTURE_LAYOUT_CAPS_0026 *>(out.data);
  assert(c->DeviceDependentLayoutCount==6&&c->DeviceDependentSwizzleCount==6&&!c->Supports64KStandardSwizzle&&
         !c->SupportsRowMajorTexture&&!c->IndexableSwizzlePatterns);}
 assert(query(D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT,nullptr,16)==E_INVALIDARG);
 for(UINT i=0;i<6;i++){
  assert(query(D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT,&i,72)==S_OK);
  UINT ids[18];memcpy(ids,out.data,72);
  for(UINT k=0;k<18;k++)assert(ids[k]==(k%6==0?0x100+i:0x100+(i<5?i+1:5)));
  assert(query(D3D12DDICAPS_TYPE_SWIZZLE_PATTERN,&i,100)==S_OK);
  D3D12DDI_SWIZZLE_PATTERN_DESC_0003 legacy;memcpy(&legacy,out.data,100);
  assert(query(D3D12DDICAPS_TYPE_0022_SWIZZLE_PATTERN,&i,sizeof(D3D12DDI_SWIZZLE_PATTERN_DESC_0022))==S_OK);
  D3D12DDI_SWIZZLE_PATTERN_DESC_0022 p;memcpy(&p,out.data,sizeof(p));
  assert(!memcmp(p.InterleavePatternSourceBits,legacy.InterleavePatternSourceBits,32));
  for(unsigned n=0;n<32;n++)assert(!p.InterleavePatternXORSourceBits[n].Valid&&!p.InterleavePatternXOR2SourceBits[n].Valid&&
      !p.InterleavePatternXOR3SourceBits[n].Valid&&!p.InterleavePatternXOR4SourceBits[n].Valid&&
      !p.PostambleXORSourceBits[n].Valid&&!p.PostambleXOR2SourceBits[n].Valid);
  assert(!p.PostambleXORImmediate&&p.Flags==D3D12DDI_SWIZZLE_PATTERN_FLAGS_NONE);
 }
 assert(query(D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT,&bad,72)==E_INVALIDARG);
 assert(query(D3D12DDICAPS_TYPE_0022_SWIZZLE_PATTERN,&bad,sizeof(D3D12DDI_SWIZZLE_PATTERN_DESC_0022))==E_INVALIDARG);
 r2=false;
 puts("PASS exact WDK100/104 ABI immutable registry/GetCaps buffer canaries/legacy mode");
}
