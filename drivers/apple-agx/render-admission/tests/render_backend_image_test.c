#include "render_backend_image.h"
#include "render_dynamic_overlay.h"
#include "render_completed_output.h"
#include "apple_agx_g13_codec.h"

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
    /* A TA/3D template must not manufacture a compute dispatch from stack data. */
    assert(job.ComputeWorkAddressCount == 0u);
    assert(job.ComputeEvent == 0u);
    assert(job.ComputeExpectedStamp == 0u);
    assert(job.ComputeExpectedDonePointer == 0u);
    for (index = 0u; index < APPLE_AGX_BACKEND_QUEUE_WORK_COUNT; ++index)
      assert(job.ComputeWorkAddresses[index] == 0ULL);
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
  assert(read_u64(image.Objects[18u].Data + 0x90u) == 0x22004ULL);
  assert(read_u64(image.Objects[18u].Data + 0x618u) == 0x23004ULL);
  assert(read_u64(image.Objects[18u].Data + 0x648u) == 0x23004ULL);
  assert(read_u64(image.Objects[36u].Data + 0x3000u) ==
         0x000003c00fc60a22ULL);
  assert(read_u64(image.Objects[36u].Data + 0x3008u) ==
         0x1000000150100000ULL);
  assert(read_u64(image.Objects[36u].Data + 0x3010u) == 0ULL);
  assert(read_u64(image.Objects[36u].Data + 0x4000u) ==
         0xffffffff00000000ULL);
  assert(read_u64(image.Objects[73u].Data + 0x2000u) ==
         0x1500920000400c1dULL);
  assert(read_u64(image.Objects[73u].Data + 0x4000u) ==
         0x15009230001000ddULL);
  assert(read_u64(image.Objects[73u].Data + 0x4008u) ==
         0x150092400040041dULL);
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

