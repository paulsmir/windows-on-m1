/* D3D12 C44: DXIL -> SPIR-V (dxil-spirv, checked C boundary) -> NIR
 * (spirv_to_nir, Vulkan environment) -> the DXBC frontend's Gallium-slot
 * conventions (agx_dxil_to_nir.h). SPDX-License-Identifier: MIT */
#include "agx_dxil_to_nir.h"
#include "agx_dxil_checked.h"
#include "agx_dxil_program.h"
#include "../dxbc/dxbc_to_nir.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_xfb_info.h"
#include "util/set.h"
#include "compiler/spirv/nir_spirv.h"
#include "compiler/spirv/spirv_info.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* descriptor sets of the SPIR-V: the binding kind; bindings: Gallium slots */
enum { SET_CBV = DXBC_BINDING_CBV, SET_SRV = DXBC_BINDING_SRV, SET_UAV = DXBC_BINDING_UAV,
       SET_SAMPLER = DXBC_BINDING_SAMPLER };

struct declaration {
   uint32_t kind, id, space, reg, range, resource_kind, slot;
   unsigned counter;
   unsigned heap; /* C47: a heap range (slot = its heap range index) */
};

/* C47 heap ranges reach the SPIR-V at binding HEAP_BINDING + heap range, a
 * heap structured UAV's counter at HEAP_COUNTER_BINDING + heap range */
#define HEAP_BINDING 0x1000u
#define HEAP_COUNTER_BINDING 0x2000u

struct job {
   struct declaration decl[DXBC_BINDING_MAP_MAX];
   unsigned decls;
   uint32_t limits[DXBC_BINDING_KIND_COUNT];
   enum agx_dxil_nir_status refusal;
   struct dxbc_binding_map map;
   char *error;
};

static void
refuse(struct job *job, enum agx_dxil_nir_status status, const char *what, const dxil_spv_d3d_binding *b)
{
   if (job->refusal != AGX_DXIL_NIR_OK)
      return;
   job->refusal = status;
   snprintf(job->error, sizeof(((struct agx_dxil_nir_result *)0)->error), "%s (space %u register %u range %u kind %u)",
            what, b ? b->register_space : 0u, b ? b->register_index : 0u, b ? b->range_size : 0u,
            b ? (unsigned)b->kind : 0u);
}

static struct declaration *
find(struct job *job, uint32_t kind, const dxil_spv_d3d_binding *b)
{
   for (unsigned i = 0; i < job->decls; ++i)
      if (job->decl[i].kind == kind && job->decl[i].space == b->register_space && job->decl[i].reg == b->register_index)
         return &job->decl[i];
   return NULL;
}

static bool
raw_buffer(dxil_spv_resource_kind kind)
{
   return kind == DXIL_SPV_RESOURCE_KIND_RAW_BUFFER || kind == DXIL_SPV_RESOURCE_KIND_STRUCTURED_BUFFER;
}

/* scan pass: every declaration once */
static void
collect(struct job *job, uint32_t kind, const dxil_spv_d3d_binding *b, unsigned counter)
{
   if (!b)
      return;
   if (!b->range_size) {
      refuse(job, AGX_DXIL_NIR_INVALID, "empty register range", b);
      return;
   }
   if (b->kind == DXIL_SPV_RESOURCE_KIND_RT_ACCELERATION_STRUCTURE ||
       b->kind == DXIL_SPV_RESOURCE_KIND_FEEDBACK_TEXTURE_2D ||
       b->kind == DXIL_SPV_RESOURCE_KIND_FEEDBACK_TEXTURE_2D_ARRAY) {
      refuse(job, AGX_DXIL_NIR_UNSUPPORTED, "resource kind", b);
      return;
   }
   struct declaration *old = find(job, kind, b);
   if (old) {
      old->counter |= counter;
      return;
   }
   if (job->decls == DXBC_BINDING_MAP_MAX) {
      refuse(job, AGX_DXIL_NIR_LIMIT, "too many declarations", b);
      return;
   }
   job->decl[job->decls++] = (struct declaration){kind, b->resource_index, b->register_space, b->register_index,
                                                  b->range_size, (uint32_t)b->kind, 0, counter, 0};
}

static dxil_spv_bool
scan_srv(void *u, const dxil_spv_d3d_binding *b, dxil_spv_srv_vulkan_binding *v)
{
   memset(v, 0, sizeof(*v));
   collect(u, DXBC_BINDING_SRV, b, 0);
   return DXIL_SPV_TRUE;
}
static dxil_spv_bool
scan_sampler(void *u, const dxil_spv_d3d_binding *b, dxil_spv_vulkan_binding *v)
{
   memset(v, 0, sizeof(*v));
   collect(u, DXBC_BINDING_SAMPLER, b, 0);
   return DXIL_SPV_TRUE;
}
static dxil_spv_bool
scan_cbv(void *u, const dxil_spv_d3d_binding *b, dxil_spv_cbv_vulkan_binding *v)
{
   memset(v, 0, sizeof(*v));
   collect(u, DXBC_BINDING_CBV, b, 0);
   return DXIL_SPV_TRUE;
}
static dxil_spv_bool
scan_uav(void *u, const dxil_spv_uav_d3d_binding *b, dxil_spv_uav_vulkan_binding *v)
{
   memset(v, 0, sizeof(*v));
   collect(u, DXBC_BINDING_UAV, b ? &b->d3d_binding : NULL, b ? b->has_counter : 0);
   return DXIL_SPV_TRUE;
}

static int
earlier(const struct declaration *a, const struct declaration *b)
{
   if (a->kind != b->kind)
      return a->kind < b->kind;
   if (a->space != b->space)
      return a->space < b->space;
   return a->reg < b->reg;
}

