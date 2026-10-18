/* Original Windows0004 candidate compared directly with linked AIL output.
 * No device-dependent layout/capability is installed by this test. */
#define main c21_ail_oracle_uninvoked_main
#include "ail_swizzle_oracle_fixture.c"
#undef main
#include "windows_swizzle_reference.h"
static unsigned layouts_checked,mips_checked,max_patterns;
static uint64_t bytes_checked;
static void compare_windows(unsigned w,unsigned h,unsigned d,unsigned b,unsigned mip) {
 enum pipe_format f=b==4?PIPE_FORMAT_R32_UINT:b==8?PIPE_FORMAT_R32G32_UINT:PIPE_FORMAT_R32G32B32A32_UINT;
 struct ail_layout a={.width_px=w,.height_px=h,.depth_px=d,.sample_count_sa=1,.levels=mip?2:1,
  .mipmapped_z=true,.tiling=AIL_TILING_GPU,.format=f,.renderable=true};
 ail_make_miptree(&a);++layouts_checked;
 uint8_t *tiled=malloc(a.size_B),*visited=calloc(a.size_B,1);assert(tiled&&visited);memset(tiled,0xa5,a.size_B);
 unsigned pattern_pairs[16][2],pattern_count=0;
 for(unsigned l=0;l<a.levels;++l) {
  unsigned lw=mini(w,l),lh=mini(h,l),ld=mini(d,l),tx=a.tilesize_el[l].width_el,ty=a.tilesize_el[l].height_el;
  unsigned existing=0;for(;existing<pattern_count;existing++)if(pattern_pairs[existing][0]==tx&&pattern_pairs[existing][1]==ty)break;
  if(existing==pattern_count){pattern_pairs[pattern_count][0]=tx;pattern_pairs[pattern_count++][1]=ty;}
  WinSwizzle0004 p;WinSwizzleAddressing addr;assert(win_morton_candidate(b,tx,ty,&p,&addr));
  addr.base=a.level_offsets_B[l];addr.row_stride=(uint64_t)((a.stride_el[l]+tx-1)/tx)*tx*ty*b;addr.depth_stride=a.layer_stride_B;
  size_t linear_bytes=(size_t)lw*lh*b;uint8_t *linear=malloc(linear_bytes);assert(linear);
  for(unsigned z=0;z<ld;++z) {
   for(size_t i=0;i<linear_bytes;++i)linear[i]=(uint8_t)(i*131+z*37+l*19);
   ail_tile(tiled+addr.base+z*a.layer_stride_B,linear,&a,l,lw*b,0,0,lw,lh);
   for(unsigned y=0;y<lh;y++)for(unsigned x=0;x<lw;x++)for(unsigned q=0;q<b;q++) {
    uint32_t source[4]={x*b+q,y,z,0};uint64_t offset=0;
    assert(win_swizzle_0004(&p,&addr,source,&offset)==WIN_SWIZZLE_OK);
    assert(offset<a.size_B &&!visited[offset]);visited[offset]=1;
    assert(tiled[offset]==linear[((size_t)y*lw+x)*b+q]);
    // Independent address accessor already checked against AIL by the other
    // fixture, additionally ensures equality even when byte values repeat.
    assert(offset==c21_ail_reference_offset(&a,l,x,y,z,q));++bytes_checked;
   }
  }
  free(linear);++mips_checked;
 }
 if(pattern_count>max_patterns)max_patterns=pattern_count;
 if(w==64&&h==2048&&b==4&&mip) {
  assert(pattern_count==7);
  puts("WINDOWS_0004_PATTERN_BUDGET resource=64x2048x2 bpp_bytes=4 native_distinct_patterns=7 per_layout_slots=6 GENERAL_ADMISSION_UNPROVEN");
 }
 free(tiled);free(visited);
}
int main(void) {
 unsigned dim[][3]={{4,4,8},{32,32,4},{31,33,3},{63,65,3},{64,32,4},{65,33,3},
  {64,64,2},{65,63,5},{513,257,1},{1,17,9},{17,1,9},{64,2048,2}};
 for(unsigned b=4;b<=16;b*=2)for(unsigned n=0;n<sizeof(dim)/sizeof(dim[0]);n++)for(unsigned mip=0;mip<2;mip++)
  compare_windows(dim[n][0],dim[n][1],dim[n][2],b,mip);
 assert(layouts_checked==72 &&max_patterns==7);
 printf("WINDOWS_AIL_0004_SUBSET_PASS layouts=%u mip_levels=%u exact_logical_bytes=%llu no_alias=YES max_native_tile_patterns=%u\n",
  layouts_checked,mips_checked,(unsigned long long)bytes_checked,max_patterns);
}
