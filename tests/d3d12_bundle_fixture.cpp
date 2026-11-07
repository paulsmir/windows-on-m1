// C30: the actual bundle call log (BundleAppend/Bundled/ExecuteBundle)
// against recording fakes of the DDIs it replays.
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <tuple>
#include <type_traits>
#define APIENTRY
#define VOID void
using UINT=unsigned;using UINT64=unsigned long long;using SIZE_T=std::size_t;using FLOAT=float;using HRESULT=int;
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_OUTOFMEMORY (-2)
enum {D3D12DDI_COMMAND_LIST_TYPE_DIRECT=0,D3D12DDI_COMMAND_LIST_TYPE_BUNDLE=1};
static constexpr UINT kVertexBuffers=32;
struct D3D12DDI_HCOMMANDLIST {void *pDrvPrivate;};
struct D3D12DDI_VERTEX_BUFFER_VIEW {UINT64 BufferLocation;UINT SizeInBytes,StrideInBytes;};
static void *GetProcessHeap() {return nullptr;}
static void *HeapAlloc(void *,UINT,SIZE_T n) {return std::malloc(n);}
static void *HeapReAlloc(void *,UINT,void *p,SIZE_T n) {return std::realloc(p,n);}
struct AgxD3d12CommandList {UINT type,closed,commands;int error;unsigned char *bundle;SIZE_T bundle_bytes,bundle_capacity;};
static void ListError(AgxD3d12CommandList *l,int e) {l->error=e;}
static void AdmissionUmdDiagnostic(const char *,HRESULT,const UINT *,UINT) {}
/* recording fakes: one line per call, with the handle it arrived on */
static char trace[65536];
static void note(const char *fmt,...) __attribute__((format(printf,1,2)));
#include <cstdarg>
static void note(const char *fmt,...) {va_list a;va_start(a,fmt);size_t n=std::strlen(trace);std::vsnprintf(trace+n,sizeof(trace)-n,fmt,a);va_end(a);}
static VOID APIENTRY DrawInstanced(D3D12DDI_HCOMMANDLIST h,UINT v,UINT i,UINT sv,UINT si) {note("draw %p %u %u %u %u;",h.pDrvPrivate,v,i,sv,si);}
static VOID APIENTRY FakeVertexBuffers(D3D12DDI_HCOMMANDLIST h,UINT start,UINT n,const D3D12DDI_VERTEX_BUFFER_VIEW *v) {
 note("vb %p %u %u",h.pDrvPrivate,start,n);for(UINT k=0;v&&k<n;++k)note(" %llx/%u/%u",v[k].BufferLocation,v[k].SizeInBytes,v[k].StrideInBytes);note(";");
}
#define AGX_VB_SETTER FakeVertexBuffers
static VOID APIENTRY SetGraphicsRoot32BitConstants(D3D12DDI_HCOMMANDLIST h,UINT idx,UINT n,const void *d,UINT off) {
 note("gconst %p %u %u %u",h.pDrvPrivate,idx,n,off);for(UINT k=0;k<n;++k)note(" %u",static_cast<const UINT *>(d)[k]);note(";");
}
static VOID APIENTRY SetComputeRoot32BitConstants(D3D12DDI_HCOMMANDLIST h,UINT idx,UINT n,const void *,UINT off) {note("cconst %p %u %u %u;",h.pDrvPrivate,idx,n,off);}
/* ACTUAL_BUNDLE */
int main() {
 AgxD3d12CommandList bundle={D3D12DDI_COMMAND_LIST_TYPE_BUNDLE},direct={D3D12DDI_COMMAND_LIST_TYPE_DIRECT},other={D3D12DDI_COMMAND_LIST_TYPE_BUNDLE};
 D3D12DDI_HCOMMANDLIST b={&bundle},d={&direct},o={&other};
 // a direct list runs the DDI at once
 Bundled<DrawInstanced>::call(d,1,2,3,4);char want[512];std::snprintf(want,sizeof want,"draw %p 1 2 3 4;",(void *)&direct);
 assert(!std::strcmp(trace,want));trace[0]=0;
 // a bundle copies pointer arguments: the caller's arrays change afterwards
 D3D12DDI_VERTEX_BUFFER_VIEW views[2]={{0x1000,64,16},{0x2000,96,12}};UINT k[3]={7,8,9};
 BundledVertexBuffers(b,1,2,views);Bundled<DrawInstanced>::call(b,3,1,0,0);BundledRootConstants<true>(b,2,3,k,5);
 std::memset(views,0xee,sizeof views);k[0]=k[1]=k[2]=0;
 assert(!trace[0] && !bundle.commands && bundle.bundle_bytes);
 ExecuteBundle(d,b);assert(direct.error==E_INVALIDARG && !trace[0]);direct.error=0; /* not closed */
 bundle.closed=1;ExecuteBundle(d,b);ExecuteBundle(d,b);
 char once[512];std::snprintf(once,sizeof once,"vb %p 1 2 1000/64/16 2000/96/12;draw %p 3 1 0 0;gconst %p 2 3 5 7 8 9;",(void *)&direct,(void *)&direct,(void *)&direct);
 std::snprintf(want,sizeof want,"%s%s",once,once);assert(!direct.error && !std::strcmp(trace,want));trace[0]=0;
 // refusals: a bundle executing a bundle, a direct list as the bundle
 other.closed=1;ExecuteBundle(o,b);assert(other.error==E_INVALIDARG && !trace[0]);
 ExecuteBundle(d,d);assert(direct.error==E_INVALIDARG && !trace[0]);direct.error=0;
 // oversized constants are refused at record time; a NULL view array replays as NULL
 BundledRootConstants<false>(b,0,65,k,0);assert(bundle.error==E_INVALIDARG);bundle.error=0;
 // growth keeps every record: 600 draws after the three calls above
 bundle.closed=0;for(UINT i=0;i<600;++i)Bundled<DrawInstanced>::call(b,i,1,0,0);
 BundledVertexBuffers(b,0,0,nullptr);bundle.closed=1;ExecuteBundle(d,b);
 char tail[128];std::snprintf(tail,sizeof tail,"draw %p 599 1 0 0;vb %p 0 0;",(void *)&direct,(void *)&direct);
 const size_t n=std::strlen(trace),t=std::strlen(tail);assert(!direct.error && n>t && !std::strcmp(trace+n-t,tail));
 assert(bundle.bundle_capacity>4096);std::free(bundle.bundle);
 std::puts("PASS C30 bundle call log: by-value copies, caller-handle replay, repeat, growth, refusals");
 return 0;
}
