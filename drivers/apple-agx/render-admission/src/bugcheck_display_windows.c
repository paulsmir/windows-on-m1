/* A2: DxgkDdiSystemDisplayEnable / DxgkDdiSystemDisplayWrite (DISPLAY_ONLY;
 * mandatory for WDDM 1.2+ full-graphics drivers).
 *
 * Microsoft: both DDIs run during a bugcheck at any IRQL; code and data must be
 * non-pageable and kernel routines might be unavailable. Enable keeps TargetId's
 * display visible in its current mode and returns Width/Height/ColorFormat;
 * NOT_SUPPORTED only when no display is connected. Write copies a 32 bpp
 * (X8R8G8B8) non-paged source to PositionX/PositionY with the CPU.
 *
 * Translation: the CPU-writable surface is the POST (GOP) surface, mapped
 * write-combined once at StartDevice. While AppleAgx owns DCP the panel scans
 * the broker pool, so Enable rings the m1n1 emergency doorbell (scanout
 * REG_EMERGENCY, CAP_EMERGENCY_SURFACE, A1) which re-presents the POST surface
 * in any broker state, and polls its status for a bounded time.
 *
 * Rules for this file: no alloc_text/PAGE code, no locks, no pool, no dxgkrnl
 * callbacks in the bugcheck-time functions (tests/test_apple_agx_bugcheck_display.py).
 * The GPU is not quiesced (FULL GRAPHICS "cancel GPU work" is not covered). */
#include "render_admission.h"
#include "apple_agx_bugcheck_blit.h"

/* Non-paged global (default .data of a non-paged image section). */
ADMISSION_BUGCHECK_RECEIPT g_AgxBugcheckReceipt = {
    ADMISSION_BUGCHECK_RECEIPT_MAGIC, ADMISSION_BUGCHECK_RECEIPT_VERSION};

#define ADMISSION_BUGCHECK_SCANOUT_REG(Reg) \
  (APPLE_AGX_SCANOUT_MMIO_OFFSET + (ULONG)(Reg))

/* The broker page from the CM memory resource (the descriptor the interrupt
 * owner also maps; generated ABI identity, address from the descriptor). */
static BOOLEAN AdmissionBugcheckFindBroker(const ADMISSION_CONTEXT *Context,
                                           PHYSICAL_ADDRESS *Address) {
  PCM_RESOURCE_LIST resources = Context->DeviceInformation.TranslatedResourceList;
  ULONG full_index, partial_index, matches = 0u;
  if (resources == NULL)
    return FALSE;
  for (full_index = 0u; full_index < resources->Count; ++full_index) {
    PCM_FULL_RESOURCE_DESCRIPTOR full = &resources->List[full_index];
    for (partial_index = 0u; partial_index < full->PartialResourceList.Count;
         ++partial_index) {
      PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor =
          &full->PartialResourceList.PartialDescriptors[partial_index];
      if (descriptor->Type == CmResourceTypeMemory &&
          (ULONGLONG)descriptor->u.Memory.Start.QuadPart ==
              J313_AGX_ABI_ADMISSION_POWER_BROKER_BASE &&
          descriptor->u.Memory.Length >= PAGE_SIZE) {
        *Address = descriptor->u.Memory.Start;
        ++matches;
      }
    }
  }
  return matches == 1u ? TRUE : FALSE;
}

_Use_decl_annotations_ VOID AdmissionBugcheckDisplayRetire(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_BUGCHECK_DISPLAY *display;
  if (Context == NULL)
    return;
  display = &Context->BugcheckDisplay;
  /* Unpublish first: a bugcheck from here on reports UNSUCCESSFUL. */
  if (InterlockedExchange(&display->Valid, 0) != 0)
    ++g_AgxBugcheckReceipt.RetireCount;
  if (display->Va != NULL)
    MmUnmapIoSpace(display->Va, display->Bytes);
  if (display->BrokerRegs != NULL)
    MmUnmapIoSpace((PVOID)display->BrokerRegs, PAGE_SIZE);
  display->Va = NULL;
  display->BrokerRegs = NULL;
  display->Bytes = 0u;
  display->EmergencyCap = FALSE;
  display->Capabilities = 0u;
}

