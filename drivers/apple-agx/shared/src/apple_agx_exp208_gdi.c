#include "apple_agx_exp208_gdi.h"

#define EXP208_GDI_NULL ((void *)0)

static void Exp208GdiCopy(void *Destination, const void *Source,
                          APPLE_AGX_U32 Bytes) {
  unsigned char *out = (unsigned char *)Destination;
  const unsigned char *in = (const unsigned char *)Source;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    out[index] = in[index];
}

static APPLE_AGX_BOOL Exp208GdiCommandValid(
    const unsigned char *SubmissionBytes,
    APPLE_AGX_U32 SubmissionByteCount,
    APPLE_AGX_U64 DestinationGpuVa) {
  APPLE_AGX_GDI_DMA_COMMAND command;
  APPLE_AGX_GDI_LOWERING_RECEIPT receipt;

  if (SubmissionBytes == EXP208_GDI_NULL ||
      SubmissionByteCount != sizeof(command) ||
      !AppleAgxGdiBuildLoweringReceipt(
          SubmissionBytes, SubmissionByteCount, &receipt) ||
      receipt.CommandCount != 1u ||
      receipt.OperationMask !=
          APPLE_AGX_GDI_OPCODE_BIT(AppleAgxGdiColorFill) ||
      receipt.RequiredPrimitiveMask !=
          APPLE_AGX_GDI_PRIMITIVE_DESTINATION_WRITE)
    return APPLE_AGX_FALSE;
  Exp208GdiCopy(&command, SubmissionBytes, sizeof(command));
  return command.Magic == APPLE_AGX_GDI_DMA_MAGIC &&
                 command.Version == APPLE_AGX_GDI_DMA_VERSION &&
                 command.RecordBytes == sizeof(command) &&
                 command.Opcode == AppleAgxGdiColorFill &&
                 command.SubRectCount == 0u && command.Reserved == 0u &&
                 command.Destination.Left == 0u &&
                 command.Destination.Top == 0u &&
                 command.Destination.Right == APPLE_AGX_EXP208_GDI_WIDTH &&
                 command.Destination.Bottom == APPLE_AGX_EXP208_GDI_HEIGHT &&
                 command.DestinationGpuAddress == DestinationGpuVa &&
                 command.DestinationPitch == APPLE_AGX_EXP208_GDI_PITCH &&
                 command.Color == APPLE_AGX_EXP208_GDI_COLOR &&
                 command.Rop == AppleAgxGdiColorFillPatCopy &&
                 command.Rop3 == 0u && command.Flags == 0u &&
                 command.SourceGpuAddress == 0ULL &&
                 command.TemporaryGpuAddress == 0ULL &&
                 command.GammaGpuAddress == 0ULL &&
                 command.AlphaGpuAddress == 0ULL
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

static APPLE_AGX_BOOL Exp208GdiRelocationsValid(
    const APPLE_AGX_EXP208_RELOCATION *Relocations,
    APPLE_AGX_U32 RelocationCount) {
  APPLE_AGX_U32 index;
  APPLE_AGX_U32 output_edges = 0u;

  if (Relocations == EXP208_GDI_NULL || RelocationCount == 0u)
    return APPLE_AGX_FALSE;
  for (index = 0u; index < RelocationCount; ++index) {
    const APPLE_AGX_EXP208_RELOCATION *relocation =
        &Relocations[index];
    if (relocation->TargetObject !=
        APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT)
      continue;
    if (relocation->AddressSpace != AppleAgxExp208RelocationGpuVa ||
        relocation->Encoding != AppleAgxExp208RelocationExactU64 ||
        relocation->TargetOffset != 0u ||
        relocation->EncodingBits != 0ULL)
      return APPLE_AGX_FALSE;
    ++output_edges;
  }
  return output_edges == 1u ? APPLE_AGX_TRUE : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxExp208BindGdiColorFill(
    const unsigned char *SubmissionBytes,
    APPLE_AGX_U32 SubmissionByteCount,
    void *DestinationCpuAddress,
    APPLE_AGX_U64 DestinationGpuVa,
    APPLE_AGX_U64 DestinationPhysical,
    APPLE_AGX_U32 DestinationCapacity,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount,
    const APPLE_AGX_EXP208_RELOCATION *Relocations,
    APPLE_AGX_U32 RelocationCount,
    APPLE_AGX_EXP208_GDI_BINDING *Binding) {
  APPLE_AGX_EXP208_RELOCATION_OBJECT *output;
  APPLE_AGX_EXP208_GDI_BINDING candidate;

  if (DestinationCpuAddress == EXP208_GDI_NULL ||
      Objects == EXP208_GDI_NULL || Binding == EXP208_GDI_NULL ||
      ObjectCount < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      DestinationGpuVa == 0ULL ||
      (DestinationGpuVa & 0x3fffULL) != 0ULL ||
      DestinationPhysical == 0ULL ||
      (DestinationPhysical & 0x3fffULL) != 0ULL ||
      DestinationPhysical >= (1ULL << 40u) ||
      DestinationCapacity < APPLE_AGX_EXP208_GDI_OUTPUT_BYTES ||
      !Exp208GdiCommandValid(
          SubmissionBytes, SubmissionByteCount, DestinationGpuVa) ||
      !Exp208GdiRelocationsValid(Relocations, RelocationCount))
    return APPLE_AGX_FALSE;
  output = &Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT];
  if (output->GpuVa == 0ULL || output->PhysicalAddress == 0ULL ||
      output->Size != APPLE_AGX_EXP208_GDI_OUTPUT_BYTES ||
      output->Data == EXP208_GDI_NULL)
    return APPLE_AGX_FALSE;

  candidate.OutputObject = APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT;
  candidate.DestinationGpuVa = DestinationGpuVa;
  candidate.DestinationPhysical = DestinationPhysical;
  candidate.DestinationBytes = APPLE_AGX_EXP208_GDI_OUTPUT_BYTES;
  output->GpuVa = DestinationGpuVa;
  output->PhysicalAddress = DestinationPhysical;
  output->Size = APPLE_AGX_EXP208_GDI_OUTPUT_BYTES;
  output->Data = (unsigned char *)DestinationCpuAddress;
  *Binding = candidate;
  return APPLE_AGX_TRUE;
}
