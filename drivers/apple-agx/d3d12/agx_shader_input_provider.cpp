/* Original UNLINKED CPU provider phase prototype. SPDX-License-Identifier: MIT */
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
#include <mutex>
#include <new>
#include <thread>
namespace {
using P=AgxCpuProviderStatus;
struct Device {uint64_t id;D3D12DDI_HDEVICE handle;AgxCpuFailureSink sink;void *context;
 SIZE_T refs=1,leases=0,operations=0,scopes=0,sinks=0,preparing=0;bool retired=false,sink_closed=false,quiescing=false;Device *next=nullptr;};
struct Root {uint64_t id;Device *device;D3D12DDI_HROOTSIGNATURE handle;unsigned char *bytes;SIZE_T count,refs;Root *next;};
struct Lease {uint64_t id;Device *device;Root *root;uint32_t stage;D3D12DDIARG_CREATE_SHADER_0010 args;
 D3D12DDIARG_STAGE_IO_SIGNATURES io;AgxShaderTrustedInput input;SIZE_T operations=0;Lease *next=nullptr;};
enum class Mode {Core,Reporting,Device};
struct Scope {AgxCpuProvider *provider;Device *device;Mode mode;Scope *previous;};
thread_local Scope *active_scope=nullptr;
struct Active {Scope scope;Active(AgxCpuProvider*p,Device*d,Mode m):scope{p,d,m,active_scope}{active_scope=&scope;}
 ~Active(){active_scope=scope.previous;}};
enum class State {Ready,Running,Finished};
struct Operation {uint64_t id;Lease *lease;Device *device;Root *root;uint32_t stage;
 D3D12DDIARG_CREATE_SHADER_0010 args;D3D12DDIARG_STAGE_IO_SIGNATURES io;AgxShaderTrustedInput input;
 State state=State::Ready;Operation *next=nullptr;};
struct DeviceScope {uint64_t id;Device *device;std::thread::id thread;Scope scope;DeviceScope *next=nullptr;};
struct Ticket {uint64_t id;Device *device;Ticket *next=nullptr;};
std::atomic<uint64_t> next_id{1};
uint64_t id(){uint64_t n=next_id.load(std::memory_order_relaxed);
 while(n!=std::numeric_limits<uint64_t>::max())if(next_id.compare_exchange_weak(n,n+1,std::memory_order_relaxed))return n;
 return 0;}
bool room(SIZE_T n){return n!=std::numeric_limits<SIZE_T>::max();}
bool reentrant(AgxCpuProvider*p){for(Scope*s=active_scope;s;s=s->previous)if(s->provider==p&&s->mode!=Mode::Device)return true;return false;}
AgxCpuProviderResult result(P s){return {s,AgxShaderStorageStatus::Invalid,false};}
struct Report {Device *device=nullptr;AgxCpuFailureSink sink=nullptr;void *context=nullptr;AgxCpuProviderResult value=result(P::Invalid);};
/* All ref changes happen under the provider mutex; final deletion is deferred. */
struct Garbage {Device *devices=nullptr;Root *roots=nullptr;Lease *leases=nullptr;Operation *operations=nullptr;DeviceScope *scopes=nullptr;Ticket *tickets=nullptr;
 void drop(Device*d){if(!--d->refs){d->next=devices;devices=d;}}
 void drop(Root*r){if(!--r->refs){drop(r->device);r->next=roots;roots=r;}}
 ~Garbage(){while(tickets){auto*n=tickets->next;delete tickets;tickets=n;}while(scopes){auto*n=scopes->next;delete scopes;scopes=n;}
  while(operations){auto*n=operations->next;delete operations;operations=n;}while(leases){auto*n=leases->next;delete leases;leases=n;}
  while(roots){auto*n=roots->next;std::free(roots->bytes);delete roots;roots=n;}while(devices){auto*n=devices->next;delete devices;devices=n;}}
};
}
struct AgxCpuProvider {std::mutex mutex;Device *devices=nullptr;Root *roots=nullptr;Lease *leases=nullptr;
 Operation *operations=nullptr;DeviceScope *scopes=nullptr;Ticket *tickets=nullptr;};
