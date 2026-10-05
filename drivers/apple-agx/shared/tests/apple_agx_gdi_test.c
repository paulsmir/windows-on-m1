#include "apple_agx_gdi.h"

#include <assert.h>
#include <string.h>

#define GDI_POOL_BYTES 0x10000ULL

typedef struct _FAKE_GDI_MEMORY {
  unsigned char Storage[0x30000];
  unsigned long long DeviceBase;
  unsigned long long RequestedBytes;
  unsigned int AllocateCount;
  unsigned int FreeCount;
  unsigned char FailAllocation;
} FAKE_GDI_MEMORY;

static unsigned char allocate_gdi_memory(
    void *context, unsigned long long bytes, void **cpu_base,
    unsigned long long *device_base, void **allocation_handle) {
  FAKE_GDI_MEMORY *fake = (FAKE_GDI_MEMORY *)context;
  ++fake->AllocateCount;
  fake->RequestedBytes = bytes;
  if (fake->FailAllocation != 0u)
    return 0u;
  *cpu_base = &fake->Storage[0x1000];
  *device_base = fake->DeviceBase;
  *allocation_handle = fake;
  return 1u;
}

static unsigned char free_gdi_memory(void *context, void *allocation_handle) {
  FAKE_GDI_MEMORY *fake = (FAKE_GDI_MEMORY *)context;
  assert(allocation_handle == fake);
  ++fake->FreeCount;
  return 1u;
}

static void put_u32(unsigned char *bytes, APPLE_AGX_U32 value) {
  bytes[0] = (unsigned char)(value & 0xffu);
  bytes[1] = (unsigned char)((value >> 8) & 0xffu);
  bytes[2] = (unsigned char)((value >> 16) & 0xffu);
  bytes[3] = (unsigned char)((value >> 24) & 0xffu);
}

static APPLE_AGX_U64 test_stream_hash(const unsigned char *bytes,
                                      APPLE_AGX_U32 byte_count) {
  APPLE_AGX_U64 hash = 14695981039346656037ULL;
  APPLE_AGX_U32 index;

  for (index = 0u; index < byte_count; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ULL;
  }
  return hash;
}

static APPLE_AGX_U32 encode_canonical_command(
    APPLE_AGX_U32 opcode, APPLE_AGX_U32 rop, APPLE_AGX_U32 flags,
    unsigned char *dma, APPLE_AGX_U32 dma_bytes) {
  APPLE_AGX_GDI_COMMAND_DESCRIPTION description;
  APPLE_AGX_GDI_RECT sub_rect;
  APPLE_AGX_U32 written = 0u;

  memset(&description, 0, sizeof(description));
  sub_rect = (APPLE_AGX_GDI_RECT){11u, 21u, 31u, 41u};
  description.Command.Opcode = opcode;
  description.Command.SubRectCount =
      opcode == (APPLE_AGX_U32)AppleAgxGdiEscape ? 0u : 1u;
  description.Command.Source = (APPLE_AGX_GDI_RECT){1u, 2u, 101u, 202u};
  description.Command.Destination =
      (APPLE_AGX_GDI_RECT){10u, 20u, 110u, 220u};
  description.Command.SourceAllocationIndex = 1u;
  description.Command.DestinationAllocationIndex = 2u;
  description.Command.TemporaryAllocationIndex = 3u;
  description.Command.GammaAllocationIndex = 4u;
  description.Command.AlphaAllocationIndex = 5u;
  description.Command.SourceGpuAddress = 0x10000ULL;
  description.Command.DestinationGpuAddress = 0x20000ULL;
  description.Command.TemporaryGpuAddress = 0x30000ULL;
  description.Command.GammaGpuAddress = 0x40000ULL;
  description.Command.AlphaGpuAddress = 0x50000ULL;
  description.Command.Flags = flags;
  description.Command.Rop = rop;
  description.Command.SourceConstantAlpha = 0xffu;
  description.Command.SourceHasAlpha = 1u;
  description.SubRects = description.Command.SubRectCount == 0u ? NULL
                                                                 : &sub_rect;
  assert(AppleAgxGdiEncodeDmaCommand(&description, dma, dma_bytes, &written));
  return written;
}

