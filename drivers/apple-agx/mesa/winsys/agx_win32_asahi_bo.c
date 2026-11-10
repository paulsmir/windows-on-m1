#include "agx_device.h"
#include "agx_win32_asahi_bo.h"
#ifdef APPLE_AGX_GPUVA_WINSYS
#include "agx_win32_sparse.h"
#endif

unsigned (*AgxWin32WaitBeginHook)(unsigned site);
void (*AgxWin32WaitEndHook)(unsigned token);

void (*AgxWin32BackendFailHook)(unsigned site);
/* Diagnostic identity: event 1 new, 2 cache reuse, 3 release. */
void (*AgxWin32BoIdentityHook)(unsigned event, unsigned token,
    unsigned long long va, unsigned long long bytes, unsigned flags, const char *label);
static void bo_identity(unsigned event, const struct agx_bo *bo) {
  if(AgxWin32BoIdentityHook && bo && bo->va)
    AgxWin32BoIdentityHook(event,bo->handle,bo->va->addr,bo->size,(unsigned)bo->flags,bo->label);
}
void (*AgxWin32GpuOnlyMapHook)(unsigned token, unsigned flags);
#ifdef APPLE_AGX_GPUVA_WINSYS
extern void (*AgxWin32BatchRefusalHook)(unsigned, unsigned, unsigned, unsigned);
#include "util/os_time.h"
/* C85 receipt-only (EXP1916 GravityMark D3D12: 8.3 s of a 24 s run went to
 * creating 87 heaps, about 4 ms per MiB, with no receipt inside): a BO
 * creation of at least 10 ms reports its MiB, class and the microseconds of
 * the Windows allocation (NativeDeviceCreateBo), the GPU VA bind and the
 * owner association. The owner sets the hook (defined in every build). */
#endif
void (*AgxWin32BoTimeHook)(unsigned mib, unsigned cls, unsigned create_us, unsigned bind_us, unsigned associate_us);
/* BO census (receipt-only; agx_win32_asahi_bo.h). Defined in every build. */
unsigned (*AgxWin32BoCensusHook)(unsigned event, uint32_t *values, unsigned count);
#include <stdarg.h>
#include <stddef.h>
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
  APPLE_AGX_U64 CacheStorageBytes;
  /* EXP1207: the cached CPU map of an AGX_WIN32_BO_SHADOW BO (Base._map);
   * Backing.CpuAddress stays the allocation's write-combined view. */
  void *Shadow;
  /* C53 B1: a reserved (virtual) buffer. Coordinate is the reserved range;
   * the BO's own allocation is a 64 KiB anchor for its handle, residency
   * and in-flight tracking. Pages[p] is the BO 16 KiB page p maps (the tile
   * scratch while unmapped; C53 T2D: pages, so that a reserved texture binds
   * its hardware tiles one page each); C53 B4: Backings lists each distinct
   * one with its page count and holds the references. */
  int Virtual;
  AGX_WIN32_GPUVA_BO Reserved;
  struct windows_bo **Pages;
  unsigned PageCount;
  struct { struct windows_bo *Bo; unsigned Pages; } *Backings;
  unsigned BackingCount;
  /* C53 T2D: a reserved texture's zeroed Sparse Block table (one
   * reference), which its texture descriptors point at */
  struct windows_bo *SparseTable;
  /* C53 Tier 2: a reserved buffer's read-only shadow range (Honeykrisp's
   * sparse_ro_offset half): its pages map the same backing read-only, and
   * the tile zero BO where the main range maps the tile scratch */
  AGX_WIN32_GPUVA_BO ReadShadow;
  /* BO census (receipt-only): 1 from a birth while AgxWin32BoCensusHook was
   * set until the first submitted batch names the BO; Serial is the
   * measure-bo-born serial (BOs of at least AGX_WIN32_CENSUS_LINE_BYTES). */
  unsigned CensusPending;
  uint32_t CensusSerial;
#endif
};
static void native_map(struct agx_device *,struct agx_bo *,void *);

#ifdef APPLE_AGX_GPUVA_WINSYS
/* BO census (receipt-only): a birth (origin 1 new allocation, 2 cache reuse)
 * while the hook is set arms the BO's first-reference stamp. Runs under the
 * caller's native-device serialization, as agx_bo_create does. Value order:
 * the owner's measure-bo-born comment (agx_d3d12_windows.cpp). */
