#include "render_backend_image.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TEST_BACKEND_BYTES 0x00800000ULL
#define TEST_BACKEND_GPU 0x1500800000ULL
#define TEST_BACKEND_PHYSICAL 0x9d0800000ULL

static unsigned long long read_u64(const unsigned char *bytes) {
  unsigned long long value = 0ULL;
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    value |= (unsigned long long)bytes[index] << (index * 8u);
  return value;
}

static APPLE_AGX_GDI_DMA_COMMAND exact_color_fill(
    unsigned long long destination_gpu) {
  APPLE_AGX_GDI_DMA_COMMAND command;
  memset(&command, 0, sizeof(command));
  command.Magic = APPLE_AGX_GDI_DMA_MAGIC;
  command.Version = APPLE_AGX_GDI_DMA_VERSION;
  command.RecordBytes = sizeof(command);
  command.Opcode = AppleAgxGdiColorFill;
  command.Destination.Right = APPLE_AGX_EXP208_GDI_WIDTH;
  command.Destination.Bottom = APPLE_AGX_EXP208_GDI_HEIGHT;
  command.DestinationGpuAddress = destination_gpu;
  command.DestinationPitch = APPLE_AGX_EXP208_GDI_PITCH;
  command.Color = APPLE_AGX_EXP208_GDI_COLOR;
  command.Rop = AppleAgxGdiColorFillPatCopy;
  return command;
}

static APPLE_AGX_GDI_DMA_COMMAND fullscreen_color_fill(
    unsigned long long destination_gpu) {
  APPLE_AGX_GDI_DMA_COMMAND command = exact_color_fill(destination_gpu);
  command.Destination.Right = APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH;
  command.Destination.Bottom = APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT;
  command.DestinationPitch = APPLE_AGX_EXP208_FRAMEBUFFER_PITCH;
  return command;
}

static void test_materializes_and_relocates_exact_rebased_image(void) {
  unsigned char *storage = (unsigned char *)malloc(TEST_BACKEND_BYTES);
  ADMISSION_LOCAL_MEMORY_VIEW view;
  ADMISSION_BACKEND_IMAGE image;
  const APPLE_AGX_EXP208_RELOCATION *relocations;
  const APPLE_AGX_EXP208_RELOCATION *first;

  assert(storage != NULL);
  memset(storage, 0xa5, TEST_BACKEND_BYTES);
  memset(&image, 0xa5, sizeof(image));
  view.CpuAddress = storage;
  view.HostPhysicalAddress = TEST_BACKEND_PHYSICAL;
  view.GpuVirtualAddress = TEST_BACKEND_GPU;
  view.Bytes = TEST_BACKEND_BYTES;

  assert(AdmissionBackendImagePrepare(&image, &view));
  assert(image.Ready == APPLE_AGX_TRUE);
  assert(image.ArenaCpuAddress == storage);
  assert(image.ArenaPhysicalAddress == TEST_BACKEND_PHYSICAL);
  assert(image.ArenaGpuAddress == TEST_BACKEND_GPU);
  assert(image.ArenaBytes == AppleAgxRenderTemplateBytes());
  assert(image.ArenaCapacity == TEST_BACKEND_BYTES);
  assert(image.Roots.Ta[0] == TEST_BACKEND_GPU + 0x80000ULL);
  assert(image.Roots.D3[0] == TEST_BACKEND_GPU + 0x70000ULL);
  assert(image.Objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX].GpuVa ==
         TEST_BACKEND_GPU);
  assert(image.Objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX]
             .PhysicalAddress == TEST_BACKEND_PHYSICAL);

  relocations = AppleAgxRenderTemplateRelocations();
  first = &relocations[0];
  assert(read_u64(image.Objects[first->SourceObject].Data +
                  first->SourceOffset) ==
         image.Objects[first->TargetObject].GpuVa + first->TargetOffset);
  assert(read_u64(image.Objects[first->SourceObject].Data +
                  first->SourceOffset) < 0x1501000000ULL);

  AdmissionBackendImageReset(&image);
  assert(image.Ready == APPLE_AGX_FALSE);
  assert(image.ArenaCpuAddress == NULL);
  free(storage);
}