static void test_native_binding_preserves_logical_attachment(void) {
  unsigned char *arena=malloc(TEST_BACKEND_BYTES), *target=malloc(0x4000);
  ADMISSION_BACKEND_IMAGE image;
  ADMISSION_LOCAL_MEMORY_VIEW view={0};
  ADMISSION_RENDER_PACKET_DESCRIPTION packet={0};
  ADMISSION_DYNAMIC_OVERLAY_BINDINGS native={0};
  APPLE_AGX_EXP208_GDI_BINDING binding;
  APPLE_AGX_EXP208_RELOCATION_OBJECT original;
  ADMISSION_BACKEND_OUTPUT_VIEW output;
  ADMISSION_ALLOCATION_DESCRIPTION description;
  ADMISSION_ALLOCATION_OBJECT allocation;
  ADMISSION_COMPLETED_OUTPUT completed;
  assert(arena && target);
  memset(target,0x6a,0x4000);
  view.CpuAddress=arena; view.HostPhysicalAddress=TEST_BACKEND_PHYSICAL;
  view.GpuVirtualAddress=TEST_BACKEND_GPU; view.Bytes=TEST_BACKEND_BYTES;
  assert(AdmissionBackendImagePrepare(&image,&view));
  original=image.Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT];
  packet.Fence=61; packet.DestinationGpuVa=0x1500200000ULL;
  packet.DestinationPhysical=0x890200000ULL; packet.DestinationBytes=0x4000;
  packet.DestinationCpuToken=(unsigned long long)(uintptr_t)target;
  native.CommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH;
  native.SurfaceWidth=16; native.SurfaceHeight=16; native.SurfacePitch=64;
  native.SurfaceBytesPerPixel=4;
  native.DestinationBytes=0x4000;
  assert(AdmissionBackendImageBindNativeSubmission(&image,&packet,target,&native,&binding));
  assert(image.NativeBound && image.Objects[40].Data==target && image.Objects[40].Size==0x4000);
  for(unsigned i=0;i<0x4000;++i) assert(target[i]==0x6a);
  /* Real native allocation is a byte buffer; preserve that allocation owner
   * while separately describing the 16x16 image and its entire tiled span. */
  assert(AdmissionAllocationDescribe(0x4000,1,1,ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE,
      ADMISSION_WIN32_ALLOCATION_FORMAT_A8,1,&description));
  assert(AdmissionAllocationCreate(&description,&allocation));
  assert(AdmissionBackendImageCaptureOutput(&image,&packet,&description,&output));
  assert(output.VerificationKind==AdmissionBackendOutputVerificationNativeCapture && !output.Framebuffer);
  assert(output.AllocationWidth==0x4000 && output.AllocationHeight==1 && output.AllocationPitch==0x4000);
  assert(output.RenderWidth==16 && output.RenderHeight==16 && output.RenderPitch==64 && output.RenderedBytes==0x4000);
  assert(!output.ExpectedColor && !output.BackgroundColor && output.RenderedCpuAddress==target);
  AdmissionCompletedOutputInitialize(&completed);
  assert(AdmissionCompletedOutputPlatformRangeValid(&output,target,packet.DestinationGpuVa,
      packet.DestinationPhysical,0x4000));
  assert(AdmissionCompletedOutputCapture(&completed,37,packet.Fence,&output,&allocation));
  assert(!AdmissionAllocationDestroy(&allocation));
  assert(!AdmissionBackendImageBindNativeSubmission(&image,&packet,target,&native,&binding));
  assert(AdmissionBackendImageReleaseSubmission(&image,packet.Fence));
  assert(!image.NativeBound && memcmp(&image.Objects[40],&original,sizeof(original))==0);
  assert(completed.View.RenderedCpuAddress==target && completed.View.RenderedBytes==0x4000);
  assert(AdmissionCompletedOutputMarkReleased(&completed,packet.Fence));
  assert(AdmissionCompletedOutputMarkPacketRetired(&completed,packet.Fence));
  assert(!AdmissionCompletedOutputRecordAccess(&completed,packet.Fence,0));
  assert(AdmissionCompletedOutputMarkNotified(&completed,packet.Fence));
  assert(AdmissionCompletedOutputRecordAccess(&completed,packet.Fence,0));
  assert(!completed.PresentationAttempted);
  assert(AdmissionCompletedOutputAbort(&completed,packet.Fence));
  assert(AdmissionAllocationDestroy(&allocation));
  {
    unsigned char *desktop=realloc(target,0xfa0000);
    assert(desktop);target=desktop;
    packet.DestinationCpuToken=(unsigned long long)(uintptr_t)target;
    packet.Fence=62;packet.DestinationBytes=0xfa0000;
    native.SurfaceWidth=2560;native.SurfaceHeight=1600;
    native.SurfacePitch=10240;native.DestinationBytes=0xfa0000;
    assert(AdmissionBackendImageBindNativeSubmission(
        &image,&packet,target,&native,&binding));
    assert(image.NativeBound && image.Objects[40].Data==target &&
           image.Objects[40].Size==0xfa0000);
    assert(AdmissionAllocationDescribe(2560,1600,4,1,1,0,&description));
    assert(AdmissionBackendImageCaptureOutput(
        &image,&packet,&description,&output));
    assert(output.RenderWidth==2560 && output.RenderHeight==1600 &&
           output.RenderPitch==10240 && output.RenderedBytes==0xfa0000 &&
           output.AllocationBytes==0xfa0000);
    assert(AdmissionBackendImageReleaseSubmission(&image,packet.Fence));
  }
  /* Linear shared row padding is not another pixel format. */
  native.SurfaceWidth=1366;native.SurfaceHeight=768;
  native.SurfacePitch=5472;native.DestinationBytes=packet.DestinationBytes=5472u*768u;
  packet.Fence=63;
  assert(AdmissionBackendImageBindNativeSubmission(&image,&packet,target,&native,&binding));
  assert(image.NativePitch==5472);
  assert(AdmissionBackendImageReleaseSubmission(&image,packet.Fence));
  native.SurfacePitch=5464+1; /* Sufficient row bytes, invalid AGX alignment. */
  assert(!AdmissionBackendImageBindNativeSubmission(&image,&packet,target,&native,&binding));
  packet.Fence=63;packet.DestinationBytes=0x400000;
  native.SurfaceWidth=1024;native.SurfaceHeight=1024;
  native.SurfacePitch=4096;native.DestinationBytes=packet.DestinationBytes;
  assert(AdmissionBackendImageBindNativeSubmission(&image,&packet,target,&native,&binding));
  assert(image.NativeWidth==1024 && image.NativeHeight==1024 && image.NativePitch==4096);
  assert(AdmissionBackendImageReleaseSubmission(&image,packet.Fence));
  native.SurfacePitch=4095;
  assert(!AdmissionBackendImageBindNativeSubmission(&image,&packet,target,&native,&binding));
  native.SurfacePitch=3072; /* No admitted native color format has three bytes per pixel. */
  assert(!AdmissionBackendImageBindNativeSubmission(&image,&packet,target,&native,&binding));
  native.SurfacePitch=4096;native.SurfaceWidth=0;
  assert(!AdmissionBackendImageBindNativeSubmission(&image,&packet,target,&native,&binding));
  native.SurfaceWidth=1024;native.DestinationBytes=packet.DestinationBytes=0x3fffff;
  assert(!AdmissionBackendImageBindNativeSubmission(&image,&packet,target,&native,&binding));
  native.DestinationBytes=0x400000;
  packet.DestinationBytes=0x2000;
  assert(!AdmissionBackendImageBindNativeSubmission(&image,&packet,target,&native,&binding));
  free(target); free(arena);
}