/* dense slots per kind in (space, register) order, one per register */
static void
freeze(struct job *job)
{
   if (job->refusal != AGX_DXIL_NIR_OK)
      return;
   for (unsigned i = 1; i < job->decls; ++i) {
      struct declaration d = job->decl[i];
      unsigned j = i;
      while (j && earlier(&d, &job->decl[j - 1])) {
         job->decl[j] = job->decl[j - 1];
         --j;
      }
      job->decl[j] = d;
   }
   uint32_t counts[DXBC_BINDING_KIND_COUNT] = {0};
   memset(&job->map, 0, sizeof(job->map));
   for (unsigned i = 0; i < job->decls; ++i) {
      struct declaration *d = &job->decl[i];
      const uint32_t limit = job->limits[d->kind] & ~DXBC_BINDING_HEAP_RANGES;
      /* C47 (DXBC frontend rule): an unbounded SRV/sampler/UAV range, a
       * UAV range (no dynamic index into slots), or one the free slots
       * cannot take, reads the descriptor heap */
      const bool heap_kind = (job->limits[d->kind] & DXBC_BINDING_HEAP_RANGES) && d->kind != DXBC_BINDING_CBV;
      if (heap_kind && (d->range == UINT32_MAX || (d->kind == DXBC_BINDING_UAV && d->range > 1) ||
                        d->range > limit - counts[d->kind])) {
         d->heap = 1;
         continue;
      }
      if (d->range == UINT32_MAX) {
         const dxil_spv_d3d_binding b = {DXIL_SPV_STAGE_UNKNOWN, d->resource_kind, d->id, d->space, d->reg, d->range, 0};
         refuse(job, AGX_DXIL_NIR_UNSUPPORTED, "unbounded register range outside the heap", &b);
         return;
      }
      if (d->range > limit - counts[d->kind] || d->range > DXBC_BINDING_MAP_MAX - job->map.count ||
          (d->counter && (d->range != 1 || counts[d->kind] >= DXBC_NIR_MAX_UAVS))) {
         const dxil_spv_d3d_binding b = {DXIL_SPV_STAGE_UNKNOWN, d->resource_kind, d->id, d->space, d->reg, d->range, 0};
         refuse(job, AGX_DXIL_NIR_LIMIT, "binding slots exhausted", &b);
         return;
      }
      d->slot = counts[d->kind];
      for (uint32_t k = 0; k < d->range; ++k)
         job->map.bindings[job->map.count++] =
            (struct dxbc_binding){d->kind, d->id, d->space, d->reg + k, counts[d->kind]++, 0u};
   }
   /* heap range indices in the DXBC frontend's declaration order (FXC
    * declares samplers before resources), (space, register) within a kind */
   static const uint32_t heap_order[] = {DXBC_BINDING_SAMPLER, DXBC_BINDING_SRV, DXBC_BINDING_UAV};
   unsigned ranges = 0;
   for (unsigned k = 0; k < ARRAY_SIZE(heap_order); ++k) {
      for (unsigned i = 0; i < job->decls; ++i) {
         struct declaration *d = &job->decl[i];
         if (!d->heap || d->kind != heap_order[k])
            continue;
         if (ranges == DXBC_HEAP_RANGE_MAX || job->map.count == DXBC_BINDING_MAP_MAX) {
            const dxil_spv_d3d_binding b = {DXIL_SPV_STAGE_UNKNOWN, d->resource_kind, d->id, d->space, d->reg, d->range, 0};
            refuse(job, AGX_DXIL_NIR_LIMIT, "heap ranges exhausted", &b);
            return;
         }
         d->slot = ranges++;
         job->map.bindings[job->map.count++] =
            (struct dxbc_binding){d->kind, d->id, d->space, d->reg, d->slot, DXBC_BINDING_HEAP};
      }
   }
   job->map.active = 1;
}

static const struct declaration *
placed(struct job *job, uint32_t kind, const dxil_spv_d3d_binding *b)
{
   const struct declaration *d = b ? find(job, kind, b) : NULL;
   if (!d || d->range != b->range_size) {
      refuse(job, AGX_DXIL_NIR_FAILED, "binding not scanned", b);
      return NULL;
   }
   return d;
}

static dxil_spv_bool
remap_srv(void *u, const dxil_spv_d3d_binding *b, dxil_spv_srv_vulkan_binding *v)
{
   memset(v, 0, sizeof(*v));
   const struct declaration *d = placed(u, DXBC_BINDING_SRV, b);
   if (!d)
      return DXIL_SPV_FALSE;
   v->buffer_binding.set = SET_SRV;
   v->buffer_binding.binding = d->heap ? HEAP_BINDING + d->slot : d->slot;
   /* the owner binds raw and structured SRVs as R32_UINT buffer views */
   v->buffer_binding.descriptor_type =
      raw_buffer(b->kind) ? DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_TEXEL_BUFFER : DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_IDENTITY;
   return DXIL_SPV_TRUE;
}
static dxil_spv_bool
remap_sampler(void *u, const dxil_spv_d3d_binding *b, dxil_spv_vulkan_binding *v)
{
   memset(v, 0, sizeof(*v));
   const struct declaration *d = placed(u, DXBC_BINDING_SAMPLER, b);
   if (!d)
      return DXIL_SPV_FALSE;
   v->set = SET_SAMPLER;
   v->binding = d->heap ? HEAP_BINDING + d->slot : d->slot;
   return DXIL_SPV_TRUE;
}
static dxil_spv_bool
remap_cbv(void *u, const dxil_spv_d3d_binding *b, dxil_spv_cbv_vulkan_binding *v)
{
   memset(v, 0, sizeof(*v));
   const struct declaration *d = placed(u, DXBC_BINDING_CBV, b);
   if (!d)
      return DXIL_SPV_FALSE;
   v->vulkan.uniform_binding.set = SET_CBV;
   v->vulkan.uniform_binding.binding = d->slot;
   v->vulkan.uniform_binding.descriptor_type = DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_UBO;
   return DXIL_SPV_TRUE;
}
static dxil_spv_bool
remap_uav(void *u, const dxil_spv_uav_d3d_binding *b, dxil_spv_uav_vulkan_binding *v)
{
   memset(v, 0, sizeof(*v));
   const struct declaration *d = placed(u, DXBC_BINDING_UAV, b ? &b->d3d_binding : NULL);
   if (!d)
      return DXIL_SPV_FALSE;
   v->buffer_binding.set = SET_UAV;
   if (d->heap) {
      /* C47: heap UAVs are images -- raw and structured ones R32_UINT texel
       * buffers, as the DXBC frontend's heap UAVs -- and a counter the
       * R32_UINT word of the heap form's counter PBE */
      v->buffer_binding.binding = HEAP_BINDING + d->slot;
      v->buffer_binding.descriptor_type =
         raw_buffer(b->d3d_binding.kind) ? DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_TEXEL_BUFFER : DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_IDENTITY;
      if (b->has_counter) {
         v->counter_binding.set = SET_UAV;
         v->counter_binding.binding = HEAP_COUNTER_BINDING + d->slot;
         v->counter_binding.descriptor_type = DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_TEXEL_BUFFER;
      }
      return DXIL_SPV_TRUE;
   }
   v->buffer_binding.binding = d->slot;
   v->buffer_binding.descriptor_type =
      raw_buffer(b->d3d_binding.kind) ? DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_SSBO : DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_IDENTITY;
   if (b->has_counter) {
      /* one 32-bit word at offset 0 of shader buffer 8 + u (DXBC frontend) */
      v->counter_binding.set = SET_UAV;
      v->counter_binding.binding = DXBC_NIR_UAV_COUNTER_SLOT + d->slot;
      v->counter_binding.descriptor_type = DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_SSBO;
   }
   return DXIL_SPV_TRUE;
}

