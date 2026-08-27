#include "apple_agx_driver.h"

#if defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION) ||                           \
    defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION) ||                    \
    defined(APPLE_AGX_G2_RTKIT_QUALIFICATION)

static BOOLEAN
AppleAgxAscRangeValid(_In_ const APPLE_AGX_WINDOWS_ASC_TRANSPORT *Transport,
                      _In_ ULONG Offset, _In_ ULONG Width) {
  return Transport != NULL && Transport->Base != NULL && Width != 0u &&
         Offset <= Transport->Length && Width <= Transport->Length - Offset;
}

static APPLE_AGX_ASC_U64 AppleAgxWindowsNowMs(void *Context) {
  UNREFERENCED_PARAMETER(Context);
  return (APPLE_AGX_ASC_U64)(KeQueryInterruptTime() / 10000ULL);
}

static APPLE_AGX_ASC_BOOL AppleAgxWindowsRead32(void *Context,
                                                APPLE_AGX_ASC_U32 Offset,
                                                APPLE_AGX_ASC_U32 *Value) {
  APPLE_AGX_WINDOWS_ASC_TRANSPORT *transport = Context;
  if (Value == NULL || (Offset & (sizeof(ULONG) - 1u)) != 0u ||
      !AppleAgxAscRangeValid(transport, Offset, sizeof(ULONG)))
    return APPLE_AGX_ASC_FALSE;
  *Value = READ_REGISTER_ULONG((volatile ULONG *)(transport->Base + Offset));
  return APPLE_AGX_ASC_TRUE;
}

static APPLE_AGX_ASC_BOOL AppleAgxWindowsRead64(void *Context,
                                                APPLE_AGX_ASC_U32 Offset,
                                                APPLE_AGX_ASC_U64 *Value) {
  APPLE_AGX_WINDOWS_ASC_TRANSPORT *transport = Context;
  if (Value == NULL || (Offset & (sizeof(ULONG64) - 1u)) != 0u ||
      !AppleAgxAscRangeValid(transport, Offset, sizeof(ULONG64)))
    return APPLE_AGX_ASC_FALSE;
  *Value =
      READ_REGISTER_ULONG64((volatile ULONG64 *)(transport->Base + Offset));
  return APPLE_AGX_ASC_TRUE;
}

static APPLE_AGX_ASC_BOOL AppleAgxWindowsWrite32(void *Context,
                                                 APPLE_AGX_ASC_U32 Offset,
                                                 APPLE_AGX_ASC_U32 Value) {
  APPLE_AGX_WINDOWS_ASC_TRANSPORT *transport = Context;
  if ((Offset & (sizeof(ULONG) - 1u)) != 0u ||
      !AppleAgxAscRangeValid(transport, Offset, sizeof(ULONG)))
    return APPLE_AGX_ASC_FALSE;
  WRITE_REGISTER_ULONG((volatile ULONG *)(transport->Base + Offset), Value);
  return APPLE_AGX_ASC_TRUE;
}

static APPLE_AGX_ASC_BOOL AppleAgxWindowsWrite64(void *Context,
                                                 APPLE_AGX_ASC_U32 Offset,
                                                 APPLE_AGX_ASC_U64 Value) {
  APPLE_AGX_WINDOWS_ASC_TRANSPORT *transport = Context;
  if ((Offset & (sizeof(ULONG64) - 1u)) != 0u ||
      !AppleAgxAscRangeValid(transport, Offset, sizeof(ULONG64)))
    return APPLE_AGX_ASC_FALSE;
  WRITE_REGISTER_ULONG64((volatile ULONG64 *)(transport->Base + Offset), Value);
  return APPLE_AGX_ASC_TRUE;
}

_IRQL_requires_max_(APC_LEVEL) static APPLE_AGX_ASC_BOOL
    AppleAgxWindowsPause(void *Context) {
  LARGE_INTEGER interval;
  UNREFERENCED_PARAMETER(Context);
  interval.QuadPart = -10000LL;
  return NT_SUCCESS(KeDelayExecutionThread(KernelMode, FALSE, &interval))
             ? APPLE_AGX_ASC_TRUE
             : APPLE_AGX_ASC_FALSE;
}

