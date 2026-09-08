/* SPDX-License-Identifier: MIT
 * Project-owned fixture that executes pinned Asahi VS/FS driver lowering. */
#include "asahi/compiler/agx_compile.h"
#include "asahi/lib/agx_linker.h"
#include "asahi/lib/agx_uvs.h"
#include "asahi/lib/agx_usc.h"
#include "asahi/lib/agx_tilebuffer.h"
#include "asahi/isa/disasm.h"
#include "compiler/glsl_types.h"
#include "nir_builder.h"
#include "shader_enums.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIPELINE_CAPACITY 512u
#define ENCODER_CAPACITY 512u

struct encoder_offsets {
   unsigned vs_pipeline_offset;
   unsigned vs_shader_offset;
   unsigned fs_pipeline_offset;
   unsigned fs_uniform_offset;
   unsigned fs_shader_offset;
   unsigned vdm_pipeline_offset;
   unsigned fragment_pipeline_offset;
   unsigned pipeline_bytes;
   unsigned encoder_bytes;
};

static unsigned
type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static uint64_t
hash_bytes(const unsigned char *data, size_t bytes)
{
   uint64_t hash = UINT64_C(0xcbf29ce484222325);
   for (size_t i = 0; i < bytes; ++i) {
      hash ^= data[i];
      hash *= UINT64_C(0x100000001b3);
   }
   return hash;
}

static nir_shader *
build_vertex(unsigned variant, struct agx_unlinked_uvs_layout *uvs)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_VERTEX, &agx_nir_options, "windows_ad03_vertex");
   nir_variable *position = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "gl_Position");
   nir_variable *colour = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "colour");
   position->data.location = VARYING_SLOT_POS;
   colour->data.location = VARYING_SLOT_VAR0;
   nir_store_var(&b, position,
                 nir_vec4(&b, nir_imm_float(&b, -0.5f),
                          nir_imm_float(&b, -0.5f),
                          nir_imm_float(&b, 0.0f), nir_imm_float(&b, 1.0f)),
                 0xfu);
   nir_store_var(&b, colour,
                 nir_vec4(&b, nir_imm_float(&b, variant ? 0.25f : 1.0f),
                          nir_imm_float(&b, variant ? 1.0f : 0.25f),
                          nir_imm_float(&b, 0.5f), nir_imm_float(&b, 1.0f)),
                 0xfu);
   nir_lower_io(b.shader, nir_var_shader_in | nir_var_shader_out, type_size,
                nir_lower_io_lower_64bit_to_32 |
                   nir_lower_io_use_interpolated_input_intrinsics);
   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
   BITSET_DECLARE(attrib_components_read, VERT_ATTRIB_MAX * 4) = {0};
   agx_nir_gather_vs_inputs(b.shader, attrib_components_read);
   agx_nir_lower_vs_input_to_prolog(b.shader);
   nir_lower_io_to_scalar(b.shader, nir_var_shader_out, NULL, NULL);
   agx_nir_lower_uvs(b.shader, uvs);
   agx_preprocess_nir(b.shader);
   return b.shader;
}

static nir_shader *
build_fragment(unsigned variant, struct agx_fs_epilog_link_info *epilog)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "windows_ad03_fragment");
   nir_variable *colour = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "colour");
   colour->data.location = FRAG_RESULT_DATA0;
   nir_store_var(&b, colour,
                 nir_vec4(&b, nir_imm_float(&b, variant ? 0.75f : 0.1f),
                          nir_imm_float(&b, variant ? 0.2f : 0.8f),
                          nir_imm_float(&b, 0.4f), nir_imm_float(&b, 1.0f)),
                 0xfu);
   nir_lower_io(b.shader, nir_var_shader_in | nir_var_shader_out, type_size,
                nir_lower_io_lower_64bit_to_32 |
                   nir_lower_io_use_interpolated_input_intrinsics);
   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
   agx_preprocess_nir(b.shader);
   agx_nir_lower_discard_zs_emit(b.shader);
   agx_nir_lower_fs_output_to_epilog(b.shader, epilog);
   agx_nir_lower_sample_mask(b.shader);
   agx_nir_lower_fs_active_samples_to_register(b.shader);
   return b.shader;
}

