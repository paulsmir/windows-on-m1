#include "render_admission.h"

#define ADMISSION_DYNAMIC_SHADER_BASE 0x1100000000ULL
#define ADMISSION_DYNAMIC_BACKGROUND_COLOR 0xff101820u

typedef struct _ADMISSION_DYNAMIC_BUILD_CONTEXT {
  ADMISSION_CONTEXT *Adapter;
  ADMISSION_RENDER_CONTEXT *Context;
  const DXGKARG_RENDER *Args;
  const ADMISSION_WIN32_RENDER_SNAPSHOT *Snapshot;
  const ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan;
} ADMISSION_DYNAMIC_BUILD_CONTEXT;

_Success_(return != FALSE)
static BOOLEAN AdmissionDynamicOpenReference(
    _In_ const ADMISSION_DYNAMIC_BUILD_CONTEXT *Build,
    _In_ APPLE_AGX_U32 ReferenceIndex,
    _Outptr_result_maybenull_ const ADMISSION_OPEN_ALLOCATION **Opened,
    _Outptr_result_maybenull_ const DXGK_ALLOCATIONLIST **Allocation,
    _Outptr_result_maybenull_
        const APPLE_AGX_WIN32_ALLOCATION_REFERENCE **Reference) {
  APPLE_AGX_U32 allocationIndex;
  const ADMISSION_OPEN_ALLOCATION *opened;
  if (Opened != NULL)
    *Opened = NULL;
  if (Allocation != NULL)
    *Allocation = NULL;
  if (Reference != NULL)
    *Reference = NULL;
  if (Build == NULL || Build->Adapter == NULL || Build->Args == NULL ||
      Build->Snapshot == NULL || Build->Snapshot->View.Header == NULL ||
      Build->Snapshot->View.References == NULL || Opened == NULL ||
      Allocation == NULL || Reference == NULL ||
      ReferenceIndex >= Build->Snapshot->View.Header->ReferenceCount)
    return FALSE;
  *Reference = &Build->Snapshot->View.References[ReferenceIndex];
  allocationIndex = (*Reference)->AllocationIndex;
  if (Build->Args->pAllocationList == NULL ||
      allocationIndex >= Build->Args->AllocationListSize)
    return FALSE;
  *Allocation = &Build->Args->pAllocationList[allocationIndex];
  opened = (const ADMISSION_OPEN_ALLOCATION *)
      (*Allocation)->hDeviceSpecificAllocation;
  if (opened == NULL || opened->Magic != ADMISSION_OPEN_ALLOCATION_MAGIC ||
      opened->Device == NULL ||
      &opened->Device->Object != Build->Context->Object.Device ||
      opened->Allocation == NULL ||
      !AdmissionAllocationDescriptionValid(&opened->Allocation->Description) ||
      (ULONGLONG)(ULONG_PTR)opened !=
          Build->Snapshot->Facts[ReferenceIndex].AllocationToken)
    return FALSE;
  *Opened = opened;
  return TRUE;
}

static int AdmissionDynamicRead(
    void *CallbackContext, APPLE_AGX_U64 AllocationToken,
    APPLE_AGX_U32 ReferenceIndex, APPLE_AGX_U32 Role,
    APPLE_AGX_U64 Offset, APPLE_AGX_U32 Bytes, void *Destination) {
  ADMISSION_DYNAMIC_BUILD_CONTEXT *build =
      (ADMISSION_DYNAMIC_BUILD_CONTEXT *)CallbackContext;
  const ADMISSION_OPEN_ALLOCATION *opened;
  const DXGK_ALLOCATIONLIST *allocation;
  const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *reference;
  ULONGLONG alignedSize;
  if (Destination == NULL || Bytes == 0u)
    return 0;
  if (!AdmissionDynamicOpenReference(build, ReferenceIndex, &opened,
                                     &allocation, &reference))
    return 0;
  if (opened == NULL || allocation == NULL || reference == NULL)
    return 0;
  if (reference->Role != Role || AllocationToken == 0ULL ||
      AllocationToken != (ULONGLONG)(ULONG_PTR)opened ||
      Offset != reference->Offset || Bytes != reference->Bytes ||
      allocation->PhysicalAddress.QuadPart <= 0 ||
      !AdmissionAllocationAlign64K(opened->Allocation->Description.Size,
                                   &alignedSize) ||
      !NT_SUCCESS(AdmissionMemoryRuntimeReadResident(
          build->Adapter, allocation->SegmentId,
          (ULONGLONG)allocation->PhysicalAddress.QuadPart,
          alignedSize, Offset, Destination, Bytes)))
    return 0;
  return 1;
}

