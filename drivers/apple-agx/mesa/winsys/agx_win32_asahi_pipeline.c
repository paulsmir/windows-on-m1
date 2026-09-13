#include "agx_device.h"
#include "agx_win32_asahi_pipeline.h"
#include <string.h>

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
int AgxWin32AsahiEmissionBegin(struct agx_device *native,void *cpu,
    APPLE_AGX_U64 address,APPLE_AGX_U32 capacity,APPLE_AGX_U32 role,
    AGX_WIN32_ASAHI_PIPELINE *s) {
  AGX_WIN32_ASAHI_BACKEND *b=native?native->windows_private:NULL;
  AGX_WIN32_ASAHI_CAPTURE *c=b?b->ActiveCapture:NULL;
  struct agx_bo *bo=NULL; APPLE_AGX_U64 offset=0;
  APPLE_AGX_U32 reference=0;
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
  unsigned previous=c->Capture.ReferenceCount;
  if(AgxWin32AsahiCaptureReference(c,bo,role,
      AppleAgxWin32AccessRead,offset,capacity,&reference)!=AgxRelocOk) return 0;
  if(reference!=previous) { (void)AgxWin32RelocAbort(&c->Capture); return 0; }
  memset(s,0,sizeof(*s)); s->Capture=c; s->Cpu=cpu;
  s->Capacity=capacity; s->Reference=reference; s->Role=role;
  s->PreviousEmission=b->ActiveEmission;
  b->ActiveEmission=s; return 1;
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
void AgxWin32AsahiPipelineRecord(AGX_WIN32_ASAHI_PIPELINE *s,const void *end,
    APPLE_AGX_U32 kind,APPLE_AGX_U64 address,APPLE_AGX_U64 bytes,APPLE_AGX_U32 role) {
  APPLE_AGX_U32 width=kind==AppleAgxWin32RelocationUscShaderOffset32?6:8,index;
  if(!s || !s->Capture || s->Failed) return;
  AGX_WIN32_ASAHI_CAPTURE *c=s->Capture;
  if(c->Backend->ActiveEmission!=s || c->Backend->ActiveCapture!=c ||
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
       role==AppleAgxWin32RoleShaderRodata); break;
  case AppleAgxWin32RelocationUscTableAddress39:
    encoded=((raw>>27)&0xfffffffffULL)<<3;
    valid=(record[0]==0xdd || record[0]==0x9d) && role==AppleAgxWin32RoleDescriptor; break;
  default: break;
  }
  if(!valid || encoded!=address || AgxWin32AsahiCaptureAddress(c,address,bytes,role,
      role==AppleAgxWin32RoleShader?AppleAgxWin32AccessRead|AppleAgxWin32AccessExecute:
      AppleAgxWin32AccessRead,&index)!=AgxRelocOk ||
     AgxWin32RelocField(&c->Capture,kind,s->Reference,offset,index,0)!=AgxRelocOk)
    s->Failed=1;
}
int AgxWin32AsahiPipelineFinish(AGX_WIN32_ASAHI_PIPELINE *s,const void *end) {
  if(!s || !s->Capture || s->Capture->Backend->ActiveEmission!=s) return 0;
  AGX_WIN32_ASAHI_CAPTURE *c=s->Capture;
  APPLE_AGX_U64 length=(uintptr_t)end-(uintptr_t)s->Cpu;
  if((uintptr_t)end<(uintptr_t)s->Cpu || !length || length>s->Capacity ||
     c->Capture.State!=1u) s->Failed=1;
  for(unsigned i=0;i<c->Capture.RelocationCount;++i) {
    const APPLE_AGX_WIN32_RELOCATION *r=&c->Capture.Relocations[i];
    if(r->DestinationReference==s->Reference &&
       (r->DestinationOffset>length || r->WidthBytes>length-r->DestinationOffset))
      s->Failed=1;
  }
  c->Backend->ActiveEmission=s->PreviousEmission;
  if(s->Failed) { (void)AgxWin32RelocAbort(&c->Capture); s->Capture=NULL; return 0; }
  c->Capture.References[s->Reference].Bytes=length;
  s->Capture=NULL; return 1;
}