static int
write_binary(const char *directory, const char *name,
             const struct agx_shader_part *part)
{
   char path[1024];
   if (snprintf(path, sizeof(path), "%s/%s.bin", directory, name) < 0)
      return 0;
   FILE *file = fopen(path, "wb");
   if (!file)
      return 0;
   size_t written = fwrite(part->binary, 1, part->info.binary_size, file);
   int closed = fclose(file);
   return written == part->info.binary_size && closed == 0;
}

static int
write_disassembly(const char *directory, const char *name,
                  const struct agx_shader_part *part)
{
   char path[1024];
   if (part->info.main_offset > part->info.binary_size ||
       part->info.main_size > part->info.binary_size - part->info.main_offset ||
       snprintf(path, sizeof(path), "%s/%s.asm", directory, name) < 0)
      return 0;
   FILE *file = fopen(path, "w");
   if (!file)
      return 0;
   bool errors = agx2_disassemble(
      part->binary + part->info.main_offset, part->info.main_size, file);
   int closed = fclose(file);
   return !errors && closed == 0;
}

static int
write_bytes(const char *directory, const char *name,
            const unsigned char *bytes, size_t size)
{
   char path[1024];
   if (snprintf(path, sizeof(path), "%s/%s.bin", directory, name) < 0)
      return 0;
   FILE *file = fopen(path, "wb");
   if (!file)
      return 0;
   size_t written = fwrite(bytes, 1, size, file);
   int closed = fclose(file);
   return written == size && closed == 0;
}

static unsigned
align_u(unsigned value, unsigned alignment)
{
   return (value + alignment - 1u) & ~(alignment - 1u);
}

static int
build_encoder_objects(const struct agx_shader_part *vs,
                      const struct agx_shader_part *fs,
                      const struct agx_unlinked_uvs_layout *uvs,
                      unsigned char pipeline[PIPELINE_CAPACITY],
                      unsigned char encoder[ENCODER_CAPACITY],
                      struct encoder_offsets *offsets)
{
   enum pipe_format formats[8] = {PIPE_FORMAT_B8G8R8A8_UNORM};
   struct agx_tilebuffer_layout tib =
      agx_build_tilebuffer_layout(formats, 8u, 1u, false);
   struct agx_usc_builder usc;
   unsigned char *head;
   memset(pipeline, 0, PIPELINE_CAPACITY);
   memset(encoder, 0, ENCODER_CAPACITY);
   memset(offsets, 0, sizeof(*offsets));

   offsets->vs_pipeline_offset = 0u;
   usc = agx_usc_builder(pipeline, PIPELINE_CAPACITY);
   agx_usc_shared_none(&usc);
   offsets->vs_shader_offset = (unsigned)(usc.head - pipeline);
   agx_usc_pack(&usc, SHADER, cfg) {
      cfg.code = 0u;
      cfg.unk_2 = 3u;
   }
   agx_usc_pack(&usc, REGISTERS, cfg)
      cfg.register_count = vs->info.nr_gprs;
   agx_usc_pack(&usc, NO_PRESHADER, cfg)
      ;

   offsets->fs_pipeline_offset = align_u((unsigned)(usc.head - pipeline), 64u);
   usc = agx_usc_builder(pipeline + offsets->fs_pipeline_offset,
                         PIPELINE_CAPACITY - offsets->fs_pipeline_offset);
   if (fs->info.rodata.size_16 != 0u) {
      offsets->fs_uniform_offset =
         offsets->fs_pipeline_offset + (unsigned)(usc.head -
                                                   (pipeline + offsets->fs_pipeline_offset));
      agx_usc_uniform(&usc, fs->info.rodata.base_uniform,
                      fs->info.rodata.size_16, 0ULL);
   }
   agx_usc_push_packed(&usc, SHARED, tib.usc);
   offsets->fs_shader_offset =
      offsets->fs_pipeline_offset +
      (unsigned)(usc.head - (pipeline + offsets->fs_pipeline_offset));
   agx_usc_pack(&usc, SHADER, cfg) {
      cfg.code = 0u;
      cfg.unk_2 = 3u;
   }
   agx_usc_pack(&usc, REGISTERS, cfg)
      cfg.register_count = fs->info.nr_gprs;
   agx_usc_pack(&usc, NO_PRESHADER, cfg)
      ;
   offsets->pipeline_bytes =
      offsets->fs_pipeline_offset +
      (unsigned)(usc.head - (pipeline + offsets->fs_pipeline_offset));

