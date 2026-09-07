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

static APPLE_AGX_U64 Exp208GdiReadU64(const unsigned char *Address) {
  APPLE_AGX_U64 value = 0ULL;
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    value |= (APPLE_AGX_U64)Address[index] << (index * 8u);
  return value;
}

static void Exp208GdiWriteU64(unsigned char *Address, APPLE_AGX_U64 Value) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    Address[index] = (unsigned char)(Value >> (index * 8u));
}

static APPLE_AGX_U64 Exp208GdiUscAddress(APPLE_AGX_U64 Word) {
  return ((Word >> APPLE_AGX_EXP208_GDI_USC_ADDRESS_SHIFT) &
          0xfffffffffULL) << 3u;
}

static APPLE_AGX_BOOL Exp208GdiPatchUscAddress(
    APPLE_AGX_U64 Word, APPLE_AGX_U64 CapturedWord,
    APPLE_AGX_U64 Address, APPLE_AGX_U64 *Patched) {
  if (Patched == EXP208_GDI_NULL ||
      (Word & ~APPLE_AGX_EXP208_GDI_USC_ADDRESS_MASK) !=
          (CapturedWord & ~APPLE_AGX_EXP208_GDI_USC_ADDRESS_MASK) ||
      Exp208GdiUscAddress(Word) != Exp208GdiUscAddress(CapturedWord) ||
      (Address & 0x7ULL) != 0ULL ||
      (Address >> 3u) > 0xfffffffffULL)
    return APPLE_AGX_FALSE;
  *Patched =
      (Word & ~APPLE_AGX_EXP208_GDI_USC_ADDRESS_MASK) |
      ((Address >> 3u) << APPLE_AGX_EXP208_GDI_USC_ADDRESS_SHIFT);
  return APPLE_AGX_TRUE;
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
  APPLE_AGX_EXP208_RELOCATION_OBJECT *descriptor;
  APPLE_AGX_EXP208_RELOCATION_OBJECT *pipeline;
  APPLE_AGX_EXP208_GDI_BINDING candidate;
  APPLE_AGX_U64 descriptorWord;
  APPLE_AGX_U64 patchedDescriptor;
  APPLE_AGX_U64 textureWord;
  APPLE_AGX_U64 uniformWord;
  APPLE_AGX_U64 clearUniformWord;
  APPLE_AGX_U64 patchedTexture;
  APPLE_AGX_U64 patchedUniform;
  APPLE_AGX_U64 patchedClearUniform;
  APPLE_AGX_U64 descriptorGpuVa;

  if (DestinationCpuAddress == EXP208_GDI_NULL ||
      Objects == EXP208_GDI_NULL || Binding == EXP208_GDI_NULL ||
      ObjectCount < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      DestinationGpuVa == 0ULL ||
      (DestinationGpuVa & 0x3fffULL) != 0ULL ||
      (DestinationGpuVa & 0xfULL) != 0ULL ||
      (DestinationGpuVa >> 4u) >
          APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_ADDRESS_MASK ||
      DestinationPhysical == 0ULL ||
      (DestinationPhysical & 0x3fffULL) != 0ULL ||
      DestinationPhysical >= (1ULL << 40u) ||
      DestinationCapacity < APPLE_AGX_EXP208_GDI_OUTPUT_BYTES ||
      !Exp208GdiCommandValid(
          SubmissionBytes, SubmissionByteCount, DestinationGpuVa) ||
      !Exp208GdiRelocationsValid(Relocations, RelocationCount))
    return APPLE_AGX_FALSE;
  output = &Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT];
  descriptor = &Objects[APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OBJECT];
  pipeline = &Objects[APPLE_AGX_EXP208_GDI_STORE_PIPELINE_OBJECT];
  if (output->GpuVa == 0ULL || output->PhysicalAddress == 0ULL ||
      output->Size != APPLE_AGX_EXP208_GDI_OUTPUT_BYTES ||
      output->Data == EXP208_GDI_NULL || descriptor->Data == EXP208_GDI_NULL ||
      descriptor->Size <
          APPLE_AGX_EXP208_GDI_UNIFORM_DATA_OFFSET + 8u ||
      descriptor->GpuVa == 0ULL ||
      descriptor->GpuVa >
          ~0ULL - APPLE_AGX_EXP208_GDI_UNIFORM_DATA_OFFSET ||
      pipeline->Data == EXP208_GDI_NULL ||
      pipeline->Size < APPLE_AGX_EXP208_GDI_STORE_UNIFORM_OFFSET + 8u)
    return APPLE_AGX_FALSE;

  descriptorWord = Exp208GdiReadU64(
      descriptor->Data + APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET);
  if ((descriptorWord &
       APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_ADDRESS_MASK) << 4u !=
      APPLE_AGX_EXP208_GDI_CAPTURED_OUTPUT_GPU_VA)
    return APPLE_AGX_FALSE;
  patchedDescriptor =
      (descriptorWord &
       ~APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_ADDRESS_MASK) |
      (DestinationGpuVa >> 4u);
  textureWord = Exp208GdiReadU64(
      pipeline->Data + APPLE_AGX_EXP208_GDI_STORE_TEXTURE_OFFSET);
  uniformWord = Exp208GdiReadU64(
      pipeline->Data + APPLE_AGX_EXP208_GDI_STORE_UNIFORM_OFFSET);
  clearUniformWord = Exp208GdiReadU64(
      pipeline->Data + APPLE_AGX_EXP208_GDI_CLEAR_UNIFORM_OFFSET);
  descriptorGpuVa = descriptor->GpuVa;
  if (!Exp208GdiPatchUscAddress(
          clearUniformWord,
          APPLE_AGX_EXP208_GDI_CAPTURED_CLEAR_UNIFORM_WORD,
          descriptorGpuVa, &patchedClearUniform) ||
      !Exp208GdiPatchUscAddress(
          textureWord, APPLE_AGX_EXP208_GDI_CAPTURED_TEXTURE_WORD,
          descriptorGpuVa + APPLE_AGX_EXP208_GDI_TEXTURE_DESCRIPTOR_OFFSET,
          &patchedTexture) ||
      !Exp208GdiPatchUscAddress(
          uniformWord, APPLE_AGX_EXP208_GDI_CAPTURED_UNIFORM_WORD,
          descriptorGpuVa + APPLE_AGX_EXP208_GDI_UNIFORM_DATA_OFFSET,
          &patchedUniform))
    return APPLE_AGX_FALSE;

  candidate.OutputObject = APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT;
  candidate.DestinationGpuVa = DestinationGpuVa;
  candidate.DestinationPhysical = DestinationPhysical;
  candidate.DestinationBytes = APPLE_AGX_EXP208_GDI_OUTPUT_BYTES;
  candidate.StoreDescriptorObject =
      APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OBJECT;
  candidate.StoreDescriptorOffset =
      APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET;
  candidate.OriginalOutputGpuVa = output->GpuVa;
  candidate.OriginalOutputPhysical = output->PhysicalAddress;
  candidate.OriginalOutputBytes = output->Size;
  candidate.OriginalOutputData = output->Data;
  candidate.OriginalStoreDescriptor = descriptorWord;
  candidate.PatchedStoreDescriptor = patchedDescriptor;
  candidate.StorePipelineObject =
      APPLE_AGX_EXP208_GDI_STORE_PIPELINE_OBJECT;
  candidate.ClearUniformOffset =
      APPLE_AGX_EXP208_GDI_CLEAR_UNIFORM_OFFSET;
  candidate.StoreTextureOffset = APPLE_AGX_EXP208_GDI_STORE_TEXTURE_OFFSET;
  candidate.StoreUniformOffset = APPLE_AGX_EXP208_GDI_STORE_UNIFORM_OFFSET;
  candidate.OriginalStoreTexture = textureWord;
  candidate.PatchedStoreTexture = patchedTexture;
  candidate.OriginalStoreUniform = uniformWord;
  candidate.PatchedStoreUniform = patchedUniform;
  candidate.OriginalClearUniform = clearUniformWord;
  candidate.PatchedClearUniform = patchedClearUniform;
  output->GpuVa = DestinationGpuVa;
  output->PhysicalAddress = DestinationPhysical;
  output->Size = APPLE_AGX_EXP208_GDI_OUTPUT_BYTES;
  output->Data = (unsigned char *)DestinationCpuAddress;
  Exp208GdiWriteU64(
      descriptor->Data + APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET,
      patchedDescriptor);
  Exp208GdiWriteU64(
      pipeline->Data + APPLE_AGX_EXP208_GDI_STORE_TEXTURE_OFFSET,
      patchedTexture);
  Exp208GdiWriteU64(
      pipeline->Data + APPLE_AGX_EXP208_GDI_STORE_UNIFORM_OFFSET,
      patchedUniform);
  Exp208GdiWriteU64(
      pipeline->Data + APPLE_AGX_EXP208_GDI_CLEAR_UNIFORM_OFFSET,
      patchedClearUniform);
  *Binding = candidate;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxExp208UnbindGdiColorFill(
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount,
    const APPLE_AGX_EXP208_GDI_BINDING *Binding) {
  APPLE_AGX_EXP208_RELOCATION_OBJECT *output;
  APPLE_AGX_EXP208_RELOCATION_OBJECT *descriptor;
  APPLE_AGX_EXP208_RELOCATION_OBJECT *pipeline;
  if (Objects == EXP208_GDI_NULL || Binding == EXP208_GDI_NULL ||
      ObjectCount < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      Binding->OutputObject != APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT ||
      Binding->StoreDescriptorObject !=
          APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OBJECT ||
      Binding->StoreDescriptorOffset !=
          APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET ||
      Binding->StorePipelineObject !=
          APPLE_AGX_EXP208_GDI_STORE_PIPELINE_OBJECT ||
      Binding->ClearUniformOffset !=
          APPLE_AGX_EXP208_GDI_CLEAR_UNIFORM_OFFSET ||
      Binding->StoreTextureOffset !=
          APPLE_AGX_EXP208_GDI_STORE_TEXTURE_OFFSET ||
      Binding->StoreUniformOffset !=
          APPLE_AGX_EXP208_GDI_STORE_UNIFORM_OFFSET)
    return APPLE_AGX_FALSE;
  output = &Objects[Binding->OutputObject];
  descriptor = &Objects[Binding->StoreDescriptorObject];
  pipeline = &Objects[Binding->StorePipelineObject];
  if (output->GpuVa != Binding->DestinationGpuVa ||
      output->PhysicalAddress != Binding->DestinationPhysical ||
      output->Size != Binding->DestinationBytes ||
      descriptor->Data == EXP208_GDI_NULL ||
      descriptor->Size < Binding->StoreDescriptorOffset + 8u ||
      Exp208GdiReadU64(descriptor->Data + Binding->StoreDescriptorOffset) !=
          Binding->PatchedStoreDescriptor ||
      pipeline->Data == EXP208_GDI_NULL ||
      pipeline->Size < Binding->StoreUniformOffset + 8u ||
      Exp208GdiReadU64(pipeline->Data + Binding->ClearUniformOffset) !=
          Binding->PatchedClearUniform ||
      Exp208GdiReadU64(pipeline->Data + Binding->StoreTextureOffset) !=
          Binding->PatchedStoreTexture ||
      Exp208GdiReadU64(pipeline->Data + Binding->StoreUniformOffset) !=
          Binding->PatchedStoreUniform)
    return APPLE_AGX_FALSE;
  output->GpuVa = Binding->OriginalOutputGpuVa;
  output->PhysicalAddress = Binding->OriginalOutputPhysical;
  output->Size = Binding->OriginalOutputBytes;
  output->Data = Binding->OriginalOutputData;
  Exp208GdiWriteU64(descriptor->Data + Binding->StoreDescriptorOffset,
                    Binding->OriginalStoreDescriptor);
  Exp208GdiWriteU64(pipeline->Data + Binding->StoreTextureOffset,
                    Binding->OriginalStoreTexture);
  Exp208GdiWriteU64(pipeline->Data + Binding->StoreUniformOffset,
                    Binding->OriginalStoreUniform);
  Exp208GdiWriteU64(pipeline->Data + Binding->ClearUniformOffset,
                    Binding->OriginalClearUniform);
  return APPLE_AGX_TRUE;
}
