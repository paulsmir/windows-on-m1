/* Original finite-domain full-block footprint proof; no production admission. */
#define main c21_unused_ail_main
#include "ail_swizzle_oracle_fixture.c"
#undef main
int main(void) {
 unsigned n=0,mips=0;uint64_t blocks=0;
 for(unsigned side=1;side<=32;side*=2)for(unsigned depth=1;depth<=2048;depth++)for(unsigned request=1;request<=2;request++)for(unsigned f=0;f<2;f++) {
  if(request>1&&side==1&&depth==1)continue;
  struct ail_layout a={.width_px=side,.height_px=side,.depth_px=depth,.sample_count_sa=1,.levels=request,.mipmapped_z=true,.tiling=AIL_TILING_GPU,.format=f?PIPE_FORMAT_R8G8B8A8_UNORM:PIPE_FORMAT_R32_UINT,.renderable=true};ail_make_miptree(&a);
  assert(a.size_B==(uint64_t)a.layer_stride_B*depth);
  for(unsigned l=0;l<request;l++) {
   unsigned s=mini(side,l),d=mini(depth,l);uint64_t block=4*s*s,off=a.level_offsets_B[l];
   assert(a.tilesize_el[l].width_el==s&&a.tilesize_el[l].height_el==s);
   assert(off%block==0);
   assert(off+block<=a.layer_stride_B);
   assert(off+(uint64_t)(d-1)*a.layer_stride_B+block<=a.size_B);
   if(l+1<a.levels)assert(off+block<=a.level_offsets_B[l+1]);
   if(l)assert(a.level_offsets_B[l-1]+4ull*mini(side,l-1)*mini(side,l-1)<=off);
   blocks+=d;mips++;
  }
  n++;
 }
 printf("NATIVE_TWO_PATTERN_FOOTPRINT_PASS resources=%u mips=%u disjoint_full_blocks=%llu formats=R32_UINT,RGBA8 squarePOT=1..32 depth=1..2048 requestLevels=1,2 aligned=YES allocationBounds=YES\n",n,mips,(unsigned long long)blocks);
}
