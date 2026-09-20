#include <stdio.h>
#include "agx_device.h"
#include "asahi/genxml/agx_pack.h"
#include "agx_win32_asahi_pipeline.h"
#include <string.h>

static int emission_begin(struct agx_device *,void *,APPLE_AGX_U64,
    APPLE_AGX_U32,APPLE_AGX_U32,AGX_WIN32_ASAHI_PIPELINE *,int);

int AgxWin32AsahiCaptureActivate(AGX_WIN32_ASAHI_CAPTURE *c) {
  if(!c || !c->Backend || c->Backend->Failed || !c->Backend->Native ||
     c->Capture.State!=1u || c->Capture.Generation!=c->Backend->Buffers.Generation ||
     c->Backend->ActiveCapture || c->Backend->ActiveEmission) return 0;
  c->Backend->ActiveCapture=c; return 1;
}
int AgxWin32AsahiCaptureDeactivate(AGX_WIN32_ASAHI_CAPTURE *c) {
  if(!c || !c->Backend || c->Backend->ActiveCapture!=c || c->Backend->ActiveEmission)
    return 0;
  c->Backend->ActiveCapture=NULL; return 1;
}
static int encoder_root_valid(AGX_WIN32_ASAHI_ENCODER_ROOT *r) {
  AGX_WIN32_ASAHI_CAPTURE *c=r?r->Scope.Capture:NULL;
  struct agx_bo *bo=NULL; APPLE_AGX_U64 offset=0;
  AGX_WIN32_RELOC_ALLOCATION identity;
  APPLE_AGX_U32 class_id=0;
  if(!c || (c->EncoderRoot!=r && c->ComputeEncoderRoot!=r) ||
     r->Scope.Root!=r || r->Finalized || r->Scope.Failed || !c->Backend ||
     c->Backend->Failed || c->Backend->ActiveCapture!=c || c->Capture.State!=1u ||
     c->Capture.Request!=r->Request || c->Capture.Owner!=r->Identity.Owner ||
     c->Capture.Generation!=r->Identity.Generation ||
     r->Scope.Reference>=c->Capture.ReferenceCount ||
     !AgxWin32AsahiFindAddress(c->Backend,c->Capture.Owner,c->Capture.Generation,
       r->Address,r->Scope.Capacity,&bo,&offset) ||
     offset!=r->AllocationOffset || !bo->_map ||
     (uintptr_t)bo->_map>UINTPTR_MAX-offset ||
     (uintptr_t)r->Scope.Cpu!=(uintptr_t)bo->_map+offset ||
     !AgxWin32AsahiClass(c->Backend,bo,&class_id) || class_id!=AgxWin32BufferClassEncoder ||
     !AgxWin32AsahiIdentity(c->Backend,bo,&identity) ||
     identity.Owner!=r->Identity.Owner || identity.Generation!=r->Identity.Generation ||
     identity.Token!=r->Identity.Token || identity.Serial!=r->Identity.Serial ||
     identity.Bytes!=r->Identity.Bytes || identity.Access!=r->Identity.Access ||
     r->Identity.AllocationIndex>=c->Count ||
     c->Bos[r->Identity.AllocationIndex]!=bo) return 0;
  unsigned encoder_roots=0;
  for(unsigned i=0;i<c->Capture.ReferenceCount;++i)
    if(c->Capture.References[i].Role==AppleAgxWin32RoleEncoder) ++encoder_roots;
  if(encoder_roots!=(c->ComputeEncoderRoot?2u:1u) ||
     (c->ComputeEncoderRoot &&
      c->Capture.CommandVersion!=APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH))
    return 0;
  const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *ref=&c->Capture.References[r->Scope.Reference];
  const AGX_WIN32_RELOC_ALLOCATION *a=&c->Capture.Allocations[r->Scope.Reference];
  return ref->Role==AppleAgxWin32RoleEncoder && ref->Access==AppleAgxWin32AccessRead &&
    ref->Offset==offset && ref->Bytes==r->Scope.Capacity &&
    a->Owner==identity.Owner && a->Generation==identity.Generation &&
    a->Token==identity.Token && a->Serial==identity.Serial &&
    a->Bytes==identity.Bytes && a->Access==identity.Access &&
    a->AllocationIndex==r->Identity.AllocationIndex;
}
static int encoder_root_begin(struct agx_device *native,void *cpu,
    APPLE_AGX_U64 address,APPLE_AGX_U32 capacity,
    AGX_WIN32_ASAHI_ENCODER_ROOT *r,int compute) {
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  AGX_WIN32_ASAHI_CAPTURE *c=b?b->ActiveCapture:NULL;
  if(!r || r->Scope.Capture || r->Finalized || !c || b->ActiveEmission ||
     (!compute && (c->EncoderRoot || c->ComputeEncoderRoot)) ||
     (compute && (c->Capture.CommandVersion!=
          APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH ||
          !c->EncoderRoot || c->ComputeEncoderRoot))) return 0;
  /* Ordinary requests own one persistent encoder interval. Mixed v8 owns an
   * ordered VDM root followed by one CDM root. */
  unsigned encoder_roots=0;
  for(unsigned i=0;i<c->Capture.ReferenceCount;++i)
    if(c->Capture.References[i].Role==AppleAgxWin32RoleEncoder) ++encoder_roots;
  if(encoder_roots!=(compute?1u:0u) ||
     !emission_begin(native,cpu,address,capacity,AppleAgxWin32RoleEncoder,
       &r->Scope,1)) return 0;
  r->Scope.Root=r;
  r->Identity=c->Capture.Allocations[r->Scope.Reference];
  r->AllocationOffset=c->Capture.References[r->Scope.Reference].Offset;
  r->Request=c->Capture.Request; r->Address=address; r->CompletedEnd=0;
  if(compute) c->ComputeEncoderRoot=r;
  else c->EncoderRoot=r;
  b->ActiveEmission=NULL;
  return 1;
}
int AgxWin32AsahiEncoderRootBegin(struct agx_device *native,void *cpu,
    APPLE_AGX_U64 address,APPLE_AGX_U32 capacity,AGX_WIN32_ASAHI_ENCODER_ROOT *r) {
  return encoder_root_begin(native,cpu,address,capacity,r,0);
}
int AgxWin32AsahiComputeEncoderRootBegin(struct agx_device *native,void *cpu,
    APPLE_AGX_U64 address,APPLE_AGX_U32 capacity,AGX_WIN32_ASAHI_ENCODER_ROOT *r) {
  return encoder_root_begin(native,cpu,address,capacity,r,1);
}
int AgxWin32AsahiEncoderRootEnter(struct agx_device *native,void *cpu,
    APPLE_AGX_U64 address,APPLE_AGX_U32 capacity,AGX_WIN32_ASAHI_ENCODER_ROOT *r) {
  if(!encoder_root_valid(r) || r->Scope.Capture->Backend->Native!=native ||
     r->Scope.Capture->Backend->ActiveEmission || cpu!=r->Scope.Cpu ||
     address!=r->Address || capacity!=r->Scope.Capacity) return 0;
  r->Scope.Capture->Backend->ActiveEmission=&r->Scope; return 1;
}
int AgxWin32AsahiEncoderRootLeave(AGX_WIN32_ASAHI_ENCODER_ROOT *r) {
  if(!r || !r->Scope.Capture ||
     r->Scope.Capture->Backend->ActiveEmission!=&r->Scope) return 0;
  /* Always permit unwinding the stable root after an inner failure/abort. */
  r->Scope.Capture->Backend->ActiveEmission=NULL; return 1;
}
int AgxWin32AsahiEncoderRootFinalize(AGX_WIN32_ASAHI_ENCODER_ROOT *r,const void *end) {
  if(!encoder_root_valid(r) || r->Scope.Capture->Backend->ActiveEmission ||
     (uintptr_t)end<(uintptr_t)r->Scope.Cpu) return 0;
  APPLE_AGX_U64 length=(uintptr_t)end-(uintptr_t)r->Scope.Cpu;
  if(!length || length>r->Scope.Capacity || length<r->CompletedEnd) return 0;
  AGX_WIN32_RELOC_CAPTURE *c=&r->Scope.Capture->Capture;
  for(unsigned i=0;i<c->RelocationCount;++i) {
    const APPLE_AGX_WIN32_RELOCATION *rel=&c->Relocations[i];
    if(rel->DestinationReference==r->Scope.Reference &&
       (rel->DestinationOffset>length || rel->WidthBytes>length-rel->DestinationOffset))
      return 0;
  }
  c->References[r->Scope.Reference].Bytes=length;
  r->CompletedEnd=(APPLE_AGX_U32)length;
  r->Finalized=1; return 1;
}
int AgxWin32AsahiEncoderDrawPreflight(struct agx_device *native,struct agx_bo *bo,
    const void *current,const void *end,APPLE_AGX_U64 bytes) {
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  if(!b || !b->ActiveCapture) return 1;
  AGX_WIN32_ASAHI_PIPELINE *active=b->ActiveEmission;
  AGX_WIN32_ASAHI_ENCODER_ROOT *r=active?active->Root:NULL;
  AGX_WIN32_RELOC_ALLOCATION identity;
  const APPLE_AGX_U64 tail=AGX_VDM_STREAM_LINK_LENGTH+0x800ULL;
  if(!r || active!=&r->Scope || !encoder_root_valid(r) || !bo ||
     !AgxWin32AsahiIdentity(b,bo,&identity) || identity.Token!=r->Identity.Token ||
     identity.Serial!=r->Identity.Serial || identity.Owner!=r->Identity.Owner ||
     identity.Generation!=r->Identity.Generation ||
     (uintptr_t)r->Scope.Cpu>UINTPTR_MAX-r->Scope.Capacity ||
     (uintptr_t)end!=(uintptr_t)r->Scope.Cpu+r->Scope.Capacity ||
     (uintptr_t)current<(uintptr_t)r->Scope.Cpu ||
     (uintptr_t)current>(uintptr_t)end ||
     (uintptr_t)current-(uintptr_t)r->Scope.Cpu<r->CompletedEnd ||
     bytes>UINT64_MAX-tail || bytes+tail>(uintptr_t)end-(uintptr_t)current) {
    b->Failed=1; return 0;
  }
  return 1;
}
int AgxWin32AsahiEncoderEmissionBeginCpu(struct agx_device *native,void *cpu,
    APPLE_AGX_U32 capacity,AGX_WIN32_ASAHI_PIPELINE *s) {
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  AGX_WIN32_ASAHI_PIPELINE *active=b?b->ActiveEmission:NULL;
  AGX_WIN32_ASAHI_ENCODER_ROOT *r=NULL;
  for(AGX_WIN32_ASAHI_PIPELINE *p=active;p;p=p->PreviousEmission) {
    if(p==s) return 0;
    if(p->Root) r=p->Root;
  }
  AGX_WIN32_ASAHI_CAPTURE *capture=b?b->ActiveCapture:NULL;
  if(!r && capture && capture->EncoderRoot) return 0;
  if(!r) return AgxWin32AsahiEmissionBeginCpu(native,cpu,capacity,
      AppleAgxWin32RoleEncoder,s);
  if(!s || !cpu || !capacity || active!=&r->Scope || !encoder_root_valid(r) ||
     (uintptr_t)cpu<(uintptr_t)r->Scope.Cpu) return 0;
  APPLE_AGX_U64 offset=(uintptr_t)cpu-(uintptr_t)r->Scope.Cpu;
  if(offset<r->CompletedEnd || offset>r->Scope.Capacity ||
     capacity>r->Scope.Capacity-offset) return 0;
  memset(s,0,sizeof(*s));
  s->Capture=r->Scope.Capture; s->Cpu=cpu; s->Capacity=capacity;
  s->Reference=r->Scope.Reference; s->Role=AppleAgxWin32RoleEncoder;
  s->PreviousEmission=active; s->Root=r; s->RootOffset=(APPLE_AGX_U32)offset;
  b->ActiveEmission=s; return 1;
}
static int emission_begin(struct agx_device *native,void *cpu,
    APPLE_AGX_U64 address,APPLE_AGX_U32 capacity,APPLE_AGX_U32 role,
    AGX_WIN32_ASAHI_PIPELINE *s,int root_begin) {
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  AGX_WIN32_ASAHI_CAPTURE *c=b?b->ActiveCapture:NULL;
  struct agx_bo *bo=NULL; APPLE_AGX_U64 offset=0;
  APPLE_AGX_U32 reference=0;
  if(!root_begin && c && (c->EncoderRoot || c->ComputeEncoderRoot) &&
     role==AppleAgxWin32RoleEncoder) return 0;
  for(AGX_WIN32_ASAHI_PIPELINE *p=b?b->ActiveEmission:NULL;p;p=p->PreviousEmission)
    if(p==s) return 0;
  if(!b || !c || !s || !cpu || !capacity || !role ||
     c->Capture.State!=1u || !AgxWin32AsahiFindAddress(b,c->Capture.Owner,
        c->Capture.Generation,address,capacity,&bo,&offset) || !bo->_map ||
     (role==AppleAgxWin32RoleUscPipeline &&
       (!(bo->flags&AGX_BO_LOW_VA) || (bo->flags&AGX_BO_EXEC))) ||
     (b->ActiveEmission &&
       ((AGX_WIN32_ASAHI_PIPELINE *)b->ActiveEmission)->Capture!=c) ||
     (uintptr_t)bo->_map>UINTPTR_MAX-offset ||
     (uintptr_t)cpu!=(uintptr_t)bo->_map+offset) return 0;
  APPLE_AGX_U32 classId=0;
  if(role==AppleAgxWin32RoleEncoder &&
     (!AgxWin32AsahiClass(b,bo,&classId) || classId!=AgxWin32BufferClassEncoder))
    return 0;
  if(role==AppleAgxWin32RolePppState &&
     (!AgxWin32AsahiClass(b,bo,&classId) || classId!=AgxWin32BufferClassGeneral ||
      ((offset|capacity)&3ULL))) return 0;
  unsigned previous=c->Capture.ReferenceCount;
  if(AgxWin32AsahiCaptureReference(c,bo,role,
      role==AppleAgxWin32RoleSharedGeometry ?
        AppleAgxWin32AccessRead|AppleAgxWin32AccessWrite :
        AppleAgxWin32AccessRead,
      offset,capacity,&reference)!=AgxRelocOk) return 0;
  if(reference!=previous) { (void)AgxWin32RelocAbort(&c->Capture); return 0; }
  memset(s,0,sizeof(*s)); s->Capture=c; s->Cpu=cpu;
  s->Capacity=capacity; s->Reference=reference; s->Role=role;
  s->PreviousEmission=b->ActiveEmission;
  b->ActiveEmission=s; return 1;
}
int AgxWin32AsahiEmissionBegin(struct agx_device *native,void *cpu,
    APPLE_AGX_U64 address,APPLE_AGX_U32 capacity,APPLE_AGX_U32 role,
    AGX_WIN32_ASAHI_PIPELINE *s) {
  return emission_begin(native,cpu,address,capacity,role,s,0);
}
int AgxWin32AsahiEmissionBeginCpu(struct agx_device *native,void *cpu,
    APPLE_AGX_U32 capacity,APPLE_AGX_U32 role,AGX_WIN32_ASAHI_PIPELINE *s) {
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  AGX_WIN32_ASAHI_CAPTURE *c=b?b->ActiveCapture:NULL;
  struct agx_bo *bo=NULL; APPLE_AGX_U64 address=0,offset=0;
  if(!b || !c || !cpu || !capacity || !role ||
     !AgxWin32AsahiFindCpuAddress(b,c->Capture.Owner,c->Capture.Generation,
       cpu,capacity,&bo,&address,&offset)) return 0;
  return AgxWin32AsahiEmissionBegin(native,cpu,address,capacity,role,s);
}
int AgxWin32AsahiPipelineBegin(struct agx_device *native,void *cpu,
    APPLE_AGX_U64 address,APPLE_AGX_U32 capacity,AGX_WIN32_ASAHI_PIPELINE *s) {
  if(!AgxWin32AsahiEmissionBegin(native,cpu,address,capacity,
                                  AppleAgxWin32RoleUscPipeline,s)) return 0;
  /* Only the USC producer owns its initial content; state/VDM intervals do not
   * pre-clear native command bytes. */
  memset(cpu,0,capacity);
  return 1;
}
static APPLE_AGX_U64 read_le(const unsigned char *p,unsigned bytes) {
  APPLE_AGX_U64 value=0;
  for(unsigned i=0;i<bytes;++i) value|=(APPLE_AGX_U64)p[i]<<(8*i);
  return value;
}
static void pipeline_record(AGX_WIN32_ASAHI_PIPELINE *s,const void *end,
    APPLE_AGX_U32 kind,APPLE_AGX_U64 address,APPLE_AGX_U64 bytes,
    APPLE_AGX_U32 role,int existing) {
  APPLE_AGX_U32 width=kind==AppleAgxWin32RelocationUscShaderOffset32?6:
      (kind==AppleAgxWin32RelocationVdmPipelineOffset32 ||
       kind==AppleAgxWin32RelocationPppPipelineOffset32 ||
       kind==AppleAgxWin32RelocationPppCfBindingsOffset32)?4:8,index;
  if(!s || !s->Capture || s->Failed) return;
  AGX_WIN32_ASAHI_CAPTURE *c=s->Capture;
  if(c->Backend->ActiveEmission!=s || c->Backend->ActiveCapture!=c ||
     (s->Root && !encoder_root_valid(s->Root)) ||
     c->Capture.State!=1u || (uintptr_t)end<(uintptr_t)s->Cpu ||
     (uintptr_t)end-(uintptr_t)s->Cpu<width ||
     (uintptr_t)end-(uintptr_t)s->Cpu>s->Capacity) { s->Failed=1; return; }
  APPLE_AGX_U64 offset=(uintptr_t)end-(uintptr_t)s->Cpu-width;
  const unsigned char *record=s->Cpu+offset;
  APPLE_AGX_U64 raw=read_le(record,width),encoded=0;
  int valid=0;
  switch(kind) {
  case AppleAgxWin32RelocationUscShaderOffset32:
    encoded=(raw>>16)+c->Backend->Native->shader_base;
    valid=record[0]==0x0d && role==AppleAgxWin32RoleShader; break;
  case AppleAgxWin32RelocationUscPreshaderOffset32:
    encoded=(raw>>32)+c->Backend->Native->shader_base;
    valid=(raw&0xffffffffULL)==0xc0800038ULL && role==AppleAgxWin32RoleShader; break;
  case AppleAgxWin32RelocationUscBufferAddress40:
    encoded=(raw>>26)<<2;
    valid=(record[0]==0x1d || record[0]==0x3d) &&
      (role==AppleAgxWin32RoleConstant || role==AppleAgxWin32RoleDescriptor ||
       role==AppleAgxWin32RoleShaderRodata || role==AppleAgxWin32RoleUniform); break;
  case AppleAgxWin32RelocationUscTableAddress39:
    encoded=((raw>>27)&0xfffffffffULL)<<3;
    valid=(record[0]==0xdd || record[0]==0x9d) && role==AppleAgxWin32RoleDescriptor; break;
  case AppleAgxWin32RelocationVdmPipelineOffset32:
  case AppleAgxWin32RelocationPppPipelineOffset32:
    encoded=(raw&~0x3fULL)+c->Backend->Native->shader_base;
    valid=role==AppleAgxWin32RoleUscPipeline &&
      s->Role==(kind==AppleAgxWin32RelocationVdmPipelineOffset32?
        AppleAgxWin32RoleEncoder:AppleAgxWin32RolePppState); break;
  case AppleAgxWin32RelocationPppCfBindingsOffset32:
    encoded=(raw&~3ULL)+c->Backend->Native->shader_base;
    valid=role==AppleAgxWin32RoleDescriptor && s->Role==AppleAgxWin32RolePppState; break;
  case AppleAgxWin32RelocationPppStateAddress40:
    encoded=((raw&0xffULL)<<32)|(raw>>32);
    valid=role==AppleAgxWin32RolePppState && s->Role==AppleAgxWin32RoleEncoder &&
      ((raw>>29)&7ULL)==AGX_VDM_BLOCK_TYPE_PPP_STATE_UPDATE &&
      ((raw>>8)&0xffULL)*4==bytes; break;
  case AppleAgxWin32RelocationVdmIndexBufferAddress40:
    encoded=((raw&0xffULL)<<32)|(raw>>32);
    valid=APPLE_AGX_WIN32_COMMAND_HAS_INDEX(c->Capture.CommandVersion) &&
      role==(c->Capture.CommandVersion==APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH ?
        AppleAgxWin32RoleSharedGeometry : AppleAgxWin32RoleIndex) &&
      s->Role==AppleAgxWin32RoleEncoder &&
      (raw&0xffffff00ULL)==
        (c->Capture.CommandVersion==APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH ?
          0x61f50900ULL : 0x61f20600ULL) &&
      bytes==(c->Capture.CommandVersion==APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH ?
        16u : 8u); break;
  case AppleAgxWin32RelocationUniformAddress64:
    encoded=raw;
    valid=s->Role==AppleAgxWin32RoleUniform ||
      (c->Capture.CommandVersion==APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH &&
       s->Role==AppleAgxWin32RoleSharedGeometry); break;
  case AppleAgxWin32RelocationTextureAddress40:
    encoded=((raw>>2)&0xfffffffffULL)<<4;
    valid=s->Role==AppleAgxWin32RoleDescriptor &&
      (role==AppleAgxWin32RoleRenderTarget || role==AppleAgxWin32RoleTexture);
    break;
  case AppleAgxWin32RelocationPbeAddress40:
    encoded=(raw&0xfffffffffULL)<<4;
    valid=s->Role==AppleAgxWin32RoleDescriptor && role==AppleAgxWin32RoleRenderTarget; break;
  default: break;
  }
  if(!valid || encoded!=address) { fprintf(stderr,"NATIVE_EDGE_FAILURE: kind=%u valid=%u encoded=%llu address=%llu\n",kind,valid,(unsigned long long)encoded,(unsigned long long)address); s->Failed=1; return; }
  APPLE_AGX_U64 targetOffset=0;
  int target=existing ? AgxWin32AsahiCaptureFind(c,address,bytes,role,&index,&targetOffset) :
      AgxWin32AsahiCaptureAddress(c,address,bytes,role,
        role==AppleAgxWin32RoleShader?AppleAgxWin32AccessRead|AppleAgxWin32AccessExecute:
        (role==AppleAgxWin32RoleRenderTarget?AppleAgxWin32AccessRead|AppleAgxWin32AccessWrite:
         AppleAgxWin32AccessRead),&index)==AgxRelocOk;
  AGX_WIN32_RELOC_RESULT result=target ?
      AgxWin32RelocField(&c->Capture,kind,s->Reference,offset+s->RootOffset,index,targetOffset) : AgxRelocArgument;
  if(!target || result!=AgxRelocOk) {
    fprintf(stderr,"NATIVE_FIELD_FAILURE: kind=%u target=%u result=%u ref=%u role=%u bytes=%llu\n",
        kind,target,(unsigned)result,s->Reference,role,(unsigned long long)bytes);
    s->Failed=1;
  }
}
void AgxWin32AsahiPipelineRecord(AGX_WIN32_ASAHI_PIPELINE *s,const void *end,
    APPLE_AGX_U32 kind,APPLE_AGX_U64 address,APPLE_AGX_U64 bytes,APPLE_AGX_U32 role) {
  pipeline_record(s,end,kind,address,bytes,role,0);
}
void AgxWin32AsahiPipelineRecordRange(AGX_WIN32_ASAHI_PIPELINE *s,const void *end,
    APPLE_AGX_U32 kind,APPLE_AGX_U64 address,APPLE_AGX_U64 bytes,APPLE_AGX_U32 role) {
  pipeline_record(s,end,kind,address,bytes,role,1);
}
void AgxWin32AsahiPipelineRecordCaptured(AGX_WIN32_ASAHI_PIPELINE *s,
    const void *end,APPLE_AGX_U32 kind,APPLE_AGX_U64 address,APPLE_AGX_U32 role) {
  struct agx_bo *bo=NULL;
  APPLE_AGX_U64 offset=0;
  AGX_WIN32_RELOC_ALLOCATION identity;
  if(!s || !s->Capture || s->Failed) return;
  AGX_WIN32_ASAHI_CAPTURE *c=s->Capture;
  if(c->Backend->ActiveEmission!=s || c->Backend->ActiveCapture!=c ||
     c->Capture.State!=1u ||
     (role!=AppleAgxWin32RoleUscPipeline && role!=AppleAgxWin32RolePppState) ||
     !AgxWin32AsahiFindAddress(c->Backend,c->Capture.Owner,c->Capture.Generation,
       address,1,&bo,&offset) || !AgxWin32AsahiIdentity(c->Backend,bo,&identity)) {
    s->Failed=1; return;
  }
  for(unsigned i=0;i<c->Capture.ReferenceCount;++i) {
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *r=&c->Capture.References[i];
    const AGX_WIN32_RELOC_ALLOCATION *a=&c->Capture.Allocations[i];
    if(r->Role==role && r->Offset==offset && r->Access==AppleAgxWin32AccessRead &&
       a->Token==identity.Token && a->Serial==identity.Serial &&
       a->Owner==identity.Owner && a->Generation==identity.Generation) {
      /* Only completed child intervals qualify. An ancestor's provisional
       * capacity is not an exact source span, even if it has the same role. */
      for(AGX_WIN32_ASAHI_PIPELINE *active=s;active;
          active=(AGX_WIN32_ASAHI_PIPELINE *)active->PreviousEmission) {
        if(active->Capture==c && active->Reference==i) { s->Failed=1; return; }
      }
      AgxWin32AsahiPipelineRecord(s,end,kind,address,r->Bytes,role);
      return;
    }
  }
  s->Failed=1;
}
int AgxWin32AsahiPipelineFinish(AGX_WIN32_ASAHI_PIPELINE *s,const void *end) {
  if(!s || !s->Capture || s->Capture->Backend->ActiveEmission!=s) return 0;
  if(s->Root && s==&s->Root->Scope) return 0;
  AGX_WIN32_ASAHI_CAPTURE *c=s->Capture;
  APPLE_AGX_U64 length=(uintptr_t)end-(uintptr_t)s->Cpu;
  if((s->Root && !encoder_root_valid(s->Root)) ||
     (uintptr_t)end<(uintptr_t)s->Cpu || !length || length>s->Capacity ||
     c->Capture.State!=1u) s->Failed=1;
  for(unsigned i=0;i<c->Capture.RelocationCount;++i) {
    const APPLE_AGX_WIN32_RELOCATION *r=&c->Capture.Relocations[i];
    if(r->DestinationReference==s->Reference &&
       r->DestinationOffset>=s->RootOffset &&
       (r->DestinationOffset-s->RootOffset>length ||
        r->WidthBytes>length-(r->DestinationOffset-s->RootOffset))) {
      s->Failed=1;
    }
  }
  c->Backend->ActiveEmission=s->PreviousEmission;
  if(s->Failed) { fprintf(stderr,"NATIVE_SCOPE_FAILURE: ref=%u length=%llu capacity=%u state=%u refs=%u relocs=%u\n",s->Reference,(unsigned long long)length,s->Capacity,c->Capture.State,c->Capture.ReferenceCount,c->Capture.RelocationCount); (void)AgxWin32RelocAbort(&c->Capture); s->Capture=NULL; return 0; }
  if(s->Root) s->Root->CompletedEnd=s->RootOffset+(APPLE_AGX_U32)length;
  else c->Capture.References[s->Reference].Bytes=length;
  s->Capture=NULL; return 1;
}
