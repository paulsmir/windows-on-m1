/* Original CPU address oracle; compares with linked, unmodified MIT AIL. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asahi/layout/layout.h"
static unsigned mn(unsigned a,unsigned b){return a<b?a:b;}
static unsigned mx(unsigned a,unsigned b){return a>b?a:b;}
static unsigned mini(unsigned a,unsigned l){return mx(a>>l,1);}
static unsigned pot(unsigned a){unsigned p=1;while(p<a)p*=2;return p;}
static uint64_t align128(uint64_t a){return (a+127)&~127ull;}
static uint64_t spread(unsigned x){uint64_t r=0;for(unsigned i=0;i<16;i++)r|=(uint64_t)((x>>i)&1)<<(2*i);return r;}
uint64_t c21_ail_reference_offset(const struct ail_layout *a, unsigned l,
 unsigned x, unsigned y, unsigned z, unsigned byte) {
 unsigned b=util_format_get_blocksize(a->format);
 unsigned tx=a->tilesize_el[l].width_el,ty=a->tilesize_el[l].height_el;
 uint64_t tile=(uint64_t)(y/ty)*((a->stride_el[l]+tx-1)/tx)+x/tx;
 return a->level_offsets_B[l]+z*a->layer_stride_B+
   b*(tile*tx*ty+spread(x%tx)+2*spread(y%ty))+byte;
}
static unsigned cases,levels,slices; static uint64_t checked;
static void check(unsigned w,unsigned h,unsigned d,unsigned b,unsigned mip,unsigned writable){
 enum pipe_format fs[]={PIPE_FORMAT_R8_UINT,PIPE_FORMAT_R16_UINT,PIPE_FORMAT_R32_UINT,PIPE_FORMAT_R32G32_UINT,PIPE_FORMAT_R32G32B32A32_UINT};
 unsigned bi=0;while((1u<<bi)!=b)bi++;
 struct ail_layout a={.width_px=w,.height_px=h,.depth_px=d,.sample_count_sa=1,.levels=mip?2:1,.mipmapped_z=true,.tiling=AIL_TILING_GPU,.format=fs[bi],.writeable_image=writable};
 ail_make_miptree(&a);cases++;
 unsigned tx=128,ty=128;for(unsigned bytes=1;bytes<b;bytes*=2){if(tx==ty)ty/=2;else tx/=2;}
 unsigned p=0;while(mini(w,p)>=tx&&mini(h,p)>=ty)p++;
 unsigned sx=(w+tx-1)/tx,sy=(h+ty-1)/ty,pw=pot(mini(w,p)),ph=pot(mini(h,p));uint64_t off=0;
 for(unsigned l=0;l<a.levels;l++){
  assert(a.level_offsets_B[l]==off);unsigned tw,th;
  if(l<p){unsigned mask=(1u<<l)-1;unsigned tiles=(sx*sy)>>(2*l);if(sx&mask)tiles+=sy>>l;if(sy&mask)tiles+=sx>>l;if((sx&mask)&&(sy&mask))tiles++;off=align128(off+(uint64_t)tiles*tx*ty*b);tw=tx;th=ty;}
  else {tw=th=pot(mini(mn(w,h),l));off=align128(off+(uint64_t)pw*ph*b);pw=mx(pw/2,1);ph=mx(ph/2,1);}
  assert(a.tilesize_el[l].width_el==tw&&a.tilesize_el[l].height_el==th);assert(a.stride_el[l]==mini(w,l));
 }
 assert(a.level_offsets_B[a.levels]==off);
 uint64_t stride=((a.levels!=1&&off>16384&&d>1)||writable)?(off+16383)&~16383ull:off;
 assert(a.layer_stride_B==stride&&a.size_B==stride*d);
 uint8_t *tiled=malloc(a.size_B),*expected=malloc(a.size_B),*occupied=calloc(a.size_B,1);assert(tiled&&expected&&occupied);memset(tiled,0xa5,a.size_B);memset(expected,0xa5,a.size_B);
 for(unsigned l=0;l<a.levels;l++){
  unsigned lw=mini(w,l),lh=mini(h,l),ld=mini(d,l);
  size_t n=(size_t)lw*lh*b;uint8_t *linear=malloc(n),*roundtrip=malloc(n);assert(linear&&roundtrip);
  for(unsigned z=0;z<ld;z++){
   for(size_t i=0;i<n;i++)linear[i]=(uint8_t)(i*131u+z*37u+l*19u);
   uint64_t base=z*stride+a.level_offsets_B[l];
   ail_tile(tiled+base,linear,&a,l,lw*b,0,0,lw,lh);
   for(unsigned y=0;y<lh;y++)for(unsigned x=0;x<lw;x++){
    uint64_t addr=c21_ail_reference_offset(&a,l,x,y,z,0);assert(addr+b<=a.size_B);
    for(unsigned q=0;q<b;q++){assert(!occupied[addr+q]);occupied[addr+q]=1;}
    memcpy(expected+addr,linear+((size_t)y*lw+x)*b,b);checked+=b;
   }
   memset(roundtrip,0,n);ail_detile(tiled+base,roundtrip,&a,l,lw*b,0,0,lw,lh);assert(!memcmp(linear,roundtrip,n));slices++;
  }
  free(linear);free(roundtrip);levels++;
 }
 assert(!memcmp(tiled,expected,a.size_B));free(tiled);free(expected);free(occupied);
}
int main(void){unsigned dims[][3]={{1,1,1},{4,4,8},{32,32,4},{33,33,7},{63,65,3},{64,64,2},{65,63,5},{127,129,2},{129,129,3},{257,193,2},{513,257,1},{17,1,9},{1,17,9}};
 for(unsigned b=1;b<=16;b*=2)for(unsigned i=0;i<sizeof(dims)/sizeof(dims[0]);i++)for(unsigned mip=0;mip<2;mip++)for(unsigned wr=0;wr<2;wr++)check(dims[i][0],dims[i][1],dims[i][2],b,mip,wr);
 printf("AIL_ORACLE_PASS cases=%u levels=%u slices=%u logical_bytes=%llu full_padding_compare=YES\n",cases,levels,slices,(unsigned long long)checked);return 0;}
