#include "apple_agx_platform_transport_validation.h"

#define TRANSPORT_NULL ((void *)0)
#define TRANSPORT_CHANNEL_STATE_BYTES 0x30u
#define TRANSPORT_RUN_BYTES APPLE_AGX_G13_RUN_MESSAGE_SIZE
#define TRANSPORT_EVENT_BYTES APPLE_AGX_G13_EVENT_MESSAGE_SIZE
#define TRANSPORT_RENDER_SLOT_BYTES 8u
#define TRANSPORT_COMMAND_SLOT_COUNT 0x100u
#define TRANSPORT_EVENT_SLOT_COUNT 0x100u

static APPLE_AGX_BACKEND_BOOL object_ready(
    const APPLE_AGX_MEMORY_OBJECT *Object) {
  return Object != TRANSPORT_NULL && Object->CpuAddress != TRANSPORT_NULL &&
                 Object->Length != 0ULL &&
                 Object->State == AppleAgxMemoryGpuMapped
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL exact_offset(
    const APPLE_AGX_MEMORY_OBJECT *Object, const void *Address,
    APPLE_AGX_BACKEND_U64 Offset, APPLE_AGX_BACKEND_U32 Bytes) {
  const unsigned char *base;
  const unsigned char *address;
  if (!object_ready(Object) || Address == TRANSPORT_NULL || Bytes == 0u ||
      Offset > Object->Length || Bytes > Object->Length - Offset)
    return APPLE_AGX_BACKEND_FALSE;
  base = (const unsigned char *)Object->CpuAddress;
  address = (const unsigned char *)Address;
  return address >= base && (APPLE_AGX_BACKEND_U64)(address - base) == Offset
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL exact_slot(
    const APPLE_AGX_MEMORY_OBJECT *Object, const void *Address,
    APPLE_AGX_BACKEND_U32 Bytes, APPLE_AGX_BACKEND_U32 SlotBytes,
    APPLE_AGX_BACKEND_U32 SlotCount) {
  const unsigned char *base;
  const unsigned char *address;
  APPLE_AGX_BACKEND_U64 offset;
  if (!object_ready(Object) || Address == TRANSPORT_NULL ||
      Bytes != SlotBytes || SlotBytes == 0u || SlotCount == 0u)
    return APPLE_AGX_BACKEND_FALSE;
  base = (const unsigned char *)Object->CpuAddress;
  address = (const unsigned char *)Address;
  if (address < base)
    return APPLE_AGX_BACKEND_FALSE;
  offset = (APPLE_AGX_BACKEND_U64)(address - base);
  if ((offset % SlotBytes) != 0ULL || offset / SlotBytes >= SlotCount ||
      offset > Object->Length || Bytes > Object->Length - offset)
    return APPLE_AGX_BACKEND_FALSE;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL owners_ready(
    const APPLE_AGX_PLATFORM_TRANSPORT_MEMORY *Memory) {
  return Memory != TRANSPORT_NULL && Memory->Channels != TRANSPORT_NULL &&
                 Memory->Render != TRANSPORT_NULL &&
                 Memory->Channels->Initialized && Memory->Channels->Built &&
                 Memory->Channels->ObjectCount ==
                     APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT &&
                 Memory->Render->Initialized && Memory->Render->Built &&
                 Memory->Render->ObjectCount ==
                     APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformTransportValidateFlushForDevice(
    const APPLE_AGX_PLATFORM_TRANSPORT_MEMORY *Memory, const void *Address,
    APPLE_AGX_BACKEND_U32 Bytes) {
  static const APPLE_AGX_BACKEND_U32 work_objects[] = {14u, 16u, 18u, 19u};
  APPLE_AGX_BACKEND_U32 index;
  if (!owners_ready(Memory))
    return APPLE_AGX_BACKEND_FALSE;
  if (exact_slot(&Memory->Channels->Objects[15], Address, Bytes,
                 TRANSPORT_RUN_BYTES,
                 TRANSPORT_COMMAND_SLOT_COUNT) ||
      exact_slot(&Memory->Channels->Objects[16], Address, Bytes,
                 TRANSPORT_RUN_BYTES,
                 TRANSPORT_COMMAND_SLOT_COUNT) ||
      exact_slot(&Memory->Channels->Objects[AppleAgxChannelMemoryDevctrlRing],
                 Address, Bytes, APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE,
                 APPLE_AGX_DEVICE_CONTROL_ENTRY_COUNT) ||
      exact_slot(&Memory->Render->Objects[4], Address, Bytes,
                 TRANSPORT_RENDER_SLOT_BYTES,
                 APPLE_AGX_G13_RING_CAPACITY) ||
      exact_slot(&Memory->Render->Objects[7], Address, Bytes,
                 TRANSPORT_RENDER_SLOT_BYTES,
                 APPLE_AGX_G13_RING_CAPACITY))
    return APPLE_AGX_BACKEND_TRUE;
  for (index = 0u; index <
                       (APPLE_AGX_BACKEND_U32)(sizeof(work_objects) /
                                               sizeof(work_objects[0]));
       ++index) {
    const APPLE_AGX_MEMORY_OBJECT *object =
        &Memory->Render->Objects[work_objects[index]];
    if (object_ready(object) && object->Length <= 0xffffffffULL &&
        exact_offset(object, Address, 0ULL, Bytes) &&
        Bytes == (APPLE_AGX_BACKEND_U32)object->Length)
      return APPLE_AGX_BACKEND_TRUE;
  }
  return APPLE_AGX_BACKEND_FALSE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformTransportValidateFlushForCpu(
    const APPLE_AGX_PLATFORM_TRANSPORT_MEMORY *Memory, const void *Address,
    APPLE_AGX_BACKEND_U32 Bytes) {
  if (!owners_ready(Memory))
    return APPLE_AGX_BACKEND_FALSE;
  return (exact_offset(&Memory->Channels->Objects[3], Address, 0ULL, Bytes) &&
          Bytes == TRANSPORT_CHANNEL_STATE_BYTES) ||
             (exact_offset(&Memory->Channels->Objects[4], Address, 0ULL,
                           Bytes) && Bytes == TRANSPORT_CHANNEL_STATE_BYTES) ||
             (exact_offset(&Memory->Channels->Objects[26], Address, 0ULL,
                           Bytes) && Bytes == TRANSPORT_CHANNEL_STATE_BYTES) ||
             (exact_offset(
                  &Memory->Channels
                       ->Objects[AppleAgxChannelMemoryDevctrlState],
                  Address, 0ULL, Bytes) &&
              Bytes == TRANSPORT_CHANNEL_STATE_BYTES) ||
             exact_slot(&Memory->Channels->Objects[27], Address, Bytes,
                        TRANSPORT_EVENT_BYTES,
                        TRANSPORT_EVENT_SLOT_COUNT)
         ? APPLE_AGX_BACKEND_TRUE
         : APPLE_AGX_BACKEND_FALSE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformTransportValidateReadU32(
    const APPLE_AGX_PLATFORM_TRANSPORT_MEMORY *Memory,
    const volatile APPLE_AGX_BACKEND_U32 *Address) {
  if (!owners_ready(Memory) ||
      (((APPLE_AGX_BACKEND_U64)(const unsigned char *)Address) & 3ULL) != 0ULL)
    return APPLE_AGX_BACKEND_FALSE;
  return exact_offset(&Memory->Channels->Objects[3], (const void *)Address,
                      0ULL, 4u) ||
                 exact_offset(&Memory->Channels->Objects[3],
                              (const void *)Address, 0x20ULL, 4u) ||
                 exact_offset(&Memory->Channels->Objects[4],
                              (const void *)Address, 0ULL, 4u) ||
                 exact_offset(&Memory->Channels->Objects[4],
                              (const void *)Address, 0x20ULL, 4u) ||
                 exact_offset(&Memory->Channels->Objects[26],
                              (const void *)Address, 0ULL, 4u) ||
                 exact_offset(&Memory->Channels->Objects[26],
                              (const void *)Address, 0x20ULL, 4u) ||
                 exact_offset(
                     &Memory->Channels
                          ->Objects[AppleAgxChannelMemoryDevctrlState],
                     (const void *)Address, 0ULL, 4u) ||
                 exact_offset(
                     &Memory->Channels
                          ->Objects[AppleAgxChannelMemoryDevctrlState],
                     (const void *)Address, 0x20ULL, 4u) ||
                 exact_offset(&Memory->Render->Objects[24],
                              (const void *)Address, 0ULL, 4u) ||
                 exact_offset(&Memory->Render->Objects[24],
                              (const void *)Address, 0x40ULL, 4u) ||
                 exact_offset(&Memory->Render->Objects[25],
                              (const void *)Address, 0ULL, 4u) ||
                 exact_offset(&Memory->Render->Objects[25],
                              (const void *)Address, 0x40ULL, 4u) ||
                 exact_offset(&Memory->Render->Objects[26],
                              (const void *)Address, 0ULL, 4u) ||
                 exact_offset(&Memory->Render->Objects[27],
                              (const void *)Address, 0ULL, 4u)
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformTransportValidatePublishU32(
    const APPLE_AGX_PLATFORM_TRANSPORT_MEMORY *Memory,
    volatile APPLE_AGX_BACKEND_U32 *Address) {
  if (!owners_ready(Memory) ||
      (((APPLE_AGX_BACKEND_U64)(unsigned char *)Address) & 3ULL) != 0ULL)
    return APPLE_AGX_BACKEND_FALSE;
  return exact_offset(&Memory->Channels->Objects[3], (const void *)Address,
                      0x20ULL, 4u) ||
                 exact_offset(&Memory->Channels->Objects[4],
                              (const void *)Address, 0x20ULL, 4u) ||
                 exact_offset(&Memory->Channels->Objects[26],
                              (const void *)Address, 0ULL, 4u) ||
                 exact_offset(
                     &Memory->Channels
                          ->Objects[AppleAgxChannelMemoryDevctrlState],
                     (const void *)Address, 0x20ULL, 4u) ||
                 exact_offset(&Memory->Render->Objects[24],
                              (const void *)Address, 0x40ULL, 4u) ||
                 exact_offset(&Memory->Render->Objects[25],
                              (const void *)Address, 0x40ULL, 4u)
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

#undef TRANSPORT_RENDER_SLOT_BYTES
#undef TRANSPORT_EVENT_BYTES
#undef TRANSPORT_RUN_BYTES
#undef TRANSPORT_CHANNEL_STATE_BYTES
#undef TRANSPORT_EVENT_SLOT_COUNT
#undef TRANSPORT_COMMAND_SLOT_COUNT
#undef TRANSPORT_NULL
