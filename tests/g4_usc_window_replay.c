/* Callback shells model process reservation ownership; production bodies own
 * attach, binding, native USC conversion and parser field walking. */
#include "agx_win32_gpuva.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_g4_submit.c"

#define USC_BASE 0x1100000000ULL
#define USC_END  0x1200000000ULL
#define PAGE 0x10000ULL
typedef unsigned long long APPLE_AGX_U64;
struct agx_device;
struct agx_bo;
typedef struct { uint64_t Generation; void *Context;
  struct { const AGX_WIN32_GPUVA_OPS *GpuvaOps; } Operations;
} AGX_WIN32_SCREEN;
typedef struct {
  int (*Enter)(void *, AGX_WIN32_SCREEN *); void (*Leave)(void *);
  int (*Associate)(void); int (*Detach)(void); int (*Identity)(void);
  int (*NextBo)(void);
} AGX_WIN32_ASAHI_OWNER_OPS;
typedef struct {
  struct agx_device *Native; int Buffers; AGX_WIN32_GPUVA_SPACE Gpuva;
  AGX_WIN32_ASAHI_OWNER_OPS Ops; void *Owner;
  unsigned LiveBos, Failed, EncoderAllocationIntent, GpuvaReady;
  void *UnpublishedBo, *ActiveCapture, *ActiveEmission;
} AGX_WIN32_ASAHI_BACKEND;
struct agx_device { void *windows_private; uint64_t shader_base;
  struct { void (*bo_mmap)(struct agx_device *,struct agx_bo *,void *); } ops;
};
#define AgxWin32NativeDeviceSuccess 0
static int AgxWin32NativeDeviceInitialize(int *buffers, AGX_WIN32_SCREEN *screen,
    APPLE_AGX_U64 base, uint64_t generation) {
  (void)buffers;(void)screen;(void)base;(void)generation; return 0;
}
static void native_map(struct agx_device *dev,struct agx_bo *bo,void *ptr) {
  (void)dev;(void)bo;(void)ptr;
}
#include "usc_functions.inc"

typedef struct {
  uint64_t relative, forced, minimum, maximum, mapped_va, mapped_bytes;
  unsigned force, maps, frees, reserves, enters, protection;
  int free_result;
} FIXTURE;
static int reserve_va(void *ctx,uint64_t bytes,uint64_t minimum,
    uint64_t maximum,uint64_t *va) {
  FIXTURE *f=ctx; ++f->reserves; f->minimum=minimum; f->maximum=maximum;
  (void)bytes;
  *va=f->force?f->forced:minimum+f->relative; return 1;
}
static int map_va(void *ctx,uint64_t allocation,uint64_t va,uint64_t pages,
    unsigned protection,uint64_t *fence) {
  FIXTURE *f=ctx; assert(allocation==17); ++f->maps;
  f->protection=protection;
  f->mapped_va=va; f->mapped_bytes=pages<<12; *fence=0; return 1;
}
static int free_va(void *ctx,uint64_t va,uint64_t bytes) {
  FIXTURE *f=ctx; assert(va && bytes); ++f->frees; return f->free_result;
}
static int resident(void *c,const uint64_t *a,unsigned n,uint64_t *f) {
  (void)c;(void)a;(void)n;*f=0;return 1;
}
static int wait_fence(void *c,uint64_t f) {(void)c;(void)f;return 1;}
static int submit(void *c,const uint64_t *w,unsigned n,uint64_t v,uint32_t z,
    const void *p,uint32_t b,uint64_t *f) {
  (void)c;(void)w;(void)n;(void)v;(void)z;(void)p;(void)b;*f=1;return 1;
}
static int evict(void *c,const uint64_t *a,unsigned n) {
  (void)c;(void)a;(void)n;return 1;
}
static AGX_WIN32_GPUVA_OPS ops={reserve_va,map_va,free_va,resident,wait_fence,
  submit,wait_fence,evict,NULL,NULL};
static int enter(void *c,AGX_WIN32_SCREEN *s) {
  FIXTURE *f=c; (void)s; ++f->enters;return 1;
}
static void leave(void *c) {(void)c;}
static int identity_shell(void) {return 1;}
static AGX_WIN32_ASAHI_OWNER_OPS owner_ops={enter,leave,identity_shell,
  identity_shell,identity_shell,identity_shell};
