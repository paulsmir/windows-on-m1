#include "render_gdi_receipt.h"

static void AdmissionGdiReceiptZero(void *Address, unsigned int Bytes) {
  unsigned char *destination = (unsigned char *)Address;
  unsigned int index;
  for (index = 0u; index < Bytes; ++index)
    destination[index] = 0u;
}

void AdmissionGdiReceiptInitialize(ADMISSION_GDI_HW_RECEIPT *Receipt) {
  if (Receipt == (void *)0)
    return;
  AdmissionGdiReceiptZero(Receipt, (unsigned int)sizeof(*Receipt));
  Receipt->Version = ADMISSION_GDI_RECEIPT_VERSION;
  Receipt->Bytes = (unsigned int)sizeof(*Receipt);
}

int AdmissionGdiReceiptBegin(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned long long ContextToken, unsigned int Opcode, unsigned int Color,
    unsigned int RectCount, unsigned int DmaBytes) {
  if (Receipt == (void *)0 || Receipt->Version != ADMISSION_GDI_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) ||
      Receipt->Stage != AdmissionGdiReceiptStageEmpty || ContextToken == 0ULL ||
      Opcode == 0u || DmaBytes == 0u)
    return 0;
  Receipt->ContextToken = ContextToken;
  Receipt->Opcode = Opcode;
  Receipt->Color = Color;
  Receipt->RectCount = RectCount;
  Receipt->DmaBytes = DmaBytes;
  Receipt->RenderKmStatus = 0u;
  Receipt->Stage = AdmissionGdiReceiptStageRenderKm;
  return 1;
}

int AdmissionGdiReceiptPatch(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned long long ContextToken, unsigned int Fence,
    unsigned long long DestinationGpuVa,
    unsigned long long DestinationPhysical, unsigned int DestinationBytes) {
  if (Receipt == (void *)0 ||
      Receipt->Stage != AdmissionGdiReceiptStageRenderKm ||
      Receipt->ContextToken != ContextToken || Fence == 0u ||
      DestinationGpuVa == 0ULL || DestinationPhysical == 0ULL ||
      DestinationBytes == 0u)
    return 0;
  Receipt->Fence = Fence;
  Receipt->DestinationGpuVa = DestinationGpuVa;
  Receipt->DestinationPhysical = DestinationPhysical;
  Receipt->DestinationBytes = DestinationBytes;
  Receipt->PatchStatus = 0u;
  Receipt->Stage = AdmissionGdiReceiptStagePatch;
  return 1;
}

int AdmissionGdiReceiptSubmit(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned long long ContextToken, unsigned int Fence, unsigned int Status) {
  if (Receipt == (void *)0 || Receipt->Stage != AdmissionGdiReceiptStagePatch ||
      Receipt->ContextToken != ContextToken || Receipt->Fence != Fence)
    return 0;
  Receipt->SubmitStatus = Status;
  Receipt->Stage = AdmissionGdiReceiptStageSubmit;
  return 1;
}

int AdmissionGdiReceiptBackend(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned int Fence, unsigned int Result, unsigned int TaEvent,
    unsigned int D3Event, unsigned int TaExpectedStamp,
    unsigned int D3ExpectedStamp, unsigned int TaExpectedDone,
    unsigned int D3ExpectedDone) {
  if (Receipt == (void *)0 || Receipt->Stage != AdmissionGdiReceiptStageSubmit ||
      Receipt->Fence != Fence ||
      (Result == 0u && (TaEvent >= 128u || D3Event >= 128u ||
       TaEvent == D3Event || TaExpectedStamp == 0u ||
       D3ExpectedStamp == 0u)))
    return 0;
  Receipt->BackendSubmitResult = Result;
  Receipt->TaEvent = TaEvent;
  Receipt->D3Event = D3Event;
  Receipt->TaExpectedStamp = TaExpectedStamp;
  Receipt->D3ExpectedStamp = D3ExpectedStamp;
  Receipt->TaExpectedDone = TaExpectedDone;
  Receipt->D3ExpectedDone = D3ExpectedDone;
  Receipt->Stage = AdmissionGdiReceiptStageBackend;
  return 1;
}

