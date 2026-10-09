/* EXP979: production dispose() with shells for the owner, native screen and
 * VA callbacks. A held screen slot must keep its VA until the slot and its
 * allocation are released. */
#include "agx_win32_gpuva.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned long long APPLE_AGX_U64;
typedef unsigned APPLE_AGX_U32;
struct agx_bo { int refcnt; void *_map; };
typedef struct { void *Screen; } AGX_WIN32_NATIVE_DEVICE;
typedef struct {
  int (*Associate)(void *, APPLE_AGX_U64, const void *, APPLE_AGX_U64, void *);
  int (*Detach)(void *, APPLE_AGX_U64, const void *, APPLE_AGX_U64);
} AGX_WIN32_ASAHI_OWNER_OPS;
typedef struct {
  AGX_WIN32_NATIVE_DEVICE Buffers; AGX_WIN32_ASAHI_OWNER_OPS Ops; void *Owner;
  APPLE_AGX_U32 LiveBos; int Failed;
  AGX_WIN32_GPUVA_SPACE Gpuva;
  AGX_WIN32_GPUVA_BO PendingVa[64];
  APPLE_AGX_U32 PendingVaCount;
} AGX_WIN32_ASAHI_BACKEND;
typedef struct { void *CpuAddress; struct { struct { APPLE_AGX_U64 Token; } Transport; } Buffer;
  APPLE_AGX_U64 ConstructionSerial; } BACKING;
struct windows_bo { struct agx_bo Base; AGX_WIN32_ASAHI_BACKEND *Backend; BACKING Backing; AGX_WIN32_GPUVA_BO Gpuva; };
#define AgxWin32NativeBoSuccess 0
#define AgxWin32NativeDeviceSuccess 0
static int destroy_fails, destroys, frees, associates;
static int AgxWin32NativeBoUnmap(void *s, BACKING *b) { (void)s; b->CpuAddress = NULL; return 0; }
static int AgxWin32NativeDeviceDestroyBo(AGX_WIN32_NATIVE_DEVICE *d, BACKING *b) {
  (void)d; (void)b; if (destroy_fails) return 1; ++destroys; return 0; }
static void release_map(void) {}
static int associate(void *o, APPLE_AGX_U64 t, const void *b, APPLE_AGX_U64 s, void *r) {
  (void)o;(void)t;(void)b;(void)s;(void)r; ++associates; return 1; }
static int detach(void *o, APPLE_AGX_U64 t, const void *b, APPLE_AGX_U64 s) {
  (void)o;(void)t;(void)b;(void)s; return 1; }
static int free_va(void *c, uint64_t va, uint64_t bytes) { (void)c;(void)va;(void)bytes; ++frees; return 1; }
static int reserve_va(void*c,uint64_t b,uint64_t mn,uint64_t mx,uint64_t*va){(void)c;(void)b;(void)mx;*va=mn;return 1;}
static int map_va(void*c,uint64_t a,uint64_t v,uint64_t p,unsigned pr,uint64_t*f){(void)c;(void)a;(void)v;(void)p;(void)pr;*f=0;return 1;}
static int res(void*c,const uint64_t*a,unsigned n,uint64_t*f){(void)c;(void)a;(void)n;*f=0;return 1;}
static int waitf(void*c,uint64_t f){(void)c;(void)f;return 1;}
static int sub(void*c,const uint64_t*w,unsigned n,uint64_t v,uint32_t z,const void*p,uint32_t b,uint64_t*f){(void)c;(void)w;(void)n;(void)v;(void)z;(void)p;(void)b;*f=1;return 1;}
static int ev(void*c,const uint64_t*a,unsigned n){(void)c;(void)a;(void)n;return 1;}
#define associate_cb(x) x
#include "dispose_function.inc"
static struct windows_bo *bound_bo(AGX_WIN32_ASAHI_BACKEND *b) {
  struct windows_bo *bo = calloc(1, sizeof(*bo));
  bo->Backend = b; ++b->LiveBos;
  assert(AgxWin32GpuvaBind(&b->Gpuva, &bo->Gpuva, 17, 0x40000, 0, 0));
  return bo;
}
int main(void) {
  static AGX_WIN32_GPUVA_OPS ops = {reserve_va,map_va,free_va,res,waitf,sub,waitf,ev,NULL,NULL};
  AGX_WIN32_ASAHI_BACKEND b; memset(&b, 0, sizeof(b));
  b.Ops.Associate = associate; b.Ops.Detach = detach;
  assert(AgxWin32GpuvaInit(&b.Gpuva, &ops, &b));
  /* 1. Screen slot still held: nothing is released, the VA stays bound. */
  struct windows_bo *held = bound_bo(&b);
  destroy_fails = 1;
  assert(dispose(held) == 0);
  assert(held->Gpuva.Bound && frees == 0 && associates == 1);
  /* 2. Slot released: allocation first, then the VA. */
  destroy_fails = 0;
  assert(dispose(held) == 1 && destroys == 1 && frees == 1);
  /* 3. Submission in flight: allocation released, VA deferred, not leaked. */
  struct windows_bo *busy = bound_bo(&b);
  uint64_t fake = 1; b.Gpuva.Held = &fake; b.Gpuva.HeldCount = 1;
  assert(dispose(busy) == 1 && destroys == 2 && frees == 1 && b.PendingVaCount == 1);
  b.Gpuva.Held = NULL; b.Gpuva.HeldCount = 0;
  assert(AgxWin32GpuvaUnbind(&b.Gpuva, &b.PendingVa[0]) && frees == 2);
  puts("EXP979 dispose order: PASS");
  return 0;
}
