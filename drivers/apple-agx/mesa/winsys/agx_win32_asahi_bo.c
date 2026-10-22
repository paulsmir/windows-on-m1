#include "agx_device.h"
#include "agx_win32_asahi_bo.h"

void (*AgxWin32BackendFailHook)(unsigned site);
#ifdef APPLE_AGX_GPUVA_WINSYS
extern void (*AgxWin32BatchRefusalHook)(unsigned, unsigned, unsigned, unsigned);
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(APPLE_AGX_GPUVA_WINSYS) && defined(_WIN32)
#include <malloc.h>
#endif

/* Windows replacement for the OS-dependent BO symbols used by native pool.c.
 * The default profile uses construction coordinates; G4 binds VidMm GPUVA. */
struct windows_bo {
  struct agx_bo Base;
  struct agx_va Coordinate;
  AGX_WIN32_NATIVE_BO Backing;
  AGX_WIN32_ASAHI_BACKEND *Backend;
#ifdef APPLE_AGX_GPUVA_WINSYS
  AGX_WIN32_GPUVA_BO Gpuva;
  int Imported;
  int Cached;
  /* EXP1207: the cached CPU map of an AGX_WIN32_BO_SHADOW BO (Base._map);
   * Backing.CpuAddress stays the allocation's write-combined view. */
  void *Shadow;
  /* C53 B1: a reserved (virtual) buffer. Coordinate is the reserved range;
   * the BO's own allocation is a 64 KiB anchor for its handle, residency
   * and in-flight tracking. Tiles[t] is the BO 64 KiB tile t maps (the tile
   * scratch while unmapped); C53 B4: Backings lists each distinct one with
   * its tile count and holds the references. */
  int Virtual;
  AGX_WIN32_GPUVA_BO Reserved;
  struct windows_bo **Tiles;
  unsigned TileCount;
  struct { struct windows_bo *Bo; unsigned Tiles; } *Backings;
  unsigned BackingCount;
#endif
};
static void native_map(struct agx_device *,struct agx_bo *,void *);

#ifdef APPLE_AGX_GPUVA_WINSYS
/* EXP1207: an x86 client runs emulated, where each scalar store into the
 * write-combined view of a Mesa allocation costs ~200 ns (EXP1206: 32-bit
 * stores 20 MB/s into WC, 1.3 GB/s into cached memory; memcpy into WC
 * 15 GB/s). Asahi packs descriptors, PPP/USC state and the VDM stream word by
 * word into the batch pools and the encoder (EXP1202: agx_encode_state
 * ~12 us per draw). Those are written only while their batch is recorded and
 * read by the GPU only after its submission, so they are recorded into a
 * cached shadow and copied into the allocation just before the submission. */
static void *shadow_alloc(size_t bytes) {
#ifdef _WIN32
  return _aligned_malloc(bytes,4096u);
#else
  void *p=NULL;
  return posix_memalign(&p,4096u,bytes) ? NULL : p;
#endif
}
static void shadow_free(void *p) {
#ifdef _WIN32
  _aligned_free(p);
#else
  free(p);
#endif
}
int AgxWin32AsahiShadowPublish(struct agx_bo *base,size_t bytes) {
  struct windows_bo *bo=(struct windows_bo *)base;
  if(!base || !((unsigned)base->flags&AGX_WIN32_BO_SHADOW) || !base->_map) return 1;
  if(base->_map!=bo->Shadow || !bo->Backing.CpuAddress) return 0;
  if(bytes>base->size) bytes=base->size;
  memcpy(bo->Backing.CpuAddress,bo->Shadow,bytes);
  return 1;
}
#endif

/* EXP1091: EXP1089 CSwitch samples put ~11 % of DWM's composition thread in
 * AgxWin32AsahiLookupBo: each referenced handle walked every registered BO
 * through the owner's NextBo, one shared lock per step. Index live BOs by
 * handle instead (linear probing, backward-shift deletion). */
static APPLE_AGX_U32 handle_slot(APPLE_AGX_U32 handle,APPLE_AGX_U32 mask) {
  return (handle*2654435761u)&mask;
}
static void handle_map_place(void **map,APPLE_AGX_U32 mask,struct windows_bo *bo) {
  APPLE_AGX_U32 i=handle_slot(bo->Base.handle,mask);
  while(map[i]) i=(i+1)&mask;
  map[i]=bo;
}
static int handle_map_put(AGX_WIN32_ASAHI_BACKEND *b,struct windows_bo *bo) {
  if((b->HandleMapCount+1u)*2u>b->HandleMapCap) {
    APPLE_AGX_U32 cap=b->HandleMapCap?b->HandleMapCap*2u:256u;
    void **map;
    if(cap<b->HandleMapCap || !(map=calloc(cap,sizeof(*map)))) return 0;
    for(APPLE_AGX_U32 i=0;i<b->HandleMapCap;++i)
      if(b->HandleMap[i]) handle_map_place(map,cap-1u,(struct windows_bo *)b->HandleMap[i]);
    free(b->HandleMap);
    b->HandleMap=map; b->HandleMapCap=cap;
  }
  handle_map_place(b->HandleMap,b->HandleMapCap-1u,bo);
  ++b->HandleMapCount;
  return 1;
}
static void handle_map_remove(AGX_WIN32_ASAHI_BACKEND *b,struct windows_bo *bo) {
  APPLE_AGX_U32 mask,i,j;
  if(!b->HandleMapCap) return;
  mask=b->HandleMapCap-1u;
  for(i=handle_slot(bo->Base.handle,mask);b->HandleMap[i]!=bo;i=(i+1)&mask)
    if(!b->HandleMap[i]) return;
  b->HandleMap[i]=NULL; --b->HandleMapCount;
  for(j=(i+1)&mask;b->HandleMap[j];j=(j+1)&mask) {
    APPLE_AGX_U32 home=handle_slot(((struct windows_bo *)b->HandleMap[j])->Base.handle,mask);
    /* Move the entry back unless its home lies cyclically in (i, j]. */
    if(i<=j ? (home<=i || home>j) : (home<=i && home>j)) {
      b->HandleMap[i]=b->HandleMap[j]; b->HandleMap[j]=NULL; i=j;
    }
  }
}
static struct windows_bo *handle_map_get(const AGX_WIN32_ASAHI_BACKEND *b,APPLE_AGX_U32 handle) {
  APPLE_AGX_U32 mask,i;
  if(!b->HandleMapCap) return NULL;
  mask=b->HandleMapCap-1u;
  for(i=handle_slot(handle,mask);b->HandleMap[i];i=(i+1)&mask)
    if(((struct windows_bo *)b->HandleMap[i])->Base.handle==handle)
      return (struct windows_bo *)b->HandleMap[i];
  return NULL;
}
static int release_map(const void *key,const void *expected,int commit) {
  struct windows_bo *bo=(struct windows_bo *)key;
  if(!bo || !expected || bo->Base._map!=expected ||
     bo->Backing.CpuAddress!=expected ||
     !bo->Backing.Buffer.Transport.Mapped) return 0;
  if(commit) {
    bo->Base._map=NULL;
    bo->Backing.CpuAddress=NULL;
    bo->Backing.Buffer.Transport.Mapped=APPLE_AGX_FALSE;
  }
  return 1;
}

