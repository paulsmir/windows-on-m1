/* Original portable opaque handoff API; no Windows Prepare types. MIT */
#pragma once
#include "agx_owned_nir_pipe.h"
struct AgxOwnedNir;
#ifdef __cplusplus
/* Actual live prepared/context, caller-serialized through nonthrowing callback;
 * no concurrent/reentrant destroy or NIR mutation. Root retained/copied by
 * future caller. No added refcount/threading/DDI policy. */
/* Actual stable, uint32_t-aligned driver-owned legacy PROGRAM span;
 * disjoint limits/out; no
 * runtime readability inference. Forced legacy/computestage; rejects SM6.
 * Initially empty out, nonempty refused unchanged; publishes owned NIR/map/GLSL
 * reference last. No input retention. Root is UNSPECIFIED (no compatibility
 * identity), existing caller root resolution remains external. Project status
 * only, no general upstream allocation recovery guarantee. */
agx_owned_nir_status AgxOwnedNirPrepareLegacyCompute(const void *,size_t,
 const uint32_t limits[4],AgxOwnedNir **) noexcept;
void AgxOwnedNirDestroy(AgxOwnedNir **) noexcept;
agx_owned_nir_stage AgxOwnedNirStage(const AgxOwnedNir *) noexcept;
const uint32_t *AgxOwnedNirWorkgroup(const AgxOwnedNir *) noexcept;
agx_owned_nir_pipe_status AgxOwnedNirCreatePipeState(
 const AgxOwnedNir *,pipe_context *,agx_owned_nir_pipe_state *) noexcept;
#endif
