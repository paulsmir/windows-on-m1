/* Original UNLINKED caller handoff, not a Windows DDI. SPDX-License-Identifier: MIT */
#pragma once
#include "agx_owned_program_nir_native.h"
#include "../dxbc/dxbc_resources.h"
#ifdef __cplusplus
extern "C" {
#endif
struct pipe_context;
struct agx_owned_nir_pipe_state {
 void *state;
 struct dxbc_binding_map bindings;
 uint32_t token_stage,block[3],shared_bytes;
 uint8_t srv_dimensions[DXBC_RESOURCE_SLOTS];
 uint8_t uav_dimensions[DXBC_UAV_DIMENSION_SLOTS];
};
enum agx_owned_nir_pipe_status {
 AGX_OWNED_NIR_PIPE_OK,AGX_OWNED_NIR_PIPE_INVALID,
 AGX_OWNED_NIR_PIPE_UNSUPPORTED,AGX_OWNED_NIR_PIPE_CLONE_FAILED,
 AGX_OWNED_NIR_PIPE_CREATE_FAILED
};
/* Stable readable disjoint source/context/output; serialized live context.
 * Output must be memberwise zero: a nonempty result is rejected unchanged.
 * Source owns its GLSL reference throughout this call. Callbacks must honor
 * NIR consume-on-invocation, including NULL return (qualified1487 native policy).
 * No translation/normalization, arbitrary handle validation or upstream OOM
 * recovery guarantee. Root compatibility bytes stay with the future caller's
 * retained CPU-owned object; no root pointer survives in this result.
 * A successful state must be deleted on the same live context before teardown. */
enum agx_owned_nir_pipe_status agx_owned_nir_create_pipe_state(
 const struct agx_owned_program_nir_native *,struct pipe_context *,
 struct agx_owned_nir_pipe_state *);
/* Empty delete is a no-op; otherwise requires the matching live callback pair. */
void agx_owned_nir_delete_pipe_state(struct pipe_context *,struct agx_owned_nir_pipe_state *);
#ifdef __cplusplus
}
#endif
