#include "agx_device.h"
#include "agx_win32_asahi_bo.h"
#include <stdlib.h>
#include <string.h>

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
#endif
};
static void native_map(struct agx_device *,struct agx_bo *,void *);
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

static int dispose(struct windows_bo *bo) {
  AGX_WIN32_ASAHI_BACKEND *b=bo->Backend;
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
                        bo->Backing.ConstructionSerial,release_map)) b->Failed=1;
    return 0;
  }
#endif
  if(AgxWin32NativeDeviceDestroyBo(&b->Buffers,&bo->Backing)!=AgxWin32NativeDeviceSuccess) {
    if(!b->Ops.Associate(b->Owner,bo->Backing.Buffer.Transport.Token,&bo->Base,
                        bo->Backing.ConstructionSerial,release_map)) b->Failed=1;
    return 0;
  }
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(bo->Gpuva.Bound && !AgxWin32GpuvaUnbind(&b->Gpuva,&bo->Gpuva))
    b->PendingVa[b->PendingVaCount++]=bo->Gpuva;
#endif
  --b->LiveBos;
  free(bo);
  return 1;
}

#ifdef APPLE_AGX_GPUVA_WINSYS
/* EXP996: every new BO costs AllocateCb, ReserveGpuVirtualAddress, a mapping
 * and residency paging-fence wait (EXP994 DWM: ~460 new BOs, 2553 paging waits,
 * 56 s). Keep released native BOs whole -- slot, VA, residency, CPU map -- and
 * hand them back to an equal request, as Mesa's agx_bo_cache does. */
static int cache_in_flight(const AGX_WIN32_ASAHI_BACKEND *b,const struct windows_bo *bo) {
  for(unsigned i=0;i<b->Gpuva.HeldCount;++i)
    if(b->Gpuva.Held[i]==bo->Gpuva.Allocation) return 1;
  return 0;
}
static int cache_put(struct windows_bo *bo) {
  AGX_WIN32_ASAHI_BACKEND *b=bo->Backend;
  if(bo->Imported || bo->Cached || !bo->Gpuva.Bound || b->Failed || b->Closing ||
     b->CacheCount>=AGX_WIN32_BO_CACHE_LIMIT ||
     bo->Base.size>AGX_WIN32_BO_CACHE_BYTES-b->CacheBytes) return 0;
  bo->Cached=1;
  b->Cache[b->CacheCount++]=bo;
  b->CacheBytes+=bo->Base.size;
  return 1;
}
/* EXP1042: as Mesa's agx_bo_cache, any BO of the same flags and class that
 * covers the request without exceeding twice its size; the smallest wins. */
static struct windows_bo *cache_take(AGX_WIN32_ASAHI_BACKEND *b,size_t bytes,
                                     unsigned flags,unsigned cls) {
  APPLE_AGX_U32 best=b->CacheCount;
  for(APPLE_AGX_U32 i=b->CacheCount;i-->0;) {
    struct windows_bo *bo=(struct windows_bo *)b->Cache[i];
    if(bo->Base.size<bytes || bo->Base.size/2>bytes ||
       (unsigned)bo->Base.flags!=flags ||
       bo->Backing.Buffer.ClassId!=cls || cache_in_flight(b,bo)) continue;
    if(best==b->CacheCount ||
       bo->Base.size<((struct windows_bo *)b->Cache[best])->Base.size) best=i;
  }
  if(best==b->CacheCount) return NULL;
  struct windows_bo *bo=(struct windows_bo *)b->Cache[best];
  b->Cache[best]=b->Cache[--b->CacheCount];
  b->CacheBytes-=bo->Base.size;
  bo->Cached=0;
  return bo;
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
  const unsigned allowed=AGX_BO_LOW_VA|AGX_BO_EXEC|AGX_BO_WRITEBACK|AGX_BO_READONLY;
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
    bo->Base.refcnt=1; bo->Base.label=label;
    return &bo->Base;
  }
#endif
  bo=calloc(1,sizeof(*bo));
  if(!bo) return NULL;
  bo->Backend=b;
  if(AgxWin32NativeDeviceCreateBo(&b->Buffers,cls,bytes,align,access,&bo->Backing)!=AgxWin32NativeDeviceSuccess) {
    /* The existing wrapper may retain a buffer if allocation rollback failed.
     * Stop new native work rather than silently losing that Windows resource. */
    b->Failed=1;
    if(bo->Backing.Live) b->UnpublishedBo=bo;
    else free(bo);
    return NULL;
  }
  bo->Base.dev=native; bo->Base.flags=flags; bo->Base.size=bytes;
  if(bo->Backing.Buffer.Transport.Token>UINT32_MAX) {
    if(AgxWin32NativeDeviceDestroyBo(&b->Buffers,&bo->Backing)!=AgxWin32NativeDeviceSuccess)
      b->UnpublishedBo=bo;
    else free(bo);
    b->Failed=1; return NULL;
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
    b->Failed=1;return NULL;
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
    b->Failed=1; return NULL;
  }
  ++b->LiveBos;
  return &bo->Base;
}

struct agx_bo *AgxWin32AsahiEncoderCreate(struct agx_device *native,
    size_t bytes,unsigned align,const char *label) {
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  struct agx_bo *bo;
  if(!b || b->Native!=native || b->Failed || b->EncoderAllocationIntent) return NULL;
  b->EncoderAllocationIntent=1;
  bo=agx_bo_create(native,bytes,align,0,label);
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
    b->Failed=1;return NULL;
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
    b->Failed=1;return NULL;
  }
  ++b->LiveBos;
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
     !bo->Gpuva.Bound || base->refcnt<=0 ||
     base->va!=&bo->Coordinate || base->va->addr!=bo->Gpuva.Va)
    return NULL;
  return &bo->Gpuva;
}
#endif
static void native_map(struct agx_device *native,struct agx_bo *base,void *fixed) {
  struct windows_bo *bo=(struct windows_bo *)base;
  void *address=NULL;
  AGX_WIN32_ASAHI_BACKEND *b=native->windows_private;
  if(!b || b!=bo->Backend || fixed || b->Failed || base->refcnt<=0) return;
  if(AgxWin32NativeBoMap(b->Buffers.Screen,&bo->Backing,AppleAgxWin32BufferCpuWrite,&address)
      !=AgxWin32NativeBoSuccess) { b->Failed=1; return; }
  base->_map=address;
}

void agx_bo_reference(struct agx_bo *base) {
  if(!base) return;
  struct windows_bo *bo=(struct windows_bo *)base;
  if(base->refcnt<=0 || base->refcnt==INT32_MAX) { bo->Backend->Failed=1; return; }
  ++base->refcnt;
}
void agx_bo_unreference(struct agx_device *native,struct agx_bo *base) {
  struct windows_bo *bo=(struct windows_bo *)base;
  if(!base) return;
  if(base->dev!=native || base->refcnt<=0) { bo->Backend->Failed=1; return; }
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
  cache_flush(b);
#endif
  if(!AgxWin32AsahiCollect(b)) return 0;
  b->Native->windows_private=NULL;
  b->Native->ops.bo_mmap=NULL;
  b->Ops.Leave(b->Owner);
  b->Native=NULL;
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
  while((key=b->Ops.NextBo(b->Owner,&cursor))!=NULL) {
    struct agx_bo *bo=(struct agx_bo *)key;
    AGX_WIN32_RELOC_ALLOCATION id;
    if(bo->dev==native && bo->handle==handle && bo->refcnt>0 &&
        AgxWin32AsahiIdentity(b,bo,&id)) return bo;
  }
  b->Failed=1;return NULL;
}