static void test_canonical_lowering_receipts_cover_exact_six_operations(void) {
  static const struct {
    APPLE_AGX_U32 Opcode;
    APPLE_AGX_U32 Rop;
    APPLE_AGX_U32 Flags;
    APPLE_AGX_U32 Required;
  } cases[] = {
      {AppleAgxGdiBitBlt, AppleAgxGdiBitBltSrcCopy, 0u,
       APPLE_AGX_GDI_PRIMITIVE_DESTINATION_WRITE |
           APPLE_AGX_GDI_PRIMITIVE_SOURCE_READ},
      {AppleAgxGdiColorFill, AppleAgxGdiColorFillPatCopy, 0u,
       APPLE_AGX_GDI_PRIMITIVE_DESTINATION_WRITE},
      {AppleAgxGdiAlphaBlend, 0u, 0u,
       APPLE_AGX_GDI_PRIMITIVE_DESTINATION_WRITE |
           APPLE_AGX_GDI_PRIMITIVE_DESTINATION_READ |
           APPLE_AGX_GDI_PRIMITIVE_SOURCE_READ |
           APPLE_AGX_GDI_PRIMITIVE_SOURCE_ALPHA_BLEND},
      {AppleAgxGdiStretchBlt, 0u, 3u,
       APPLE_AGX_GDI_PRIMITIVE_DESTINATION_WRITE |
           APPLE_AGX_GDI_PRIMITIVE_SOURCE_READ |
           APPLE_AGX_GDI_PRIMITIVE_NEAREST_SAMPLE},
      {AppleAgxGdiTransparentBlt, 0u, 0u,
       APPLE_AGX_GDI_PRIMITIVE_DESTINATION_WRITE |
           APPLE_AGX_GDI_PRIMITIVE_SOURCE_READ |
           APPLE_AGX_GDI_PRIMITIVE_COLOR_KEY},
      {AppleAgxGdiClearTypeBlend, 0u, 0u,
       APPLE_AGX_GDI_PRIMITIVE_DESTINATION_WRITE |
           APPLE_AGX_GDI_PRIMITIVE_DESTINATION_READ |
           APPLE_AGX_GDI_PRIMITIVE_CLEARTYPE |
           APPLE_AGX_GDI_PRIMITIVE_AUXILIARY_SURFACES},
  };
  unsigned char dma[512];
  APPLE_AGX_U32 index;

  for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
    APPLE_AGX_GDI_LOWERING_RECEIPT receipt;
    APPLE_AGX_U32 bytes = encode_canonical_command(
        cases[index].Opcode, cases[index].Rop, cases[index].Flags, dma,
        sizeof(dma));

    memset(&receipt, 0xa5, sizeof(receipt));
    assert(AppleAgxGdiBuildLoweringReceipt(dma, bytes, &receipt));
    assert(receipt.StreamHash == test_stream_hash(dma, bytes));
    assert(receipt.StreamBytes == bytes);
    assert(receipt.CommandCount == 1u);
    assert(receipt.OperationMask == APPLE_AGX_GDI_OPCODE_BIT(cases[index].Opcode));
    assert(receipt.RequiredPrimitiveMask == cases[index].Required);
  }
}

