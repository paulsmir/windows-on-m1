#include "render_backend_image.h"

#define ADMISSION_BACKEND_IMAGE_NULL ((void *)0)
#define ADMISSION_BACKEND_PHYSICAL_LIMIT (1ULL << 40u)

static void AdmissionBackendImageZero(
    ADMISSION_BACKEND_IMAGE *Image) {
  unsigned char *bytes = (unsigned char *)Image;
  APPLE_AGX_U32 index;
  for (index = 0u;
       index < (APPLE_AGX_U32)sizeof(*Image); ++index)
    bytes[index] = 0u;
}

static void AdmissionBackendBindingZero(
    APPLE_AGX_EXP208_GDI_BINDING *Binding) {
  unsigned char *bytes = (unsigned char *)Binding;
  APPLE_AGX_U32 index;
  for (index = 0u;
       index < (APPLE_AGX_U32)sizeof(*Binding); ++index)
    bytes[index] = 0u;
}

APPLE_AGX_BOOL AdmissionBackendImagePrepare(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_LOCAL_MEMORY_VIEW *BackendView) {
  ADMISSION_BACKEND_IMAGE candidate;
  APPLE_AGX_U32 template_bytes;

  if (Image == ADMISSION_BACKEND_IMAGE_NULL ||
      BackendView == ADMISSION_BACKEND_IMAGE_NULL ||
      BackendView->CpuAddress == ADMISSION_BACKEND_IMAGE_NULL ||
      BackendView->HostPhysicalAddress == 0ULL ||
      BackendView->GpuVirtualAddress == 0ULL ||
      BackendView->Bytes == 0ULL || BackendView->Bytes > 0xffffffffULL ||
      (BackendView->HostPhysicalAddress & 0x3fffULL) != 0ULL ||
      (BackendView->GpuVirtualAddress &
       (APPLE_AGX_RENDER_TEMPLATE_ALIGNMENT - 1u)) != 0ULL)
    return APPLE_AGX_FALSE;
  template_bytes = AppleAgxRenderTemplateBytes();
  if (template_bytes == 0u || template_bytes > BackendView->Bytes ||
      BackendView->HostPhysicalAddress >= ADMISSION_BACKEND_PHYSICAL_LIMIT ||
      template_bytes > ADMISSION_BACKEND_PHYSICAL_LIMIT -
                           BackendView->HostPhysicalAddress)
    return APPLE_AGX_FALSE;

  AdmissionBackendImageZero(&candidate);
  if (!AppleAgxRenderTemplateMaterialize(
          BackendView->CpuAddress, (APPLE_AGX_U32)BackendView->Bytes,
          &candidate.Roots) ||
      !AppleAgxRenderTemplateBuildRelocationObjectsRebased(
          BackendView->CpuAddress, (APPLE_AGX_U32)BackendView->Bytes,
          BackendView->HostPhysicalAddress,
          BackendView->GpuVirtualAddress,
          BackendView->GpuVirtualAddress, BackendView->Bytes,
          candidate.Objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          &candidate.Roots) ||
      !AppleAgxApplyRelocations(
          candidate.Objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          AppleAgxRenderTemplateRelocations(),
          AppleAgxRenderTemplateRelocationCount()))
    return APPLE_AGX_FALSE;

  candidate.ArenaCpuAddress = BackendView->CpuAddress;
  candidate.ArenaPhysicalAddress = BackendView->HostPhysicalAddress;
  candidate.ArenaGpuAddress = BackendView->GpuVirtualAddress;
  candidate.ArenaBytes = template_bytes;
  candidate.Ready = APPLE_AGX_TRUE;
  *Image = candidate;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AdmissionBackendImageBindSubmission(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *Packet,
    void *DestinationCpuAddress,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_U32 SubmissionByteCount,
    APPLE_AGX_EXP208_GDI_BINDING *Binding) {
  APPLE_AGX_EXP208_RELOCATION_OBJECT saved_output;
  APPLE_AGX_EXP208_GDI_BINDING candidate;
  APPLE_AGX_BOOL bound = APPLE_AGX_FALSE;

  if (Image == ADMISSION_BACKEND_IMAGE_NULL ||
      Packet == ADMISSION_BACKEND_IMAGE_NULL ||
      DestinationCpuAddress == ADMISSION_BACKEND_IMAGE_NULL ||
      SubmissionBytes == ADMISSION_BACKEND_IMAGE_NULL ||
      Binding == ADMISSION_BACKEND_IMAGE_NULL ||
      Image->Ready != APPLE_AGX_TRUE || Image->BoundFence != 0u ||
      Packet->Fence == 0u || Packet->DestinationCpuToken == 0ULL ||
      Packet->DestinationGpuVa == 0ULL ||
      Packet->DestinationPhysical == 0ULL ||
      Packet->DestinationBytes == 0u)
    return APPLE_AGX_FALSE;

  saved_output =
      Image->Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT];
  if (!AppleAgxExp208BindGdiColorFill(
          SubmissionBytes, SubmissionByteCount,
          DestinationCpuAddress, Packet->DestinationGpuVa,
          Packet->DestinationPhysical, Packet->DestinationBytes,
          Image->Objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          AppleAgxRenderTemplateRelocations(),
          AppleAgxRenderTemplateRelocationCount(), &candidate))
    return APPLE_AGX_FALSE;
  bound = APPLE_AGX_TRUE;
  if (!AppleAgxApplyRelocations(
          Image->Objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          AppleAgxRenderTemplateRelocations(),
          AppleAgxRenderTemplateRelocationCount())) {
    if (bound)
      (void)AppleAgxExp208UnbindGdiColorFill(
          Image->Objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          &candidate);
    Image->Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT] = saved_output;
    if (!AppleAgxApplyRelocations(
            Image->Objects,
            APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
            AppleAgxRenderTemplateRelocations(),
            AppleAgxRenderTemplateRelocationCount()))
      Image->Ready = APPLE_AGX_FALSE;
    return APPLE_AGX_FALSE;
  }
  Image->Binding = candidate;
  Image->BoundFence = Packet->Fence;
  *Binding = candidate;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AdmissionBackendImageReleaseSubmission(
    ADMISSION_BACKEND_IMAGE *Image, APPLE_AGX_U32 Fence) {
  unsigned char *job_bytes;
  unsigned char *dynamic_bytes;
  APPLE_AGX_U32 index;

  if (Image == ADMISSION_BACKEND_IMAGE_NULL ||
      Image->Ready != APPLE_AGX_TRUE || Fence == 0u ||
      Image->BoundFence != Fence)
    return APPLE_AGX_FALSE;
  if (!AppleAgxExp208UnbindGdiColorFill(
          Image->Objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          &Image->Binding) ||
      !AppleAgxApplyRelocations(
          Image->Objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          AppleAgxRenderTemplateRelocations(),
          AppleAgxRenderTemplateRelocationCount()))
    return APPLE_AGX_FALSE;
  Image->BoundFence = 0u;
  Image->JobFence = 0u;
  Image->JobReady = APPLE_AGX_FALSE;
  AdmissionBackendBindingZero(&Image->Binding);
  job_bytes = (unsigned char *)&Image->Job;
  for (index = 0u; index < (APPLE_AGX_U32)sizeof(Image->Job); ++index)
    job_bytes[index] = 0u;
  dynamic_bytes = (unsigned char *)&Image->Dynamic;
  for (index = 0u; index < (APPLE_AGX_U32)sizeof(Image->Dynamic); ++index)
    dynamic_bytes[index] = 0u;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AdmissionBackendImageStageJob(
    ADMISSION_BACKEND_IMAGE *Image, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 TaEvent, APPLE_AGX_U32 D3Event,
    APPLE_AGX_U32 TaExpectedDonePointer,
    APPLE_AGX_U32 D3ExpectedDonePointer,
    APPLE_AGX_BOOL IncludeInitBm,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  APPLE_AGX_EXP208_DYNAMIC_INPUT dynamic_input;
  APPLE_AGX_EXP208_DYNAMIC_RESULT dynamic;
  APPLE_AGX_EXP208_JOB_PARAMETERS parameters;
  APPLE_AGX_BACKEND_JOB_IMAGE candidate;

  if (Image == ADMISSION_BACKEND_IMAGE_NULL ||
      Job == ADMISSION_BACKEND_IMAGE_NULL ||
      Image->Ready != APPLE_AGX_TRUE || Image->JobReady ||
      Fence == 0u || Image->BoundFence != Fence ||
      Image->Sequence == 0xffffffffu)
    return APPLE_AGX_FALSE;
  dynamic_input.Sequence = Image->Sequence + 1u;
  dynamic_input.TaEventNumber = TaEvent;
  dynamic_input.D3EventNumber = D3Event;
  dynamic_input.IncludeInitBm = IncludeInitBm;
  if (!AppleAgxExp208DeriveDynamic(&dynamic_input, &dynamic))
    return APPLE_AGX_FALSE;

  parameters.ArenaGpuAddress = Image->ArenaGpuAddress;
  parameters.ArenaBytes = Image->ArenaBytes;
  parameters.TaEvent = TaEvent;
  parameters.D3Event = D3Event;
  parameters.TaExpectedStamp = dynamic.TaCurrentStamp;
  parameters.D3ExpectedStamp = dynamic.D3CurrentStamp;
  parameters.TaExpectedDonePointer = TaExpectedDonePointer;
  parameters.D3ExpectedDonePointer = D3ExpectedDonePointer;
  if (!AppleAgxExp208BuildJob(
          &parameters, Image->Objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          AppleAgxRenderTemplateRelocations(),
          AppleAgxRenderTemplateRelocationCount(), &candidate) ||
      !AppleAgxExp208PatchDynamic(
          Image->ArenaCpuAddress, Image->ArenaBytes, &dynamic_input))
    return APPLE_AGX_FALSE;

  Image->Dynamic = dynamic;
  Image->Job = candidate;
  Image->Sequence = dynamic_input.Sequence;
  Image->JobFence = Fence;
  Image->JobReady = APPLE_AGX_TRUE;
  *Job = candidate;
  return APPLE_AGX_TRUE;
}

void AdmissionBackendImageReset(ADMISSION_BACKEND_IMAGE *Image) {
  if (Image != ADMISSION_BACKEND_IMAGE_NULL)
    AdmissionBackendImageZero(Image);
}

#undef ADMISSION_BACKEND_IMAGE_NULL