   head = encoder;
   {
      struct AGX_VDM_STATE state = {
         .vertex_shader_word_0_present = true,
         .vertex_shader_word_1_present = true,
         .vertex_outputs_present = true,
         .vertex_unknown_present = true,
      };
      AGX_VDM_STATE_pack((uint32_t *)head, &state);
      head += AGX_VDM_STATE_LENGTH;
   }
   {
      struct AGX_VDM_STATE_VERTEX_SHADER_WORD_0 word = {
         .uniform_register_count = vs->info.push_count,
         .texture_state_register_count = 0u,
         .sampler_state_register_count = 0u,
         .preshader_register_count = vs->info.nr_preamble_gprs,
      };
      AGX_VDM_STATE_VERTEX_SHADER_WORD_0_pack((uint32_t *)head, &word);
      head += AGX_VDM_STATE_VERTEX_SHADER_WORD_0_LENGTH;
   }
   offsets->vdm_pipeline_offset = (unsigned)(head - encoder);
   {
      struct AGX_VDM_STATE_VERTEX_SHADER_WORD_1 word = {.pipeline = 0u};
      AGX_VDM_STATE_VERTEX_SHADER_WORD_1_pack((uint32_t *)head, &word);
      head += AGX_VDM_STATE_VERTEX_SHADER_WORD_1_LENGTH;
   }
   memcpy(head, uvs->vdm.opaque, AGX_VDM_STATE_VERTEX_OUTPUTS_LENGTH);
   head += AGX_VDM_STATE_VERTEX_OUTPUTS_LENGTH;
   {
      struct AGX_VDM_STATE_VERTEX_UNKNOWN word = {
         .flat_shading_control = AGX_VDM_VERTEX_2,
      };
      AGX_VDM_STATE_VERTEX_UNKNOWN_pack((uint32_t *)head, &word);
      head += AGX_VDM_STATE_VERTEX_UNKNOWN_LENGTH;
   }
   memset(head, 0, 4u);
   head += 4u;
   head = encoder + align_u((unsigned)(head - encoder), 64u);
   offsets->fragment_pipeline_offset = (unsigned)(head - encoder);
   {
      struct AGX_FRAGMENT_SHADER_WORD_1 word = {.pipeline = 0u};
      AGX_FRAGMENT_SHADER_WORD_1_pack((uint32_t *)head, &word);
      head += AGX_FRAGMENT_SHADER_WORD_1_LENGTH;
   }
   offsets->encoder_bytes = (unsigned)(head - encoder);

   struct AGX_USC_SHADER usc_shader;
   struct AGX_VDM_STATE_VERTEX_SHADER_WORD_1 vdm_word;
   struct AGX_FRAGMENT_SHADER_WORD_1 fragment_word;
   if (!AGX_USC_SHADER_unpack(NULL, pipeline + offsets->vs_shader_offset,
                              &usc_shader) ||
       usc_shader.code != 0u || usc_shader.unk_2 != 3u ||
       !AGX_USC_SHADER_unpack(NULL, pipeline + offsets->fs_shader_offset,
                              &usc_shader) ||
       !AGX_VDM_STATE_VERTEX_SHADER_WORD_1_unpack(
          NULL, encoder + offsets->vdm_pipeline_offset, &vdm_word) ||
       vdm_word.pipeline != 0u ||
       !AGX_FRAGMENT_SHADER_WORD_1_unpack(
          NULL, encoder + offsets->fragment_pipeline_offset,
          &fragment_word) || fragment_word.pipeline != 0u)
      return 0;
   if (fs->info.rodata.size_16 != 0u) {
      struct AGX_USC_UNIFORM uniform;
      if (!AGX_USC_UNIFORM_unpack(NULL,
                                  pipeline + offsets->fs_uniform_offset,
                                  &uniform) || uniform.buffer != 0u)
         return 0;
   }
   return offsets->pipeline_bytes <= PIPELINE_CAPACITY &&
          offsets->encoder_bytes <= ENCODER_CAPACITY;
}