static void test_canonical_lowering_receipt_binds_complete_stream(void) {
  unsigned char dma[512];
  APPLE_AGX_GDI_LOWERING_RECEIPT receipt;
  APPLE_AGX_U32 first;
  APPLE_AGX_U32 second;

  first = encode_canonical_command(AppleAgxGdiColorFill,
                                   AppleAgxGdiColorFillPatInvert, 0u,
                                   dma, sizeof(dma));
  second = encode_canonical_command(AppleAgxGdiTransparentBlt, 0u, 1u,
                                    dma + first, sizeof(dma) - first);
  assert(AppleAgxGdiBuildLoweringReceipt(dma, first + second, &receipt));
  assert(receipt.StreamHash == test_stream_hash(dma, first + second));
  assert(receipt.StreamBytes == first + second);
  assert(receipt.CommandCount == 2u);
  assert(receipt.OperationMask ==
         (APPLE_AGX_GDI_OPCODE_BIT(AppleAgxGdiColorFill) |
          APPLE_AGX_GDI_OPCODE_BIT(AppleAgxGdiTransparentBlt)));
  assert(receipt.RequiredPrimitiveMask ==
         (APPLE_AGX_GDI_PRIMITIVE_DESTINATION_WRITE |
          APPLE_AGX_GDI_PRIMITIVE_DESTINATION_READ |
          APPLE_AGX_GDI_PRIMITIVE_BOOLEAN_ROP |
          APPLE_AGX_GDI_PRIMITIVE_SOURCE_READ |
          APPLE_AGX_GDI_PRIMITIVE_COLOR_KEY |
          APPLE_AGX_GDI_PRIMITIVE_SOURCE_ALPHA_BLEND));

  ((APPLE_AGX_GDI_DMA_COMMAND *)(dma + first))->Color ^= 1u;
  {
    APPLE_AGX_GDI_LOWERING_RECEIPT changed;
    assert(AppleAgxGdiBuildLoweringReceipt(dma, first + second, &changed));
    assert(changed.StreamHash != receipt.StreamHash);
  }
}

static void test_full_destination_colorfill_needs_no_subrect_pointer(void) {
  APPLE_AGX_GDI_COMMAND_DESCRIPTION description;
  APPLE_AGX_GDI_LOWERING_RECEIPT receipt;
  unsigned char dma[256];
  APPLE_AGX_U32 written = 0u;

  memset(&description, 0, sizeof(description));
  description.Command.Opcode = AppleAgxGdiColorFill;
  description.Command.Destination =
      (APPLE_AGX_GDI_RECT){0u, 0u, 16u, 16u};
  description.Command.DestinationGpuAddress = 0x1500010000ULL;
  description.Command.DestinationPitch = 64u;
  description.Command.Color = 0xff112233u;
  description.Command.Rop = AppleAgxGdiColorFillPatCopy;
  assert(AppleAgxGdiEncodeDmaCommand(
      &description, dma, sizeof(dma), &written));
  assert(written == sizeof(APPLE_AGX_GDI_DMA_COMMAND));
  assert(AppleAgxGdiBuildLoweringReceipt(dma, written, &receipt));
  assert(receipt.CommandCount == 1u);
  assert(receipt.RequiredPrimitiveMask ==
         APPLE_AGX_GDI_PRIMITIVE_DESTINATION_WRITE);
}

static void assert_lowering_rejected_without_receipt_mutation(
    const unsigned char *dma, APPLE_AGX_U32 bytes) {
  APPLE_AGX_GDI_LOWERING_RECEIPT receipt;
  APPLE_AGX_GDI_LOWERING_RECEIPT before;

  memset(&receipt, 0x5a, sizeof(receipt));
  before = receipt;
  assert(!AppleAgxGdiBuildLoweringReceipt(dma, bytes, &receipt));
  assert(memcmp(&receipt, &before, sizeof(receipt)) == 0);
}

