/* Original unlinked descriptor-namespace adapter proposal. SPDX-License-Identifier: MIT */
#include "agx_dxil_nir_map.h"
#include "nir.h"
#include "nir_builder.h"
#include <string.h>

static const struct dxbc_binding *slot(const struct dxbc_binding_map *map,
                                      unsigned kind, unsigned index)
{
   for (unsigned j = 0; j < map->count; ++j)
      if (map->bindings[j].kind == kind && map->bindings[j].slot == index)
         return &map->bindings[j];
   return NULL;
}
static bool map_valid(const struct dxbc_binding_map *map)
{
   if (!map || map->active != 1 || map->count > DXBC_BINDING_MAP_MAX) return false;
   static const unsigned limits[] = {14, 128, 8};
   unsigned counts[3] = {0};
   for (unsigned j = 0; j < map->count; ++j) {
      const struct dxbc_binding *v = &map->bindings[j];
      if (v->kind > DXBC_BINDING_UAV || v->slot >= limits[v->kind]) return false;
      ++counts[v->kind];
      for (unsigned k = 0; k < j; ++k) {
         const struct dxbc_binding *p = &map->bindings[k];
         if (v->kind == p->kind && (v->slot == p->slot || v->id == p->id ||
             (v->space == p->space && v->reg == p->reg))) return false;
      }
   }
   for (unsigned j = 0; j < 3; ++j)
      for (unsigned k = 0; k < counts[j]; ++k)
         if (!slot(map, j, k)) return false;
   return true;
}
static bool pair32(nir_def *def) { return def->num_components == 2 && def->bit_size == 32; }
static bool buffer_index(nir_intrinsic_instr *i, const struct dxbc_binding_map *map)
{
   if (i->intrinsic != nir_intrinsic_vulkan_resource_index || !pair32(&i->def) ||
       i->num_components != 2 || !nir_src_is_const(i->src[0]) ||
       i->src[0].ssa->num_components != 1 || i->src[0].ssa->bit_size != 32 ||
       nir_src_as_uint(i->src[0]) != 0) return false;
   unsigned kind = nir_intrinsic_desc_set(i);
   if (kind != DXBC_BINDING_CBV && kind != DXBC_BINDING_UAV) return false;
   nir_descriptor_type expected = kind == DXBC_BINDING_CBV ?
      nir_descriptor_type_uniform_buffer : nir_descriptor_type_storage_buffer;
   return nir_intrinsic_desc_type(i) == expected && slot(map, kind, nir_intrinsic_binding(i));
}
static bool descriptor_load(nir_intrinsic_instr *i, const struct dxbc_binding_map *map)
{
   if (!pair32(&i->def) || i->num_components != 2 || !pair32(i->src[0].ssa) ||
       nir_def_instr(i->src[0].ssa)->type != nir_instr_type_intrinsic) return false;
   nir_intrinsic_instr *index = nir_instr_as_intrinsic(nir_def_instr(i->src[0].ssa));
   return buffer_index(index, map) && nir_intrinsic_desc_type(i) == nir_intrinsic_desc_type(index);
}
static bool raw_srv_var(nir_variable *v, const struct dxbc_binding_map *map)
{
   return v && v->data.mode == nir_var_uniform &&
      glsl_type_is_texture(v->type) && !glsl_type_is_array(v->type) &&
      glsl_get_sampler_dim(v->type) == GLSL_SAMPLER_DIM_BUF &&
      !glsl_sampler_type_is_array(v->type) &&
      glsl_get_sampler_result_type(v->type) == GLSL_TYPE_UINT &&
      v->data.descriptor_set == DXBC_BINDING_SRV && slot(map, DXBC_BINDING_SRV, v->data.binding);
}
static bool srv_fetch(nir_tex_instr *tex, const struct dxbc_binding_map *map)
{
   if (tex->op != nir_texop_txf || tex->sampler_dim != GLSL_SAMPLER_DIM_BUF || tex->is_array ||
       tex->coord_components != 1 || nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref) >= 0 ||
       nir_tex_instr_src_index(tex, nir_tex_src_texture_handle) >= 0 ||
       nir_tex_instr_src_index(tex, nir_tex_src_sampler_handle) >= 0 ||
       nir_tex_instr_src_index(tex, nir_tex_src_texture_offset) >= 0 ||
       nir_tex_instr_src_index(tex, nir_tex_src_sampler_offset) >= 0) return false;
   nir_deref_instr *d = nir_get_tex_deref(tex, nir_tex_src_texture_deref);
   return d && d->deref_type == nir_deref_type_var && raw_srv_var(d->var, map);
}
static bool rewrite(nir_builder *b, nir_intrinsic_instr *i, void *unused)
{
   (void)unused;
   nir_def *value;
   if (i->intrinsic == nir_intrinsic_vulkan_resource_index) {
      b->cursor = nir_before_instr(&i->instr);
      value = nir_imm_ivec2(b, nir_intrinsic_binding(i), 0);
   } else if (i->intrinsic == nir_intrinsic_load_vulkan_descriptor) {
      value = i->src[0].ssa;
   } else return false;
   nir_def_rewrite_uses(&i->def, value);
   nir_instr_remove(&i->instr);
   return true;
}
enum agx_dxil_nir_map_status agx_dxil_nir_lower_map(nir_shader *s,
   const struct dxbc_binding_map *map, struct agx_dxil_nir_map_stats *stats)
{
   if (stats) memset(stats, 0, sizeof(*stats));
   if (!s || !stats || !map_valid(map)) return AGX_DXIL_NIR_MAP_INVALID;
   if (s->info.stage != MESA_SHADER_COMPUTE) return AGX_DXIL_NIR_MAP_UNSUPPORTED;
   struct agx_dxil_nir_map_stats found = {0};
   nir_foreach_variable_with_modes(v, s, nir_var_uniform | nir_var_image | nir_var_mem_ubo | nir_var_mem_ssbo) {
      if (v->data.mode == nir_var_uniform) {
         if (!raw_srv_var(v, map)) return AGX_DXIL_NIR_MAP_UNSUPPORTED;
      } else {
         unsigned kind = v->data.mode == nir_var_mem_ubo ? DXBC_BINDING_CBV : DXBC_BINDING_UAV;
         if (v->data.mode == nir_var_image || glsl_type_is_array(v->type) ||
             v->data.descriptor_set != kind || !slot(map, kind, v->data.binding))
            return AGX_DXIL_NIR_MAP_UNSUPPORTED;
      }
   }
   /* Complete structural preflight before any NIR rewrite or sampler lowering. */
   nir_foreach_function_impl(impl, s) nir_foreach_block(block, impl) nir_foreach_instr(ins, block) {
      if (ins->type == nir_instr_type_tex) {
         if (!srv_fetch(nir_instr_as_tex(ins), map)) return AGX_DXIL_NIR_MAP_UNSUPPORTED;
         ++found.srv_fetches;
      } else if (ins->type == nir_instr_type_intrinsic) {
         nir_intrinsic_instr *i = nir_instr_as_intrinsic(ins);
         if (i->intrinsic == nir_intrinsic_vulkan_resource_reindex) return AGX_DXIL_NIR_MAP_UNSUPPORTED;
         if (i->intrinsic == nir_intrinsic_vulkan_resource_index) {
            if (!buffer_index(i, map)) return AGX_DXIL_NIR_MAP_UNSUPPORTED;
            if (nir_intrinsic_desc_set(i) == DXBC_BINDING_CBV) ++found.ubo_indices;
            else ++found.ssbo_indices;
         } else if (i->intrinsic == nir_intrinsic_load_vulkan_descriptor) {
            if (!descriptor_load(i, map)) return AGX_DXIL_NIR_MAP_UNSUPPORTED;
            ++found.descriptors;
         }
      }
   }
   nir_shader_intrinsics_pass(s, rewrite, nir_metadata_control_flow, NULL);
   /* All texture derefs have already been checked before this loses set identity. */
   nir_lower_samplers(s);
   *stats = found;
   return AGX_DXIL_NIR_MAP_OK;
}
