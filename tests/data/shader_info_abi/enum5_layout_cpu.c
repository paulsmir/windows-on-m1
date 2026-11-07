/* Original CPU-only actual Mesa enum-bitfield representation probe. SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "compiler/shader_info.h"
static unsigned report(const char *name, int expected, int promoted,
                       int enum_copy, unsigned unsigned_copy)
{
   printf("field=%s expected=%d promoted=%d enum_copy=%d unsigned_copy=%u\n",
          name, expected, promoted, enum_copy, unsigned_copy);
   unsigned bad = promoted != expected || enum_copy != expected ||
                  unsigned_copy != (unsigned)expected;
   const char *marker = !strcmp(name,"depth") ? "DEPTH" :
                        !strcmp(name,"derivative") ? "DERIVATIVE" :
                        !strcmp(name,"stencil_front") ? "STENCIL_FRONT" : "STENCIL_BACK";
   printf("AGX_D3D12_C15_ENUM_%s %s expected=%d getter_only=YES\n",
          marker, bad ? "FAIL" : "PASS", expected);
   return bad;
}
static int original_four_fields(void)
{
   volatile shader_info value = {0};
   unsigned failed = 0;
   value.derivative_group = DERIVATIVE_GROUP_NONE;
   value.fs.depth_layout = FRAG_DEPTH_LAYOUT_NONE;
   value.fs.stencil_front_layout = FRAG_STENCIL_LAYOUT_NONE;
   value.fs.stencil_back_layout = FRAG_STENCIL_LAYOUT_NONE;
   if (value.derivative_group != DERIVATIVE_GROUP_NONE ||
       value.fs.depth_layout != FRAG_DEPTH_LAYOUT_NONE ||
       value.fs.stencil_front_layout != FRAG_STENCIL_LAYOUT_NONE ||
       value.fs.stencil_back_layout != FRAG_STENCIL_LAYOUT_NONE) ++failed;
   printf("sizes int=%zu depth_enum=%zu stencil_enum=%zu derivative_enum=%zu shader_info=%zu\n",
          sizeof(int), sizeof(enum gl_frag_depth_layout),
          sizeof(enum gl_frag_stencil_layout), sizeof(enum gl_derivative_group),
          sizeof(shader_info));
   value.fs.depth_layout = FRAG_DEPTH_LAYOUT_UNCHANGED;
   enum gl_frag_depth_layout depth = value.fs.depth_layout;
   failed += report("depth", FRAG_DEPTH_LAYOUT_UNCHANGED,
                    (int)value.fs.depth_layout, (int)depth,
                    (unsigned)value.fs.depth_layout);
   value.derivative_group = DERIVATIVE_GROUP_LINEAR;
   enum gl_derivative_group derivative = value.derivative_group;
   failed += report("derivative", DERIVATIVE_GROUP_LINEAR,
                    (int)value.derivative_group, (int)derivative,
                    (unsigned)value.derivative_group);
   value.fs.stencil_front_layout = FRAG_STENCIL_LAYOUT_UNCHANGED;
   enum gl_frag_stencil_layout front = value.fs.stencil_front_layout;
   failed += report("stencil_front", FRAG_STENCIL_LAYOUT_UNCHANGED,
                    (int)value.fs.stencil_front_layout, (int)front,
                    (unsigned)value.fs.stencil_front_layout);
   value.fs.stencil_back_layout = FRAG_STENCIL_LAYOUT_UNCHANGED;
   enum gl_frag_stencil_layout back = value.fs.stencil_back_layout;
   failed += report("stencil_back", FRAG_STENCIL_LAYOUT_UNCHANGED,
                    (int)value.fs.stencil_back_layout, (int)back,
                    (unsigned)value.fs.stencil_back_layout);
   printf("AGX_D3D12_C15_ENUM_ABI_CPU %s failed=%u fields=4 volatile_owned_object=YES\n",
          failed ? "FAIL" : "PASS", failed);
   return failed ? 1 : 0;
}


/* Original helpers inspect the ACTUAL volatile header object; no duplicate layout. */
static void reset_bytes(volatile shader_info *value)
{
   volatile unsigned char *bytes = (volatile unsigned char *)value;
   for (size_t i = 0; i < sizeof(*value); ++i) bytes[i] = 0;
}
static unsigned snapshot(const char *field, const volatile shader_info *value)
{
   const volatile unsigned char *bytes = (const volatile unsigned char *)value;
   unsigned changed = 0;
   if (sizeof(*value) > 4096) {
      puts("AGX_D3D12_C15_RAW_LAYOUT REFUSED size_exceeds4096");
      return 1;
   }
   printf("raw field=%s object_bytes=%zu changed=",field,sizeof(*value));
   for (size_t i = 0; i < sizeof(*value); ++i) {
      unsigned byte = bytes[i];
      if (byte) { printf("%s%zu:%02x",changed ? "," : "",i,byte); ++changed; }
   }
   if (!changed) putchar('-');
   printf(" changed_count=%u\n",changed);
   return 0;
}
#define STORE_SNAPSHOT(tag,store) do { reset_bytes(&value); store; failed += snapshot(tag,&value); } while (0)
static unsigned layouts(void)
{
   volatile shader_info value;
   unsigned failed = 0;
   printf("layout shader_info=%zu hash_enum=%zu source_hash=%zu num_ubos=%zu shared_size=%zu workgroup_size=%zu legacy_math=%zu union_vs=%zu union_fs=%zu fs_advanced_blend=%zu\n",
          sizeof(shader_info),sizeof(enum shader_info_hash_type),
          offsetof(shader_info,source_blake3),offsetof(shader_info,num_ubos),
          offsetof(shader_info,shared_size),offsetof(shader_info,workgroup_size),
          offsetof(shader_info,use_legacy_math_rules),offsetof(shader_info,vs),
          offsetof(shader_info,fs),offsetof(shader_info,fs.advanced_blend_modes));
   reset_bytes(&value); failed += snapshot("reset_zero",&value);
   STORE_SNAPSHOT("depth_none",value.fs.depth_layout = FRAG_DEPTH_LAYOUT_NONE);
   STORE_SNAPSHOT("depth_unchanged",value.fs.depth_layout = FRAG_DEPTH_LAYOUT_UNCHANGED);
   STORE_SNAPSHOT("derivative_none",value.derivative_group = DERIVATIVE_GROUP_NONE);
   STORE_SNAPSHOT("derivative_linear",value.derivative_group = DERIVATIVE_GROUP_LINEAR);
   STORE_SNAPSHOT("stencil_front_none",value.fs.stencil_front_layout = FRAG_STENCIL_LAYOUT_NONE);
   STORE_SNAPSHOT("stencil_front_unchanged",value.fs.stencil_front_layout = FRAG_STENCIL_LAYOUT_UNCHANGED);
   STORE_SNAPSHOT("stencil_back_none",value.fs.stencil_back_layout = FRAG_STENCIL_LAYOUT_NONE);
   STORE_SNAPSHOT("stencil_back_unchanged",value.fs.stencil_back_layout = FRAG_STENCIL_LAYOUT_UNCHANGED);
   STORE_SNAPSHOT("hash_raw",value.hash_type = SHADER_INFO_HASH_TYPE_RAW);
   STORE_SNAPSHOT("hash_dxil",value.hash_type = SHADER_INFO_HASH_TYPE_DXIL);
   STORE_SNAPSHOT("hash_dxbc",value.hash_type = SHADER_INFO_HASH_TYPE_DXBC);
   printf("AGX_D3D12_C15_RAW_LAYOUT %s isolated_stores=11 reset_control=1 offsets_only_legal_nonbitfields=YES\n",
          failed ? "FAIL" : "RECORDED");
   return failed;
}
#undef STORE_SNAPSHOT
static unsigned hash_getters(void)
{
   volatile shader_info value;
   unsigned controls = 0;
   reset_bytes(&value); value.hash_type = SHADER_INFO_HASH_TYPE_RAW;
   enum shader_info_hash_type raw = value.hash_type;
   printf("hash_field=RAW expected=%d promoted=%d enum_copy=%d unsigned_copy=%u\n",
          SHADER_INFO_HASH_TYPE_RAW,(int)value.hash_type,(int)raw,(unsigned)value.hash_type);
   controls += value.hash_type != SHADER_INFO_HASH_TYPE_RAW || raw != SHADER_INFO_HASH_TYPE_RAW ||
               (unsigned)value.hash_type != (unsigned)SHADER_INFO_HASH_TYPE_RAW;
   reset_bytes(&value); value.hash_type = SHADER_INFO_HASH_TYPE_DXIL;
   enum shader_info_hash_type dxil = value.hash_type;
   printf("hash_field=DXIL expected=%d promoted=%d enum_copy=%d unsigned_copy=%u\n",
          SHADER_INFO_HASH_TYPE_DXIL,(int)value.hash_type,(int)dxil,(unsigned)value.hash_type);
   controls += value.hash_type != SHADER_INFO_HASH_TYPE_DXIL || dxil != SHADER_INFO_HASH_TYPE_DXIL ||
               (unsigned)value.hash_type != (unsigned)SHADER_INFO_HASH_TYPE_DXIL;
   reset_bytes(&value); value.hash_type = SHADER_INFO_HASH_TYPE_DXBC;
   enum shader_info_hash_type dxbc = value.hash_type;
   printf("hash_field=DXBC expected=%d promoted=%d enum_copy=%d unsigned_copy=%u\n",
          SHADER_INFO_HASH_TYPE_DXBC,(int)value.hash_type,(int)dxbc,(unsigned)value.hash_type);
   unsigned bad = value.hash_type != SHADER_INFO_HASH_TYPE_DXBC || dxbc != SHADER_INFO_HASH_TYPE_DXBC ||
                  (unsigned)value.hash_type != (unsigned)SHADER_INFO_HASH_TYPE_DXBC;
   printf("AGX_D3D12_C15_ENUM_HASH %s expected=%d getter_only=YES\n",
          bad ? "FAIL" : "PASS",SHADER_INFO_HASH_TYPE_DXBC);
   printf("AGX_D3D12_C15_HASH_ENUM_ABI_CPU %s failed=%u RAW0_DXIL1_controls=%s DXBC2_getter=YES volatile_owned_object=YES\n",
          (bad || controls) ? "FAIL" : "PASS",bad+controls,controls ? "FAIL" : "PASS");
   return bad+controls;
}
static unsigned none_controls(void)
{
   volatile shader_info value;
   reset_bytes(&value);
   value.fs.depth_layout = FRAG_DEPTH_LAYOUT_NONE;
   value.derivative_group = DERIVATIVE_GROUP_NONE;
   value.fs.stencil_front_layout = FRAG_STENCIL_LAYOUT_NONE;
   value.fs.stencil_back_layout = FRAG_STENCIL_LAYOUT_NONE;
   value.hash_type = SHADER_INFO_HASH_TYPE_RAW;
   unsigned depth = value.fs.depth_layout != FRAG_DEPTH_LAYOUT_NONE;
   unsigned derivative = value.derivative_group != DERIVATIVE_GROUP_NONE;
   unsigned front = value.fs.stencil_front_layout != FRAG_STENCIL_LAYOUT_NONE;
   unsigned back = value.fs.stencil_back_layout != FRAG_STENCIL_LAYOUT_NONE;
   unsigned hash = value.hash_type != SHADER_INFO_HASH_TYPE_RAW;
   printf("AGX_D3D12_C15_ENUM_NONE_CPU %s depth=%u derivative=%u stencil_front=%u stencil_back=%u hash_raw=%u\n",
          (depth||derivative||front||back||hash) ? "FAIL" : "PASS",depth,derivative,front,back,hash);
   return depth+derivative+front+back+hash;
}
int main(void)
{
   unsigned four = original_four_fields();
   unsigned hash = hash_getters();
   unsigned none = none_controls();
   unsigned layout = layouts();
   return (four || hash || none || layout) ? 1 : 0;
}