int
agx_dxil_is_container(const void *data, size_t bytes)
{
   const uint8_t *p = data;
   if (!p || bytes < 32 || memcmp(p, "DXBC", 4))
      return 0;
   uint32_t parts;
   memcpy(&parts, p + 28, 4);
   if (parts > (bytes - 32) / 4)
      return 0;
   for (uint32_t i = 0; i < parts; ++i) {
      uint32_t offset;
      memcpy(&offset, p + 32 + 4 * i, 4);
      if (offset > bytes - 8)
         return 0;
      if (!memcmp(p + offset, "DXIL", 4))
         return 1;
   }
   return 0;
}

/* the program the D3D12 runtime hands CreateShader (a DXIL program part:
 * version token, size in dwords, "DXIL" and the bitcode header), or a whole
 * container; any SM 6.x model, every graphics/compute kind */
static const struct agx_dxil_profile any_sm6 = {0x3f, 0x60, 0x6f};

int
agx_dxil_is_program(const void *data, size_t bytes)
{
   struct agx_dxil_program program;
   return agx_dxil_parse(data, bytes, AGX_DXIL_INPUT_PROGRAM, &any_sm6, &program) == AGX_DXIL_PARSE_OK;
}

static mesa_shader_stage
nir_stage(dxil_spv_shader_stage stage)
{
   switch (stage) {
   case DXIL_SPV_STAGE_VERTEX: return MESA_SHADER_VERTEX;
   case DXIL_SPV_STAGE_PIXEL: return MESA_SHADER_FRAGMENT;
   case DXIL_SPV_STAGE_COMPUTE: return MESA_SHADER_COMPUTE;
   case DXIL_SPV_STAGE_GEOMETRY: return MESA_SHADER_GEOMETRY;
   case DXIL_SPV_STAGE_HULL: return MESA_SHADER_TESS_CTRL;
   case DXIL_SPV_STAGE_DOMAIN: return MESA_SHADER_TESS_EVAL;
   default: return MESA_SHADER_NONE;
   }
}

/* Vulkan buffer descriptors -> slot: (set, binding + array index) becomes
 * vec2(slot, 0) in nir_address_format_32bit_index_offset, so
 * nir_lower_explicit_io emits load_ubo/load_ssbo/store_ssbo/ssbo_atomic
 * with the slot as the buffer index, as the DXBC frontend does */
static bool
lower_buffer_slot(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   bool *bad = data;
   b->cursor = nir_before_instr(&intr->instr);
   nir_def *value;
   switch (intr->intrinsic) {
   case nir_intrinsic_vulkan_resource_index: {
      const unsigned set = nir_intrinsic_desc_set(intr);
      const nir_descriptor_type type = nir_intrinsic_desc_type(intr);
      if (!((set == SET_CBV && type == nir_descriptor_type_uniform_buffer) ||
            (set == SET_UAV && type == nir_descriptor_type_storage_buffer)))
         *bad = true;
      value = nir_vec2(b, nir_iadd_imm(b, intr->src[0].ssa, nir_intrinsic_binding(intr)), nir_imm_int(b, 0));
      break;
   }
   case nir_intrinsic_vulkan_resource_reindex:
      value = nir_vec2(b, nir_iadd(b, nir_channel(b, intr->src[0].ssa, 0), intr->src[1].ssa),
                       nir_channel(b, intr->src[0].ssa, 1));
      break;
   case nir_intrinsic_load_vulkan_descriptor:
      value = intr->src[0].ssa;
      break;
   default:
      return false;
   }
   nir_def_rewrite_uses(&intr->def, value);
   nir_instr_remove(&intr->instr);
   return true;
}

static nir_def *heap_range(nir_builder *b, unsigned range);

/* image_deref_* -> the indexed image_* intrinsics on the UAV image slot (the
 * DXBC frontend never emits image_deref_*, EXP1304) */
