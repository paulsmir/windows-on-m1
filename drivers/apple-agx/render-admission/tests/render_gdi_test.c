#include "render_gdi.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static ADMISSION_GDI_COLOR_FILL_INPUT valid_input(
    APPLE_AGX_GDI_RECT *sub_rects) {
  ADMISSION_GDI_COLOR_FILL_INPUT input;

  memset(&input, 0, sizeof(input));
  input.Destination =
      (APPLE_AGX_GDI_RECT){10u, 20u, 210u, 220u};
  input.DestinationAllocationIndex = 3u;
  input.AllocationCount = 4u;
  input.DestinationWritable = 1u;
  input.Color = 0xff336699u;
  input.DestinationPitch = 1024u;
  input.Rop = (unsigned int)AppleAgxGdiColorFillPatCopy;
  input.SubRectCount = 2u;
  input.SubRects = sub_rects;
  return input;
}

static void test_colorfill_preparation_is_pointer_free_and_exact(void) {
  APPLE_AGX_GDI_RECT sub_rects[2] = {
      {10u, 20u, 110u, 120u},
      {110u, 120u, 210u, 220u},
  };
  unsigned char dma[512];
  ADMISSION_GDI_COLOR_FILL_INPUT input = valid_input(sub_rects);
  ADMISSION_GDI_PREPARED prepared;
  APPLE_AGX_GDI_DMA_COMMAND *command;
  APPLE_AGX_GDI_RECT *copied_rects;
  unsigned int expected_bytes =
      (unsigned int)sizeof(APPLE_AGX_GDI_DMA_COMMAND) +
      2u * (unsigned int)sizeof(APPLE_AGX_GDI_RECT);

  memset(dma, 0xa5, sizeof(dma));
  memset(&prepared, 0, sizeof(prepared));
  assert(AdmissionGdiPrepareColorFill(&input, 0x80u, dma, sizeof(dma),
                                      &prepared));
  assert(prepared.DmaOffset == 0x80u);
  assert(prepared.DmaBytes == expected_bytes);
  assert(prepared.PatchCount == 1u);
  assert(prepared.Patches[0].AllocationIndex == 3u);
  assert(prepared.Patches[0].SlotId == 2u);
  assert(prepared.Patches[0].PatchOffset ==
         0x80u + offsetof(APPLE_AGX_GDI_DMA_COMMAND,
                          DestinationGpuAddress));
  assert(prepared.Patches[0].SplitOffset == 0x80u);
  command = (APPLE_AGX_GDI_DMA_COMMAND *)dma;
  assert(command->Magic == APPLE_AGX_GDI_DMA_MAGIC);
  assert(command->Version == APPLE_AGX_GDI_DMA_VERSION);
  assert(command->Opcode == (unsigned int)AppleAgxGdiColorFill);
  assert(command->Rop == (unsigned int)AppleAgxGdiColorFillPatCopy);
  assert(command->Rop3 == 0u);
  assert(command->DestinationAllocationIndex == 3u);
  assert(command->DestinationGpuAddress == 0u);
  copied_rects = (APPLE_AGX_GDI_RECT *)(dma + sizeof(*command));
  sub_rects[0].Left = 99u;
  assert(copied_rects[0].Left == 10u);
  assert(copied_rects[1].Right == 210u);
}

static void test_malformed_or_unsupported_colorfill_is_atomic(void) {
  APPLE_AGX_GDI_RECT sub_rects[1] = {{10u, 20u, 210u, 220u}};
  unsigned char dma[256];
  unsigned char before[256];
  ADMISSION_GDI_COLOR_FILL_INPUT input = valid_input(sub_rects);
  ADMISSION_GDI_PREPARED prepared;

  input.SubRectCount = 1u;
  memset(dma, 0x5a, sizeof(dma));
  memcpy(before, dma, sizeof(dma));
  input.Rop = (unsigned int)AppleAgxGdiColorFillPatInvert;
  assert(!AdmissionGdiPrepareColorFill(&input, 0u, dma, sizeof(dma),
                                       &prepared));
  assert(memcmp(dma, before, sizeof(dma)) == 0);
  input.Rop = (unsigned int)AppleAgxGdiColorFillPatCopy;
  input.DestinationWritable = 0u;
  assert(!AdmissionGdiPrepareColorFill(&input, 0u, dma, sizeof(dma),
                                       &prepared));
  input.DestinationWritable = 1u;
  input.DestinationAllocationIndex = input.AllocationCount;
  assert(!AdmissionGdiPrepareColorFill(&input, 0u, dma, sizeof(dma),
                                       &prepared));
  input.DestinationAllocationIndex = 0u;
  sub_rects[0].Right = input.Destination.Right + 1u;
  assert(!AdmissionGdiPrepareColorFill(&input, 0u, dma, sizeof(dma),
                                       &prepared));
  sub_rects[0].Right = input.Destination.Right;
  assert(!AdmissionGdiPrepareColorFill(
      &input, 0xfffffff0u, dma, sizeof(dma), &prepared));
}