#if defined(_WIN32)
#define AGX_WIN32_THREAD_LOCAL __declspec(thread)
#else
#define AGX_WIN32_THREAD_LOCAL _Thread_local
#endif
/* EXP1069: VAs of the current device's zero/scratch pages (agx_abi.h). */
AGX_WIN32_THREAD_LOCAL uint64_t agx_win32_zero_page_va;
AGX_WIN32_THREAD_LOCAL uint64_t agx_win32_scratch_page_va;
/* EXP1070: Asahi perf_debug messages (why a batch is flushed or synced,
 * shadowing, fallbacks), formatted and handed to the UMD, which counts them
 * per process. Formatting is skipped while no counter is installed. */
void (*AgxWin32PerfHook)(const char *message);
void AgxWin32PerfNote(const char *fmt, ...) {
  char message[64];
  va_list args;
  if(!AgxWin32PerfHook || !fmt) return;
  va_start(args,fmt);
  vsnprintf(message,sizeof(message),fmt,args);
  va_end(args);
  AgxWin32PerfHook(message);
}

void AgxWin32AsahiPublishPages(struct agx_device *native) {
  agx_win32_zero_page_va =
      native && native->zero_bo && native->zero_bo->va ? native->zero_bo->va->addr : 0;
  agx_win32_scratch_page_va =
      native && native->scratch_bo && native->scratch_bo->va ? native->scratch_bo->va->addr : 0;
}

int AgxWin32AsahiAttach(AGX_WIN32_ASAHI_BACKEND *b, struct agx_device *native,
    AGX_WIN32_SCREEN *screen, const AGX_WIN32_ASAHI_OWNER_OPS *ops, void *owner,
    APPLE_AGX_U64 base) {
  if(!b || !native || !screen || !ops || !owner || b->Native || native->windows_private ||
     !ops->Enter || !ops->Leave || !ops->Associate || !ops->Detach || !ops->Identity || !ops->NextBo)
    return 0;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(base!=APPLE_AGX_G4_USC_EXECUTION_BASE) return 0;
#endif
  if(!ops->Enter(owner,screen)) return 0;
  if(AgxWin32NativeDeviceInitialize(&b->Buffers,screen,base,screen->Generation)!=AgxWin32NativeDeviceSuccess) {
    ops->Leave(owner);
    return 0;
  }
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(!AgxWin32GpuvaInit(&b->Gpuva,screen->Operations.GpuvaOps,screen->Context)) {
    ops->Leave(owner);
    return 0;
  }
#endif
  b->Native=native; b->Ops=*ops; b->Owner=owner; b->LiveBos=0; b->Failed=0; b->UnpublishedBo=NULL;
  b->ActiveCapture=NULL; b->ActiveEmission=NULL; b->EncoderAllocationIntent=0;
  native->windows_private=b;
  native->shader_base=base;
#ifdef APPLE_AGX_GPUVA_WINSYS
  b->GpuvaReady=1;
#endif
  native->ops.bo_mmap=native_map;
  return 1;
}

#ifdef APPLE_AGX_GPUVA_WINSYS
static void virtual_release(struct windows_bo *bo);
#endif
static int dispose(struct windows_bo *bo) {
  AGX_WIN32_ASAHI_BACKEND *b=bo->Backend;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(bo->Virtual) virtual_release(bo);
#endif
  if(bo->Backing.CpuAddress) {
    if(AgxWin32NativeBoUnmap(b->Buffers.Screen,&bo->Backing)!=AgxWin32NativeBoSuccess)
      return 0;
    bo->Base._map=NULL;
  }
  /* A failed unmap/detach preserves the registered BO for later collection. */
  if(!b->Ops.Detach(b->Owner,bo->Backing.Buffer.Transport.Token,&bo->Base,
                   bo->Backing.ConstructionSerial)) return 0;
#ifdef APPLE_AGX_GPUVA_WINSYS
  /* EXP979: the screen slot may still be held by an in-progress copy or
   * submission. Freeing the VA first left that slot naming a released (and
   * soon reused) VA, and the copy QUERY then failed predicate57. Release the
   * slot and allocation first; free the VA only once nothing names it. */
  if(b->PendingVaCount>=sizeof(b->PendingVa)/sizeof(b->PendingVa[0])) {
    if(!b->Ops.Associate(b->Owner,bo->Backing.Buffer.Transport.Token,&bo->Base,
                        bo->Backing.ConstructionSerial,release_map)) AGX_WIN32_ASAHI_FAIL(b, 1u);
    return 0;
  }
#endif
  if(AgxWin32NativeDeviceDestroyBo(&b->Buffers,&bo->Backing)!=AgxWin32NativeDeviceSuccess) {
    if(!b->Ops.Associate(b->Owner,bo->Backing.Buffer.Transport.Token,&bo->Base,
                        bo->Backing.ConstructionSerial,release_map)) AGX_WIN32_ASAHI_FAIL(b, 1u);
    return 0;
  }
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(bo->Gpuva.Bound && !AgxWin32GpuvaUnbind(&b->Gpuva,&bo->Gpuva))
    b->PendingVa[b->PendingVaCount++]=bo->Gpuva;
#endif
  handle_map_remove(b,bo);
  --b->LiveBos;
#ifdef APPLE_AGX_GPUVA_WINSYS
  shadow_free(bo->Shadow);
#endif
  free(bo);
  return 1;
}

