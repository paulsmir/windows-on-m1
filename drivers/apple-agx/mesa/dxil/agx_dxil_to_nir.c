/* D3D12 C44: DXIL -> SPIR-V (dxil-spirv, checked C boundary) -> NIR
 * (spirv_to_nir, Vulkan environment) -> the DXBC frontend's Gallium-slot
 * conventions (agx_dxil_to_nir.h). SPDX-License-Identifier: MIT */
#include "agx_dxil_to_nir.h"
#include "agx_dxil_checked.h"
#include "../dxbc/dxbc_to_nir.h"
#include "nir.h"
#include "nir_builder.h"
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
};

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
   if (b->range_size == UINT32_MAX || !b->range_size) {
      /* C47 heap ranges are not translated for DXIL yet */
      refuse(job, AGX_DXIL_NIR_UNSUPPORTED, "unbounded register range", b);
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
                                                  b->range_size, (uint32_t)b->kind, 0, counter};
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
   v->buffer_binding.binding = d->slot;
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
   v->binding = d->slot;
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
   nir_def *index = nir_imm_int(b, var->data.binding);
   if (deref->deref_type == nir_deref_type_array)
      index = nir_iadd(b, index, nir_u2u32(b, deref->arr.index.ssa));
   nir_rewrite_image_intrinsic(intr, index, nir_image_intrinsic_type_default);
   /* nir_rewrite_image_intrinsic leaves a stale range_base */
   nir_intrinsic_set_range_base(intr, 0);
   return true;
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
               if (tv) {
                  set_range(nir->info.textures_used, BITSET_SIZE(nir->info.textures_used), tv);
                  if (tex->op == nir_texop_txf || tex->op == nir_texop_txf_ms)
                     set_range(nir->info.textures_used_by_txf, BITSET_SIZE(nir->info.textures_used_by_txf), tv);
               }
               if (sv)
                  set_range(nir->info.samplers_used, BITSET_SIZE(nir->info.samplers_used), sv);
            }
         }
      }
   }
   nir_foreach_variable_with_modes(var, nir, nir_var_image)
      set_range(nir->info.images_used, BITSET_SIZE(nir->info.images_used), var);
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

enum agx_dxil_nir_status
agx_dxil_to_nir(const void *data, size_t bytes, const uint32_t limits[DXBC_BINDING_KIND_COUNT],
                const struct nir_shader_compiler_options *options, struct agx_dxil_nir_result *result)
{
   if (!result)
      return AGX_DXIL_NIR_INVALID;
   memset(result, 0, sizeof(*result));
   if (!data || !limits || !options || !agx_dxil_is_container(data, bytes)) {
      snprintf(result->error, sizeof(result->error), "not a DXIL container");
      return AGX_DXIL_NIR_INVALID;
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
   dxil_spv_result r = began ? agx_checked_parse_blob(data, bytes, &blob) : DXIL_SPV_ERROR_GENERIC;
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
   if (entries != 1 || !name || result->stage == MESA_SHADER_NONE ||
       result->stage == MESA_SHADER_TESS_CTRL || result->stage == MESA_SHADER_TESS_EVAL ||
       result->stage == MESA_SHADER_GEOMETRY) {
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
   if (r == DXIL_SPV_SUCCESS)
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
   record_slots(nir);
   NIR_PASS(_, nir, nir_lower_samplers);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ubo | nir_var_mem_ssbo, nir_address_format_32bit_index_offset);
   NIR_PASS(_, nir, nir_opt_dce);
   nir_remove_dead_derefs(nir);
   /* image and texture variables stay: gather_info counts them */
   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_mem_ubo | nir_var_mem_ssbo | nir_var_mem_push_const, NULL);

   /* the DXBC frontend's tail: outputs at the end, shared memory, opt loop */
   dxbc_nir_finalize(nir);
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
