#include "apple_agx_driver.h"

#include "j313_agx_g2.generated.h"

#define J313_AGX_G2_MEMORY_RESOURCE_COUNT 3u
#define J313_AGX_G2_DEVICE_PRIVATE_RESOURCE_COUNT J313_AGX_G2_MEMORY_RESOURCE_COUNT
#define J313_AGX_G2_TRANSLATED_RESOURCE_COUNT                              \
  (J313_AGX_G2_MEMORY_RESOURCE_COUNT +                                     \
   J313_AGX_G2_DEVICE_PRIVATE_RESOURCE_COUNT +                             \
   J313_AGX_G2_INTERRUPT_ROUTE_COUNT)

static BOOLEAN AppleAgxRecordTranslatedInterrupt(ULONG Vector,
                                                 ULONG *SeenVectors,
                                                 ULONG SeenCount) {
  ULONG index;

  if (Vector == 0 || SeenCount >= J313_AGX_G2_INTERRUPT_ROUTE_COUNT)
    return FALSE;
  for (index = 0; index < SeenCount; ++index) {
    if (SeenVectors[index] == Vector)
      return FALSE;
  }
  SeenVectors[SeenCount] = Vector;
  return TRUE;
}

_Use_decl_annotations_ NTSTATUS
AppleAgxValidateTranslatedResources(PCM_RESOURCE_LIST TranslatedResources) {
  ULONG seenInterruptVectors[J313_AGX_G2_INTERRUPT_ROUTE_COUNT] = {0};
  BOOLEAN seenSgxMemory = FALSE;
  BOOLEAN seenGpuMemory = FALSE;
  BOOLEAN seenPowerBrokerMemory = FALSE;
  ULONG devicePrivateCount = 0;
  ULONG interruptCount = 0;
  ULONG fullIndex;

  if (TranslatedResources == NULL || TranslatedResources->Count != 1)
    return STATUS_DEVICE_CONFIGURATION_ERROR;
  if (TranslatedResources->List[0].PartialResourceList.Count !=
      J313_AGX_G2_TRANSLATED_RESOURCE_COUNT)
    return STATUS_DEVICE_CONFIGURATION_ERROR;

  for (fullIndex = 0; fullIndex < TranslatedResources->Count; ++fullIndex) {
    PCM_FULL_RESOURCE_DESCRIPTOR full = &TranslatedResources->List[fullIndex];
    ULONG partialIndex;

    for (partialIndex = 0; partialIndex < full->PartialResourceList.Count;
         ++partialIndex) {
      PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor =
          &full->PartialResourceList.PartialDescriptors[partialIndex];

      if (descriptor->Type == CmResourceTypeMemory) {
        ULONGLONG start = (ULONGLONG)descriptor->u.Memory.Start.QuadPart;

        if (descriptor->ShareDisposition != CmResourceShareDeviceExclusive)
          return STATUS_DEVICE_CONFIGURATION_ERROR;
        if (start == J313_AGX_G2_SGX_MMIO_BASE &&
            descriptor->u.Memory.Length == J313_AGX_G2_SGX_MMIO_SIZE &&
            !seenSgxMemory) {
          seenSgxMemory = TRUE;
          continue;
        }
        if (start == J313_AGX_G2_POWER_BROKER_BASE &&
            descriptor->u.Memory.Length == J313_AGX_G2_POWER_BROKER_SIZE &&
            !seenPowerBrokerMemory) {
          seenPowerBrokerMemory = TRUE;
          continue;
        }
        if (start == J313_AGX_G2_GPU_BASE &&
            descriptor->u.Memory.Length == J313_AGX_G2_GPU_SIZE &&
            !seenGpuMemory) {
          seenGpuMemory = TRUE;
          continue;
        }
        return STATUS_DEVICE_CONFIGURATION_ERROR;
      }

      if (descriptor->Type == CmResourceTypeDevicePrivate) {
        /*
         * The PnP manager owns this payload.  WDM documents it as reserved
         * for system use, so only its bounded presence and ownership are part
         * of our contract; AppleAgx must never interpret its union member.
         */
        if (descriptor->ShareDisposition != CmResourceShareDeviceExclusive ||
            devicePrivateCount >= J313_AGX_G2_DEVICE_PRIVATE_RESOURCE_COUNT)
          return STATUS_DEVICE_CONFIGURATION_ERROR;
        ++devicePrivateCount;
        continue;
      }

      if (descriptor->Type == CmResourceTypeInterrupt) {
        if (descriptor->ShareDisposition != CmResourceShareDeviceExclusive ||
            descriptor->Flags != CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE ||
            descriptor->u.Interrupt.Affinity == 0 ||
            !AppleAgxRecordTranslatedInterrupt(
                descriptor->u.Interrupt.Vector, seenInterruptVectors,
                interruptCount))
          return STATUS_DEVICE_CONFIGURATION_ERROR;
        ++interruptCount;
        continue;
      }

      return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
  }

  if (!seenSgxMemory || !seenGpuMemory || !seenPowerBrokerMemory ||
      devicePrivateCount != J313_AGX_G2_DEVICE_PRIVATE_RESOURCE_COUNT ||
      interruptCount != J313_AGX_G2_INTERRUPT_ROUTE_COUNT)
    return STATUS_DEVICE_CONFIGURATION_ERROR;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AppleAgxGetGpuRegionAddress(
    PCM_RESOURCE_LIST TranslatedResources,
    PPHYSICAL_ADDRESS GpuRegionAddress) {
  ULONG fullIndex;

  if (GpuRegionAddress == NULL)
    return STATUS_INVALID_PARAMETER;
  if (!NT_SUCCESS(AppleAgxValidateTranslatedResources(TranslatedResources)))
    return STATUS_DEVICE_CONFIGURATION_ERROR;

  for (fullIndex = 0; fullIndex < TranslatedResources->Count; ++fullIndex) {
    PCM_FULL_RESOURCE_DESCRIPTOR full = &TranslatedResources->List[fullIndex];
    ULONG partialIndex;

    for (partialIndex = 0; partialIndex < full->PartialResourceList.Count;
         ++partialIndex) {
      PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor =
          &full->PartialResourceList.PartialDescriptors[partialIndex];
      if (descriptor->Type == CmResourceTypeMemory &&
          (ULONGLONG)descriptor->u.Memory.Start.QuadPart ==
              J313_AGX_G2_GPU_BASE &&
          descriptor->u.Memory.Length == J313_AGX_G2_GPU_SIZE) {
        *GpuRegionAddress = descriptor->u.Memory.Start;
        return STATUS_SUCCESS;
      }
    }
  }
  return STATUS_DEVICE_CONFIGURATION_ERROR;
}

_Use_decl_annotations_ NTSTATUS AppleAgxGetPowerBrokerAddress(
    PCM_RESOURCE_LIST TranslatedResources,
    PPHYSICAL_ADDRESS PowerBrokerAddress) {
  ULONG fullIndex;

  if (PowerBrokerAddress == NULL)
    return STATUS_INVALID_PARAMETER;
  if (!NT_SUCCESS(AppleAgxValidateTranslatedResources(TranslatedResources)))
    return STATUS_DEVICE_CONFIGURATION_ERROR;

  for (fullIndex = 0; fullIndex < TranslatedResources->Count; ++fullIndex) {
    PCM_FULL_RESOURCE_DESCRIPTOR full = &TranslatedResources->List[fullIndex];
    ULONG partialIndex;

    for (partialIndex = 0; partialIndex < full->PartialResourceList.Count;
         ++partialIndex) {
      PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor =
          &full->PartialResourceList.PartialDescriptors[partialIndex];
      if (descriptor->Type == CmResourceTypeMemory &&
          (ULONGLONG)descriptor->u.Memory.Start.QuadPart ==
              J313_AGX_G2_POWER_BROKER_BASE &&
          descriptor->u.Memory.Length == J313_AGX_G2_POWER_BROKER_SIZE) {
        *PowerBrokerAddress = descriptor->u.Memory.Start;
        return STATUS_SUCCESS;
      }
    }
  }
  return STATUS_DEVICE_CONFIGURATION_ERROR;
}
