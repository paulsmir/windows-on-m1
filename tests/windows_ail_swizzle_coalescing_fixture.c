/* Original finite candidate cover search. No production layout admission. */
#define main c21_ail_oracle_uninvoked_main
#include "ail_swizzle_oracle_fixture.c"
#undef main
#include "windows_swizzle_reference.h"
static unsigned resources,covered_mips,max_cover;static uint64_t compared;
static int candidate_matches(const struct ail_layout *a,unsigned l,unsigned b,unsigned tx,unsigned ty) {
 WinSwizzle0004 p;WinSwizzleAddressing addr;
 assert(win_morton_candidate(b,tx,ty,&p,&addr));
 addr.base=a->level_offsets_B[l];addr.row_stride=(uint64_t)((a->stride_el[l]+tx-1)/tx)*tx*ty*b;addr.depth_stride=a->layer_stride_B;
 unsigned w=mini(a->width_px,l),h=mini(a->height_px,l);
 /* All active XY/byte inputs checked. Z has exactly the same additive stride
  * in both accessors; selected covers additionally execute every physical Z. */
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)for(unsigned q=0;q<b;q++) {
  uint32_t in[4]={x*b+q,y,0,0};uint64_t address=0;
  if(win_swizzle_0004(&p,&addr,in,&address)!=WIN_SWIZZLE_OK ||
     address!=c21_ail_reference_offset(a,l,x,y,0,q))return 0;
 }
 return 1;
}
static void cover(unsigned w,unsigned h,unsigned d,unsigned b) {
 enum pipe_format f=b==4?PIPE_FORMAT_R32_UINT:b==8?PIPE_FORMAT_R32G32_UINT:PIPE_FORMAT_R32G32B32A32_UINT;
 struct ail_layout a={.width_px=w,.height_px=h,.depth_px=d,.sample_count_sa=1,.levels=2,.mipmapped_z=true,.tiling=AIL_TILING_GPU,.format=f,.renderable=true};ail_make_miptree(&a);
 unsigned tx[16],ty[16],n=0;uint32_t masks[16]={0};assert(a.levels<32);
 for(unsigned l=0;l<a.levels;l++){
  unsigned x=a.tilesize_el[l].width_el,y=a.tilesize_el[l].height_el,k=0;
  for(;k<n;k++)if(tx[k]==x&&ty[k]==y)break;
  if(k==n){assert(n<16);tx[n]=x;ty[n++]=y;}
 }
 for(unsigned k=0;k<n;k++)for(unsigned l=0;l<a.levels;l++)if(candidate_matches(&a,l,b,tx[k],ty[k]))masks[k]|=1u<<l;
 uint32_t all=(1u<<a.levels)-1,best=0;unsigned best_count=n+1;
 for(uint32_t choice=1;choice<(1u<<n);choice++){
  unsigned count=__builtin_popcount(choice);if(count>=best_count)continue;uint32_t m=0;
  for(unsigned k=0;k<n;k++)if(choice&(1u<<k))m|=masks[k];
  if(m==all){best=choice;best_count=count;}
 }
 assert(best&&best_count<=n);
 /* The public layout may need mip transition slots rather than only unique
  * pattern identities. Independently minimize contiguous covered mip runs. */
 unsigned runs[32];for(unsigned l=0;l<32;l++)runs[l]=1000;runs[0]=0;
 for(unsigned l=0;l<a.levels;l++)for(unsigned k=0;k<n;k++){
  for(unsigned end=l;end<a.levels&&(masks[k]&(1u<<end));end++)
   if(runs[end+1]>runs[l]+1)runs[end+1]=runs[l]+1;
 }
 assert(runs[a.levels]>=best_count);
 if((w==4&&h==4&&d==8)||(w==32&&h==32&&d==4))assert(runs[a.levels]==1);
 if(w==4&&h==4&&d==8)assert(best_count==1);
 if(w==32&&h==32&&d==4)assert(best_count==1);
 /* Execute actual AIL for every active byte using the winning finite cover. */
 uint8_t *actual=malloc(a.size_B),*seen=calloc(a.size_B,1);assert(actual&&seen);memset(actual,0xa5,a.size_B);
 for(unsigned l=0;l<a.levels;l++){
  unsigned k=0;while(k<n&&(!(best&(1u<<k))||!(masks[k]&(1u<<l))))k++;assert(k<n);
  WinSwizzle0004 p;WinSwizzleAddressing addr;assert(win_morton_candidate(b,tx[k],ty[k],&p,&addr));
  addr.base=a.level_offsets_B[l];addr.row_stride=(uint64_t)((a.stride_el[l]+tx[k]-1)/tx[k])*tx[k]*ty[k]*b;addr.depth_stride=a.layer_stride_B;
  unsigned lw=mini(w,l),lh=mini(h,l),ld=mini(d,l);size_t size=(size_t)lw*lh*b;uint8_t *linear=malloc(size);assert(linear);
  for(unsigned z=0;z<ld;z++){
   for(size_t q=0;q<size;q++)linear[q]=(uint8_t)(q*131+z*37+l*19);
   ail_tile(actual+addr.base+z*a.layer_stride_B,linear,&a,l,lw*b,0,0,lw,lh);
   for(unsigned y=0;y<lh;y++)for(unsigned x=0;x<lw;x++)for(unsigned q=0;q<b;q++){
    uint32_t in[4]={x*b+q,y,z,0};uint64_t address=0;assert(win_swizzle_0004(&p,&addr,in,&address)==WIN_SWIZZLE_OK);
    assert(address==c21_ail_reference_offset(&a,l,x,y,z,q)&&address<a.size_B&&!seen[address]);seen[address]=1;
    assert(actual[address]==linear[((size_t)y*lw+x)*b+q]);compared++;
   }
  }
  free(linear);covered_mips++;
 }
 if(best_count>max_cover)max_cover=best_count;
 printf("WINDOWS_AIL_CANDIDATE_COVER dims=%ux%ux%u bytes=%u native=%u minimum_in_native_basis=%u within_six=%u minimum_contiguous_runs=%u\n",w,h,d,b,n,best_count,best_count<=6,runs[a.levels]);
 free(actual);free(seen);resources++;
}
int main(void){unsigned dims[][3]={{4,4,8},{32,32,4},{64,2048,2},{2048,64,2},{129,129,3},{513,257,1},{1,17,9},{17,1,9},{63,65,3}};
 for(unsigned b=4;b<=16;b*=2)for(unsigned i=0;i<sizeof(dims)/sizeof(dims[0]);i++)cover(dims[i][0],dims[i][1],dims[i][2],b);
 printf("WINDOWS_AIL_COALESCING_PASS resources=%u mips=%u exact_bytes=%llu max_minimum_native_basis=%u GENERAL_ADMISSION_UNPROVEN\n",resources,covered_mips,(unsigned long long)compared,max_cover);
}