#ifdef APPLE_AGX_GPUVA_WINSYS
static int access_va(void *ctx,unsigned long long va,unsigned int bytes,
    int write,APPLE_AGX_G4_ACCESS_KIND kind,unsigned ordinal) {
  FIXTURE *f=ctx;(void)write;(void)kind;(void)ordinal;
  if(va>=0x50000 && va+bytes<=0x70000) return 1;
  return va>=f->mapped_va && va+bytes<=f->mapped_va+f->mapped_bytes;
}
static void producer_to_parser(uint64_t relative,unsigned offset,int executable) {
  FIXTURE f={.relative=relative,.free_result=1};
  AGX_WIN32_SCREEN screen={.Generation=1,.Context=&f,.Operations={&ops}};
  AGX_WIN32_ASAHI_BACKEND backend={0}; struct agx_device native={0};
  AGX_WIN32_GPUVA_BO bo={0}; APPLE_AGX_G4_FAILURE failure={0};
  AGX4_ACCESS_STATE access={.Typed=access_va,.Context=&f,
    .Failure=&failure,.Ordinal=11};
  assert(AgxWin32AsahiAttach(&backend,&native,&screen,&owner_ops,&f,USC_BASE));
  unsigned protection=AGX_GPUVA_MAP_WRITE|
    (executable?AGX_GPUVA_MAP_EXECUTE:0);
  assert(AgxWin32GpuvaBind(&backend.Gpuva,&bo,17,PAGE,1,protection));
  /* agx_build_bg_eot assigns t.gpu to its uint32 usc field. With the
   * required 4GiB-aligned base this equals native agx_usc_addr. */
  uint32_t pipeline=(uint32_t)(bo.Va+offset);
  assert(pipeline==agx_usc_addr(&native,bo.Va+offset));
  APPLE_AGX_G4_NATIVE_RENDER render={.WidthPx=16,.HeightPx=16,.Layers=1,
    .Samples=1,.SampleSizeBytes=4,.UtileWidthPx=32,.UtileHeightPx=32,
    .VdmCtrlStreamBase=0x50000,.IspScissorBase=0x60100,.IspDbiasBase=0x60200};
  if(executable) render.VertexHelper.Binary=agx_usc_addr(&native,bo.Va+offset);
  else render.Bg.Usc=pipeline|4u;
  APPLE_AGX_G4_PARSE_RESULT result=validate_render((const unsigned char *)&render,&access);
  printf("mapped=0x%llx shader_base=0x%llx packed=0x%x result=%u ordinal=%u va=0x%llx\n",
    (unsigned long long)bo.Va,(unsigned long long)native.shader_base,
    render.Bg.Usc,result,failure.Ordinal,failure.Va);
  fflush(stdout);
  assert(result==AppleAgxG4ParseOk);
  assert(native.shader_base==USC_BASE);
  assert(f.minimum==USC_BASE+PAGE && f.maximum==USC_END);
  assert(f.maps==1 && !f.frees && f.protection==protection);
  assert(AgxWin32GpuvaUnbind(&backend.Gpuva,&bo));
  assert(f.frees==1 && !bo.Bound);
}
static void bind_bounds(void) {
  const uint64_t bad[]={0,PAGE,USC_BASE,USC_BASE+PAGE+1,USC_END,
    USC_END-PAGE,UINT64_MAX-0xffff};
  for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
    FIXTURE f={.force=1,.forced=bad[i],.free_result=1};
    AGX_WIN32_GPUVA_SPACE space={.Ops=ops,.Context=&f};
    AGX_WIN32_GPUVA_BO bo={0};
    assert(!AgxWin32GpuvaBind(&space,&bo,17,2*PAGE,1,
      AGX_GPUVA_MAP_WRITE|AGX_GPUVA_MAP_EXECUTE));
    assert(!f.maps && !bo.Bound && !space.Terminal);
    assert(f.frees==(bad[i]!=0));
  }
  FIXTURE f={.force=1,.forced=USC_END-PAGE,.free_result=1};
  AGX_WIN32_GPUVA_SPACE space={.Ops=ops,.Context=&f};
  AGX_WIN32_GPUVA_BO bo={0};
  assert(AgxWin32GpuvaBind(&space,&bo,17,PAGE,1,
    AGX_GPUVA_MAP_WRITE|AGX_GPUVA_MAP_EXECUTE));
  assert(bo.Va==USC_END-PAGE && bo.Bytes==PAGE);
  f=(FIXTURE){.force=1,.forced=PAGE,.free_result=0};
  space=(AGX_WIN32_GPUVA_SPACE){.Ops=ops,.Context=&f};bo=(AGX_WIN32_GPUVA_BO){0};
  assert(!AgxWin32GpuvaBind(&space,&bo,17,PAGE,1,0));
  assert(space.Terminal && f.frees==1 && !f.maps);
  f=(FIXTURE){.free_result=1};
  space=(AGX_WIN32_GPUVA_SPACE){.Ops=ops,.Context=&f};
  assert(!AgxWin32GpuvaBind(&space,&bo,17,0x100000000ULL,1,0));
  assert(!f.reserves);
  assert(AgxWin32GpuvaBind(&space,&bo,17,PAGE,0,
    AGX_GPUVA_MAP_WRITE|AGX_GPUVA_MAP_EXECUTE));
  assert(f.minimum==PAGE && f.maximum==(1ULL<<39) && bo.Va==PAGE);
  AGX_WIN32_SCREEN screen={.Generation=1,.Context=&f,.Operations={&ops}};
  AGX_WIN32_ASAHI_BACKEND backend={0}; struct agx_device native={0};
  assert(!AgxWin32AsahiAttach(&backend,&native,&screen,&owner_ops,&f,0x1200000000ULL));
  assert(!f.enters && !native.windows_private);
}
int main(void) {
  producer_to_parser(0x2a0000,0x140,0);
  producer_to_parser(0x820000,0x8c0,0);
  producer_to_parser(0xf10000,0x240,1);
  bind_bounds();
  puts("R149 USC window producer/parser and fail-closed bounds: PASS");return 0;
}
#else
int main(void) {
  FIXTURE f={0};
  AGX_WIN32_SCREEN screen={.Generation=1,.Context=&f,.Operations={&ops}};
  AGX_WIN32_ASAHI_BACKEND backend={0}; struct agx_device native={0};
  const uint64_t construction_base=0x2100000000ULL;
  assert(AgxWin32AsahiAttach(&backend,&native,&screen,&owner_ops,&f,
    construction_base));
  assert(native.shader_base==construction_base && !backend.GpuvaReady);
  assert(agx_usc_addr(&native,construction_base+0x1840)==0x1840);
  assert(f.enters==1 && !f.reserves && !f.maps);
  puts("R149 non-GPUVA construction coordinates unchanged: PASS");return 0;
}
#endif
