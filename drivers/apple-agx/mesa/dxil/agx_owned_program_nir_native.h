/* Original internal C frontend for unlinked owned PROGRAM composition. SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../dxbc/dxbc_bindings.h"
#ifdef __cplusplus
extern "C" {
#endif
struct nir_shader;
enum agx_owned_nir_status {AGX_OWNED_NIR_OK,AGX_OWNED_NIR_INVALID,AGX_OWNED_NIR_UNSUPPORTED,AGX_OWNED_NIR_CONVERSION_ERROR,AGX_OWNED_NIR_FRONTEND_ERROR,AGX_OWNED_NIR_PROJECT_OOM};
struct agx_owned_program_nir_native {struct nir_shader *nir;struct dxbc_binding_map map;uint32_t workgroup[3];};
/* Internal caller supplies stable, readable, disjoint input/output, initially
 * empty out. kind1=legacy PROGRAM,2=DXIL PROGRAM; token stages0/1/5 only.
 * Success owns NIR plus one GLSL-type singleton reference; failure is all-zero.
 * DXIL profile is compute6.0/static singleton CBV/rawSRV/rawUAV/non-base Dispatch.
 * No BO/backend, operation-admission completeness or upstream-OOM guarantee. */
enum agx_owned_nir_status agx_owned_program_nir_native_prepare(unsigned kind,unsigned token_stage,const void *program,size_t bytes,const char *entry,size_t entry_bytes,const uint32_t limits[4],struct agx_owned_program_nir_native *out);
void agx_owned_program_nir_native_free(struct agx_owned_program_nir_native *);
#ifdef __cplusplus
}
#endif