static bool
lower_image_slot(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   bool *bad = data;
   switch (intr->intrinsic) {
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples:
      break;
   default:
      return false;
   }
   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   if (!var || var->data.descriptor_set != SET_UAV) {
      *bad = true;
      return false;
   }
   b->cursor = nir_before_instr(&intr->instr);
   /* typed loads/stores take the format of the view (the DXBC frontend's
    * format NONE); dxil-spirv names R32 formats for typed UAV loads */
   const bool atomic = intr->intrinsic == nir_intrinsic_image_deref_atomic ||
                       intr->intrinsic == nir_intrinsic_image_deref_atomic_swap;
   if (var->data.binding >= HEAP_BINDING) {
      /* C47: the heap form's texture descriptor reads and sizes, its PBE
       * writes and atomics (Asahi's image convention), a counter's word
       * through the counter PBE */
      const bool counter = var->data.binding >= HEAP_COUNTER_BINDING;
      const unsigned range = var->data.binding - (counter ? HEAP_COUNTER_BINDING : HEAP_BINDING);
      nir_def *element = deref->deref_type == nir_deref_type_array ? nir_u2u32(b, deref->arr.index.ssa) : nir_imm_int(b, 0);
      nir_def *heap = nir_iadd(b, nir_channel(b, heap_range(b, range), 0), element);
      const bool read = intr->intrinsic == nir_intrinsic_image_deref_load || intr->intrinsic == nir_intrinsic_image_deref_size ||
                        intr->intrinsic == nir_intrinsic_image_deref_samples;
      const unsigned delta = counter ? DXBC_HEAP_DESCRIPTOR_COUNTER_DELTA : read ? 0u : DXBC_HEAP_DESCRIPTOR_PBE_DELTA;
      nir_def *bytes = nir_iadd_imm(b, nir_imul_imm(b, heap, DXBC_HEAP_DESCRIPTOR_STRIDE), DXBC_HEAP_DESCRIPTOR_AGX_OFFSET + delta);
      nir_rewrite_image_intrinsic(intr, nir_bindless_image_agx(b, bytes, .desc_set = DXBC_HEAP_HANDLE_MARK),
                                  nir_image_intrinsic_type_bindless);
      if (!atomic)
         nir_intrinsic_set_format(intr, PIPE_FORMAT_NONE);
      return true;
   }
   nir_def *index = nir_imm_int(b, var->data.binding);
   if (deref->deref_type == nir_deref_type_array)
      index = nir_iadd(b, index, nir_u2u32(b, deref->arr.index.ssa));
   nir_rewrite_image_intrinsic(intr, index, nir_image_intrinsic_type_default);
   /* nir_rewrite_image_intrinsic leaves a stale range_base */
   nir_intrinsic_set_range_base(intr, 0);
   return true;
}

/* C47 heap ranges, as the DXBC frontend reads them (dxbc_bindings.h): the
 * range's vec4 in DXBC_HEAP_TABLE_CB (.x heap index of its first register,
 * .y the sampler-map base), the heap descriptor's marked bindless handle */
static nir_def *
heap_range(nir_builder *b, unsigned range)
{
   return nir_load_ubo(b, 4, 32, nir_imm_int(b, DXBC_HEAP_TABLE_CB), nir_imm_int(b, (int)(range * 16u)),
                       .align_mul = 16, .align_offset = 0, .range_base = 0, .range = ~0u);
}

/* the heap variable a texture/sampler deref names: its heap range and the
 * array index (0 for a single descriptor); false for a slot variable */
static bool
heap_deref(nir_builder *b, nir_deref_instr *deref, unsigned *range, nir_def **index)
{
   nir_variable *var = deref ? nir_deref_instr_get_variable(deref) : NULL;
   if (!var || var->data.binding < HEAP_BINDING)
      return false;
   *range = var->data.binding - HEAP_BINDING;
   *index = deref->deref_type == nir_deref_type_array ? nir_u2u32(b, deref->arr.index.ssa) : nir_imm_int(b, 0);
   return true;
}

static bool
lower_heap_tex(nir_builder *b, nir_tex_instr *tex, void *data)
{
   (void)data;
   b->cursor = nir_before_instr(&tex->instr);
   bool progress = false;
   unsigned range;
   nir_def *index;
   const int t = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (t >= 0 && heap_deref(b, nir_src_as_deref(tex->src[t].src), &range, &index)) {
      nir_def *heap = nir_iadd(b, nir_channel(b, heap_range(b, range), 0), index);
      nir_def *bytes = nir_iadd_imm(b, nir_imul_imm(b, heap, DXBC_HEAP_DESCRIPTOR_STRIDE), DXBC_HEAP_DESCRIPTOR_AGX_OFFSET);
      nir_tex_instr_remove_src(tex, t);
      nir_tex_instr_add_src(tex, nir_tex_src_texture_handle, nir_bindless_image_agx(b, bytes, .desc_set = DXBC_HEAP_HANDLE_MARK));
      tex->texture_index = 0;
      progress = true;
   }
   const int sidx = nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   if (sidx >= 0 && heap_deref(b, nir_src_as_deref(tex->src[sidx].src), &range, &index)) {
      /* the map entry's AGX sampler handle (bits 0-15) and MipLODBias (fp16,
       * bits 16-31), which the shader applies as the DXBC frontend does */
      nir_def *r = heap_range(b, range);
      nir_def *entry = nir_isub(b, nir_iadd(b, nir_channel(b, r, 0), index), nir_channel(b, r, 1));
      nir_def *word = nir_load_ubo(b, 1, 32, nir_imm_int(b, DXBC_HEAP_TABLE_CB),
                                   nir_iadd_imm(b, nir_ishl_imm(b, entry, 2), DXBC_HEAP_SAMPLER_MAP_OFFSET),
                                   .align_mul = 4, .align_offset = 0, .range_base = 0, .range = ~0u);
      nir_def *bias = nir_f2f32(b, nir_u2u16(b, nir_ushr_imm(b, word, 16)));
      nir_tex_instr_remove_src(tex, sidx);
      nir_tex_instr_add_src(tex, nir_tex_src_sampler_handle, nir_u2u16(b, word));
      tex->sampler_index = DXBC_HEAP_SAMPLER_INDEX;
      if (tex->op == nir_texop_tex) {
         tex->op = nir_texop_txb;
         nir_tex_instr_add_src(tex, nir_tex_src_bias, bias);
      } else if (tex->op == nir_texop_txb) {
         const int i = nir_tex_instr_src_index(tex, nir_tex_src_bias);
         nir_def *sum = nir_fadd(b, tex->src[i].src.ssa, bias);
         nir_src_rewrite(&tex->src[i].src, sum);
      } else if (tex->op == nir_texop_txd) {
         nir_def *scale = nir_fexp2(b, bias);
         for (unsigned i = 0; i < tex->num_srcs; ++i)
            if (tex->src[i].src_type == nir_tex_src_ddx || tex->src[i].src_type == nir_tex_src_ddy)
               nir_src_rewrite(&tex->src[i].src, nir_fmul(b, tex->src[i].src.ssa, scale));
      }
      progress = true;
   }
   return progress;
}