static void census_born(AGX_WIN32_ASAHI_BACKEND *b,struct windows_bo *bo,
                        unsigned cls,unsigned origin) {
  unsigned (*hook)(unsigned,uint32_t *,unsigned)=AgxWin32BoCensusHook;
  if(!hook || bo->Virtual) return;
  bo->CensusPending=1u;
  bo->CensusSerial=0u;
  if(bo->Base.size<AGX_WIN32_CENSUS_LINE_BYTES) {
    ++b->CensusSmall;
    b->CensusSmallBytes+=bo->Base.size;
    return;
  }
  ++b->CensusLarge;
  {
    const uint64_t va=bo->Coordinate.addr;
    uint32_t v[8]={0u,bo->Base.handle,(uint32_t)(bo->Base.size>>16),(uint32_t)(va>>16),
                   (uint32_t)(va>>32),cls,origin,(uint32_t)bo->Base.flags};
    bo->CensusSerial=hook(1u,v,8u);
  }
}
void AgxWin32AsahiCensusRefs(AGX_WIN32_ASAHI_BACKEND *b,
    const AGX_WIN32_GPUVA_BO *const *refs,unsigned count,unsigned kind) {
  unsigned (*hook)(unsigned,uint32_t *,unsigned)=AgxWin32BoCensusHook;
  if(!b || !refs || !hook) return;
  for(unsigned i=0;i<count;++i) {
    /* add_bo stores AgxWin32AsahiGpuvaBo views: &windows_bo.Gpuva */
    struct windows_bo *bo=(struct windows_bo *)(uintptr_t)
        ((const char *)refs[i]-offsetof(struct windows_bo,Gpuva));
    if(!bo->CensusPending) continue;
    bo->CensusPending=0u;
    if(bo->Base.size<AGX_WIN32_CENSUS_LINE_BYTES) {
      ++b->CensusSmallRef;
      b->CensusSmallRefBytes+=bo->Base.size;
      continue;
    }
    ++b->CensusLargeRef;
    {
      uint32_t v[4]={bo->CensusSerial,bo->Base.handle,(uint32_t)(bo->Base.size>>16),kind};
      (void)hook(2u,v,4u);
    }
  }
}
/* One aggregate line per completed backend teardown. */
static void census_teardown(AGX_WIN32_ASAHI_BACKEND *b) {
  unsigned (*hook)(unsigned,uint32_t *,unsigned)=AgxWin32BoCensusHook;
  if(!hook || !(b->CensusSmall|b->CensusLarge)) return;
  {
    uint32_t v[6]={b->CensusSmall,(uint32_t)(b->CensusSmallBytes>>16),b->CensusSmallRef,
                   (uint32_t)(b->CensusSmallRefBytes>>16),b->CensusLarge,b->CensusLargeRef};
    (void)hook(3u,v,6u);
  }
  b->CensusSmall=b->CensusSmallRef=b->CensusLarge=b->CensusLargeRef=0u;
  b->CensusSmallBytes=b->CensusSmallRefBytes=0u;
}
#endif

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
  /* A proven T13 map is already cached and write-watched. The UMD publishes
   * its dirty bytes to canonical storage at submission. No second copy. */
  if(!bo->Shadow) return base->_map==bo->Backing.CpuAddress;
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
/* RC71 (EXP1990): free each deferred VA whose fence has retired. */
static void pending_va_free_retired(AGX_WIN32_ASAHI_BACKEND *b) {
  for(APPLE_AGX_U32 i=0;i<b->PendingVaCount;) {
    if(AgxWin32GpuvaUnbindAfter(&b->Gpuva,&b->PendingVa[i],b->PendingVaFence[i])) {
      --b->PendingVaCount;
      b->PendingVa[i]=b->PendingVa[b->PendingVaCount];
      b->PendingVaFence[i]=b->PendingVaFence[b->PendingVaCount];
    } else ++i;
  }
}
/* Defer a VA until the newest submission now in flight retires. */
static int pending_va_push(AGX_WIN32_ASAHI_BACKEND *b,const AGX_WIN32_GPUVA_BO *va) {
  pending_va_free_retired(b);
  if(b->PendingVaCount>=AGX_WIN32_PENDING_VA_LIMIT) return 0;
  b->PendingVa[b->PendingVaCount]=*va;
  b->PendingVaFence[b->PendingVaCount++]=AgxWin32GpuvaNewestFence(&b->Gpuva);
  return 1;
}
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
  pending_va_free_retired(b);
  if(b->PendingVaCount>=AGX_WIN32_PENDING_VA_LIMIT) {
    if(!b->Ops.Associate(b->Owner,bo->Backing.Buffer.Transport.Token,&bo->Base,
                        bo->Backing.ConstructionSerial,release_map)) AGX_WIN32_ASAHI_FAIL(b, 1u);
    b->RefusedRelease=1;
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
    (void)pending_va_push(b,&bo->Gpuva); /* room was checked above */
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
/* Count every retained copy, not just canonical and winsys storage. T13 may
 * own private staging plus a GPU-write merge baseline. The native owner
 * serializes this query with mapping/submission; cached BOs have no clients
 * that can create new CPU storage. Save the charge at insertion so removal
 * never depends on a later owner query or teardown state. */
static APPLE_AGX_U64 cache_storage_bytes(const struct windows_bo *bo) {
  const AGX_WIN32_ASAHI_BACKEND *b=bo->Backend;
  AGX_WIN32_ASAHI_CPU_STORAGE storage={0};
  if(bo->Base.size>AGX_WIN32_BO_CACHE_BYTES) return AGX_WIN32_BO_CACHE_BYTES+1;
  APPLE_AGX_U64 bytes=(APPLE_AGX_U64)bo->Base.size*(bo->Shadow?2u:1u);
  if(bytes>AGX_WIN32_BO_CACHE_BYTES) return AGX_WIN32_BO_CACHE_BYTES+1;
  if(b->Ops.CpuStorage &&
     (!b->Ops.CpuStorage(b->Owner,&bo->Base,bo->Backing.ConstructionSerial,&storage) ||
      storage.Bytes>AGX_WIN32_BO_CACHE_BYTES-bytes)) return AGX_WIN32_BO_CACHE_BYTES+1;
  return bytes+storage.Bytes;
}
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
  b->CacheBytes-=bo->CacheStorageBytes;
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
  APPLE_AGX_U64 storage=cache_storage_bytes(bo);
  if(bo->Imported || bo->Cached || bo->Virtual || !bo->Gpuva.Bound || b->Failed || b->Closing ||
     storage>AGX_WIN32_BO_CACHE_BYTES) return 0;
  while(b->CacheCount && (b->CacheCount>=AGX_WIN32_BO_CACHE_LIMIT ||
        storage>AGX_WIN32_BO_CACHE_BYTES-b->CacheBytes))
    cache_evict_oldest(b);
  bo->Cached=1;
  bo->CacheStorageBytes=storage;
  b->Cache[b->CacheCount++]=bo;
  b->CacheBytes+=storage;
  return 1;
}
static struct windows_bo *cache_take(AGX_WIN32_ASAHI_BACKEND *b,size_t bytes,
                                     unsigned flags,unsigned cls) {
  for(APPLE_AGX_U32 i=b->CacheCount;i-->0;) {
    struct windows_bo *bo=(struct windows_bo *)b->Cache[i];
    if(bo->Base.size!=bytes || (unsigned)bo->Base.flags!=flags ||
       bo->Backing.Buffer.ClassId!=cls || cache_in_flight(b,bo)) continue;
    memmove(&b->Cache[i],&b->Cache[i+1],(size_t)(--b->CacheCount-i)*sizeof(b->Cache[0]));
    b->CacheBytes-=bo->CacheStorageBytes;
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
    b->CacheBytes-=bo->CacheStorageBytes;
    bo->Cached=0;
    (void)dispose(bo);
  }
}