static void test_canonical_lowering_rejects_unsupported_or_malformed_atomically(void) {
  unsigned char dma[512];
  APPLE_AGX_GDI_DMA_COMMAND *command = (APPLE_AGX_GDI_DMA_COMMAND *)dma;
  APPLE_AGX_GDI_RECT *sub_rect;
  APPLE_AGX_U32 bytes;

  bytes = encode_canonical_command(AppleAgxGdiEscape, 0u, 0u, dma,
                                   sizeof(dma));
  assert_lowering_rejected_without_receipt_mutation(dma, bytes);

  bytes = encode_canonical_command(AppleAgxGdiBitBlt,
                                   AppleAgxGdiBitBltRop3, 0u, dma,
                                   sizeof(dma));
  assert_lowering_rejected_without_receipt_mutation(dma, bytes);

  bytes = encode_canonical_command(AppleAgxGdiColorFill,
                                   AppleAgxGdiColorFillRop3, 0u, dma,
                                   sizeof(dma));
  assert_lowering_rejected_without_receipt_mutation(dma, bytes);

  bytes = encode_canonical_command(AppleAgxGdiStretchBlt, 0u, 4u, dma,
                                   sizeof(dma));
  assert_lowering_rejected_without_receipt_mutation(dma, bytes);

  bytes = encode_canonical_command(AppleAgxGdiTransparentBlt, 0u, 2u, dma,
                                   sizeof(dma));
  assert_lowering_rejected_without_receipt_mutation(dma, bytes);

  bytes = encode_canonical_command(AppleAgxGdiAlphaBlend, 0u, 0u, dma,
                                   sizeof(dma));
  command->SourceHasAlpha = 2u;
  assert_lowering_rejected_without_receipt_mutation(dma, bytes);

  bytes = encode_canonical_command(AppleAgxGdiBitBlt,
                                   AppleAgxGdiBitBltSrcCopy, 0u, dma,
                                   sizeof(dma));
  command->SourceGpuAddress = 0u;
  assert_lowering_rejected_without_receipt_mutation(dma, bytes);
  command->SourceGpuAddress = 0x10000ULL;
  command->SourceAllocationIndex = command->DestinationAllocationIndex;
  assert_lowering_rejected_without_receipt_mutation(dma, bytes);
  command->SourceAllocationIndex = 1u;
  command->Destination.Right = command->Destination.Left;
  assert_lowering_rejected_without_receipt_mutation(dma, bytes);
  command->Destination.Right = 110u;
  sub_rect = (APPLE_AGX_GDI_RECT *)(dma + sizeof(*command));
  sub_rect->Right = 111u;
  assert_lowering_rejected_without_receipt_mutation(dma, bytes);
  assert_lowering_rejected_without_receipt_mutation(dma, bytes - 1u);
  assert_lowering_rejected_without_receipt_mutation(NULL, bytes);
  assert(!AppleAgxGdiBuildLoweringReceipt(dma, bytes, NULL));
}

static void test_surface_geometry_is_aligned_and_overflow_safe(void) {
  APPLE_AGX_GDI_SURFACE surface;

  memset(&surface, 0, sizeof(surface));
  assert(AppleAgxGdiDescribeSurface(2560, 1600, 4, 4, &surface));
  assert(surface.Pitch == 10240);
  assert(surface.Size == 16384000ULL);

  assert(AppleAgxGdiDescribeSurface(3, 2, 4, 4, &surface));
  assert(surface.Pitch == 16);
  assert(surface.Size == 32);

  assert(!AppleAgxGdiDescribeSurface(0, 2, 4, 4, &surface));
  assert(!AppleAgxGdiDescribeSurface(2, 0, 4, 4, &surface));
  assert(!AppleAgxGdiDescribeSurface(2, 2, 0, 4, &surface));
  assert(!AppleAgxGdiDescribeSurface(2, 2, 4, 32, &surface));
  assert(!AppleAgxGdiDescribeSurface(0xffffffffu, 2, 4, 4, &surface));
}

static void test_command_stream_requires_defined_framed_commands(void) {
  unsigned char commands[56];
  APPLE_AGX_U32 count = 99;

  memset(commands, 0, sizeof(commands));
  put_u32(commands, AppleAgxGdiColorFill);
  put_u32(commands + 4, 48);
  put_u32(commands + 48, AppleAgxGdiEscape);
  put_u32(commands + 52, 8);
  assert(AppleAgxGdiValidateCommandStream(commands, sizeof(commands), &count));
  assert(count == 2);

  put_u32(commands, 0);
  assert(!AppleAgxGdiValidateCommandStream(commands, sizeof(commands), &count));
  put_u32(commands, AppleAgxGdiColorFill);
  put_u32(commands + 4, 7);
  assert(!AppleAgxGdiValidateCommandStream(commands, sizeof(commands), &count));
  put_u32(commands + 4, 57);
  assert(!AppleAgxGdiValidateCommandStream(commands, sizeof(commands), &count));
  put_u32(commands + 4, 47);
  assert(!AppleAgxGdiValidateCommandStream(commands, sizeof(commands), &count));
  put_u32(commands + 4, 48);
  assert(!AppleAgxGdiValidateCommandStream(commands, 55, &count));
  assert(!AppleAgxGdiValidateCommandStream(commands, 0, &count));
  assert(!AppleAgxGdiValidateCommandStream(commands, sizeof(commands), 0));
}