/* the slots a variable's (array of) descriptors spans */
static void
set_range(BITSET_WORD *set, unsigned bits, const nir_variable *var)
{
   const unsigned count = glsl_type_is_array(var->type) ? glsl_get_aoa_size(var->type) : 1u;
   for (unsigned i = 0; i < count && var->data.binding + i < bits; ++i)
      BITSET_SET(set, var->data.binding + i);
}

/* the DXBC frontend records the slots it uses: Asahi sizes its binding
 * tables from textures_used, samplers_used and images_used (gather_info
 * does not recompute them once nir_lower_samplers drops the derefs) */
static void
record_slots(nir_shader *nir)
{
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type == nir_instr_type_tex) {
               nir_tex_instr *tex = nir_instr_as_tex(instr);
               nir_deref_instr *texture = nir_get_tex_deref(tex, nir_tex_src_texture_deref);
               nir_deref_instr *sampler = nir_get_tex_deref(tex, nir_tex_src_sampler_deref);
               nir_variable *tv = texture ? nir_deref_instr_get_variable(texture) : NULL;
               nir_variable *sv = sampler ? nir_deref_instr_get_variable(sampler) : NULL;
               if (tv && tv->data.binding < HEAP_BINDING) {
                  set_range(nir->info.textures_used, BITSET_SIZE(nir->info.textures_used), tv);
                  if (tex->op == nir_texop_txf || tex->op == nir_texop_txf_ms)
                     set_range(nir->info.textures_used_by_txf, BITSET_SIZE(nir->info.textures_used_by_txf), tv);
               }
               if (sv && sv->data.binding < HEAP_BINDING)
                  set_range(nir->info.samplers_used, BITSET_SIZE(nir->info.samplers_used), sv);
            }
         }
      }
   }
   nir_foreach_variable_with_modes(var, nir, nir_var_image)
      if (var->data.binding < HEAP_BINDING)
         set_range(nir->info.images_used, BITSET_SIZE(nir->info.images_used), var);
   /* EXP1859 C41B: Asahi clamps image indices to info.num_images - 1, which
    * nir_shader_gather_info counts from the image variables; a slot without
    * a typed UAV below the highest one (a raw UAV at u0) gets a placeholder,
    * as in the DXBC frontend (EXP1841) */
   const unsigned span = BITSET_LAST_BIT(nir->info.images_used);
   for (unsigned u = 0; u < span; ++u) {
      bool covered = false;
      nir_foreach_variable_with_modes(var, nir, nir_var_image) {
         const unsigned count = glsl_type_is_array(var->type) ? glsl_get_aoa_size(var->type) : 1;
         covered |= var->data.binding < HEAP_BINDING && u >= var->data.binding && u < var->data.binding + count;
      }
      if (covered)
         continue;
      nir_variable *gap = nir_variable_create(nir, nir_var_image,
                                              glsl_image_type(GLSL_SAMPLER_DIM_2D, false, GLSL_TYPE_FLOAT), "u_unused");
      gap->data.binding = u;
      gap->data.image.format = PIPE_FORMAT_NONE;
   }
}

/* num_ssbos spans every shader-buffer slot the code names (counters at
 * DXBC_NIR_UAV_COUNTER_SLOT + u included), as the DXBC frontend sets it;
 * false for a dynamic buffer index (not translated yet) */
static bool
count_ssbos(nir_shader *nir)
{
   unsigned count = 0;
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            const int src = intr->intrinsic == nir_intrinsic_store_ssbo ? 1
                          : intr->intrinsic == nir_intrinsic_load_ssbo || intr->intrinsic == nir_intrinsic_ssbo_atomic ||
                                  intr->intrinsic == nir_intrinsic_ssbo_atomic_swap || intr->intrinsic == nir_intrinsic_get_ssbo_size
                             ? 0
                             : -1;
            if (src < 0)
               continue;
            if (!nir_src_is_const(intr->src[src]))
               return false;
            const unsigned index = nir_src_as_uint(intr->src[src]);
            if (index + 1 > count)
               count = index + 1;
         }
      }
   }
   nir->info.num_ssbos = count;
   return true;
}

/* texture and sampler variables keep their slots as Gallium bindings */
static bool
tex_sets_valid(nir_shader *nir)
{
   nir_foreach_variable_with_modes(var, nir, nir_var_uniform) {
      if (glsl_type_is_sampler(glsl_without_array(var->type)) && var->data.descriptor_set != SET_SAMPLER)
         return false;
      if (glsl_type_is_texture(glsl_without_array(var->type)) && var->data.descriptor_set != SET_SRV)
         return false;
   }
   return true;
}

static const struct spirv_capabilities caps = {
   .Shader = true, .Matrix = true, .Geometry = true, .Tessellation = true,
   .Float16 = true, .Int16 = true, .Int64 = true, .Int8 = true,
   .ImageQuery = true, .DerivativeControl = true, .SampledBuffer = true, .ImageBuffer = true,
   .Sampled1D = true, .Image1D = true, .SampledCubeArray = true, .ImageCubeArray = true,
   .ImageMSArray = true, .ImageGatherExtended = true, .StorageImageExtendedFormats = true,
   .StorageImageReadWithoutFormat = true, .StorageImageWriteWithoutFormat = true,
   .StorageImageMultisample = true, .SampleRateShading = true, .MinLod = true,
   .ClipDistance = true, .CullDistance = true, .DemoteToHelperInvocation = true,
   .GroupNonUniform = true, .GroupNonUniformVote = true, .GroupNonUniformArithmetic = true,
   .GroupNonUniformBallot = true, .GroupNonUniformShuffle = true,
   .GroupNonUniformShuffleRelative = true, .GroupNonUniformClustered = true,
   .GroupNonUniformQuad = true, .StorageBuffer16BitAccess = true,
   .UniformAndStorageBuffer16BitAccess = true, .ShaderViewportIndex = true, .ShaderLayer = true,
   /* register ranges: slots or (C47) the descriptor heap; vertex IDs */
   .DrawParameters = true, .RuntimeDescriptorArray = true, .ShaderNonUniform = true,
   .UniformBufferArrayDynamicIndexing = true, .SampledImageArrayDynamicIndexing = true,
   .StorageBufferArrayDynamicIndexing = true, .StorageImageArrayDynamicIndexing = true,
   .UniformTexelBufferArrayDynamicIndexing = true, .StorageTexelBufferArrayDynamicIndexing = true,
   .UniformBufferArrayNonUniformIndexing = true, .SampledImageArrayNonUniformIndexing = true,
   .StorageBufferArrayNonUniformIndexing = true, .StorageImageArrayNonUniformIndexing = true,
   .UniformTexelBufferArrayNonUniformIndexing = true, .StorageTexelBufferArrayNonUniformIndexing = true,
};

