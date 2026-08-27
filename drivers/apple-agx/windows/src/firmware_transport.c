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
static NTSTATUS AppleAgxRtkitSessionStatus(
    APPLE_AGX_RTKIT_SESSION_RESULT Result) {
  if (Result == AppleAgxRtkitSessionResultOk)
    return STATUS_SUCCESS;
  if (Result == AppleAgxRtkitSessionResultTimeout)
    return STATUS_IO_TIMEOUT;
  if (Result == AppleAgxRtkitSessionResultInvalidArgument)
    return STATUS_INVALID_PARAMETER;
  if (Result == AppleAgxRtkitSessionResultInvalidState)
    return STATUS_INVALID_DEVICE_STATE;
  return STATUS_DEVICE_PROTOCOL_ERROR;
}

static ULONG AppleAgxRtkitBootFlags(
    const APPLE_AGX_RTKIT_SESSION *Session) {
  ULONG flags = 0;

  if (Session->Boot.Begun != APPLE_AGX_RTKIT_FALSE)
    flags |= APPLE_AGX_RTKIT_BOOT_FLAG_BEGUN;
  if (Session->Boot.HelloSeen != APPLE_AGX_RTKIT_FALSE)
    flags |= APPLE_AGX_RTKIT_BOOT_FLAG_HELLO_SEEN;
  if (Session->Boot.EndpointMapComplete != APPLE_AGX_RTKIT_FALSE)
    flags |= APPLE_AGX_RTKIT_BOOT_FLAG_ENDPOINT_MAP_COMPLETE;
  if (Session->Boot.IopPowerReady != APPLE_AGX_RTKIT_FALSE)
    flags |= APPLE_AGX_RTKIT_BOOT_FLAG_IOP_POWER_READY;
  if (Session->Boot.ApPowerRequested != APPLE_AGX_RTKIT_FALSE)
    flags |= APPLE_AGX_RTKIT_BOOT_FLAG_AP_POWER_REQUESTED;
  if (Session->Boot.ApPowerReady != APPLE_AGX_RTKIT_FALSE)
    flags |= APPLE_AGX_RTKIT_BOOT_FLAG_AP_POWER_READY;
  if (Session->Running != APPLE_AGX_RTKIT_FALSE)
    flags |= APPLE_AGX_RTKIT_BOOT_FLAG_RUNNING;
  if (Session->CpuReady != APPLE_AGX_RTKIT_FALSE)
    flags |= APPLE_AGX_RTKIT_BOOT_FLAG_CPU_READY;
  return flags;
}

static ULONG AppleAgxRtkitMailboxSnapshotFlags(
    const APPLE_AGX_RTKIT_SESSION *Session) {
  ULONG flags = 0;
  if (Session->InboxBeforeInitValid != APPLE_AGX_RTKIT_FALSE)
    flags |= APPLE_AGX_RTKIT_MAILBOX_BEFORE_INIT_VALID;
  if (Session->InboxAfterInitValid != APPLE_AGX_RTKIT_FALSE)
    flags |= APPLE_AGX_RTKIT_MAILBOX_AFTER_INIT_VALID;
  if (Session->InboxAtFailureValid != APPLE_AGX_RTKIT_FALSE)
    flags |= APPLE_AGX_RTKIT_MAILBOX_INBOX_FAILURE_VALID;
  if (Session->OutboxAtFailureValid != APPLE_AGX_RTKIT_FALSE)
    flags |= APPLE_AGX_RTKIT_MAILBOX_OUTBOX_FAILURE_VALID;
  return flags;
}

_Use_decl_annotations_ NTSTATUS AppleAgxQualifyRtkitReadyStop(
    volatile UCHAR *AscBase, ULONG AscLength,
    APPLE_AGX_RTKIT_QUALIFICATION_RESULT *Result) {
  APPLE_AGX_WINDOWS_ASC_TRANSPORT transport;
  APPLE_AGX_ASC_IO io;
  APPLE_AGX_RTKIT_SESSION session;
  APPLE_AGX_RTKIT_SESSION_RESULT sessionResult;
  APPLE_AGX_ASC_RESULT cpuStatusResult;
  APPLE_AGX_ASC_U32 cpuStatus = 0;
  APPLE_AGX_ASC_U64 deadline;
  NTSTATUS status;

  if (Result == NULL)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(Result, sizeof(*Result));
  Result->BootStatus = STATUS_NOT_SUPPORTED;
  Result->StopStatus = STATUS_NOT_SUPPORTED;
  Result->FinalCpuStatusReadStatus = STATUS_NOT_SUPPORTED;
  status = AppleAgxFirmwareTransportInitialize(AscBase, AscLength, &transport,
                                               &io);
  if (!NT_SUCCESS(status))
    return status;
  AppleAgxRtkitSessionInitialize(&session);
  deadline = (APPLE_AGX_ASC_U64)(KeQueryInterruptTime() / 10000ULL) + 5000ULL;
  sessionResult = AppleAgxRtkitSessionBoot(&session, &io, deadline);
  Result->BootStatus = AppleAgxRtkitSessionStatus(sessionResult);
  Result->BootPhase = (ULONG)session.Boot.Phase;
  Result->BootFlags = AppleAgxRtkitBootFlags(&session);
  Result->NegotiatedVersion = (ULONG)session.Boot.NegotiatedVersion;
  Result->MailboxSnapshotFlags =
      AppleAgxRtkitMailboxSnapshotFlags(&session);
  Result->InboxControlBeforeInit = (ULONG)session.InboxControlBeforeInit;
  Result->InboxControlAfterInit = (ULONG)session.InboxControlAfterInit;
  Result->InboxControlAtFailure = (ULONG)session.InboxControlAtFailure;
  Result->OutboxControlAtFailure = (ULONG)session.OutboxControlAtFailure;
  if (sessionResult == AppleAgxRtkitSessionResultOk) {
    deadline =
        (APPLE_AGX_ASC_U64)(KeQueryInterruptTime() / 10000ULL) + 5000ULL;
    sessionResult = AppleAgxRtkitSessionStop(&session, &io, deadline);
    Result->StopStatus = AppleAgxRtkitSessionStatus(sessionResult);
  }
  cpuStatusResult = AppleAgxAscReadCpuStatus(&io, &cpuStatus);
  Result->FinalCpuStatusReadStatus =
      cpuStatusResult == AppleAgxAscResultOk ? STATUS_SUCCESS
                                            : STATUS_IO_DEVICE_ERROR;
  Result->FinalCpuStatus = (ULONG)cpuStatus;
  if (!NT_SUCCESS(Result->BootStatus))
    return Result->BootStatus;
  return Result->StopStatus;
}
#endif