static void test_command_stream_bounds_variable_subrect_payload(void) {
  unsigned char command[80];
  APPLE_AGX_U32 count = 0;

  memset(command, 0, sizeof(command));
  put_u32(command, AppleAgxGdiColorFill);
  put_u32(command + 4, sizeof(command));
  /* DXGK_GDIARG_COLORFILL.NumSubRects at command byte offset 28. */
  put_u32(command + 28, 2);
  assert(AppleAgxGdiValidateCommandStream(command, sizeof(command), &count));
  assert(count == 1);

  put_u32(command + 28, 3);
  assert(!AppleAgxGdiValidateCommandStream(command, sizeof(command), &count));

  put_u32(command, AppleAgxGdiBitBlt);
  put_u32(command + 4, sizeof(command));
  /* The fixed ARM64 WDK BitBlt command is already 80 bytes. */
  put_u32(command + 48, 1);
  assert(!AppleAgxGdiValidateCommandStream(command, sizeof(command), &count));
}

static void test_dma_records_inline_subrects_without_pointers(void) {
  APPLE_AGX_GDI_COMMAND_DESCRIPTION description;
  APPLE_AGX_GDI_RECT subRects[2];
  APPLE_AGX_GDI_DMA_COMMAND *encoded;
  APPLE_AGX_U32 expectedBytes;
  APPLE_AGX_U32 bytesWritten = 99u;
  APPLE_AGX_U32 commandCount = 99u;
  unsigned char dma[256];

  memset(&description, 0, sizeof(description));
  memset(subRects, 0, sizeof(subRects));
  memset(dma, 0xa5, sizeof(dma));
  description.Command.Opcode = AppleAgxGdiColorFill;
  description.Command.DestinationAllocationIndex = 3u;
  description.Command.Color = 0xff336699u;
  description.Command.Rop = 1u;
  description.Command.Destination.Left = 10u;
  description.Command.Destination.Top = 20u;
  description.Command.Destination.Right = 110u;
  description.Command.Destination.Bottom = 220u;
  description.Command.SubRectCount = 2u;
  subRects[0].Left = 10u;
  subRects[0].Top = 20u;
  subRects[0].Right = 40u;
  subRects[0].Bottom = 50u;
  subRects[1].Left = 41u;
  subRects[1].Top = 51u;
  subRects[1].Right = 110u;
  subRects[1].Bottom = 220u;
  description.SubRects = subRects;

  assert(AppleAgxGdiDmaRecordBytes(2u, &expectedBytes));
  assert(expectedBytes == sizeof(APPLE_AGX_GDI_DMA_COMMAND) +
                              sizeof(subRects));
  assert(AppleAgxGdiEncodeDmaCommand(&description, dma, sizeof(dma),
                                     &bytesWritten));
  assert(bytesWritten == expectedBytes);
  encoded = (APPLE_AGX_GDI_DMA_COMMAND *)dma;
  assert(encoded->Magic == APPLE_AGX_GDI_DMA_MAGIC);
  assert(encoded->Version == APPLE_AGX_GDI_DMA_VERSION);
  assert(encoded->RecordBytes == expectedBytes);
  assert(encoded->Reserved == 0u);
  assert(encoded->DestinationAllocationIndex == 3u);
  assert(encoded->SourceGpuAddress == 0u);
  assert(encoded->DestinationGpuAddress == 0u);
  assert(encoded->Color == 0xff336699u);
  assert(memcmp(dma + sizeof(*encoded), subRects, sizeof(subRects)) == 0);
  assert(AppleAgxGdiValidateDmaStream(dma, bytesWritten, &commandCount));
  assert(commandCount == 1u);

  /* A source pointer is required only while encoding non-empty inline data. */
  description.SubRects = NULL;
  assert(!AppleAgxGdiEncodeDmaCommand(&description, dma, sizeof(dma),
                                      &bytesWritten));
  description.SubRects = subRects;
  assert(!AppleAgxGdiEncodeDmaCommand(&description, dma,
                                      expectedBytes - 1u, &bytesWritten));
  assert(bytesWritten == expectedBytes);

  encoded->RecordBytes -= sizeof(APPLE_AGX_GDI_RECT);
  assert(!AppleAgxGdiValidateDmaStream(dma, expectedBytes, &commandCount));
}