static void test_b1_same_va_distinct_physical_output(void) {
  unsigned char *arena[2], *output[2];
  ADMISSION_BACKEND_IMAGE image[2];
  ADMISSION_LOCAL_MEMORY_VIEW view;
  ADMISSION_RENDER_PACKET_DESCRIPTION packet;
  APPLE_AGX_EXP208_GDI_BINDING binding;
  APPLE_AGX_GDI_DMA_COMMAND command=exact_color_fill(0x15001d0000ULL);
  const APPLE_AGX_EXP208_RELOCATION *reloc=AppleAgxRenderTemplateRelocations();
  unsigned edge=~0u;
  for(unsigned i=0;i<AppleAgxRenderTemplateRelocationCount();i++)
    if(reloc[i].TargetObject==40u){
      assert(reloc[i].AddressSpace==AppleAgxExp208RelocationGpuVa);
      edge=i;break;
    }
  assert(edge!=~0u);
  for(unsigned i=0;i<2;i++){
    arena[i]=malloc(TEST_BACKEND_BYTES);output[i]=malloc(0x4000u);
    assert(arena[i]&&output[i]);
    memset(&view,0,sizeof(view));
    view.CpuAddress=arena[i];view.HostPhysicalAddress=TEST_BACKEND_PHYSICAL;
    view.GpuVirtualAddress=TEST_BACKEND_GPU;view.Bytes=TEST_BACKEND_BYTES;
    assert(AdmissionBackendImagePrepare(&image[i],&view));
    memset(&packet,0,sizeof(packet));packet.Fence=19u;
    packet.DestinationCpuToken=(unsigned long long)(unsigned long)output[i];
    packet.DestinationGpuVa=0x15001d0000ULL;
    packet.DestinationPhysical=0x9d0040000ULL+i*0x4000ULL;
    packet.DestinationBytes=0x4000u;
    assert(AdmissionBackendImageBindSubmission(&image[i],&packet,output[i],
      (const unsigned char *)&command,sizeof(command),&binding));
    assert(read_u64(image[i].Objects[reloc[edge].SourceObject].Data+
                    reloc[edge].SourceOffset)==0x15001d0000ULL);
  }
  assert(image[0].Objects[40].PhysicalAddress!=image[1].Objects[40].PhysicalAddress);
  for(unsigned i=0;i<2;i++){free(output[i]);free(arena[i]);}
}

