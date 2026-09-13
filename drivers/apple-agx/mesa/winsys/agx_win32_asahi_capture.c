#include "agx_bo.h"
#include "agx_win32_asahi_capture.h"
#include <string.h>

static int same(const AGX_WIN32_RELOC_ALLOCATION *a,const AGX_WIN32_RELOC_ALLOCATION *b) {
  return a->Owner==b->Owner && a->Generation==b->Generation && a->Token==b->Token &&
    a->Serial==b->Serial && a->Bytes==b->Bytes && a->Access==b->Access &&
    a->AllocationIndex==b->AllocationIndex;
}
static int query(void *context,APPLE_AGX_U64 token,AGX_WIN32_RELOC_ALLOCATION *out) {
  AGX_WIN32_ASAHI_CAPTURE *c=context;
  for(unsigned i=0;i<c->Count;++i) {
    if(c->Identities[i].Token==token) {
      AGX_WIN32_RELOC_ALLOCATION current;
      if(!AgxWin32AsahiIdentity(c->Backend,c->Bos[i],&current)) return 0;
      current.AllocationIndex=i;
      if(!same(&current,&c->Identities[i])) return 0;
      *out=current; return 1;
    }
  }
  return 0;
}
static int retain(void *context,APPLE_AGX_U64 token,APPLE_AGX_U64 serial) {
  AGX_WIN32_ASAHI_CAPTURE *c=context;
  AGX_WIN32_RELOC_ALLOCATION current;
  if(!query(context,token,&current) || current.Serial!=serial) return 0;
  struct agx_bo *bo=c->Bos[current.AllocationIndex];
  if(bo->refcnt<=0 || bo->refcnt==INT32_MAX) return 0;
  agx_bo_reference(bo);
  return !c->Backend->Failed;
}
static int retain_exact(void *context,const AGX_WIN32_RELOC_ALLOCATION *expected) {
  AGX_WIN32_RELOC_ALLOCATION current;
  if(!query(context,expected->Token,&current) || !same(&current,expected)) return 0;
  return retain(context,expected->Token,expected->Serial);
}
static void release(void *context,APPLE_AGX_U64 token,APPLE_AGX_U64 serial) {
  AGX_WIN32_ASAHI_CAPTURE *c=context;
  for(unsigned i=0;i<c->Count;++i)
    if(c->Identities[i].Token==token && c->Identities[i].Serial==serial) {
      agx_bo_unreference(c->Backend->Native,c->Bos[i]); return;
    }
  c->Backend->Failed=1;
}
AGX_WIN32_RELOC_RESULT AgxWin32AsahiCaptureBegin(AGX_WIN32_ASAHI_CAPTURE *c,
    AGX_WIN32_ASAHI_BACKEND *backend,APPLE_AGX_U64 owner,APPLE_AGX_U32 generation,
    APPLE_AGX_U64 request) {
  const AGX_WIN32_RELOC_OPERATIONS ops={query,retain,release,retain_exact};
  if(!c || !backend || !backend->Native || backend->Failed ||
     backend->ActiveCapture || backend->ActiveEmission) return AgxRelocArgument;
  AGX_WIN32_RELOC_RESULT result=AgxWin32RelocBegin(&c->Capture,owner,generation,request,&ops,c);
  if(result!=AgxRelocOk) return result;
  c->Backend=backend; c->Count=0;
  memset(c->Bos,0,sizeof(c->Bos)); memset(c->Identities,0,sizeof(c->Identities));
  return AgxRelocOk;
}
AGX_WIN32_RELOC_RESULT AgxWin32AsahiCaptureReference(AGX_WIN32_ASAHI_CAPTURE *c,
    struct agx_bo *bo,APPLE_AGX_U32 role,APPLE_AGX_U32 access,
    APPLE_AGX_U64 offset,APPLE_AGX_U64 bytes,APPLE_AGX_U32 *index) {
  AGX_WIN32_RELOC_ALLOCATION a;
  unsigned i;
  if(!c || !c->Backend || !bo || !index) return AgxRelocArgument;
  if(!AgxWin32AsahiIdentity(c->Backend,bo,&a)) return AgxRelocStale;
  for(i=0;i<c->Count;++i) if(c->Identities[i].Token==a.Token) break;
  if(i==APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES) return AgxRelocCapacity;
  a.AllocationIndex=i;
  int added=i==c->Count;
  if(added) { c->Bos[i]=bo; c->Identities[i]=a; ++c->Count; }
  else if(!same(&a,&c->Identities[i])) return AgxRelocStale;
  AGX_WIN32_RELOC_RESULT result=AgxWin32RelocReferenceExpected(&c->Capture,&a,role,access,offset,bytes,index);
  if(result!=AgxRelocOk && added) { --c->Count; c->Bos[i]=NULL; }
  return result;
}

AGX_WIN32_RELOC_RESULT AgxWin32AsahiCaptureAddress(AGX_WIN32_ASAHI_CAPTURE *c,
    APPLE_AGX_U64 address,APPLE_AGX_U64 bytes,APPLE_AGX_U32 role,
    APPLE_AGX_U32 access,APPLE_AGX_U32 *index) {
  struct agx_bo *bo=NULL; APPLE_AGX_U64 offset=0;
  if(index) *index=~0u;
  if(!c || !c->Backend || !index) return AgxRelocArgument;
  if(!AgxWin32AsahiFindAddress(c->Backend,c->Capture.Owner,c->Capture.Generation,
                              address,bytes,&bo,&offset)) return AgxRelocStale;
  /* Caller serialization covers lookup through RetainExact; this is not an
   * unlocked lookup/retain protocol for concurrent native producers. */
  return AgxWin32AsahiCaptureReference(c,bo,role,access,offset,bytes,index);
}

AGX_WIN32_RELOC_RESULT AgxWin32AsahiCaptureCpuRange(AGX_WIN32_ASAHI_CAPTURE *c,
    const void *cpu,APPLE_AGX_U64 address,APPLE_AGX_U64 bytes,
    APPLE_AGX_U32 role,APPLE_AGX_U32 access,APPLE_AGX_U32 *index) {
  struct agx_bo *bo=NULL; APPLE_AGX_U64 actual=0,offset=0;
  if(index) *index=~0u;
  if(!c || !c->Backend || !cpu || !address || !bytes || !index) return AgxRelocArgument;
  if(!AgxWin32AsahiFindCpuAddress(c->Backend,c->Capture.Owner,c->Capture.Generation,
      cpu,bytes,&bo,&actual,&offset) || actual!=address) return AgxRelocStale;
  return AgxWin32AsahiCaptureReference(c,bo,role,access,offset,bytes,index);
}
