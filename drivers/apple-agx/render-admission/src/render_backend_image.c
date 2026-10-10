#include "render_backend_image.h"
#include "render_dynamic_overlay.h"
#include "apple_agx_render_template_vm_slot.h"
#include <string.h>

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
  candidate.Pristine = APPLE_AGX_TRUE;
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
  Image->Pristine = APPLE_AGX_FALSE;

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
  Image->Pristine = APPLE_AGX_FALSE;
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

APPLE_AGX_BOOL AdmissionBackendImageBindNativeSubmission(
    ADMISSION_BACKEND_IMAGE *Image, const ADMISSION_RENDER_PACKET_DESCRIPTION *Packet,
    void *DestinationCpuAddress, const ADMISSION_DYNAMIC_OVERLAY_BINDINGS *Native,
    APPLE_AGX_EXP208_GDI_BINDING *Binding) {
  APPLE_AGX_EXP208_RELOCATION_OBJECT saved;
  APPLE_AGX_EXP208_RELOCATION_OBJECT *output;
  APPLE_AGX_EXP208_GDI_BINDING candidate;
  if(!AdmissionDynamicOverlaySurfaceValid(Native)) return APPLE_AGX_FALSE;
  if(!Image || !Packet || !DestinationCpuAddress || !Native || !Binding ||
      Image->Ready!=APPLE_AGX_TRUE || Image->BoundFence || Image->NativeBound ||
      !Packet->Fence || !Packet->DestinationGpuVa || !Packet->DestinationPhysical ||
      (!APPLE_AGX_WIN32_COMMAND_IS_NATIVE(Native->CommandVersion)) ||
      Native->DestinationBytes!=Packet->DestinationBytes ||
      Native->DestinationBytes>0xffffffffULL ||
      Packet->DestinationGpuVa>=(1ULL<<40) ||
      Packet->DestinationPhysical>=(1ULL<<40) ||
      Native->DestinationBytes>(1ULL<<40)-Packet->DestinationGpuVa ||
      Native->DestinationBytes>(1ULL<<40)-Packet->DestinationPhysical)
    return APPLE_AGX_FALSE;
  Image->Pristine=APPLE_AGX_FALSE;
  output=&Image->Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT];
  saved=*output;
  output->Data=DestinationCpuAddress;
  output->GpuVa=Packet->DestinationGpuVa;
  output->PhysicalAddress=Packet->DestinationPhysical;
  output->Size=Packet->DestinationBytes;
  if(!AppleAgxApplyRelocations(Image->Objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
      AppleAgxRenderTemplateRelocations(),AppleAgxRenderTemplateRelocationCount())) {
    *output=saved;
    if(!AppleAgxApplyRelocations(Image->Objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
        AppleAgxRenderTemplateRelocations(),AppleAgxRenderTemplateRelocationCount())) Image->Ready=APPLE_AGX_FALSE;
    return APPLE_AGX_FALSE;
  }
  AdmissionBackendBindingZero(&candidate);
  candidate.OutputObject=APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT;
  candidate.DestinationGpuVa=Packet->DestinationGpuVa;
  candidate.DestinationPhysical=Packet->DestinationPhysical;
  candidate.DestinationBytes=Packet->DestinationBytes;
  Image->NativeOriginalOutput=saved; Image->NativeBound=APPLE_AGX_TRUE;
  Image->NativeWidth=Native->SurfaceWidth;
  Image->NativeHeight=Native->SurfaceHeight;
  Image->NativePitch=Native->SurfacePitch;
  Image->Binding=candidate; Image->BoundFence=Packet->Fence;
  *Binding=candidate;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AdmissionBackendImageBindG4Submission(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *Packet,
    void *DestinationCpuAddress,
    const APPLE_AGX_G4_SUBMIT_VIEW *View,
    APPLE_AGX_EXP208_GDI_BINDING *Binding) {
  ADMISSION_LOCAL_MEMORY_VIEW backend;
  APPLE_AGX_G4_NATIVE_RENDER render;
  APPLE_AGX_G4_ATTACHMENT color;
  APPLE_AGX_EXP208_GDI_BINDING candidate;
  APPLE_AGX_EXP208_RELOCATION_OBJECT saved;
  APPLE_AGX_U32 sequence;
  if (Image == 0 || Packet == 0 || DestinationCpuAddress == 0 ||
      View == 0 || View->Native == 0 || View->Render == 0 ||
      View->Attachments == 0 ||
      Binding == 0 || Image->Ready != APPLE_AGX_TRUE ||
      Image->BoundFence != 0u || Image->G4Native ||
      View->RenderBytes != sizeof(render) || View->AttachmentCount == 0u ||
      View->AttachmentCount > APPLE_AGX_G4_MAX_ATTACHMENTS ||
      View->CommandBytes == 0u ||
      View->CommandBytes > APPLE_AGX_G4_NATIVE_MAX_BYTES ||
      Packet->Fence == 0u || Packet->DestinationPhysical == 0ULL ||
      Packet->DestinationGpuVa == 0ULL || Packet->DestinationBytes == 0u)
    return APPLE_AGX_FALSE;
  memcpy(&render, View->Render, sizeof(render));
  memcpy(&color, View->Attachments, sizeof(color));
  if (color.Pointer != Packet->DestinationGpuVa ||
      color.Size != Packet->DestinationBytes ||
      Packet->DestinationCpuToken !=
          (APPLE_AGX_U64)(unsigned long long)DestinationCpuAddress)
    return APPLE_AGX_FALSE;
  backend.CpuAddress = Image->ArenaCpuAddress;
  backend.HostPhysicalAddress = Image->ArenaPhysicalAddress;
  backend.GpuVirtualAddress = Image->ArenaGpuAddress;
  backend.Bytes = Image->ArenaCapacity;
  sequence = Image->Sequence;
  /* A release already restored the exact Prepare image. */
  if (Image->Pristine != APPLE_AGX_TRUE &&
      !AdmissionBackendImagePrepare(Image, &backend)) {
    Image->Ready = APPLE_AGX_FALSE;
    return APPLE_AGX_FALSE;
  }
  Image->Pristine = APPLE_AGX_FALSE;
  if (!AppleAgxG4BuildTa3d(View, Image->ArenaCpuAddress,
          Image->ArenaBytes, 1u, Image->Objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT)) {
    Image->Ready = APPLE_AGX_FALSE;
    return APPLE_AGX_FALSE;
  }
  /* Template reconstruction is not a firmware queue restart. */
  Image->Sequence = sequence;
  saved = Image->Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT];
  Image->Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Data =
      (unsigned char *)DestinationCpuAddress;
  Image->Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].GpuVa =
      Packet->DestinationGpuVa;
  Image->Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].PhysicalAddress =
      Packet->DestinationPhysical;
  Image->Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Size =
      Packet->DestinationBytes;
  if (!AppleAgxG4ApplySceneRelocations(Image->Objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT)) {
    Image->Ready = APPLE_AGX_FALSE;
    return APPLE_AGX_FALSE;
  }
  if (!AppleAgxG4ComposeHeaderV2(&Image->G4Header, &render,
          View->CommandVa, View->CommandBytes,
          View->ColorFormat, View->Process)) {
    Image->Ready = APPLE_AGX_FALSE;
    return APPLE_AGX_FALSE;
  }
  memcpy(Image->G4Command, View->Native, View->CommandBytes);
  Image->G4CommandBytes = View->CommandBytes;
  Image->G4Lease = View->Lease;
  AdmissionBackendBindingZero(&candidate);
  candidate.OutputObject = APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT;
  candidate.DestinationGpuVa = Packet->DestinationGpuVa;
  candidate.DestinationPhysical = Packet->DestinationPhysical;
  candidate.DestinationBytes = Packet->DestinationBytes;
  Image->NativeOriginalOutput = saved;
  Image->NativeBound = APPLE_AGX_TRUE;
  Image->G4Native = APPLE_AGX_TRUE;
  Image->NativeWidth = render.WidthPx;
  Image->NativeHeight = render.HeightPx;
  Image->NativePitch = (APPLE_AGX_U32)render.WidthPx *
      AppleAgxG4ColorBytes(View->ColorFormat);
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
      (Image->NativeBound ? (Packet->DestinationBytes > Allocation->Size ||
                            Allocation->Size > 0xffffffffULL) :
                           Packet->DestinationBytes != Allocation->Size) ||
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
  if (Image->NativeBound) {
    if(framebuffer || gpu_offset ||
        !Image->NativeWidth || !Image->NativeHeight || !Image->NativePitch)
      return APPLE_AGX_FALSE;
  } else if (framebuffer == APPLE_AGX_TRUE) {
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
  candidate.AllocationBytes = Image->NativeBound ? (APPLE_AGX_U32)Allocation->Size : Packet->DestinationBytes;
  candidate.RenderedCpuAddress = object->Data;
  candidate.RenderedGpuAddress = object->GpuVa;
  candidate.RenderedPhysicalAddress = object->PhysicalAddress;
  candidate.RenderedOffset = (APPLE_AGX_U32)gpu_offset;
  candidate.RenderedBytes = (Image->NativeBound || framebuffer == APPLE_AGX_TRUE)
      ? object->Size
      : APPLE_AGX_EXP208_GDI_WIDTH * APPLE_AGX_EXP208_GDI_HEIGHT * 4u;
  candidate.AllocationWidth = Allocation->Width;
  candidate.AllocationHeight = Allocation->Height;
  candidate.AllocationPitch = Allocation->Pitch;
  candidate.AllocationFormat = Allocation->Format;
  candidate.RenderWidth = Image->NativeBound ? Image->NativeWidth : framebuffer == APPLE_AGX_TRUE
      ? APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH : APPLE_AGX_EXP208_GDI_WIDTH;
  candidate.RenderHeight = Image->NativeBound ? Image->NativeHeight : framebuffer == APPLE_AGX_TRUE
      ? Image->Binding.Framebuffer.RenderHeight : APPLE_AGX_EXP208_GDI_HEIGHT;
  candidate.RenderPitch = Image->NativeBound ? Image->NativePitch : framebuffer == APPLE_AGX_TRUE
      ? APPLE_AGX_EXP208_FRAMEBUFFER_PITCH : APPLE_AGX_EXP208_GDI_PITCH;
  candidate.ExpectedColor = Image->NativeBound ? 0u : framebuffer == APPLE_AGX_TRUE
      ? Image->Binding.Framebuffer.ClearColor
      : APPLE_AGX_EXP208_GDI_COLOR;
  candidate.BackgroundColor = candidate.ExpectedColor;
  candidate.VerificationKind = Image->NativeBound ? AdmissionBackendOutputVerificationNativeCapture :
      AdmissionBackendOutputVerificationUniform;
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
  if (Image->G4Native) {
    ADMISSION_LOCAL_MEMORY_VIEW backend;
    APPLE_AGX_U32 sequence = Image->Sequence;
    backend.CpuAddress = Image->ArenaCpuAddress;
    backend.HostPhysicalAddress = Image->ArenaPhysicalAddress;
    backend.GpuVirtualAddress = Image->ArenaGpuAddress;
    backend.Bytes = Image->ArenaCapacity;
    if (!AdmissionBackendImagePrepare(Image, &backend))
      return APPLE_AGX_FALSE;
    /* Event slots and queues outlive this scene; preserve their stamp epoch. */
    Image->Sequence = sequence;
    return APPLE_AGX_TRUE;
  }
  if (Image->NativeBound) {
    APPLE_AGX_EXP208_RELOCATION_OBJECT *output=&Image->Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT];
    if(output->GpuVa!=Image->Binding.DestinationGpuVa ||
        output->PhysicalAddress!=Image->Binding.DestinationPhysical ||
        output->Size!=Image->Binding.DestinationBytes) return APPLE_AGX_FALSE;
    *output=Image->NativeOriginalOutput;
    Image->NativeBound=APPLE_AGX_FALSE;
    Image->NativeWidth=Image->NativeHeight=Image->NativePitch=0;
  } else if (!AppleAgxExp208UnbindGdiColorFill(
          Image->Objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          &Image->Binding)) return APPLE_AGX_FALSE;
  if (!AppleAgxApplyRelocations(
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

APPLE_AGX_BOOL AdmissionBackendImageRestoredObject(APPLE_AGX_U32 Index) {
  return Index <= 42u || Index == 63u ? APPLE_AGX_TRUE : APPLE_AGX_FALSE;
}

static APPLE_AGX_BOOL snapshot_matches(
    const ADMISSION_BACKEND_IMAGE_SNAPSHOT *Snapshot,
    const ADMISSION_BACKEND_IMAGE *Image) {
  return Snapshot != ADMISSION_BACKEND_IMAGE_NULL &&
      Snapshot->Valid == APPLE_AGX_TRUE &&
      Snapshot->StoredBytes <= ADMISSION_BACKEND_SNAPSHOT_BYTES &&
      Snapshot->Image.Ready == APPLE_AGX_TRUE &&
      Snapshot->Image.ArenaCpuAddress == Image->ArenaCpuAddress &&
      Snapshot->Image.ArenaPhysicalAddress == Image->ArenaPhysicalAddress &&
      Snapshot->Image.ArenaGpuAddress == Image->ArenaGpuAddress &&
      Snapshot->Image.ArenaBytes == Image->ArenaBytes &&
      Snapshot->Image.ArenaCapacity == Image->ArenaCapacity;
}

APPLE_AGX_BOOL AdmissionBackendImageCaptureSnapshot(
    const ADMISSION_BACKEND_IMAGE *Image,
    ADMISSION_BACKEND_IMAGE_SNAPSHOT *Snapshot) {
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;
  const unsigned char *arena;
  APPLE_AGX_U32 index, stored = 0u;
  if (Snapshot == ADMISSION_BACKEND_IMAGE_NULL)
    return APPLE_AGX_FALSE;
  Snapshot->Valid = APPLE_AGX_FALSE;
  layouts = AppleAgxRenderTemplateObjectLayouts();
  if (Image == ADMISSION_BACKEND_IMAGE_NULL || layouts == 0 ||
      Image->Ready != APPLE_AGX_TRUE || Image->Pristine != APPLE_AGX_TRUE ||
      Image->BoundFence != 0u || Image->ArenaCpuAddress == 0 ||
      Image->ArenaBytes != AppleAgxRenderTemplateBytes())
    return APPLE_AGX_FALSE;
  arena = (const unsigned char *)Image->ArenaCpuAddress;
  for (index = 0u; index < APPLE_AGX_RENDER_TEMPLATE_OBJECT_COUNT; ++index) {
    APPLE_AGX_U32 length;
    Snapshot->Stored[index] = 0u;
    if (!AdmissionBackendImageRestoredObject(index))
      continue;
    if (layouts[index].ArenaOffset > Image->ArenaBytes ||
        layouts[index].Size > Image->ArenaBytes - layouts[index].ArenaOffset)
      return APPLE_AGX_FALSE;
    /* One uncached pass at StartDevice: whole words while aligned. */
    length = layouts[index].Size;
    while (length != 0u) {
      const unsigned char *end = arena + layouts[index].ArenaOffset + length;
      if (((APPLE_AGX_U64)(unsigned long long)end & 7ULL) == 0ULL &&
          length >= 8u) {
        if (*(const volatile APPLE_AGX_U64 *)(end - 8) != 0ULL) break;
        length -= 8u;
      } else {
        if (end[-1] != 0u) break;
        --length;
      }
    }
    while (length != 0u &&
           arena[layouts[index].ArenaOffset + length - 1u] == 0u)
      --length;
    if (length > ADMISSION_BACKEND_SNAPSHOT_BYTES - stored)
      return APPLE_AGX_FALSE;
    memcpy(Snapshot->Data + stored, arena + layouts[index].ArenaOffset, length);
    Snapshot->Stored[index] = length;
    stored += length;
  }
  Snapshot->StoredBytes = stored;
  Snapshot->Image = *Image;
  Snapshot->Valid = APPLE_AGX_TRUE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AdmissionBackendImageReleaseSubmissionRestore(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_BACKEND_IMAGE_SNAPSHOT *Snapshot, APPLE_AGX_U32 Fence) {
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;
  unsigned char *arena;
  APPLE_AGX_U32 index, offset = 0u, sequence;
  if (Image == ADMISSION_BACKEND_IMAGE_NULL ||
      Image->Ready != APPLE_AGX_TRUE || Fence == 0u ||
      Image->BoundFence != Fence || !Image->G4Native ||
      !snapshot_matches(Snapshot, Image))
    return AdmissionBackendImageReleaseSubmission(Image, Fence);
  layouts = AppleAgxRenderTemplateObjectLayouts();
  arena = (unsigned char *)Image->ArenaCpuAddress;
  for (index = 0u; index < APPLE_AGX_RENDER_TEMPLATE_OBJECT_COUNT; ++index) {
    APPLE_AGX_U32 stored = Snapshot->Stored[index];
    unsigned char *object = arena + layouts[index].ArenaOffset;
    if (!AdmissionBackendImageRestoredObject(index))
      continue;
    memcpy(object, Snapshot->Data + offset, stored);
    memset(object + stored, 0, layouts[index].Size - stored);
    offset += stored;
  }
  /* Event slots and queues outlive this scene; preserve their stamp epoch. */
  sequence = Image->Sequence;
  *Image = Snapshot->Image;
  Image->Sequence = sequence;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AdmissionBackendImageSelectVmSlot(
    ADMISSION_BACKEND_IMAGE *Image, APPLE_AGX_U32 Slot) {
  if (Image == ADMISSION_BACKEND_IMAGE_NULL ||
      Image->Ready != APPLE_AGX_TRUE || Image->BoundFence != 0u)
    return APPLE_AGX_FALSE;
  Image->Pristine = APPLE_AGX_FALSE;
  return AppleAgxRenderTemplateSelectVmSlot(
      Image->ArenaCpuAddress, Image->ArenaCapacity, Slot);
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
  if (!(Image->G4Native ?
          AppleAgxG4StageJob(&parameters, Image->Objects,
              APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &candidate) :
          AppleAgxExp208BuildJob(&parameters, Image->Objects,
              APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
              AppleAgxRenderTemplateRelocations(),
              AppleAgxRenderTemplateRelocationCount(), &candidate)) ||
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