static enum agx_dxil_nir_status
from_result(dxil_spv_result r)
{
   switch (r) {
   case DXIL_SPV_SUCCESS: return AGX_DXIL_NIR_OK;
   case DXIL_SPV_ERROR_OUT_OF_MEMORY: return AGX_DXIL_NIR_OUT_OF_MEMORY;
   case DXIL_SPV_ERROR_UNSUPPORTED_FEATURE: return AGX_DXIL_NIR_UNSUPPORTED;
   case DXIL_SPV_ERROR_PARSER:
   case DXIL_SPV_ERROR_INVALID_ARGUMENT: return AGX_DXIL_NIR_INVALID;
   default: return AGX_DXIL_NIR_FAILED;
   }
}

static nir_shader *
spirv_nir(const uint32_t *words, size_t count, mesa_shader_stage stage, const char *entry,
          const struct nir_shader_compiler_options *options)
{
   const struct spirv_to_nir_options spirv = {
      .environment = NIR_SPIRV_VULKAN,
      .capabilities = &caps,
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = nir_address_format_32bit_index_offset,
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .push_const_addr_format = nir_address_format_32bit_offset,
      .shared_addr_format = nir_address_format_32bit_offset,
      .min_ubo_alignment = 16,
      .min_ssbo_alignment = 4,
      .group_non_uniform_subgroup_size = 32,
   };
   return spirv_to_nir(words, count, NULL, stage, entry, &spirv, options);
}

/* EXP1859: dxilconv declares SignedZeroInfNanPreserve, so spirv_to_nir marks
 * every float instruction preserve:sz,inf,nan; Asahi's agx_nir_lower_fminmax
 * turns fmin/fmax into fmin_agx/fmax_agx, which do not take those flags
 * (NIR validation failure, abort in the assertion-enabled x64 UMD: HDR,
 * ModelViewer). The DXBC frontend sets none: drop them as well. */
static bool
strip_preserve(nir_builder *b, nir_alu_instr *alu, void *data)
{
   (void)b;
   (void)data;
   if (!(alu->fp_math_ctrl & nir_fp_preserve_sz_inf_nan))
      return false;
   alu->fp_math_ctrl &= ~nir_fp_preserve_sz_inf_nan;
   return true;
}

/* the components of 'location' some stream-0 output variable covers */
static unsigned
output_components(nir_shader *nir, int location)
{
   unsigned covered = 0;
   nir_foreach_shader_out_variable(var, nir) {
      const struct glsl_type *type = var->type;
      unsigned rows = 1;
      if (glsl_type_is_array(type)) {
         rows = glsl_get_length(type);
         type = glsl_get_array_element(type);
      }
      if (var->data.stream != 0 || !glsl_type_is_vector_or_scalar(type) || glsl_type_is_64bit(type) ||
          location < var->data.location || location >= var->data.location + (int)rows)
         continue;
      covered |= ((1u << glsl_get_vector_elements(type)) - 1u) << var->data.location_frac;
   }
   return covered & 0xfu;
}

int
agx_dxil_nir_xfb(struct nir_shader *nir, const struct dxbc_nir_xfb *xfb)
{
   if (!nir || !xfb || !xfb->num_outputs || xfb->num_outputs > DXBC_NIR_MAX_XFB_OUTPUTS ||
       (nir->info.stage != MESA_SHADER_VERTEX && nir->info.stage != MESA_SHADER_GEOMETRY))
      return 0;
   nir_xfb_info *info = rzalloc_size(nir, nir_xfb_info_size(xfb->num_outputs));
   if (!info)
      return 0;
   for (unsigned i = 0; i < xfb->num_outputs; ++i) {
      const struct dxbc_nir_xfb_output *o = &xfb->outputs[i];
      const unsigned want = o->num_components && o->num_components <= 4 ?
         ((1u << o->num_components) - 1u) << o->start_component : 0u;
      if (!want || o->start_component + o->num_components > 4 || o->stream || o->reg >= 32 ||
          o->buffer >= DXBC_NIR_MAX_XFB_BUFFERS) {
         ralloc_free(info);
         return 0;
      }
      const int location = (xfb->position_registers & (1u << o->reg)) ? VARYING_SLOT_POS
                                                                        : VARYING_SLOT_VAR0 + (int)o->reg;
      if ((output_components(nir, location) & want) != want) {
         ralloc_free(info);
         return 0;
      }
      nir_xfb_output_info *out = &info->outputs[i];
      out->buffer = o->buffer;
      out->offset = o->dst_offset * 4u;
      out->location = (uint8_t)location;
      out->component_offset = o->start_component;
      out->component_mask = (uint8_t)want;
      info->buffers_written |= (uint8_t)BITFIELD_BIT(o->buffer);
      info->buffers[o->buffer].varying_count++;
   }
   info->streams_written = BITFIELD_BIT(0);
   for (unsigned buffer = 0; buffer < DXBC_NIR_MAX_XFB_BUFFERS; ++buffer) {
      info->buffer_to_stream[buffer] = 0;
      if (info->buffers_written & BITFIELD_BIT(buffer)) {
         info->buffers[buffer].stride = (uint16_t)(xfb->stride[buffer] * 4u);
         nir->info.xfb_stride[buffer] = (uint8_t)xfb->stride[buffer];
      }
   }
   info->output_count = (uint16_t)xfb->num_outputs;
   nir->xfb_info = info;
   nir->info.has_transform_feedback_varyings = true;
   return 1;
}