int AdmissionGdiReceiptComplete(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned int Fence, unsigned int Status, unsigned int NotifyInterrupt) {
  if (Receipt == (void *)0 ||
      Receipt->Stage != AdmissionGdiReceiptStageBackend ||
      Receipt->Fence != Fence)
    return 0;
  Receipt->CompletionStatus = Status;
  Receipt->CompletionFence = Fence;
  Receipt->NotifyInterrupt = NotifyInterrupt;
  Receipt->Stage = Receipt->NotifyDpc
                       ? AdmissionGdiReceiptStageDpc
                       : AdmissionGdiReceiptStageComplete;
  return 1;
}

int AdmissionGdiReceiptProgress(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned int Fence, unsigned int TaDone, unsigned int TaStamp,
    unsigned int TaEventSeen, unsigned int TaComplete, unsigned int D3Done,
    unsigned int D3Stamp, unsigned int D3EventSeen, unsigned int D3Complete,
    unsigned int WorkerFinalPhase) {
  if (Receipt == (void *)0 ||
      Receipt->Stage < AdmissionGdiReceiptStageBackend ||
      Receipt->Fence != Fence)
    return 0;
  Receipt->ProgressFence = Fence;
  Receipt->TaDone = TaDone;
  Receipt->TaStamp = TaStamp;
  Receipt->TaEventSeen = TaEventSeen;
  Receipt->TaComplete = TaComplete;
  Receipt->D3Done = D3Done;
  Receipt->D3Stamp = D3Stamp;
  Receipt->D3EventSeen = D3EventSeen;
  Receipt->D3Complete = D3Complete;
  Receipt->WorkerFinalPhase = WorkerFinalPhase;
  if (Receipt->Stage >= AdmissionGdiReceiptStageComplete &&
      Receipt->Stage < AdmissionGdiReceiptStageProgress)
    Receipt->Stage = AdmissionGdiReceiptStageProgress;
  return 1;
}

int AdmissionGdiReceiptDpc(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned int Fence) {
  if (Receipt == (void *)0 ||
      Receipt->Stage < AdmissionGdiReceiptStageBackend ||
      Receipt->Fence != Fence)
    return 0;
  Receipt->NotifyDpc = 1u;
  if (Receipt->Stage >= AdmissionGdiReceiptStageComplete)
    Receipt->Stage = AdmissionGdiReceiptStageDpc;
  return 1;
}

void AdmissionTerminalReceiptInitialize(ADMISSION_TERMINAL_RECEIPT *Receipt) {
  if (Receipt == (void *)0)
    return;
  AdmissionGdiReceiptZero(Receipt, (unsigned int)sizeof(*Receipt));
  Receipt->Version = ADMISSION_TERMINAL_RECEIPT_VERSION;
  Receipt->Bytes = (unsigned int)sizeof(*Receipt);
}

int AdmissionTerminalReceiptBegin(ADMISSION_TERMINAL_RECEIPT *Receipt,
    unsigned int SubmissionSequence, unsigned long long BootEpoch,
    unsigned long long RootIdentity, unsigned int Fence,
    unsigned long long ContextToken, unsigned long long AllocationToken,
    unsigned long long DestinationGpuVa,
    unsigned long long DestinationPhysical, unsigned int DestinationBytes,
    unsigned int TaEvent, unsigned int D3Event,
    unsigned int TaExpectedStamp, unsigned int D3ExpectedStamp,
    unsigned int TaExpectedDone, unsigned int D3ExpectedDone,
    unsigned long long StatsTaStart, unsigned long long StatsTaFinalize,
    unsigned long long Stats3dStart, unsigned long long Stats3dFinalize) {
  if (Receipt == (void *)0 ||
      Receipt->Version != ADMISSION_TERMINAL_RECEIPT_VERSION ||
      Receipt->Bytes != sizeof(*Receipt) || Receipt->ValidMask != 0u ||
      SubmissionSequence == 0u || BootEpoch == 0ULL ||
      RootIdentity == 0ULL || Fence == 0u || ContextToken == 0ULL ||
      AllocationToken == 0ULL || DestinationGpuVa == 0ULL ||
      DestinationPhysical == 0ULL || DestinationBytes == 0u ||
      TaEvent >= 128u || D3Event >= 128u || TaEvent == D3Event ||
      TaExpectedStamp == 0u || D3ExpectedStamp == 0u ||
      StatsTaStart == 0ULL || StatsTaFinalize == 0ULL ||
      Stats3dStart == 0ULL || Stats3dFinalize == 0ULL)
    return 0;
  Receipt->SubmissionSequence = SubmissionSequence;
  Receipt->BootEpoch = BootEpoch;
  Receipt->RootIdentity = RootIdentity;
  Receipt->Fence = Fence;
  Receipt->ContextToken = ContextToken;
  Receipt->AllocationToken = AllocationToken;
  Receipt->DestinationGpuVa = DestinationGpuVa;
  Receipt->DestinationPhysical = DestinationPhysical;
  Receipt->DestinationBytes = DestinationBytes;
  Receipt->TaEvent = TaEvent;
  Receipt->D3Event = D3Event;
  Receipt->TaExpectedStamp = TaExpectedStamp;
  Receipt->D3ExpectedStamp = D3ExpectedStamp;
  Receipt->TaExpectedDone = TaExpectedDone;
  Receipt->D3ExpectedDone = D3ExpectedDone;
  Receipt->StatsTaStart = StatsTaStart;
  Receipt->StatsTaFinalize = StatsTaFinalize;
  Receipt->Stats3dStart = Stats3dStart;
  Receipt->Stats3dFinalize = Stats3dFinalize;
  Receipt->ValidMask = ADMISSION_TERMINAL_VALID_BEGIN;
  return 1;
}

