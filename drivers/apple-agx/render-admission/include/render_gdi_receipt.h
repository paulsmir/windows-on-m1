#ifndef APPLE_AGX_RENDER_GDI_RECEIPT_H
#define APPLE_AGX_RENDER_GDI_RECEIPT_H

#define ADMISSION_GDI_RECEIPT_VERSION 1u

typedef enum _ADMISSION_GDI_RECEIPT_STAGE {
  AdmissionGdiReceiptStageEmpty = 0u,
  AdmissionGdiReceiptStageRenderKm = 1u,
  AdmissionGdiReceiptStagePatch = 2u,
  AdmissionGdiReceiptStageSubmit = 3u,
  AdmissionGdiReceiptStageBackend = 4u,
  AdmissionGdiReceiptStageComplete = 5u,
  AdmissionGdiReceiptStageProgress = 6u,
  AdmissionGdiReceiptStageDpc = 7u,
} ADMISSION_GDI_RECEIPT_STAGE;

typedef struct _ADMISSION_GDI_HW_RECEIPT {
  unsigned int Version, Bytes, Stage;
  unsigned int RenderKmStatus, PatchStatus, SubmitStatus;
  unsigned int BackendSubmitResult, CompletionStatus, Fence;
  unsigned int Opcode, Color, RectCount, DmaBytes;
  unsigned int TaEvent, D3Event, TaExpectedStamp, D3ExpectedStamp;
  unsigned int TaExpectedDone, D3ExpectedDone, ProgressFence;
  unsigned int TaDone, TaStamp, TaEventSeen, TaComplete;
  unsigned int D3Done, D3Stamp, D3EventSeen, D3Complete;
  unsigned int NotifyInterrupt, NotifyDpc, WorkerFinalPhase;
  unsigned int CompletionFence;
  unsigned int DestinationBytes;
  unsigned long long ContextToken;
  unsigned long long DestinationGpuVa;
  unsigned long long DestinationPhysical;
} ADMISSION_GDI_HW_RECEIPT;

void AdmissionGdiReceiptInitialize(ADMISSION_GDI_HW_RECEIPT *Receipt);
int AdmissionGdiReceiptBegin(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned long long ContextToken, unsigned int Opcode, unsigned int Color,
    unsigned int RectCount, unsigned int DmaBytes);
int AdmissionGdiReceiptPatch(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned long long ContextToken, unsigned int Fence,
    unsigned long long DestinationGpuVa,
    unsigned long long DestinationPhysical, unsigned int DestinationBytes);
int AdmissionGdiReceiptSubmit(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned long long ContextToken, unsigned int Fence, unsigned int Status);
int AdmissionGdiReceiptBackend(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned int Fence, unsigned int Result, unsigned int TaEvent,
    unsigned int D3Event, unsigned int TaExpectedStamp,
    unsigned int D3ExpectedStamp, unsigned int TaExpectedDone,
    unsigned int D3ExpectedDone);
int AdmissionGdiReceiptComplete(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned int Fence, unsigned int Status, unsigned int NotifyInterrupt);
int AdmissionGdiReceiptProgress(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned int Fence, unsigned int TaDone, unsigned int TaStamp,
    unsigned int TaEventSeen, unsigned int TaComplete, unsigned int D3Done,
    unsigned int D3Stamp, unsigned int D3EventSeen, unsigned int D3Complete,
    unsigned int WorkerFinalPhase);
int AdmissionGdiReceiptDpc(ADMISSION_GDI_HW_RECEIPT *Receipt,
    unsigned int Fence);

#endif /* APPLE_AGX_RENDER_GDI_RECEIPT_H */
