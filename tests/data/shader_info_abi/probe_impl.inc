/* Original project fixture shared by actual C and C++ TUs. SPDX-License-Identifier: MIT */
#include "abi_probe.h"
#include <string.h>
#define JOIN2(a,b) a##b
#define JOIN(a,b) JOIN2(a,b)
void JOIN(PREFIX,_write)(shader_info *p, unsigned field, unsigned value)
{
 switch(field) {
 case 0: p->derivative_group=(enum gl_derivative_group)value; break;
 case 1: p->fs.depth_layout=(enum gl_frag_depth_layout)value; break;
 case 2: p->fs.stencil_front_layout=(enum gl_frag_stencil_layout)value; break;
 case 3: p->fs.stencil_back_layout=(enum gl_frag_stencil_layout)value; break;
 case 4: p->hash_type=(enum shader_info_hash_type)value; break;
 }
}
int JOIN(PREFIX,_read)(const shader_info *p, unsigned field)
{
 switch(field) {
 case 0: return p->derivative_group;
 case 1: return p->fs.depth_layout;
 case 2: return p->fs.stencil_front_layout;
 case 3: return p->fs.stencil_back_layout;
 case 4: return p->hash_type;
 default: return -99;
 }
}
void JOIN(PREFIX,_layout)(size_t out[6])
{
 out[0]=sizeof(shader_info); out[1]=ALIGNOF(shader_info);
 out[2]=offsetof(shader_info,use_legacy_math_rules);
 out[3]=offsetof(shader_info,fs.advanced_blend_modes);
 out[4]=offsetof(shader_info,cs.workgroup_size_hint);
 out[5]=sizeof(enum gl_frag_depth_layout);
}
