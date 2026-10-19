/* Original portable opaque handoff API; no Windows Prepare types. MIT */
#pragma once
#include "agx_owned_nir_pipe.h"
struct AgxOwnedNir;
#ifdef __cplusplus
/* Actual live prepared/context, caller-serialized through nonthrowing callback;
 * no concurrent/reentrant destroy or NIR mutation. Root retained/copied by
 * future caller. No added refcount/threading/DDI policy. */
agx_owned_nir_stage AgxOwnedNirStage(const AgxOwnedNir *) noexcept;
const uint32_t *AgxOwnedNirWorkgroup(const AgxOwnedNir *) noexcept;
agx_owned_nir_pipe_status AgxOwnedNirCreatePipeState(
 const AgxOwnedNir *,pipe_context *,agx_owned_nir_pipe_state *) noexcept;
#endif
