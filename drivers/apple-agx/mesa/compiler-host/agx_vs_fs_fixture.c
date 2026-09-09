/* SPDX-License-Identifier: MIT
 * Project-owned fixture that executes pinned Asahi VS/FS driver lowering. */
#include "asahi/compiler/agx_compile.h"
#include "asahi/lib/agx_linker.h"
#include "asahi/lib/agx_ppp.h"
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
#define ENCODER_CAPACITY 1024u
#define FRAME_WIDTH 2560u
#define FRAME_HEIGHT 1600u

struct encoder_offsets {
   unsigned vs_pipeline_offset;
   unsigned vs_uniform_offset;
   unsigned vs_shader_offset;
   unsigned fs_pipeline_offset;
   unsigned fs_uniform_offset;
   unsigned fs_shader_offset;
   unsigned vdm_pipeline_offset;
   unsigned ppp_state_address_offset;
   unsigned ppp_offset;
   unsigned ppp_bytes;
   unsigned varying_counts_32_offset;
   unsigned varying_counts_16_offset;
   unsigned varying_smooth_32;
   unsigned varying_flat_32;
   unsigned varying_linear_32;
   unsigned varying_total_16;
   unsigned fragment_pipeline_offset;
   unsigned draw_offset;
   unsigned terminate_offset;
   unsigned pipeline_bytes;
   unsigned encoder_bytes;
};

struct linked_fragment {
   unsigned char *binary;
   unsigned binary_bytes;
   unsigned register_count;
   bool reads_tib;
   bool writes_sample_mask;
   bool disable_tri_merging;
   bool tag_write_disable;
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
   /* The hardware vertex stage exposes load_vertex_id directly.  The Draw ABI
    * currently requires FirstVertex == 0, so it is also the zero-based ID for
    * this canonical non-indexed triangle. */
   nir_def *vertex_id = nir_load_vertex_id(&b);
   nir_def *is_vertex_0 = nir_ieq_imm(&b, vertex_id, 0u);
   nir_def *is_vertex_1 = nir_ieq_imm(&b, vertex_id, 1u);
   nir_def *is_vertex_2 = nir_ieq_imm(&b, vertex_id, 2u);
   nir_def *x = nir_bcsel(
      &b, is_vertex_0, nir_imm_float(&b, -0.8f),
      nir_bcsel(&b, is_vertex_1, nir_imm_float(&b, 0.8f),
                nir_imm_float(&b, 0.0f)));
   nir_def *y = nir_bcsel(&b, is_vertex_2, nir_imm_float(&b, 0.8f),
                          nir_imm_float(&b, -0.8f));
   nir_store_var(&b, position,
                 nir_vec4(&b, x, y, nir_imm_float(&b, 0.0f),
                          nir_imm_float(&b, 1.0f)),
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
build_fragment_epilog(const struct agx_fs_epilog_link_info *link)
{
   struct agx_fs_epilog_key epilog_key = {0};
   epilog_key.link = *link;
   epilog_key.nr_samples = 1u;
   epilog_key.rt_formats[0] = PIPE_FORMAT_B8G8R8A8_UNORM;
   for (unsigned i = 1u; i < 8u; ++i)
      epilog_key.rt_formats[i] = PIPE_FORMAT_NONE;
   for (unsigned i = 0u; i < 8u; ++i)
      epilog_key.remap[i] = (int8_t)i;
   epilog_key.blend.rt[0].colormask = PIPE_MASK_RGBA;
   epilog_key.blend.rt[0].mode = agx_pack_blend_standard(
      PIPE_BLEND_ADD, PIPE_BLENDFACTOR_ONE, PIPE_BLENDFACTOR_ZERO,
      PIPE_BLEND_ADD, PIPE_BLENDFACTOR_ONE, PIPE_BLENDFACTOR_ZERO);

   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, &agx_nir_options, "windows_ad03_fragment_epilog");
   agx_nir_fs_epilog(&b, &epilog_key);
   agx_preprocess_nir(b.shader);
   return b.shader;
}

static int
link_fragment(const struct agx_shader_part *main,
              const struct agx_fs_epilog_link_info *link,
              struct linked_fragment *linked)
{
   nir_shader *epilog_nir = build_fragment_epilog(link);
   struct agx_shader_key epilog_key = {.secondary = true};
   struct agx_shader_part epilog = {0};
   agx_compile_shader_nir(epilog_nir, &epilog_key, &epilog);
   if (!epilog.binary || main->info.main_offset > main->info.binary_size ||
       main->info.main_size > main->info.binary_size - main->info.main_offset ||
       epilog.info.main_offset > epilog.info.binary_size ||
       epilog.info.main_size >
          epilog.info.binary_size - epilog.info.main_offset) {
      free(epilog.binary);
      ralloc_free(epilog_nir);
      return 0;
   }