static void test_rejects_invalid_tail_atomically(void) {
  unsigned char *storage = (unsigned char *)malloc(TEST_BACKEND_BYTES);
  ADMISSION_LOCAL_MEMORY_VIEW view;
  ADMISSION_BACKEND_IMAGE image;
  ADMISSION_BACKEND_IMAGE before;

  assert(storage != NULL);
  memset(&image, 0x5a, sizeof(image));
  before = image;
  view.CpuAddress = storage;
  view.HostPhysicalAddress = TEST_BACKEND_PHYSICAL;
  view.GpuVirtualAddress = TEST_BACKEND_GPU + 0x4000ULL;
  view.Bytes = AppleAgxRenderTemplateBytes();
  assert(!AdmissionBackendImagePrepare(&image, &view));
  assert(memcmp(&image, &before, sizeof(image)) == 0);
  free(storage);
}

static void test_exact_packet_binds_output_and_reapplies_relocations(void) {
  unsigned char *storage = (unsigned char *)malloc(TEST_BACKEND_BYTES);
  unsigned char *destination = (unsigned char *)malloc(0x4000u);
  ADMISSION_LOCAL_MEMORY_VIEW view;
  ADMISSION_BACKEND_IMAGE image;
  ADMISSION_RENDER_PACKET_DESCRIPTION packet;
  APPLE_AGX_GDI_DMA_COMMAND command;
  APPLE_AGX_EXP208_GDI_BINDING binding;
  const APPLE_AGX_EXP208_RELOCATION *relocations;
  unsigned int index;
  unsigned int output_edge = ~0u;

  assert(storage != NULL && destination != NULL);
  view.CpuAddress = storage;
  view.HostPhysicalAddress = TEST_BACKEND_PHYSICAL;
  view.GpuVirtualAddress = TEST_BACKEND_GPU;
  view.Bytes = TEST_BACKEND_BYTES;
  assert(AdmissionBackendImagePrepare(&image, &view));

  memset(&packet, 0, sizeof(packet));
  packet.Fence = 19u;
  packet.DestinationCpuToken =
      (unsigned long long)(unsigned long)destination;
  packet.DestinationGpuVa = 0x1500040000ULL;
  packet.DestinationPhysical = 0x9d0040000ULL;
  packet.DestinationBytes = 0x4000u;
  command = exact_color_fill(packet.DestinationGpuVa);
  assert(AdmissionBackendImageBindSubmission(
      &image, &packet, destination, (const unsigned char *)&command,
      sizeof(command), &binding));
  assert(image.BoundFence == packet.Fence);
  assert(binding.OutputObject == APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT);
  assert(image.Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Data ==
         destination);
  assert(image.Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].GpuVa ==
         packet.DestinationGpuVa);
  assert(image.Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].PhysicalAddress ==
         packet.DestinationPhysical);

  relocations = AppleAgxRenderTemplateRelocations();
  for (index = 0u; index < AppleAgxRenderTemplateRelocationCount(); ++index) {
    if (relocations[index].TargetObject ==
        APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT) {
      output_edge = index;
      break;
    }
  }
  assert(output_edge != ~0u);
  assert(read_u64(
             image.Objects[relocations[output_edge].SourceObject].Data +
             relocations[output_edge].SourceOffset) ==
         packet.DestinationGpuVa);
  {
    APPLE_AGX_BACKEND_JOB_IMAGE job;
    assert(!AdmissionBackendImageStageJob(
        &image, 18u, 1u, 2u, 2u, 2u, APPLE_AGX_TRUE, &job));
    assert(AdmissionBackendImageStageJob(
        &image, packet.Fence, 1u, 2u, 2u, 2u,
        APPLE_AGX_TRUE, &job));
    assert(image.JobReady == APPLE_AGX_TRUE);
    assert(image.JobFence == packet.Fence);
    assert(job.TaWorkAddresses[0] == image.Roots.Ta[0]);
    assert(job.TaWorkAddresses[1] == image.Roots.Ta[1]);
    assert(job.D3WorkAddresses[0] == image.Roots.D3[0]);
    assert(job.D3WorkAddresses[1] == image.Roots.D3[1]);
    assert(job.TaEvent == 1u);
    assert(job.D3Event == 2u);
    assert(job.TaExpectedStamp == 0x7a000100u);
    assert(job.D3ExpectedStamp == 0x3d000100u);
  }
  assert(!AdmissionBackendImageReleaseSubmission(&image, 18u));
  assert(AdmissionBackendImageReleaseSubmission(&image, packet.Fence));
  assert(image.BoundFence == 0u);
  free(destination);
  free(storage);
}