static void test_dma_record_size_rejects_overflow(void) {
  APPLE_AGX_U32 bytes = 0u;

  assert(AppleAgxGdiDmaRecordBytes(0u, &bytes));
  assert(bytes == sizeof(APPLE_AGX_GDI_DMA_COMMAND));
  assert(!AppleAgxGdiDmaRecordBytes(0xffffffffu, &bytes));
  assert(!AppleAgxGdiDmaRecordBytes(1u, NULL));
}

static void test_gdi_pool_is_64k_aligned_zeroed_and_reversible(void) {
  FAKE_GDI_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_GDI_MEMORY_POOL pool;
  unsigned long long index;

  memset(&fake, 0xa5, sizeof(fake));
  memset(&io, 0, sizeof(io));
  memset(&pool, 0, sizeof(pool));
  fake.DeviceBase = 0x20001000ULL;
  fake.AllocateCount = 0u;
  fake.FreeCount = 0u;
  fake.FailAllocation = 0u;
  io.Context = &fake;
  io.AllocateContiguous = allocate_gdi_memory;
  io.FreeContiguous = free_gdi_memory;

  assert(AppleAgxGdiMemoryPoolCreate(&io, GDI_POOL_BYTES, &pool));
  assert(fake.AllocateCount == 1u);
  assert(fake.RequestedBytes == 0x20000ULL);
  assert(pool.Active == APPLE_AGX_TRUE);
  assert(pool.Object.State == AppleAgxMemoryPrepared);
  assert(pool.Object.DeviceAddress == 0x20010000ULL);
  assert((pool.Object.DeviceAddress & 0xffffULL) == 0ULL);
  assert(pool.Object.Length == GDI_POOL_BYTES);
  for (index = 0; index < GDI_POOL_BYTES; ++index)
    assert(((unsigned char *)pool.Object.CpuAddress)[index] == 0u);

  assert(AppleAgxGdiMemoryPoolDestroy(&io, &pool));
  assert(fake.FreeCount == 1u);
  assert(pool.Active == APPLE_AGX_FALSE);
  assert(pool.Object.State == AppleAgxMemoryEmpty);
}

static void test_gdi_pool_rejects_untruthful_size_and_rolls_back(void) {
  FAKE_GDI_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_GDI_MEMORY_POOL pool;

  memset(&fake, 0, sizeof(fake));
  memset(&io, 0, sizeof(io));
  memset(&pool, 0, sizeof(pool));
  fake.DeviceBase = 0x20001000ULL;
  io.Context = &fake;
  io.AllocateContiguous = allocate_gdi_memory;
  io.FreeContiguous = free_gdi_memory;

  assert(!AppleAgxGdiMemoryPoolCreate(&io, 0x4000ULL, &pool));
  assert(fake.AllocateCount == 0u);
  fake.FailAllocation = 1u;
  assert(!AppleAgxGdiMemoryPoolCreate(&io, GDI_POOL_BYTES, &pool));
  assert(fake.AllocateCount == 1u);
  assert(pool.Active == APPLE_AGX_FALSE);
  assert(pool.Object.State == AppleAgxMemoryEmpty);
}