#ifdef APPLE_AGX_GPUVA_WINSYS
/* EXP996: every new BO costs AllocateCb, ReserveGpuVirtualAddress, a mapping
 * and residency paging-fence wait (EXP994 DWM: ~460 new BOs, 2553 paging waits,
 * 56 s). Keep released native BOs whole -- slot, VA, residency, CPU map -- and
 * hand them back to an equal request, as Mesa's agx_bo_cache does. */
static int cache_in_flight(const AGX_WIN32_ASAHI_BACKEND *b,const struct windows_bo *bo) {
  for(unsigned i=0;b->Gpuva.Held && i<b->Gpuva.HeldCount;++i)
    if(b->Gpuva.Held[i]==bo->Gpuva.Allocation) return 1;
  /* EXP1093: and the older submissions still in flight. */
  for(unsigned k=0;k<b->Gpuva.OlderCount;++k)
    for(unsigned i=0;i<b->Gpuva.Older[k].Count;++i)
      if(b->Gpuva.Older[k].Handles[i]==bo->Gpuva.Allocation) return 1;
  return 0;
}
/* Cache[] is kept in release order, oldest first. A refused dispose leaves
 * the BO for AgxWin32AsahiCollect, as cache_flush does. */
static void cache_evict_oldest(AGX_WIN32_ASAHI_BACKEND *b) {
  struct windows_bo *bo=(struct windows_bo *)b->Cache[0];
  memmove(&b->Cache[0],&b->Cache[1],(size_t)(--b->CacheCount)*sizeof(b->Cache[0]));
  b->CacheBytes-=bo->Base.size;
  bo->Cached=0;
  AgxWin32PerfNote("BO cache evict");
  (void)dispose(bo);
}
/* EXP1080: a full cache refused every later release, so DWM's cache stayed
 * full of stale entries while each released 2.9 MB texture was disposed and
 * created again (DestroyResource ~10 ms, CreateResource ~18 ms of MakeResident
 * and map paging waits). Admit the released BO and dispose the least recently
 * released entries instead, as Mesa's agx_bo_cache does. */
static int cache_put(struct windows_bo *bo) {
  AGX_WIN32_ASAHI_BACKEND *b=bo->Backend;
  if(bo->Imported || bo->Cached || bo->Virtual || !bo->Gpuva.Bound || b->Failed || b->Closing ||
     bo->Base.size>AGX_WIN32_BO_CACHE_BYTES) return 0;
  while(b->CacheCount && (b->CacheCount>=AGX_WIN32_BO_CACHE_LIMIT ||
        bo->Base.size>AGX_WIN32_BO_CACHE_BYTES-b->CacheBytes))
    cache_evict_oldest(b);
  bo->Cached=1;
  b->Cache[b->CacheCount++]=bo;
  b->CacheBytes+=bo->Base.size;
  return 1;
}
static struct windows_bo *cache_take(AGX_WIN32_ASAHI_BACKEND *b,size_t bytes,
                                     unsigned flags,unsigned cls) {
  for(APPLE_AGX_U32 i=b->CacheCount;i-->0;) {
    struct windows_bo *bo=(struct windows_bo *)b->Cache[i];
    if(bo->Base.size!=bytes || (unsigned)bo->Base.flags!=flags ||
       bo->Backing.Buffer.ClassId!=cls || cache_in_flight(b,bo)) continue;
    memmove(&b->Cache[i],&b->Cache[i+1],(size_t)(--b->CacheCount-i)*sizeof(b->Cache[0]));
    b->CacheBytes-=bo->Base.size;
    bo->Cached=0;
    return bo;
  }
  return NULL;
}
/* A BO whose dispose is refused stays registered with refcnt 0 and is retried
 * by AgxWin32AsahiCollect, exactly as an uncached release would be. */
static void cache_flush(AGX_WIN32_ASAHI_BACKEND *b) {
  while(b->CacheCount) {
    struct windows_bo *bo=(struct windows_bo *)b->Cache[--b->CacheCount];
    b->CacheBytes-=bo->Base.size;
    bo->Cached=0;
    (void)dispose(bo);
  }
}
#endif