static void test_restart_queue_lifetime_resets_only_firmware_sequence(void) {
  unsigned char *storage = (unsigned char *)malloc(TEST_BACKEND_BYTES);
  unsigned char *destination = (unsigned char *)malloc(0x4000u);
  ADMISSION_LOCAL_MEMORY_VIEW view;
  ADMISSION_BACKEND_IMAGE image;
  ADMISSION_RENDER_PACKET_DESCRIPTION packet;
  APPLE_AGX_GDI_DMA_COMMAND command;
  APPLE_AGX_EXP208_GDI_BINDING binding;
  APPLE_AGX_BACKEND_JOB_IMAGE job;

  assert(storage != NULL && destination != NULL);
  view.CpuAddress = storage;
  view.HostPhysicalAddress = TEST_BACKEND_PHYSICAL;
  view.GpuVirtualAddress = TEST_BACKEND_GPU;
  view.Bytes = TEST_BACKEND_BYTES;
  assert(AdmissionBackendImagePrepare(&image, &view));

  memset(&packet, 0, sizeof(packet));
  packet.Fence = 91u;
  packet.DestinationCpuToken =
      (unsigned long long)(unsigned long)destination;
  packet.DestinationGpuVa = 0x1500040000ULL;
  packet.DestinationPhysical = 0x9d0040000ULL;
  packet.DestinationBytes = 0x4000u;
  command = exact_color_fill(packet.DestinationGpuVa);
  assert(AdmissionBackendImageBindSubmission(
      &image, &packet, destination, (const unsigned char *)&command,
      sizeof(command), &binding));
  assert(AdmissionBackendImageStageJob(
      &image, packet.Fence, 1u, 2u, 2u, 2u, APPLE_AGX_TRUE, &job));
  assert(image.Sequence == 1u);
  assert(AdmissionBackendImageReleaseSubmission(&image, packet.Fence));

  packet.Fence = 92u;
  assert(AdmissionBackendImageBindSubmission(
      &image, &packet, destination, (const unsigned char *)&command,
      sizeof(command), &binding));
  assert(AdmissionBackendImageStageJob(
      &image, packet.Fence, 1u, 2u, 3u, 4u, APPLE_AGX_FALSE, &job));
  assert(image.Sequence == 2u);
  assert(AdmissionBackendImageReleaseSubmission(&image, packet.Fence));

  assert(AdmissionBackendImageRestartQueueLifetime(&image));
  assert(image.Sequence == 0u);
  assert(image.Ready == APPLE_AGX_TRUE);
  assert(image.BoundFence == 0u && image.JobReady == APPLE_AGX_FALSE);

  packet.Fence = 193u;
  assert(AdmissionBackendImageBindSubmission(
      &image, &packet, destination, (const unsigned char *)&command,
      sizeof(command), &binding));
  assert(AdmissionBackendImageStageJob(
      &image, packet.Fence, 1u, 2u, 2u, 2u, APPLE_AGX_TRUE, &job));
  assert(image.Sequence == 1u);
  assert(job.TaExpectedStamp == 0x7a000100u);
  assert(job.D3ExpectedStamp == 0x3d000100u);
  assert(packet.Fence == 193u);
  assert(!AdmissionBackendImageRestartQueueLifetime(&image));
  assert(image.Sequence == 1u && image.BoundFence == packet.Fence);
  assert(AdmissionBackendImageReleaseSubmission(&image, packet.Fence));

  free(destination);
  free(storage);
}