_Use_decl_annotations_ VOID AdmissionBugcheckDisplayPrepare(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_BUGCHECK_DISPLAY *display;
  const DXGK_DISPLAY_INFORMATION *post;
  PHYSICAL_ADDRESS base, broker;
  ULONGLONG pa, bytes;
  ULONG format, magic = 0u, capabilities = 0u;
  PVOID surface;
  volatile UCHAR *regs = NULL;

  if (Context == NULL)
    return;
  display = &Context->BugcheckDisplay;
  AdmissionBugcheckDisplayRetire(Context); /* a repeated start re-prepares */
  post = &Context->PostDisplayInformation;
  ++g_AgxBugcheckReceipt.PrepareCount;
  g_AgxBugcheckReceipt.Width = post->Width;
  g_AgxBugcheckReceipt.Height = post->Height;
  g_AgxBugcheckReceipt.Pitch = post->Pitch;
  format = (ULONG)post->ColorFormat;
  g_AgxBugcheckReceipt.Format = format;
  g_AgxBugcheckReceipt.Capabilities = 0u;
  g_AgxBugcheckReceipt.BrokerMapped = 0u;
  g_AgxBugcheckReceipt.EmergencyCap = 0u;
  pa = (ULONGLONG)post->PhysicAddress.QuadPart;
  /* OwnScanout route: no surface until the Mu _DSD descriptor exists. */
  if (post->Width == 0u) {
    g_AgxBugcheckReceipt.PrepareResult = ADMISSION_BUGCHECK_PREPARE_OWN_SCANOUT;
    return;
  }
  if (!AppleAgxPostDisplaySurfaceValid(post->Width, post->Height, post->Pitch,
                                       format, pa, AdmissionPanelLimits())) {
    g_AgxBugcheckReceipt.PrepareResult = ADMISSION_BUGCHECK_PREPARE_INVALID;
    return;
  }
  bytes = (ULONGLONG)post->Pitch * post->Height;
  if (!AdmissionPhysicalRangeOutsideRam(pa, bytes)) {
    g_AgxBugcheckReceipt.PrepareResult = ADMISSION_BUGCHECK_PREPARE_RAM;
    return;
  }
  base.QuadPart = (LONGLONG)pa;
  surface = MmMapIoSpaceEx(base, (SIZE_T)bytes, PAGE_READWRITE | PAGE_WRITECOMBINE);
  if (surface == NULL) {
    g_AgxBugcheckReceipt.PrepareResult = ADMISSION_BUGCHECK_PREPARE_MAP_FAIL;
    return;
  }
  /* The doorbell is optional: without it the writes still land in the POST
   * surface (visible once m1n1 shows it), and Enable still succeeds. */
  broker.QuadPart = 0;
  if (AdmissionBugcheckFindBroker(Context, &broker))
    regs = (volatile UCHAR *)MmMapIoSpaceEx(broker, PAGE_SIZE,
                                            PAGE_READWRITE | PAGE_NOCACHE);
  if (regs != NULL) {
    magic = READ_REGISTER_ULONG((volatile ULONG *)(
        regs + ADMISSION_BUGCHECK_SCANOUT_REG(APPLE_AGX_SCANOUT_REG_MAGIC)));
    if (magic == APPLE_AGX_SCANOUT_MAGIC)
      capabilities = READ_REGISTER_ULONG((volatile ULONG *)(
          regs + ADMISSION_BUGCHECK_SCANOUT_REG(APPLE_AGX_SCANOUT_REG_CAPABILITIES)));
  }
  display->Pa = pa;
  display->Bytes = (SIZE_T)bytes;
  display->Va = (PUCHAR)surface;
  display->Width = post->Width;
  display->Height = post->Height;
  display->Pitch = post->Pitch;
  display->Format = format;
  display->TargetId = 0u;
  display->BrokerRegs = regs;
  display->Capabilities = capabilities;
  display->EmergencyCap =
      (capabilities & APPLE_AGX_SCANOUT_CAP_EMERGENCY_SURFACE) != 0u ? TRUE : FALSE;
  InterlockedExchange(&display->BugcheckActive, 0);
  g_AgxBugcheckReceipt.Capabilities = capabilities;
  g_AgxBugcheckReceipt.BrokerMapped = regs != NULL ? 1u : 0u;
  g_AgxBugcheckReceipt.EmergencyCap = display->EmergencyCap ? 1u : 0u;
  g_AgxBugcheckReceipt.PrepareResult = ADMISSION_BUGCHECK_PREPARE_READY;
  /* Publish last. */
  InterlockedExchange(&display->Valid, 1);
}

/* Black (0 in X8R8G8B8 / A8R8G8B8) with volatile 64-bit stores; Bytes is
 * Pitch * Height with Pitch a multiple of 4, so at most one 32-bit tail. */