struct agx_bo *agx_bo_create(struct agx_device *native,size_t bytes,unsigned align,
                            enum agx_bo_flags flags,const char *label) {
  const unsigned allowed=AGX_BO_LOW_VA|AGX_BO_EXEC|AGX_BO_WRITEBACK|AGX_BO_READONLY
#ifdef APPLE_AGX_GPUVA_WINSYS
      |AGX_WIN32_BO_SHADOW
#endif
      ;
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  struct windows_bo *bo;
  unsigned cls,access;
  if(!b || b->Native!=native || b->Failed || !bytes || bytes>SIZE_MAX-0x3fff ||
     ((unsigned)flags & ~allowed) || ((flags&AGX_BO_EXEC) && !(flags&AGX_BO_LOW_VA)))
    return NULL;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(!b->GpuvaReady || bytes>SIZE_MAX-0xffff) return NULL;
  bytes=(bytes+0xffff)&~(size_t)0xffff;
  if(align<0x10000) align=0x10000;
  if(align!=0x10000) return NULL;
#else
  bytes=(bytes+0x3fff)&~(size_t)0x3fff;
  if(align<0x4000) align=0x4000;
  if(align!=0x4000) return NULL; /* construction allocator currently guarantees 16 KiB */
#endif
  cls=(flags&AGX_BO_EXEC)?AgxWin32BufferClassShader:
      ((flags&AGX_BO_LOW_VA || b->EncoderAllocationIntent)?AgxWin32BufferClassEncoder:AgxWin32BufferClassGeneral);
  access=AppleAgxWin32BufferCpuWrite|AppleAgxWin32BufferGpuRead;
  if(cls==AgxWin32BufferClassGeneral) {
    access|=AppleAgxWin32BufferCpuRead;
    if(!(flags&AGX_BO_READONLY)) access|=AppleAgxWin32BufferGpuWrite;
  }
#ifdef APPLE_AGX_GPUVA_WINSYS
  if((bo=cache_take(b,bytes,(unsigned)flags,cls))) {
    AgxWin32PerfNote("BO cache hit");
    bo->Base.refcnt=1; bo->Base.label=label;
    return &bo->Base;
  }
  /* EXP1081 receipt-only: hit/miss/evict counts in measure-perf-reason. */
  AgxWin32PerfNote("BO cache miss");
#endif
  bo=calloc(1,sizeof(*bo));
  if(!bo) return NULL;
  bo->Backend=b;
  {
  AGX_WIN32_NATIVE_DEVICE_RESULT created=
      AgxWin32NativeDeviceCreateBo(&b->Buffers,cls,bytes,align,access,&bo->Backing);
  if(created!=AgxWin32NativeDeviceSuccess) {
#ifdef APPLE_AGX_GPUVA_WINSYS
    /* EXP1058 receipt-only (reject-batch kind 5): result code, size in 4 KiB
     * pages, class | alignment in 4 KiB pages << 8. */
    if(AgxWin32BatchRefusalHook)
      AgxWin32BatchRefusalHook(5u,(unsigned)created,(unsigned)(bytes>>12),
          (unsigned)cls|((unsigned)(align>>12)<<8));
#endif
    /* The existing wrapper may retain a buffer if allocation rollback failed.
     * Stop new native work rather than silently losing that Windows resource.
     * EXP1265-C: a clean refusal (nothing retained) only fails this creation;
     * the runtime reports E_OUTOFMEMORY and the device keeps working. */
    if(bo->Backing.Live) {
      AGX_WIN32_ASAHI_FAIL(b, 1u);
      b->UnpublishedBo=bo;
    } else free(bo);
    return NULL;
  }
  }
  bo->Base.dev=native; bo->Base.flags=flags; bo->Base.size=bytes;
  if(bo->Backing.Buffer.Transport.Token>UINT32_MAX) {
    if(AgxWin32NativeDeviceDestroyBo(&b->Buffers,&bo->Backing)!=AgxWin32NativeDeviceSuccess)
      b->UnpublishedBo=bo;
    else free(bo);
    AGX_WIN32_ASAHI_FAIL(b, 1u); return NULL;
  }
  bo->Base.handle=(uint32_t)bo->Backing.Buffer.Transport.Token;
  bo->Base.align=align; bo->Base.prime_fd=-1; bo->Base.refcnt=1; bo->Base.label=label;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(!AgxWin32GpuvaBind(&b->Gpuva,&bo->Gpuva,
      bo->Backing.Buffer.Transport.Token,bytes,(flags&AGX_BO_LOW_VA)!=0,
      ((flags&AGX_BO_READONLY)?0:AGX_GPUVA_MAP_WRITE)|
      ((flags&AGX_BO_EXEC)?AGX_GPUVA_MAP_EXECUTE:0))) {
    if(b->Gpuva.Terminal || AgxWin32NativeDeviceDestroyBo(&b->Buffers,&bo->Backing)!=
        AgxWin32NativeDeviceSuccess) b->UnpublishedBo=bo;
    else free(bo);
    AGX_WIN32_ASAHI_FAIL(b, 1u);return NULL;
  }
  bo->Coordinate.addr=bo->Gpuva.Va;
#else
  bo->Coordinate.addr=bo->Backing.ConstructionAddress; bo->Coordinate.size_B=bytes;
#endif
  bo->Coordinate.size_B=bytes;
  bo->Coordinate.flags=(flags&AGX_BO_LOW_VA)?AGX_VA_USC:0;
  bo->Base.va=&bo->Coordinate;
  if(!b->Ops.Associate(b->Owner,bo->Backing.Buffer.Transport.Token,&bo->Base,
                      bo->Backing.ConstructionSerial,release_map)) {
    if(AgxWin32NativeDeviceDestroyBo(&b->Buffers,&bo->Backing)!=AgxWin32NativeDeviceSuccess)
      b->UnpublishedBo=bo;
    else free(bo);
    AGX_WIN32_ASAHI_FAIL(b, 1u); return NULL;
  }
  ++b->LiveBos;
  (void)handle_map_put(b,bo); /* a miss falls back to the owner walk */
  return &bo->Base;
}

struct agx_bo *AgxWin32AsahiEncoderCreate(struct agx_device *native,
    size_t bytes,unsigned align,const char *label) {
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  struct agx_bo *bo;
  if(!b || b->Native!=native || b->Failed || b->EncoderAllocationIntent) return NULL;
  b->EncoderAllocationIntent=1;
#ifdef APPLE_AGX_GPUVA_WINSYS
  bo=agx_bo_create(native,bytes,align,AGX_WIN32_BO_SHADOW,label);
#else
  bo=agx_bo_create(native,bytes,align,0,label);
#endif
  b->EncoderAllocationIntent=0;
  return bo;
}

struct agx_bo *AgxWin32AsahiImportBo(
    AGX_WIN32_ASAHI_BACKEND *b,const AGX_WIN32_SCREEN_BUFFER *buffer,
    const char *label) {
  struct windows_bo *bo;
  if(!b || !b->Native || !buffer || !buffer->Transport.Token ||
     buffer->Transport.Token>UINT32_MAX || b->Failed)
    return NULL;
  bo=calloc(1,sizeof(*bo));
  if(!bo) return NULL;
  bo->Backend=b;
  if(AgxWin32NativeDeviceImportBo(&b->Buffers,buffer,&bo->Backing)!=
     AgxWin32NativeDeviceSuccess) {
    free(bo);return NULL;
  }
  bo->Base.dev=b->Native;bo->Base.size=buffer->Transport.Bytes;
  bo->Base.handle=(uint32_t)buffer->Transport.Token;
  bo->Base.align=(unsigned)buffer->Alignment;bo->Base.prime_fd=-1;
  bo->Base.refcnt=1;bo->Base.label=label;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(!b->GpuvaReady ||
     !AgxWin32GpuvaBind(&b->Gpuva,&bo->Gpuva,
      buffer->Transport.Token,buffer->Transport.Bytes,0,
      AGX_GPUVA_MAP_WRITE)) {
    if(b->Gpuva.Terminal || AgxWin32NativeDeviceDestroyBo(&b->Buffers,&bo->Backing)!=
        AgxWin32NativeDeviceSuccess) b->UnpublishedBo=bo;
    else free(bo);
    AGX_WIN32_ASAHI_FAIL(b, 1u);return NULL;
  }
  bo->Coordinate.addr=bo->Gpuva.Va;
#else
  bo->Coordinate.addr=bo->Backing.ConstructionAddress;
#endif
#ifdef APPLE_AGX_GPUVA_WINSYS
  bo->Imported=1;
#endif
  bo->Coordinate.size_B=buffer->Transport.Bytes;bo->Base.va=&bo->Coordinate;
  if(!b->Ops.Associate(b->Owner,buffer->Transport.Token,&bo->Base,
                      bo->Backing.ConstructionSerial,release_map)) {
    if(AgxWin32NativeDeviceDestroyBo(&b->Buffers,&bo->Backing)!=
       AgxWin32NativeDeviceSuccess) b->UnpublishedBo=bo;
    else free(bo);
    AGX_WIN32_ASAHI_FAIL(b, 1u);return NULL;
  }
  ++b->LiveBos;
  (void)handle_map_put(b,bo); /* a miss falls back to the owner walk */
  return &bo->Base;
}

