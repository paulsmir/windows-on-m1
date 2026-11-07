/* Original regression: public allocation composition must preserve a nonzero
 * resource when both extra-data regions are absent. WDK records and actual
 * query/existing callback bodies are inserted by the renderer. Native layout
 * test inputs below are stubs: this test does not claim native size coverage. */
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using UINT=unsigned;using UINT32=unsigned;using UINT16=unsigned short;
using UINT8=unsigned char;using UINT64=unsigned long long;using BOOL=int;
using HRESULT=int;using VOID=void;using DXGI_FORMAT=unsigned;using D3DKMT_HANDLE=unsigned;
#define APIENTRY
#define CONST const
#define S_OK 0
#define APPLE_AGX_D3D12_NATIVE_OWNER 1
struct D3D12DDI_HDEVICE {void *pDrvPrivate;};struct D3D12DDI_HRESOURCE {void *pDrvPrivate;};
struct DXGI_SAMPLE_DESC {UINT Count,Quality;};
/* PINNED_WDK */
struct AGX_D3D12_VOLUME_LAYOUT {UINT64 Bytes,NativeBytes,DepthStride,MipOffsets[16];};
static constexpr UINT64 kHeapPage=65536,kVaMinimum=65536,kVaLimit=1ull<<39;
static void AdmissionUmdDiagnostic(const char *,HRESULT,const UINT *,UINT){}
static bool LineLayoutInfo(const D3D12DDIARG_CREATERESOURCE_0003 *,AGX_D3D12_VOLUME_LAYOUT *o,D3D12DDI_TEXTURE_LAYOUT *l){o->Bytes=65536;*l=static_cast<D3D12DDI_TEXTURE_LAYOUT>(0x117);return true;}
/* C21G: VolumeAllocationQuery's selection, stubbed (two exact square slots) */
#define AGX_VOLUME_MAX_SLOTS 6u
#define AGX_VOLUME_NO_TRANSITION 0xffu
static bool VolumeLayoutInfo(const D3D12DDIARG_CREATERESOURCE_0003 *a,AGX_D3D12_VOLUME_LAYOUT *o,D3D12DDI_TEXTURE_LAYOUT *l,UINT8 *t,UINT,UINT64 *,UINT *r,HRESULT *s){o->Bytes=a->Width==32?65536:131072;*l=static_cast<D3D12DDI_TEXTURE_LAYOUT>(a->Width==32?0x112:0x115);std::memset(t,0xff,5);t[0]=1;*r=0;*s=0;return true;}
static UINT64 TextureBytes(const D3D12DDIARG_CREATERESOURCE_0003 *a){return a&&a->ResourceType==D3D12DDI_RT_TEXTURE2D?65536:0;}
/* ACTUAL_FUNCTIONS */
static unsigned checks,failures;
static void check(bool good,const char *what){++checks;if(!good){++failures;std::printf("FAIL %s\n",what);}}
// Original semantic oracle: checked concatenation of three aligned regions.
// This enforces the observed runtime invariant; it is not copied runtime code.
static UINT64 total(const D3D12DDI_RESOURCE_ALLOCATION_INFO &i){
 UINT64 at=0;const UINT64 sizes[]={i.ResourceDataSize,i.AdditionalDataHeaderSize,i.AdditionalDataSize};
 const UINT64 aligns[]={i.ResourceDataAlignment,i.AdditionalDataHeaderAlignment,i.AdditionalDataAlignment};
 for(unsigned n=0;n<3;++n){const auto a=aligns[n];if(!a||(a&(a-1))||at>~0ull-(a-1))return ~0ull;at=(at+a-1)&~(a-1);if(sizes[n]>~0ull-at)return ~0ull;at+=sizes[n];}
 if(at>~0ull-(i.ResourceDataAlignment-1))return ~0ull;
 return (at+i.ResourceDataAlignment-1)&~(i.ResourceDataAlignment-1);
}
static void canonical(const D3D12DDI_RESOURCE_ALLOCATION_INFO &i,UINT64 bytes){
 check(i.ResourceDataSize==bytes,"resource extent preserved");
 check(!i.AdditionalDataHeaderSize&&!i.AdditionalDataSize,"no invented metadata");
 check(i.AdditionalDataHeaderAlignment==1&&i.AdditionalDataAlignment==1,"canonical byte alignments");
 check(total(i)==bytes,"public composition retains resource size");
 auto bad=i;bad.AdditionalDataHeaderAlignment=0;check(total(bad)==~0ull,"zero header alignment rejected");
 bad=i;bad.AdditionalDataAlignment=0;check(total(bad)==~0ull,"zero data alignment rejected");
}
int main(){
 static_assert(sizeof(D3D12DDI_RESOURCE_ALLOCATION_INFO)==56);
 static_assert(offsetof(D3D12DDI_RESOURCE_ALLOCATION_INFO,AdditionalDataHeaderAlignment)==32);
 static_assert(offsetof(D3D12DDI_RESOURCE_ALLOCATION_INFO,AdditionalDataAlignment)==40);
 CheckResourceAllocationInfo({},nullptr,{},0,0,nullptr);
 CheckExistingResourceAllocationInfo({}, {},nullptr,nullptr);
 for(unsigned poison:{0u,0xa5u,0xffu})for(unsigned kind:{1u,2u,3u,4u})for(UINT64 width:{4ull,32ull}){
  if(kind==2&&!HAS_LINE_BRANCH)continue;
  D3D12DDIARG_CREATERESOURCE_0003 a={};a.ResourceType=static_cast<D3D12DDI_RESOURCE_TYPE>(kind);a.Width=width;a.Height=kind==2?1:static_cast<UINT>(width);a.DepthOrArraySize=kind==4?8:1;a.MipLevels=2;a.SampleDesc.Count=1;
  D3D12DDI_RESOURCE_ALLOCATION_INFO i;memset(&i,poison,sizeof(i));CheckResourceAllocationInfo({},&a,{},0,0,&i);
  const UINT64 bytes=kind==4&&width==4?131072:65536;canonical(i,bytes);
  AgxD3d12Heap heap={};heap.bytes=bytes;heap.allocation=77;
  AgxD3d12Resource resource={};resource.heap=&heap;resource.type=kind;resource.volume_layout=i.Layout;resource.volume_transition=i.MipLevelSwizzleTransition;
  D3D12DDI_RESOURCE_ALLOCATION_INFO e;memset(&e,poison,sizeof(e));D3DKMT_HANDLE allocation=0;
  CheckExistingResourceAllocationInfo({}, {&resource},&e,&allocation);canonical(e,bytes);
  check(allocation==77,"existing allocation handle preserved");
  if(kind==2||kind==4)check(!memcmp(&i,&e,sizeof(i)),"opaque query/existing complete tuple agrees");
 }
 std::printf("CANONICAL_ALLOCATION checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
