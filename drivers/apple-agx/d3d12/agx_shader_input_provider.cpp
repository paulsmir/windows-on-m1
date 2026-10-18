/* Original UNLINKED CPU lease checkpoint. SPDX-License-Identifier: MIT */
#include <windows.h>
#ifdef _WIN32
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable:4201)
#endif
#include <d3d12umddi.h>
#ifdef _WIN32
#pragma warning(pop)
#endif
#include "agx_shader_input_provider.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
namespace {
struct Device {uint64_t id;D3D12DDI_HDEVICE handle;AgxCpuFailureSink sink;void *context;SIZE_T refs;bool retired;Device *next;};
struct Root {uint64_t id;Device *device;D3D12DDI_HROOTSIGNATURE handle;unsigned char *bytes;SIZE_T count,refs;Root *next;};
struct Lease {uint64_t id;Device *device;Root *root;uint32_t stage;D3D12DDIARG_CREATE_SHADER_0010 args;
 D3D12DDIARG_STAGE_IO_SIGNATURES io;AgxShaderTrustedInput input;Lease *next;};
/* ID uniqueness alone uses an atomic; registry operations remain explicitly
 * caller-serialized. No caller-visible ID ever wraps/reuses. */
std::atomic<uint64_t> next_id{1};
uint64_t id(){uint64_t n=next_id.load(std::memory_order_relaxed);
 while(n!=std::numeric_limits<uint64_t>::max())if(next_id.compare_exchange_weak(n,n+1,std::memory_order_relaxed))return n;
 return 0;}
void drop(Device *d){if(!--d->refs)delete d;}
void drop(Root *r){if(!--r->refs){Device*d=r->device;std::free(r->bytes);delete r;drop(d);}}
AgxCpuProviderResult result(AgxCpuProviderStatus s){return {s,AgxShaderStorageStatus::Invalid,false};}
AgxCpuProviderResult report(Device*d,AgxCpuProviderResult r){if(d&&d->sink&&r.status!=AgxCpuProviderStatus::Ok)d->sink(d->context,{d->id},r);return r;}
}
struct AgxCpuProvider {Device *devices=nullptr;Root *roots=nullptr;Lease *leases=nullptr;};
namespace {
Device **find(AgxCpuProvider*p,AgxCpuDevice h){Device**d=&p->devices;while(*d&&(*d)->id!=h.value)d=&(*d)->next;return d;}
Root **find(AgxCpuProvider*p,AgxCpuRoot h){Root**r=&p->roots;while(*r&&(*r)->id!=h.value)r=&(*r)->next;return r;}
Lease **find(AgxCpuProvider*p,AgxCpuInputLease h){Lease**l=&p->leases;while(*l&&(*l)->id!=h.value)l=&(*l)->next;return l;}
}
AgxCpuProvider *AgxCpuProviderCreate(){return new(std::nothrow)AgxCpuProvider{};}
AgxCpuProviderStatus AgxCpuProviderDestroy(AgxCpuProvider **p){
 if(!p||!*p)return AgxCpuProviderStatus::Invalid;if((*p)->leases)return AgxCpuProviderStatus::Busy;
 while((*p)->devices)AgxCpuRetireDevice(*p,{(*p)->devices->id});delete *p;*p=nullptr;return AgxCpuProviderStatus::Ok;
}
AgxCpuProviderStatus AgxCpuRegisterDevice(AgxCpuProvider*p,D3D12DDI_HDEVICE h,AgxCpuFailureSink sink,void*context,AgxCpuDevice*out){
 if(!out)return AgxCpuProviderStatus::Invalid;*out={};if(!p||!h.pDrvPrivate)return AgxCpuProviderStatus::Invalid;
 for(Device*d=p->devices;d;d=d->next)if(d->handle.pDrvPrivate==h.pDrvPrivate)return AgxCpuProviderStatus::Invalid;
 const uint64_t n=id();if(!n)return AgxCpuProviderStatus::Exhausted;auto*d=new(std::nothrow)Device{n,h,sink,context,1,false,p->devices};
 if(!d)return AgxCpuProviderStatus::ProjectOOM;p->devices=d;*out={n};return AgxCpuProviderStatus::Ok;
}
AgxCpuProviderStatus AgxCpuRetireRoot(AgxCpuProvider*p,AgxCpuDevice dh,AgxCpuRoot rh){
 if(!p)return AgxCpuProviderStatus::Invalid;Device*d=*find(p,dh);if(!d)return AgxCpuProviderStatus::NotLiveDevice;
 Root**entry=find(p,rh);Root*r=*entry;if(!r)return AgxCpuProviderStatus::NotLiveRoot;if(r->device!=d)return AgxCpuProviderStatus::WrongDevice;
 *entry=r->next;drop(r);return AgxCpuProviderStatus::Ok;
}
AgxCpuProviderStatus AgxCpuRetireDevice(AgxCpuProvider*p,AgxCpuDevice dh){
 if(!p)return AgxCpuProviderStatus::Invalid;Device**entry=find(p,dh);Device*d=*entry;if(!d)return AgxCpuProviderStatus::NotLiveDevice;
 Root**r=&p->roots;while(*r){if((*r)->device==d){Root*old=*r;*r=old->next;drop(old);}else r=&(*r)->next;}
 *entry=d->next;d->retired=true;drop(d);return AgxCpuProviderStatus::Ok;
}
AgxCpuProviderStatus AgxCpuRegisterRoot(AgxCpuProvider*p,AgxCpuDevice dh,D3D12DDI_HROOTSIGNATURE h,AgxShaderTrustedSpan bytes,AgxCpuRoot*out){
 if(!out)return AgxCpuProviderStatus::Invalid;*out={};if(!p||!h.pDrvPrivate||(bytes.bytes&&!bytes.data))return AgxCpuProviderStatus::Invalid;
 Device*d=*find(p,dh);if(!d)return AgxCpuProviderStatus::NotLiveDevice;
 for(Root*r=p->roots;r;r=r->next)if(r->handle.pDrvPrivate==h.pDrvPrivate)return AgxCpuProviderStatus::Invalid;
 if(d->refs==std::numeric_limits<SIZE_T>::max())return AgxCpuProviderStatus::Exhausted;
 uint64_t n=id();if(!n)return AgxCpuProviderStatus::Exhausted;Root*r=new(std::nothrow)Root{n,d,h,nullptr,bytes.bytes,1,nullptr};
 if(!r)return AgxCpuProviderStatus::ProjectOOM;
 if(bytes.bytes){r->bytes=static_cast<unsigned char*>(std::malloc(bytes.bytes));if(!r->bytes){delete r;return AgxCpuProviderStatus::ProjectOOM;}std::memcpy(r->bytes,bytes.data,bytes.bytes);}
 ++d->refs;r->next=p->roots;p->roots=r;*out={n};return AgxCpuProviderStatus::Ok;
}
AgxCpuProviderResult AgxCpuAcquireInput(AgxCpuProvider*p,AgxCpuDevice dh,AgxCpuRoot rh,
 const D3D12DDIARG_CREATE_SHADER_0010*args,uint32_t stage,AgxShaderTrustedSpan program,
 AgxShaderTrustedSpan si,AgxShaderTrustedSpan so,AgxCpuInputLease*out){
 if(!out)return result(AgxCpuProviderStatus::Invalid);*out={};if(!p)return result(AgxCpuProviderStatus::Invalid);
 Device*d=*find(p,dh);if(!d)return result(AgxCpuProviderStatus::NotLiveDevice);
 Root*r=*find(p,rh);if(!r)return report(d,result(AgxCpuProviderStatus::NotLiveRoot));
 if(r->device!=d)return report(d,result(AgxCpuProviderStatus::WrongDevice));
 if(!args||args->hRootSignature.pDrvPrivate!=r->handle.pDrvPrivate||program.data!=args->pShaderCode||!program.data||!program.bytes)
  return report(d,result(AgxCpuProviderStatus::Invalid));
 if(d->refs==std::numeric_limits<SIZE_T>::max()||r->refs==std::numeric_limits<SIZE_T>::max())return report(d,result(AgxCpuProviderStatus::Exhausted));
 uint64_t n=id();if(!n)return report(d,result(AgxCpuProviderStatus::Exhausted));auto*l=new(std::nothrow)Lease{};
 if(!l)return report(d,result(AgxCpuProviderStatus::ProjectOOM));
 l->id=n;l->device=d;l->root=r;l->stage=stage;l->args=*args;
 if(args->IOSignatures.Standard){l->io=*args->IOSignatures.Standard;l->args.IOSignatures.Standard=&l->io;}
 l->input={program,si,so,{r->bytes,r->count}};++d->refs;++r->refs;l->next=p->leases;p->leases=l;*out={n};return result(AgxCpuProviderStatus::Ok);
}
AgxCpuProviderStatus AgxCpuReleaseInput(AgxCpuProvider*p,AgxCpuInputLease h){
 if(!p)return AgxCpuProviderStatus::Invalid;Lease**entry=find(p,h);Lease*l=*entry;if(!l)return AgxCpuProviderStatus::Invalid;
 *entry=l->next;Root*r=l->root;Device*d=l->device;delete l;drop(r);drop(d);return AgxCpuProviderStatus::Ok;
}
AgxCpuProviderStatus AgxCpuLeaseRootBytes(AgxCpuProvider*p,AgxCpuInputLease h,AgxShaderTrustedSpan*out){
 if(!out)return AgxCpuProviderStatus::Invalid;*out={};if(!p)return AgxCpuProviderStatus::Invalid;
 Lease*l=*find(p,h);if(!l)return AgxCpuProviderStatus::Invalid;*out={l->root->bytes,l->root->count};return AgxCpuProviderStatus::Ok;
}
AgxCpuProviderResult AgxCpuCalculate(AgxCpuProvider*p,AgxCpuInputLease h,AgxShaderOwnedLayout*out){
 if(!out)return result(AgxCpuProviderStatus::Invalid);std::memset(out,0,sizeof(*out));if(!p)return result(AgxCpuProviderStatus::Invalid);
 Lease*l=*find(p,h);if(!l)return result(AgxCpuProviderStatus::Invalid);
 if(l->device->retired)return report(l->device,result(AgxCpuProviderStatus::RetiredDevice));
 auto c=AgxShaderOwnedCalculate(l->device->handle,&l->args,l->stage,&l->input,out);
 return report(l->device,{c==AgxShaderStorageStatus::Ok?AgxCpuProviderStatus::Ok:AgxCpuProviderStatus::CoreFailure,c,true});
}
AgxCpuProviderResult AgxCpuCopyOwned(AgxCpuProvider*p,AgxCpuInputLease h,void*storage,SIZE_T capacity,AgxShaderOwned**out){
 if(!out)return result(AgxCpuProviderStatus::Invalid);*out=nullptr;if(!p)return result(AgxCpuProviderStatus::Invalid);
 Lease*l=*find(p,h);if(!l)return result(AgxCpuProviderStatus::Invalid);
 if(l->device->retired)return report(l->device,result(AgxCpuProviderStatus::RetiredDevice));
 AgxShaderOwned*pending=nullptr;auto c=AgxShaderOwnedCreate(l->device->handle,&l->args,l->stage,&l->input,storage,capacity,&pending);
 if(c!=AgxShaderStorageStatus::Ok)return report(l->device,{AgxCpuProviderStatus::CoreFailure,c,true});
 *out=pending;return {AgxCpuProviderStatus::Ok,c,true};
}
const void *AgxCpuResolveDeviceIdentity(AgxCpuProvider*p,D3D12DDI_HDEVICE h){
 if(!p)return nullptr;for(Device*d=p->devices;d;d=d->next)if(d->handle.pDrvPrivate==h.pDrvPrivate)return d;return nullptr;
}
