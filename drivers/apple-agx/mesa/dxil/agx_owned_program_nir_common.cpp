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
#include "agx_owned_program_nir_private.h"
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

agx_owned_nir_status AgxOwnedNirPrepareLegacyCompute(const void *program,size_t bytes,
 const uint32_t limits[4],AgxOwnedNir **out) noexcept {
 if(!out||*out)return AGX_OWNED_NIR_INVALID;
 if(!program||!limits||bytes<8)return AGX_OWNED_NIR_INVALID;
 try{
  std::unique_ptr<AgxOwnedNir> value(new AgxOwnedNir);
  const auto status=agx_owned_program_nir_native_prepare(1,5,program,bytes,nullptr,0,limits,&value->native);
  if(status!=AGX_OWNED_NIR_OK)return status;
  /* Empty root is UNSPECIFIED translation-only, NOT an empty-root key. */
  *out=value.release();return AGX_OWNED_NIR_OK;
 }catch(const std::bad_alloc &){return AGX_OWNED_NIR_PROJECT_OOM;}
 catch(...){return AGX_OWNED_NIR_FRONTEND_ERROR;}
}
