/* Original deterministic PSO-publication gate. SPDX-License-Identifier: MIT.
 * Only external owner constructors/destructors and Windows locks are stand-ins.
 * CompileCompute/CompileGraphics/private PSO definition are extracted verbatim.
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <vector>
#include <initializer_list>
using UINT=unsigned; using UINT8=uint8_t; using BOOL=int; using INT=int;
using FLOAT=float; using HRESULT=int32_t; using DXGI_FORMAT=unsigned;
using UINT64=uint64_t; using ULONG=unsigned; using LONG=int;
#define APIENTRY
#define VOID void
#define APPLE_AGX_D3D12_NATIVE_OWNER 1
#define D3D12DDI_SIMULTANEOUS_RENDER_TARGET_COUNT 8
#define AGX_D3D12_MAX_ELEMENTS 16
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
static constexpr HRESULT S_OK=0,S_FALSE=1,E_FAIL=static_cast<HRESULT>(0x80004005u);
struct Handle {void *pDrvPrivate;};
using D3D12DDI_HDEVICE=Handle;using D3D12DDI_HSHADER=Handle;
using D3D12DDI_HPIPELINESTATE=Handle;using D3D12DDI_HROOTSIGNATURE=Handle;
using D3D12DDI_HBLENDSTATE=Handle;using D3D12DDI_HRASTERIZERSTATE=Handle;
using D3D12DDI_HDEPTHSTENCILSTATE=Handle;using D3D12DDI_HELEMENTLAYOUT=Handle;
struct DXGI_SAMPLE_DESC {UINT Count,Quality;};
#include "dxbc_bindings.h"
#include "dxbc_resources.h"
/* PINNED_WDK_TYPES */
/* ACTUAL_OWNER_TYPES */

