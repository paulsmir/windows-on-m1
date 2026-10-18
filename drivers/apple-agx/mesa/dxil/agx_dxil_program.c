/* Original bounded DXIL framing parser. SPDX-License-Identifier: MIT
 * Source/layout/license: investigation/analysis/D3D12-C15-bounded-dxil-input.md.
 * No upstream implementation is copied or linked. */
#include "agx_dxil_program.h"
#include <string.h>

static uint32_t read32(const unsigned char *p)
{
   return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
          (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static enum agx_dxil_parse_status container_program(const unsigned char *p,
   size_t length, struct agx_dxil_program *result)
{
   if (length < 32 || memcmp(p, "DXBC", 4))
      return AGX_DXIL_PARSE_INVALID;
   const uint32_t major = (uint32_t)p[20] | (uint32_t)p[21] << 8;
   const size_t bytes = read32(p + 24);
   const uint32_t count = read32(p + 28);
   if (bytes < 32 || bytes > length || bytes > UINT32_C(0x80000000) ||
       count > (bytes - 32) / 4)
      return AGX_DXIL_PARSE_INVALID;
   size_t next = 32 + (size_t)count * 4;
   unsigned found = 0;
   for (uint32_t i = 0; i < count; ++i) {
      const size_t offset = read32(p + 32 + (size_t)i * 4);
      if (offset != next || offset > bytes || bytes - offset < 8)
         return AGX_DXIL_PARSE_INVALID;
      const size_t body = read32(p + offset + 4);
      if (body > bytes - offset - 8)
         return AGX_DXIL_PARSE_INVALID;
      if (!memcmp(p + offset, "DXIL", 4)) {
         if (found)
            return AGX_DXIL_PARSE_INVALID;
         found = 1;
         result->program_offset = offset + 8;
         result->program_bytes = body;
      }
      next = offset + 8 + body;
   }
   if (next != bytes)
      return AGX_DXIL_PARSE_INVALID;
   if (major != 1 || !found)
      return AGX_DXIL_PARSE_UNSUPPORTED;
   result->container_bytes = bytes;
   return AGX_DXIL_PARSE_OK;
}

enum agx_dxil_parse_status agx_dxil_parse(const void *input, size_t caller_bytes,
   enum agx_dxil_input_kind kind, const struct agx_dxil_profile *profile,
   struct agx_dxil_program *output)
{
   if (!output)
      return AGX_DXIL_PARSE_INVALID;
   memset(output, 0, sizeof(*output));
   if (!input || !profile || !caller_bytes ||
       profile->minimum_shader_model > profile->maximum_shader_model ||
       profile->maximum_shader_model > 0xff)
      return AGX_DXIL_PARSE_INVALID;
   const unsigned char *bytes = input;
   struct agx_dxil_program parsed;
   memset(&parsed, 0, sizeof(parsed));
   if (kind == AGX_DXIL_INPUT_CONTAINER) {
      enum agx_dxil_parse_status status = container_program(bytes, caller_bytes, &parsed);
      if (status != AGX_DXIL_PARSE_OK)
         return status;
   } else if (kind == AGX_DXIL_INPUT_PROGRAM) {
      parsed.program_bytes = caller_bytes;
   } else {
      return AGX_DXIL_PARSE_INVALID;
   }
   if (parsed.program_bytes < 24)
      return AGX_DXIL_PARSE_INVALID;
   const unsigned char *program = bytes + parsed.program_offset;
   const uint32_t words = read32(program + 4);
   if (words < 6 || words > parsed.program_bytes / 4 ||
       memcmp(program + 8, "DXIL", 4))
      return AGX_DXIL_PARSE_INVALID;
   const size_t program_bytes = (size_t)words * 4;
   const size_t bitcode_relative = read32(program + 16);
   const size_t bitcode_size = read32(program + 20);
   if (bitcode_relative < 16 || bitcode_relative > program_bytes - 8 ||
       !bitcode_size || bitcode_size > program_bytes - 8 - bitcode_relative)
      return AGX_DXIL_PARSE_INVALID;
   parsed.program_bytes = program_bytes;
   parsed.bitcode_offset = parsed.program_offset + 8 + bitcode_relative;
   parsed.bitcode_bytes = bitcode_size;
   parsed.program_version = read32(program);
   parsed.dxil_version = read32(program + 12);
   parsed.shader_kind = (uint16_t)(parsed.program_version >> 16);
   parsed.shader_major = (uint8_t)((parsed.program_version >> 4) & 15);
   parsed.shader_minor = (uint8_t)(parsed.program_version & 15);
   const unsigned model = (unsigned)parsed.shader_major * 16 + parsed.shader_minor;
   if (parsed.shader_kind >= 32 || !(profile->shader_kind_mask & (UINT32_C(1) << parsed.shader_kind)) ||
       model < profile->minimum_shader_model || model > profile->maximum_shader_model)
      return AGX_DXIL_PARSE_UNSUPPORTED;
   *output = parsed;
   return AGX_DXIL_PARSE_OK;
}
