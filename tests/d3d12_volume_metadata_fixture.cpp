#include <cassert>
#include <cstring>
#include <cstdint>
#include <cstdio>
using UINT=unsigned;using UINT64=unsigned long long;using BOOL=int;using D3DKMT_HANDLE=unsigned;using HRESULT=int;using VOID=void;
#define APIENTRY
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_FAIL (-2)
#define FAILED(x) ((x)<0)
enum {D3D12DDI_RT_TEXTURE1D=2,D3D12DDI_RT_BUFFER=1,D3D12DDI_RT_TEXTURE2D=3,D3D12DDI_RT_TEXTURE3D=4,D3D12DDI_TL_UNDEFINED=0,D3D12DDI_TL_ROW_MAJOR=1};
using DXGI_FORMAT=unsigned;using D3D12DDI_TEXTURE_LAYOUT=unsigned;using UINT8=unsigned char;
struct D3D12DDI_HDEVICE {void *pDrvPrivate;};struct D3D12DDI_HHEAP {void *pDrvPrivate;};struct D3D12DDI_HRESOURCE {void *pDrvPrivate;};
struct D3D12DDI_SUBRESOURCE_INFO {UINT64 Offset,RowStride,DepthStride;unsigned short RowBytePreSwizzleOffset,ColumnPreSwizzleOffset,DepthPreSwizzleOffset;};
struct D3D12DDI_RESOURCE_ALLOCATION_INFO {UINT64 ResourceDataSize,ResourceDataAlignment;unsigned Layout;UINT8 MipLevelSwizzleTransition;};
struct D3DDDICB_LOCK {D3DKMT_HANDLE hAllocation;struct {int LockEntire;} Flags;void *pData;};
struct Kernel {HRESULT (*pfnLockCb)(void *,D3DDDICB_LOCK *);};
struct AgxD3d12Device {Kernel *kernel;struct {void *handle;} runtime;};
static AgxD3d12Device *DeviceOf(D3D12DDI_HDEVICE h){return static_cast<AgxD3d12Device *>(h.pDrvPrivate);}
static void AdmissionUmdDiagnostic(const char *,HRESULT,const UINT *,UINT) {}
static const UINT64 kHeapPage=0x10000;
/* ACTUAL_METADATA */
int main() {
 AgxD3d12Heap heap={};heap.bytes=0x20000;heap.allocation=7;heap.volume=1;
 AgxD3d12Resource resource={};resource.heap=&heap;resource.type=D3D12DDI_RT_TEXTURE3D;resource.levels=2;resource.width=4;resource.volume_depth_stride=16384;resource.volume_layout=0x103;resource.volume_transition=1;
 resource.volume_offsets[0]=0;resource.volume_offsets[1]=128;
 D3D12DDI_HRESOURCE h={&resource};D3D12DDI_SUBRESOURCE_INFO info={};
 CheckSubresourceInfo({},h,1,&info);assert(info.Offset==128 && info.RowStride==16 && info.DepthStride==16384);
 CheckSubresourceInfo({},h,2,&info);assert(!info.Offset && !info.RowStride && !info.DepthStride);
 CheckSubresourceInfo({},h,3,&info);assert(!info.Offset && !info.RowStride && !info.DepthStride);
 D3D12DDI_RESOURCE_ALLOCATION_INFO allocation={};D3DKMT_HANDLE allocation_handle=0;
 CheckExistingResourceAllocationInfo({},h,&allocation,&allocation_handle);
 assert(allocation.ResourceDataSize==0x20000 && allocation.ResourceDataAlignment==0x10000 && allocation_handle==7 && allocation.Layout==0x103 && allocation.MipLevelSwizzleTransition==1);
 // Even deliberately injected CPU fields cannot make a DEFAULT volume map.
 heap.native=&resource;heap.cpu=&resource;heap.cpu_visible=1;void *mapped=&heap;
 assert(MapHeap({}, {&heap}, &mapped)==E_INVALIDARG && !mapped);
 heap.volume=0;resource.type=D3D12DDI_RT_BUFFER;resource.bytes=65536;
 CheckSubresourceInfo({},h,0,&info);assert(info.RowStride==65536 &&info.DepthStride==65536);
 puts("PASS volume nativeMipOffsets/nativeBlockStrides/explicitMaprefusal");
}
