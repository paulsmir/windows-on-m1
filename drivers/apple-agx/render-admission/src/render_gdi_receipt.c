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
      (Result == 0u && (TaEvent == 0u || D3Event == 0u ||
       TaExpectedStamp == 0u || D3ExpectedStamp == 0u)))
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
  if (Receipt == (void *)0 || Receipt->Stage != AdmissionGdiReceiptStageBackend ||
      Receipt->Fence != Fence)
    return 0;
  Receipt->CompletionStatus = Status;
  Receipt->CompletionFence = Fence;
  Receipt->NotifyInterrupt = NotifyInterrupt;
  Receipt->Stage = AdmissionGdiReceiptStageComplete;
  return 1;
}

int AdmissionGdiReceiptProgress(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned int Fence, unsigned int TaDone, unsigned int TaStamp,
    unsigned int TaEventSeen, unsigned int TaComplete, unsigned int D3Done,
    unsigned int D3Stamp, unsigned int D3EventSeen, unsigned int D3Complete,
    unsigned int WorkerFinalPhase) {
  if (Receipt == (void *)0 ||
      Receipt->Stage < AdmissionGdiReceiptStageComplete ||
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
  if (Receipt->Stage < AdmissionGdiReceiptStageProgress)
    Receipt->Stage = AdmissionGdiReceiptStageProgress;
  return 1;
}

int AdmissionGdiReceiptDpc(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned int Fence) {
  if (Receipt == (void *)0 ||
      Receipt->Stage < AdmissionGdiReceiptStageComplete ||
      Receipt->Fence != Fence)
    return 0;
  Receipt->NotifyDpc = 1u;
  Receipt->Stage = AdmissionGdiReceiptStageDpc;
  return 1;
}
