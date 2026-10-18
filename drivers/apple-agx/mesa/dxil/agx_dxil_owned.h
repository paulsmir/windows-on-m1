/* Original standalone owned converter checkpoint. SPDX-License-Identifier: MIT */
#ifndef AGX_DXIL_OWNED_H
#define AGX_DXIL_OWNED_H
#include "agx_dxil_program.h"
#include "../dxbc/dxbc_bindings.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Initial offline profile: one compute6.0 entry, static singleton raw UAVs only.
 * No DDI/owner integration and no guarantee of recoverable internal library OOM.
 * Caller thread must have no foreign dxil_spv allocator context. */
struct agx_dxil_owned_request {
   const void *input;
   size_t input_bytes;
   enum agx_dxil_input_kind input_kind;
   const char *entry;
   size_t entry_bytes; /* No embedded NUL, 1..255 bytes, terminator not included. */
   uint32_t binding_limits[DXBC_BINDING_KIND_COUNT];
};
enum agx_dxil_owned_status {
   AGX_DXIL_OWNED_OK = 0,
   AGX_DXIL_OWNED_INVALID = 1,
   AGX_DXIL_OWNED_UNSUPPORTED = 2,
   AGX_DXIL_OWNED_REPORTED_ERROR = 3,
   AGX_DXIL_OWNED_PROJECT_OOM = 4,
   AGX_DXIL_OWNED_REENTRANT = 5
};
struct agx_dxil_owned_result {
   uint32_t *spirv; /* Project-owned ordinary allocation. */
   size_t spirv_bytes;
   struct dxbc_binding_map bindings;
   uint32_t workgroup[3];
   char entry[256];
};
/* Result must initially be empty; input/request/result must not overlap.
 * All normal reported failures leave result empty. Owns a complete per-call TLS
 * scope. No C API object or borrowed output survives return. */
enum agx_dxil_owned_status agx_dxil_convert_owned(
   const struct agx_dxil_owned_request *request, struct agx_dxil_owned_result *result);
void agx_dxil_owned_free(struct agx_dxil_owned_result *result);
#ifdef __cplusplus
}
#endif
#endif