int AdmissionTerminalReceiptObserve(ADMISSION_TERMINAL_RECEIPT *Receipt,
    unsigned int Fence, unsigned int BackendResult,
    unsigned int CompletionStatus, unsigned int Source,
    const unsigned char *RawEvent, unsigned int RawEventBytes,
    unsigned int ActualValid,
    unsigned int TaObservedStamp, unsigned int TaObservedDone,
    unsigned int D3ObservedStamp, unsigned int D3ObservedDone) {
  unsigned int index;
  if (Receipt == (void *)0 ||
      Receipt->ValidMask != ADMISSION_TERMINAL_VALID_BEGIN ||
      Receipt->Fence != Fence || Source == AdmissionTerminalSourceNone ||
      Source > AdmissionTerminalSourceCancellation || ActualValid > 1u ||
      ((RawEvent == (void *)0 && RawEventBytes != 0u) ||
       (RawEvent != (void *)0 &&
        RawEventBytes != ADMISSION_TERMINAL_RAW_EVENT_BYTES)))
    return 0;
  Receipt->BackendResult = BackendResult;
  Receipt->CompletionStatus = CompletionStatus;
  Receipt->CompletedFence = Fence;
  Receipt->Source = Source;
  if (ActualValid) {
    Receipt->TaObservedStamp = TaObservedStamp;
    Receipt->TaObservedDone = TaObservedDone;
    Receipt->D3ObservedStamp = D3ObservedStamp;
    Receipt->D3ObservedDone = D3ObservedDone;
    Receipt->ValidMask |= ADMISSION_TERMINAL_VALID_ACTUAL;
  }
  if (RawEvent != (void *)0) {
    Receipt->RawEventBytes = RawEventBytes;
    for (index = 0u; index < RawEventBytes; ++index)
      Receipt->RawEvent[index] = RawEvent[index];
    Receipt->ValidMask |= ADMISSION_TERMINAL_VALID_RAW_EVENT;
  }
  Receipt->ValidMask |= ADMISSION_TERMINAL_VALID_TERMINAL;
  return 1;
}

int AdmissionTerminalReceiptNotifyInterrupt(
    ADMISSION_TERMINAL_RECEIPT *Receipt, unsigned int Fence) {
  if (Receipt == (void *)0 || Receipt->Fence != Fence ||
      !(Receipt->ValidMask & ADMISSION_TERMINAL_VALID_TERMINAL))
    return 0;
  Receipt->NotifyInterrupt = 1u;
  Receipt->ValidMask |= ADMISSION_TERMINAL_VALID_INTERRUPT;
  return 1;
}

int AdmissionTerminalReceiptNotifyDpc(
    ADMISSION_TERMINAL_RECEIPT *Receipt, unsigned int Fence) {
  if (Receipt == (void *)0 || Receipt->Fence != Fence ||
      !(Receipt->ValidMask & ADMISSION_TERMINAL_VALID_INTERRUPT))
    return 0;
  Receipt->NotifyDpc = 1u;
  Receipt->ValidMask |= ADMISSION_TERMINAL_VALID_DPC;
  return 1;
}

