/* Original native-WDK actual-owner function fixture. MIT.
 * owner_selected.inc is exact full actual prefix/types+functions, no replicas. */
#include "owner_selected.inc"
#include <cstdio>
#include <cstdlib>
#include <type_traits>
extern "C" struct pipe_context *owned_owner_callbacks_create(struct nir_shader *);
extern "C" void owned_owner_callbacks_fail(struct pipe_context *,unsigned);
extern "C" unsigned owned_owner_callbacks_calls(struct pipe_context *);
extern "C" unsigned owned_owner_callbacks_all_calls(struct pipe_context *);
extern "C" unsigned owned_owner_callbacks_graphics_calls(struct pipe_context *);
extern "C" unsigned owned_owner_callbacks_deletes(struct pipe_context *);
extern "C" unsigned owned_owner_callbacks_frees(struct pipe_context *);
extern "C" void owned_owner_callbacks_finish(struct pipe_context *);
extern "C" int owned_owner_dimensions(struct nir_shader *,uint8_t *,uint8_t *);
extern "C" int owned_owner_query_unknown_control(struct nir_shader *);
static unsigned checks;
static void need(bool p,const char *why){++checks;if(!p){std::fprintf(stderr,"OWNER_PREPARED_FAIL %s\n",why);std::abort();}}
static bool maps(const dxbc_binding_map &a,const dxbc_binding_map &b){
 if(a.active!=b.active||a.count!=b.count)return false;
 for(unsigned i=0;i<DXBC_BINDING_MAP_MAX;i++){const auto &x=a.bindings[i];const auto &y=b.bindings[i];if(x.kind!=y.kind||x.id!=y.id||x.space!=y.space||x.reg!=y.reg||x.slot!=y.slot)return false;}return true;
}
extern "C" void owned_compute_owner_fixture(const AgxOwnedNir *prepared,struct nir_shader *nir,
 const dxbc_binding_map *map,const uint32_t *wg,unsigned token_stage){
 static_assert(std::is_same<decltype(&AgxD3d12OwnerCreateComputePrepared),HRESULT (*)(AGX_D3D12_OWNER *,const AgxOwnedNir *,AGX_D3D12_COMPUTE *)>::value,"actual declared owner API");
 need(AgxOwnedNirStage(NULL)==AGX_OWNED_NIR_STAGE_UNKNOWN,"opaque NULL stage refuses");
 need(owned_owner_query_unknown_control(nir),"direct C actual unknown-stage control");
 auto *owner=new AGX_D3D12_OWNER{};owner->Context=owned_owner_callbacks_create(nir);owner->Screen=owner->Context->screen;
 AGX_D3D12_COMPUTE out={};
 if(token_stage!=5){
  need(AgxD3d12OwnerCreateComputePrepared(owner,prepared,&out)==E_NOTIMPL,"actual VS/FS rejected by actual stage before callback");
  need(compute_prepared_empty(&out)&&owned_owner_callbacks_all_calls(owner->Context)==0,"VS/FS no compute/graphics create/delete callback or publication");
 }else{
  need(AgxOwnedNirStage(prepared)==AGX_OWNED_NIR_STAGE_COMPUTE,"actual opaque compute query");
  need(AgxD3d12OwnerCreateComputePrepared(owner,prepared,&out)==S_OK&&out.State,"actual owner publishes success");
  need(maps(out.Bindings,*map),"actual owner copied exact binding map");
  for(unsigned i=0;i<3;i++)need(out.Block[i]==wg[i],"actual owner copied block");
  uint8_t srv[DXBC_RESOURCE_SLOTS],uav[DXBC_UAV_DIMENSION_SLOTS];need(owned_owner_dimensions(nir,srv,uav),"actual metadata query");
  need(!memcmp(srv,out.SrvDimensions,sizeof(srv))&&!memcmp(uav,out.UavDimensions,sizeof(uav)),"actual owner copied dimension metadata");
  auto saved=out;need(AgxD3d12OwnerCreateComputePrepared(owner,prepared,&out)==E_INVALIDARG,"nonempty live result rejected");
  need(out.State==saved.State&&maps(out.Bindings,saved.Bindings)&&out.Block[0]==saved.Block[0]&&out.Block[1]==saved.Block[1]&&out.Block[2]==saved.Block[2]&&!memcmp(out.SrvDimensions,saved.SrvDimensions,sizeof(out.SrvDimensions))&&!memcmp(out.UavDimensions,saved.UavDimensions,sizeof(out.UavDimensions)),"nonempty complete metadata unchanged");
  need(owned_owner_callbacks_calls(owner->Context)==1,"nonempty callback not invoked");
  AgxD3d12OwnerDestroyCompute(owner,&out);need(compute_prepared_empty(&out)&&owned_owner_callbacks_deletes(owner->Context)==1,"actual existing destroy clears/deletes once");
  AgxD3d12OwnerDestroyCompute(owner,&out);need(owned_owner_callbacks_deletes(owner->Context)==1,"actual empty destroy no double delete");
  owned_owner_callbacks_fail(owner->Context,1);
  need(AgxD3d12OwnerCreateComputePrepared(owner,prepared,&out)==E_FAIL&&compute_prepared_empty(&out),"ordinary NULL callback failure empty");
  owned_owner_callbacks_fail(owner->Context,0);
  need(AgxD3d12OwnerCreateComputePrepared(owner,prepared,&out)==S_OK,"valid owner call after ordinary callback failure");AgxD3d12OwnerDestroyCompute(owner,&out);
  need(owned_owner_callbacks_calls(owner->Context)==3&&owned_owner_callbacks_frees(owner->Context)==3&&owned_owner_callbacks_deletes(owner->Context)==2,"all actual clones consumed, returnedstates deleted exactly");
 }
 need(AgxD3d12OwnerCreateComputePrepared(owner,NULL,&out)==E_INVALIDARG&&compute_prepared_empty(&out),"NULL prepared empty refusal");
 auto *screen=owner->Screen;owner->Screen=NULL;unsigned calls=owned_owner_callbacks_calls(owner->Context);
 need(AgxD3d12OwnerCreateComputePrepared(owner,prepared,&out)==E_INVALIDARG&&compute_prepared_empty(&out)&&owned_owner_callbacks_calls(owner->Context)==calls,"missing screen before callback");owner->Screen=screen;
 out.Block[0]=1;need(AgxD3d12OwnerCreateComputePrepared(owner,prepared,&out)==E_INVALIDARG&&out.Block[0]==1&&owned_owner_callbacks_calls(owner->Context)==calls,"nonempty metadata-only output refused unchanged");out.Block[0]=0;
 need(owned_owner_callbacks_graphics_calls(owner->Context)==0,"available graphics callbacks never invoked by compute-only entry");
 owned_owner_callbacks_finish(owner->Context);delete owner;
 std::printf("C15_ACTUAL_OWNER_PREPARED_PASS token_stage=%u checks=%u NATIVE_WDK_FIXTURE_CALLBACKS_NO_BO\n",token_stage,checks);
}
