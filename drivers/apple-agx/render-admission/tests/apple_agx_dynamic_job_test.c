#include "apple_agx_dynamic_job.h"

#include <assert.h>
#include <string.h>

typedef struct _DYNAMIC_FIXTURE {
  unsigned char Data[9][0x10000];
  unsigned Reads;
  unsigned Resolves;
  unsigned FailRead;
  unsigned FailResolve;
} DYNAMIC_FIXTURE;

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
                           ? 0x1500000000ULL + Offset
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

int main(void) {
  static DYNAMIC_FIXTURE fixture;
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
