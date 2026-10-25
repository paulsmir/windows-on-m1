#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
using UINT=unsigned; using UINT64=uint64_t; using SIZE_T=size_t; using BYTE=unsigned char; using HRESULT=int;
#define APIENTRY
#define APPLE_AGX_D3D12_NATIVE_OWNER
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
#define ARRAYSIZE(x) (sizeof(x)/sizeof((x)[0]))
constexpr int S_OK=0,E_INVALIDARG=-1,E_OUTOFMEMORY=-2;
constexpr UINT MEM_COMMIT=1,MEM_RESERVE=2,MEM_RELEASE=4,PAGE_READWRITE=1;
constexpr UINT D3D12DDI_DESCRIPTOR_HEAP_TYPE_NUM_TYPES=4,D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV=0,D3D12DDI_DESCRIPTOR_HEAP_TYPE_SAMPLER=1;
constexpr UINT D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE=2,D3D12DDI_MEMORY_POOL_L0=0,D3D12DDI_CPU_PAGE_PROPERTY_WRITE_COMBINE=1;
constexpr UINT64 kHeapPage=65536;
constexpr UINT kCbvSrvUavDescriptorBytes=160;
struct AgxD3d12Heap {void *native,*cpu; BYTE *mirror; UINT64 bytes,gpuva,descriptor_bytes;};
struct AgxD3d12DescriptorHeap {AgxD3d12Heap memory;void *cpu;UINT64 gpuva;UINT type,descriptors;};
struct AgxD3d12Device {int heap_lock;AgxD3d12Heap *mirrored[64];UINT mirrored_count;};
struct D3D12DDI_HDEVICE {AgxD3d12Device *p;};
struct D3D12DDI_HDESCRIPTORHEAP {void *pDrvPrivate;};
struct D3D12DDI_HHEAP {AgxD3d12Heap *p;};
struct D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 {UINT Type,NumDescriptors,Flags;};
struct D3D12DDIARG_CREATEHEAP_0001 {UINT64 ByteSize,Alignment;UINT MemoryPool,CPUPageProperty;};
struct AgxD3d12HeapForm {BYTE texture[24],pbe[24],counter[24],rest[24];};
static AgxD3d12HeapForm *HeapFormOf(void *p){return reinterpret_cast<AgxD3d12HeapForm *>(static_cast<BYTE *>(p)+64);}
static UINT DescriptorBytes(UINT t){return t==0?160:t==1?128:64;}
static AgxD3d12Device *DeviceOf(D3D12DDI_HDEVICE h){return h.p;}
static const char *option=""; static bool fail_map=false,fail_shadow=false;static UINT64 allocated;
static UINT GetEnvironmentVariableA(const char *,char *v,SIZE_T n){size_t len=strlen(option);if(len<n)memcpy(v,option,len+1);return UINT(len);}
static void *VirtualAlloc(void *,SIZE_T n,UINT,UINT){return fail_shadow?nullptr:calloc(1,n);}
static void VirtualFree(void *p,UINT,UINT){free(p);}
static HRESULT CreateHeap(AgxD3d12Device *,const D3D12DDIARG_CREATEHEAP_0001 &a,AgxD3d12Heap *h){
 memset(h,0,sizeof(*h));h->bytes=(a.ByteSize+65535)&~UINT64(65535);allocated=h->bytes;h->cpu=malloc(h->bytes);memset(h->cpu,0xa5,h->bytes);h->native=h->cpu;h->gpuva=0x1234000000ull;return S_OK;}
static HRESULT MapHeap(D3D12DDI_HDEVICE,D3D12DDI_HHEAP h,void **p){if(fail_map)return E_OUTOFMEMORY;*p=h.p->cpu;return S_OK;}
static void ReleaseHeap(AgxD3d12Device *,AgxD3d12Heap *h){free(h->native);memset(h,0,sizeof(*h));}
static void AcquireSRWLockExclusive(int *){} static void ReleaseSRWLockExclusive(int *){}
static void AgxD3d12NullTextureDescriptor(void *p){memset(p,0x11,24);}
static void AgxD3d12NullPbeDescriptor(void *p){memset(p,0x22,24);}
static void AdmissionUmdDiagnostic(const char *,HRESULT,const UINT *,UINT){}
/* CREATE */
int main(){
 for(UINT type: {0u,1u,2u}) for(UINT count: {1u,409u,2048u,131072u}) for(UINT flags: {0u,1u,2u,3u}) for(const char *env: {"","0","1","11"}){
  option=env;AgxD3d12Device d={};AgxD3d12DescriptorHeap h={};D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 a={type,count,flags};
  assert(CreateDescriptorHeap({&d},&a,{&h})==S_OK);UINT64 logical=UINT64(count)*DescriptorBytes(type);
  bool visible=(flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)!=0;
  bool guard=visible && type<2 && !strcmp(env,"1");
  assert(h.descriptors==count);
  if(visible){if(!guard)assert(DescriptorHeapBytes(&h.memory)==h.memory.bytes);assert(allocated==((logical+65535)&~UINT64(65535))+(guard?65536:0));
   if(guard){assert(DescriptorHeapBytes(&h.memory)==logical);assert(h.memory.bytes-logical>=65536);
    const BYTE *gpu=h.memory.mirror;assert(gpu);
    for(UINT64 at=logical;at+DescriptorBytes(type)<=h.memory.bytes;at+=DescriptorBytes(type)){
     for(UINT j=0;j<DescriptorBytes(type);++j){BYTE expected=type==0 && j>=64 && j<88?0x11:type==0 && j>=88 && j<136?0x22:0;assert(gpu[at+j]==expected);}
    }
   }
   free(h.memory.cpu);free(h.memory.native);
  }else free(h.cpu);
 }
 fail_map=fail_shadow=false;
 {option="1";AgxD3d12Device d={};d.mirrored_count=64;AgxD3d12DescriptorHeap h={};D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 a={0,10,3};assert(CreateDescriptorHeap({&d},&a,{&h})==E_OUTOFMEMORY);assert(!h.cpu && !h.gpuva && !h.memory.native);}
 for(bool map: {false,true}) {option="1";fail_map=map;fail_shadow=!map;AgxD3d12Device d={};AgxD3d12DescriptorHeap h={};D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 a={0,10,3};assert(CreateDescriptorHeap({&d},&a,{&h})==E_OUTOFMEMORY);assert(!h.cpu && !h.gpuva && !h.memory.native);}
}
