#include "apple_agx_driver.h"

#include "j313_agx_g2.generated.h"

static const J313_AGX_G2_INTERRUPT_ROUTE AppleAgxInterruptRoutes[] =
    J313_AGX_G2_INTERRUPT_ROUTE_VALUES;

static BOOLEAN AppleAgxRecordInterrupt(ULONG Vector, BOOLEAN *Seen) {
  ULONG index;

  for (index = 0; index < J313_AGX_G2_INTERRUPT_ROUTE_COUNT; ++index) {
    if (AppleAgxInterruptRoutes[index].GuestIntId != Vector)
      continue;
    if (Seen[index])
      return FALSE;
    Seen[index] = TRUE;
    return TRUE;
  }
  return FALSE;
}

_Use_decl_annotations_ NTSTATUS
AppleAgxValidateTranslatedResources(PCM_RESOURCE_LIST TranslatedResources) {
  BOOLEAN seenInterrupts[J313_AGX_G2_INTERRUPT_ROUTE_COUNT] = {FALSE};
  BOOLEAN seenMemory = FALSE;
  ULONG interruptCount = 0;
  ULONG fullIndex;

  if (TranslatedResources == NULL || TranslatedResources->Count != 1)
    return STATUS_DEVICE_CONFIGURATION_ERROR;

  for (fullIndex = 0; fullIndex < TranslatedResources->Count; ++fullIndex) {
    PCM_FULL_RESOURCE_DESCRIPTOR full = &TranslatedResources->List[fullIndex];
    ULONG partialIndex;

    for (partialIndex = 0; partialIndex < full->PartialResourceList.Count;
         ++partialIndex) {
      PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor =
          &full->PartialResourceList.PartialDescriptors[partialIndex];

      if (descriptor->Type == CmResourceTypeMemory) {
        if (seenMemory ||
            (ULONGLONG)descriptor->u.Memory.Start.QuadPart !=
                J313_AGX_G2_SGX_MMIO_BASE ||
            descriptor->u.Memory.Length != J313_AGX_G2_SGX_MMIO_SIZE)
          return STATUS_DEVICE_CONFIGURATION_ERROR;
        seenMemory = TRUE;
        continue;
      }

      if (descriptor->Type == CmResourceTypeInterrupt) {
        if ((descriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) !=
                CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE ||
            !AppleAgxRecordInterrupt(descriptor->u.Interrupt.Vector,
                                     seenInterrupts))
          return STATUS_DEVICE_CONFIGURATION_ERROR;
        ++interruptCount;
        continue;
      }

      return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
  }

  if (!seenMemory || interruptCount != J313_AGX_G2_INTERRUPT_ROUTE_COUNT)
    return STATUS_DEVICE_CONFIGURATION_ERROR;
  return STATUS_SUCCESS;
}
