#include "apple_agx_device_control.h"
#include "apple_agx_platform_provider.h"

#include <assert.h>
#include <string.h>

typedef struct _FAKE_TRANSPORT {
  unsigned char *State;
  unsigned char *Ring;
  unsigned int FlushDeviceCalls;
  unsigned int FlushCpuCalls;
  unsigned int BarrierCalls;
  unsigned int PublishCalls;
  unsigned int ReadCalls;
  unsigned int DoorbellCalls;
  unsigned int ReceiptAfterFlushes;
  unsigned int ReceiptValue;
  unsigned int LastDoorbell;
  unsigned char FailRead;
} FAKE_TRANSPORT;

static unsigned int get_u32(const unsigned char *wire) {
  return (unsigned int)wire[0] | ((unsigned int)wire[1] << 8u) |
         ((unsigned int)wire[2] << 16u) | ((unsigned int)wire[3] << 24u);
}

static unsigned long long get_u64(const unsigned char *wire) {
  return (unsigned long long)get_u32(wire) |
         ((unsigned long long)get_u32(wire + 4u) << 32u);
}

static APPLE_AGX_BACKEND_BOOL fake_flush_device(void *context,
                                                const void *address,
                                                APPLE_AGX_BACKEND_U32 bytes) {
  FAKE_TRANSPORT *transport = (FAKE_TRANSPORT *)context;
  assert(address >= (const void *)transport->Ring);
  assert(address < (const void *)(transport->Ring + 0x3000u));
  assert(bytes == APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE);
  ++transport->FlushDeviceCalls;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL fake_flush_cpu(void *context,
                                             const void *address,
                                             APPLE_AGX_BACKEND_U32 bytes) {
  FAKE_TRANSPORT *transport = (FAKE_TRANSPORT *)context;
  assert(address == transport->State);
  assert(bytes == 0x30u);
  ++transport->FlushCpuCalls;
  if (transport->ReceiptAfterFlushes != 0u &&
      transport->FlushCpuCalls == transport->ReceiptAfterFlushes)
    memcpy(transport->State, &transport->ReceiptValue,
           sizeof(transport->ReceiptValue));
  return APPLE_AGX_BACKEND_TRUE;
}

static void fake_barrier(void *context) {
  ++((FAKE_TRANSPORT *)context)->BarrierCalls;
}

static APPLE_AGX_BACKEND_BOOL fake_publish_u32(
    void *context, volatile APPLE_AGX_BACKEND_U32 *address,
    APPLE_AGX_BACKEND_U32 value) {
  FAKE_TRANSPORT *transport = (FAKE_TRANSPORT *)context;
  assert((unsigned char *)address == transport->State + 0x20u);
  *address = value;
  ++transport->PublishCalls;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL fake_read_u32(
    void *context, const volatile APPLE_AGX_BACKEND_U32 *address,
    APPLE_AGX_BACKEND_U32 *value) {
  FAKE_TRANSPORT *transport = (FAKE_TRANSPORT *)context;
  ++transport->ReadCalls;
  if (transport->FailRead)
    return APPLE_AGX_BACKEND_FALSE;
  assert((const unsigned char *)address == transport->State ||
         (const unsigned char *)address == transport->State + 0x20u);
  *value = *address;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL fake_ring_doorbell(
    void *context, APPLE_AGX_BACKEND_U32 doorbell) {
  FAKE_TRANSPORT *transport = (FAKE_TRANSPORT *)context;
  transport->LastDoorbell = doorbell;
  ++transport->DoorbellCalls;
  return APPLE_AGX_BACKEND_TRUE;
}

static void prepare_owner(APPLE_AGX_CHANNEL_MEMORY_OWNER *owner,
                          unsigned char *state, unsigned char *ring) {
  memset(owner, 0, sizeof(*owner));
  owner->Initialized = 1u;
  owner->Built = 1u;
  owner->ObjectCount = APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT;
  owner->Objects[AppleAgxChannelMemoryDevctrlState].CpuAddress = state;
  owner->Objects[AppleAgxChannelMemoryDevctrlState].Length = 0x30u;
  owner->Objects[AppleAgxChannelMemoryDevctrlState].State =
      AppleAgxMemoryGpuMapped;
  owner->Objects[AppleAgxChannelMemoryDevctrlRing].CpuAddress = ring;
  owner->Objects[AppleAgxChannelMemoryDevctrlRing].Length = 0x3000u;
  owner->Objects[AppleAgxChannelMemoryDevctrlRing].State =
      AppleAgxMemoryGpuMapped;
  owner->VirtualAddresses[AppleAgxChannelMemoryDevctrlState] =
      0xffffffa010000000ULL;
  owner->VirtualAddresses[AppleAgxChannelMemoryDevctrlRing] =
      0xffffffa010004000ULL;
  owner->ChannelInfo.Entries[APPLE_AGX_DEVICE_CONTROL_CHANNEL_INFO_INDEX]
      .StateAddress =
      owner->VirtualAddresses[AppleAgxChannelMemoryDevctrlState];
  owner->ChannelInfo.Entries[APPLE_AGX_DEVICE_CONTROL_CHANNEL_INFO_INDEX]
      .RingAddress =
      owner->VirtualAddresses[AppleAgxChannelMemoryDevctrlRing];
}

static APPLE_AGX_PLATFORM_TRANSPORT_IO make_io(FAKE_TRANSPORT *transport) {
  APPLE_AGX_PLATFORM_TRANSPORT_IO io;
  memset(&io, 0, sizeof(io));
  io.Context = transport;
  io.FlushForDevice = fake_flush_device;
  io.FlushForCpu = fake_flush_cpu;
  io.MemoryBarrier = fake_barrier;
  io.PublishU32 = fake_publish_u32;
  io.ReadU32 = fake_read_u32;
  io.RingDoorbell = fake_ring_doorbell;
  return io;
}

static void expect_zero_tail(const unsigned char *message) {
  unsigned int index;
  for (index = 4u; index < APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE; ++index)
    assert(message[index] == 0u);
}

static void test_exact_g13_v13_5_messages(void) {
  unsigned char init[APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE] = {0};
  unsigned char idle[APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE] = {0};

  assert(AppleAgxDeviceControlEncodeInitG13V13_5(init, sizeof(init)) ==
         AppleAgxDeviceControlResultOk);
  assert(init[0] == 0x1au && init[1] == 0u && init[2] == 0u &&
         init[3] == 0u);
  expect_zero_tail(init);

  assert(AppleAgxDeviceControlEncodeUpdateIdleTimestampG13V13_5(
             idle, sizeof(idle)) == AppleAgxDeviceControlResultOk);
  assert(idle[0] == 0x23u && idle[1] == 0u && idle[2] == 0u &&
         idle[3] == 0u);
  expect_zero_tail(idle);
}

static void test_encoder_is_fail_closed(void) {
  unsigned char message[APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE] = {0};

  assert(AppleAgxDeviceControlEncodeInitG13V13_5(0, sizeof(message)) ==
         AppleAgxDeviceControlResultInvalidArgument);
  assert(AppleAgxDeviceControlEncodeInitG13V13_5(
             message, sizeof(message) - 1u) ==
         AppleAgxDeviceControlResultDestinationSize);
  message[9] = 1u;
  assert(AppleAgxDeviceControlEncodeInitG13V13_5(
             message, sizeof(message)) ==
         AppleAgxDeviceControlResultDestinationNotZero);
  assert(message[9] == 1u);
}

static void test_exact_destroy_context_message(void) {
  unsigned char message[APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE] = {0};

  assert(AppleAgxDeviceControlEncodeDestroyContextG13V13_5(
             APPLE_AGX_DEVICE_CONTROL_EXP208_CONTEXT_GPU_VA, message,
             sizeof(message)) == AppleAgxDeviceControlResultOk);
  assert(get_u32(message) == 0x18u);
  assert(get_u32(message + 0x04u) == 0u);
  assert(get_u32(message + 0x08u) == 2u);
  assert(get_u32(message + 0x0cu) == 0u);
  assert(get_u32(message + 0x10u) == 0u);
  assert(get_u32(message + 0x14u) == 0xffffu);
  assert(get_u32(message + 0x18u) == 0u);
  assert(get_u64(message + 0x1cu) ==
         APPLE_AGX_DEVICE_CONTROL_EXP208_CONTEXT_GPU_VA);
  assert(get_u32(message + 0x24u) == 0u);
  assert(get_u32(message + 0x28u) == 0u);
  assert(get_u32(message + 0x2cu) == 0u);

  memset(message, 0, sizeof(message));
  assert(AppleAgxDeviceControlEncodeDestroyContextG13V13_5(
             APPLE_AGX_DEVICE_CONTROL_EXP208_CONTEXT_GPU_VA + 0x4000ULL,
             message, sizeof(message)) ==
         AppleAgxDeviceControlResultContextAddress);
  expect_zero_tail(message);
}

static void test_channel_12_publication_and_bounded_receipt(void) {
  _Alignas(4) unsigned char state[0x30] = {0};
  _Alignas(8) unsigned char ring[0x3000] = {0};
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  APPLE_AGX_DEVICE_CONTROL_PUBLICATION publication;
  APPLE_AGX_PLATFORM_TRANSPORT_IO io;
  FAKE_TRANSPORT transport;
  unsigned int read_pointer = 3u;
  unsigned int write_pointer = 5u;
  unsigned int polls = 0u;

  prepare_owner(&owner, state, ring);
  memcpy(state, &read_pointer, sizeof(read_pointer));
  memcpy(state + 0x20u, &write_pointer, sizeof(write_pointer));
  memset(&transport, 0, sizeof(transport));
  transport.State = state;
  transport.Ring = ring;
  io = make_io(&transport);

  assert(AppleAgxDeviceControlPublishDestroyContextG13V13_5(
             &owner, &io,
             APPLE_AGX_DEVICE_CONTROL_EXP208_CONTEXT_GPU_VA + 0x4000ULL,
             &publication) == AppleAgxDeviceControlResultContextAddress);
  assert(transport.FlushCpuCalls == 0u);
  assert(transport.FlushDeviceCalls == 0u);
  assert(transport.PublishCalls == 0u);
  assert(transport.DoorbellCalls == 0u);

  assert(AppleAgxDeviceControlPublishDestroyContextG13V13_5(
             &owner, &io, APPLE_AGX_DEVICE_CONTROL_EXP208_CONTEXT_GPU_VA,
             &publication) == AppleAgxDeviceControlResultOk);
  assert(publication.ChannelInfoIndex == 12u);
  assert(publication.SlotOffset == 5u * 0x30u);
  assert(publication.NextWritePointer == 6u);
  assert(publication.ReceiptCookie == 6u);
  assert(publication.StateCpuAddress == state);
  assert(publication.RingCpuAddress == ring);
  assert(get_u32(ring + publication.SlotOffset) == 0x18u);
  assert(get_u64(ring + publication.SlotOffset + 0x1cu) ==
         APPLE_AGX_DEVICE_CONTROL_EXP208_CONTEXT_GPU_VA);
  assert(get_u32(state + 0x20u) == publication.ReceiptCookie);
  assert(transport.FlushDeviceCalls == 1u);
  assert(transport.PublishCalls == 1u);
  assert(transport.DoorbellCalls == 1u);
  assert(transport.LastDoorbell == APPLE_AGX_DEVICE_CONTROL_DOORBELL_CHANNEL);

  transport.FlushCpuCalls = 0u;
  transport.ReceiptAfterFlushes = 3u;
  transport.ReceiptValue = publication.ReceiptCookie;
  assert(AppleAgxDeviceControlWaitForReceiptG13V13_5(
             &publication, &io, 3u, &polls) ==
         AppleAgxDeviceControlResultOk);
  assert(polls == 3u);
  assert(get_u32(state) == publication.ReceiptCookie);
}

static void test_init_and_idle_use_the_same_exact_channel_12_contract(void) {
  _Alignas(4) unsigned char state[0x30] = {0};
  _Alignas(8) unsigned char ring[0x3000] = {0};
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  APPLE_AGX_DEVICE_CONTROL_PUBLICATION init_publication;
  APPLE_AGX_DEVICE_CONTROL_PUBLICATION idle_publication;
  APPLE_AGX_PLATFORM_TRANSPORT_IO io;
  FAKE_TRANSPORT transport;

  prepare_owner(&owner, state, ring);
  memset(&transport, 0, sizeof(transport));
  transport.State = state;
  transport.Ring = ring;
  io = make_io(&transport);

  assert(AppleAgxDeviceControlPublishInitG13V13_5(
             &owner, &io, &init_publication) ==
         AppleAgxDeviceControlResultOk);
  assert(get_u32(ring + init_publication.SlotOffset) == 0x1au);
  assert(init_publication.ReceiptCookie == 1u);

  assert(AppleAgxDeviceControlPublishUpdateIdleTimestampG13V13_5(
             &owner, &io, &idle_publication) ==
         AppleAgxDeviceControlResultOk);
  assert(get_u32(ring + idle_publication.SlotOffset) == 0x23u);
  assert(idle_publication.ReceiptCookie == 2u);
  assert(transport.FlushDeviceCalls == 2u);
  assert(transport.PublishCalls == 2u);
  assert(transport.DoorbellCalls == 2u);
  assert(transport.LastDoorbell == APPLE_AGX_DEVICE_CONTROL_DOORBELL_CHANNEL);
}

static void test_publication_and_receipt_fail_closed(void) {
  _Alignas(4) unsigned char state[0x30] = {0};
  _Alignas(8) unsigned char ring[0x3000] = {0};
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  APPLE_AGX_DEVICE_CONTROL_PUBLICATION publication;
  APPLE_AGX_PLATFORM_TRANSPORT_IO io;
  FAKE_TRANSPORT transport;
  unsigned int read_pointer = 6u;
  unsigned int write_pointer = 5u;
  unsigned int polls = 99u;

  prepare_owner(&owner, state, ring);
  memcpy(state, &read_pointer, sizeof(read_pointer));
  memcpy(state + 0x20u, &write_pointer, sizeof(write_pointer));
  memset(&transport, 0, sizeof(transport));
  transport.State = state;
  transport.Ring = ring;
  io = make_io(&transport);

  assert(AppleAgxDeviceControlPublishDestroyContextG13V13_5(
             &owner, &io, APPLE_AGX_DEVICE_CONTROL_EXP208_CONTEXT_GPU_VA,
             &publication) == AppleAgxDeviceControlResultRingFull);
  assert(transport.FlushDeviceCalls == 0u);
  assert(transport.PublishCalls == 0u);
  assert(transport.DoorbellCalls == 0u);

  read_pointer = APPLE_AGX_DEVICE_CONTROL_ENTRY_COUNT;
  memcpy(state, &read_pointer, sizeof(read_pointer));
  assert(AppleAgxDeviceControlPublishDestroyContextG13V13_5(
             &owner, &io, APPLE_AGX_DEVICE_CONTROL_EXP208_CONTEXT_GPU_VA,
             &publication) == AppleAgxDeviceControlResultPointer);
  assert(transport.FlushDeviceCalls == 0u);
  assert(transport.PublishCalls == 0u);
  assert(transport.DoorbellCalls == 0u);

  read_pointer = 0u;
  memcpy(state, &read_pointer, sizeof(read_pointer));
  memset(ring + write_pointer * APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE, 0xa5,
         APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE);
  assert(AppleAgxDeviceControlPublishDestroyContextG13V13_5(
             &owner, &io, APPLE_AGX_DEVICE_CONTROL_EXP208_CONTEXT_GPU_VA,
             &publication) == AppleAgxDeviceControlResultOk);
  assert(AppleAgxDeviceControlWaitForReceiptG13V13_5(
             &publication, &io, 2u, &polls) ==
         AppleAgxDeviceControlResultReceiptTimedOut);
  assert(polls == 2u);

  transport.FlushCpuCalls = 0u;
  transport.ReceiptAfterFlushes = 1u;
  transport.ReceiptValue = APPLE_AGX_DEVICE_CONTROL_ENTRY_COUNT;
  assert(AppleAgxDeviceControlWaitForReceiptG13V13_5(
             &publication, &io, 2u, &polls) ==
         AppleAgxDeviceControlResultPointer);
  assert(polls == 1u);

  transport.FailRead = 1u;
  assert(AppleAgxDeviceControlWaitForReceiptG13V13_5(
             &publication, &io, 1u, &polls) ==
         AppleAgxDeviceControlResultTransportFailed);
  assert(AppleAgxDeviceControlWaitForReceiptG13V13_5(
             &publication, &io, 0u, &polls) ==
         AppleAgxDeviceControlResultInvalidArgument);
}

static void test_exact_ring_publication_plan(void) {
  APPLE_AGX_DEVICE_CONTROL_PUBLICATION plan;

  assert(AppleAgxDeviceControlPlanPublicationG13V13_5(0u, &plan) ==
         AppleAgxDeviceControlResultOk);
  assert(plan.SlotOffset == 0u);
  assert(plan.NextWritePointer == 1u);
  assert(plan.StateReadPointerOffset == 0u);
  assert(plan.StateWritePointerOffset == 0x20u);
  assert(plan.DoorbellEndpoint == 0x21u);
  assert(plan.DoorbellChannel == 0x11u);
  assert(plan.DoorbellMessage == 0x0083000000000011ULL);

  assert(AppleAgxDeviceControlPlanPublicationG13V13_5(255u, &plan) ==
         AppleAgxDeviceControlResultOk);
  assert(plan.SlotOffset == 255u * 0x30u);
  assert(plan.NextWritePointer == 0u);

  assert(AppleAgxDeviceControlPlanPublicationG13V13_5(256u, &plan) ==
         AppleAgxDeviceControlResultPointer);
  assert(AppleAgxDeviceControlPlanPublicationG13V13_5(0u, 0) ==
         AppleAgxDeviceControlResultInvalidArgument);
}

int main(void) {
  test_exact_g13_v13_5_messages();
  test_encoder_is_fail_closed();
  test_exact_destroy_context_message();
  test_exact_ring_publication_plan();
  test_channel_12_publication_and_bounded_receipt();
  test_init_and_idle_use_the_same_exact_channel_12_contract();
  test_publication_and_receipt_fail_closed();
  return 0;
}