namespace {
using Lock=std::lock_guard<std::mutex>;
Device *device(AgxCpuProvider*p,AgxCpuDevice h,bool live){for(auto*d=p->devices;d;d=d->next)if(d->id==h.value&&(!live||!d->retired))return d;return nullptr;}
Root **find(AgxCpuProvider*p,AgxCpuRoot h){Root**r=&p->roots;while(*r&&(*r)->id!=h.value)r=&(*r)->next;return r;}
Lease **find(AgxCpuProvider*p,AgxCpuInputLease h){Lease**r=&p->leases;while(*r&&(*r)->id!=h.value)r=&(*r)->next;return r;}
Operation **find(AgxCpuProvider*p,AgxCpuOperation h){Operation**r=&p->operations;while(*r&&(*r)->id!=h.value)r=&(*r)->next;return r;}
DeviceScope **find(AgxCpuProvider*p,AgxCpuDeviceScope h){DeviceScope**r=&p->scopes;while(*r&&(*r)->id!=h.value)r=&(*r)->next;return r;}
Ticket **find(AgxCpuProvider*p,AgxCpuQuiesceTicket h){Ticket**r=&p->tickets;while(*r&&(*r)->id!=h.value)r=&(*r)->next;return r;}
void retire(AgxCpuProvider*p,Device*d,Garbage&g){d->retired=true;Root**r=&p->roots;
 while(*r){if((*r)->device==d){Root*old=*r;*r=old->next;g.drop(old);}else r=&(*r)->next;}}
Report report(Device*d,AgxCpuProviderResult value){Report r;
 if(d&&d->sink&&!d->sink_closed&&value.status!=P::Ok&&room(d->refs)&&room(d->sinks)){
  ++d->refs;++d->sinks;r={d,d->sink,d->context,value};}return r;}
void dispatch(AgxCpuProvider*p,Report r){if(!r.sink)return;
 {Active active(p,r.device,Mode::Reporting);r.sink(r.context,{r.device->id},r.value);}
 Garbage g;{Lock lock(p->mutex);--r.device->sinks;g.drop(r.device);}}
}
AgxCpuProvider *AgxCpuProviderCreate(){return new(std::nothrow)AgxCpuProvider{};}
P AgxCpuProviderDestroy(AgxCpuProvider **pointer){
 if(!pointer||!*pointer)return P::Invalid;auto*p=*pointer;if(reentrant(p))return P::Reentrant;
 Garbage g;{Lock lock(p->mutex);if(p->leases||p->operations||p->scopes||p->tickets)return P::Busy;
  for(auto*d=p->devices;d;d=d->next)if(d->preparing||d->sinks)return P::Busy;
  while(p->roots){auto*r=p->roots;p->roots=r->next;g.drop(r);}while(p->devices){auto*d=p->devices;p->devices=d->next;g.drop(d);}}
 delete p;*pointer=nullptr;return P::Ok;
}
P AgxCpuRegisterDevice(AgxCpuProvider*p,D3D12DDI_HDEVICE h,AgxCpuFailureSink sink,void*context,AgxCpuDevice*out){
 if(!out)return P::Invalid;*out={};if(!p||!h.pDrvPrivate)return P::Invalid;if(reentrant(p))return P::Reentrant;
 auto*d=new(std::nothrow)Device{};if(!d)return P::ProjectOOM;P status=P::Ok;
 {Lock lock(p->mutex);for(auto*x=p->devices;x;x=x->next)if(!x->retired&&x->handle.pDrvPrivate==h.pDrvPrivate){status=P::Invalid;break;}
  if(status==P::Ok){const auto n=id();if(!n)status=P::Exhausted;else{d->id=n;d->handle=h;d->sink=sink;d->context=context;d->next=p->devices;p->devices=d;*out={n};}}}
 if(status!=P::Ok)delete d;return status;
}
P AgxCpuRegisterRoot(AgxCpuProvider*p,AgxCpuDevice dh,D3D12DDI_HROOTSIGNATURE h,AgxShaderTrustedSpan bytes,AgxCpuRoot*out){
 if(!out)return P::Invalid;*out={};if(!p||!h.pDrvPrivate||(bytes.bytes&&!bytes.data))return P::Invalid;if(reentrant(p))return P::Reentrant;
 Device*d;{Lock lock(p->mutex);d=device(p,dh,true);if(!d)return P::NotLiveDevice;if(!room(d->refs)||!room(d->preparing))return P::Exhausted;++d->refs;++d->preparing;}
 Root*r=new(std::nothrow)Root{};P status=r?P::Ok:P::ProjectOOM;
 if(r&&bytes.bytes){r->bytes=static_cast<unsigned char*>(std::malloc(bytes.bytes));if(!r->bytes)status=P::ProjectOOM;else std::memcpy(r->bytes,bytes.data,bytes.bytes);}
 Garbage g;{Lock lock(p->mutex);
  if(status==P::Ok&&d->retired)status=P::NotLiveDevice;
  if(status==P::Ok)for(auto*x=p->roots;x;x=x->next)if(x->handle.pDrvPrivate==h.pDrvPrivate){status=P::Invalid;break;}
  if(status==P::Ok){const auto n=id();if(!n)status=P::Exhausted;else{r->id=n;r->device=d;r->handle=h;r->count=bytes.bytes;r->refs=1;r->next=p->roots;p->roots=r;*out={n};}}
  --d->preparing;if(status!=P::Ok)g.drop(d); /* success transfers the pin to Root */
 }
 if(status!=P::Ok&&r){std::free(r->bytes);delete r;}return status;
}
P AgxCpuRetireRoot(AgxCpuProvider*p,AgxCpuDevice dh,AgxCpuRoot rh){
 if(!p)return P::Invalid;if(reentrant(p))return P::Reentrant;Garbage g;{Lock lock(p->mutex);auto*d=device(p,dh,true);if(!d)return P::NotLiveDevice;
  Root**entry=find(p,rh);auto*r=*entry;if(!r)return P::NotLiveRoot;if(r->device!=d)return P::WrongDevice;*entry=r->next;g.drop(r);}return P::Ok;
}
P AgxCpuRetireDevice(AgxCpuProvider*p,AgxCpuDevice dh){
 if(!p)return P::Invalid;if(reentrant(p))return P::Reentrant;Garbage g;{Lock lock(p->mutex);auto*d=device(p,dh,true);if(!d)return P::NotLiveDevice;retire(p,d,g);}
 return P::Ok; /* retained record supports explicit quiescence; sink gate OPEN */
}
AgxCpuProviderResult AgxCpuAcquireInput(AgxCpuProvider*p,AgxCpuDevice dh,AgxCpuRoot rh,const D3D12DDIARG_CREATE_SHADER_0010*args,
 uint32_t stage,AgxShaderTrustedSpan program,AgxShaderTrustedSpan si,AgxShaderTrustedSpan so,AgxCpuInputLease*out){
 if(!out)return result(P::Invalid);*out={};if(!p)return result(P::Invalid);if(reentrant(p))return result(P::Reentrant);
 Device*d=nullptr;Root*r=nullptr;Report event;auto value=result(P::Ok);
 {Lock lock(p->mutex);d=device(p,dh,true);if(!d)return result(P::NotLiveDevice);r=*find(p,rh);
  if(!r)value=result(P::NotLiveRoot);else if(r->device!=d)value=result(P::WrongDevice);
  else if(!args||args->hRootSignature.pDrvPrivate!=r->handle.pDrvPrivate||program.data!=args->pShaderCode||!program.data||!program.bytes)value=result(P::Invalid);
  else if(!room(d->refs)||!room(r->refs)||!room(d->preparing)||!room(d->leases))value=result(P::Exhausted);
  if(value.status!=P::Ok)event=report(d,value);else{++d->refs;++d->preparing;++r->refs;}}
 if(value.status!=P::Ok){dispatch(p,event);return value;}
 auto*l=new(std::nothrow)Lease{};
 if(l){l->device=d;l->root=r;l->stage=stage;l->args=*args;if(args->IOSignatures.Standard){l->io=*args->IOSignatures.Standard;l->args.IOSignatures.Standard=&l->io;}l->input={program,si,so,{r->bytes,r->count}};}
 Garbage g;{Lock lock(p->mutex);
  if(!l)value=result(P::ProjectOOM);else if(d->retired)value=result(P::NotLiveDevice);else if(*find(p,rh)!=r)value=result(P::NotLiveRoot);else if(!room(d->leases))value=result(P::Exhausted);
  else{const auto n=id();if(!n)value=result(P::Exhausted);else{l->id=n;l->next=p->leases;p->leases=l;++d->leases;*out={n};}}
  --d->preparing;if(value.status!=P::Ok){event=report(d,value);g.drop(r);g.drop(d);}}
 if(value.status!=P::Ok)delete l;dispatch(p,event);return value;
}
P AgxCpuReleaseInput(AgxCpuProvider*p,AgxCpuInputLease h){
 if(!p)return P::Invalid;if(reentrant(p))return P::Reentrant;Garbage g;{Lock lock(p->mutex);auto**entry=find(p,h);auto*l=*entry;if(!l)return P::Invalid;if(l->operations)return P::Busy;
  *entry=l->next;--l->device->leases;g.drop(l->root);g.drop(l->device);l->next=g.leases;g.leases=l;}return P::Ok;
}
P AgxCpuLeaseRootBytes(AgxCpuProvider*p,AgxCpuInputLease h,AgxShaderTrustedSpan*out){
 if(!out)return P::Invalid;*out={};if(!p)return P::Invalid;Lock lock(p->mutex);auto*l=*find(p,h);if(!l)return P::Invalid;*out={l->root->bytes,l->root->count};return P::Ok;
}
AgxCpuProviderResult AgxCpuBeginOperation(AgxCpuProvider*p,AgxCpuInputLease h,AgxCpuOperation*out){
 if(!out)return result(P::Invalid);*out={};if(!p)return result(P::Invalid);if(reentrant(p))return result(P::Reentrant);
 auto*op=new(std::nothrow)Operation{};Report event;auto value=result(P::Ok);
 {Lock lock(p->mutex);auto*l=*find(p,h);if(!l)value=result(P::Invalid);
  else if(l->device->retired)value=result(P::RetiredDevice);else if(!op)value=result(P::ProjectOOM);
  else if(!room(l->operations)||!room(l->device->operations)||!room(l->device->refs)||!room(l->root->refs))value=result(P::Exhausted);
  else{const auto n=id();if(!n)value=result(P::Exhausted);else{
    op->id=n;op->lease=l;op->device=l->device;op->root=l->root;op->stage=l->stage;op->args=l->args;op->io=l->io;
    if(op->args.IOSignatures.Standard)op->args.IOSignatures.Standard=&op->io;op->input=l->input;
    ++l->operations;++op->device->operations;++op->device->refs;++op->root->refs;op->next=p->operations;p->operations=op;*out={n};}}
  if(value.status!=P::Ok&&l)event=report(l->device,value);}
 if(value.status!=P::Ok)delete op;dispatch(p,event);return value;
}
namespace {
Operation *claim(AgxCpuProvider*p,AgxCpuOperation h){Lock lock(p->mutex);auto*op=*find(p,h);if(!op||op->state!=State::Ready)return nullptr;op->state=State::Running;return op;}
AgxCpuProviderResult complete(AgxCpuProvider*p,Operation*op,AgxShaderStorageStatus status){
 auto value=AgxCpuProviderResult{status==AgxShaderStorageStatus::Ok?P::Ok:P::CoreFailure,status,true};Report event;
 {Lock lock(p->mutex);if(value.status!=P::Ok)event=report(op->device,value);}
 dispatch(p,event);return value;
}
void finish(AgxCpuProvider*p,Operation*op){Lock lock(p->mutex);op->state=State::Finished;}
}
AgxCpuProviderResult AgxCpuCalculatePinned(AgxCpuProvider*p,AgxCpuOperation h,AgxShaderOwnedLayout*out){
 if(!out)return result(P::Invalid);std::memset(out,0,sizeof(*out));if(!p)return result(P::Invalid);if(reentrant(p))return result(P::Reentrant);auto*op=claim(p,h);if(!op)return result(P::Invalid);
 AgxShaderStorageStatus status;{Active active(p,op->device,Mode::Core);status=AgxShaderOwnedCalculate(op->device->handle,&op->args,op->stage,&op->input,out);}auto value=complete(p,op,status);finish(p,op);return value;
}
AgxCpuProviderResult AgxCpuCopyPinned(AgxCpuProvider*p,AgxCpuOperation h,void*storage,SIZE_T capacity,AgxShaderOwned**out){
 if(!out)return result(P::Invalid);*out=nullptr;if(!p)return result(P::Invalid);if(reentrant(p))return result(P::Reentrant);auto*op=claim(p,h);if(!op)return result(P::Invalid);
 AgxShaderOwned*pending=nullptr;AgxShaderStorageStatus status;{Active active(p,op->device,Mode::Core);status=AgxShaderOwnedCreate(op->device->handle,&op->args,op->stage,&op->input,storage,capacity,&pending);}
 auto value=complete(p,op,status);if(value.status==P::Ok)*out=pending;finish(p,op);return value;
}
P AgxCpuEndOperation(AgxCpuProvider*p,AgxCpuOperation*h){
 if(!p||!h)return P::Invalid;if(reentrant(p))return P::Reentrant;Garbage g;{Lock lock(p->mutex);auto**entry=find(p,*h);auto*op=*entry;if(!op)return P::Invalid;if(op->state==State::Running)return P::Busy;
  *entry=op->next;--op->lease->operations;--op->device->operations;g.drop(op->root);g.drop(op->device);op->next=g.operations;g.operations=op;*h={};}return P::Ok;
}
P AgxCpuOperationRootBytes(AgxCpuProvider*p,AgxCpuOperation h,AgxShaderTrustedSpan*out){
 if(!out)return P::Invalid;*out={};if(!p)return P::Invalid;Lock lock(p->mutex);auto*op=*find(p,h);if(!op)return P::Invalid;*out={op->root->bytes,op->root->count};return P::Ok;
}
AgxCpuProviderResult AgxCpuCalculate(AgxCpuProvider*p,AgxCpuInputLease h,AgxShaderOwnedLayout*out){
 if(!out)return result(P::Invalid);std::memset(out,0,sizeof(*out));AgxCpuOperation op{};auto value=AgxCpuBeginOperation(p,h,&op);if(value.status!=P::Ok)return value;
 value=AgxCpuCalculatePinned(p,op,out);(void)AgxCpuEndOperation(p,&op);return value;
}
AgxCpuProviderResult AgxCpuCopyOwned(AgxCpuProvider*p,AgxCpuInputLease h,void*storage,SIZE_T capacity,AgxShaderOwned**out){
 if(!out)return result(P::Invalid);*out=nullptr;AgxCpuOperation op{};auto value=AgxCpuBeginOperation(p,h,&op);if(value.status!=P::Ok)return value;
 value=AgxCpuCopyPinned(p,op,storage,capacity,out);(void)AgxCpuEndOperation(p,&op);return value;
}
const void *AgxCpuResolveDeviceIdentity(AgxCpuProvider*p,D3D12DDI_HDEVICE h){
 if(!p)return nullptr;Lock lock(p->mutex);for(auto*d=p->devices;d;d=d->next)if(!d->retired&&d->handle.pDrvPrivate==h.pDrvPrivate)return d;return nullptr;
}
const void *AgxCpuResolvePinnedDeviceIdentity(AgxCpuProvider*p,D3D12DDI_HDEVICE h){
 if(!p)return nullptr;for(Scope*s=active_scope;s;s=s->previous)if(s->provider==p&&s->device->handle.pDrvPrivate==h.pDrvPrivate)return s->device;return nullptr;
}
P AgxCpuBeginDeviceScope(AgxCpuProvider*p,AgxCpuDevice dh,AgxCpuDeviceScope*out){
 if(!out)return P::Invalid;*out={};if(!p)return P::Invalid;if(reentrant(p))return P::Reentrant;auto*s=new(std::nothrow)DeviceScope{};if(!s)return P::ProjectOOM;P status=P::Ok;
 {Lock lock(p->mutex);auto*d=device(p,dh,true);if(!d)status=P::NotLiveDevice;else if(!room(d->refs)||!room(d->scopes))status=P::Exhausted;
  else{const auto n=id();if(!n)status=P::Exhausted;else{s->id=n;s->device=d;s->thread=std::this_thread::get_id();s->scope={p,d,Mode::Device,active_scope};++d->refs;++d->scopes;s->next=p->scopes;p->scopes=s;*out={n};}}}
 if(status!=P::Ok)delete s;else active_scope=&s->scope;return status;
}
P AgxCpuEndDeviceScope(AgxCpuProvider*p,AgxCpuDeviceScope*h){
 if(!p||!h)return P::Invalid;if(reentrant(p))return P::Reentrant;Garbage g;{Lock lock(p->mutex);auto**entry=find(p,*h);auto*s=*entry;if(!s)return P::Invalid;
  if(s->thread!=std::this_thread::get_id()||active_scope!=&s->scope)return P::Invalid;active_scope=s->scope.previous;
  *entry=s->next;--s->device->scopes;g.drop(s->device);s->next=g.scopes;g.scopes=s;*h={};}return P::Ok;
}
P AgxCpuBeginQuiesce(AgxCpuProvider*p,AgxCpuDevice dh,AgxCpuQuiesceTicket*out){
 if(!out)return P::Invalid;*out={};if(!p)return P::Invalid;if(reentrant(p))return P::Reentrant;auto*t=new(std::nothrow)Ticket{};if(!t)return P::ProjectOOM;P status=P::Ok;Garbage g;
 {Lock lock(p->mutex);auto*d=device(p,dh,false);if(!d)status=P::NotLiveDevice;else if(d->quiescing)status=P::Busy;else if(!room(d->refs))status=P::Exhausted;
  else{const auto n=id();if(!n)status=P::Exhausted;else{retire(p,d,g);d->sink_closed=d->quiescing=true;++d->refs;t->id=n;t->device=d;t->next=p->tickets;p->tickets=t;*out={n};}}}
 if(status!=P::Ok)delete t;return status;
}
P AgxCpuTryQuiesce(AgxCpuProvider*p,AgxCpuQuiesceTicket*h){
 if(!p||!h)return P::Invalid;if(reentrant(p))return P::Reentrant;Garbage g;{Lock lock(p->mutex);auto**entry=find(p,*h);auto*t=*entry;if(!t)return P::Invalid;auto*d=t->device;
  if(d->operations||d->scopes||d->sinks||d->preparing)return P::Busy;
  Device**node=&p->devices;while(*node&&*node!=d)node=&(*node)->next;if(*node!=d)return P::Invalid;*node=d->next;g.drop(d);
  *entry=t->next;g.drop(t->device);t->next=g.tickets;g.tickets=t;*h={};}return P::Ok;
}