static int AdmissionDynamicResolve(
    void *CallbackContext, APPLE_AGX_U64 AllocationToken,
    APPLE_AGX_U32 ClassId, APPLE_AGX_U32 ReferenceIndex,
    APPLE_AGX_U32 Role, APPLE_AGX_U64 Offset, APPLE_AGX_U32 Bytes,
    APPLE_AGX_U64 *GpuVirtualAddress) {
  ADMISSION_DYNAMIC_BUILD_CONTEXT *build =
      (ADMISSION_DYNAMIC_BUILD_CONTEXT *)CallbackContext;
  const ADMISSION_OPEN_ALLOCATION *opened;
  const DXGK_ALLOCATIONLIST *allocation;
  const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *reference;
  ADMISSION_DYNAMIC_OVERLAY_RESULT overlayResult;
  ADMISSION_LOCAL_MEMORY_VIEW view;
  ULONGLONG alignedSize;
  if (GpuVirtualAddress == NULL)
    return 0;
  *GpuVirtualAddress = 0ULL;
  if (!AdmissionDynamicOpenReference(build, ReferenceIndex, &opened,
                                     &allocation, &reference))
    return 0;
  if (opened == NULL || allocation == NULL || reference == NULL)
    return 0;
  if (AllocationToken != (ULONGLONG)(ULONG_PTR)opened ||
      ClassId != build->Snapshot->Facts[ReferenceIndex].ClassId ||
      Role != reference->Role)
    return 0;
  overlayResult = AdmissionDynamicOverlayResolve(
      build->Plan, ReferenceIndex, Offset, Bytes, GpuVirtualAddress);
  if (overlayResult == AdmissionDynamicOverlaySuccess)
    return 1;
  if (overlayResult != AdmissionDynamicOverlayLayout ||
      allocation->SegmentId != ADMISSION_MEMORY_LOCAL_SEGMENT ||
      allocation->PhysicalAddress.QuadPart <= 0 ||
      !AdmissionAllocationAlign64K(opened->Allocation->Description.Size,
                                   &alignedSize) ||
      !NT_SUCCESS(AdmissionMemoryRuntimeResolveLocal(
          build->Adapter, (ULONGLONG)allocation->PhysicalAddress.QuadPart,
          alignedSize, Offset, &view)) ||
      view.GpuVirtualAddress == 0ULL || Bytes > view.Bytes)
    return 0;
  *GpuVirtualAddress = view.GpuVirtualAddress;
  return 1;
}