   memset(linked, 0, sizeof(*linked));
   linked->binary_bytes = main->info.main_size + epilog.info.main_size;
   linked->binary = malloc(linked->binary_bytes);
   if (!linked->binary) {
      free(epilog.binary);
      ralloc_free(epilog_nir);
      return 0;
   }
   memcpy(linked->binary,
          (const unsigned char *)main->binary + main->info.main_offset,
          main->info.main_size);
   memcpy(linked->binary + main->info.main_size,
          (const unsigned char *)epilog.binary + epilog.info.main_offset,
          epilog.info.main_size);
   linked->register_count =
      main->info.nr_gprs > epilog.info.nr_gprs ? main->info.nr_gprs
                                               : epilog.info.nr_gprs;
   linked->reads_tib = main->info.reads_tib || epilog.info.reads_tib;
   linked->writes_sample_mask =
      main->info.writes_sample_mask || epilog.info.writes_sample_mask;
   linked->disable_tri_merging =
      main->info.disable_tri_merging || epilog.info.disable_tri_merging;
   linked->tag_write_disable =
      main->info.tag_write_disable && epilog.info.tag_write_disable;
   free(epilog.binary);
   ralloc_free(epilog_nir);
   return linked->binary_bytes != 0u;
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
                      const struct linked_fragment *linked_fs,
                      const struct agx_unlinked_uvs_layout *uvs,
                      unsigned char pipeline[PIPELINE_CAPACITY],
                      unsigned char encoder[ENCODER_CAPACITY],
                      unsigned char scissor[AGX_SCISSOR_LENGTH],
                      unsigned char depth_bias[AGX_DEPTH_BIAS_LENGTH],
                      struct encoder_offsets *offsets)
{
   enum pipe_format formats[8] = {PIPE_FORMAT_B8G8R8A8_UNORM};
   struct agx_tilebuffer_layout tib =
      agx_build_tilebuffer_layout(formats, 8u, 1u, false);
   struct agx_unlinked_uvs_layout linked_uvs = *uvs;
   struct agx_varyings_vs varyings;
   struct agx_usc_builder usc;
   unsigned char *head;
   memset(pipeline, 0, PIPELINE_CAPACITY);
   memset(encoder, 0, ENCODER_CAPACITY);
   memset(offsets, 0, sizeof(*offsets));
   agx_assign_uvs(&varyings, &linked_uvs, 0u, 0u);

   offsets->vs_pipeline_offset = 0u;
   usc = agx_usc_builder(pipeline, PIPELINE_CAPACITY);
   agx_usc_shared_none(&usc);
   if (vs->info.rodata.size_16 != 0u) {
      offsets->vs_uniform_offset = (unsigned)(usc.head - pipeline);
      agx_usc_uniform(&usc, vs->info.rodata.base_uniform,
                      vs->info.rodata.size_16, 0ULL);
   }
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
      cfg.unk_2 = 2u;
   }
   agx_usc_pack(&usc, REGISTERS, cfg)
   {
      cfg.register_count = linked_fs->register_count;
      cfg.unk_1 = 1u;
      cfg.unk_4 = 1u;
   }
   agx_usc_pack(&usc, FRAGMENT_PROPERTIES, cfg) {
      cfg.early_z_testing = !linked_fs->writes_sample_mask;
      cfg.unk_2 = true;
      cfg.unk_3 = 0xfu;
      cfg.unk_4 = 0x2u;
   }
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

   offsets->ppp_state_address_offset = (unsigned)(head - encoder);
   {
      struct AGX_PPP_STATE state = {0};
      AGX_PPP_STATE_pack((uint32_t *)head, &state);
      head += AGX_PPP_STATE_LENGTH;
   }
   offsets->draw_offset = (unsigned)(head - encoder);
   {
      struct agx_draw draw = {.b = agx_3d(3u, 1u, 1u)};
      head = (unsigned char *)agx_vdm_draw(
         (uint32_t *)head, AGX_CHIP_G13G, draw, AGX_PRIMITIVE_TRIANGLES);
   }
   offsets->terminate_offset = (unsigned)(head - encoder);
   head = (unsigned char *)agx_vdm_terminate((uint32_t *)head);