int AgxWin32AsahiClass(AGX_WIN32_ASAHI_BACKEND *b,struct agx_bo *base,
    APPLE_AGX_U32 *classId) {
  struct windows_bo *bo=(struct windows_bo *)base;
  AGX_WIN32_RELOC_ALLOCATION identity;
  if(classId) *classId=0;
  if(!b || !base || !classId || !AgxWin32AsahiIdentity(b,base,&identity) ||
     bo->Backing.Buffer.ClassId<AgxWin32BufferClassGeneral ||
     bo->Backing.Buffer.ClassId>AgxWin32BufferClassEncoder) return 0;
  *classId=bo->Backing.Buffer.ClassId;
  return 1;
}
#ifdef APPLE_AGX_GPUVA_WINSYS
const AGX_WIN32_GPUVA_BO *AgxWin32AsahiGpuvaBo(
    AGX_WIN32_ASAHI_BACKEND *b,struct agx_bo *base) {
  struct windows_bo *bo=(struct windows_bo *)base;
  if(!b || !base || bo->Backend!=b || base->dev!=b->Native ||
     !bo->Gpuva.Bound || base->refcnt<=0 || base->va!=&bo->Coordinate ||
     base->va->addr!=(bo->Virtual ? bo->Reserved.Va : bo->Gpuva.Va))
    return NULL;
  return &bo->Gpuva;
}

/* C53 B1 (D3D12-C53-tiled-buffers-plan.md, "B1 decision"); C53 B4
 * (D3D12-C53-tiled-textures.md): unmapped tiles map the backend's tile
 * scratch BO, in runs of up to AGX_WIN32_TILE_SCRATCH_TILES tiles per map
 * (one map and one UMD alias per run, not per tile). */
#define AGX_WIN32_TILE 0x10000u
#define AGX_WIN32_TILE_SCRATCH_TILES 16u
static struct windows_bo *virtual_scratch(AGX_WIN32_ASAHI_BACKEND *b) {
  struct windows_bo *scratch=(struct windows_bo *)b->TileScratch;
  if(!scratch && b->Native) {
    b->TileScratch=agx_bo_create(b->Native,AGX_WIN32_TILE_SCRATCH_TILES*AGX_WIN32_TILE,0,0,
                                 "Tile scratch");
    scratch=(struct windows_bo *)b->TileScratch;
  }
  return scratch && scratch->Backend==b && scratch->Gpuva.Bound && !scratch->Virtual &&
         scratch->Gpuva.Bytes>=AGX_WIN32_TILE_SCRATCH_TILES*AGX_WIN32_TILE ? scratch : NULL;
}
static int map_scratch(AGX_WIN32_ASAHI_BACKEND *b,struct windows_bo *scratch,uint64_t va,
                       unsigned tiles) {
  while(tiles) {
    const unsigned n=tiles<AGX_WIN32_TILE_SCRATCH_TILES?tiles:AGX_WIN32_TILE_SCRATCH_TILES;
    if(!AgxWin32GpuvaMapRange(&b->Gpuva,va,scratch->Gpuva.Allocation,0,
                              (uint64_t)n*AGX_WIN32_TILE,AGX_GPUVA_MAP_WRITE)) return 0;
    va+=(uint64_t)n*AGX_WIN32_TILE;
    tiles-=n;
  }
  return 1;
}
/* The distinct BOs a virtual BO's tiles map (the tile scratch included),
 * each with its tile count and one reference: what a submission naming the
 * virtual BO holds (C53 B4: the expansion no longer walks every tile). */
static int backing_add(AGX_WIN32_ASAHI_BACKEND *b,struct windows_bo *bo,
                       struct windows_bo *backing,unsigned tiles) {
  void *grown;
  for(unsigned i=0;i<bo->BackingCount;++i)
    if(bo->Backings[i].Bo==backing) { bo->Backings[i].Tiles+=tiles; return 1; }
  grown=realloc(bo->Backings,(bo->BackingCount+1u)*sizeof(*bo->Backings));
  if(!grown) return 0;
  bo->Backings=grown;
  agx_bo_reference(&backing->Base);
  bo->Backings[bo->BackingCount].Bo=backing;
  bo->Backings[bo->BackingCount++].Tiles=tiles;
  ++b->VirtualBackingRefs;
  return 1;
}
static void backing_drop(AGX_WIN32_ASAHI_BACKEND *b,struct windows_bo *bo,
                         struct windows_bo *backing,unsigned tiles) {
  for(unsigned i=0;i<bo->BackingCount;++i) {
    if(bo->Backings[i].Bo!=backing) continue;
    if(bo->Backings[i].Tiles<=tiles) {
      bo->Backings[i]=bo->Backings[--bo->BackingCount];
      --b->VirtualBackingRefs;
      agx_bo_unreference(b->Native,&backing->Base);
    } else {
      bo->Backings[i].Tiles-=tiles;
    }
    return;
  }
}
/* Submissions naming the anchor still read the old tiles: retire them
 * first, as a CPU map does (EXP1082). */
