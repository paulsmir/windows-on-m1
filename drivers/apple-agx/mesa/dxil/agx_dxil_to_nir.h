/* D3D12 C44: DXIL (Shader Model 6.x) -> NIR in the DXBC frontend's
 * conventions (investigation/analysis/D3D12-C44-dxil.md, parity checklist):
 * the same dxbc_binding_map namespace (dense slots per kind in (space,
 * register) order, one slot per register of a range), CBVs as load_ubo
 * slots, SRVs as tex instructions on texture/sampler slots (raw and
 * structured SRVs as R32_UINT texel buffers), typed UAVs as indexed image
 * intrinsics, raw/structured UAVs as SSBO slots with append/consume counters
 * at DXBC_NIR_UAV_COUNTER_SLOT + u, stage I/O at the signature registers.
 * SPDX-License-Identifier: MIT */
#ifndef AGX_DXIL_TO_NIR_H
#define AGX_DXIL_TO_NIR_H

#include <stddef.h>
#include <stdint.h>
#include "../dxbc/dxbc_bindings.h"
#include "compiler/shader_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nir_shader;
struct nir_shader_compiler_options;
struct dxbc_nir_xfb;

enum agx_dxil_nir_status {
   AGX_DXIL_NIR_OK = 0,
   AGX_DXIL_NIR_INVALID,     /* not a DXIL container or a malformed one */
   AGX_DXIL_NIR_UNSUPPORTED, /* valid DXIL using something not translated yet */
   AGX_DXIL_NIR_LIMIT,       /* more bindings than the slot limits allow */
   AGX_DXIL_NIR_OUT_OF_MEMORY,
   AGX_DXIL_NIR_FAILED,      /* the converter or spirv_to_nir failed */
};

struct agx_dxil_nir_result {
   struct nir_shader *nir;
   struct dxbc_binding_map bindings;
   mesa_shader_stage stage;
   char error[160];
};

/* True when a DXBC container carries a DXIL program part (DXIL or ILDB). */
int agx_dxil_is_container(const void *data, size_t bytes);
/* True for a DXIL program part (the form D3D12 CreateShader DDIs carry:
 * the version token, the size in dwords, then "DXIL" and its bitcode). */
int agx_dxil_is_program(const void *data, size_t bytes);

/* Translates the single entry point of a container or program part. limits are the slot
 * capacities per DXBC_BINDING_* kind; DXBC_BINDING_HEAP_RANGES lets that kind's
 * unbounded or oversized ranges read the descriptor heap (C47).
 * On success result->nir is owned by the caller (ralloc). */
enum agx_dxil_nir_status agx_dxil_to_nir(const void *data, size_t bytes,
                                         const uint32_t limits[DXBC_BINDING_KIND_COUNT],
                                         const struct nir_shader_compiler_options *options,
                                         struct agx_dxil_nir_result *result);

/* M1: rewrites (x & M) * 2^s (amul, imul or ishl) to x * 2^s when M keeps the
 * low 32 - s bits (an identity in 32-bit arithmetic); run by agx_dxil_to_nir
 * unless APPLE_AGX_DXIL_INDEX_MASK=0. */
void agx_dxil_drop_index_masks(struct nir_shader *nir);

/* C39 for DXIL: nir_xfb_info for a translated vertex or geometry shader
 * from the D3D stream-output declaration, as the DXBC translator builds it
 * (stream 0; register r captured from VARYING_SLOT_POS when the output
 * signature names SV_Position there, else from VAR0 + r, where the frontend
 * places signature rows). False when an entry has no covering output. */
int agx_dxil_nir_xfb(struct nir_shader *nir, const struct dxbc_nir_xfb *xfb);

#ifdef __cplusplus
}
#endif

#endif