   offsets->ppp_offset = align_u((unsigned)(head - encoder), 64u);
   head = encoder + offsets->ppp_offset;
   struct AGX_PPP_HEADER present = {
      .fragment_control = true,
      .fragment_control_2 = true,
      .fragment_front_face = true,
      .fragment_front_face_2 = true,
      .fragment_back_face = true,
      .fragment_back_face_2 = true,
      .depth_bias_scissor = true,
      .region_clip = true,
      .viewport = true,
      .viewport_count = 1u,
      .output_select = true,
      .varying_counts_32 = true,
      .varying_counts_16 = true,
      .cull = true,
      .cull_2 = true,
      .fragment_shader = true,
      .output_size = true,
   };
   offsets->ppp_bytes = (unsigned)agx_ppp_update_size(&present);
   if (offsets->ppp_offset > ENCODER_CAPACITY ||
       offsets->ppp_bytes > ENCODER_CAPACITY - offsets->ppp_offset)
      return 0;
   struct agx_ptr ppp_ptr = {.cpu = head, .gpu = 0u};
   struct agx_ppp_update ppp =
      agx_new_ppp_update(ppp_ptr, offsets->ppp_bytes, &present);
   agx_ppp_push(&ppp, FRAGMENT_CONTROL, cfg) {
      cfg.scissor_enable = true;
      cfg.disable_tri_merging = linked_fs->disable_tri_merging;
   }
   agx_ppp_push(&ppp, FRAGMENT_CONTROL, cfg) {
      cfg.tag_write_disable = linked_fs->tag_write_disable;
      cfg.disable_tri_merging = linked_fs->disable_tri_merging;
      cfg.pass_type = linked_fs->reads_tib
                         ? (linked_fs->writes_sample_mask
                               ? AGX_PASS_TYPE_TRANSLUCENT_PUNCH_THROUGH
                               : AGX_PASS_TYPE_TRANSLUCENT)
                         : (linked_fs->writes_sample_mask
                               ? AGX_PASS_TYPE_PUNCH_THROUGH
                               : AGX_PASS_TYPE_OPAQUE);
   }
   agx_ppp_push(&ppp, FRAGMENT_FACE, cfg) {
      cfg.line_width = 15u;
      cfg.polygon_mode = AGX_POLYGON_MODE_FILL;
      cfg.disable_depth_write = true;
      cfg.depth_function = AGX_ZS_FUNC_ALWAYS;
   }
   agx_ppp_push(&ppp, FRAGMENT_FACE_2, cfg) {
      cfg.disable_depth_write = true;
      cfg.conservative_depth = AGX_CONSERVATIVE_DEPTH_UNCHANGED;
      cfg.depth_function = AGX_ZS_FUNC_ALWAYS;
      cfg.object_type = AGX_OBJECT_TYPE_TRIANGLE;
   }
   agx_ppp_push(&ppp, FRAGMENT_FACE, cfg) {
      cfg.line_width = 15u;
      cfg.polygon_mode = AGX_POLYGON_MODE_FILL;
      cfg.disable_depth_write = true;
      cfg.depth_function = AGX_ZS_FUNC_ALWAYS;
   }
   agx_ppp_push(&ppp, FRAGMENT_FACE_2, cfg) {
      cfg.disable_depth_write = true;
      cfg.conservative_depth = AGX_CONSERVATIVE_DEPTH_UNCHANGED;
      cfg.depth_function = AGX_ZS_FUNC_ALWAYS;
      cfg.object_type = AGX_OBJECT_TYPE_TRIANGLE;
   }
   agx_ppp_push(&ppp, DEPTH_BIAS_SCISSOR, cfg) {
      cfg.scissor = 0u;
      cfg.depth_bias = 0u;
   }
   agx_ppp_push(&ppp, REGION_CLIP, cfg) {
      cfg.enable = true;
      cfg.min_x = 0u;
      cfg.min_y = 0u;
      cfg.max_x = FRAME_WIDTH / 32u;
      cfg.max_y = FRAME_HEIGHT / 32u;
   }
   agx_ppp_push(&ppp, VIEWPORT_CONTROL, cfg)
      ;
   agx_ppp_push(&ppp, VIEWPORT, cfg) {
      cfg.translate_x = FRAME_WIDTH * 0.5f;
      cfg.scale_x = FRAME_WIDTH * 0.5f;
      cfg.translate_y = FRAME_HEIGHT * 0.5f;
      cfg.scale_y = FRAME_HEIGHT * 0.5f;
      cfg.translate_z = 0.5f;
      cfg.scale_z = 0.5f;
   }
   agx_ppp_push_packed(&ppp, &uvs->osel, OUTPUT_SELECT);
   offsets->varying_counts_32_offset = (unsigned)(ppp.head - encoder);
   agx_ppp_push_packed(&ppp, &varyings.counts_32, VARYING_COUNTS);
   offsets->varying_counts_16_offset = (unsigned)(ppp.head - encoder);
   agx_ppp_push_packed(&ppp, &varyings.counts_16, VARYING_COUNTS);
   agx_ppp_push(&ppp, CULL, cfg) {
      cfg.flat_shading_vertex = AGX_PPP_VERTEX_2;
      cfg.depth_clip = true;
      cfg.front_face_ccw = true;
   }
   agx_ppp_push(&ppp, CULL_2, cfg)
      cfg.clamp_w = true;
   agx_ppp_push(&ppp, FRAGMENT_SHADER_WORD_0, cfg) {
      cfg.uniform_register_count = fs->info.push_count;
      cfg.preshader_register_count = 0u;
      cfg.texture_state_register_count = 0u;
      cfg.sampler_state_register_count = AGX_SAMPLER_STATES_0;
      cfg.cf_binding_count = 0u;
   }
   offsets->fragment_pipeline_offset = (unsigned)(ppp.head - encoder);
   agx_ppp_push(&ppp, FRAGMENT_SHADER_WORD_1, cfg)
      cfg.pipeline = 0u;
   agx_ppp_push(&ppp, FRAGMENT_SHADER_WORD_2, cfg)
      cfg.cf_bindings = 0u;
   agx_ppp_push(&ppp, FRAGMENT_SHADER_WORD_3, cfg)
      ;
   agx_ppp_push(&ppp, OUTPUT_SIZE, cfg)
      cfg.count = uvs->size;
   if ((unsigned)(ppp.head - (encoder + offsets->ppp_offset)) !=
       offsets->ppp_bytes)
      return 0;
   head = ppp.head;
   offsets->encoder_bytes = (unsigned)(head - encoder);