static void test_dynamic_packet_reuses_framebuffer_owner_without_gdi_dma(void) {
  unsigned char *storage = (unsigned char *)malloc(TEST_BACKEND_BYTES);
  unsigned char *destination =
      (unsigned char *)malloc(APPLE_AGX_EXP208_FRAMEBUFFER_BYTES);
  ADMISSION_LOCAL_MEMORY_VIEW view;
  ADMISSION_BACKEND_IMAGE image;
  ADMISSION_RENDER_PACKET_DESCRIPTION packet;
  APPLE_AGX_EXP208_GDI_BINDING binding;
  ADMISSION_ALLOCATION_DESCRIPTION allocation;
  ADMISSION_BACKEND_OUTPUT_VIEW output;
  assert(storage != NULL && destination != NULL);
  view.CpuAddress = storage;
  view.HostPhysicalAddress = TEST_BACKEND_PHYSICAL;
  view.GpuVirtualAddress = TEST_BACKEND_GPU;
  view.Bytes = TEST_BACKEND_BYTES;
  assert(AdmissionBackendImagePrepare(&image, &view));
  memset(&packet, 0, sizeof(packet));
  packet.Fence = 93u;
  packet.DestinationCpuToken =
      (unsigned long long)(unsigned long)destination;
  packet.DestinationGpuVa = 0x1501000000ULL;
  packet.DestinationPhysical = 0x9d1000000ULL;
  packet.DestinationBytes = APPLE_AGX_EXP208_FRAMEBUFFER_BYTES;
  assert(AdmissionBackendImageBindDynamicSubmission(
      &image, &packet, destination, 0xff101820u, &binding));
  assert(image.BoundFence == packet.Fence);
  assert(binding.Framebuffer.Active == APPLE_AGX_TRUE);
  assert(binding.Framebuffer.ClearColor == 0xff101820u);
  assert(image.Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Data ==
         destination);
  assert(AdmissionBackendImageReleaseSubmission(&image, packet.Fence));

  packet.Fence = 94u;
  packet.DestinationBytes = APPLE_AGX_EXP208_GDI_OUTPUT_BYTES;
  assert(AdmissionBackendImageBindDynamicSubmission(
      &image, &packet, destination, APPLE_AGX_EXP208_GDI_COLOR, &binding));
  assert(image.BoundFence == packet.Fence);
  assert(binding.Framebuffer.Active == APPLE_AGX_FALSE);
  assert(image.Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Data ==
         destination);
  assert(image.Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Size ==
         APPLE_AGX_EXP208_GDI_OUTPUT_BYTES);
  assert(AdmissionAllocationDescribe(
      16u, 256u, 4u, 3u, 21u, 0u, &allocation));
  assert(AdmissionBackendImageCaptureOutput(
      &image, &packet, &allocation, &output));
  assert(output.Framebuffer == APPLE_AGX_FALSE);
  assert(output.RenderWidth == 16u && output.RenderHeight == 16u &&
         output.RenderPitch == 64u && output.RenderedBytes == 1024u);
  assert(AdmissionBackendImageReleaseSubmission(&image, packet.Fence));
  free(destination);
  free(storage);
}