static void
print_part(const char *name, const struct agx_shader_part *part)
{
   printf("\"%s\":{\"binary_bytes\":%u,\"main_offset\":%u,"
          "\"main_bytes\":%u,\"gprs\":%u,\"preamble_gprs\":%u,"
          "\"scratch_bytes\":%u,\"rodata_offset\":%u,"
          "\"rodata_halfwords\":%u,\"fnv1a64\":\"0x%016llx\"}",
          name, part->info.binary_size, part->info.main_offset,
          part->info.main_size, part->info.nr_gprs,
          part->info.nr_preamble_gprs, part->info.scratch_size,
          part->info.rodata.offset, part->info.rodata.size_16,
          (unsigned long long)hash_bytes(part->binary,
                                         part->info.binary_size));
}

int
main(int argc, char **argv)
{
   if (argc != 3)
      return 2;
   char *end = NULL;
   errno = 0;
   unsigned long parsed = strtoul(argv[2], &end, 10);
   if (errno || end == argv[2] || *end != '\0' || parsed > 1)
      return 2;
   unsigned variant = (unsigned)parsed;
   struct agx_unlinked_uvs_layout uvs = {0};
   struct agx_fs_epilog_link_info epilog = {0};
   struct agx_shader_key key = {.promote_constants = true};
   struct agx_shader_part vs = {0};
   struct agx_shader_part fs = {0};
   _Alignas(8) unsigned char pipeline[PIPELINE_CAPACITY];
   _Alignas(8) unsigned char encoder[ENCODER_CAPACITY];
   struct encoder_offsets offsets;
   glsl_type_singleton_init_or_ref();
   nir_shader *vs_nir = build_vertex(variant, &uvs);
   nir_shader *fs_nir = build_fragment(variant, &epilog);
   agx_compile_shader_nir(vs_nir, &key, &vs);
   agx_compile_shader_nir(fs_nir, &key, &fs);
   if (!vs.binary || !vs.info.binary_size || !fs.binary ||
       !fs.info.binary_size || !write_binary(argv[1], "vertex", &vs) ||
       !write_binary(argv[1], "fragment", &fs) ||
       !write_disassembly(argv[1], "vertex", &vs) ||
       !write_disassembly(argv[1], "fragment", &fs) ||
       !build_encoder_objects(&vs, &fs, &uvs, pipeline, encoder, &offsets) ||
       !write_bytes(argv[1], "pipeline", pipeline, offsets.pipeline_bytes) ||
       !write_bytes(argv[1], "encoder", encoder, offsets.encoder_bytes))
      return 1;
   printf("{\"schema\":1,\"variant\":%u,\"uvs_size\":%u,"
          "\"uvs_user_size\":%u,\"epilog_loc_written\":%u,"
          "\"pipeline_bytes\":%u,\"encoder_bytes\":%u,"
          "\"relocations\":["
          "{\"kind\":\"UscShaderOffset32\",\"destination\":%u,"
          "\"target\":\"vertex.main\"},"
          "{\"kind\":\"UscBufferAddress40\",\"destination\":%u,"
          "\"target\":\"fragment.rodata\"},"
          "{\"kind\":\"UscShaderOffset32\",\"destination\":%u,"
          "\"target\":\"fragment.main\"},"
          "{\"kind\":\"VdmPipelineOffset32\",\"destination\":%u,"
          "\"target_offset\":%u},"
          "{\"kind\":\"VdmPipelineOffset32\",\"destination\":%u,"
          "\"target_offset\":%u}],",
          variant, uvs.size, uvs.user_size, epilog.loc_written,
          offsets.pipeline_bytes, offsets.encoder_bytes,
          offsets.vs_shader_offset, offsets.fs_uniform_offset,
          offsets.fs_shader_offset, offsets.vdm_pipeline_offset,
          offsets.vs_pipeline_offset, offsets.fragment_pipeline_offset,
          offsets.fs_pipeline_offset);
   print_part("vertex", &vs);
   printf(",");
   print_part("fragment", &fs);
   printf("}\n");
   free(vs.binary);
   free(fs.binary);
   ralloc_free(vs_nir);
   ralloc_free(fs_nir);
   glsl_type_singleton_decref();
   return 0;
}