enum agx_dxil_nir_status
agx_dxil_to_nir(const void *data, size_t bytes, const uint32_t limits[DXBC_BINDING_KIND_COUNT],
                const struct nir_shader_compiler_options *options, struct agx_dxil_nir_result *result)
{
   if (!result)
      return AGX_DXIL_NIR_INVALID;
   memset(result, 0, sizeof(*result));
   struct agx_dxil_program program;
   const enum agx_dxil_parse_status framing =
      !data || !limits || !options ? AGX_DXIL_PARSE_INVALID
      : agx_dxil_parse(data, bytes, agx_dxil_is_container(data, bytes) ? AGX_DXIL_INPUT_CONTAINER : AGX_DXIL_INPUT_PROGRAM,
                       &any_sm6, &program);
   if (framing != AGX_DXIL_PARSE_OK) {
      snprintf(result->error, sizeof(result->error), "not a DXIL program or container (%d)", (int)framing);
      return framing == AGX_DXIL_PARSE_UNSUPPORTED ? AGX_DXIL_NIR_UNSUPPORTED : AGX_DXIL_NIR_INVALID;
   }
   struct job *job = calloc(1, sizeof(*job));
   if (!job)
      return AGX_DXIL_NIR_OUT_OF_MEMORY;
   job->error = result->error;
   memcpy(job->limits, limits, sizeof(job->limits));
   dxil_spv_parsed_blob blob = NULL;
   dxil_spv_converter converter = NULL;
   nir_shader *nir = NULL;
   enum agx_dxil_nir_status status = AGX_DXIL_NIR_FAILED;
   int began = agx_checked_begin() == DXIL_SPV_SUCCESS;
   /* the bitcode alone: the signatures live in its metadata */
   dxil_spv_result r = began ? agx_checked_parse((const uint8_t *)data + program.bitcode_offset, program.bitcode_bytes, &blob)
                             : DXIL_SPV_ERROR_GENERIC;
   dxil_spv_shader_stage dxil_stage = DXIL_SPV_STAGE_UNKNOWN;
   unsigned entries = 0;
   const char *name = NULL;
   if (r == DXIL_SPV_SUCCESS)
      r = agx_checked_stage(blob, &dxil_stage);
   if (r == DXIL_SPV_SUCCESS)
      r = agx_checked_entry_count(blob, &entries);
   if (r == DXIL_SPV_SUCCESS && entries == 1)
      r = agx_checked_entry_name(blob, 0, &name);
   if (r != DXIL_SPV_SUCCESS) {
      status = from_result(r);
      snprintf(result->error, sizeof(result->error), "DXIL parse failed (%d)", (int)r);
      goto done;
   }
   result->stage = nir_stage(dxil_stage);
   /* EXP1858: the runtime's DXBC -> DXIL conversion (dxilconv) of a hull
    * shader without a control point phase names no entry function;
    * dxil-spirv then takes the module's only entry point and builds the
    * pass-through control point function itself */
   const bool passthrough_hull = dxil_stage == DXIL_SPV_STAGE_HULL && entries == 0;
   if ((!passthrough_hull && (entries != 1 || !name)) || result->stage == MESA_SHADER_NONE) {
      status = AGX_DXIL_NIR_UNSUPPORTED;
      snprintf(result->error, sizeof(result->error), "stage %d, %u entry points", (int)dxil_stage, entries);
      goto done;
   }
   r = agx_checked_scan(blob, scan_srv, scan_sampler, scan_cbv, scan_uav, job);
   freeze(job);
   if (r != DXIL_SPV_SUCCESS || job->refusal != AGX_DXIL_NIR_OK) {
      status = job->refusal != AGX_DXIL_NIR_OK ? job->refusal : from_result(r);
      goto done;
   }
   r = agx_checked_create(blob, &converter);
   if (r == DXIL_SPV_SUCCESS && name)
      r = agx_checked_entry(converter, name);
   if (r == DXIL_SPV_SUCCESS)
      r = agx_checked_srv(converter, remap_srv, job);
   if (r == DXIL_SPV_SUCCESS)
      r = agx_checked_sampler(converter, remap_sampler, job);
   if (r == DXIL_SPV_SUCCESS)
      r = agx_checked_cbv(converter, remap_cbv, job);
   if (r == DXIL_SPV_SUCCESS)
      r = agx_checked_uav(converter, remap_uav, job);
   if (r == DXIL_SPV_SUCCESS) {
      /* AGX SIMD groups are 32 wide (WaveGetLaneCount) */
      const dxil_spv_option_subgroup_properties subgroup = {{DXIL_SPV_OPTION_SUBGROUP_PROPERTIES}, 32, 32};
      r = agx_checked_option(converter, &subgroup.base);
   }
   if (r == DXIL_SPV_SUCCESS) {
      /* the owner binds shader buffers at any 4-byte offset (structured
       * strides of 4); dxil-spirv otherwise needs bindless offsets */
      const dxil_spv_option_ssbo_alignment alignment = {{DXIL_SPV_OPTION_SSBO_ALIGNMENT}, 4};
      r = agx_checked_option(converter, &alignment.base);
   }
   if (r == DXIL_SPV_SUCCESS)
      r = agx_checked_run(converter);
   dxil_spv_compiled_spirv spirv = {0};
   if (r == DXIL_SPV_SUCCESS)
      r = agx_checked_spirv(converter, &spirv);
   if (r != DXIL_SPV_SUCCESS || job->refusal != AGX_DXIL_NIR_OK || !spirv.data || spirv.size < 20 || spirv.size % 4) {
      status = job->refusal != AGX_DXIL_NIR_OK ? job->refusal
             : r != DXIL_SPV_SUCCESS          ? from_result(r)
                                              : AGX_DXIL_NIR_FAILED;
      if (!result->error[0])
         snprintf(result->error, sizeof(result->error), "dxil-spirv conversion failed (%d)", (int)r);
      goto done;
   }
   nir = spirv_nir(spirv.data, spirv.size / 4, result->stage, "main", options);
   if (!nir) {
      snprintf(result->error, sizeof(result->error), "spirv_to_nir failed");
      goto done;
   }

   /* a vertex input reads the vertex element at its D3D input register (its
    * io base, which Asahi's vertex prolog maps to an attribute), as the DXBC
    * frontend sets it (EXP1297: without it every input read attribute 0) */
   if (result->stage == MESA_SHADER_VERTEX)
      nir_foreach_shader_in_variable(var, nir)
         if (var->data.location >= VERT_ATTRIB_GENERIC0)
            var->data.driver_location = var->data.location - VERT_ATTRIB_GENERIC0;

   /* a D3D triangle_cw hull shader (SPIR-V VertexOrderCw) is GL ccw, as in
    * the DXBC frontend (DCL_TESS_OUTPUT_PRIMITIVE) */
   if (result->stage == MESA_SHADER_TESS_CTRL && !nir->info.tess.point_mode &&
       nir->info.tess._primitive_mode != TESS_PRIMITIVE_ISOLINES)
      nir->info.tess.ccw = !nir->info.tess.ccw;
   nir->info.float_controls_execution_mode = 0;
   NIR_PASS(_, nir, nir_shader_alu_pass, strip_preserve, nir_metadata_all, NULL);

   /* vk_spirv_to_nir's tail (src/vulkan/runtime/vk_nir.c) */
   NIR_PASS(_, nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS(_, nir, nir_lower_returns);
   NIR_PASS(_, nir, nir_inline_functions);
   NIR_PASS(_, nir, nir_opt_copy_prop);
   NIR_PASS(_, nir, nir_opt_constant_folding);
   NIR_PASS(_, nir, nir_opt_deref);
   nir_remove_non_entrypoints(nir);
   NIR_PASS(_, nir, nir_lower_variable_initializers, ~0);
   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_split_per_member_structs);
   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_shader_in | nir_var_shader_out | nir_var_system_value, NULL);
   if (result->stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, nir_lower_terminate_to_demote);
      NIR_PASS(_, nir, nir_lower_halt_to_return);
      NIR_PASS(_, nir, nir_lower_returns);
   }
   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_opt_copy_prop);
   NIR_PASS(_, nir, nir_opt_dce);

   /* Vulkan builtins carry a dispatch base (GlobalInvocationId = global id +
    * load_base_global_invocation_id, WorkgroupId likewise): D3D has none,
    * and the AGX backend handles neither intrinsic (EXP1857: "Unhandled
    * intrinsic load_base_global_invocation_id") */
   {
      const nir_lower_compute_system_values_options no_base = {0};
      NIR_PASS(_, nir, nir_lower_system_values);
      NIR_PASS(_, nir, nir_lower_compute_system_values, &no_base);
   }

   /* descriptors -> Gallium slots */
   bool bad = !tex_sets_valid(nir);
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_buffer_slot, nir_metadata_control_flow, &bad);
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_image_slot, nir_metadata_control_flow, &bad);
   if (bad) {
      snprintf(result->error, sizeof(result->error), "descriptor outside its binding kind");
      ralloc_free(nir);
      nir = NULL;
      goto done;
   }
   NIR_PASS(_, nir, nir_shader_tex_pass, lower_heap_tex, nir_metadata_control_flow, NULL);
   record_slots(nir);
   NIR_PASS(_, nir, nir_lower_samplers);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ubo | nir_var_mem_ssbo, nir_address_format_32bit_index_offset);
   NIR_PASS(_, nir, nir_opt_dce);
   nir_remove_dead_derefs(nir);
   /* image and texture variables stay: gather_info counts them */
   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_mem_ubo | nir_var_mem_ssbo | nir_var_mem_push_const, NULL);

   /* the DXBC frontend's tail: outputs at the end, shared memory, opt loop */
   dxbc_nir_finalize(nir);
   /* EXP1859: dxilconv turns a fork phase over an index range (dcl_indexrange
    * o0.x 3) into a loop writing SV_TessFactor by a dynamic index; the
    * tessellation levels are compact patch arrays the DXBC frontend writes
    * with constant indices only, so lower just their indirect derefs (the
    * per-vertex arrays keep their invocation index); after the frontend's
    * tail, where copy propagation has produced the indexed stores */
   if (result->stage == MESA_SHADER_TESS_CTRL || result->stage == MESA_SHADER_TESS_EVAL) {
      struct set *levels = _mesa_pointer_set_create(NULL);
      nir_foreach_variable_with_modes(var, nir, nir_var_shader_in | nir_var_shader_out)
         if (var->data.location == VARYING_SLOT_TESS_LEVEL_OUTER || var->data.location == VARYING_SLOT_TESS_LEVEL_INNER)
            _mesa_set_add(levels, var);
      if (levels->entries) {
         /* the pass skips compact arrays a stage's options call indirectly
          * addressable (Asahi's); indexed compact stores then reach
          * nir_lower_io as slot offsets (outer[1] at TESS_LEVEL_INNER) */
         const nir_shader_compiler_options *agx = nir->options;
         nir_shader_compiler_options direct = *agx;
         direct.support_indirect_inputs &= ~BITFIELD_BIT(result->stage);
         direct.support_indirect_outputs &= ~BITFIELD_BIT(result->stage);
         nir->options = &direct;
         NIR_PASS(_, nir, nir_lower_indirect_var_derefs_to_if_else_trees, levels);
         nir->options = agx;
      }
      _mesa_set_destroy(levels, NULL);
   }
   if (!count_ssbos(nir)) {
      status = AGX_DXIL_NIR_UNSUPPORTED;
      snprintf(result->error, sizeof(result->error), "dynamically indexed shader buffer");
      goto done;
   }
   nir->info.separate_shader = true;
   result->bindings = job->map;
   result->nir = nir;
   status = AGX_DXIL_NIR_OK;
done:
   if (converter)
      (void)agx_checked_converter_free(converter);
   if (blob)
      (void)agx_checked_blob_free(blob);
   if (began)
      (void)agx_checked_end();
   if (status != AGX_DXIL_NIR_OK) {
      if (nir)
         ralloc_free(nir);
      result->nir = NULL;
      memset(&result->bindings, 0, sizeof(result->bindings));
   }
   free(job);
   return status;
}