_Use_decl_annotations_ NTSTATUS AppleAgxFirmwareTransportInitialize(
    volatile UCHAR *AscBase, ULONG AscLength,
    APPLE_AGX_WINDOWS_ASC_TRANSPORT *Transport, APPLE_AGX_ASC_IO *Io) {
  if (AscBase == NULL || Transport == NULL || Io == NULL ||
      AscLength != (ULONG)J313_AGX_G2_ASC_MMIO_SIZE)
    return STATUS_INVALID_PARAMETER;

  RtlZeroMemory(Transport, sizeof(*Transport));
  RtlZeroMemory(Io, sizeof(*Io));
  Transport->Base = AscBase;
  Transport->Length = AscLength;
  Io->Context = Transport;
  Io->NowMs = AppleAgxWindowsNowMs;
  Io->Read32 = AppleAgxWindowsRead32;
  Io->Read64 = AppleAgxWindowsRead64;
  Io->Write32 = AppleAgxWindowsWrite32;
  Io->Write64 = AppleAgxWindowsWrite64;
  Io->Pause = AppleAgxWindowsPause;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AppleAgxQualifyAscCpuStatus(
    volatile UCHAR *AscBase, ULONG AscLength, PULONG CpuStatus) {
  APPLE_AGX_WINDOWS_ASC_TRANSPORT transport;
  APPLE_AGX_ASC_IO io;
  APPLE_AGX_ASC_RESULT result;
  APPLE_AGX_ASC_U32 typedStatus = 0;
  NTSTATUS status;

  if (CpuStatus == NULL)
    return STATUS_INVALID_PARAMETER;
  *CpuStatus = 0;
  status =
      AppleAgxFirmwareTransportInitialize(AscBase, AscLength, &transport, &io);
  if (!NT_SUCCESS(status))
    return status;
  result = AppleAgxAscReadCpuStatus(&io, &typedStatus);
  if (result == AppleAgxAscResultOk) {
    *CpuStatus = (ULONG)typedStatus;
    return STATUS_SUCCESS;
  }
  if (result == AppleAgxAscResultInvalidArgument)
    return STATUS_INVALID_PARAMETER;
  return STATUS_IO_DEVICE_ERROR;
}

#endif

#ifdef APPLE_AGX_G2_RTKIT_QUALIFICATION
_Use_decl_annotations_ NTSTATUS AppleAgxQualifyRtkitReadyStop(
    volatile UCHAR *AscBase, ULONG AscLength) {
  APPLE_AGX_WINDOWS_ASC_TRANSPORT transport;
  APPLE_AGX_ASC_IO io;
  APPLE_AGX_RTKIT_SESSION session;
  APPLE_AGX_RTKIT_SESSION_RESULT result;
  APPLE_AGX_ASC_U64 deadline;
  NTSTATUS status = AppleAgxFirmwareTransportInitialize(
      AscBase, AscLength, &transport, &io);

  if (!NT_SUCCESS(status))
    return status;
  AppleAgxRtkitSessionInitialize(&session);
  deadline = (APPLE_AGX_ASC_U64)(KeQueryInterruptTime() / 10000ULL) + 5000ULL;
  result = AppleAgxRtkitSessionBoot(&session, &io, deadline);
  if (result != AppleAgxRtkitSessionResultOk)
    return result == AppleAgxRtkitSessionResultTimeout
               ? STATUS_IO_TIMEOUT
               : STATUS_DEVICE_PROTOCOL_ERROR;
  deadline = (APPLE_AGX_ASC_U64)(KeQueryInterruptTime() / 10000ULL) + 5000ULL;
  result = AppleAgxRtkitSessionStop(&session, &io, deadline);
  if (result == AppleAgxRtkitSessionResultOk)
    return STATUS_SUCCESS;
  return result == AppleAgxRtkitSessionResultTimeout
             ? STATUS_IO_TIMEOUT
             : STATUS_DEVICE_PROTOCOL_ERROR;
}
#endif
