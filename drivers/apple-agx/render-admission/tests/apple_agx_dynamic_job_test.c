#include "apple_agx_dynamic_job.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct _DYNAMIC_FIXTURE {
  unsigned char Data[9][0x10000];
  unsigned Reads;
  unsigned Resolves;
  unsigned FailRead;
  unsigned FailResolve;
  APPLE_AGX_U64 IndexPlacement;
} DYNAMIC_FIXTURE;
static DYNAMIC_FIXTURE fixture;

static int read_object(void *Context, APPLE_AGX_U64 Token,
                       APPLE_AGX_U32 ReferenceIndex,
                       APPLE_AGX_U32 Role, APPLE_AGX_U64 Offset,
                       APPLE_AGX_U32 Bytes, void *Destination) {
  DYNAMIC_FIXTURE *fixture = Context;
  unsigned index = (unsigned)(Token - 1ULL);
  ++fixture->Reads;
  if (fixture->FailRead || index >= 9u || ReferenceIndex >= 9u ||
      Role < AppleAgxWin32RoleRenderTarget ||
      Role > AppleAgxWin32RoleDepthBias || Offset > 0x10000ULL ||
      Bytes > 0x10000ULL - Offset)
    return 0;
  memcpy(Destination, fixture->Data[index] + (size_t)Offset, Bytes);
  return 1;
}

static int resolve_object(void *Context, APPLE_AGX_U64 Token,
                          APPLE_AGX_U32 ClassId,
                          APPLE_AGX_U32 ReferenceIndex,
                          APPLE_AGX_U32 Role, APPLE_AGX_U64 Offset,
                          APPLE_AGX_U32 Bytes,
                          APPLE_AGX_U64 *GpuVirtualAddress) {
  DYNAMIC_FIXTURE *fixture = Context;
  ++fixture->Resolves;
  if (fixture->FailResolve || Token == 0ULL || ClassId > 3u ||
      ReferenceIndex >= 9u || Role < AppleAgxWin32RoleRenderTarget ||
      Role > AppleAgxWin32RoleDepthBias ||
      Offset >= 0x10000ULL || Bytes != 1u)
    return 0;
  *GpuVirtualAddress = Token == 9ULL
                           ? (fixture->IndexPlacement != 0ULL
                                  ? fixture->IndexPlacement
                                  : 0x1500000000ULL) + Offset
                           : 0x10000000ULL + Token * 0x10000ULL + Offset;
  return 1;
}

static APPLE_AGX_U64 read_le(const void *Data, unsigned Bytes) {
  const unsigned char *data = Data;
  APPLE_AGX_U64 value = 0ULL;
  unsigned index;
  for (index = 0u; index < Bytes; ++index)
    value |= (APPLE_AGX_U64)data[index] << (index * 8u);
  return value;
}

