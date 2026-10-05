#include "apple_agx_device_control.h"

#include "apple_agx_channel_memory.h"
#include "apple_agx_platform_provider.h"

#define APPLE_AGX_DEVICE_CONTROL_DESTROY_CONTEXT_TYPE 0x18u
#define APPLE_AGX_DEVICE_CONTROL_INIT_TYPE 0x1au
#define APPLE_AGX_DEVICE_CONTROL_UPDATE_IDLE_TIMESTAMP_TYPE 0x23u
#define APPLE_AGX_DEVICE_CONTROL_NULL ((void *)0)

static unsigned char AppleAgxDeviceControlDestinationIsZero(
    const unsigned char *Destination, unsigned int DestinationSize) {
  unsigned int index;
  for (index = 0u; index < DestinationSize; ++index) {
    if (Destination[index] != 0u)
      return 0u;
  }
  return 1u;
}

static APPLE_AGX_DEVICE_CONTROL_RESULT AppleAgxDeviceControlEncode(
    unsigned int Type, unsigned char *Destination,
    unsigned int DestinationSize) {
  if (Destination == 0)
    return AppleAgxDeviceControlResultInvalidArgument;
  if (DestinationSize != APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE)
    return AppleAgxDeviceControlResultDestinationSize;
  if (AppleAgxDeviceControlDestinationIsZero(Destination,
                                              DestinationSize) == 0u)
    return AppleAgxDeviceControlResultDestinationNotZero;
  if (J313_AGX_G2_DEVCTRL_RING_SIZE !=
          APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE *
              APPLE_AGX_DEVICE_CONTROL_ENTRY_COUNT ||
      J313_AGX_G2_CHANNEL_STATE_STRIDE != 0x30u)
    return AppleAgxDeviceControlResultUnsupportedVersion;

  Destination[0] = (unsigned char)(Type & 0xffu);
  Destination[1] = (unsigned char)((Type >> 8u) & 0xffu);
  Destination[2] = (unsigned char)((Type >> 16u) & 0xffu);
  Destination[3] = (unsigned char)((Type >> 24u) & 0xffu);
  return AppleAgxDeviceControlResultOk;
}

static void AppleAgxDeviceControlWriteU32(unsigned char *Destination,
                                          unsigned int Value) {
  Destination[0] = (unsigned char)(Value & 0xffu);
  Destination[1] = (unsigned char)((Value >> 8u) & 0xffu);
  Destination[2] = (unsigned char)((Value >> 16u) & 0xffu);
  Destination[3] = (unsigned char)((Value >> 24u) & 0xffu);
}

static void AppleAgxDeviceControlWriteU64(unsigned char *Destination,
                                          unsigned long long Value) {
  AppleAgxDeviceControlWriteU32(Destination, (unsigned int)Value);
  AppleAgxDeviceControlWriteU32(Destination + 4u,
                                (unsigned int)(Value >> 32u));
}

static void AppleAgxDeviceControlCopy(unsigned char *Destination,
                                      const unsigned char *Source,
                                      unsigned int Bytes) {
  unsigned int index;
  for (index = 0u; index < Bytes; ++index)
    Destination[index] = Source[index];
}

APPLE_AGX_DEVICE_CONTROL_RESULT AppleAgxDeviceControlEncodeInitG13V13_5(
    unsigned char *Destination, unsigned int DestinationSize) {
  return AppleAgxDeviceControlEncode(APPLE_AGX_DEVICE_CONTROL_INIT_TYPE,
                                     Destination, DestinationSize);
}

APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlEncodeUpdateIdleTimestampG13V13_5(
    unsigned char *Destination, unsigned int DestinationSize) {
  return AppleAgxDeviceControlEncode(
      APPLE_AGX_DEVICE_CONTROL_UPDATE_IDLE_TIMESTAMP_TYPE, Destination,
      DestinationSize);
}

APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlEncodeDestroyContextG13V13_5(
    unsigned long long ContextGpuAddress, unsigned char *Destination,
    unsigned int DestinationSize) {
  APPLE_AGX_DEVICE_CONTROL_RESULT result;
  if (ContextGpuAddress != APPLE_AGX_DEVICE_CONTROL_EXP208_CONTEXT_GPU_VA)
    return AppleAgxDeviceControlResultContextAddress;
  result = AppleAgxDeviceControlEncode(
      APPLE_AGX_DEVICE_CONTROL_DESTROY_CONTEXT_TYPE, Destination,
      DestinationSize);
  if (result != AppleAgxDeviceControlResultOk)
    return result;
  AppleAgxDeviceControlWriteU32(Destination + 0x08u, 2u);
  AppleAgxDeviceControlWriteU32(Destination + 0x14u, 0xffffu);
  AppleAgxDeviceControlWriteU64(Destination + 0x1cu, ContextGpuAddress);
  return AppleAgxDeviceControlResultOk;
}

APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlPlanPublicationG13V13_5(
    unsigned int CurrentWritePointer,
    APPLE_AGX_DEVICE_CONTROL_PUBLICATION *Publication) {
  unsigned long long doorbell;

  if (Publication == 0)
    return AppleAgxDeviceControlResultInvalidArgument;
  if (CurrentWritePointer >= APPLE_AGX_DEVICE_CONTROL_ENTRY_COUNT)
    return AppleAgxDeviceControlResultPointer;
  if (J313_AGX_G2_DEVCTRL_RING_SIZE !=
          APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE *
              APPLE_AGX_DEVICE_CONTROL_ENTRY_COUNT ||
      J313_AGX_G2_CHANNEL_STATE_STRIDE != 0x30u)
    return AppleAgxDeviceControlResultUnsupportedVersion;

  doorbell =
      AppleAgxRtkitDoorbell(APPLE_AGX_DEVICE_CONTROL_DOORBELL_CHANNEL);
  if (doorbell == APPLE_AGX_RTKIT_INVALID_MESSAGE)
    return AppleAgxDeviceControlResultUnsupportedVersion;

  Publication->SlotOffset =
      CurrentWritePointer * APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE;
  Publication->NextWritePointer =
      (CurrentWritePointer + 1u) % APPLE_AGX_DEVICE_CONTROL_ENTRY_COUNT;
  Publication->StateReadPointerOffset =
      APPLE_AGX_DEVICE_CONTROL_STATE_READ_POINTER_OFFSET;
  Publication->StateWritePointerOffset =
      APPLE_AGX_DEVICE_CONTROL_STATE_WRITE_POINTER_OFFSET;
  Publication->DoorbellEndpoint =
      APPLE_AGX_DEVICE_CONTROL_DOORBELL_ENDPOINT;
  Publication->DoorbellChannel =
      APPLE_AGX_DEVICE_CONTROL_DOORBELL_CHANNEL;
  Publication->DoorbellMessage = doorbell;
  Publication->ChannelInfoIndex =
      APPLE_AGX_DEVICE_CONTROL_CHANNEL_INFO_INDEX;
  Publication->ReceiptCookie = Publication->NextWritePointer;
  Publication->StateCpuAddress = APPLE_AGX_DEVICE_CONTROL_NULL;
  Publication->RingCpuAddress = APPLE_AGX_DEVICE_CONTROL_NULL;
  return AppleAgxDeviceControlResultOk;
}

static unsigned char AppleAgxDeviceControlObjectReady(
    const APPLE_AGX_MEMORY_OBJECT *Object, unsigned long long MinimumLength) {
  return Object != APPLE_AGX_DEVICE_CONTROL_NULL &&
         Object->CpuAddress != APPLE_AGX_DEVICE_CONTROL_NULL &&
         Object->Length >= MinimumLength &&
         Object->State == AppleAgxMemoryGpuMapped;
}