static void test_g4_native_scene_stages_and_releases(void) {
  unsigned char *arena=malloc(TEST_BACKEND_BYTES);
  unsigned char *target=malloc(1280u*720u*4u);
  ADMISSION_BACKEND_IMAGE image;
  ADMISSION_LOCAL_MEMORY_VIEW backend={0};
  ADMISSION_RENDER_PACKET_DESCRIPTION packet={0};
  APPLE_AGX_G4_SUBMIT_VIEW view={0};
  APPLE_AGX_G4_NATIVE_RENDER render={0};
  APPLE_AGX_G4_ATTACHMENT color={0};
  struct {
    APPLE_AGX_G4_NATIVE_HEADER AttachCommand;
    APPLE_AGX_G4_ATTACHMENT Attachment;
    APPLE_AGX_G4_NATIVE_HEADER RenderCommand;
    APPLE_AGX_G4_NATIVE_RENDER Render;
  } native={0};
  APPLE_AGX_EXP208_GDI_BINDING binding;
  APPLE_AGX_BACKEND_JOB_IMAGE job;
  unsigned required[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
  unsigned long long va=0x10000000ULL;
  assert(arena && target);
  backend.CpuAddress=arena;backend.HostPhysicalAddress=TEST_BACKEND_PHYSICAL;
  backend.GpuVirtualAddress=TEST_BACKEND_GPU;backend.Bytes=TEST_BACKEND_BYTES;
  assert(AdmissionBackendImagePrepare(&image,&backend));
  render.Flags=1u<<2;render.WidthPx=1280;render.HeightPx=720;
  render.Layers=1;render.UtileWidthPx=render.UtileHeightPx=32;
  render.Samples=1;render.SampleSizeBytes=8;
  render.VdmCtrlStreamBase=0x12000000ULL;
  render.IspScissorBase=0x12100000ULL;
  render.IspDbiasBase=0x12200000ULL;
  render.SamplerHeap=0x12300000ULL;
  assert(AppleAgxG4ProcessRequiredBytes(&render,required));
  for(unsigned i=0;i<APPLE_AGX_G4_PROCESS_RANGE_COUNT;++i){
    view.Process[i].Va=va;view.Process[i].Bytes=required[i];va+=required[i];
  }
  color.Pointer=0x13000000ULL;color.Size=1280ULL*720ULL*4ULL;
  native.AttachCommand.Type=APPLE_AGX_G4_FRAGMENT_ATTACHMENTS;
  native.AttachCommand.Size=sizeof(color);
  native.AttachCommand.VdmBarrier=0xffffu;
  native.AttachCommand.CdmBarrier=0xffffu;
  native.Attachment=color;
  native.RenderCommand.Type=APPLE_AGX_G4_RENDER;
  native.RenderCommand.Size=sizeof(render);
  native.Render=render;
  view.Native=(const unsigned char *)&native;
  view.CommandVa=0x20000ULL;view.CommandBytes=sizeof(native);
  view.Render=(const unsigned char *)&native.Render;
  view.RenderBytes=sizeof(render);
  view.Attachments=&native.Attachment;view.AttachmentCount=1u;
  view.ColorFormat=APPLE_AGX_G4_COLOR_BGRA8;
  packet.Fence=77u;packet.DestinationGpuVa=color.Pointer;
  packet.DestinationPhysical=0x890200000ULL;
  packet.DestinationCpuToken=(unsigned long long)(uintptr_t)target;
  packet.DestinationBytes=(unsigned)color.Size;
  assert(AdmissionBackendImageBindG4Submission(
      &image,&packet,target,&view,&binding));
  assert(image.G4Native && image.NativeBound && image.BoundFence==77u);
  assert(image.G4CommandBytes==sizeof(native) &&
      image.G4Header.Base.Reserved==APPLE_AGX_G4_COLOR_BGRA8 &&
      memcmp(image.G4Command,&native,sizeof(native))==0);
  assert(AdmissionBackendImageStageJob(&image,77u,1u,2u,5u,6u,
      APPLE_AGX_TRUE,&job));
  assert(job.TaWorkAddresses[1]==image.Objects[19].GpuVa);
  assert(read_u64(image.Objects[19].Data+88u)==0ULL);
  assert(read_u64(image.Objects[19].Data+1352u)==render.SamplerHeap);
  assert(AdmissionBackendImageReleaseSubmission(&image,77u));
  assert(image.Ready && !image.G4Native && !image.NativeBound &&
      image.BoundFence==0u);
  /* R151: image refresh must not restart a live queue's stamp lifetime.
   * The stale event is deliberately paired with the NEW ring done pointer:
   * event identity and pointer alone cannot qualify the second completion. */
  assert(image.Sequence == 1u);
  packet.Fence=220u;
  assert(AdmissionBackendImageBindG4Submission(
      &image,&packet,target,&view,&binding));
  assert(image.Sequence == 1u);
  assert(AdmissionBackendImageStageJob(&image,220u,1u,2u,7u,8u,
      APPLE_AGX_TRUE,&job));
  assert(image.Sequence == 2u);
  assert(job.TaWorkAddresses[0] == image.Objects[APPLE_AGX_EXP208_TA_INITBM_OBJECT].GpuVa);
  assert(job.TaWorkAddresses[1] == image.Objects[19].GpuVa);
  assert(job.TaExpectedStamp == 0x7a000200u);
  assert(job.D3ExpectedStamp == 0x3d000200u);
  assert(image.Dynamic.TaPreviousStamp == 0x7a000100u);
  assert(image.Dynamic.D3PreviousStamp == 0x3d000100u);
  assert(image.Dynamic.EventCount == 4u);
  assert(image.Dynamic.Start3dQueueCommandCount == 0x3d0001u);
  /* Check the serialized barrier/finalizers, independently of job metadata. */
  {
    const unsigned offsets[] = {0x7000cu,0x70014u,0x7823cu,0x8824cu,
                                0x98484u,0x98580u,0x60000u};
    const unsigned values[] = {0x7a000200u,0x3d000200u,0x3d000200u,
                               0x7a000200u,0x7a000200u,0x7a000200u,4u};
    APPLE_AGX_G13_EVENT event={0};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
      unsigned v;memcpy(&v,arena+offsets[i],sizeof(v));assert(v==values[i]);
    }
    event.Kind=AppleAgxG13EventFlag;event.Firing[0]=(1ULL<<1)|(1ULL<<2);
    assert(!AppleAgxG13CompletionSatisfied(&event,1u,0x7a000100u,
        job.TaExpectedStamp,7u,job.TaExpectedDonePointer));
    assert(!AppleAgxG13CompletionSatisfied(&event,2u,0x3d000100u,
        job.D3ExpectedStamp,8u,job.D3ExpectedDonePointer));
    assert(AppleAgxG13CompletionSatisfied(&event,1u,0x7a000200u,
        job.TaExpectedStamp,7u,job.TaExpectedDonePointer));
    assert(AppleAgxG13CompletionSatisfied(&event,2u,0x3d000200u,
        job.D3ExpectedStamp,8u,job.D3ExpectedDonePointer));
  }
  assert(!AdmissionBackendImageRestartQueueLifetime(&image));
  assert(!AdmissionBackendImageReleaseSubmission(&image,219u));
  assert(image.Sequence==2u && image.BoundFence==220u);
  assert(AdmissionBackendImageReleaseSubmission(&image,220u));
  assert(image.Sequence==2u);
  /* Legacy and G4 use the same live firmware queues and stamp lifetime. */
  {
    APPLE_AGX_GDI_DMA_COMMAND command=exact_color_fill(packet.DestinationGpuVa);
    packet.Fence=240u;packet.DestinationBytes=0x4000u;
    assert(AdmissionBackendImageBindSubmission(&image,&packet,target,
        (const unsigned char *)&command,sizeof(command),&binding));
    assert(AdmissionBackendImageStageJob(&image,240u,1u,2u,7u,10u,
        APPLE_AGX_FALSE,&job));
    assert(image.Sequence==3u && job.TaExpectedStamp==0x7a000300u);
    assert(AdmissionBackendImageReleaseSubmission(&image,240u));
    packet.Fence=270u;packet.DestinationBytes=(unsigned)color.Size;
    assert(AdmissionBackendImageBindG4Submission(
        &image,&packet,target,&view,&binding));
    assert(image.Sequence==3u);
    assert(AdmissionBackendImageStageJob(&image,270u,1u,2u,8u,12u,
        APPLE_AGX_FALSE,&job));
    assert(image.Sequence==4u && job.D3ExpectedStamp==0x3d000400u);
    assert(AdmissionBackendImageReleaseSubmission(&image,270u));
  }
  /* Only a real queue-lifetime restart permits first-job values again. */
  assert(AdmissionBackendImageRestartQueueLifetime(&image));
  packet.Fence=301u;
  assert(AdmissionBackendImageBindG4Submission(
      &image,&packet,target,&view,&binding));
  assert(AdmissionBackendImageStageJob(&image,301u,1u,2u,2u,2u,
      APPLE_AGX_TRUE,&job));
  assert(image.Sequence==1u && job.TaExpectedStamp==0x7a000100u);
  assert(AdmissionBackendImageReleaseSubmission(&image,301u));
  /* Refresh cannot evade the existing stamp-overflow refusal. */
  image.Sequence=0xffffffffu;packet.Fence=302u;
  assert(AdmissionBackendImageBindG4Submission(
      &image,&packet,target,&view,&binding));
  assert(!AdmissionBackendImageStageJob(&image,302u,1u,2u,3u,4u,
      APPLE_AGX_FALSE,&job));
  assert(image.Sequence==0xffffffffu && !image.JobReady);
  assert(AdmissionBackendImageReleaseSubmission(&image,302u));
  free(target);free(arena);
}

int main(void) {
  test_materializes_and_relocates_exact_rebased_image();
  test_rejects_invalid_tail_atomically();
  test_exact_packet_binds_output_and_reapplies_relocations();
  test_restart_queue_lifetime_resets_only_firmware_sequence();
  test_dynamic_packet_reuses_framebuffer_owner_without_gdi_dma();
  test_fullscreen_packet_repoints_tiling_graph_and_restores_template();
  test_native_binding_preserves_logical_attachment();
  test_b1_same_va_distinct_physical_output();
  test_g4_native_scene_stages_and_releases();
  return 0;
}
