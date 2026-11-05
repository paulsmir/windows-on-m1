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

static void AdmissionBackendOutputZero(
    ADMISSION_BACKEND_OUTPUT_VIEW *Output) {
  unsigned char *bytes = (unsigned char *)Output;
  APPLE_AGX_U32 index;
  for (index = 0u;
       index < (APPLE_AGX_U32)sizeof(*Output); ++index)
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
  candidate.ArenaCapacity = (APPLE_AGX_U32)BackendView->Bytes;
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
  if (Packet->DestinationBytes ==
      APPLE_AGX_EXP208_FRAMEBUFFER_BYTES) {
    if (!AppleAgxExp208BindGdiFramebufferColorFill(
            SubmissionBytes, SubmissionByteCount,
            Image->ArenaCpuAddress, Image->ArenaGpuAddress,
            Image->ArenaPhysicalAddress, Image->ArenaCapacity,
            DestinationCpuAddress, Packet->DestinationGpuVa,
            Packet->DestinationPhysical, Packet->DestinationBytes,
            Image->Objects,
            APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
            AppleAgxRenderTemplateRelocations(),
            AppleAgxRenderTemplateRelocationCount(), &candidate))
      return APPLE_AGX_FALSE;
  } else if (!AppleAgxExp208BindGdiColorFill(
                 SubmissionBytes, SubmissionByteCount,
                 DestinationCpuAddress, Packet->DestinationGpuVa,
                 Packet->DestinationPhysical, Packet->DestinationBytes,
                 Image->Objects,
                 APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
                 AppleAgxRenderTemplateRelocations(),
                 AppleAgxRenderTemplateRelocationCount(), &candidate)) {
    return APPLE_AGX_FALSE;
  }
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

APPLE_AGX_BOOL AdmissionBackendImageBindDynamicSubmission(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *Packet,
    void *DestinationCpuAddress,
    APPLE_AGX_U32 BackgroundColor,
    APPLE_AGX_EXP208_GDI_BINDING *Binding) {
  APPLE_AGX_EXP208_RELOCATION_OBJECT saved_output;
  APPLE_AGX_EXP208_GDI_BINDING candidate;
  if (Image == ADMISSION_BACKEND_IMAGE_NULL ||
      Packet == ADMISSION_BACKEND_IMAGE_NULL ||
      DestinationCpuAddress == ADMISSION_BACKEND_IMAGE_NULL ||
      Binding == ADMISSION_BACKEND_IMAGE_NULL ||
      Image->Ready != APPLE_AGX_TRUE || Image->BoundFence != 0u ||
      Packet->Fence == 0u || Packet->DestinationCpuToken == 0ULL ||
      Packet->DestinationGpuVa == 0ULL ||
      Packet->DestinationPhysical == 0ULL ||
      (Packet->DestinationBytes != APPLE_AGX_EXP208_FRAMEBUFFER_BYTES &&
       Packet->DestinationBytes != APPLE_AGX_EXP208_GDI_OUTPUT_BYTES))
    return APPLE_AGX_FALSE;
  if (!AppleAgxExp208AdoptNativePipelineLayout(
          Image->Objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT))
    return APPLE_AGX_FALSE;
  saved_output = Image->Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT];
  if (Packet->DestinationBytes == APPLE_AGX_EXP208_FRAMEBUFFER_BYTES) {
    if (!AppleAgxExp208BindDynamicFramebuffer(
            BackgroundColor, Image->ArenaCpuAddress, Image->ArenaGpuAddress,
            Image->ArenaPhysicalAddress, Image->ArenaCapacity,
            DestinationCpuAddress, Packet->DestinationGpuVa,
            Packet->DestinationPhysical, Packet->DestinationBytes,
            Image->Objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
            AppleAgxRenderTemplateRelocations(),
            AppleAgxRenderTemplateRelocationCount(), &candidate))
      return APPLE_AGX_FALSE;
  } else {
    APPLE_AGX_GDI_COMMAND_DESCRIPTION description = {0};
    unsigned char dma[sizeof(APPLE_AGX_GDI_DMA_COMMAND)];
    APPLE_AGX_U32 written = 0u;
    if (BackgroundColor != APPLE_AGX_EXP208_GDI_COLOR)
      return APPLE_AGX_FALSE;
    description.Command.Opcode = AppleAgxGdiColorFill;
    description.Command.Destination = (APPLE_AGX_GDI_RECT){
        0u, 0u, APPLE_AGX_EXP208_GDI_WIDTH,
        APPLE_AGX_EXP208_GDI_HEIGHT};
    description.Command.DestinationAllocationIndex = 0u;
    description.Command.DestinationGpuAddress = Packet->DestinationGpuVa;
    description.Command.DestinationPitch = APPLE_AGX_EXP208_GDI_PITCH;
    description.Command.Color = BackgroundColor;
    description.Command.Rop = AppleAgxGdiColorFillPatCopy;
    if (!AppleAgxGdiEncodeDmaCommand(
            &description, dma, sizeof(dma), &written) ||
        written != sizeof(dma) ||
        !AppleAgxExp208BindGdiColorFill(
            dma, written, DestinationCpuAddress, Packet->DestinationGpuVa,
            Packet->DestinationPhysical, Packet->DestinationBytes,
            Image->Objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
            AppleAgxRenderTemplateRelocations(),
            AppleAgxRenderTemplateRelocationCount(), &candidate))
      return APPLE_AGX_FALSE;
  }
  if (!AppleAgxApplyRelocations(
          Image->Objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          AppleAgxRenderTemplateRelocations(),
          AppleAgxRenderTemplateRelocationCount())) {
    (void)AppleAgxExp208UnbindGdiColorFill(
        Image->Objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
        &candidate);
    Image->Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT] = saved_output;
    if (!AppleAgxApplyRelocations(
            Image->Objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
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

APPLE_AGX_BOOL AdmissionBackendImageCaptureOutput(
    const ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *Packet,
    const ADMISSION_ALLOCATION_DESCRIPTION *Allocation,
    ADMISSION_BACKEND_OUTPUT_VIEW *Output) {
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *object;
  ADMISSION_BACKEND_OUTPUT_VIEW candidate;
  APPLE_AGX_BOOL framebuffer;
  APPLE_AGX_U64 gpu_offset;
  APPLE_AGX_U64 physical_offset;
  APPLE_AGX_U64 cpu_offset;
  if (Output != ADMISSION_BACKEND_IMAGE_NULL)
    AdmissionBackendOutputZero(&candidate);
  if (Image == ADMISSION_BACKEND_IMAGE_NULL ||
      Packet == ADMISSION_BACKEND_IMAGE_NULL ||
      Allocation == ADMISSION_BACKEND_IMAGE_NULL ||
      Output == ADMISSION_BACKEND_IMAGE_NULL ||
      Image->Ready != APPLE_AGX_TRUE || Packet->Fence == 0u ||
      Image->BoundFence != Packet->Fence ||
      !AdmissionAllocationDescriptionValid(Allocation) ||
      Packet->DestinationCpuToken == 0ULL ||
      Packet->DestinationGpuVa == 0ULL ||
      Packet->DestinationPhysical == 0ULL ||
      Packet->DestinationBytes != Allocation->Size ||
      Packet->DestinationBytes > 0xffffffffULL)
    return APPLE_AGX_FALSE;
  object = &Image->Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT];
  framebuffer = Image->Binding.Framebuffer.Active;
  if (object->Data == ADMISSION_BACKEND_IMAGE_NULL || object->Size == 0u ||
      object->GpuVa != Image->Binding.DestinationGpuVa ||
      object->PhysicalAddress != Image->Binding.DestinationPhysical ||
      object->Size != Image->Binding.DestinationBytes ||
      object->GpuVa < Packet->DestinationGpuVa ||
      object->PhysicalAddress < Packet->DestinationPhysical ||
      (const unsigned char *)object->Data <
          (const unsigned char *)(unsigned long long)
              Packet->DestinationCpuToken)
    return APPLE_AGX_FALSE;
  gpu_offset = object->GpuVa - Packet->DestinationGpuVa;
  physical_offset = object->PhysicalAddress - Packet->DestinationPhysical;
  cpu_offset = (APPLE_AGX_U64)((const unsigned char *)object->Data -
      (const unsigned char *)(unsigned long long)
          Packet->DestinationCpuToken);
  if (gpu_offset != physical_offset || gpu_offset != cpu_offset ||
      gpu_offset > Packet->DestinationBytes ||
      object->Size > Packet->DestinationBytes - gpu_offset)
    return APPLE_AGX_FALSE;
  if (framebuffer == APPLE_AGX_TRUE) {
    if (Allocation->Width != APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH ||
        Allocation->Height != APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT ||
        Allocation->Pitch != APPLE_AGX_EXP208_FRAMEBUFFER_PITCH ||
        Packet->DestinationBytes != APPLE_AGX_EXP208_FRAMEBUFFER_BYTES ||
        !((Image->Binding.Framebuffer.RenderHeight ==
               APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT &&
           gpu_offset == 0u &&
           object->Size == APPLE_AGX_EXP208_FRAMEBUFFER_BYTES) ||
          (Image->Binding.Framebuffer.RenderHeight ==
               APPLE_AGX_EXP208_FRAMEBUFFER_BAND_HEIGHT &&
           gpu_offset == APPLE_AGX_EXP208_FRAMEBUFFER_BAND_OFFSET &&
           object->Size == APPLE_AGX_EXP208_FRAMEBUFFER_BAND_BYTES)))
      return APPLE_AGX_FALSE;
  } else if (gpu_offset != 0u ||
             object->Size < APPLE_AGX_EXP208_GDI_OUTPUT_BYTES) {
    return APPLE_AGX_FALSE;
  }
  candidate.AllocationCpuAddress =
      (void *)(unsigned long long)Packet->DestinationCpuToken;
  candidate.AllocationGpuAddress = Packet->DestinationGpuVa;
  candidate.AllocationPhysicalAddress = Packet->DestinationPhysical;
  candidate.AllocationBytes = Packet->DestinationBytes;
  candidate.RenderedCpuAddress = object->Data;
  candidate.RenderedGpuAddress = object->GpuVa;
  candidate.RenderedPhysicalAddress = object->PhysicalAddress;
  candidate.RenderedOffset = (APPLE_AGX_U32)gpu_offset;
  candidate.RenderedBytes = framebuffer == APPLE_AGX_TRUE
      ? object->Size
      : APPLE_AGX_EXP208_GDI_WIDTH * APPLE_AGX_EXP208_GDI_HEIGHT * 4u;
  candidate.AllocationWidth = Allocation->Width;
  candidate.AllocationHeight = Allocation->Height;
  candidate.AllocationPitch = Allocation->Pitch;
  candidate.AllocationFormat = Allocation->Format;
  candidate.RenderWidth = framebuffer == APPLE_AGX_TRUE
      ? APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH : APPLE_AGX_EXP208_GDI_WIDTH;
  candidate.RenderHeight = framebuffer == APPLE_AGX_TRUE
      ? Image->Binding.Framebuffer.RenderHeight : APPLE_AGX_EXP208_GDI_HEIGHT;
  candidate.RenderPitch = framebuffer == APPLE_AGX_TRUE
      ? APPLE_AGX_EXP208_FRAMEBUFFER_PITCH : APPLE_AGX_EXP208_GDI_PITCH;
  candidate.ExpectedColor = framebuffer == APPLE_AGX_TRUE
      ? Image->Binding.Framebuffer.ClearColor
      : APPLE_AGX_EXP208_GDI_COLOR;
  candidate.BackgroundColor = candidate.ExpectedColor;
  candidate.VerificationKind = AdmissionBackendOutputVerificationUniform;
  candidate.Framebuffer = framebuffer;
  *Output = candidate;
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

APPLE_AGX_BOOL AdmissionBackendImageRestartQueueLifetime(
    ADMISSION_BACKEND_IMAGE *Image) {
  if (Image == ADMISSION_BACKEND_IMAGE_NULL ||
      Image->Ready != APPLE_AGX_TRUE || Image->BoundFence != 0u ||
      Image->JobFence != 0u || Image->JobReady != APPLE_AGX_FALSE)
    return APPLE_AGX_FALSE;
  Image->Sequence = 0u;
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
