#include "agx_device.h"
#include "agx_win32_asahi_bo.h"
#include <stdlib.h>
#include <string.h>

/* Windows replacement for the OS-dependent BO symbols used by native pool.c.
 * va.addr is construction-only. No Linux VM bind or hardware VA is allocated. */
struct windows_bo {
  struct agx_bo Base;
  struct agx_va Coordinate;
  AGX_WIN32_NATIVE_BO Backing;
  AGX_WIN32_ASAHI_BACKEND *Backend;
};
static void native_map(struct agx_device *,struct agx_bo *,void *);

int AgxWin32AsahiAttach(AGX_WIN32_ASAHI_BACKEND *b, struct agx_device *native,
    AGX_WIN32_SCREEN *screen, const AGX_WIN32_ASAHI_OWNER_OPS *ops, void *owner,
    APPLE_AGX_U64 base) {
  if(!b || !native || !screen || !ops || !owner || b->Native || native->windows_private ||
     !ops->Enter || !ops->Leave || !ops->Associate || !ops->Detach || !ops->Identity || !ops->NextBo)
    return 0;
  if(!ops->Enter(owner,screen)) return 0;
  if(AgxWin32NativeDeviceInitialize(&b->Buffers,screen,base,screen->Generation)!=AgxWin32NativeDeviceSuccess) {
    ops->Leave(owner);
    return 0;
  }
  b->Native=native; b->Ops=*ops; b->Owner=owner; b->LiveBos=0; b->Failed=0; b->UnpublishedBo=NULL;
  b->ActiveCapture=NULL; b->ActiveEmission=NULL;
  native->windows_private=b; native->shader_base=base;
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
  if(AgxWin32NativeDeviceDestroyBo(&b->Buffers,&bo->Backing)!=AgxWin32NativeDeviceSuccess) {
    if(!b->Ops.Associate(b->Owner,bo->Backing.Buffer.Transport.Token,&bo->Base,
                        bo->Backing.ConstructionSerial)) b->Failed=1;
    return 0;
  }
  --b->LiveBos;
  free(bo);
  return 1;
}

struct agx_bo *agx_bo_create(struct agx_device *native,size_t bytes,unsigned align,
                            enum agx_bo_flags flags,const char *label) {
  const unsigned allowed=AGX_BO_LOW_VA|AGX_BO_EXEC|AGX_BO_WRITEBACK|AGX_BO_READONLY;
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  struct windows_bo *bo;
  unsigned cls,access;
  if(!b || b->Native!=native || b->Failed || !bytes || bytes>SIZE_MAX-0x3fff ||
     ((unsigned)flags & ~allowed) || ((flags&AGX_BO_EXEC) && !(flags&AGX_BO_LOW_VA)))
    return NULL;
  bytes=(bytes+0x3fff)&~(size_t)0x3fff;
  if(align<0x4000) align=0x4000;
  if(align!=0x4000) return NULL; /* construction allocator currently guarantees 16 KiB */
  cls=(flags&AGX_BO_EXEC)?AgxWin32BufferClassShader:
      ((flags&AGX_BO_LOW_VA)?AgxWin32BufferClassEncoder:AgxWin32BufferClassGeneral);
  access=AppleAgxWin32BufferCpuWrite|AppleAgxWin32BufferGpuRead;
  if(cls==AgxWin32BufferClassGeneral) {
    access|=AppleAgxWin32BufferCpuRead;
    if(!(flags&AGX_BO_READONLY)) access|=AppleAgxWin32BufferGpuWrite;
  }
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
  bo->Coordinate.addr=bo->Backing.ConstructionAddress; bo->Coordinate.size_B=bytes;
  bo->Coordinate.flags=(flags&AGX_BO_LOW_VA)?AGX_VA_USC:0;
  bo->Base.va=&bo->Coordinate;
  if(!b->Ops.Associate(b->Owner,bo->Backing.Buffer.Transport.Token,&bo->Base,
                      bo->Backing.ConstructionSerial)) {
    if(AgxWin32NativeDeviceDestroyBo(&b->Buffers,&bo->Backing)!=AgxWin32NativeDeviceSuccess)
      b->UnpublishedBo=bo;
    else free(bo);
    b->Failed=1; return NULL;
  }
  ++b->LiveBos;
  return &bo->Base;
}

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
  if(--base->refcnt==0) (void)dispose(bo);
}
int AgxWin32AsahiCollect(AGX_WIN32_ASAHI_BACKEND *b) {
  APPLE_AGX_U32 cursor=0;
  const struct agx_bo *key;
  if(!b || !b->Native) return 0;
  if(b->UnpublishedBo) {
    struct windows_bo *bo=b->UnpublishedBo;
    int released=bo->Backing.ConstructionSerial?
      AgxWin32NativeDeviceDestroyBo(&b->Buffers,&bo->Backing)==AgxWin32NativeDeviceSuccess:
      AgxWin32NativeBoDestroy(b->Buffers.Screen,&bo->Backing)==AgxWin32NativeBoSuccess;
    if(released) { free(bo); b->UnpublishedBo=NULL; }
  }
  while((key=b->Ops.NextBo(b->Owner,&cursor))) {
    struct windows_bo *bo=(struct windows_bo *)key;
    if(bo->Backend==b && bo->Base.refcnt==0) (void)dispose(bo);
  }
  return b->LiveBos==0 && b->UnpublishedBo==NULL;
}
int AgxWin32AsahiDetach(AGX_WIN32_ASAHI_BACKEND *b) {
  if(!b || !b->Native || b->ActiveCapture || b->ActiveEmission || !AgxWin32AsahiCollect(b)) return 0;
  b->Native->windows_private=NULL;
  b->Native->ops.bo_mmap=NULL;
  b->Ops.Leave(b->Owner);
  b->Native=NULL;
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
    if(AgxWin32NativeDeviceResolveBo(&b->Buffers,&bo->Backing,0,bo->Backing.Bytes,&base)
         !=AgxWin32NativeDeviceSuccess || !bo->Base.va ||
       bo->Base.va->addr!=base || bo->Base.size!=identity.Bytes ||
       bo->Backing.Bytes!=identity.Bytes) return 0;
    if(address<base || address-base>=identity.Bytes || bytes>identity.Bytes-(address-base))
      continue;
    if(found || AgxWin32NativeDeviceResolveBo(&b->Buffers,&bo->Backing,address-base,
        bytes,&resolved)!=AgxWin32NativeDeviceSuccess || resolved!=address) return 0;
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
       AgxWin32NativeDeviceResolveBo(&b->Buffers,&bo->Backing,0,
         bo->Backing.Bytes,&base)!=AgxWin32NativeDeviceSuccess ||
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