static void test_only_prerecorded_patch_can_seal_exact_fence(void) {
  APPLE_AGX_GDI_RECT sub_rect = {0u, 0u, 64u, 64u};
  ADMISSION_GDI_COLOR_FILL_INPUT input;
  ADMISSION_GDI_PREPARED prepared;
  ADMISSION_GDI_PATCH patch;
  APPLE_AGX_DMA_SHADOW shadow;
  unsigned char dma[256];
  unsigned char private_data[512];
  unsigned long long gpu_va = 0x1500010000ULL;
  unsigned long long observed = 0ULL;

  memset(&input, 0, sizeof(input));
  input.Destination = sub_rect;
  input.DestinationAllocationIndex = 0u;
  input.AllocationCount = 1u;
  input.DestinationWritable = 1u;
  input.Color = 0xff102030u;
  input.DestinationPitch = 256u;
  input.Rop = (unsigned int)AppleAgxGdiColorFillPatCopy;
  input.SubRectCount = 1u;
  input.SubRects = &sub_rect;
  memset(dma, 0, sizeof(dma));
  assert(AdmissionGdiPrepareColorFill(&input, 0x40u, dma, sizeof(dma),
                                      &prepared));
  AppleAgxDmaShadowInitialize(&shadow, private_data,
                              (unsigned int)sizeof(private_data));
  assert(AppleAgxDmaShadowAppend(&shadow, prepared.DmaOffset, dma,
                                 prepared.DmaBytes));
  patch = prepared.Patches[0];
  assert(AdmissionGdiPatchAuthorized(&prepared, &patch, 0x40u,
                                     0x40u + prepared.DmaBytes));
  patch.PatchOffset++;
  assert(!AdmissionGdiPatchAuthorized(&prepared, &patch, 0x40u,
                                      0x40u + prepared.DmaBytes));
  patch = prepared.Patches[0];
  patch.AllocationIndex++;
  assert(!AdmissionGdiPatchAuthorized(&prepared, &patch, 0x40u,
                                      0x40u + prepared.DmaBytes));
  patch = prepared.Patches[0];
  memcpy(dma + offsetof(APPLE_AGX_GDI_DMA_COMMAND,
                        DestinationGpuAddress),
         &gpu_va, sizeof(gpu_va));
  assert(AppleAgxDmaShadowPatchU64(
      shadow.Storage, shadow.BytesUsed, prepared.Patches[0].PatchOffset,
      gpu_va));
  assert(AppleAgxDmaShadowSeal(&shadow, 17u));
  assert(AppleAgxDmaShadowMatchesU64(
      shadow.Storage, shadow.BytesUsed, prepared.Patches[0].PatchOffset,
      gpu_va));
  memcpy(&observed,
         dma + offsetof(APPLE_AGX_GDI_DMA_COMMAND,
                        DestinationGpuAddress),
         sizeof(observed));
  assert(observed == gpu_va);
  assert(!AppleAgxDmaShadowPatchU64(
      shadow.Storage, shadow.BytesUsed, prepared.Patches[0].PatchOffset,
      gpu_va + 0x10000ULL));
}

static void test_prepared_record_is_reconstructed_from_immutable_bytes(void) {
  APPLE_AGX_GDI_RECT sub_rect = {0u, 0u, 32u, 32u};
  ADMISSION_GDI_COLOR_FILL_INPUT input;
  ADMISSION_GDI_PREPARED prepared;
  ADMISSION_GDI_PREPARED reconstructed;
  unsigned char dma[256];
  APPLE_AGX_GDI_DMA_COMMAND *command;

  memset(&input, 0, sizeof(input));
  input.Destination = sub_rect;
  input.DestinationAllocationIndex = 1u;
  input.AllocationCount = 2u;
  input.DestinationWritable = 1u;
  input.Color = 0xff010203u;
  input.DestinationPitch = 128u;
  input.Rop = (unsigned int)AppleAgxGdiColorFillPatCopy;
  input.SubRectCount = 1u;
  input.SubRects = &sub_rect;
  assert(AdmissionGdiPrepareColorFill(&input, 0x100u, dma, sizeof(dma),
                                      &prepared));
  assert(AdmissionGdiDescribePreparedRecord(
      dma, prepared.DmaBytes, prepared.DmaOffset, &reconstructed));
  assert(memcmp(&prepared, &reconstructed, sizeof(prepared)) == 0);
  command = (APPLE_AGX_GDI_DMA_COMMAND *)dma;
  command->DestinationGpuAddress = 0x1500010000ULL;
  assert(AdmissionGdiDescribePreparedRecord(
      dma, prepared.DmaBytes, prepared.DmaOffset, &reconstructed));
  command->Rop = (unsigned int)AppleAgxGdiColorFillPatInvert;
  assert(!AdmissionGdiDescribePreparedRecord(
      dma, prepared.DmaBytes, prepared.DmaOffset, &reconstructed));
}

int main(void) {
  test_colorfill_preparation_is_pointer_free_and_exact();
  test_malformed_or_unsupported_colorfill_is_atomic();
  test_only_prerecorded_patch_can_seal_exact_fence();
  test_prepared_record_is_reconstructed_from_immutable_bytes();
  return 0;
}