   {
      struct AGX_PPP_STATE state = {
         .size_words = offsets->ppp_bytes / 4u,
      };
      AGX_PPP_STATE_pack(
         (uint32_t *)(encoder + offsets->ppp_state_address_offset), &state);
   }

   {
      struct AGX_SCISSOR state = {
         .max_x = FRAME_WIDTH,
         .min_x = 0u,
         .max_y = FRAME_HEIGHT,
         .min_y = 0u,
         .min_z = 0.0f,
         .max_z = 1.0f,
      };
      AGX_SCISSOR_pack((uint32_t *)scissor, &state);
   }
   {
      struct AGX_DEPTH_BIAS state = {0};
      AGX_DEPTH_BIAS_pack((uint32_t *)depth_bias, &state);
   }

   struct AGX_USC_SHADER usc_shader;
   struct AGX_VDM_STATE_VERTEX_SHADER_WORD_1 vdm_word;
   struct AGX_PPP_STATE ppp_state;
   struct AGX_FRAGMENT_SHADER_WORD_1 fragment_word;
   struct AGX_VARYING_COUNTS varying_counts_32;
   struct AGX_VARYING_COUNTS varying_counts_16;
   struct AGX_INDEX_LIST index_list;
   struct AGX_INDEX_LIST_COUNT index_count;
   struct AGX_INDEX_LIST_INSTANCES instance_count;
   struct AGX_VDM_STREAM_TERMINATE terminate;
   if (!AGX_USC_SHADER_unpack(NULL, pipeline + offsets->vs_shader_offset,
                              &usc_shader) ||
       usc_shader.code != 0u || usc_shader.unk_2 != 3u ||
       !AGX_USC_SHADER_unpack(NULL, pipeline + offsets->fs_shader_offset,
                              &usc_shader) ||
       usc_shader.code != 0u || usc_shader.unk_2 != 2u ||
       !AGX_VDM_STATE_VERTEX_SHADER_WORD_1_unpack(
          NULL, encoder + offsets->vdm_pipeline_offset, &vdm_word) ||
       vdm_word.pipeline != 0u ||
       !AGX_PPP_STATE_unpack(NULL,
                             encoder + offsets->ppp_state_address_offset,
                             &ppp_state) ||
       ppp_state.pointer_hi != 0u || ppp_state.pointer_lo != 0u ||
       ppp_state.size_words != offsets->ppp_bytes / 4u ||
       !AGX_VARYING_COUNTS_unpack(
          NULL, encoder + offsets->varying_counts_32_offset,
          &varying_counts_32) ||
       varying_counts_32.smooth != uvs->user_size ||
       varying_counts_32.flat != 0u || varying_counts_32.linear != 0u ||
       !AGX_VARYING_COUNTS_unpack(
          NULL, encoder + offsets->varying_counts_16_offset,
          &varying_counts_16) ||
       varying_counts_16.smooth != 0u || varying_counts_16.flat != 0u ||
       varying_counts_16.linear != 0u ||
       !AGX_FRAGMENT_SHADER_WORD_1_unpack(
          NULL, encoder + offsets->fragment_pipeline_offset,
          &fragment_word) || fragment_word.pipeline != 0u ||
       !AGX_INDEX_LIST_unpack(NULL, encoder + offsets->draw_offset,
                              &index_list) ||
       index_list.primitive != AGX_PRIMITIVE_TRIANGLES ||
       !index_list.index_count_present ||
       !index_list.instance_count_present || !index_list.start_present ||
       !AGX_INDEX_LIST_COUNT_unpack(
          NULL, encoder + offsets->draw_offset + AGX_INDEX_LIST_LENGTH,
          &index_count) ||
       index_count.count != 3u ||
       !AGX_INDEX_LIST_INSTANCES_unpack(
          NULL, encoder + offsets->draw_offset + AGX_INDEX_LIST_LENGTH +
                   AGX_INDEX_LIST_COUNT_LENGTH,
          &instance_count) ||
       instance_count.count != 1u ||
       !AGX_VDM_STREAM_TERMINATE_unpack(
          NULL, encoder + offsets->terminate_offset, &terminate))
      return 0;
   offsets->varying_smooth_32 = varying_counts_32.smooth;
   offsets->varying_flat_32 = varying_counts_32.flat;
   offsets->varying_linear_32 = varying_counts_32.linear;
   offsets->varying_total_16 = varying_counts_16.smooth +
                               varying_counts_16.flat +
                               varying_counts_16.linear;
   if (fs->info.rodata.size_16 != 0u) {
      struct AGX_USC_UNIFORM uniform;
      if (!AGX_USC_UNIFORM_unpack(NULL,
                                  pipeline + offsets->fs_uniform_offset,
                                  &uniform) || uniform.buffer != 0u)
         return 0;
   }
   if (vs->info.rodata.size_16 != 0u) {
      struct AGX_USC_UNIFORM uniform;
      if (!AGX_USC_UNIFORM_unpack(NULL,
                                  pipeline + offsets->vs_uniform_offset,
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
   struct agx_shader_key vs_key = {.promote_constants = true};
   struct agx_shader_key fs_key = {
      .promote_constants = true,
      .no_stop = true,
   };
   struct agx_shader_part vs = {0};
   struct agx_shader_part fs = {0};
   struct linked_fragment linked_fs = {0};
   _Alignas(8) unsigned char pipeline[PIPELINE_CAPACITY];
   _Alignas(8) unsigned char encoder[ENCODER_CAPACITY];
   _Alignas(8) unsigned char scissor[AGX_SCISSOR_LENGTH];
   _Alignas(8) unsigned char depth_bias[AGX_DEPTH_BIAS_LENGTH];
   struct encoder_offsets offsets;
   glsl_type_singleton_init_or_ref();
   nir_shader *vs_nir = build_vertex(variant, &uvs);
   nir_shader *fs_nir = build_fragment(variant, &epilog);
   agx_compile_shader_nir(vs_nir, &vs_key, &vs);
   agx_compile_shader_nir(fs_nir, &fs_key, &fs);
   if (!vs.binary || !vs.info.binary_size || !fs.binary ||
       !fs.info.binary_size || !link_fragment(&fs, &epilog, &linked_fs) ||
       !write_binary(argv[1], "vertex", &vs) ||
       !write_binary(argv[1], "fragment", &fs) ||
       !write_bytes(argv[1], "fragment-linked", linked_fs.binary,
                    linked_fs.binary_bytes) ||
       !write_disassembly(argv[1], "vertex", &vs) ||
       !write_disassembly(argv[1], "fragment", &fs) ||
       !build_encoder_objects(&vs, &fs, &linked_fs, &uvs, pipeline, encoder,
                              scissor, depth_bias, &offsets) ||
       !write_bytes(argv[1], "pipeline", pipeline, offsets.pipeline_bytes) ||
       !write_bytes(argv[1], "encoder", encoder, offsets.encoder_bytes) ||
       !write_bytes(argv[1], "scissor", scissor, sizeof(scissor)) ||
       !write_bytes(argv[1], "depth_bias", depth_bias, sizeof(depth_bias)))
      return 1;
   struct agx_shader_part linked_fs_part = {
      .info = {
         .binary_size = linked_fs.binary_bytes,
         .main_size = linked_fs.binary_bytes,
      },
      .binary = linked_fs.binary,
   };
   if (!write_disassembly(argv[1], "fragment-linked", &linked_fs_part))
      return 1;
   printf("{\"schema\":2,\"variant\":%u,\"uvs_size\":%u,"
          "\"uvs_user_size\":%u,\"epilog_loc_written\":%u,"
          "\"pipeline_bytes\":%u,\"encoder_bytes\":%u,\"ppp_bytes\":%u,"
          "\"draw\":{\"topology\":\"triangle-list\","
          "\"vertex_count\":3,\"instance_count\":1,"
          "\"stream_terminated\":true},"
          "\"viewport\":{\"width\":%u,\"height\":%u,"
          "\"scissor_count\":1,\"depth_bias_count\":1},"
          "\"varying_counts\":{\"published_32\":true,"
          "\"published_16\":true,\"smooth_32\":%u,\"flat_32\":%u,"
          "\"linear_32\":%u,\"total_16\":%u},"
          "\"render_pass\":{"
          "\"owner\":\"EXP208-hardware-proven-3D-skeleton\","
          "\"dynamic_scope\":\"VDM-PPP-USC\","
          "\"store_pipeline_reused\":true},"
          "\"generated_unpack_valid\":true,"
          "\"relocations\":["
          "{\"kind\":\"UscBufferAddress40\",\"destination\":%u,"
          "\"target\":\"vertex.rodata\"},"
          "{\"kind\":\"UscShaderOffset32\",\"destination\":%u,"
          "\"target\":\"vertex.main\"},"
          "{\"kind\":\"UscBufferAddress40\",\"destination\":%u,"
          "\"target\":\"fragment.rodata\"},"
          "{\"kind\":\"UscShaderOffset32\",\"destination\":%u,"
          "\"target\":\"fragment-linked\"},"
          "{\"kind\":\"VdmPipelineOffset32\",\"destination\":%u,"
          "\"target_offset\":%u},"
          "{\"kind\":\"PppStateAddress40\",\"destination\":%u,"
          "\"target\":\"encoder.ppp\",\"target_offset\":%u},"
          "{\"kind\":\"VdmPipelineOffset32\",\"destination\":%u,"
          "\"target_offset\":%u}],",
          variant, uvs.size, uvs.user_size, epilog.loc_written,
          offsets.pipeline_bytes, offsets.encoder_bytes, offsets.ppp_bytes,
          FRAME_WIDTH, FRAME_HEIGHT, offsets.varying_smooth_32,
          offsets.varying_flat_32, offsets.varying_linear_32,
          offsets.varying_total_16,
          offsets.vs_uniform_offset, offsets.vs_shader_offset,
          offsets.fs_uniform_offset,
          offsets.fs_shader_offset, offsets.vdm_pipeline_offset,
          offsets.vs_pipeline_offset, offsets.ppp_state_address_offset,
          offsets.ppp_offset, offsets.fragment_pipeline_offset,
          offsets.fs_pipeline_offset);
   print_part("vertex", &vs);
   printf(",");
   print_part("fragment", &fs);
   printf(",\"fragment_linked\":{\"binary_bytes\":%u,"
          "\"gprs\":%u,\"reads_tib\":%s,\"writes_sample_mask\":%s,"
          "\"tag_write_disable\":%s,\"fnv1a64\":\"0x%016llx\"}",
          linked_fs.binary_bytes, linked_fs.register_count,
          linked_fs.reads_tib ? "true" : "false",
          linked_fs.writes_sample_mask ? "true" : "false",
          linked_fs.tag_write_disable ? "true" : "false",
          (unsigned long long)hash_bytes(linked_fs.binary,
                                         linked_fs.binary_bytes));
   printf("}\n");
   free(linked_fs.binary);
   free(vs.binary);
   free(fs.binary);
   ralloc_free(vs_nir);
   ralloc_free(fs_nir);
   glsl_type_singleton_decref();
   return 0;
}
