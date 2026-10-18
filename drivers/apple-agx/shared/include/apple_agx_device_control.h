#ifndef APPLE_AGX_DEVICE_CONTROL_H
#define APPLE_AGX_DEVICE_CONTROL_H

#include "apple_agx_rtkit.h"
#include "j313_agx_g2.generated.h"

/*
 * Exact J313 G13/V13_5 device-control contract mirrored from m1n1:
 * DeviceControlMsg is 0x30 bytes, the ring has 0x100 entries, channel state
 * stores READ_PTR at 0x00 and WRITE_PTR at 0x20, and channel 0x11 is kicked
 * through endpoint 0x21 after the write pointer is advanced.
 */
#define APPLE_AGX_DEVICE_CONTROL_MESSAGE_SIZE 0x30u
#define APPLE_AGX_DEVICE_CONTROL_ENTRY_COUNT 0x100u
#define APPLE_AGX_DEVICE_CONTROL_STATE_READ_POINTER_OFFSET 0x00u
#define APPLE_AGX_DEVICE_CONTROL_STATE_WRITE_POINTER_OFFSET 0x20u
#define APPLE_AGX_DEVICE_CONTROL_DOORBELL_ENDPOINT 0x21u
#define APPLE_AGX_DEVICE_CONTROL_DOORBELL_CHANNEL 0x11u
#define APPLE_AGX_DEVICE_CONTROL_CHANNEL_INFO_INDEX 12u
#define APPLE_AGX_DEVICE_CONTROL_EXP208_CONTEXT_GPU_VA 0x1503800000ULL

struct _APPLE_AGX_CHANNEL_MEMORY_OWNER;
struct _APPLE_AGX_PLATFORM_TRANSPORT_IO;

typedef enum _APPLE_AGX_DEVICE_CONTROL_RESULT {
  AppleAgxDeviceControlResultOk = 0,
  AppleAgxDeviceControlResultInvalidArgument,
  AppleAgxDeviceControlResultUnsupportedVersion,
  AppleAgxDeviceControlResultDestinationSize,
  AppleAgxDeviceControlResultDestinationNotZero,
  AppleAgxDeviceControlResultPointer,
  AppleAgxDeviceControlResultContextAddress,
  AppleAgxDeviceControlResultChannelBinding,
  AppleAgxDeviceControlResultRingFull,
  AppleAgxDeviceControlResultTransportFailed,
  AppleAgxDeviceControlResultReceiptTimedOut,
} APPLE_AGX_DEVICE_CONTROL_RESULT;

typedef struct _APPLE_AGX_DEVICE_CONTROL_PUBLICATION {
  unsigned int SlotOffset;
  unsigned int NextWritePointer;
  unsigned int StateReadPointerOffset;
  unsigned int StateWritePointerOffset;
  unsigned int DoorbellEndpoint;
  unsigned int DoorbellChannel;
  unsigned long long DoorbellMessage;
  unsigned int ChannelInfoIndex;
  unsigned int ReceiptCookie;
  unsigned char *StateCpuAddress;
  unsigned char *RingCpuAddress;
} APPLE_AGX_DEVICE_CONTROL_PUBLICATION;

APPLE_AGX_DEVICE_CONTROL_RESULT AppleAgxDeviceControlEncodeInitG13V13_5(
    unsigned char *Destination, unsigned int DestinationSize);
APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlEncodeUpdateIdleTimestampG13V13_5(
    unsigned char *Destination, unsigned int DestinationSize);
APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlEncodeDestroyContextG13V13_5(
    unsigned long long ContextGpuAddress, unsigned char *Destination,
    unsigned int DestinationSize);
APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlPlanPublicationG13V13_5(
    unsigned int CurrentWritePointer,
    APPLE_AGX_DEVICE_CONTROL_PUBLICATION *Publication);

/*
 * Publish the G13/V13.5 DestroyContext message through ChannelInfo[12].
 * This is a natural post-retirement context-lifetime operation.  It neither
 * cancels active TA/3D work nor proves that a submitted job has retired.
 */
APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlPublishInitG13V13_5(
    const struct _APPLE_AGX_CHANNEL_MEMORY_OWNER *ChannelMemory,
    const struct _APPLE_AGX_PLATFORM_TRANSPORT_IO *Transport,
    APPLE_AGX_DEVICE_CONTROL_PUBLICATION *Publication);
APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlPublishUpdateIdleTimestampG13V13_5(
    const struct _APPLE_AGX_CHANNEL_MEMORY_OWNER *ChannelMemory,
    const struct _APPLE_AGX_PLATFORM_TRANSPORT_IO *Transport,
    APPLE_AGX_DEVICE_CONTROL_PUBLICATION *Publication);
APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlPublishDestroyContextG13V13_5(
    const struct _APPLE_AGX_CHANNEL_MEMORY_OWNER *ChannelMemory,
    const struct _APPLE_AGX_PLATFORM_TRANSPORT_IO *Transport,
    unsigned long long ContextGpuAddress,
    APPLE_AGX_DEVICE_CONTROL_PUBLICATION *Publication);

/*
 * Bounded receipt observation.  Success means firmware advanced Channel 12
 * READ_PTR to the write-pointer cookie returned by PublishDestroyContext.
 */
APPLE_AGX_DEVICE_CONTROL_RESULT
AppleAgxDeviceControlWaitForReceiptG13V13_5(
    const APPLE_AGX_DEVICE_CONTROL_PUBLICATION *Publication,
    const struct _APPLE_AGX_PLATFORM_TRANSPORT_IO *Transport,
    unsigned int MaximumPolls, unsigned int *CompletedPolls);

#endif /* APPLE_AGX_DEVICE_CONTROL_H */
