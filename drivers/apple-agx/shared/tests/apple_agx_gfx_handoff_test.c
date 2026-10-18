#include "apple_agx_gfx_handoff.h"
#include <assert.h>
#include <string.h>

typedef struct _FAKE { unsigned char m[J313_AGX_G2_HANDOFF_SIZE];
  unsigned long long now; unsigned int relax; } FAKE;
static unsigned char r8(void *c,unsigned int o,unsigned char *v){FAKE*f=c;if(o>=sizeof(f->m))return 0;*v=f->m[o];return 1;}
static unsigned char r32(void*c,unsigned int o,unsigned int*v){FAKE*f=c;unsigned int i;if(o>sizeof(f->m)-4)return 0;*v=0;for(i=0;i<4;i++)*v|=(unsigned int)f->m[o+i]<<(i*8);return 1;}
static unsigned char r64(void*c,unsigned int o,unsigned long long*v){FAKE*f=c;unsigned int i;if(o>sizeof(f->m)-8)return 0;*v=0;for(i=0;i<8;i++)*v|=(unsigned long long)f->m[o+i]<<(i*8);return 1;}
static unsigned char w8(void*c,unsigned int o,unsigned char v){FAKE*f=c;if(o>=sizeof(f->m))return 0;f->m[o]=v;return 1;}
static unsigned char w32(void*c,unsigned int o,unsigned int v){FAKE*f=c;unsigned int i;if(o>sizeof(f->m)-4)return 0;for(i=0;i<4;i++)f->m[o+i]=(unsigned char)(v>>(i*8));return 1;}
static unsigned char w64(void*c,unsigned int o,unsigned long long v){FAKE*f=c;unsigned int i;if(o>sizeof(f->m)-8)return 0;for(i=0;i<8;i++)f->m[o+i]=(unsigned char)(v>>(i*8));return 1;}
static void barrier(void*c){(void)c;} static void relax(void*c){FAKE*f=c;f->now++;f->relax++;}
static unsigned long long now(void*c){return ((FAKE*)c)->now;}
static APPLE_AGX_GFX_HANDOFF_STATE bind(FAKE*f){APPLE_AGX_GFX_HANDOFF_STATE s;APPLE_AGX_GFX_HANDOFF_REGION r={J313_AGX_G2_HANDOFF_BASE,(unsigned int)J313_AGX_G2_HANDOFF_SIZE};APPLE_AGX_GFX_HANDOFF_IO io={f,r8,r32,r64,w8,w32,w64,barrier,relax,now};memset(&s,0,sizeof(s));assert(AppleAgxGfxHandoffBindJ313(&s,&r,&io)==AppleAgxGfxHandoffResultOk);return s;}
static void store64(FAKE*f,unsigned int o,unsigned long long v){assert(w64(f,o,v));}
static unsigned long long load64(FAKE*f,unsigned int o){unsigned long long v=0;assert(r64(f,o,&v));return v;}
int main(void){FAKE f;APPLE_AGX_GFX_HANDOFF_STATE s;unsigned int i;memset(&f,0,sizeof(f));s=bind(&f);assert(AppleAgxGfxHandoffAcquire(&s,10)==AppleAgxGfxHandoffResultOk);assert(s.Locked);assert(AppleAgxGfxHandoffRelease(&s)==AppleAgxGfxHandoffResultOk);assert(!s.Locked);memset(&f,0,sizeof(f));f.m[APPLE_AGX_GFX_HANDOFF_LOCK_FW_OFFSET]=1;s=bind(&f);assert(AppleAgxGfxHandoffAcquire(&s,3)==AppleAgxGfxHandoffResultTimeout);assert(!f.m[APPLE_AGX_GFX_HANDOFF_LOCK_AP_OFFSET]);memset(&f,0,sizeof(f));store64(&f,APPLE_AGX_GFX_HANDOFF_MAGIC_FW_OFFSET,APPLE_AGX_GFX_HANDOFF_PPL_MAGIC);for(i=0;i<APPLE_AGX_GFX_HANDOFF_FLUSH_COUNT;i++){store64(&f,APPLE_AGX_GFX_HANDOFF_FLUSH_STATE_OFFSET+i*APPLE_AGX_GFX_HANDOFF_FLUSH_STRIDE,1);store64(&f,APPLE_AGX_GFX_HANDOFF_FLUSH_ADDR_OFFSET+i*APPLE_AGX_GFX_HANDOFF_FLUSH_STRIDE,2);store64(&f,APPLE_AGX_GFX_HANDOFF_FLUSH_SIZE_OFFSET+i*APPLE_AGX_GFX_HANDOFF_FLUSH_STRIDE,3);}s=bind(&f);assert(AppleAgxGfxHandoffInitialize(&s,10)==AppleAgxGfxHandoffResultOk);assert(s.Initialized);assert(load64(&f,APPLE_AGX_GFX_HANDOFF_MAGIC_AP_OFFSET)==APPLE_AGX_GFX_HANDOFF_PPL_MAGIC);for(i=0;i<APPLE_AGX_GFX_HANDOFF_FLUSH_COUNT;i++){assert(load64(&f,APPLE_AGX_GFX_HANDOFF_FLUSH_STATE_OFFSET+i*APPLE_AGX_GFX_HANDOFF_FLUSH_STRIDE)==0);assert(load64(&f,APPLE_AGX_GFX_HANDOFF_FLUSH_ADDR_OFFSET+i*APPLE_AGX_GFX_HANDOFF_FLUSH_STRIDE)==0);assert(load64(&f,APPLE_AGX_GFX_HANDOFF_FLUSH_SIZE_OFFSET+i*APPLE_AGX_GFX_HANDOFF_FLUSH_STRIDE)==0);}memset(&f,0,sizeof(f));s=bind(&f);assert(AppleAgxGfxHandoffInitialize(&s,3)==AppleAgxGfxHandoffResultTimeout);assert(!s.Initialized);assert(!s.Locked);return 0;}
