/* Original project fixture. SPDX-License-Identifier: MIT */
#include "abi_probe.h"
#include <stdio.h>
#include <string.h>
static unsigned failures, checks;
static void check(int okay, const char *what) { ++checks; if(!okay) {++failures; printf("FAIL %s\n",what);} }
static void neighbors(shader_info *p) {
 p->use_legacy_math_rules=1; p->assume_no_data_races=1;
 p->occupancy_bounded_workgroup_fairness=1;

 p->fs.untyped_color_outputs=1; p->fs.advanced_blend_modes=0x12345678u;
 p->fs.early_and_late_fragment_tests=1;
}
static int neighbors_ok(const shader_info *p) {
 return p->use_legacy_math_rules && p->assume_no_data_races &&
 p->occupancy_bounded_workgroup_fairness &&
 p->fs.untyped_color_outputs && p->fs.advanced_blend_modes==0x12345678u &&
 p->fs.early_and_late_fragment_tests;
}
int main(void) {
 size_t c[6],cpp[6]; c_layout(c);cpp_layout(cpp);
 check(memcmp(c,cpp,sizeof(c))==0,"C/C++ layout equality");
 printf("layout size=%zu align=%zu legacy=%zu blend=%zu cs=%zu enum=%zu\n",c[0],c[1],c[2],c[3],c[4],c[5]);
 unsigned char masks[5][sizeof(shader_info)]; memset(masks,0,sizeof(masks));
 for(unsigned f=0;f<5;++f) {
  unsigned max=(f==0||f==4)?2:4;
  for(unsigned value=0;value<=max;++value) {
   shader_info a,b;memset(&a,0,sizeof(a));memset(&b,0,sizeof(b));neighbors(&a);neighbors(&b);
   c_write(&a,f,value);cpp_write(&b,f,value);
   check(c_read(&a,f)==(int)value && cpp_read(&a,f)==(int)value,"C write cross-language reads");
   check(c_read(&b,f)==(int)value && cpp_read(&b,f)==(int)value,"C++ write cross-language reads");
   check(memcmp(&a,&b,sizeof(a))==0,"C/C++ identical raw writes");
   check(neighbors_ok(&a)&&neighbors_ok(&b),"neighbors preserved");
   for(unsigned other=0;other<5;++other) if(other!=f)
    check(c_read(&a,other)==0&&cpp_read(&b,other)==0,"other bitfield unchanged");
   shader_info baseline;memset(&baseline,0,sizeof(baseline));neighbors(&baseline);
   for(size_t j=0;j<sizeof(a);++j) masks[f][j]|=((unsigned char*)&a)[j]^((unsigned char*)&baseline)[j];
  }
 }
 for(unsigned f=0;f<5;++f) {
  printf("mask field=%u",f);
  for(size_t j=0;j<sizeof(shader_info);++j) if(masks[f][j]) printf(" %zu:%02x",j,(unsigned)masks[f][j]);
  puts("");
  for(unsigned g=f+1;g<5;++g) for(size_t j=0;j<sizeof(shader_info);++j)
   check((masks[f][j]&masks[g][j])==0,"field masks disjoint");
 }
 printf("AGX_D3D12_C15_ENUM_CROSS_TU %s checks=%u failures=%u fields=5\n",failures?"FAIL":"PASS",checks,failures);
 return failures?1:0;
}