static void make_view(APPLE_AGX_WIN32_COMMAND_VIEW *View,
                      APPLE_AGX_WIN32_COMMAND_HEADER *Header,
                      APPLE_AGX_WIN32_ALLOCATION_REFERENCE References[9],
                      APPLE_AGX_WIN32_DRAW_PAYLOAD *Draw,
                      APPLE_AGX_WIN32_RELOCATION Relocations[7],
                      ADMISSION_WIN32_ALLOCATION_FACT Facts[9]) {
  unsigned index;
  memset(Header, 0, sizeof(*Header));
  memset(References, 0, 9u * sizeof(*References));
  memset(Draw, 0, sizeof(*Draw));
  memset(Relocations, 0, 7u * sizeof(*Relocations));
  memset(Facts, 0, 9u * sizeof(*Facts));
  memset(View, 0, sizeof(*View));
  Header->Opcode = AppleAgxWin32OpcodeDraw;
  Header->Generation = 7u;
  Header->ReferenceCount = 9u;
  Draw->RelocationCount = 7u;
  View->Header = Header;
  View->References = References;
  View->Draw = Draw;
  View->Relocations = Relocations;
  for (index = 0u; index < 9u; ++index) {
    References[index].AllocationIndex = index;
    References[index].Access = AppleAgxWin32AccessRead;
    References[index].Bytes = index == 8u ? 0x8000ULL : 0x4000ULL;
    Facts[index].AllocationToken = index + 1ULL;
    Facts[index].Bytes = 0x10000ULL;
    Facts[index].ClassId = index >= 4u ? AgxWin32BufferClassEncoder
                                      : AgxWin32BufferClassGeneral;
    Facts[index].Flags =
        AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead;
  }
  References[0].Role = AppleAgxWin32RoleRenderTarget;
  References[1].Role = AppleAgxWin32RoleVertex;
  References[2].Role = AppleAgxWin32RoleShader;
  References[3].Role = AppleAgxWin32RoleShader;
  References[4].Role = AppleAgxWin32RoleUscPipeline;
  References[5].Role = AppleAgxWin32RoleDescriptor;
  References[6].Role = AppleAgxWin32RoleScissor;
  References[7].Role = AppleAgxWin32RoleDepthBias;
  References[8].Role = AppleAgxWin32RoleEncoder;
  Facts[0].ClassId = 0u;
  Facts[2].ClassId = Facts[3].ClassId = AgxWin32BufferClassShader;
  for (index = 0u; index < 7u; ++index) {
    Relocations[index].WidthBytes = 8u;
    Relocations[index].DestinationOffset = index * 8u;
  }
  Relocations[0] = (APPLE_AGX_WIN32_RELOCATION){
      AppleAgxWin32RelocationEncoderAddress, 8u, 0u, 8u, 0u, 0u, 0u, 0u};
  Relocations[1] = (APPLE_AGX_WIN32_RELOCATION){
      AppleAgxWin32RelocationEncoderAddress, 8u, 0u, 8u, 1u, 8u, 0u, 0u};
  Relocations[2] = (APPLE_AGX_WIN32_RELOCATION){
      AppleAgxWin32RelocationVdmPipelineOffset32, 4u, 0u,
      8u, 4u, 16u, 0u, 0u};
  Relocations[3] = (APPLE_AGX_WIN32_RELOCATION){
      AppleAgxWin32RelocationUscShaderOffset32, 6u, 0u,
      4u, 2u, 0u, 0u, 0u};
  Relocations[4] = (APPLE_AGX_WIN32_RELOCATION){
      AppleAgxWin32RelocationUscShaderOffset32, 6u, 0u,
      4u, 3u, 8u, 0u, 0u};
  Relocations[5] = (APPLE_AGX_WIN32_RELOCATION){
      AppleAgxWin32RelocationUscBufferAddress40, 8u, 0u,
      4u, 5u, 16u, 0u, 0u};
  Relocations[6] = (APPLE_AGX_WIN32_RELOCATION){
      AppleAgxWin32RelocationPppStateAddress40, 8u, 0u,
      8u, 8u, 24u, 0x100u, 0u};
}

