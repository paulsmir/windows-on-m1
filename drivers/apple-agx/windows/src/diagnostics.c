#include "apple_agx_driver.h"

#define APPLE_AGX_START_LOG_BASE ((NTSTATUS)0xC0E10000L)

void AppleAgxLogStartStage(PDEVICE_OBJECT DeviceObject,
                           APPLE_AGX_START_STAGE Stage, NTSTATUS Status) {
  PIO_ERROR_LOG_PACKET packet;

  if (DeviceObject == NULL)
    return;

  packet = IoAllocateErrorLogEntry(DeviceObject,
                                   (UCHAR)sizeof(IO_ERROR_LOG_PACKET));
  if (packet == NULL)
    return;

  RtlZeroMemory(packet, sizeof(*packet));
  packet->MajorFunctionCode = IRP_MJ_PNP;
  packet->ErrorCode = APPLE_AGX_START_LOG_BASE | (NTSTATUS)Stage;
  packet->UniqueErrorValue = (ULONG)Stage;
  packet->FinalStatus = Status;
  IoWriteErrorLogEntry(packet);
}