static APPLE_AGX_DEVICE_CONTROL_RESULT AppleAgxDeviceControlBindChannel12(
    const APPLE_AGX_CHANNEL_MEMORY_OWNER *ChannelMemory,
    APPLE_AGX_DEVICE_CONTROL_PUBLICATION *Publication) {
  const APPLE_AGX_MEMORY_OBJECT *state;
  const APPLE_AGX_MEMORY_OBJECT *ring;
  if (ChannelMemory == APPLE_AGX_DEVICE_CONTROL_NULL ||
      Publication == APPLE_AGX_DEVICE_CONTROL_NULL ||
      !ChannelMemory->Initialized || !ChannelMemory->Built ||
      ChannelMemory->ObjectCount != APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT)
    return AppleAgxDeviceControlResultChannelBinding;
  state = &ChannelMemory->Objects[AppleAgxChannelMemoryDevctrlState];
  ring = &ChannelMemory->Objects[AppleAgxChannelMemoryDevctrlRing];
  if (!AppleAgxDeviceControlObjectReady(
          state, J313_AGX_G2_CHANNEL_STATE_STRIDE) ||
      !AppleAgxDeviceControlObjectReady(ring, J313_AGX_G2_DEVCTRL_RING_SIZE) ||
      ChannelMemory
              ->ChannelInfo
              .Entries[APPLE_AGX_DEVICE_CONTROL_CHANNEL_INFO_INDEX]
              .StateAddress !=
          ChannelMemory
              ->VirtualAddresses[AppleAgxChannelMemoryDevctrlState] ||
      ChannelMemory
              ->ChannelInfo
              .Entries[APPLE_AGX_DEVICE_CONTROL_CHANNEL_INFO_INDEX]
              .RingAddress !=
          ChannelMemory->VirtualAddresses[AppleAgxChannelMemoryDevctrlRing])
    return AppleAgxDeviceControlResultChannelBinding;
  Publication->StateCpuAddress = (unsigned char *)state->CpuAddress;
  Publication->RingCpuAddress = (unsigned char *)ring->CpuAddress;
  return AppleAgxDeviceControlResultOk;
}

static unsigned char AppleAgxDeviceControlTransportValid(
    const APPLE_AGX_PLATFORM_TRANSPORT_IO *Transport) {
  return Transport != APPLE_AGX_DEVICE_CONTROL_NULL &&
         Transport->Context != APPLE_AGX_DEVICE_CONTROL_NULL &&
         Transport->FlushForDevice != APPLE_AGX_DEVICE_CONTROL_NULL &&
         Transport->FlushForCpu != APPLE_AGX_DEVICE_CONTROL_NULL &&
         Transport->MemoryBarrier != APPLE_AGX_DEVICE_CONTROL_NULL &&
         Transport->PublishU32 != APPLE_AGX_DEVICE_CONTROL_NULL &&
         Transport->ReadU32 != APPLE_AGX_DEVICE_CONTROL_NULL &&
         Transport->RingDoorbell != APPLE_AGX_DEVICE_CONTROL_NULL;
}

static APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlPublishEncodedG13V13_5(
    const APPLE_AGX_CHANNEL_MEMORY_OWNER *ChannelMemory,
    const APPLE_AGX_PLATFORM_TRANSPORT_IO *Transport,
    const unsigned char *Message,
    APPLE_AGX_DEVICE_CONTROL_PUBLICATION *Publication) {
  APPLE_AGX_DEVICE_CONTROL_RESULT result;
  APPLE_AGX_DEVICE_CONTROL_PUBLICATION candidate;
  APPLE_AGX_BACKEND_U32 read_pointer;
  APPLE_AGX_BACKEND_U32 write_pointer;
  unsigned char *slot;

  if (Message == APPLE_AGX_DEVICE_CONTROL_NULL ||
      Publication == APPLE_AGX_DEVICE_CONTROL_NULL ||
      !AppleAgxDeviceControlTransportValid(Transport))
    return AppleAgxDeviceControlResultInvalidArgument;
  result = AppleAgxDeviceControlPlanPublicationG13V13_5(0u, &candidate);
  if (result != AppleAgxDeviceControlResultOk)
    return result;
  result = AppleAgxDeviceControlBindChannel12(ChannelMemory, &candidate);
  if (result != AppleAgxDeviceControlResultOk)
    return result;
  if (!Transport->FlushForCpu(Transport->Context, candidate.StateCpuAddress,
                              J313_AGX_G2_CHANNEL_STATE_STRIDE))
    return AppleAgxDeviceControlResultTransportFailed;
  Transport->MemoryBarrier(Transport->Context);
  if (!Transport->ReadU32(
          Transport->Context,
          (const volatile APPLE_AGX_BACKEND_U32 *)(
              candidate.StateCpuAddress +
              APPLE_AGX_DEVICE_CONTROL_STATE_READ_POINTER_OFFSET),
          &read_pointer) ||
      !Transport->ReadU32(
          Transport->Context,
          (const volatile APPLE_AGX_BACKEND_U32 *)(
              candidate.StateCpuAddress +
              APPLE_AGX_DEVICE_CONTROL_STATE_WRITE_POINTER_OFFSET),
          &write_pointer))
    return AppleAgxDeviceControlResultTransportFailed;
  if (read_pointer >= APPLE_AGX_DEVICE_CONTROL_ENTRY_COUNT)
    return AppleAgxDeviceControlResultPointer;
  result = AppleAgxDeviceControlPlanPublicationG13V13_5(write_pointer,
                                                        &candidate);
  if (result != AppleAgxDeviceControlResultOk)
    return result;
  result = AppleAgxDeviceControlBindChannel12(ChannelMemory, &candidate);
  if (result != AppleAgxDeviceControlResultOk)
    return result;
  if (candidate.NextWritePointer == read_pointer)
    return AppleAgxDeviceControlResultRingFull;
  slot = candidate.RingCpuAddress + candidate.SlotOffset;
  AppleAgxDeviceControlCopy(slot, Message,
                            APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE);
  if (!Transport->FlushForDevice(
          Transport->Context, slot,
          APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE))
    return AppleAgxDeviceControlResultTransportFailed;
  Transport->MemoryBarrier(Transport->Context);
  if (!Transport->PublishU32(
          Transport->Context,
          (volatile APPLE_AGX_BACKEND_U32 *)(
              candidate.StateCpuAddress +
              APPLE_AGX_DEVICE_CONTROL_STATE_WRITE_POINTER_OFFSET),
          candidate.NextWritePointer))
    return AppleAgxDeviceControlResultTransportFailed;
  Transport->MemoryBarrier(Transport->Context);
  if (!Transport->RingDoorbell(Transport->Context,
                               candidate.DoorbellChannel))
    return AppleAgxDeviceControlResultTransportFailed;
  *Publication = candidate;
  return AppleAgxDeviceControlResultOk;
}

APPLE_AGX_DEVICE_CONTROL_RESULT AppleAgxDeviceControlPublishInitG13V13_5(
    const APPLE_AGX_CHANNEL_MEMORY_OWNER *ChannelMemory,
    const APPLE_AGX_PLATFORM_TRANSPORT_IO *Transport,
    APPLE_AGX_DEVICE_CONTROL_PUBLICATION *Publication) {
  unsigned char message[APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE] = {0};
  APPLE_AGX_DEVICE_CONTROL_RESULT result =
      AppleAgxDeviceControlEncodeInitG13V13_5(message, sizeof(message));
  return result == AppleAgxDeviceControlResultOk
             ? AppleAgxDeviceControlPublishEncodedG13V13_5(
                   ChannelMemory, Transport, message, Publication)
             : result;
}

APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlPublishUpdateIdleTimestampG13V13_5(
    const APPLE_AGX_CHANNEL_MEMORY_OWNER *ChannelMemory,
    const APPLE_AGX_PLATFORM_TRANSPORT_IO *Transport,
    APPLE_AGX_DEVICE_CONTROL_PUBLICATION *Publication) {
  unsigned char message[APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE] = {0};
  APPLE_AGX_DEVICE_CONTROL_RESULT result =
      AppleAgxDeviceControlEncodeUpdateIdleTimestampG13V13_5(
          message, sizeof(message));
  return result == AppleAgxDeviceControlResultOk
             ? AppleAgxDeviceControlPublishEncodedG13V13_5(
                   ChannelMemory, Transport, message, Publication)
             : result;
}

APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlPublishDestroyContextG13V13_5(
    const APPLE_AGX_CHANNEL_MEMORY_OWNER *ChannelMemory,
    const APPLE_AGX_PLATFORM_TRANSPORT_IO *Transport,
    unsigned long long ContextGpuAddress,
    APPLE_AGX_DEVICE_CONTROL_PUBLICATION *Publication) {
  unsigned char message[APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE] = {0};
  APPLE_AGX_DEVICE_CONTROL_RESULT result;
  if (ContextGpuAddress != APPLE_AGX_DEVICE_CONTROL_EXP208_CONTEXT_GPU_VA)
    return AppleAgxDeviceControlResultContextAddress;
  result = AppleAgxDeviceControlEncodeDestroyContextG13V13_5(
      ContextGpuAddress, message, sizeof(message));
  return result == AppleAgxDeviceControlResultOk
             ? AppleAgxDeviceControlPublishEncodedG13V13_5(
                   ChannelMemory, Transport, message, Publication)
             : result;
}

APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlWaitForReceiptG13V13_5(
    const APPLE_AGX_DEVICE_CONTROL_PUBLICATION *Publication,
    const APPLE_AGX_PLATFORM_TRANSPORT_IO *Transport,
    unsigned int MaximumPolls, unsigned int *CompletedPolls) {
  APPLE_AGX_BACKEND_U32 read_pointer;
  unsigned int poll;
  if (Publication == APPLE_AGX_DEVICE_CONTROL_NULL ||
      CompletedPolls == APPLE_AGX_DEVICE_CONTROL_NULL ||
      MaximumPolls == 0u ||
      Publication->StateCpuAddress == APPLE_AGX_DEVICE_CONTROL_NULL ||
      Publication->ChannelInfoIndex !=
          APPLE_AGX_DEVICE_CONTROL_CHANNEL_INFO_INDEX ||
      Publication->ReceiptCookie >= APPLE_AGX_DEVICE_CONTROL_ENTRY_COUNT ||
      !AppleAgxDeviceControlTransportValid(Transport))
    return AppleAgxDeviceControlResultInvalidArgument;
  *CompletedPolls = 0u;
  for (poll = 0u; poll < MaximumPolls; ++poll) {
    if (!Transport->FlushForCpu(Transport->Context,
                                Publication->StateCpuAddress,
                                J313_AGX_G2_CHANNEL_STATE_STRIDE))
      return AppleAgxDeviceControlResultTransportFailed;
    Transport->MemoryBarrier(Transport->Context);
    if (!Transport->ReadU32(
            Transport->Context,
            (const volatile APPLE_AGX_BACKEND_U32 *)(
                Publication->StateCpuAddress +
                APPLE_AGX_DEVICE_CONTROL_STATE_READ_POINTER_OFFSET),
            &read_pointer))
      return AppleAgxDeviceControlResultTransportFailed;
    *CompletedPolls = poll + 1u;
    if (read_pointer >= APPLE_AGX_DEVICE_CONTROL_ENTRY_COUNT)
      return AppleAgxDeviceControlResultPointer;
    if (read_pointer == Publication->ReceiptCookie)
      return AppleAgxDeviceControlResultOk;
  }
  return AppleAgxDeviceControlResultReceiptTimedOut;
}

#undef APPLE_AGX_DEVICE_CONTROL_NULL
