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
  assert(!AdmissionBackendImageReleaseSubmission(&image, 18u));
  assert(AdmissionBackendImageReleaseSubmission(&image, packet.Fence));
  assert(image.BoundFence == 0u);
  free(destination);
  free(storage);
}

int main(void) {
  test_materializes_and_relocates_exact_rebased_image();
  test_rejects_invalid_tail_atomically();
  test_exact_packet_binds_output_and_reapplies_relocations();
  return 0;
}