static void test_fullscreen_packet_repoints_tiling_graph_and_restores_template(void) {
  unsigned char *storage = (unsigned char *)malloc(TEST_BACKEND_BYTES);
  unsigned char *template_before =
      (unsigned char *)malloc(AppleAgxRenderTemplateBytes());
  unsigned char *destination =
      (unsigned char *)malloc(APPLE_AGX_EXP208_FRAMEBUFFER_BYTES);
  ADMISSION_LOCAL_MEMORY_VIEW view;
  ADMISSION_BACKEND_IMAGE image;
  ADMISSION_RENDER_PACKET_DESCRIPTION packet;
  APPLE_AGX_GDI_DMA_COMMAND command;
  APPLE_AGX_EXP208_GDI_BINDING binding;
  ADMISSION_BACKEND_OUTPUT_VIEW completed_output;
  const APPLE_AGX_EXP208_RELOCATION *relocations;
  unsigned int index;
  unsigned int tpc_edges = 0u;
  unsigned int tilemap_edges = 0u;
  unsigned int cluster_edges = 0u;

  assert(storage != NULL && template_before != NULL && destination != NULL);
  memset(storage, 0xa5, TEST_BACKEND_BYTES);
  view.CpuAddress = storage;
  view.HostPhysicalAddress = TEST_BACKEND_PHYSICAL;
  view.GpuVirtualAddress = TEST_BACKEND_GPU;
  view.Bytes = TEST_BACKEND_BYTES;
  assert(AdmissionBackendImagePrepare(&image, &view));
  memcpy(template_before, storage, AppleAgxRenderTemplateBytes());

  memset(&packet, 0, sizeof(packet));
  packet.Fence = 200u;
  packet.DestinationCpuToken =
      (unsigned long long)(unsigned long)destination;
  packet.DestinationGpuVa = 0x1501000000ULL;
  packet.DestinationPhysical = 0x9d1000000ULL;
  packet.DestinationBytes = APPLE_AGX_EXP208_FRAMEBUFFER_BYTES;
  command = fullscreen_color_fill(packet.DestinationGpuVa);
  command.Color = 0xff00ff00u;
  assert(AdmissionBackendImageBindSubmission(
      &image, &packet, destination, (const unsigned char *)&command,
      sizeof(command), &binding));
  assert(binding.Framebuffer.Active == APPLE_AGX_TRUE);
  assert(AdmissionBackendImageCaptureOutput(
      &image, &packet, &(ADMISSION_ALLOCATION_DESCRIPTION){
          ADMISSION_ALLOCATION_MAGIC, ADMISSION_ALLOCATION_VERSION,
          1u, 21u, 2560u, 1600u, 10240u, 4u,
          APPLE_AGX_EXP208_FRAMEBUFFER_BYTES, 0u, 0u},
      &completed_output));
  assert(completed_output.AllocationCpuAddress == destination);
  assert(completed_output.AllocationGpuAddress == packet.DestinationGpuVa);
  assert(completed_output.AllocationPhysicalAddress == packet.DestinationPhysical);
  assert(completed_output.AllocationBytes == APPLE_AGX_EXP208_FRAMEBUFFER_BYTES);
  assert(completed_output.RenderedBytes == APPLE_AGX_EXP208_FRAMEBUFFER_BYTES);
  assert(completed_output.ExpectedColor == 0xff00ff00u);
  assert(completed_output.Framebuffer == APPLE_AGX_TRUE);
  assert(image.Objects[64u].GpuVa == TEST_BACKEND_GPU + 0x5d0000ULL);
  assert(image.Objects[65u].GpuVa == TEST_BACKEND_GPU + 0x620000ULL);
  assert(image.Objects[67u].GpuVa == TEST_BACKEND_GPU + 0x628000ULL);
  relocations = AppleAgxRenderTemplateRelocations();
  for (index = 0u; index < AppleAgxRenderTemplateRelocationCount(); ++index) {
    const APPLE_AGX_EXP208_RELOCATION *relocation = &relocations[index];
    unsigned long long expected;
    if (relocation->TargetObject != 64u &&
        relocation->TargetObject != 65u &&
        relocation->TargetObject != 67u)
      continue;
    assert(relocation->AddressSpace == AppleAgxExp208RelocationGpuVa);
    assert(relocation->Encoding == AppleAgxExp208RelocationExactU64);
    expected = image.Objects[relocation->TargetObject].GpuVa +
               relocation->TargetOffset;
    assert(read_u64(image.Objects[relocation->SourceObject].Data +
                    relocation->SourceOffset) == expected);
    if (relocation->TargetObject == 64u)
      ++tpc_edges;
    else if (relocation->TargetObject == 65u)
      ++tilemap_edges;
    else
      ++cluster_edges;
  }
  assert(tpc_edges == 2u && tilemap_edges == 3u && cluster_edges == 1u);
  assert(AdmissionBackendImageReleaseSubmission(&image, packet.Fence));
  assert(completed_output.AllocationCpuAddress == destination);
  assert(completed_output.AllocationGpuAddress == packet.DestinationGpuVa);
  assert(completed_output.AllocationPhysicalAddress == packet.DestinationPhysical);
  assert(completed_output.AllocationBytes == APPLE_AGX_EXP208_FRAMEBUFFER_BYTES);
  assert(completed_output.Framebuffer == APPLE_AGX_TRUE);
  assert(memcmp(template_before, storage,
                AppleAgxRenderTemplateBytes()) == 0);
  assert(image.Objects[64u].GpuVa == TEST_BACKEND_GPU + 0x540000ULL);
  assert(image.Objects[65u].GpuVa == TEST_BACKEND_GPU + 0x548000ULL);
  assert(image.Objects[67u].GpuVa == TEST_BACKEND_GPU + 0x558000ULL);

  packet.Fence = 201u;
  command.Destination.Top = APPLE_AGX_EXP208_FRAMEBUFFER_BAND_TOP;
  command.Color = 0xff0000ffu;
  assert(AdmissionBackendImageBindSubmission(
      &image, &packet, destination, (const unsigned char *)&command,
      sizeof(command), &binding));
  assert(AdmissionBackendImageCaptureOutput(
      &image, &packet, &(ADMISSION_ALLOCATION_DESCRIPTION){
          ADMISSION_ALLOCATION_MAGIC, ADMISSION_ALLOCATION_VERSION,
          1u, 21u, 2560u, 1600u, 10240u, 4u,
          APPLE_AGX_EXP208_FRAMEBUFFER_BYTES, 0u, 0u},
      &completed_output));
  assert(completed_output.AllocationCpuAddress == destination);
  assert(completed_output.RenderedCpuAddress ==
         destination + APPLE_AGX_EXP208_FRAMEBUFFER_BAND_OFFSET);
  assert(completed_output.RenderedOffset ==
         APPLE_AGX_EXP208_FRAMEBUFFER_BAND_OFFSET);
  assert(completed_output.RenderedBytes ==
         APPLE_AGX_EXP208_FRAMEBUFFER_BAND_BYTES);
  assert(completed_output.RenderHeight ==
         APPLE_AGX_EXP208_FRAMEBUFFER_BAND_HEIGHT);
  assert(completed_output.ExpectedColor == 0xff0000ffu);
  assert(AdmissionBackendImageReleaseSubmission(&image, packet.Fence));
  free(destination);
  free(template_before);
  free(storage);
}

int main(void) {
  test_materializes_and_relocates_exact_rebased_image();
  test_rejects_invalid_tail_atomically();
  test_exact_packet_binds_output_and_reapplies_relocations();
  test_restart_queue_lifetime_resets_only_firmware_sequence();
  test_dynamic_packet_reuses_framebuffer_owner_without_gdi_dma();
  test_fullscreen_packet_repoints_tiling_graph_and_restores_template();
  return 0;
}
