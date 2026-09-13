#include "agx_win32_reloc_capture.h"
#include <string.h>
enum { RECORDING=1, SEALED=2, SUBMITTED=3 };
static unsigned width(unsigned kind) {
  switch(kind) {
    case AppleAgxWin32RelocationEncoderAddress:
    case AppleAgxWin32RelocationPipelineAddress:
    case AppleAgxWin32RelocationDescriptorAddress:
    case AppleAgxWin32RelocationUscBufferAddress40:
    case AppleAgxWin32RelocationUscPreshaderOffset32:
    case AppleAgxWin32RelocationUscTableAddress39:
    case AppleAgxWin32RelocationPppStateAddress40: return 8;
    case AppleAgxWin32RelocationUscShaderOffset32: return 6;
    case AppleAgxWin32RelocationVdmPipelineOffset32: return 4;
    case AppleAgxWin32RelocationPppPipelineOffset32:
    case AppleAgxWin32RelocationPppCfBindingsOffset32: return 4;
    default: return 0;
  }
}
static int same(const AGX_WIN32_RELOC_ALLOCATION *a,const AGX_WIN32_RELOC_ALLOCATION *b) {
  return a->Owner==b->Owner && a->Token==b->Token && a->Serial==b->Serial &&
      a->Bytes==b->Bytes && a->Generation==b->Generation &&
      a->AllocationIndex==b->AllocationIndex && a->Access==b->Access;
}
static int overlap(APPLE_AGX_U64 a,APPLE_AGX_U64 na,APPLE_AGX_U64 b,APPLE_AGX_U64 nb) {
  return a<=b ? b-a<na : a-b<nb;
}
AGX_WIN32_RELOC_RESULT AgxWin32RelocBegin(AGX_WIN32_RELOC_CAPTURE *c,
    APPLE_AGX_U64 owner,APPLE_AGX_U32 generation,APPLE_AGX_U64 request,
    const AGX_WIN32_RELOC_OPERATIONS *ops,void *context) {
  if(!c || !owner || !generation || !request || !ops || !context ||
      !ops->Query || !ops->Retain || !ops->Release) return AgxRelocArgument;
  if(c->State) return AgxRelocState;
  if(request<=c->LastRequest) return AgxRelocStale;
  AGX_WIN32_RELOC_OPERATIONS saved_ops=*ops;
  memset(c,0,sizeof(*c)); c->Owner=owner; c->Generation=generation;
  c->Request=c->LastRequest=request; c->Operations=saved_ops; c->Context=context;
  c->State=RECORDING; return AgxRelocOk;
}
AGX_WIN32_RELOC_RESULT AgxWin32RelocReference(AGX_WIN32_RELOC_CAPTURE *c,
    APPLE_AGX_U64 token,APPLE_AGX_U32 role,APPLE_AGX_U32 access,
    APPLE_AGX_U64 offset,APPLE_AGX_U64 bytes,APPLE_AGX_U32 *index) {
  AGX_WIN32_RELOC_ALLOCATION a={0};
  if(!c || !token || !index || !access || access&~7u || role<1 || role>13) return AgxRelocArgument;
  if(c->State!=RECORDING) return AgxRelocState;
  if(!c->Operations.Query(c->Context,token,&a)) return AgxRelocCallback;
  if(a.Owner!=c->Owner || a.Generation!=c->Generation || a.Token!=token || !a.Serial ||
      a.AllocationIndex==~0u) return AgxRelocStale;
  if(!bytes || offset>a.Bytes || bytes>a.Bytes-offset || (access&a.Access)!=access) return AgxRelocRange;
  for(unsigned i=0;i<c->ReferenceCount;++i) {
    const AGX_WIN32_RELOC_ALLOCATION *old=&c->Allocations[i];
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *ref=&c->References[i];
    if(old->Token==token || old->AllocationIndex==a.AllocationIndex) {
      if(!same(old,&a)) return AgxRelocStale;
      if(ref->Role==role && ref->Access==access && ref->Offset==offset && ref->Bytes==bytes) {
        *index=i; return AgxRelocOk;
      }
      /* Disjoint pool suballocations are allowed. Aliased overlapping roles
       * need an explicit copy/alias contract, never implicit duplicate images. */
      if(overlap(ref->Offset,ref->Bytes,offset,bytes)) return AgxRelocOverlap;
    }
  }
  if(c->ReferenceCount>=APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES) return AgxRelocCapacity;
  if(!c->Operations.Retain(c->Context,token,a.Serial)) return AgxRelocCallback;
  unsigned n=c->ReferenceCount++;
  c->Allocations[n]=a;
  c->References[n]=(APPLE_AGX_WIN32_ALLOCATION_REFERENCE){a.AllocationIndex,access,role,0,offset,bytes};
  *index=n; return AgxRelocOk;
}
AGX_WIN32_RELOC_RESULT AgxWin32RelocReferenceExpected(
    AGX_WIN32_RELOC_CAPTURE *c, const AGX_WIN32_RELOC_ALLOCATION *expected,
    APPLE_AGX_U32 role, APPLE_AGX_U32 access, APPLE_AGX_U64 offset,
    APPLE_AGX_U64 bytes, APPLE_AGX_U32 *index) {
  AGX_WIN32_RELOC_ALLOCATION current={0};
  if(!c || !expected || !expected->Owner || !expected->Token ||
      !expected->Serial || !expected->Generation || !index || !access ||
      access&~7u || role<1 || role>13) return AgxRelocArgument;
  if(c->State!=RECORDING) return AgxRelocState;
  if(expected->Owner!=c->Owner || expected->Generation!=c->Generation ||
      expected->AllocationIndex==~0u) return AgxRelocStale;
  if(!c->Operations.Query(c->Context,expected->Token,&current)) return AgxRelocCallback;
  if(!same(&current,expected)) return AgxRelocStale;
  if(!bytes || offset>expected->Bytes || bytes>expected->Bytes-offset ||
      (access&expected->Access)!=access) return AgxRelocRange;
  for(unsigned i=0;i<c->ReferenceCount;++i) {
    const AGX_WIN32_RELOC_ALLOCATION *old=&c->Allocations[i];
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *ref=&c->References[i];
    if(old->Token==expected->Token || old->AllocationIndex==expected->AllocationIndex) {
      if(!same(old,expected)) return AgxRelocStale;
      if(ref->Role==role && ref->Access==access && ref->Offset==offset && ref->Bytes==bytes) {
        *index=i; return AgxRelocOk;
      }
      if(overlap(ref->Offset,ref->Bytes,offset,bytes)) return AgxRelocOverlap;
    }
  }
  if(c->ReferenceCount>=APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES) return AgxRelocCapacity;
  if(!c->Operations.RetainExact ||
      !c->Operations.RetainExact(c->Context,expected)) return AgxRelocCallback;
  unsigned n=c->ReferenceCount++;
  c->Allocations[n]=*expected;
  c->References[n]=(APPLE_AGX_WIN32_ALLOCATION_REFERENCE){expected->AllocationIndex,access,role,0,offset,bytes};
  *index=n; return AgxRelocOk;
}
AGX_WIN32_RELOC_RESULT AgxWin32RelocField(AGX_WIN32_RELOC_CAPTURE *c,
    APPLE_AGX_U32 kind,APPLE_AGX_U32 dest,APPLE_AGX_U64 destoff,
    APPLE_AGX_U32 target,APPLE_AGX_U64 targetoff) {
  unsigned w=width(kind);
  if(!c || !w) return AgxRelocArgument;
  if(c->State!=RECORDING) return AgxRelocState;
  if(dest>=c->ReferenceCount || target>=c->ReferenceCount ||
      destoff>c->References[dest].Bytes || w>c->References[dest].Bytes-destoff ||
      targetoff>=c->References[target].Bytes) return AgxRelocRange;
  for(unsigned i=0;i<c->RelocationCount;++i) {
    const APPLE_AGX_WIN32_RELOCATION *r=&c->Relocations[i];
    if(r->DestinationReference==dest && overlap(r->DestinationOffset,r->WidthBytes,destoff,w))
      return AgxRelocOverlap;
  }
  if(c->RelocationCount>=APPLE_AGX_WIN32_COMMAND_MAX_RELOCATIONS) return AgxRelocCapacity;
  c->Relocations[c->RelocationCount++]=(APPLE_AGX_WIN32_RELOCATION){kind,(APPLE_AGX_U16)w,0,
      dest,target,destoff,targetoff,0};
  return AgxRelocOk;
}
AGX_WIN32_RELOC_RESULT AgxWin32RelocSealVersion(
    AGX_WIN32_RELOC_CAPTURE *c, APPLE_AGX_U16 commandVersion,
    const APPLE_AGX_WIN32_DRAW_PAYLOAD *draw, void *command,
    APPLE_AGX_U32 capacity, APPLE_AGX_U32 *bytes) {
  if(!c || !draw || !command || !bytes) return AgxRelocArgument;
  if(commandVersion != APPLE_AGX_WIN32_COMMAND_VERSION &&
      commandVersion != APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_PIPELINES &&
      commandVersion != APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC)
    return AgxRelocCommand;
  if(c->State!=RECORDING) return AgxRelocState;
  AGX_WIN32_DRAW_REQUEST request={0}; request.Generation=c->Generation;
  request.ReferenceCount=c->ReferenceCount; request.RelocationCount=c->RelocationCount;
  request.References=c->References; request.Relocations=c->Relocations; request.Draw=*draw;
  for(unsigned i=0;i<c->ReferenceCount;++i) {
    AGX_WIN32_RELOC_ALLOCATION current={0};
    if(!c->Operations.Query(c->Context,c->Allocations[i].Token,&current)) return AgxRelocCallback;
    if(!same(&current,&c->Allocations[i])) return AgxRelocStale;
    if(current.AllocationIndex>=request.AllocationCount) request.AllocationCount=current.AllocationIndex+1;
  }
  if(AgxWin32TransportBuildDrawVersion(&request,commandVersion,command,capacity,bytes)!=AppleAgxWin32AbiSuccess)
    return AgxRelocCommand;
  c->State=SEALED; return AgxRelocOk;
}
AGX_WIN32_RELOC_RESULT AgxWin32RelocPrepareDraw(
    AGX_WIN32_RELOC_CAPTURE *c, APPLE_AGX_U16 commandVersion,
    const APPLE_AGX_WIN32_DRAW_PAYLOAD *draw, AGX_WIN32_DRAW_REQUEST *request) {
  if(!c || !draw || !request) return AgxRelocArgument;
  if(commandVersion != APPLE_AGX_WIN32_COMMAND_VERSION &&
      commandVersion != APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_PIPELINES &&
      commandVersion != APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC)
    return AgxRelocCommand;
  if(c->State!=RECORDING) return AgxRelocState;
  memset(request,0,sizeof(*request));
  request->Generation=c->Generation;
  request->ReferenceCount=c->ReferenceCount;
  request->RelocationCount=c->RelocationCount;
  request->References=c->References;
  request->Relocations=c->Relocations;
  request->Draw=*draw;
  for(unsigned i=0;i<c->ReferenceCount;++i) {
    AGX_WIN32_RELOC_ALLOCATION current={0};
    if(!c->Operations.Query(c->Context,c->Allocations[i].Token,&current)) return AgxRelocCallback;
    if(!same(&current,&c->Allocations[i])) return AgxRelocStale;
  }
  c->State=SEALED;
  return AgxRelocOk;
}
AGX_WIN32_RELOC_RESULT AgxWin32RelocSeal(AGX_WIN32_RELOC_CAPTURE *c,
    const APPLE_AGX_WIN32_DRAW_PAYLOAD *draw,void *command,
    APPLE_AGX_U32 capacity,APPLE_AGX_U32 *bytes) {
  return AgxWin32RelocSealVersion(c, APPLE_AGX_WIN32_COMMAND_VERSION, draw,
                                  command, capacity, bytes);
}
AGX_WIN32_RELOC_RESULT AgxWin32RelocSubmitted(AGX_WIN32_RELOC_CAPTURE *c,APPLE_AGX_U32 fence) {
  if(!c || !fence) return AgxRelocArgument;
  if(c->State!=SEALED) return AgxRelocState;
  c->Fence=fence; c->State=SUBMITTED; return AgxRelocOk;
}
static void release_all(AGX_WIN32_RELOC_CAPTURE *c) {
  while(c->ReferenceCount) {
    unsigned i=--c->ReferenceCount;
    c->Operations.Release(c->Context,c->Allocations[i].Token,c->Allocations[i].Serial);
  }
  c->RelocationCount=0; c->State=0; c->Fence=0;
}
AGX_WIN32_RELOC_RESULT AgxWin32RelocRetire(AGX_WIN32_RELOC_CAPTURE *c,
    APPLE_AGX_U64 owner,APPLE_AGX_U32 generation,APPLE_AGX_U64 request,APPLE_AGX_U32 fence) {
  if(!c) return AgxRelocArgument;
  if(c->State!=SUBMITTED) return AgxRelocState;
  if(owner!=c->Owner || generation!=c->Generation || request!=c->Request || fence!=c->Fence)
    return AgxRelocStale;
  release_all(c); return AgxRelocOk;
}
AGX_WIN32_RELOC_RESULT AgxWin32RelocAbort(AGX_WIN32_RELOC_CAPTURE *c) {
  if(!c) return AgxRelocArgument;
  if(c->State!=RECORDING && c->State!=SEALED) return AgxRelocState;
  release_all(c); return AgxRelocOk;
}