_Use_decl_annotations_ NTSTATUS AdmissionDynamicRenderBuild(
    ADMISSION_CONTEXT *Adapter, ADMISSION_RENDER_CONTEXT *Context,
    DXGKARG_RENDER *Args,
    const ADMISSION_WIN32_RENDER_SNAPSHOT *Snapshot,
    ADMISSION_OPEN_ALLOCATION **Opened,
    ADMISSION_LOCAL_MEMORY_VIEW *Destination,
    ADMISSION_GDI_PREPARED *Prepared,
    ULONGLONG *AllocationOffset, ULONGLONG *AllocationBytes) {
  ADMISSION_DYNAMIC_OVERLAY_PLAN plan;
  ADMISSION_DYNAMIC_OVERLAY_BINDINGS bindings;
  ADMISSION_DYNAMIC_BUILD_CONTEXT build;
  const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *destinationReference;
  const DXGK_ALLOCATIONLIST *destinationAllocation;
  const ADMISSION_OPEN_ALLOCATION *destinationOpened;
  ADMISSION_DYNAMIC_DMA_HEADER *dmaHeader;
  APPLE_AGX_DYNAMIC_JOB *job;
  unsigned char *storage;
  ULONG storageCapacity;
  APPLE_AGX_U32 dmaBytes = 0u;
  APPLE_AGX_U32 backgroundColor;
  ULONGLONG alignedSize;
  if (Adapter == NULL || Context == NULL || Args == NULL ||
      Snapshot == NULL || Snapshot->View.Header == NULL ||
      Snapshot->View.Draw == NULL || Opened == NULL || Destination == NULL ||
      Prepared == NULL || AllocationOffset == NULL ||
      AllocationBytes == NULL || Args->pDmaBuffer == NULL ||
      Args->DmaSize > ADMISSION_DYNAMIC_DMA_MAX_BYTES ||
      Args->DmaSize < sizeof(ADMISSION_DYNAMIC_DMA_HEADER) +
                          sizeof(APPLE_AGX_DYNAMIC_JOB))
    return STATUS_INVALID_PARAMETER;
  *Opened = NULL;
  RtlZeroMemory(Destination, sizeof(*Destination));
  RtlZeroMemory(Prepared, sizeof(*Prepared));
  *AllocationOffset = 0ULL;
  *AllocationBytes = 0ULL;
  if (AdmissionDynamicOverlayPlan(
          &Adapter->BackendImage, &Snapshot->View, &plan) !=
          AdmissionDynamicOverlaySuccess ||
      AdmissionDynamicOverlayBindingsFromView(
          &Snapshot->View, &bindings) != AdmissionDynamicOverlaySuccess)
    return STATUS_INVALID_IMAGE_FORMAT;
  RtlZeroMemory(&build, sizeof(build));
  build.Adapter = Adapter;
  build.Context = Context;
  build.Args = Args;
  build.Snapshot = Snapshot;
  build.Plan = &plan;
  if (!AdmissionDynamicOpenReference(
          &build, Snapshot->View.Draw->DestinationReference,
          &destinationOpened, &destinationAllocation,
          &destinationReference))
    return STATUS_INVALID_ADDRESS;
  if (destinationOpened == NULL || destinationAllocation == NULL ||
      destinationReference == NULL)
    return STATUS_INVALID_ADDRESS;
  if (destinationReference->Role != AppleAgxWin32RoleRenderTarget ||
      destinationAllocation->SegmentId != ADMISSION_MEMORY_LOCAL_SEGMENT ||
      destinationAllocation->PhysicalAddress.QuadPart <= 0 ||
      destinationReference->Offset > MAXULONG ||
      destinationReference->Bytes > MAXULONG ||
      !AdmissionAllocationAlign64K(
          destinationOpened->Allocation->Description.Size, &alignedSize) ||
      !NT_SUCCESS(AdmissionMemoryRuntimeResolveLocal(
          Adapter,
          (ULONGLONG)destinationAllocation->PhysicalAddress.QuadPart,
          alignedSize, destinationReference->Offset, Destination)) ||
      Destination->CpuAddress == NULL ||
      destinationReference->Bytes > Destination->Bytes ||
      Snapshot->View.Draw->Format != AppleAgxWin32FormatBgra8Unorm ||
      !AdmissionAllocationContainsView(
          &destinationOpened->Allocation->Description,
          Snapshot->View.Draw->SurfaceWidth,
          Snapshot->View.Draw->SurfaceHeight,
          Snapshot->View.Draw->SurfacePitch,
          destinationReference->Bytes))
    return STATUS_INVALID_ADDRESS;
  Destination->Bytes = destinationReference->Bytes;
  backgroundColor =
      Snapshot->View.Draw->SurfaceWidth == APPLE_AGX_EXP208_GDI_WIDTH &&
              Snapshot->View.Draw->SurfaceHeight ==
                  APPLE_AGX_EXP208_GDI_HEIGHT &&
              Snapshot->View.Draw->SurfacePitch ==
                  APPLE_AGX_EXP208_GDI_PITCH &&
              destinationReference->Bytes ==
                  APPLE_AGX_EXP208_GDI_OUTPUT_BYTES
          ? APPLE_AGX_EXP208_GDI_COLOR
          : ADMISSION_DYNAMIC_BACKGROUND_COLOR;

  RtlZeroMemory(Args->pDmaBuffer, Args->DmaSize);
  dmaHeader = (ADMISSION_DYNAMIC_DMA_HEADER *)Args->pDmaBuffer;
  job = (APPLE_AGX_DYNAMIC_JOB *)((PUCHAR)Args->pDmaBuffer +
                                  sizeof(*dmaHeader));
  storage = (PUCHAR)job + sizeof(*job);
  storageCapacity = Args->DmaSize -
                    (ULONG)(storage - (PUCHAR)Args->pDmaBuffer);
  if (AppleAgxDynamicJobMaterialize(
          &Snapshot->View, Snapshot->Facts,
          Snapshot->View.Header->ReferenceCount,
          ADMISSION_DYNAMIC_SHADER_BASE, AdmissionDynamicRead,
          AdmissionDynamicResolve, &build, storage, storageCapacity,
          job) != AppleAgxDynamicJobSuccess)
    return STATUS_INVALID_IMAGE_FORMAT;
  if (AdmissionDynamicDmaBuild(
          Snapshot->View.Header->Generation,
          Snapshot->View.Header->ContentHash,
          Destination->GpuVirtualAddress,
          destinationReference->AllocationIndex,
          backgroundColor,
          (Snapshot->View.Draw->Flags &
                   APPLE_AGX_WIN32_DRAW_FLAG_EXPECTED_FOREGROUND)
              ? Snapshot->View.Draw->ExpectedForegroundColor
              : 0u,
          &bindings, job, storage,
          job->StorageBytes, Args->pDmaBuffer, Args->DmaSize,
          &dmaBytes) != AdmissionDynamicDmaSuccess)
    return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
  Prepared->Magic = ADMISSION_GDI_PREPARED_MAGIC;
  Prepared->Version = ADMISSION_GDI_PREPARED_VERSION;
  Prepared->DmaOffset = 0u;
  Prepared->DmaBytes = dmaBytes;
  Prepared->PatchCount = 1u;
  Prepared->Patches[0].AllocationIndex =
      destinationReference->AllocationIndex;
  Prepared->Patches[0].SlotId = ADMISSION_GDI_DESTINATION_SLOT;
  Prepared->Patches[0].PatchOffset =
      AdmissionDynamicDmaDestinationPatchOffset();
  Prepared->Patches[0].SplitOffset = 0u;
  *Opened = (ADMISSION_OPEN_ALLOCATION *)destinationOpened;
  *AllocationOffset = destinationReference->Offset;
  *AllocationBytes = destinationReference->Bytes;
  return STATUS_SUCCESS;
}
