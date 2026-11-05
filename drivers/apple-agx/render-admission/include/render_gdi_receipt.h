#ifndef APPLE_AGX_RENDER_GDI_RECEIPT_H
#define APPLE_AGX_RENDER_GDI_RECEIPT_H

#include "render_dynamic_output.h"

#define ADMISSION_GDI_RECEIPT_VERSION 1u
#define ADMISSION_TERMINAL_RECEIPT_VERSION 1u
#define ADMISSION_TERMINAL_RAW_EVENT_BYTES 56u
#define ADMISSION_TERMINAL_VALID_BEGIN 0x01u
#define ADMISSION_TERMINAL_VALID_TERMINAL 0x02u
#define ADMISSION_TERMINAL_VALID_INTERRUPT 0x04u
#define ADMISSION_TERMINAL_VALID_DPC 0x08u
#define ADMISSION_TERMINAL_VALID_EXIT 0x10u
#define ADMISSION_TERMINAL_VALID_RAW_EVENT 0x20u
#define ADMISSION_TERMINAL_VALID_ACTUAL 0x40u
#define ADMISSION_TERMINAL_VALID_OUTPUT 0x80u
#define ADMISSION_TERMINAL_VALID_ALL 0xffu
#define ADMISSION_TERMINAL_OUTPUT_PREFIX_BYTES 64u

typedef enum _ADMISSION_TERMINAL_SOURCE {
  AdmissionTerminalSourceNone = 0u,
  AdmissionTerminalSourcePollingEvent = 1u,
  AdmissionTerminalSourceTimeout = 2u,
  AdmissionTerminalSourceFault = 3u,
  AdmissionTerminalSourceCancellation = 4u,
} ADMISSION_TERMINAL_SOURCE;

typedef enum _ADMISSION_TERMINAL_EXIT_REASON {
  AdmissionTerminalExitNone = 0u,
  AdmissionTerminalExitCompleted = 1u,
  AdmissionTerminalExitBackendFailure = 2u,
  AdmissionTerminalExitPollFailure = 3u,
  AdmissionTerminalExitStopped = 4u,
  AdmissionTerminalExitReset = 5u,
  AdmissionTerminalExitNonterminal = 6u,
} ADMISSION_TERMINAL_EXIT_REASON;

typedef struct _ADMISSION_TERMINAL_RECEIPT {
  unsigned int Version, Bytes, ValidMask, SubmissionSequence;
  unsigned int Fence, BackendResult, CompletionStatus, CompletedFence;
  unsigned int TaEvent, D3Event, TaExpectedStamp, D3ExpectedStamp;
  unsigned int TaExpectedDone, D3ExpectedDone;
  unsigned int TaObservedStamp, TaObservedDone;
  unsigned int D3ObservedStamp, D3ObservedDone;
  unsigned int Source, NotifyInterrupt, NotifyDpc, WorkerExitReason;
  unsigned int ProviderPhase, RuntimePhase, Stopping, Resetting;
  unsigned int SchedulerFaulted, DestinationBytes, RawEventBytes;
  unsigned int EventReadPointer, EventWritePointer;
  unsigned int OutputFirstPixelActual, OutputFirstMismatchIndex;
  unsigned int OutputFirstMismatchActual, OutputPixelsExpected;
  unsigned int OutputPixelsPoison, OutputChangedBytes;
  unsigned int OutputGuardCorrupt, OutputBytesExamined;
  unsigned long long BootEpoch, RootIdentity;
  unsigned long long ContextToken, AllocationToken;
  unsigned long long DestinationGpuVa, DestinationPhysical;
  unsigned long long StatsTaStart, StatsTaFinalize;
  unsigned long long Stats3dStart, Stats3dFinalize;
  unsigned long long OutputTargetFnv1a;
  unsigned char RawEvent[ADMISSION_TERMINAL_RAW_EVENT_BYTES];
  unsigned char OutputPrefix[ADMISSION_TERMINAL_OUTPUT_PREFIX_BYTES];
} ADMISSION_TERMINAL_RECEIPT;

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
void AdmissionTerminalReceiptInitialize(ADMISSION_TERMINAL_RECEIPT *Receipt);
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
    unsigned long long Stats3dStart, unsigned long long Stats3dFinalize);
int AdmissionTerminalReceiptObserve(ADMISSION_TERMINAL_RECEIPT *Receipt,
    unsigned int Fence, unsigned int BackendResult,
    unsigned int CompletionStatus, unsigned int Source,
    const unsigned char *RawEvent, unsigned int RawEventBytes,
    unsigned int ActualValid,
    unsigned int TaObservedStamp, unsigned int TaObservedDone,
    unsigned int D3ObservedStamp, unsigned int D3ObservedDone);
int AdmissionTerminalReceiptNotifyInterrupt(
    ADMISSION_TERMINAL_RECEIPT *Receipt, unsigned int Fence);
int AdmissionTerminalReceiptNotifyDpc(
    ADMISSION_TERMINAL_RECEIPT *Receipt, unsigned int Fence);
int AdmissionTerminalReceiptExit(ADMISSION_TERMINAL_RECEIPT *Receipt,
    unsigned int WorkerExitReason, unsigned int ProviderPhase,
    unsigned int RuntimePhase, unsigned int Stopping, unsigned int Resetting,
    unsigned int SchedulerFaulted);
int AdmissionTerminalReceiptCaptureOutput(
    ADMISSION_TERMINAL_RECEIPT *Receipt, unsigned int Fence,
    const unsigned char *Bytes, unsigned int TargetBytes,
    unsigned int ExaminedBytes, unsigned int ExpectedPixel,
    unsigned char PoisonByte);
typedef int (*ADMISSION_TERMINAL_OUTPUT_PROGRESS)(void *Context);
int AdmissionTerminalReceiptCaptureOutputProgress(
    ADMISSION_TERMINAL_RECEIPT *Receipt, unsigned int Fence,
    const unsigned char *Bytes, unsigned int TargetBytes,
    unsigned int ExaminedBytes, unsigned int ExpectedPixel,
    unsigned char PoisonByte, unsigned int ChunkBytes,
    ADMISSION_TERMINAL_OUTPUT_PROGRESS Progress, void *ProgressContext);
int AdmissionTerminalReceiptCaptureTriangleOutputProgress(
    ADMISSION_TERMINAL_RECEIPT *Receipt, unsigned int Fence,
    const unsigned char *Bytes, unsigned int TargetBytes,
    const ADMISSION_DYNAMIC_OUTPUT_EXPECTATION *Expectation,
    unsigned int ChunkBytes, ADMISSION_TERMINAL_OUTPUT_PROGRESS Progress,
    void *ProgressContext, unsigned int *ObservedForegroundColor,
    ADMISSION_DYNAMIC_OUTPUT_RESULT *OutputResult);

#endif /* APPLE_AGX_RENDER_GDI_RECEIPT_H */