static void test_minimum_wddm_profile_is_exact_and_coherent(void) {
  APPLE_AGX_GDI_CAPS_PROFILE profile;
  APPLE_AGX_U32 opcode;

  AppleAgxGdiMinimumCapsProfile(&profile);
  assert(AppleAgxGdiCapsProfileValid(&profile));
  assert(profile.SupportKernelModeCommandBuffer == APPLE_AGX_TRUE);
  assert(profile.CacheCoherentAperture == APPLE_AGX_TRUE);
  assert(profile.NoCacheCoherentApertureMemory == APPLE_AGX_FALSE);
  for (opcode = (APPLE_AGX_U32)AppleAgxGdiBitBlt;
       opcode <= (APPLE_AGX_U32)AppleAgxGdiClearTypeBlend; ++opcode) {
    if (opcode == (APPLE_AGX_U32)AppleAgxGdiEscape)
      assert(!AppleAgxGdiCapsSupportsOperation(&profile, opcode, 0u));
    else
      assert(AppleAgxGdiCapsSupportsOperation(&profile, opcode, 0u));
  }

  assert(AppleAgxGdiCapsSupportsOperation(
      &profile, AppleAgxGdiBitBlt, AppleAgxGdiBitBltSrcCopy));
  assert(AppleAgxGdiCapsSupportsOperation(
      &profile, AppleAgxGdiBitBlt, AppleAgxGdiBitBltSrcInvert));
  assert(AppleAgxGdiCapsSupportsOperation(
      &profile, AppleAgxGdiBitBlt, AppleAgxGdiBitBltSrcAnd));
  assert(AppleAgxGdiCapsSupportsOperation(
      &profile, AppleAgxGdiBitBlt, AppleAgxGdiBitBltSrcOr));
  assert(!AppleAgxGdiCapsSupportsOperation(
      &profile, AppleAgxGdiBitBlt, AppleAgxGdiBitBltRop3));

  assert(AppleAgxGdiCapsSupportsOperation(
      &profile, AppleAgxGdiColorFill, AppleAgxGdiColorFillPatCopy));
  assert(AppleAgxGdiCapsSupportsOperation(
      &profile, AppleAgxGdiColorFill, AppleAgxGdiColorFillPatInvert));
  assert(AppleAgxGdiCapsSupportsOperation(
      &profile, AppleAgxGdiColorFill, AppleAgxGdiColorFillPdxn));
  assert(AppleAgxGdiCapsSupportsOperation(
      &profile, AppleAgxGdiColorFill, AppleAgxGdiColorFillDstInvert));
  assert(AppleAgxGdiCapsSupportsOperation(
      &profile, AppleAgxGdiColorFill, AppleAgxGdiColorFillPatAnd));
  assert(AppleAgxGdiCapsSupportsOperation(
      &profile, AppleAgxGdiColorFill, AppleAgxGdiColorFillPatOr));
  assert(!AppleAgxGdiCapsSupportsOperation(
      &profile, AppleAgxGdiColorFill, AppleAgxGdiColorFillRop3));

  profile.CacheCoherentAperture = APPLE_AGX_FALSE;
  profile.NoCacheCoherentApertureMemory = APPLE_AGX_TRUE;
  assert(!AppleAgxGdiCapsProfileValid(&profile));
  profile = (APPLE_AGX_GDI_CAPS_PROFILE){0};
  assert(!AppleAgxGdiCapsProfileValid(&profile));
}

int main(void) {
  test_surface_geometry_is_aligned_and_overflow_safe();
  test_command_stream_requires_defined_framed_commands();
  test_command_stream_bounds_variable_subrect_payload();
  test_dma_records_inline_subrects_without_pointers();
  test_dma_record_size_rejects_overflow();
  test_gdi_pool_is_64k_aligned_zeroed_and_reversible();
  test_gdi_pool_rejects_untruthful_size_and_rolls_back();
  test_minimum_wddm_profile_is_exact_and_coherent();
  test_canonical_lowering_receipts_cover_exact_six_operations();
  test_canonical_lowering_receipt_binds_complete_stream();
  test_full_destination_colorfill_needs_no_subrect_pointer();
  test_canonical_lowering_rejects_unsupported_or_malformed_atomically();
  return 0;
}