static void indexed_list_patch(void) {
  APPLE_AGX_WIN32_COMMAND_VIEW view = {0};
  APPLE_AGX_WIN32_COMMAND_HEADER header = {0};
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE references[3] = {0};
  APPLE_AGX_WIN32_DRAW_PAYLOAD draw = {0};
  APPLE_AGX_WIN32_RELOCATION relocation = {0};
  ADMISSION_WIN32_ALLOCATION_FACT facts[3] = {0};
  unsigned char storage[64];
  APPLE_AGX_DYNAMIC_JOB job;
  static const unsigned char indexBytes[8] = {0, 0, 1, 0, 2, 0, 0, 0};
  static const APPLE_AGX_U64 placements[2] = {
      0x1500000000ULL, 0x1600004000ULL};
  header.Version = 5u;
  header.Opcode = AppleAgxWin32OpcodeDraw;
  header.Generation = 7u;
  header.ReferenceCount = 3u;
  references[0] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
      0u, AppleAgxWin32AccessRead, AppleAgxWin32RoleEncoder, 0u, 0u, 24u};
  references[1] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
      1u, AppleAgxWin32AccessRead, AppleAgxWin32RoleIndex, 0u, 0u, 8u};
  references[2] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
      2u, AppleAgxWin32AccessRead, AppleAgxWin32RoleConstant, 0u, 0u, 16u};
  draw.IndexReference = 1u;
  draw.ConstantReference = 2u;
  draw.EncoderReference = 0u;
  draw.RelocationCount = 1u;
  relocation = (APPLE_AGX_WIN32_RELOCATION){
      15u, 8u, 0u, 0u, 1u, 0u, 0u, 0u};
  facts[0].AllocationToken = 1u;
  facts[0].Bytes = 0x10000u;
  facts[0].ClassId = AgxWin32BufferClassEncoder;
  facts[1].AllocationToken = 9u;
  facts[1].Bytes = 0x10000u;
  facts[1].ClassId = AgxWin32BufferClassGeneral;
  facts[2].AllocationToken = 3u;
  facts[2].Bytes = 0x10000u;
  facts[2].ClassId = AgxWin32BufferClassGeneral;
  view.Header = &header;
  view.References = references;
  view.Draw = &draw;
  view.Relocations = &relocation;
  memcpy(fixture.Data[0],
      (const unsigned char[]){0x00,0x06,0xf2,0x61, 0,0,0,0,
                              3,0,0,0, 1,0,0,0,
                              0,0,0,0, 2,0,0,0},24u);
  memcpy(fixture.Data[8],indexBytes,sizeof(indexBytes));
  memset(fixture.Data[2],0x3c,16u);
  for(unsigned pass=0;pass<2u;++pass) {
    fixture.IndexPlacement=placements[pass];
    memset(storage,0xa5,sizeof(storage));
    memset(&job,0xa5,sizeof(job));
    APPLE_AGX_DYNAMIC_JOB_RESULT result=AppleAgxDynamicJobMaterialize(
        &view,facts,3u,0x10000000ULL,read_object,resolve_object,&fixture,
        storage,sizeof(storage),&job);
    fprintf(stderr,"INDEX_V5_MATERIALIZE: pass=%u result=%u objects=%u relocations=%u\n",
        pass,(unsigned)result,job.ObjectCount,job.RelocationCount);
    assert(result==AppleAgxDynamicJobSuccess);
    assert(job.ObjectCount==3u && job.RelocationCount==1u &&
           job.Objects[0].ReferenceIndex==0u && job.Objects[0].Bytes==24u &&
           job.Objects[1].ReferenceIndex==1u && job.Objects[1].Bytes==8u &&
           job.Objects[2].ReferenceIndex==2u &&
           job.Objects[2].Role==AppleAgxWin32RoleConstant &&
           job.Objects[2].Bytes==16u);
    assert(read_le(storage,4u)==(0x61f20600u | (placements[pass]>>32u)) &&
           read_le(storage+4u,4u)==(placements[pass]&0xffffffffu) &&
           read_le(storage+8u,4u)==3u && read_le(storage+12u,4u)==1u &&
           read_le(storage+16u,4u)==0u && read_le(storage+20u,4u)==2u);
    assert(!memcmp(storage+job.Objects[1].StorageOffset,indexBytes,8u));
    assert(!memcmp(storage+job.Objects[2].StorageOffset,fixture.Data[2],16u));
    assert(!memcmp(fixture.Data[8],indexBytes,8u));
  }
  header.Version=4u;
  memset(storage,0xa5,sizeof(storage));
  assert(AppleAgxDynamicJobMaterialize(&view,facts,3u,0x10000000ULL,
      read_object,resolve_object,&fixture,storage,sizeof(storage),&job)==
      AppleAgxDynamicJobRelocation);
  header.Version=5u;
  fixture.Data[0][3]^=0x20u;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,3u,0x10000000ULL,
      read_object,resolve_object,&fixture,storage,sizeof(storage),&job)==
      AppleAgxDynamicJobRelocation);
  fixture.Data[0][3]^=0x20u;
  fixture.Data[0][8]=4u;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,3u,0x10000000ULL,
      read_object,resolve_object,&fixture,storage,sizeof(storage),&job)==
      AppleAgxDynamicJobRelocation);
  fixture.Data[0][8]=3u;
  fixture.Data[8][6]=1u;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,3u,0x10000000ULL,
      read_object,resolve_object,&fixture,storage,sizeof(storage),&job)==
      AppleAgxDynamicJobRelocation);
  fixture.Data[8][6]=0u;
  references[1].Bytes=7u;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,3u,0x10000000ULL,
      read_object,resolve_object,&fixture,storage,sizeof(storage),&job)==
      AppleAgxDynamicJobRelocation);
  references[1].Bytes=8u;
  references[1].Role=AppleAgxWin32RoleVertex;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,3u,0x10000000ULL,
      read_object,resolve_object,&fixture,storage,sizeof(storage),&job)==
      AppleAgxDynamicJobRelocation);
  references[1].Role=AppleAgxWin32RoleIndex;
  references[0].Bytes=23u;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,3u,0x10000000ULL,
      read_object,resolve_object,&fixture,storage,sizeof(storage),&job)==
      AppleAgxDynamicJobRelocation);
  references[0].Bytes=24u;
  relocation.TargetOffset=1u;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,3u,0x10000000ULL,
      read_object,resolve_object,&fixture,storage,sizeof(storage),&job)==
      AppleAgxDynamicJobRelocation);
  relocation.TargetOffset=0u;
  fixture.IndexPlacement=0x1500000002ULL;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,3u,0x10000000ULL,
      read_object,resolve_object,&fixture,storage,sizeof(storage),&job)==
      AppleAgxDynamicJobRelocation);
  fixture.IndexPlacement=1ULL<<40u;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,3u,0x10000000ULL,
      read_object,resolve_object,&fixture,storage,sizeof(storage),&job)==
      AppleAgxDynamicJobResolve);
  fixture.IndexPlacement=placements[0];
  APPLE_AGX_WIN32_RELOCATION duplicate[2]={relocation,relocation};
  view.Relocations=duplicate;
  draw.RelocationCount=2u;
  assert(AppleAgxDynamicJobMaterialize(&view,facts,3u,0x10000000ULL,
      read_object,resolve_object,&fixture,storage,sizeof(storage),&job)==
      AppleAgxDynamicJobRelocation);
  view.Relocations=&relocation;
  draw.RelocationCount=1u;
  fixture.IndexPlacement=0;
  memset(fixture.Data[2],0x12,sizeof(fixture.Data[2]));
  fixture.Reads=0;
  fixture.Resolves=0;
}