/* Allocation pressure must not retain dispensable cache storage. Unlike
 * shutdown, leave BOs named by any in-flight submission in the cache. A
 * refused dispose remains registered for Collect; only actual destruction
 * counts as progress that can justify retrying the allocation. */
static unsigned cache_reclaim(AGX_WIN32_ASAHI_BACKEND *b) {
  unsigned released=0;
  for(APPLE_AGX_U32 i=0;i<b->CacheCount && !b->Failed && !b->Closing;) {
    struct windows_bo *bo=(struct windows_bo *)b->Cache[i];
    if(cache_in_flight(b,bo)) { ++i; continue; }
    memmove(&b->Cache[i],&b->Cache[i+1],
        (size_t)(--b->CacheCount-i)*sizeof(b->Cache[0]));
    b->CacheBytes-=bo->CacheStorageBytes;
    bo->Cached=0;
    if(dispose(bo)) {
      ++released;
      AgxWin32PerfNote("BO cache pressure release");
    }
  }
  return released;
}
#endif

struct agx_bo *agx_bo_create(struct agx_device *native,size_t bytes,unsigned align,
                            enum agx_bo_flags flags,const char *label) {
  const unsigned allowed=AGX_BO_LOW_VA|AGX_BO_EXEC|AGX_BO_WRITEBACK|AGX_BO_READONLY
#ifdef APPLE_AGX_GPUVA_WINSYS
      |AGX_WIN32_BO_SHADOW|AGX_WIN32_BO_GPU_ONLY
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
      ((flags&AGX_WIN32_BO_GPU_ONLY)?AgxWin32BufferClassGeneral:
      ((flags&AGX_BO_LOW_VA || b->EncoderAllocationIntent)?AgxWin32BufferClassEncoder:AgxWin32BufferClassGeneral));
  access=AppleAgxWin32BufferCpuWrite|AppleAgxWin32BufferGpuRead;
  if(cls==AgxWin32BufferClassGeneral) {
    access|=AppleAgxWin32BufferCpuRead;
    if (flags & AGX_WIN32_BO_GPU_ONLY)
      access = AppleAgxWin32BufferGpuOnly | AppleAgxWin32BufferGpuRead;
    if(!(flags&AGX_BO_READONLY)) access|=AppleAgxWin32BufferGpuWrite;
  }
#ifdef APPLE_AGX_GPUVA_WINSYS
  if ((flags & AGX_WIN32_BO_GPU_ONLY) &&
      ((flags & (AGX_BO_EXEC | AGX_WIN32_BO_SHADOW)) ||
       b->EncoderAllocationIntent || b->NextVirtual)) return NULL;
  /* RC71: VAs released while their submissions ran are free now. */
  AgxWin32AsahiDrainPendingVa(b);
  /* GPU-produced USC is writable General storage, reserved in the USC VA
   * window by LOW_VA below. Encoder/Shader class permissions stay unchanged. */
  /* C53 T2D: the image BO of a reserved texture under construction */
  if(b->NextVirtual) {
    b->NextVirtual=0;
    return AgxWin32AsahiCreateVirtualBo(native,bytes,label);
  }
  if((bo=cache_take(b,bytes,(unsigned)flags,cls))) {
    AgxWin32PerfNote("BO cache hit");
    bo->Base.refcnt=1; bo->Base.label=label;
    bo_identity(2u,&bo->Base);
    if(AgxWin32BoCensusHook) census_born(b,bo,cls,2u);
    return &bo->Base;
  }
  /* EXP1081 receipt-only: hit/miss/evict counts in measure-perf-reason. */
  AgxWin32PerfNote("BO cache miss");
#endif
  bo=calloc(1,sizeof(*bo));
  if(!bo) return NULL;
  bo->Backend=b;
#ifdef APPLE_AGX_GPUVA_WINSYS
  const int64_t timed_start=os_time_get_nano();
  int64_t timed_created=0,timed_bound=0;
#endif
  {
  AGX_WIN32_NATIVE_DEVICE_RESULT created=
      AgxWin32NativeDeviceCreateBo(&b->Buffers,cls,bytes,align,access,&bo->Backing);
#ifdef APPLE_AGX_GPUVA_WINSYS
  /* Asahi also releases its BO cache on allocation failure. Retry once,
   * only after a clean callback refusal and real reclamation. A retained
   * rollback resource or backend fault must keep the fail-closed path. */
  if(created==AgxWin32NativeDeviceCallback && !bo->Backing.Live &&
     !b->Failed && !b->Closing && cache_reclaim(b) && !b->Failed && !b->Closing) {
    AgxWin32PerfNote("BO cache pressure retry");
    created=AgxWin32NativeDeviceCreateBo(&b->Buffers,cls,bytes,align,access,&bo->Backing);
  }
#endif
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
#ifdef APPLE_AGX_GPUVA_WINSYS
  timed_created=os_time_get_nano();
#endif
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
  timed_bound=os_time_get_nano();
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
  bo_identity(1u,&bo->Base);
  (void)handle_map_put(b,bo); /* a miss falls back to the owner walk */
#ifdef APPLE_AGX_GPUVA_WINSYS
  {
    const int64_t timed_end=os_time_get_nano();
    if(AgxWin32BoTimeHook && timed_end-timed_start>=10000000)
      AgxWin32BoTimeHook((unsigned)(bytes>>20),cls,(unsigned)((timed_created-timed_start)/1000),
          (unsigned)((timed_bound-timed_created)/1000),(unsigned)((timed_end-timed_bound)/1000));
  }
  /* after the C85 timing: its line I/O stays outside measure-bo-create */
  if(AgxWin32BoCensusHook) census_born(b,bo,cls,1u);
#endif
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
  bo_identity(1u,&bo->Base);
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
/* Diagnostic alias bounds. Actual mappings remain owned by the paging path. */
int AgxWin32AsahiVirtualContains(struct agx_bo *base,uint64_t va,uint64_t bytes) {
  struct windows_bo *bo=(struct windows_bo *)base;
  if(!bo || !bo->Virtual || !bytes) return 0;
  const AGX_WIN32_GPUVA_BO *ranges[2]={&bo->Reserved,&bo->ReadShadow};
  for(unsigned i=0;i<2;++i) {
    const AGX_WIN32_GPUVA_BO *r=ranges[i];
    if(r->Bound && va>=r->Va && va-r->Va<r->Bytes && bytes<=r->Bytes-(va-r->Va)) return 1;
  }
  return 0;
}
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
#define AGX_WIN32_PAGE 0x4000u
#define AGX_WIN32_TILE_SCRATCH_TILES 16u
#define AGX_WIN32_SCRATCH_PAGES (AGX_WIN32_TILE_SCRATCH_TILES * (AGX_WIN32_TILE / AGX_WIN32_PAGE))
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
/* C53 Tier 2: the zeroed BO a shadow's unmapped pages read (created on
 * first use, the tile scratch's size) */
static struct windows_bo *virtual_zero(AGX_WIN32_ASAHI_BACKEND *b) {
  struct windows_bo *zero=(struct windows_bo *)b->TileZero;
  if(!zero && b->Native) {
    struct agx_bo *base=agx_bo_create(b->Native,AGX_WIN32_TILE_SCRATCH_TILES*AGX_WIN32_TILE,0,0,"Tile zero");
    void *map=base?agx_bo_map(base):NULL;
    if(base && !map) { agx_bo_unreference(b->Native,base); base=NULL; }
    if(map) memset(map,0,AGX_WIN32_TILE_SCRATCH_TILES*AGX_WIN32_TILE);
    b->TileZero=base;
    zero=(struct windows_bo *)base;
  }
  return zero && zero->Backend==b && zero->Gpuva.Bound && !zero->Virtual &&
         zero->Gpuva.Bytes>=AGX_WIN32_TILE_SCRATCH_TILES*AGX_WIN32_TILE ? zero : NULL;
}
static int map_scratch_as(AGX_WIN32_ASAHI_BACKEND *b,struct windows_bo *scratch,uint64_t va,
                          unsigned pages,unsigned protection) {
  while(pages) {
    const unsigned n=pages<AGX_WIN32_SCRATCH_PAGES?pages:AGX_WIN32_SCRATCH_PAGES;
    if(!AgxWin32GpuvaMapRange(&b->Gpuva,va,scratch->Gpuva.Allocation,0,
                              (uint64_t)n*AGX_WIN32_PAGE,protection)) return 0;
    va+=(uint64_t)n*AGX_WIN32_PAGE;
    pages-=n;
  }
  return 1;
}
static int map_scratch(AGX_WIN32_ASAHI_BACKEND *b,struct windows_bo *scratch,uint64_t va,
                       unsigned pages) {
  return map_scratch_as(b,scratch,va,pages,AGX_GPUVA_MAP_WRITE);
}
/* The distinct BOs a virtual BO's tiles map (the tile scratch included),
 * each with its tile count and one reference: what a submission naming the
 * virtual BO holds (C53 B4: the expansion no longer walks every tile). */
static int backing_add(AGX_WIN32_ASAHI_BACKEND *b,struct windows_bo *bo,
                       struct windows_bo *backing,unsigned pages) {
  void *grown;
  for(unsigned i=0;i<bo->BackingCount;++i)
    if(bo->Backings[i].Bo==backing) { bo->Backings[i].Pages+=pages; return 1; }
  grown=realloc(bo->Backings,(bo->BackingCount+1u)*sizeof(*bo->Backings));
  if(!grown) return 0;
  bo->Backings=grown;
  agx_bo_reference(&backing->Base);
  bo->Backings[bo->BackingCount].Bo=backing;
  bo->Backings[bo->BackingCount++].Pages=pages;
  ++b->VirtualBackingRefs;
  return 1;
}
static void backing_drop(AGX_WIN32_ASAHI_BACKEND *b,struct windows_bo *bo,
                         struct windows_bo *backing,unsigned pages) {
  for(unsigned i=0;i<bo->BackingCount;++i) {
    if(bo->Backings[i].Bo!=backing) continue;
    if(bo->Backings[i].Pages<=pages) {
      bo->Backings[i]=bo->Backings[--bo->BackingCount];
      --b->VirtualBackingRefs;
      agx_bo_unreference(b->Native,&backing->Base);
    } else {
      bo->Backings[i].Pages-=pages;
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
  if(bo->SparseTable) {
    agx_bo_unreference(b->Native,&bo->SparseTable->Base);
    bo->SparseTable=NULL;
  }
  free(bo->Pages);
  bo->Pages=NULL;
  bo->PageCount=0;
  if(bo->Reserved.Bound) {
    /* the range is freed once no submission is in flight (EXP979) */
    if(!virtual_retire(b,bo) ||
       !AgxWin32GpuvaFreeRange(&b->Gpuva,bo->Reserved.Va,bo->Reserved.Bytes)) {
      /* Unbind frees exactly Bytes: include the range's guard tile */
      AGX_WIN32_GPUVA_BO range=bo->Reserved;
      range.Bytes+=AGX_WIN32_GPUVA_RANGE_GUARD;
      if(!pending_va_push(b,&range)) AGX_WIN32_ASAHI_FAIL(b, 1u);
    }
    memset(&bo->Reserved,0,sizeof(bo->Reserved));
  }
  if(bo->ReadShadow.Bound) {
    if(!virtual_retire(b,bo) ||
       !AgxWin32GpuvaFreeRange(&b->Gpuva,bo->ReadShadow.Va,bo->ReadShadow.Bytes)) {
      AGX_WIN32_GPUVA_BO range=bo->ReadShadow;
      range.Bytes+=AGX_WIN32_GPUVA_RANGE_GUARD;
      if(!pending_va_push(b,&range)) AGX_WIN32_ASAHI_FAIL(b, 1u);
    }
    memset(&bo->ReadShadow,0,sizeof(bo->ReadShadow));
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
  if(bytes/AGX_WIN32_PAGE>UINT32_MAX || !(scratch=virtual_scratch(b))) return NULL;
  if(!(base=agx_bo_create(native,AGX_WIN32_TILE,0,0,label))) return NULL;
  bo=(struct windows_bo *)base;
  bo->Pages=calloc(bytes/AGX_WIN32_PAGE,sizeof(*bo->Pages));
  if(!bo->Pages || !AgxWin32GpuvaReserveRange(&b->Gpuva,bytes,&va)) {
    free(bo->Pages); bo->Pages=NULL;
    agx_bo_unreference(native,base);
    return NULL;
  }
  bo->Virtual=1;
  bo->PageCount=(unsigned)(bytes/AGX_WIN32_PAGE);
  bo->Reserved.Allocation=bo->Gpuva.Allocation;
  bo->Reserved.Va=va; bo->Reserved.Bytes=bytes; bo->Reserved.Bound=1;
  /* every tile starts on the tile scratch: reads see its contents, writes go
   * nowhere a real resource lives (plan: NULL tiles map scratch) */
  for(unsigned p=0;p<bo->PageCount;++p) bo->Pages[p]=scratch;
  if(!backing_add(b,bo,scratch,bo->PageCount) || !map_scratch(b,scratch,va,bo->PageCount)) {
    agx_bo_unreference(native,base);
    return NULL;
  }
  bo->Coordinate.addr=va;
  bo->Coordinate.size_B=bytes;
  base->size=bytes;
  return base;
}

int AgxWin32AsahiMapVirtualPages(struct agx_bo *base,unsigned first,unsigned count,
                                 struct agx_bo *backing_base,unsigned backing_page) {
  struct windows_bo *bo=(struct windows_bo *)base;
  struct windows_bo *backing=(struct windows_bo *)backing_base;
  AGX_WIN32_ASAHI_BACKEND *b=bo?bo->Backend:NULL;
  struct windows_bo *scratch,*target,*zero=NULL,*shadow_target=NULL;
  if(!b || !bo->Virtual || b->Failed || !count || first>bo->PageCount ||
     count>bo->PageCount-first || !(scratch=virtual_scratch(b))) return 0;
  if(backing && (backing->Backend!=b || backing->Virtual || !backing->Gpuva.Bound ||
                 backing_base->refcnt<=0 ||
                 (uint64_t)backing_page+count>backing->Gpuva.Bytes/AGX_WIN32_PAGE)) return 0;
  /* C53 Tier 2: the read-only shadow follows, onto the zero BO for NULL */
  const int shadow=bo->ReadShadow.Bound;
  if(shadow && !(zero=virtual_zero(b))) return 0;
  target=backing?backing:scratch;
  if(!virtual_retire(b,bo)) { AGX_WIN32_ASAHI_FAIL(b, 1u); return 0; }
  /* counted before the map so the reference never lapses on a remap onto
   * the same BO */
  if(!backing_add(b,bo,target,count)) return 0;
  if(shadow) {
    shadow_target=backing?backing:zero;
    if(!backing_add(b,bo,shadow_target,count)) { backing_drop(b,bo,target,count); return 0; }
  }
  const uint64_t offset=(uint64_t)first*AGX_WIN32_PAGE;
  int mapped=backing ?
      AgxWin32GpuvaMapRange(&b->Gpuva,bo->Reserved.Va+offset,backing->Gpuva.Allocation,
                            (uint64_t)backing_page*AGX_WIN32_PAGE,
                            (uint64_t)count*AGX_WIN32_PAGE,AGX_GPUVA_MAP_WRITE) :
      map_scratch(b,scratch,bo->Reserved.Va+offset,count);
  if(mapped && shadow)
    mapped=backing ?
        AgxWin32GpuvaMapRange(&b->Gpuva,bo->ReadShadow.Va+offset,backing->Gpuva.Allocation,
                              (uint64_t)backing_page*AGX_WIN32_PAGE,(uint64_t)count*AGX_WIN32_PAGE,0u) :
        map_scratch_as(b,zero,bo->ReadShadow.Va+offset,count,0u);
  if(!mapped) {
    /* a refused map leaves the old pages; after a partly mapped scratch run
     * some pages read scratch while their old references stay held (safe) */
    backing_drop(b,bo,target,count);
    if(shadow) backing_drop(b,bo,shadow_target,count);
    return 0;
  }
  for(unsigned p=first;p<first+count;++p) {
    struct windows_bo *old=bo->Pages[p];
    bo->Pages[p]=target;
    backing_drop(b,bo,old,1u);
    if(shadow) backing_drop(b,bo,old==scratch?zero:old,1u);
  }
  return 1;
}
int AgxWin32AsahiMapVirtualTiles(struct agx_bo *base,unsigned first,unsigned count,
                                 struct agx_bo *backing_base,unsigned backing_tile) {
  const unsigned per=AGX_WIN32_TILE/AGX_WIN32_PAGE;
  if(first>UINT32_MAX/per || count>UINT32_MAX/per || backing_tile>UINT32_MAX/per) return 0;
  return AgxWin32AsahiMapVirtualPages(base,first*per,count*per,backing_base,backing_tile*per);
}

int AgxWin32AsahiBoPrivate(const AGX_WIN32_ASAHI_BACKEND *b,
                           const struct agx_bo *base) {
#ifdef APPLE_AGX_GPUVA_WINSYS
  const struct windows_bo *bo=(const struct windows_bo *)base;
  return b && bo && bo->Backend==b && !bo->Imported;
#else
  (void)b;(void)base;
  return 0;
#endif
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
  /* the zero BO of read-only shadows is never written by the GPU either */
  return b && bo && (b->TileScratch==bo || b->TileZero==bo);
}

/* C53 T2D (D3D12-C53-tiled-textures.md) */
int AgxWin32AsahiArmVirtual(struct agx_device *native) {
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  if(!b || b->Native!=native || b->Failed || !b->GpuvaReady || b->NextVirtual) return 0;
  b->NextVirtual=1;
  return 1;
}
void AgxWin32AsahiDisarmVirtual(struct agx_device *native) {
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  if(b && b->Native==native) b->NextVirtual=0;
}
int AgxWin32AsahiIsVirtual(struct agx_bo *base) {
  const struct windows_bo *bo=(const struct windows_bo *)base;
  return bo && bo->Virtual;
}
int AgxWin32AsahiAttachSparseTable(struct agx_bo *base,size_t bytes) {
  struct windows_bo *bo=(struct windows_bo *)base;
  AGX_WIN32_ASAHI_BACKEND *b=bo?bo->Backend:NULL;
  struct agx_bo *table;
  void *map;
  if(!b || !bo->Virtual || bo->SparseTable || !bytes || b->Failed) return 0;
  if(!(table=agx_bo_create(b->Native,bytes,0,0,"Sparse table"))) return 0;
  if(!(map=agx_bo_map(table))) { agx_bo_unreference(b->Native,table); return 0; }
  /* every page starts unmapped: reads 0, non-resident */
  memset(map,0,bytes);
  bo->SparseTable=(struct windows_bo *)table;
  return 1;
}
/* C53 Tier 2: a reserved buffer's read-only shadow (Honeykrisp's
 * sparse_ro_offset half): a second range whose pages map the same backing
 * read-only and the tile zero BO where the buffer maps the tile scratch, so
 * reads through it return 0 for unmapped tiles. Attached while every page
 * is still unmapped (at creation). */
int AgxWin32AsahiAttachReadShadow(struct agx_bo *base) {
  struct windows_bo *bo=(struct windows_bo *)base;
  AGX_WIN32_ASAHI_BACKEND *b=bo?bo->Backend:NULL;
  struct windows_bo *zero,*scratch;
  uint64_t va=0;
  if(!b || !bo->Virtual || bo->ReadShadow.Bound || bo->SparseTable || b->Failed ||
     !(scratch=virtual_scratch(b)) || !(zero=virtual_zero(b)) ||
     bo->BackingCount!=1 || bo->Backings[0].Bo!=scratch) return 0;
  if(!AgxWin32GpuvaReserveRange(&b->Gpuva,bo->Reserved.Bytes,&va)) return 0;
  bo->ReadShadow.Allocation=bo->Gpuva.Allocation;
  bo->ReadShadow.Va=va; bo->ReadShadow.Bytes=bo->Reserved.Bytes; bo->ReadShadow.Bound=1;
  if(!backing_add(b,bo,zero,bo->PageCount)) return 0;
  /* a failed map leaves the shadow bound; virtual_release frees it */
  return map_scratch_as(b,zero,va,bo->PageCount,0u);
}
APPLE_AGX_U64 AgxWin32AsahiReadShadowVa(struct agx_bo *base) {
  const struct windows_bo *bo=(const struct windows_bo *)base;
  return bo && bo->Virtual && bo->ReadShadow.Bound ? bo->ReadShadow.Va : 0u;
}
APPLE_AGX_U64 AgxWin32AsahiSparseTableVa(struct agx_bo *base) {
  const struct windows_bo *bo=(const struct windows_bo *)base;
  return bo && bo->Virtual && bo->SparseTable && bo->SparseTable->Gpuva.Bound ?
      bo->SparseTable->Gpuva.Va : 0u;
}
struct agx_bo *AgxWin32AsahiVirtualTable(struct agx_bo *base) {
  const struct windows_bo *bo=(const struct windows_bo *)base;
  return bo && bo->Virtual && bo->SparseTable ? &bo->SparseTable->Base : NULL;
}
/* One reserved-texture tile: its pages (agx_win32_sparse.h) map heap tile
 * BackingTile (NULL: the tile scratch), runs of consecutive pages in one map,
 * then their Sparse Blocks point at the heap pages (0 for NULL). The blocks
 * name the heap's own pages, so texture reads do not depend on the image VA
 * (render-target and image writes use the image VA). */
int AgxWin32AsahiMapVirtualTilePages(struct agx_bo *base,const AGX_WIN32_SPARSE_PAGES *pages,
                                     struct agx_bo *backing_base,unsigned backing_tile) {
  struct windows_bo *bo=(struct windows_bo *)base;
  struct windows_bo *backing=(struct windows_bo *)backing_base;
  uint32_t *table;
  const unsigned per=AGX_WIN32_TILE/AGX_WIN32_PAGE;
  if(!bo || !bo->Virtual || !bo->SparseTable || !pages || !pages->Count || pages->Count>per ||
     backing_tile>UINT32_MAX/per ||
     !(table=(uint32_t *)agx_bo_map(&bo->SparseTable->Base))) return 0;
  for(unsigned i=0;i<pages->Count;++i)
    if((pages->Offset[i]%AGX_WIN32_PAGE) || pages->Offset[i]/AGX_WIN32_PAGE>=bo->PageCount ||
       pages->HeapPage[i]>=per ||
       (uint64_t)pages->TableIndex[i]*4u>=bo->SparseTable->Gpuva.Bytes) return 0;
  for(unsigned i=0;i<pages->Count;) {
    unsigned n=1;
    while(i+n<pages->Count && pages->Offset[i+n]==pages->Offset[i]+(uint64_t)n*AGX_WIN32_PAGE &&
          pages->HeapPage[i+n]==pages->HeapPage[i]+n) ++n;
    if(!AgxWin32AsahiMapVirtualPages(base,(unsigned)(pages->Offset[i]/AGX_WIN32_PAGE),n,backing_base,
                                     backing_tile*per+pages->HeapPage[i])) return 0;
    i+=n;
  }
  for(unsigned i=0;i<pages->Count;++i)
    table[pages->TableIndex[i]]=backing ?
        AgxWin32SparseBlock(backing->Gpuva.Va+(uint64_t)backing_tile*AGX_WIN32_TILE+
                            (uint64_t)pages->HeapPage[i]*AGX_WIN32_PAGE,1) : 0u;
  return 1;
}
#endif
static void native_map(struct agx_device *native,struct agx_bo *base,void *fixed) {
  struct windows_bo *bo=(struct windows_bo *)base;
  void *address=NULL;
  AGX_WIN32_ASAHI_BACKEND *b=native->windows_private;
  if(!b || b!=bo->Backend || fixed || b->Failed || base->refcnt<=0) return;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if ((unsigned)base->flags & AGX_WIN32_BO_GPU_ONLY) {
    if (AgxWin32GpuOnlyMapHook)
      AgxWin32GpuOnlyMapHook(base->handle, (unsigned)base->flags);
    return;
  }
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
    AGX_WIN32_ASAHI_CPU_STORAGE storage={0};
    if(!bo->Shadow && b->Ops.CpuStorage &&
       b->Ops.CpuStorage(b->Owner,base,bo->Backing.ConstructionSerial,&storage) &&
       storage.PrivateMap==address) {
      base->_map=address;
      return;
    }
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
    bo_identity(3u,base);
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
  pending_va_free_retired(b);
  b->RefusedRelease=0; /* the walk below retries every refused release */
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
void AgxWin32AsahiDrainPendingVa(AGX_WIN32_ASAHI_BACKEND *b) {
#ifdef APPLE_AGX_GPUVA_WINSYS
  APPLE_AGX_U32 cursor=0;
  const struct agx_bo *key;
  if(!b || !b->Native) return;
  pending_va_free_retired(b);
  if(!b->RefusedRelease || b->PendingVaCount>=AGX_WIN32_PENDING_VA_LIMIT) return;
  b->RefusedRelease=0;
  /* As Collect: a refused release is a registered BO with refcnt 0. */
  while((key=b->Ops.NextBo(b->Owner,&cursor))) {
    struct windows_bo *bo=(struct windows_bo *)key;
    if(bo->Backend==b && bo->Base.refcnt==0 && !bo->Cached) (void)dispose(bo);
  }
#else
  (void)b;
#endif
}
int AgxWin32AsahiDetach(AGX_WIN32_ASAHI_BACKEND *b) {
  if(!b || !b->Native || b->ActiveCapture || b->ActiveEmission) return 0;
#ifdef APPLE_AGX_GPUVA_WINSYS
  /* before the cache flush: a released tile scratch may enter the cache */
  if(b->TileScratch) { agx_bo_unreference(b->Native,b->TileScratch); b->TileScratch=NULL; }
  if(b->TileZero) { agx_bo_unreference(b->Native,b->TileZero); b->TileZero=NULL; }
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
#ifdef APPLE_AGX_GPUVA_WINSYS
  census_teardown(b);
#endif
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
