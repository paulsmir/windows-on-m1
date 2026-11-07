/* Original bounded DXIL framing parser. SPDX-License-Identifier: MIT */
#ifndef AGX_DXIL_PROGRAM_H
#define AGX_DXIL_PROGRAM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum agx_dxil_input_kind {
   AGX_DXIL_INPUT_CONTAINER = 1,
   AGX_DXIL_INPUT_PROGRAM = 2
};
enum agx_dxil_parse_status {
   AGX_DXIL_PARSE_OK = 0,
   AGX_DXIL_PARSE_INVALID = 1,
   AGX_DXIL_PARSE_UNSUPPORTED = 2
};
struct agx_dxil_profile {
   uint32_t shader_kind_mask; /* DXIL ShaderKind bit positions; not DDI stages. */
   uint16_t minimum_shader_model; /* major << 4 | minor; <= 0xff */
   uint16_t maximum_shader_model;
};
struct agx_dxil_program {
   size_t container_bytes; /* Declared container size, or zero for PROGRAM. */
   size_t program_offset, program_bytes;
   size_t bitcode_offset, bitcode_bytes; /* Relative to original input. */
   uint32_t program_version, dxil_version;
   uint16_t shader_kind;
   uint8_t shader_major, shader_minor;
};
/* No allocations or retained pointers. Input may be unaligned. Success validates
 * framing only, not checksum, LLVM bitcode, DXIL semantics or DDI acceptance.
 * All output bytes are zero on INVALID/UNSUPPORTED when output is non-NULL.
 * Output must not alias input/profile. Caller supplies actual accessible length; this never infers a DDI length.
 * Profile stage/model checks occur only after complete structural validation. */
enum agx_dxil_parse_status agx_dxil_parse(const void *input, size_t caller_bytes,
   enum agx_dxil_input_kind kind, const struct agx_dxil_profile *profile,
   struct agx_dxil_program *output);
#ifdef __cplusplus
}
#endif
#endif