static VOID AdmissionBugcheckClear(PUCHAR Va, SIZE_T Bytes) {
  volatile ULONG64 *q = (volatile ULONG64 *)Va;
  SIZE_T i, count = Bytes / sizeof(ULONG64);
  for (i = 0u; i < count; ++i)
    q[i] = 0u;
  if ((Bytes & (sizeof(ULONG64) - 1u)) != 0u)
    *(volatile ULONG *)(Va + count * sizeof(ULONG64)) = 0u;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiSystemDisplayEnable(
    PVOID MiniportDeviceContext, D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags, UINT *Width, UINT *Height,
    D3DDDIFORMAT *ColorFormat) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  ADMISSION_BUGCHECK_DISPLAY *display;
  NTSTATUS status = STATUS_SUCCESS;
  ULONG emergency = APPLE_AGX_SCANOUT_EMERGENCY_IDLE;
  ULONG polls = 0u;

  UNREFERENCED_PARAMETER(Flags); /* reserved (dispmprt.h) */
  InterlockedIncrement(&g_AgxBugcheckReceipt.EnableCalls);
  g_AgxBugcheckReceipt.EnableTargetIn = (ULONG)TargetId;
  if (context == NULL || Width == NULL || Height == NULL || ColorFormat == NULL) {
    g_AgxBugcheckReceipt.EnableStatus = (ULONG)STATUS_INVALID_PARAMETER;
    return STATUS_INVALID_PARAMETER;
  }
  display = &context->BugcheckDisplay;
  /* 1. No prepared surface (own-scanout route, RAM, map failure, stopped). */
  if (InterlockedCompareExchange(&display->Valid, 0, 0) == 0 || display->Va == NULL) {
    g_AgxBugcheckReceipt.EnableStatus = (ULONG)STATUS_UNSUCCESSFUL;
    return STATUS_UNSUCCESSFUL;
  }
  /* 2. Any TargetId (0, D3DDDI_ID_UNINITIALIZED, ...) is the internal panel,
   * which is always connected: NOT_SUPPORTED is never returned. */
  /* 3. Windows is in a bugcheck; the AGX/ASC is not touched. */
  InterlockedExchange(&display->BugcheckActive, 1);
  /* 4. Black before the panel shows the surface. */
  AdmissionBugcheckClear(display->Va, display->Bytes);
  __dsb(_ARM64_BARRIER_SY);
  /* 5. Emergency hand-back: only when CAPABILITIES advertised the register
   * (else 0x1c aborts, a nested crash). Bounded: 5000 x 100 us. */
  if (display->EmergencyCap && display->BrokerRegs != NULL) {
    volatile ULONG *reg = (volatile ULONG *)(
        display->BrokerRegs +
        ADMISSION_BUGCHECK_SCANOUT_REG(APPLE_AGX_SCANOUT_REG_EMERGENCY));
    WRITE_REGISTER_ULONG(reg, APPLE_AGX_SCANOUT_EMERGENCY_MAGIC);
    for (;;) {
      emergency = READ_REGISTER_ULONG(reg);
      if (emergency == APPLE_AGX_SCANOUT_EMERGENCY_LATCHED_GUEST ||
          emergency == APPLE_AGX_SCANOUT_EMERGENCY_LATCHED_OWN ||
          (emergency & APPLE_AGX_SCANOUT_EMERGENCY_FAILED) != 0u ||
          polls >= ADMISSION_BUGCHECK_MAX_POLLS)
        break;
      KeStallExecutionProcessor(ADMISSION_BUGCHECK_POLL_US);
      ++polls;
    }
    /* Still REQUESTED at the bound: success, the swap latches late and the
     * writes are already in the surface. A failure status is UNSUCCESSFUL. */
    if ((emergency & APPLE_AGX_SCANOUT_EMERGENCY_FAILED) != 0u)
      status = STATUS_UNSUCCESSFUL;
  }
  g_AgxBugcheckReceipt.Polls = polls;
  g_AgxBugcheckReceipt.StallUs = polls * ADMISSION_BUGCHECK_POLL_US;
  g_AgxBugcheckReceipt.EmergencyStatus = emergency;
  /* 6. The current (POST) mode. */
  *Width = display->Width;
  *Height = display->Height;
  *ColorFormat = (D3DDDIFORMAT)display->Format;
  g_AgxBugcheckReceipt.EnableStatus = (ULONG)status;
  return status;
}

_Use_decl_annotations_ VOID AdmissionDdiSystemDisplayWrite(
    PVOID MiniportDeviceContext, PVOID Source, UINT SourceWidth,
    UINT SourceHeight, UINT SourceStride, UINT PositionX, UINT PositionY) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  ADMISSION_BUGCHECK_DISPLAY *display;
  APPLE_AGX_BUGCHECK_BLIT_RESULT result;

  InterlockedIncrement(&g_AgxBugcheckReceipt.WriteCalls);
  g_AgxBugcheckReceipt.LastX = PositionX;
  g_AgxBugcheckReceipt.LastY = PositionY;
  g_AgxBugcheckReceipt.LastW = SourceWidth;
  g_AgxBugcheckReceipt.LastH = SourceHeight;
  if (context == NULL) {
    InterlockedIncrement(&g_AgxBugcheckReceipt.WriteRejected);
    return;
  }
  display = &context->BugcheckDisplay;
  if (InterlockedCompareExchange(&display->Valid, 0, 0) == 0 || display->Va == NULL) {
    InterlockedIncrement(&g_AgxBugcheckReceipt.WriteRejected);
    return;
  }
  result = AppleAgxBugcheckBlitRect(display->Va, display->Pitch, display->Width,
                                    display->Height, Source, SourceStride,
                                    SourceWidth, SourceHeight, PositionX, PositionY);
  if (result == AppleAgxBugcheckBlitClipped)
    InterlockedIncrement(&g_AgxBugcheckReceipt.WriteClipped);
  else if (result == AppleAgxBugcheckBlitRejected)
    InterlockedIncrement(&g_AgxBugcheckReceipt.WriteRejected);
  __dsb(_ARM64_BARRIER_SY);
}