static int virtual_retire(AGX_WIN32_ASAHI_BACKEND *b,struct windows_bo *bo) {
  uint64_t fence=AgxWin32GpuvaHoldingFence(&b->Gpuva,bo->Gpuva.Allocation);
  return !fence || AgxWin32GpuvaRetire(&b->Gpuva,fence);
}
static void virtual_release(struct windows_bo *bo) {
  AGX_WIN32_ASAHI_BACKEND *b=bo->Backend;
  while(bo->BackingCount) {
    struct windows_bo *backing=bo->Backings[--bo->BackingCount].Bo;
    --b->VirtualBackingRefs;
    agx_bo_unreference(b->Native,&backing->Base);
  }
  free(bo->Backings);
  bo->Backings=NULL;
  free(bo->Tiles);
  bo->Tiles=NULL;
  bo->TileCount=0;
  if(bo->Reserved.Bound) {
    /* the range is freed once no submission is in flight (EXP979) */
    if(!virtual_retire(b,bo) ||
       !AgxWin32GpuvaFreeRange(&b->Gpuva,bo->Reserved.Va,bo->Reserved.Bytes)) {
      if(b->PendingVaCount<sizeof(b->PendingVa)/sizeof(b->PendingVa[0])) {
        /* Unbind frees exactly Bytes: include the range's guard tile */
        b->PendingVa[b->PendingVaCount]=bo->Reserved;
        b->PendingVa[b->PendingVaCount++].Bytes+=AGX_WIN32_GPUVA_RANGE_GUARD;
      } else {
        AGX_WIN32_ASAHI_FAIL(b, 1u);
      }
    }
    memset(&bo->Reserved,0,sizeof(bo->Reserved));
  }
  bo->Virtual=0;
  bo->Coordinate.addr=bo->Gpuva.Va;
  bo->Coordinate.size_B=bo->Gpuva.Bytes;
  bo->Base.size=bo->Gpuva.Bytes;
}

struct agx_bo *AgxWin32AsahiCreateVirtualBo(struct agx_device *native,size_t bytes,const char *label) {
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  struct windows_bo *scratch,*bo;
  struct agx_bo *base;
  uint64_t va=0;
  if(!b || b->Native!=native || b->Failed || !b->GpuvaReady || !bytes ||
     bytes>SIZE_MAX-(AGX_WIN32_TILE-1)) return NULL;
  bytes=(bytes+AGX_WIN32_TILE-1)&~(size_t)(AGX_WIN32_TILE-1);
  if(bytes/AGX_WIN32_TILE>UINT32_MAX || !(scratch=virtual_scratch(b))) return NULL;
  if(!(base=agx_bo_create(native,AGX_WIN32_TILE,0,0,label))) return NULL;
  bo=(struct windows_bo *)base;
  bo->Tiles=calloc(bytes/AGX_WIN32_TILE,sizeof(*bo->Tiles));
  if(!bo->Tiles || !AgxWin32GpuvaReserveRange(&b->Gpuva,bytes,&va)) {
    free(bo->Tiles); bo->Tiles=NULL;
    agx_bo_unreference(native,base);
    return NULL;
  }
  bo->Virtual=1;
  bo->TileCount=(unsigned)(bytes/AGX_WIN32_TILE);
  bo->Reserved.Allocation=bo->Gpuva.Allocation;
  bo->Reserved.Va=va; bo->Reserved.Bytes=bytes; bo->Reserved.Bound=1;
  /* every tile starts on the tile scratch: reads see its contents, writes go
   * nowhere a real resource lives (plan: NULL tiles map scratch) */
  for(unsigned t=0;t<bo->TileCount;++t) bo->Tiles[t]=scratch;
  if(!backing_add(b,bo,scratch,bo->TileCount) || !map_scratch(b,scratch,va,bo->TileCount)) {
    agx_bo_unreference(native,base);
    return NULL;
  }
  bo->Coordinate.addr=va;
  bo->Coordinate.size_B=bytes;
  base->size=bytes;
  return base;
}

int AgxWin32AsahiMapVirtualTiles(struct agx_bo *base,unsigned first,unsigned count,
                                 struct agx_bo *backing_base,unsigned backing_tile) {
  struct windows_bo *bo=(struct windows_bo *)base;
  struct windows_bo *backing=(struct windows_bo *)backing_base;
  AGX_WIN32_ASAHI_BACKEND *b=bo?bo->Backend:NULL;
  struct windows_bo *scratch,*target;
  if(!b || !bo->Virtual || b->Failed || !count || first>bo->TileCount ||
     count>bo->TileCount-first || !(scratch=virtual_scratch(b))) return 0;
  if(backing && (backing->Backend!=b || backing->Virtual || !backing->Gpuva.Bound ||
                 backing_base->refcnt<=0 ||
                 (uint64_t)backing_tile+count>backing->Gpuva.Bytes/AGX_WIN32_TILE)) return 0;
  target=backing?backing:scratch;
  if(!virtual_retire(b,bo)) { AGX_WIN32_ASAHI_FAIL(b, 1u); return 0; }
  /* counted before the map so the reference never lapses on a remap onto
   * the same BO */
  if(!backing_add(b,bo,target,count)) return 0;
  const uint64_t va=bo->Reserved.Va+(uint64_t)first*AGX_WIN32_TILE;
  const int mapped=backing ?
      AgxWin32GpuvaMapRange(&b->Gpuva,va,backing->Gpuva.Allocation,
                            (uint64_t)backing_tile*AGX_WIN32_TILE,
                            (uint64_t)count*AGX_WIN32_TILE,AGX_GPUVA_MAP_WRITE) :
      map_scratch(b,scratch,va,count);
  if(!mapped) {
    /* a refused map leaves the old tiles; after a partly mapped scratch run
     * some tiles read scratch while their old references stay held (safe) */
    backing_drop(b,bo,target,count);
    return 0;
  }
  for(unsigned t=first;t<first+count;++t) {
    struct windows_bo *old=bo->Tiles[t];
    bo->Tiles[t]=target;
    backing_drop(b,bo,old,1u);
  }
  return 1;
}

