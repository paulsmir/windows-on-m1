#include "apple_agx_platform_transport_validation.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void map_object(APPLE_AGX_MEMORY_OBJECT *Object, void *Address,
                       unsigned long long Bytes) {
  memset(Object, 0, sizeof(*Object));
  Object->CpuAddress = Address;
  Object->Length = Bytes;
  Object->State = AppleAgxMemoryGpuMapped;
}

int main(void) {
  APPLE_AGX_CHANNEL_MEMORY_OWNER channels;
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER render;
  APPLE_AGX_PLATFORM_TRANSPORT_MEMORY memory;
  unsigned char channel_storage[35][0x4000];
  unsigned char render_storage[36][0x8000];
  unsigned int index;

  memset(&channels, 0, sizeof(channels));
  memset(&render, 0, sizeof(render));
  memset(channel_storage, 0, sizeof(channel_storage));
  memset(render_storage, 0, sizeof(render_storage));
  channels.Initialized = 1u;
  channels.Built = 1u;
  channels.ObjectCount = APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT;
  render.Initialized = APPLE_AGX_TRUE;
  render.Built = APPLE_AGX_TRUE;
  render.ObjectCount = APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
  for (index = 0u; index < APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT; ++index)
    map_object(&channels.Objects[index], channel_storage[index], 0x4000u);
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index)
    map_object(&render.Objects[index], render_storage[index], 0x8000u);
  memory.Channels = &channels;
  memory.Render = &render;

  assert(AppleAgxPlatformTransportValidateFlushForDevice(
      &memory, channel_storage[15] + 3u * 0x30u, 0x30u));
  assert(AppleAgxPlatformTransportValidateFlushForDevice(
      &memory, channel_storage[25] + 255u * 0x30u, 0x30u));
  assert(!AppleAgxPlatformTransportValidateFlushForDevice(
      &memory, channel_storage[25] + 256u * 0x30u, 0x30u));
  assert(!AppleAgxPlatformTransportValidateFlushForDevice(
      &memory, channel_storage[15] + 1u, 0x30u));
  assert(AppleAgxPlatformTransportValidateFlushForDevice(
      &memory, render_storage[4] + 0x4ffu * 8u, 8u));
  assert(!AppleAgxPlatformTransportValidateFlushForDevice(
      &memory, render_storage[4] + 0x500u * 8u, 8u));
  assert(AppleAgxPlatformTransportValidateFlushForDevice(
      &memory, render_storage[14], 0x8000u));
  assert(!AppleAgxPlatformTransportValidateFlushForDevice(
      &memory, render_storage[14], 0x100u));

  assert(AppleAgxPlatformTransportValidateFlushForCpu(
      &memory, channel_storage[26], 0x30u));
  assert(AppleAgxPlatformTransportValidateFlushForCpu(
      &memory, channel_storage[24], 0x30u));
  assert(AppleAgxPlatformTransportValidateFlushForCpu(
      &memory, channel_storage[27] + 255u * 0x38u, 0x38u));
  assert(!AppleAgxPlatformTransportValidateFlushForCpu(
      &memory, channel_storage[27] + 256u * 0x38u, 0x38u));

  assert(AppleAgxPlatformTransportValidateReadU32(
      &memory, (volatile APPLE_AGX_BACKEND_U32 *)(render_storage[24])));
  assert(AppleAgxPlatformTransportValidateReadU32(
      &memory, (volatile APPLE_AGX_BACKEND_U32 *)(channel_storage[24])));
  assert(AppleAgxPlatformTransportValidateReadU32(
      &memory,
      (volatile APPLE_AGX_BACKEND_U32 *)(channel_storage[24] + 0x20u)));
  assert(AppleAgxPlatformTransportValidateReadU32(
      &memory,
      (volatile APPLE_AGX_BACKEND_U32 *)(render_storage[25] + 0x40u)));
  assert(!AppleAgxPlatformTransportValidateReadU32(
      &memory,
      (volatile APPLE_AGX_BACKEND_U32 *)(render_storage[25] + 0x44u)));

  assert(AppleAgxPlatformTransportValidatePublishU32(
      &memory,
      (volatile APPLE_AGX_BACKEND_U32 *)(channel_storage[3] + 0x20u)));
  assert(AppleAgxPlatformTransportValidatePublishU32(
      &memory,
      (volatile APPLE_AGX_BACKEND_U32 *)(channel_storage[24] + 0x20u)));
  assert(!AppleAgxPlatformTransportValidatePublishU32(
      &memory, (volatile APPLE_AGX_BACKEND_U32 *)(channel_storage[3])));

  channels.Objects[15].State = AppleAgxMemoryCpuOwned;
  assert(!AppleAgxPlatformTransportValidateFlushForDevice(
      &memory, channel_storage[15], 0x30u));
  puts("apple_agx_platform_transport_validation_test: ok");
  return 0;
}
