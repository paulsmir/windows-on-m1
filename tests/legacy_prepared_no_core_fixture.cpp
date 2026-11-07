/* Original true legacy constructor/link fixture. NO core/provider/testresolver
 * objects or definitions. Actual owner callback fixture separately labelled. MIT */
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
#include "agx_owned_program_nir.h"
#include <vector>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
extern "C" unsigned legacy_prepared_refs(void);
extern "C" unsigned legacy_prepared_ref_acquired(void);
extern "C" unsigned legacy_prepared_ref_released(void);
extern "C" int legacy_prepared_nir_equal(nir_shader *,nir_shader *);
#ifdef _WIN32
extern "C" void owned_compute_owner_fixture(const AgxOwnedNir *,nir_shader *,const dxbc_binding_map *,const uint32_t *,unsigned);
#endif
static unsigned checks;
static void need(bool p,const char *why){++checks;if(!p){std::fprintf(stderr,"LEGACY_FACTORY_FAIL %s\n",why);std::abort();}}
static std::vector<unsigned char> read(const char *path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static bool same_map(const dxbc_binding_map &a,const dxbc_binding_map &b){
 if(a.active!=b.active||a.count!=b.count)return false;
 for(unsigned i=0;i<DXBC_BINDING_MAP_MAX;i++){const auto &x=a.bindings[i],&y=b.bindings[i];if(x.kind!=y.kind||x.id!=y.id||x.space!=y.space||x.reg!=y.reg||x.slot!=y.slot)return false;}return true;
}
int main(int argc,char **argv){
 need(argc==5,"actual legacyCS50/CS51static/SM6PROGRAM/VS40PROGRAM");
 static_assert(noexcept(AgxOwnedNirPrepareLegacyCompute(nullptr,0,nullptr,nullptr)),"factory noexcept");
 const uint32_t limits[4]={14,128,8,16};
 need(legacy_prepared_refs()==0,"observed frontend reference baseline");
 for(unsigned i=0;i<2;i++){
  auto bytes=read(argv[i+1]);need(bytes.size()>=8,"actual sourcePROGRAM");
  AgxOwnedNir *bad=nullptr;
  need(AgxOwnedNirPrepareLegacyCompute(bytes.data(),bytes.size()-1,limits,&bad)!=AGX_OWNED_NIR_OK&&!bad,"short explicitextent refusal");
  auto malformed=bytes;std::memset(malformed.data()+4,0,4);
  need(AgxOwnedNirPrepareLegacyCompute(malformed.data(),malformed.size(),limits,&bad)!=AGX_OWNED_NIR_OK&&!bad,"malformed length refusal");
  need(legacy_prepared_refs()==0,"ordinary refusals balance real GLSLrefs");
  agx_owned_program_nir_native reference={};
  need(agx_owned_program_nir_native_prepare(1,5,bytes.data(),bytes.size(),nullptr,0,limits,&reference)==AGX_OWNED_NIR_OK,"existing real legacy frontend reference");
  AgxOwnedNir *value=nullptr;need(AgxOwnedNirPrepareLegacyCompute(bytes.data(),bytes.size(),limits,&value)==AGX_OWNED_NIR_OK&&value,"real opaque legacy construction");
  need(legacy_prepared_refs()==2,"two actual owned frontendreferences");
  auto *saved=value;need(AgxOwnedNirPrepareLegacyCompute(bytes.data(),bytes.size(),limits,&value)==AGX_OWNED_NIR_INVALID&&value==saved,"nonempty output refuses unchanged");
  std::memset(bytes.data(),0xcc,bytes.size());bytes.clear();bytes.shrink_to_fit();
  need(AgxOwnedNirStage(value)==AGX_OWNED_NIR_STAGE_COMPUTE,"actual opaque compute stage");
  need(AgxOwnedNirRoot(value).bytes==0,"root UNSPECIFIED no compatibilityidentity");
  need(same_map(*AgxOwnedNirBindings(value),reference.map),"actual legacy map copiedindependently");
  const auto *wg=AgxOwnedNirWorkgroup(value);for(unsigned d=0;d<3;d++)need(wg[d]&&wg[d]==reference.workgroup[d],"workgroup parity");
  need(legacy_prepared_nir_equal(AgxOwnedNirShader(value),reference.nir),"actual serialized NIR parity after inputreleased");
  if(i==1){const auto *map=AgxOwnedNirBindings(value);need(map->active&&map->count==3,"realSM51namespace declarations");const auto *a=dxbc_binding_find(map,DXBC_BINDING_UAV,1,0),*b=dxbc_binding_find(map,DXBC_BINDING_UAV,2,0),*c=dxbc_binding_find(map,DXBC_BINDING_CBV,3,0);need(a&&b&&c&&a->slot!=b->slot&&c->slot==0,"realSM51distinct u0spaces/mapCBV0");}
#ifdef _WIN32
  owned_compute_owner_fixture(value,AgxOwnedNirShader(value),AgxOwnedNirBindings(value),wg,5);
  need(legacy_prepared_nir_equal(AgxOwnedNirShader(value),reference.nir),"owner callback leaves original intact");
#endif
  agx_owned_program_nir_native_free(&reference);need(legacy_prepared_refs()==1,"directreference freed, factory stillalive");
  AgxOwnedNirDestroy(&value);need(!value&&legacy_prepared_refs()==0,"matching destructor clears and balances realref");AgxOwnedNirDestroy(&value);
 }
 for(unsigned i=3;i<5;i++){auto rejected=read(argv[i]);AgxOwnedNir *value=nullptr;need(AgxOwnedNirPrepareLegacyCompute(rejected.data(),rejected.size(),limits,&value)!=AGX_OWNED_NIR_OK&&!value,"forcedlegacycompute refusesSM6andVS");need(legacy_prepared_refs()==0,"profile refusal referencebalance");}
 need(legacy_prepared_ref_acquired()==legacy_prepared_ref_released(),"all observed projectfrontend realreferences balanced");
 std::printf("C15_LEGACY_PREPARED_NO_CORE_PASS checks=%u acquired=%u released=%u ROOT_UNSPECIFIED NO_PROVIDER_NO_CORE_NO_GPU\n",checks,legacy_prepared_ref_acquired(),legacy_prepared_ref_released());
}