unsigned AgxWin32AsahiVirtualBackingCount(struct agx_bo *base) {
  const struct windows_bo *bo=(const struct windows_bo *)base;
  return bo && bo->Virtual ? bo->BackingCount : 0u;
}
struct agx_bo *AgxWin32AsahiVirtualBacking(struct agx_bo *base,unsigned index) {
  const struct windows_bo *bo=(const struct windows_bo *)base;
  return bo && bo->Virtual && index<bo->BackingCount ? &bo->Backings[index].Bo->Base : NULL;
}
int AgxWin32AsahiIsTileScratch(AGX_WIN32_ASAHI_BACKEND *b,struct agx_bo *bo) {
  return b && bo && b->TileScratch==bo;
}
#endif
static void native_map(struct agx_device *native,struct agx_bo *base,void *fixed) {
  struct windows_bo *bo=(struct windows_bo *)base;
  void *address=NULL;
  AGX_WIN32_ASAHI_BACKEND *b=native->windows_private;
  if(!b || b!=bo->Backend || fixed || b->Failed || base->refcnt<=0) return;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(bo->Virtual) return; /* C53 B1: no CPU map of a reserved range yet */
#endif
#ifdef APPLE_AGX_GPUVA_WINSYS
  /* EXP1082: a submission now returns once queued, so the one in flight may
   * still hold this BO's slot, which refuses a CPU map (DWM's backend failed
   * on such a first map every ~2.3 s), and its completion must run before
   * CPU access. Retire it first, as the next submission would. */
  {
    /* EXP1093: retire through the newest in-flight submission naming it. */
    uint64_t fence=AgxWin32GpuvaHoldingFence(&b->Gpuva,bo->Gpuva.Allocation);
    if(fence && !AgxWin32GpuvaRetire(&b->Gpuva,fence)) {
      AGX_WIN32_ASAHI_FAIL(b, 1u); return;
    }
  }
#endif
  if(AgxWin32NativeBoMap(b->Buffers.Screen,&bo->Backing,AppleAgxWin32BufferCpuWrite,&address)
      !=AgxWin32NativeBoSuccess) { AGX_WIN32_ASAHI_FAIL(b, 1u); return; }
#ifdef APPLE_AGX_GPUVA_WINSYS
  /* EXP1207: the shadow outlives the map with the BO (BO cache reuse). */
  if((unsigned)base->flags&AGX_WIN32_BO_SHADOW) {
    if(!bo->Shadow && !(bo->Shadow=shadow_alloc(base->size))) {
      AGX_WIN32_ASAHI_FAIL(b, 1u); return;
    }
    base->_map=bo->Shadow;
    return;
  }
#endif
  base->_map=address;
}

void agx_bo_reference(struct agx_bo *base) {
  if(!base) return;
  struct windows_bo *bo=(struct windows_bo *)base;
  if(base->refcnt<=0 || base->refcnt==INT32_MAX) { AGX_WIN32_ASAHI_FAIL(bo->Backend, 1u); return; }
  ++base->refcnt;
}
void agx_bo_unreference(struct agx_device *native,struct agx_bo *base) {
  struct windows_bo *bo=(struct windows_bo *)base;
  if(!base) return;
  if(base->dev!=native || base->refcnt<=0) { AGX_WIN32_ASAHI_FAIL(bo->Backend, 1u); return; }
  if(--base->refcnt==0) {
#ifdef APPLE_AGX_GPUVA_WINSYS
    if(cache_put(bo)) return;
#endif
    (void)dispose(bo);
  }
}
int AgxWin32AsahiCollect(AGX_WIN32_ASAHI_BACKEND *b) {
  APPLE_AGX_U32 cursor=0;
  const struct agx_bo *key;
  if(!b || !b->Native) return 0;
#ifdef APPLE_AGX_GPUVA_WINSYS
  for(APPLE_AGX_U32 i=0;i<b->PendingVaCount;) {
    if(AgxWin32GpuvaUnbind(&b->Gpuva,&b->PendingVa[i]))
      b->PendingVa[i]=b->PendingVa[--b->PendingVaCount];
    else ++i;
  }
#endif
  if(b->UnpublishedBo) {
    struct windows_bo *bo=b->UnpublishedBo;
#ifdef APPLE_AGX_GPUVA_WINSYS
    if(b->Gpuva.Terminal) return 0;
    if(bo->Gpuva.Bound && !AgxWin32GpuvaUnbind(&b->Gpuva,&bo->Gpuva)) return 0;
#endif
    int released=bo->Backing.ConstructionSerial?
      AgxWin32NativeDeviceDestroyBo(&b->Buffers,&bo->Backing)==AgxWin32NativeDeviceSuccess:
      AgxWin32NativeBoDestroy(b->Buffers.Screen,&bo->Backing)==AgxWin32NativeBoSuccess;
    if(released) { free(bo); b->UnpublishedBo=NULL; }
  }
  while((key=b->Ops.NextBo(b->Owner,&cursor))) {
    struct windows_bo *bo=(struct windows_bo *)key;
    if(bo->Backend==b && bo->Base.refcnt==0
#ifdef APPLE_AGX_GPUVA_WINSYS
       && !bo->Cached
#endif
       ) (void)dispose(bo);
  }
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(b->PendingVaCount) return 0;
#endif
  return b->LiveBos==0 && b->UnpublishedBo==NULL;
}
int AgxWin32AsahiDetach(AGX_WIN32_ASAHI_BACKEND *b) {
  if(!b || !b->Native || b->ActiveCapture || b->ActiveEmission) return 0;
#ifdef APPLE_AGX_GPUVA_WINSYS
  /* before the cache flush: a released tile scratch may enter the cache */
  if(b->TileScratch) { agx_bo_unreference(b->Native,b->TileScratch); b->TileScratch=NULL; }
  cache_flush(b);
#endif
  if(!AgxWin32AsahiCollect(b)) return 0;
  b->Native->windows_private=NULL;
  b->Native->ops.bo_mmap=NULL;
  b->Ops.Leave(b->Owner);
  b->Native=NULL;
  free(b->HandleMap); b->HandleMap=NULL; b->HandleMapCap=b->HandleMapCount=0;
  /* Failed detach returns above with the original transaction owner intact.
   * A completed detach may reuse this backend for another native screen. */
  b->BatchOps=NULL;
  b->BatchOwner=NULL;
  return 1;
}
int AgxWin32AsahiIdentity(AGX_WIN32_ASAHI_BACKEND *b,struct agx_bo *base,
                         AGX_WIN32_RELOC_ALLOCATION *out) {
  AGX_WIN32_RELOC_ALLOCATION current;
  struct windows_bo *bo=(struct windows_bo *)base;
  if(!b || !base || !out || !b->Ops.Identity(b->Owner,base,0,&current)) return 0;
  if(base->dev!=b->Native || bo->Backend!=b || base->refcnt<=0)
    return 0;
  return b->Ops.Identity(b->Owner,base,bo->Backing.ConstructionSerial,out);
}