struct Schedule {
 std::mutex mutex;std::condition_variable changed;
 bool entered[3]={},allow[3]={},attempt[3]={},fail_first=false;
 unsigned calls=0,created=0,destroyed=0;
 struct Token {unsigned owner,part;bool live;};
 Token tokens[3][6]={};
};
static Schedule *schedule;
static thread_local unsigned role;
static void require(bool yes,const char *why){if(!yes){std::fprintf(stderr,"HARNESS_ERROR %s\n",why);std::exit(3);}}
static void observe_lock_attempt(){
 if(!schedule || role!=2)return;
 std::lock_guard<std::mutex> guard(schedule->mutex);
 schedule->attempt[role]=true;schedule->changed.notify_all();
}
// Hook reports B's lock attempt BEFORE blocking. Coordinator can release A even
// after a correct fix serializes B outside the constructor; no two-arrival barrier.
struct SRWLOCK {std::mutex mutex;};
static void InitializeSRWLock(SRWLOCK *){}
static void AcquireSRWLockExclusive(SRWLOCK *lock){observe_lock_attempt();lock->mutex.lock();}
static void ReleaseSRWLockExclusive(SRWLOCK *lock){lock->mutex.unlock();}
static void AcquireSRWLockShared(SRWLOCK *lock){observe_lock_attempt();lock->mutex.lock();}
static void ReleaseSRWLockShared(SRWLOCK *lock){lock->mutex.unlock();}
static void *token(unsigned part){
 auto &t=schedule->tokens[role][part];require(!t.live,"duplicate native token");
 t={role,part,true};++schedule->created;return &t;
}
static void release_token(void *p){
 if(!p)return;
 auto *t=static_cast<Schedule::Token *>(p);require(t->live,"double native destroy");
 t->live=false;++schedule->destroyed;
}
struct AGX_D3D12_OWNER {};
struct AgxD3d12Device {AGX_D3D12_OWNER *owner;};
static AGX_D3D12_OWNER *EnsureOwner(AgxD3d12Device *d){return d->owner;}
static AgxD3d12Device *DeviceOf(D3D12DDI_HDEVICE h){return static_cast<AgxD3d12Device *>(h.pDrvPrivate);}
static HRESULT constructor_gate(){
 std::unique_lock<std::mutex> guard(schedule->mutex);++schedule->calls;
 schedule->entered[role]=true;schedule->changed.notify_all();
 require(schedule->changed.wait_for(guard,std::chrono::seconds(5),[]{return schedule->allow[role];}),"constructor release timeout");
 return schedule->fail_first && role==1 ? E_FAIL:S_OK;
}
static void map(struct dxbc_binding_map *m,unsigned tag){
 std::memset(m,0,sizeof(*m));m->active=1;m->count=1;m->bindings[0]={DXBC_BINDING_UAV,tag,tag+100,0,tag};
}
static HRESULT AgxD3d12OwnerCreateCompute(AGX_D3D12_OWNER *,const UINT *,AGX_D3D12_COMPUTE *out){
 HRESULT hr=constructor_gate();if(FAILED(hr)){*out={};return hr;}
 std::lock_guard<std::mutex> guard(schedule->mutex);
 *out={};out->State=token(0);out->Block[0]=64+role;out->Block[1]=role;out->Block[2]=1;
 map(&out->Bindings,role);out->SrvDimensions[role]=DXBC_SRV_3D;return S_OK;
}
static HRESULT AgxD3d12OwnerCreateGraphics(AGX_D3D12_OWNER *,const AGX_D3D12_GRAPHICS_DESC *desc,AGX_D3D12_GRAPHICS *out){
 require(desc && desc->Vs && desc->Ps,"actual graphics descriptor lost stages");
 HRESULT hr=constructor_gate();if(FAILED(hr)){*out={};return hr;}
 std::lock_guard<std::mutex> guard(schedule->mutex);
 *out={};out->Vs=token(0);out->Fs=token(1);out->Blend=token(2);
 out->DepthStencil=token(3);out->Rasterizer=token(4);out->NumElements=role;
 map(&out->VsBindings,role);map(&out->PsBindings,role);
 out->VsSrvDimensions[role]=DXBC_SRV_3D;out->PsSrvDimensions[role]=DXBC_SRV_CUBE;
 return S_OK;
}
static void AgxD3d12OwnerDestroyCompute(AGX_D3D12_OWNER *,AGX_D3D12_COMPUTE *out){
 std::lock_guard<std::mutex> guard(schedule->mutex);release_token(out->State);*out={};
}
static void AgxD3d12OwnerDestroyGraphics(AGX_D3D12_OWNER *,AGX_D3D12_GRAPHICS *out){
 std::lock_guard<std::mutex> guard(schedule->mutex);
 for(auto *p:{out->Vs,out->Fs,out->Blend,out->DepthStencil,out->Rasterizer,out->Velems})release_token(p);
 *out={};
}
/* ACTUAL_PSO_FUNCTIONS */
static void init(AgxD3d12PipelineState *p,bool graphics){
 p->compiled=S_FALSE;p->elements=0;
 p->cs_dwords=graphics?0:1;p->vs_dwords=graphics?1:0;p->ps_dwords=graphics?1:0;
 // No shader bytes are interpreted by native stand-ins. For graphics, Ps is
 // the legal one-past pointer formed by actual CompileGraphics, never dereferenced.
}
static bool tuple(const AgxD3d12PipelineState *p,bool graphics,unsigned tag){
 if(p->compiled!=S_OK)return false;
 if(graphics){const auto &g=p->graphics;return g.Vs==&schedule->tokens[tag][0] &&
  g.Fs==&schedule->tokens[tag][1] && g.Blend==&schedule->tokens[tag][2] &&
  g.DepthStencil==&schedule->tokens[tag][3] && g.Rasterizer==&schedule->tokens[tag][4] &&
  g.NumElements==tag && g.VsBindings.active && g.VsBindings.count==1 &&
  g.PsBindings.active && g.PsBindings.count==1 &&
  g.VsBindings.bindings[0].space==tag+100 && g.PsBindings.bindings[0].slot==tag &&
  g.VsSrvDimensions[tag]==DXBC_SRV_3D && g.PsSrvDimensions[tag]==DXBC_SRV_CUBE;}
 const auto &c=p->compute;return c.State==&schedule->tokens[tag][0] &&
  c.Block[0]==64+tag && c.Block[1]==tag && c.Block[2]==1 &&
  c.Bindings.active && c.Bindings.count==1 && c.Bindings.bindings[0].id==tag &&
  c.Bindings.bindings[0].space==tag+100 && c.Bindings.bindings[0].slot==tag && c.SrvDimensions[tag]==DXBC_SRV_3D;
}
static void wait_event(bool(*predicate)(),const char *why){
 std::unique_lock<std::mutex> guard(schedule->mutex);
 require(schedule->changed.wait_for(guard,std::chrono::seconds(5),predicate),why);
}
int main(int argc,char **argv){
 require(argc==3,"usage: fixture compute|graphics serial|parallel|failure");
 bool graphics=std::strcmp(argv[1],"graphics")==0;
 bool serial=std::strcmp(argv[2],"serial")==0;
 bool failing=std::strcmp(argv[2],"failure")==0;
 Schedule state;state.fail_first=failing;schedule=&state;
 AGX_D3D12_OWNER owner;AgxD3d12Device device={&owner};AgxD3d12PipelineState pso={};init(&pso,graphics);
 auto compile=[&](){return graphics?CompileGraphics(&device,&pso):CompileCompute(&device,&pso);};
 HRESULT result[3]={};bool tuple_a=false;
 if(serial){
  role=1;state.allow[1]=true;result[1]=compile();tuple_a=tuple(&pso,graphics,1);result[2]=compile();
 }else{
  std::thread a([&](){role=1;result[1]=compile();});
  wait_event([](){return schedule->entered[1];},"A did not reach constructor");
  std::thread b([&](){role=2;result[2]=compile();});
  // Existing code reaches B's constructor. Correct serialization reports B's
  // lock attempt instead. Both events are causal, no sleep/poll/required barrier.
  wait_event([](){return schedule->entered[2] || schedule->attempt[2];},"B reached neither constructor nor lock attempt");
  {std::lock_guard<std::mutex> guard(state.mutex);state.allow[1]=true;state.changed.notify_all();}
  a.join(); // HB: A's publication completes before B is allowed to publish.
  tuple_a=failing ? pso.compiled==E_FAIL:tuple(&pso,graphics,1);
  {std::lock_guard<std::mutex> guard(state.mutex);state.allow[2]=true;state.changed.notify_all();}
  b.join();
 }
 bool values=failing ? result[1]==E_FAIL && result[2]==E_FAIL && pso.compiled==E_FAIL:
                       result[1]==S_OK && result[2]==S_OK && tuple(&pso,graphics,1);
 const unsigned calls=state.calls;
 DestroyPipelineState({&device},{&pso});
 bool no_leak=state.created==state.destroyed;
 // Failure belongs to this PSO only: a distinct fresh PSO still compiles once.
 bool fresh=true;
 if(failing){AgxD3d12PipelineState next={};init(&next,graphics);state.fail_first=false;
  role=1;unsigned before=state.calls;
  auto hr=graphics?CompileGraphics(&device,&next):CompileCompute(&device,&next);
  fresh=hr==S_OK && state.calls==before+1 && tuple(&next,graphics,1);
  DestroyPipelineState({&device},{&next});}
 std::printf("PSO_PUBLICATION %s/%s calls=%u resultA=%08x resultB=%08x tupleA=%u tupleFinal=%u created=%u destroyed=%u noLeak=%u freshPSO=%u\n",argv[1],argv[2],calls,(unsigned)result[1],(unsigned)result[2],tuple_a,values,state.created,state.destroyed,no_leak,fresh);
 return calls==1 && tuple_a && values && no_leak && fresh ? 0:1;
}
