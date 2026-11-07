/* Original CPU reference for the non-stacked0004 parameterized-swizzle subset.
 * SPDX-License-Identifier: MIT
 *
 * Specification/limitations: D3D12-C21-volume-implementation.md. This is test
 * infrastructure, not driver capability advertisement or imported spec code.
 */
#ifndef WINDOWS_SWIZZLE_REFERENCE_H
#define WINDOWS_SWIZZLE_REFERENCE_H
#include <stdint.h>
#include <string.h>

typedef struct { uint8_t valid,channel,bit; } WinSwizzleBit;
typedef struct {
 WinSwizzleBit primary[32],xor_a[32],xor_b[32];
 uint32_t xor_constant,flags;
} WinSwizzle0004;
typedef struct {
 uint8_t block_bits,channel_bits[4],byte_bits;
 uint32_t pre[4];
 uint64_t base,row_stride,depth_stride;
} WinSwizzleAddressing;
enum { WIN_SWIZZLE_OK=1,WIN_SWIZZLE_INVALID=0,WIN_SWIZZLE_UNSUPPORTED=-1 };
static int win_add64(uint64_t a,uint64_t b,uint64_t *r) {
 if(b>UINT64_MAX-a)return 0;*r=a+b;return 1;
}
static int win_mul64(uint64_t a,uint64_t b,uint64_t *r) {
 if(a && b>UINT64_MAX/a)return 0;*r=a*b;return 1;
}
static int win_swizzle_0004(const WinSwizzle0004 *p,const WinSwizzleAddressing *a,
 const uint32_t source[4],uint64_t *out) {
 if(!p || !a || !source || !out)return WIN_SWIZZLE_INVALID;
 *out=0;
 if(p->flags)return WIN_SWIZZLE_UNSUPPORTED; /* stacked-depth semantics not needed for AIL */
 if(a->block_bits>32 || a->byte_bits>a->block_bits || a->byte_bits>4)return WIN_SWIZZLE_INVALID;
 unsigned total=0;uint64_t address[4],index[4];
 for(unsigned c=0;c<4;c++) {
  if(a->channel_bits[c]>32)return WIN_SWIZZLE_INVALID;
  total+=a->channel_bits[c];
  address[c]=(uint64_t)source[c]+a->pre[c];
  if(address[c]>UINT32_MAX)return WIN_SWIZZLE_INVALID;
  index[c]=address[c]>>a->channel_bits[c];
 }
 if(total!=a->block_bits || a->channel_bits[0]<a->byte_bits || index[3])return WIN_SWIZZLE_INVALID;
 const uint64_t block=UINT64_C(1)<<a->block_bits;
 // Restrict constant XOR to low block bits: order relative to row/depth
 // additions cannot then change the result. No0022 postamble is accepted.
 if(p->xor_constant>=block || (p->xor_constant && (a->row_stride%block || a->depth_stride%block)))
  return WIN_SWIZZLE_UNSUPPORTED;
 uint64_t within=address[0]&((UINT64_C(1)<<a->byte_bits)-1);
 for(unsigned n=0;n<32;n++) {
  WinSwizzleBit v[3]={p->primary[n],p->xor_a[n],p->xor_b[n]};
  if(n>=a->block_bits) {
   if(v[0].valid || v[1].valid || v[2].valid)return WIN_SWIZZLE_INVALID;
   continue;
  }
  if(!v[0].valid)return WIN_SWIZZLE_INVALID;
  unsigned value=0;
  for(unsigned k=0;k<3;k++) {
   if(!v[k].valid)continue;
   if(v[k].channel>=4 || v[k].bit>=32)return WIN_SWIZZLE_INVALID;
   if(!k && v[k].bit>=a->channel_bits[v[k].channel])return WIN_SWIZZLE_INVALID;
   value^=(unsigned)((address[v[k].channel]>>v[k].bit)&1);
  }
  if(n>=a->byte_bits)within|=(uint64_t)value<<n;
 }
 if(p->xor_constant&((UINT64_C(1)<<a->byte_bits)-1))return WIN_SWIZZLE_INVALID;
 within^=p->xor_constant;
 uint64_t x,y,z,result;
 if(!win_mul64(index[0],block,&x) || !win_mul64(index[1],a->row_stride,&y) ||
    !win_mul64(index[2],a->depth_stride,&z) || !win_add64(x,y,&result) ||
    !win_add64(result,z,&result) || !win_add64(result,within,&result) ||
    !win_add64(result,a->base,out))return WIN_SWIZZLE_INVALID;
 return WIN_SWIZZLE_OK;
}

// Candidate builder is original math: byte-preserving Morton within a power-
// of-two tile, external row/depth strides. It is not a production layout ID.
static int win_morton_candidate(unsigned bytes,unsigned tx,unsigned ty,
 WinSwizzle0004 *p,WinSwizzleAddressing *a) {
 if(!p || !a || !bytes || bytes>16 || (bytes&(bytes-1)) || !tx || !ty ||
    (tx&(tx-1)) || (ty&(ty-1)) || tx<ty || tx>2*ty)return 0;
 memset(p,0,sizeof(*p));memset(a,0,sizeof(*a));
 unsigned b=0,xb=0,yb=0,n=0;
 while((1u<<b)<bytes)b++;while((1u<<xb)<tx)xb++;while((1u<<yb)<ty)yb++;
 if(b+xb+yb>32)return 0;
 a->byte_bits=b;a->channel_bits[0]=b+xb;a->channel_bits[1]=yb;
 for(unsigned k=0;k<b;k++)p->primary[n++]=(WinSwizzleBit){1,0,(uint8_t)k};
 for(unsigned k=0;k<xb;k++) {
  p->primary[n++]=(WinSwizzleBit){1,0,(uint8_t)(b+k)};
  if(k<yb)p->primary[n++]=(WinSwizzleBit){1,1,(uint8_t)k};
 }
 a->block_bits=n;return 1;
}
#endif