static int resolve_bo(AGX_WIN32_ASAHI_BACKEND *b,struct windows_bo *bo,
    APPLE_AGX_U64 offset,APPLE_AGX_U64 bytes,APPLE_AGX_U64 *address) {
#ifdef APPLE_AGX_GPUVA_WINSYS
  (void)b;
  if(!bo->Gpuva.Bound || !address || !bytes ||
     offset>bo->Backing.Bytes || bytes>bo->Backing.Bytes-offset ||
     bo->Gpuva.Va>UINT64_MAX-offset) return 0;
  *address=bo->Gpuva.Va+offset;
  return 1;
#else
  return AgxWin32NativeDeviceResolveBo(&b->Buffers,&bo->Backing,
      offset,bytes,address)==AgxWin32NativeDeviceSuccess;
#endif
}

int AgxWin32AsahiFindAddress(AGX_WIN32_ASAHI_BACKEND *b,APPLE_AGX_U64 owner,
    APPLE_AGX_U32 generation,APPLE_AGX_U64 address,APPLE_AGX_U64 bytes,
    struct agx_bo **out,APPLE_AGX_U64 *offset) {
  APPLE_AGX_U32 cursor=0,seen=0;
  APPLE_AGX_U64 found_offset=0;
  struct agx_bo *found=NULL;
  const void *key;
  if(out) *out=NULL;
  if(offset) *offset=0;
  if(!b || !b->Native || b->Failed || !out || !offset || !owner || !generation ||
     generation!=b->Buffers.Generation || !bytes || !address || bytes>~address ||
     !b->Buffers.Screen || !b->Buffers.Screen->Active ||
     b->Buffers.Screen->Generation!=generation) return 0;
  for(;;) {
    APPLE_AGX_U32 previous=cursor;
    key=b->Ops.NextBo(b->Owner,&cursor);
    if(!key) break;
    if(cursor<=previous || ++seen>b->LiveBos) return 0;
    AGX_WIN32_RELOC_ALLOCATION identity;
    /* The owner validates membership before any native pointer dereference.
     * A retired zero-ref BO awaiting collection is not an eligible source. */
    if(!AgxWin32AsahiIdentity(b,(struct agx_bo *)key,&identity)) continue;
    if(identity.Owner!=owner || identity.Generation!=generation) continue;
    struct windows_bo *bo=(struct windows_bo *)key;
    APPLE_AGX_U64 base=0,resolved=0;
    if(!resolve_bo(b,bo,0,bo->Backing.Bytes,&base) || !bo->Base.va ||
       bo->Base.va->addr!=base || bo->Base.size!=identity.Bytes ||
       bo->Backing.Bytes!=identity.Bytes) return 0;
    if(address<base || address-base>=identity.Bytes || bytes>identity.Bytes-(address-base))
      continue;
    if(found || !resolve_bo(b,bo,address-base,bytes,&resolved) ||
       resolved!=address) return 0;
    found=&bo->Base; found_offset=address-base;
  }
  if(!found) return 0;
  *out=found; *offset=found_offset;
  return 1;
}

int AgxWin32AsahiFindCpuAddress(AGX_WIN32_ASAHI_BACKEND *b,
    APPLE_AGX_U64 owner,APPLE_AGX_U32 generation,const void *cpu,
    APPLE_AGX_U64 bytes,struct agx_bo **out,APPLE_AGX_U64 *address,
    APPLE_AGX_U64 *offset) {
  APPLE_AGX_U32 cursor=0,seen=0;
  struct agx_bo *found=NULL;
  APPLE_AGX_U64 found_offset=0,found_address=0;
  const void *key;
  if(out) *out=NULL;
  if(address) *address=0;
  if(offset) *offset=0;
  if(!b || !b->Native || b->Failed || !cpu || !bytes || !out || !address ||
     !offset || !owner || !generation || generation!=b->Buffers.Generation ||
     !b->Buffers.Screen || !b->Buffers.Screen->Active ||
     b->Buffers.Screen->Generation!=generation) return 0;
  for(;;) {
    APPLE_AGX_U32 previous=cursor;
    key=b->Ops.NextBo(b->Owner,&cursor);
    if(!key) break;
    if(cursor<=previous || ++seen>b->LiveBos) return 0;
    AGX_WIN32_RELOC_ALLOCATION identity;
    if(!AgxWin32AsahiIdentity(b,(struct agx_bo *)key,&identity) ||
       identity.Owner!=owner || identity.Generation!=generation) continue;
    struct windows_bo *bo=(struct windows_bo *)key;
    APPLE_AGX_U64 base=0;
    if(!bo->Base._map ||
       !resolve_bo(b,bo,0,bo->Backing.Bytes,&base) ||
       bo->Base.va==NULL || bo->Base.va->addr!=base ||
       bo->Base.size!=identity.Bytes || bo->Backing.Bytes!=identity.Bytes ||
       (uintptr_t)cpu<(uintptr_t)bo->Base._map ||
       (uintptr_t)cpu-(uintptr_t)bo->Base._map>=identity.Bytes ||
       bytes>identity.Bytes-((uintptr_t)cpu-(uintptr_t)bo->Base._map)) continue;
    if(found) return 0;
    found=&bo->Base;
    found_offset=(uintptr_t)cpu-(uintptr_t)bo->Base._map;
    found_address=base+found_offset;
  }
  if(!found || !AgxWin32AsahiFindAddress(b,owner,generation,found_address,
      bytes,out,offset) || *out!=found || *offset!=found_offset) {
    if(out) *out=NULL; if(address) *address=0; if(offset) *offset=0;
    return 0;
  }
  *address=found_address;
  return 1;
}

/* Native handle lookup uses the same authoritative Windows BO association
 * already used for capture; Linux sparse-array/GEM slots do not exist here. */
struct agx_bo *AgxWin32AsahiLookupBo(struct agx_device *native,uint32_t handle) {
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  APPLE_AGX_U32 cursor=0;
  const void *key;
  if(!b || !handle) return NULL;
  {
    struct windows_bo *hit=handle_map_get(b,handle);
    AGX_WIN32_RELOC_ALLOCATION id;
    if(hit && hit->Base.dev==native && hit->Base.refcnt>0 &&
       AgxWin32AsahiIdentity(b,&hit->Base,&id)) return &hit->Base;
  }
  while((key=b->Ops.NextBo(b->Owner,&cursor))!=NULL) {
    struct agx_bo *bo=(struct agx_bo *)key;
    AGX_WIN32_RELOC_ALLOCATION id;
    if(bo->dev==native && bo->handle==handle && bo->refcnt>0 &&
        AgxWin32AsahiIdentity(b,bo,&id)) return bo;
  }
  AGX_WIN32_ASAHI_FAIL(b, 1u);return NULL;
}
