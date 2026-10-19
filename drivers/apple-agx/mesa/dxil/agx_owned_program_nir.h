/* Original unlinked owned-PROGRAM to owner-facing NIR adapter. SPDX-License-Identifier: MIT */
/* Include actual windows.h and d3d12umddi.h before this header. Internal NIR
 * stays opaque; this interface is not a Windows DDI callback. */
#pragma once
#include "../../d3d12/agx_d3d12_shader_owned_core.h"
#include "agx_owned_program_nir_native.h"
#include "agx_owned_nir_pipe.h"
struct AgxOwnedNir;
struct AgxOwnedNirRequest {const AgxShaderOwned *owned;SIZE_T readable_owned_bytes;const char *entry;size_t entry_bytes;uint32_t limits[4];};
/* Caller provides a valid live CPU-owned object, stable accessible bytes and
 * disjoint input/output. Not an arbitrary handle validator. Client/context
 * lifetime remains caller-owned; no Windows provenance/readability guarantee.
 * Success owns NIR/map/root snapshot. Target profile is Asahi non-base
 * dispatch: DXIL compute output is normalized to have no base global/workgroup
 * ID loads. No permission to discard a future promised nonzero dispatch base.
 * NIR is borrowed until Destroy; consuming Gallium callers must clone.
 * GLSL type singleton reference stays owned with the NIR. Do not transfer the
 * raw pointer independently or infer runtime-owner validity from copied bytes.
 * PROJECT_OOM covers only project allocations/reported status, not a generic
 * recoverable upstream allocation-failure guarantee. No borrowed storage survives return.
 * Output must initially be empty. Failure publishes NULL, no legacy fallback. */
agx_owned_nir_status AgxOwnedNirPrepare(const AgxOwnedNirRequest *,AgxOwnedNir **) noexcept;
void AgxOwnedNirDestroy(AgxOwnedNir **) noexcept;
nir_shader *AgxOwnedNirShader(AgxOwnedNir *) noexcept;
const dxbc_binding_map *AgxOwnedNirBindings(const AgxOwnedNir *) noexcept;
AgxShaderTrustedSpan AgxOwnedNirRoot(const AgxOwnedNir *) noexcept;
const uint32_t *AgxOwnedNirWorkgroup(const AgxOwnedNir *) noexcept;

/* Caller-serialized stable live prepared/context held through nonthrowing
 * callback. No concurrent/reentrant destruction. Root retained/copied by the
 * future caller; this forwarding entry adds no reference/thread/DDI policy.
 * Same empty/nonempty-output rules and statuses as the C helper. */
agx_owned_nir_pipe_status AgxOwnedNirCreatePipeState(
 const AgxOwnedNir *,pipe_context *,agx_owned_nir_pipe_state *) noexcept;
