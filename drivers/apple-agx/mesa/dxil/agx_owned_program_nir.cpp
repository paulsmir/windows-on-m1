/* Original bounded owned-object bridge, not DDI registration. SPDX-License-Identifier: MIT */
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
#include <memory>
#include <cstring>
#include <new>
struct AgxOwnedNir {agx_owned_program_nir_native native{};std::vector<unsigned char> root;~AgxOwnedNir(){agx_owned_program_nir_native_free(&native);}};
static bool range(SIZE_T at,SIZE_T bytes,SIZE_T total){return at<=total&&bytes<=total-at;}
agx_owned_nir_status AgxOwnedNirPrepare(const AgxOwnedNirRequest *request,AgxOwnedNir **out) noexcept{
 if(!out)return AGX_OWNED_NIR_INVALID;*out=nullptr;
 if(!request||!request->owned||request->readable_owned_bytes<sizeof(AgxShaderOwned))return AGX_OWNED_NIR_INVALID;
 const auto *owned=request->owned;const auto &l=owned->layout;
 const auto kind=AgxShaderOwnedGetProgramKind(owned);
 if(kind==AgxShaderProgramKind::Unknown||l.total>request->readable_owned_bytes||l.total<sizeof(*owned)||l.program_offset<sizeof(*owned)||!range(l.program_offset,l.program_bytes,l.total)||!range(l.root_offset,l.root_bytes,l.total)||l.root_offset+l.root_bytes!=l.total||l.program_offset+l.program_bytes>l.root_offset)return AGX_OWNED_NIR_INVALID;
 if(owned->flags||(owned->stage!=0&&owned->stage!=1&&owned->stage!=5))return AGX_OWNED_NIR_UNSUPPORTED;
 try{
  std::unique_ptr<AgxOwnedNir> value(new AgxOwnedNir);
  const auto *base=reinterpret_cast<const unsigned char *>(owned);
  value->root.assign(base+l.root_offset,base+l.root_offset+l.root_bytes);
  agx_owned_nir_status rc=agx_owned_program_nir_native_prepare(kind==AgxShaderProgramKind::LegacyDxbc?1u:2u,owned->stage,base+l.program_offset,l.program_bytes,request->entry,request->entry_bytes,request->limits,&value->native);
  if(rc!=AGX_OWNED_NIR_OK)return rc;
  *out=value.release();return AGX_OWNED_NIR_OK;
 }catch(const std::bad_alloc &){return AGX_OWNED_NIR_PROJECT_OOM;}catch(...){return AGX_OWNED_NIR_CONVERSION_ERROR;}
}
void AgxOwnedNirDestroy(AgxOwnedNir **v) noexcept{if(v){delete *v;*v=nullptr;}}
nir_shader *AgxOwnedNirShader(AgxOwnedNir *v) noexcept{return v?v->native.nir:nullptr;}
const dxbc_binding_map *AgxOwnedNirBindings(const AgxOwnedNir *v) noexcept{return v?&v->native.map:nullptr;}
AgxShaderTrustedSpan AgxOwnedNirRoot(const AgxOwnedNir *v) noexcept{return v?AgxShaderTrustedSpan{v->root.data(),v->root.size()}:AgxShaderTrustedSpan{};}
const uint32_t *AgxOwnedNirWorkgroup(const AgxOwnedNir *v) noexcept{return v?v->native.workgroup:nullptr;}

agx_owned_nir_pipe_status AgxOwnedNirCreatePipeState(
 const AgxOwnedNir *value,pipe_context *pipe,
 agx_owned_nir_pipe_state *out) noexcept {
 return agx_owned_nir_create_pipe_state(value ? &value->native : nullptr,pipe,out);
}

agx_owned_nir_stage AgxOwnedNirStage(const AgxOwnedNir *value) noexcept {
 return agx_owned_program_nir_native_stage(value ? &value->native : nullptr);
}
