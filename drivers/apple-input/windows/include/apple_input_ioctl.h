#pragma once

#include <guiddef.h>

#define AI_DIAGNOSTIC_SNAPSHOT_VERSION_1 1u
#define AI_DIAGNOSTIC_SNAPSHOT_VERSION_2 2u
#define AI_PACKET_HEADER_RING_CAPACITY 16u

DEFINE_GUID(GUID_DEVINTERFACE_APPLE_INPUT_DIAGNOSTIC,
            0x8db27de1, 0xb531, 0x41fb, 0x99, 0x48, 0x45, 0xc7, 0x44, 0x89, 0xd0, 0x32);

#define IOCTL_AI_GET_SNAPSHOT \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800u, METHOD_BUFFERED, FILE_READ_DATA)

typedef struct _AI_PACKET_HEADER_V1 {
    ULONGLONG Sequence;
    ULONG Result;
    USHORT Offset;
    USHORT Remaining;
    USHORT Length;
    UCHAR Flags;
    UCHAR Device;
} AI_PACKET_HEADER_V1, *PAI_PACKET_HEADER_V1;

typedef struct _AI_DIAGNOSTIC_SNAPSHOT_V1 {
    ULONG Version;
    ULONG Size;
    ULONG TransportPhase;
    ULONG HeaderWriteIndex;
    ULONGLONG InterruptCount;
    ULONGLONG WorkerQueuedCount;
    ULONGLONG WorkerCompletedCount;
    ULONGLONG SpiTransferCount;
    ULONGLONG SpiTimeoutCount;
    ULONGLONG PacketCrcFailureCount;
    ULONGLONG MessageCrcFailureCount;
    ULONGLONG FragmentFailureCount;
    ULONGLONG KeyboardReportCount;
    ULONGLONG TrackpadReportCount;
    ULONGLONG ResetCount;
    ULONGLONG OfflineCount;
    AI_PACKET_HEADER_V1 Headers[AI_PACKET_HEADER_RING_CAPACITY];
} AI_DIAGNOSTIC_SNAPSHOT_V1, *PAI_DIAGNOSTIC_SNAPSHOT_V1;

/*
 * Version 2 appends only the decoded message header.  It intentionally stores
 * no report descriptor, input report, or other payload bytes.
 */
typedef struct _AI_DIAGNOSTIC_SNAPSHOT_V2 {
    ULONG Version;
    ULONG Size;
    ULONG TransportPhase;
    ULONG HeaderWriteIndex;
    ULONGLONG InterruptCount;
    ULONGLONG WorkerQueuedCount;
    ULONGLONG WorkerCompletedCount;
    ULONGLONG SpiTransferCount;
    ULONGLONG SpiTimeoutCount;
    ULONGLONG PacketCrcFailureCount;
    ULONGLONG MessageCrcFailureCount;
    ULONGLONG FragmentFailureCount;
    ULONGLONG KeyboardReportCount;
    ULONGLONG TrackpadReportCount;
    ULONGLONG ResetCount;
    ULONGLONG OfflineCount;
    AI_PACKET_HEADER_V1 Headers[AI_PACKET_HEADER_RING_CAPACITY];
    ULONG MessagePhase;
    UCHAR MessageType;
    UCHAR MessageReport;
    UCHAR MessageDevice;
    UCHAR MessageId;
    USHORT MessageResponseLength;
    USHORT MessagePayloadLength;
} AI_DIAGNOSTIC_SNAPSHOT_V2, *PAI_DIAGNOSTIC_SNAPSHOT_V2;