int main(void) {
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  APPLE_AGX_WIN32_COMMAND_HEADER header;
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE references[9];
  APPLE_AGX_WIN32_DRAW_PAYLOAD draw;
  APPLE_AGX_WIN32_RELOCATION relocations[7];
  ADMISSION_WIN32_ALLOCATION_FACT facts[9];
  unsigned char storage[APPLE_AGX_DYNAMIC_JOB_MAX_STORAGE_BYTES];
  unsigned char copy[APPLE_AGX_DYNAMIC_JOB_MAX_STORAGE_BYTES];
  APPLE_AGX_DYNAMIC_JOB job;
  unsigned index;
  for (index = 0u; index < 9u; ++index)
    memset(fixture.Data[index], 0x10 + index, sizeof(fixture.Data[index]));
  indexed_list_patch();
  make_view(&view, &header, references, &draw, relocations, facts);
  memset(storage, 0xa5, sizeof(storage));
  assert(AppleAgxDynamicJobMaterialize(
      &view, facts, 9u, 0x10000000ULL, read_object, resolve_object, &fixture,
      storage, sizeof(storage), &job) == AppleAgxDynamicJobSuccess);
  assert(job.Magic == APPLE_AGX_DYNAMIC_JOB_MAGIC && job.Generation == 7u);
  assert(job.ObjectCount == 8u && job.RelocationCount == 7u);
  assert(fixture.Reads == 8u && fixture.Resolves == 7u);
  assert(job.Objects[0].ReferenceIndex == 1u);
  assert(job.Objects[1].ReferenceIndex == 2u);
  assert(job.Objects[2].ReferenceIndex == 3u);
  assert(job.Objects[3].ReferenceIndex == 4u);
  assert(job.Objects[4].ReferenceIndex == 5u);
  assert(job.Objects[5].ReferenceIndex == 6u);
  assert(job.Objects[6].ReferenceIndex == 7u);
  assert(job.Objects[7].ReferenceIndex == 8u);
  assert(storage[job.Objects[0].StorageOffset] == 0x11u);
  assert(storage[job.Objects[1].StorageOffset] == 0x12u);
  assert(storage[job.Objects[2].StorageOffset] == 0x13u);
  assert(read_le(storage + job.Objects[7].StorageOffset, 8u) ==
         0x10010000ULL);
  assert((read_le(storage + job.Objects[7].StorageOffset + 16u, 4u) &
          ~0x3fULL) == 0x50000ULL);
  assert((read_le(storage + job.Objects[7].StorageOffset + 24u, 4u) &
          0xffULL) == 0x15ULL);
  assert((read_le(storage + job.Objects[7].StorageOffset + 24u, 4u) &
          ~0xffULL) == 0x18181800ULL);
  assert(read_le(storage + job.Objects[7].StorageOffset + 28u, 4u) ==
         0x100ULL);
  assert(job.Relocations[6].ResolvedAddress == 0x1500000100ULL);
  assert(job.Relocations[6].EncodedValue == 0x1500000100ULL);
  assert(read_le(storage + job.Objects[3].StorageOffset, 6u) ==
         ((0x30000ULL << 16u) | 0x1414ULL));
  assert(job.Relocations[2].EncodedValue == 0x50000ULL);
  assert(job.Relocations[3].ResolvedAddress == 0x10030000ULL);
  assert(job.Relocations[3].EncodedValue == 0x30000ULL);
  memcpy(copy, storage, job.StorageBytes);
  memset(fixture.Data[2], 0xdd, sizeof(fixture.Data[2]));
  memset(fixture.Data[3], 0xcc, sizeof(fixture.Data[3]));
  memset(fixture.Data[8], 0xff, sizeof(fixture.Data[8]));
  memset(fixture.Data[4], 0xee, sizeof(fixture.Data[4]));
  assert(memcmp(copy, storage, job.StorageBytes) == 0);

  make_view(&view, &header, references, &draw, relocations, facts);
  fixture.FailRead = 1u;
  memset(storage, 0xa5, sizeof(storage));
  memset(&job, 0xa5, sizeof(job));
  assert(AppleAgxDynamicJobMaterialize(
      &view, facts, 9u, 0x10000000ULL, read_object, resolve_object, &fixture,
      storage, sizeof(storage), &job) == AppleAgxDynamicJobRead);
  assert(job.Magic == 0u && storage[0] == 0u);
  fixture.FailRead = 0u;

  make_view(&view, &header, references, &draw, relocations, facts);
  fixture.FailResolve = 1u;
  memset(storage, 0xa5, sizeof(storage));
  assert(AppleAgxDynamicJobMaterialize(
      &view, facts, 9u, 0x10000000ULL, read_object, resolve_object, &fixture,
      storage, sizeof(storage), &job) == AppleAgxDynamicJobResolve);
  assert(job.Magic == 0u && storage[0] == 0u);
  fixture.FailResolve = 0u;

  make_view(&view, &header, references, &draw, relocations, facts);
  relocations[6].TargetOffset = 0x102u;
  memset(storage, 0xa5, sizeof(storage));
  assert(AppleAgxDynamicJobMaterialize(
      &view, facts, 9u, 0x10000000ULL, read_object, resolve_object, &fixture,
      storage, sizeof(storage), &job) == AppleAgxDynamicJobRelocation);
  assert(job.Magic == 0u && storage[0] == 0u);

  make_view(&view, &header, references, &draw, relocations, facts);
  references[8].Bytes = APPLE_AGX_DYNAMIC_JOB_MAX_OBJECT_BYTES + 1u;
  assert(AppleAgxDynamicJobMaterialize(
      &view, facts, 9u, 0x10000000ULL, read_object, resolve_object, &fixture,
      storage, sizeof(storage), &job) == AppleAgxDynamicJobRange);
  return 0;
}