int AdmissionTerminalReceiptExit(ADMISSION_TERMINAL_RECEIPT *Receipt,
    unsigned int WorkerExitReason, unsigned int ProviderPhase,
    unsigned int RuntimePhase, unsigned int Stopping, unsigned int Resetting,
    unsigned int SchedulerFaulted) {
  if (Receipt == (void *)0 ||
      !(Receipt->ValidMask & ADMISSION_TERMINAL_VALID_BEGIN) ||
      (Receipt->ValidMask & ADMISSION_TERMINAL_VALID_EXIT) ||
      WorkerExitReason == AdmissionTerminalExitNone ||
      WorkerExitReason > AdmissionTerminalExitNonterminal ||
      Stopping > 1u || Resetting > 1u || SchedulerFaulted > 1u)
    return 0;
  Receipt->WorkerExitReason = WorkerExitReason;
  Receipt->ProviderPhase = ProviderPhase;
  Receipt->RuntimePhase = RuntimePhase;
  Receipt->Stopping = Stopping;
  Receipt->Resetting = Resetting;
  Receipt->SchedulerFaulted = SchedulerFaulted;
  Receipt->ValidMask |= ADMISSION_TERMINAL_VALID_EXIT;
  return 1;
}

int AdmissionTerminalReceiptCaptureOutput(
    ADMISSION_TERMINAL_RECEIPT *Receipt, unsigned int Fence,
    const unsigned char *Bytes, unsigned int TargetBytes,
    unsigned int ExaminedBytes, unsigned int ExpectedPixel,
    unsigned char PoisonByte) {
  unsigned int index;
  unsigned int prefixBytes;
  unsigned int poisonPixel = (unsigned int)PoisonByte * 0x01010101u;
  unsigned long long hash = 0xcbf29ce484222325ULL;
  if (Receipt == (void *)0 || Bytes == (void *)0 ||
      !(Receipt->ValidMask & ADMISSION_TERMINAL_VALID_TERMINAL) ||
      Receipt->Fence != Fence || TargetBytes == 0u ||
      (TargetBytes & 3u) != 0u || ExaminedBytes < TargetBytes)
    return 0;
  Receipt->OutputFirstMismatchIndex = 0xffffffffu;
  Receipt->OutputFirstPixelActual =
      (unsigned int)Bytes[0] | ((unsigned int)Bytes[1] << 8u) |
      ((unsigned int)Bytes[2] << 16u) |
      ((unsigned int)Bytes[3] << 24u);
  for (index = 0u; index < TargetBytes / 4u; ++index) {
    const unsigned char *pixel = Bytes + index * 4u;
    unsigned int actual =
        (unsigned int)pixel[0] | ((unsigned int)pixel[1] << 8u) |
        ((unsigned int)pixel[2] << 16u) |
        ((unsigned int)pixel[3] << 24u);
    if (actual == ExpectedPixel)
      ++Receipt->OutputPixelsExpected;
    else if (Receipt->OutputFirstMismatchIndex == 0xffffffffu) {
      Receipt->OutputFirstMismatchIndex = index;
      Receipt->OutputFirstMismatchActual = actual;
    }
    if (actual == poisonPixel)
      ++Receipt->OutputPixelsPoison;
  }
  for (index = 0u; index < TargetBytes; ++index) {
    if (Bytes[index] != PoisonByte)
      ++Receipt->OutputChangedBytes;
    hash ^= Bytes[index];
    hash *= 0x100000001b3ULL;
  }
  for (index = TargetBytes; index < ExaminedBytes; ++index)
    if (Bytes[index] != PoisonByte)
      ++Receipt->OutputGuardCorrupt;
  prefixBytes = ExaminedBytes < ADMISSION_TERMINAL_OUTPUT_PREFIX_BYTES
                    ? ExaminedBytes
                    : ADMISSION_TERMINAL_OUTPUT_PREFIX_BYTES;
  for (index = 0u; index < prefixBytes; ++index)
    Receipt->OutputPrefix[index] = Bytes[index];
  Receipt->OutputBytesExamined = ExaminedBytes;
  Receipt->OutputTargetFnv1a = hash;
  Receipt->ValidMask |= ADMISSION_TERMINAL_VALID_OUTPUT;
  return 1;
}
