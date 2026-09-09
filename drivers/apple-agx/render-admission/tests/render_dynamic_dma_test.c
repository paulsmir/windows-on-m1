#include "render_dynamic_dma.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static void make_job(APPLE_AGX_DYNAMIC_JOB *job, unsigned char *storage,
                     APPLE_AGX_U32 storageBytes) {
  static const APPLE_AGX_U32 references[] = {1u, 2u, 3u, 9u, 10u,
                                              4u, 5u, 6u, 7u, 8u};
  static const APPLE_AGX_U32 roles[] = {
      AppleAgxWin32RoleVertex,       AppleAgxWin32RoleShader,
      AppleAgxWin32RoleShader,
      AppleAgxWin32RoleShaderRodata, AppleAgxWin32RoleShaderRodata,
      AppleAgxWin32RoleUscPipeline,  AppleAgxWin32RoleDescriptor,
      AppleAgxWin32RoleScissor,      AppleAgxWin32RoleDepthBias,
      AppleAgxWin32RoleEncoder};
  memset(job, 0, sizeof(*job));
  memset(storage, 0x5a, storageBytes);
  job->Magic = APPLE_AGX_DYNAMIC_JOB_MAGIC;
  job->Version = APPLE_AGX_DYNAMIC_JOB_VERSION;
  job->Generation = 7u;
  job->ObjectCount = 10u;
  job->RelocationCount = 7u;
  job->StorageBytes = storageBytes;
  job->MaterializedHash = AppleAgxDynamicDmaBytesHash(storage, storageBytes);
  for (APPLE_AGX_U32 index = 0u; index < job->ObjectCount; ++index) {
    job->Objects[index].ReferenceIndex = references[index];
    job->Objects[index].Role = roles[index];
    job->Objects[index].StorageOffset = index * 64u;
    job->Objects[index].Bytes = 64u;
  }
  for (APPLE_AGX_U32 index = 0u; index < job->RelocationCount; ++index) {
    job->Relocations[index].Kind = AppleAgxWin32RelocationEncoderAddress;
    job->Relocations[index].DestinationReference = 8u;
    job->Relocations[index].TargetReference = index == 0u ? 2u : 3u;
    job->Relocations[index].DestinationOffset = index * 8u;
    job->Relocations[index].ResolvedAddress = 0x1500000000ULL + index * 0x10u;
    job->Relocations[index].EncodedValue =
        job->Relocations[index].ResolvedAddress;
  }
}

int main(void) {
  unsigned char bytes[ADMISSION_DYNAMIC_DMA_MAX_BYTES];
  unsigned char storage[704];
  APPLE_AGX_DYNAMIC_JOB job;
  ADMISSION_DYNAMIC_OVERLAY_BINDINGS bindings = {
      1u, 2u, 3u, 9u, 10u, 4u, 5u, 6u, 7u, 8u};
  ADMISSION_DYNAMIC_DMA_VIEW view;
  ADMISSION_GDI_PREPARED prepared;
  APPLE_AGX_U32 total = 0u;

  make_job(&job, storage, sizeof(storage));
  memset(bytes, 0xa5, sizeof(bytes));
  assert(AdmissionDynamicDmaBuild(
             7u, 0x1122334455667788ULL, 0x1500120000ULL,
             0u, 0xff101820u, &bindings, &job, storage, sizeof(storage),
             bytes, sizeof(bytes), &total) == AdmissionDynamicDmaSuccess);
  assert(total <= 4096u && total > sizeof(job));
  assert(AdmissionDynamicDmaOpen(bytes, total, &view) ==
         AdmissionDynamicDmaSuccess);
  assert(view.Header->Generation == 7u);
  assert(view.Header->CommandHash == 0x1122334455667788ULL);
  assert(view.Header->DestinationGpuVa == 0x1500120000ULL);
  assert(view.Header->BackgroundColor == 0xff101820u);
  assert(view.Job->ObjectCount == 10u && view.Job->RelocationCount == 7u);
  assert(view.StorageBytes == sizeof(storage));
  assert(memcmp(view.Storage, storage, sizeof(storage)) == 0);
  assert(view.Bindings->VertexReference == 1u);
  assert(view.Bindings->EncoderReference == 8u);
  assert(AdmissionDynamicDmaDescribePreparedRecord(
      bytes, total, 32u, &prepared));
  assert(prepared.DmaOffset == 32u && prepared.DmaBytes == total);
  assert(prepared.Patches[0].AllocationIndex == 0u);
  assert(prepared.Patches[0].PatchOffset ==
         32u + offsetof(ADMISSION_DYNAMIC_DMA_HEADER, DestinationGpuVa));
  assert(AdmissionDynamicDmaDestinationPatchOffset() ==
         offsetof(ADMISSION_DYNAMIC_DMA_HEADER, DestinationGpuVa));
  assert(AdmissionDynamicDmaPatchDestination(
             bytes, total, 0x1500220000ULL) ==
         AdmissionDynamicDmaSuccess);
  assert(AdmissionDynamicDmaOpen(bytes, total, &view) ==
         AdmissionDynamicDmaSuccess);
  assert(view.Header->DestinationGpuVa == 0x1500220000ULL);

  bytes[total - 1u] ^= 1u;
  assert(AdmissionDynamicDmaOpen(bytes, total, &view) ==
         AdmissionDynamicDmaHash);
  bytes[total - 1u] ^= 1u;
  ((ADMISSION_DYNAMIC_DMA_HEADER *)bytes)->Reserved = 1u;
  ((ADMISSION_DYNAMIC_DMA_HEADER *)bytes)->ContentHash =
      AppleAgxDynamicDmaRecordHash(bytes, total);
  assert(AdmissionDynamicDmaOpen(bytes, total, &view) ==
         AdmissionDynamicDmaLayout);

  make_job(&job, storage, sizeof(storage));
  job.Objects[8].StorageOffset = sizeof(storage);
  assert(AdmissionDynamicDmaBuild(
             7u, 1ULL, 0x1500120000ULL, 0u, 0u, &bindings, &job, storage,
             sizeof(storage), bytes, sizeof(bytes), &total) ==
         AdmissionDynamicDmaJob);
  make_job(&job, storage, sizeof(storage));
  assert(AdmissionDynamicDmaBuild(
             7u, 1ULL, 0x1500120000ULL, 0u, 0u, &bindings, &job, storage,
             sizeof(storage), bytes,
             sizeof(ADMISSION_DYNAMIC_DMA_HEADER) + sizeof(job) +
                 sizeof(storage) - 1u,
             &total) ==
         AdmissionDynamicDmaCapacity);
  return 0;
}
