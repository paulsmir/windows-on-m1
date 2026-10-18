#include <assert.h>
#include <stdio.h>
#include "windows_swizzle_reference.h"
int main(void) {
 WinSwizzle0004 p;WinSwizzleAddressing a;uint64_t out;
 assert(win_morton_candidate(4,2,2,&p,&a));a.row_stride=48;a.depth_stride=192;a.base=128;
 // Hand-derived2x2 byte-preserving tile: X1 ->4,Y1 ->8; next tile16.
 uint32_t v[4]={4,1,2,0};assert(win_swizzle_0004(&p,&a,v,&out)==1 &&out==128+384+12);
 v[0]=8;v[1]=3;v[2]=0;assert(win_swizzle_0004(&p,&a,v,&out)==1 &&out==128+16+48+8);
 // Higher coordinate XOR and constant operate on only spatial output bits.
 p.xor_a[2]=(WinSwizzleBit){1,1,2};p.xor_b[3]=(WinSwizzleBit){1,2,0};p.xor_constant=4;
 v[0]=0;v[1]=4;v[2]=1;assert(win_swizzle_0004(&p,&a,v,&out)==1 &&out==128+96+192+8);
 p.xor_a[2].valid=p.xor_b[3].valid=0;p.xor_constant=0;
 a.pre[0]=4;a.pre[1]=1;v[0]=1;v[1]=0;v[2]=0;assert(win_swizzle_0004(&p,&a,v,&out)==1 &&out==128+13);
 p.flags=1;assert(win_swizzle_0004(&p,&a,v,&out)==WIN_SWIZZLE_UNSUPPORTED);p.flags=0;
 p.primary[2].bit=32;assert(win_swizzle_0004(&p,&a,v,&out)==WIN_SWIZZLE_INVALID);
 assert(win_morton_candidate(16,32,32,&p,&a));a.row_stride=16384;a.depth_stride=65536;
 v[0]=15;v[1]=0;assert(win_swizzle_0004(&p,&a,v,&out)==1 &&out==15);
 a.base=UINT64_MAX;v[0]=1;assert(win_swizzle_0004(&p,&a,v,&out)==WIN_SWIZZLE_INVALID);
 puts("WINDOWS_0004_REFERENCE_PASS interleave,rowcarry,Z,byteoffset,XOR,preoffset,overflow,unsupported-flags");
}
