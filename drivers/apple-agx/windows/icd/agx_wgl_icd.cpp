/* CS 1.6 ICD plan, phase 2/4: the WGL target of the AGX OpenGL ICD.
 *
 * Replaces Mesa's targets/wgl/wgl.c (softpipe/llvmpipe) in libgallium_wgl:
 * stw_winsys::create_screen opens the AGX adapter behind the window's DC,
 * creates a D3DKMT device, drives the validated umd_* device/screen code
 * through the GPUVA D3DKMT bridge and returns the Windows Asahi pipe_screen;
 * present blits the finished colour buffer into a linear present source on
 * the GPU and presents it with D3DKMTPresent (G2 HW3), or, where the kernel
 * refuses that, reads it back and draws it into the DC with GDI (DWM
 * composes the window's redirection surface either way). One screen per
 * process, created on the first DC, as Mesa's stw device expects. */
#include <windows.h>
#include <psapi.h>   /* K32EnumProcessModules (kernel32): EXP1222 module receipt */
#include <string.h>
#include "agx_kmt_gpuva_bridge.h"
/* C units of this DLL (render_allocation.c, agx_win32_screen.c) whose
 * headers have no C++ guards: C linkage, as umd_win32_screen.c sees them. */
extern "C" {
#include "render_allocation.h"
#include "agx_win32_screen.h"
}
#include "agx_wgl_present_copy.h"
#include "agx_wgl_present_ring.h"
#include "agx_wgl_draw_hook.h"
/* winsys (closure): EXP1206 switch for the native draw phase marks. */
extern "C" int AgxWin32AsahiDrawPhasesOn;
#include "umd_internal.h"
/* umd_asahi_owner.c / umd_asahi_batch_adapter.c define these with C linkage. */
extern "C" {
#include "umd_asahi_owner.h"
#include "umd_asahi_batch_adapter.h"
}
#include "agx_win32_asahi_scene.h"
/* Mesa headers carry their own C++ guards (and C++-only templates). */
#include "util/u_debug.h"
#include "util/box.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
extern "C" {
#include "stw_winsys.h" /* declares stw_init/stw_cleanup without C++ guards */
}
#include "stw_device.h"
#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"

typedef struct AGX_WGL_ADAPTER {
  AGX_KMT_THUNKS Kmt;
  PFND3DKMT_OPENADAPTERFROMHDC OpenAdapterFromHdc;
  PFND3DKMT_CREATEDEVICE CreateDevice;
  D3DKMT_HANDLE AdapterHandle, DeviceHandle;
  LUID Luid;
  D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;  /* G3: the display's vblanks */
  AGX_KMT_GPUVA_BRIDGE Bridge;
  ADMISSION_UMD_ADAPTER Runtime;
  ADMISSION_UMD_DEVICE Device;
  ADMISSION_UMD_ASAHI_OWNER Owner;
  AGX_WIN32_ASAHI_BACKEND Backend;
  AGX_WIN32_ASAHI_OWNER_OPS OwnerOperations;
  struct pipe_screen *Screen;
  HRESULT Failure;
} AGX_WGL_ADAPTER;

static AGX_WGL_ADAPTER *AgxWgl;

/* EXP1147: the native batch path reports refusals and faults through these
 * winsys hooks, which only the D3D10 glue (agx_d3d10_windows.cpp) set, so the
 * ICD's GL jobs failed silently. Log them like the D3D10 glue does. */
extern "C" {
extern void (*AgxWin32BatchRefusalHook)(unsigned, unsigned, unsigned, unsigned);
extern void (*AgxWin32BackendFailHook)(unsigned site);
extern void (*AgxWin32FirstFaultHook)(unsigned site, uintptr_t context,
                                      unsigned flags, unsigned draws);
extern void (*AgxWin32VdmTraceHook)(uint64_t va, const uint32_t *words,
                                    unsigned count, unsigned draws);
extern void (*AgxWin32PerfHook)(const char *message);
}

static void wgl_batch_refusal(unsigned kind, unsigned site, unsigned d0, unsigned d1) {
  UINT values[4] = {kind, site, d0, d1};
  AdmissionUmdDiagnostic("reject-batch", E_FAIL, values, 4u);
}

static void wgl_backend_fail(unsigned site) { wgl_batch_refusal(4u, site, 0u, 0u); }

static void wgl_first_fault(unsigned site, uintptr_t context, unsigned flags,
                            unsigned draws) {
  static volatile LONG records;
  if (InterlockedIncrement(&records) > 64) return;
  UINT values[4] = {site, (UINT)context, flags, draws};
  AdmissionUmdDiagnostic("measure-native-first-fault", S_OK, values, 4u);
}

/* EXP1182 receipt-only: the winsys calls this hook once per submitted render
 * batch with its draw count; count batches and draws for measure-wgl-draws. */
static volatile LONG AgxWglBatches, AgxWglDraws, AgxWglMaxDraws;

static void wgl_vdm_trace(uint64_t va, const uint32_t *words, unsigned count,
                          unsigned draws) {
  static volatile LONG records;
  InterlockedIncrement(&AgxWglBatches);
  InterlockedExchangeAdd(&AgxWglDraws, (LONG)draws);
  if ((LONG)draws > AgxWglMaxDraws) AgxWglMaxDraws = (LONG)draws;
  if (InterlockedIncrement(&records) > 32 || count > 13u) return;
  UINT values[16] = {(UINT)va, (UINT)(va >> 32), draws};
  for (unsigned i = 0; i < count; ++i) values[3 + i] = words[i];
  AdmissionUmdDiagnostic("measure-vdm", S_OK, values, 3u + count);
}

/* Asahi perf_debug text (why batches flush or sync): the first 200 lines. */
/* EXP1198 receipt-only: steady-state counts of the perf notes (flush and
 * sync reasons, BO cache hits/misses) by text, reported and reset with the
 * present receipt (the first 200 notes are still written one by one). */
#define AGX_WGL_PERF_SLOTS 24u
static struct { char Text[64]; UINT Count; } AgxWglPerfCounts[AGX_WGL_PERF_SLOTS];
static UINT AgxWglPerfLost;
static SRWLOCK AgxWglPerfLock = SRWLOCK_INIT;

static void wgl_perf_count(const char *message) {
  AcquireSRWLockExclusive(&AgxWglPerfLock);
  unsigned i = 0;
  for (; i < AGX_WGL_PERF_SLOTS && AgxWglPerfCounts[i].Count; ++i)
    if (!strncmp(AgxWglPerfCounts[i].Text, message, sizeof(AgxWglPerfCounts[i].Text) - 1)) break;
  if (i == AGX_WGL_PERF_SLOTS) ++AgxWglPerfLost;
  else {
    if (!AgxWglPerfCounts[i].Count)
      strncpy_s(AgxWglPerfCounts[i].Text, sizeof(AgxWglPerfCounts[i].Text), message, _TRUNCATE);
    ++AgxWglPerfCounts[i].Count;
  }
  ReleaseSRWLockExclusive(&AgxWglPerfLock);
}

static void wgl_perf_report(void) {
  AcquireSRWLockExclusive(&AgxWglPerfLock);
  for (unsigned i = 0; i < AGX_WGL_PERF_SLOTS && AgxWglPerfCounts[i].Count; ++i) {
    char stage[96];
    if (_snprintf_s(stage, sizeof(stage), _TRUNCATE, "measure-perfc %s", AgxWglPerfCounts[i].Text) >= 0) {
      for (unsigned k = 0; k < sizeof(stage) && stage[k]; ++k)
        if (stage[k] == '\n' || stage[k] == '\r' || stage[k] == ' ') stage[k] = '_';
      UINT values[2] = {AgxWglPerfCounts[i].Count, AgxWglPerfLost};
      AdmissionUmdDiagnostic(stage, S_OK, values, 2u);
    }
  }
  ZeroMemory(AgxWglPerfCounts, sizeof(AgxWglPerfCounts));
  AgxWglPerfLost = 0;
  ReleaseSRWLockExclusive(&AgxWglPerfLock);
}

static void wgl_perf_note(const char *message) {
  static volatile LONG records;
  char stage[96];
  if (message) wgl_perf_count(message);
  if (!message || InterlockedIncrement(&records) > 200) return;
  if (_snprintf_s(stage, sizeof(stage), _TRUNCATE, "measure-perf %s", message) < 0) return;
  /* One token per line: measure-perf_<text>. */
  for (unsigned i = 0; i < sizeof(stage) && stage[i]; ++i)
    if (stage[i] == '\n' || stage[i] == '\r' || stage[i] == ' ') stage[i] = '_';
  AdmissionUmdDiagnostic(stage, S_OK, NULL, 0u);
}
static struct pipe_resource *(*AgxWglResourceCreate)(struct pipe_screen *,
                                                     const struct pipe_resource *);

/* EXP1146: Mesa's WGL frontend creates window colour buffers with
 * PIPE_BIND_DISPLAY_TARGET (display through a winsys display target); the
 * Asahi driver turns that into AGX_BO_SHAREABLE, which the Windows BO layer
 * refuses, so the back buffer was never created ("no readbuffer"). This
 * winsys displays by CPU readback (wgl_present), so a window colour buffer is
 * an ordinary render target here. */
static struct pipe_resource *wgl_resource_create(struct pipe_screen *screen,
                                                 const struct pipe_resource *templ) {
  struct pipe_resource local;
  if (!templ) return NULL;
  local = *templ;
  local.bind &= ~(unsigned)PIPE_BIND_DISPLAY_TARGET;
  return AgxWglResourceCreate(screen, &local);
}

/* EXP1206: the per-draw and per-note diagnostics (draw phase and
 * draw/flush timers, fence-wait stack scans, perf-note formatting and
 * counts) run only with the UMD trace and APPLE_AGX_ICD_PHASES=1; the UMD
 * trace alone (machine-wide for experiments and play) keeps the per-frame
 * receipts only. */
static BOOL wgl_deep_diagnostics(void) {
  static int enabled = -1;
  if (enabled < 0) {
    char value[4];
    enabled = AdmissionUmdDiagnosticEnabled() &&
              GetEnvironmentVariableA("APPLE_AGX_ICD_PHASES", value, sizeof(value)) == 1u &&
              value[0] == '1';
  }
  return enabled > 0;
}

static void wgl_note(const char *stage, HRESULT status) {
  UINT values[1] = {AgxWgl ? AgxWgl->Bridge.Receipt.LastFailedOp : 0u};
  AdmissionUmdDiagnostic(stage, status, values, 1u);
}

static HRESULT open_adapter(AGX_WGL_ADAPTER *a, HDC hdc) {
  HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
  D3DKMT_OPENADAPTERFROMHDC open;
  D3DKMT_CREATEDEVICE create;
  NTSTATUS status;
  if (!gdi || !AgxKmtThunksLoad(&a->Kmt)) return E_NOINTERFACE;
  *(FARPROC *)&a->OpenAdapterFromHdc = GetProcAddress(gdi, "D3DKMTOpenAdapterFromHdc");
  *(FARPROC *)&a->CreateDevice = GetProcAddress(gdi, "D3DKMTCreateDevice");
  if (!a->OpenAdapterFromHdc || !a->CreateDevice) return E_NOINTERFACE;
  ZeroMemory(&open, sizeof(open));
  open.hDc = hdc;
  status = a->OpenAdapterFromHdc(&open);
  if (status < 0) return AgxKmtGpuvaResult(status);
  a->AdapterHandle = open.hAdapter;
  a->Luid = open.AdapterLuid;
  a->VidPnSourceId = open.VidPnSourceId;
  ZeroMemory(&create, sizeof(create));
  create.hAdapter = open.hAdapter;
  status = a->CreateDevice(&create);
  if (status < 0) return AgxKmtGpuvaResult(status);
  a->DeviceHandle = create.hDevice;
  return S_OK;
}

/* The umd_* entry points take the D3D10 DDI argument blocks; fill them with
 * the bridge's callback tables (the bridge is every runtime handle). */
static HRESULT open_runtime(AGX_WGL_ADAPTER *a) {
  D3D10DDIARG_OPENADAPTER open;
  D3D10DDIARG_CREATEDEVICE create;
  HRESULT result = AgxKmtGpuvaBridgeInitialize(&a->Bridge, &a->Kmt,
                                               a->AdapterHandle, a->DeviceHandle);
  if (FAILED(result)) return result;
  a->Bridge.Receipt.ScanWaits = wgl_deep_diagnostics();
  ZeroMemory(&open, sizeof(open));
  open.hRTAdapter.handle = &a->Bridge;
  open.Interface = D3DWDDM1_3_DDI_INTERFACE_VERSION;
  open.Version = D3DWDDM1_3_DDI_BUILD_VERSION;
  open.pAdapterCallbacks = &a->Bridge.AdapterCallbacks;
  result = AdmissionUmdRuntimeAdapterInitialize(&a->Runtime, &open);
  if (FAILED(result)) return result;
  ZeroMemory(&create, sizeof(create));
  create.hRTDevice.handle = &a->Bridge;
  create.hRTCoreLayer.handle = &a->Bridge;
  create.Interface = D3DWDDM1_3_DDI_INTERFACE_VERSION;
  create.Version = D3DWDDM1_3_DDI_BUILD_VERSION;
  create.pKTCallbacks = &a->Bridge.DeviceCallbacks;
  create.p11UMCallbacks = &a->Bridge.CoreCallbacks;
  create.DXGIBaseDDI.pDXGIBaseCallbacks = &a->Bridge.DxgiCallbacks;
  return AdmissionUmdRuntimeDeviceInitialize(&a->Device, &a->Runtime, &create);
}

static struct pipe_screen *wgl_screen_create(HDC hdc) {
  AGX_WGL_ADAPTER *a;
  HRESULT result;
  if (AgxWgl) return AgxWgl->Screen;
  a = (AGX_WGL_ADAPTER *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*a));
  if (!a) return NULL;
  AgxWgl = a;
  AgxWin32BatchRefusalHook = wgl_batch_refusal;
  AgxWin32BackendFailHook = wgl_backend_fail;
  AgxWin32FirstFaultHook = wgl_first_fault;
  AgxWin32VdmTraceHook = wgl_vdm_trace;
  /* EXP1206: a perf hook makes the winsys format every perf note. */
  AgxWin32PerfHook = wgl_deep_diagnostics() ? wgl_perf_note : NULL;
  AgxWin32AsahiDrawPhasesOn = wgl_deep_diagnostics();
  agx_wgl_draw_hook_timing(wgl_deep_diagnostics() != FALSE);
  result = open_adapter(a, hdc);
  if (SUCCEEDED(result)) result = open_runtime(a);
  if (SUCCEEDED(result)) {
    a->Owner.Device = &a->Device;
    a->Owner.Backend = &a->Backend;
    /* Every Mesa WGL pixel format has depth: GL windows submit ZLS. */
    a->Backend.ZlsAttachments = 1;
    AdmissionUmdAsahiOwnerOperations(&a->OwnerOperations);
    a->Screen = AgxWin32AsahiScreenCreateForWindows(&a->Backend, &a->Device.Screen,
        &a->OwnerOperations, &a->Owner, AdmissionUmdAsahiBatchOperations());
    if (!a->Screen)
      result = FAILED(a->Device.LastScreenError) ? a->Device.LastScreenError : E_FAIL;
    else if (!a->Screen->resource_create)
      result = E_NOINTERFACE;
    else {
      AgxWglResourceCreate = a->Screen->resource_create;
      a->Screen->resource_create = wgl_resource_create;
      /* EXP1183: merge GL multi-draws before Asahi splits them per draw. */
      if (!agx_wgl_draw_hook_install(a->Screen))
        wgl_note("reject-wgl-draw-hook", E_FAIL);
    }
  }
  a->Failure = result;
  wgl_note(SUCCEEDED(result) ? "wgl-screen-create" : "reject-wgl-screen-create", result);
  /* G2 HW3 receipt-only: which present callbacks opengl32 handed the ICD
   * (DrvSetCallbackProcs, before the first context): PresentBuffers,
   * GetAdapterLuid, CheckFullscreenSupport, PresentToRedirectionSurface,
   * SubmitPresentToRedirectionSurface, and the D3DKMTPresent export. */
  if (stw_dev) {
    UINT callbacks[6] = {
      stw_dev->callbacks.pfnPresentBuffers != NULL,
#if WINVER >= 0xA00
      stw_dev->callbacks.pfnGetAdapterLuid != NULL,
      stw_dev->callbacks.pfnCheckFullscreenSupport != NULL,
      stw_dev->callbacks.pfnPresentToRedirectionSurface != NULL,
      stw_dev->callbacks.pfnSubmitPresentToRedirectionSurface != NULL,
#else
      2u, 2u, 2u, 2u,
#endif
      a->Kmt.Present != NULL };
    AdmissionUmdDiagnostic("measure-wgl-callbacks", S_OK, callbacks, 6u);
  }
  /* A failed adapter stays recorded (no second attempt with half-owned
   * kernel objects); stw reports the missing screen to the application. */
  return a->Screen;
}

/* Copy a mapped BGRX/RGBX image into the window: SetDIBitsToDevice with a
 * top-down 32-bit DIB, repacking when the pitch or channel order differs. */
static void wgl_blit_to_dc(HDC hdc, const BYTE *map, unsigned stride,
                           unsigned width, unsigned height, BOOL bgra) {
  BITMAPINFO info;
  ZeroMemory(&info, sizeof(info));
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = (LONG)width;
  info.bmiHeader.biHeight = -(LONG)height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  if (bgra && stride == width * 4u) {
    SetDIBitsToDevice(hdc, 0, 0, width, height, 0, 0, 0, height, map, &info,
                      DIB_RGB_COLORS);
    return;
  }
  BYTE *packed = (BYTE *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)width * height * 4u);
  if (!packed) return;
  for (unsigned y = 0; y < height; ++y) {
    const BYTE *src = map + (SIZE_T)y * stride;
    BYTE *dst = packed + (SIZE_T)y * width * 4u;
    if (bgra) memcpy(dst, src, (SIZE_T)width * 4u);
    else for (unsigned x = 0; x < width; ++x) {
      dst[4 * x + 0] = src[4 * x + 2]; dst[4 * x + 1] = src[4 * x + 1];
      dst[4 * x + 2] = src[4 * x + 0]; dst[4 * x + 3] = src[4 * x + 3];
    }
  }
  SetDIBitsToDevice(hdc, 0, 0, width, height, 0, 0, 0, height, packed, &info,
                    DIB_RGB_COLORS);
  HeapFree(GetProcessHeap(), 0, packed);
}

/* EXP1168: mapping the back buffer for read each frame flushed the frame,
 * waited for the GPU, blitted into a staging copy and waited again, so the
 * CPU (the game, under x86 emulation) and the GPU never overlapped: CS 1.6
 * fullscreen ran at ~13 presents per second. Present queues a GPU copy of
 * frame N into a linear staging image and submits it without waiting.
 *
 * EXP1171: the show (copy out of the write-combined mapping + SetDIBits,
 * ~14 ms in play) still ran on the game thread. A present worker now shows
 * the newest finished frame while the game builds the next, through a
 * three-slot mailbox (agx_wgl_present_ring.h): the game thread queues the
 * GPU copy of frame N into a free slot, makes the slot of frame N-1
 * CPU-visible (texture_map + unmap: waits for its copy, normally finished
 * long ago) and hands it to the worker if the worker is idle; otherwise the
 * frame waits and a newer one replaces it. Every pipe_context call stays on
 * the game thread. The worker reads the staging image through its BO's CPU
 * mapping: a linear staging texture maps directly to agx_bo_map, which stays
 * valid for the BO's lifetime (the ring holds the reference and drains the
 * worker before releasing it). The worker draws through its own GetDC of
 * the window. One ring per window, size and format (EXP1191: keyed by the
 * window, as the winsys framebuffer presents without a DC). */
typedef struct {
  HWND Window;
  unsigned Width, Height;
  enum pipe_format Format;
  BOOL Bgra;
  struct pipe_resource *Staging[AGX_WGL_RING_SLOTS];
  /* EXP1195: completion of each slot's GPU copy; a copied slot goes to the
   * worker once its fence has signalled (polled, never waited for). */
  struct pipe_screen *Screen;
  struct pipe_fence_handle *Fence[AGX_WGL_RING_SLOTS];
  const BYTE *Map[AGX_WGL_RING_SLOTS];
  unsigned Stride[AGX_WGL_RING_SLOTS];
  unsigned State[AGX_WGL_RING_SLOTS];  /* AGX_WGL_SLOT_*, AgxWglWorkerLock */
  long long Seq[AGX_WGL_RING_SLOTS];
  BYTE *Cpu;                 /* cached copy of the shown image (EXP1170) */
  /* Receipt sums: game thread (present, interval, sync) and worker (show,
   * copy, dib per shown frame), the latter under AgxWglWorkerLock. */
  LONGLONG Frames, LastTick, SumPresent, SumInterval, SumSync;
  LONGLONG SumShow, SumCopy, SumDib;
  unsigned Shown, Dropped, DibFailed, Slices;
  unsigned NotReady;  /* EXP1195: presents whose shown candidate was still copying */
} AGX_WGL_PRESENT_RING;
static AGX_WGL_PRESENT_RING AgxWglRings[4];
static SRWLOCK AgxWglRingLock = SRWLOCK_INIT;     /* the ring table; taken first */
static SRWLOCK AgxWglWorkerLock = SRWLOCK_INIT;   /* slot states and the job */
static CONDITION_VARIABLE AgxWglWorkerWake = CONDITION_VARIABLE_INIT;
static CONDITION_VARIABLE AgxWglWorkerIdle = CONDITION_VARIABLE_INIT;
static AGX_WGL_PRESENT_RING *AgxWglJobRing;       /* non-NULL: a job is queued or running */
static unsigned AgxWglJobSlot;
static HANDLE AgxWglWorker;

/* EXP1170: the copy out of the write-combined mapping is ~92% of the show
 * (19 ms per 2560x1600 present on one thread). Uncached loads are latency
 * bound per core, so the rows are split into AGX_WGL_COPY_SLICES slices
 * copied concurrently on the process thread pool; the copying thread copies
 * one slice itself and waits for the others. Only the present worker copies
 * through the single job. */
typedef struct {
  BYTE *Dst;
  const BYTE *Src;
  SIZE_T DstStride, SrcStride, RowBytes;
  unsigned Height;
  volatile LONG Next;
} AGX_WGL_COPY_JOB;
static AGX_WGL_COPY_JOB AgxWglCopyJob;
static PTP_WORK AgxWglCopyWork;

static VOID CALLBACK wgl_copy_slice(PTP_CALLBACK_INSTANCE instance, PVOID context,
                                    PTP_WORK work) {
  (void)instance; (void)work;
  AGX_WGL_COPY_JOB *job = (AGX_WGL_COPY_JOB *)context;
  unsigned index = (unsigned)InterlockedIncrement(&job->Next) - 1u;
  unsigned first, end;
  if (index >= AGX_WGL_COPY_SLICES) return;
  agx_wgl_slice_rows(job->Height, AGX_WGL_COPY_SLICES, index, &first, &end);
  agx_wgl_copy_rows(job->Dst, job->DstStride, job->Src, job->SrcStride,
                    job->RowBytes, first, end);
}

/* Returns the number of slices copied concurrently (1: no thread pool). */
static unsigned wgl_copy_image(BYTE *dst, const BYTE *src, unsigned src_stride,
                               unsigned width, unsigned height) {
  if (!AgxWglCopyWork)
    AgxWglCopyWork = CreateThreadpoolWork(wgl_copy_slice, &AgxWglCopyJob, NULL);
  if (!AgxWglCopyWork) {
    agx_wgl_copy_rows(dst, (SIZE_T)width * 4u, src, src_stride,
                      (SIZE_T)width * 4u, 0u, height);
    return 1u;
  }
  AgxWglCopyJob.Dst = dst;
  AgxWglCopyJob.Src = src;
  AgxWglCopyJob.DstStride = AgxWglCopyJob.RowBytes = (SIZE_T)width * 4u;
  AgxWglCopyJob.SrcStride = src_stride;
  AgxWglCopyJob.Height = height;
  AgxWglCopyJob.Next = 0;
  for (unsigned i = 1; i < AGX_WGL_COPY_SLICES; ++i)
    SubmitThreadpoolWork(AgxWglCopyWork);
  wgl_copy_slice(NULL, &AgxWglCopyJob, AgxWglCopyWork);
  WaitForThreadpoolWorkCallbacks(AgxWglCopyWork, FALSE);
  return AGX_WGL_COPY_SLICES;
}

/* The worker: copy the slot into cached memory (EXP1169: SetDIBits straight
 * from the write-combined mapping took 71 ms) and draw it into the window.
 * The ring's fields other than State/receipts are fixed while a job for it
 * is outstanding (wgl_ring_drain). It never exits: it pins the ICD. */
static DWORD WINAPI wgl_present_worker(LPVOID unused) {
  (void)unused;
  AcquireSRWLockExclusive(&AgxWglWorkerLock);
  for (;;) {
    while (!AgxWglJobRing)
      SleepConditionVariableSRW(&AgxWglWorkerWake, &AgxWglWorkerLock, INFINITE, 0);
    AGX_WGL_PRESENT_RING *ring = AgxWglJobRing;
    unsigned slot = AgxWglJobSlot;
    ReleaseSRWLockExclusive(&AgxWglWorkerLock);
    LARGE_INTEGER t0, t1, t2;
    QueryPerformanceCounter(&t0);
    unsigned slices = wgl_copy_image(ring->Cpu, ring->Map[slot], ring->Stride[slot],
                                     ring->Width, ring->Height);
    QueryPerformanceCounter(&t1);
    HDC dc = GetDC(ring->Window);
    if (dc) {
      wgl_blit_to_dc(dc, ring->Cpu, ring->Width * 4u, ring->Width, ring->Height,
                     ring->Bgra);
      ReleaseDC(ring->Window, dc);
    }
    QueryPerformanceCounter(&t2);
    AcquireSRWLockExclusive(&AgxWglWorkerLock);
    ring->SumShow += t2.QuadPart - t0.QuadPart;
    ring->SumCopy += t1.QuadPart - t0.QuadPart;
    ring->SumDib += t2.QuadPart - t1.QuadPart;
    ring->Slices = slices;
    ++ring->Shown;
    if (!dc) ++ring->DibFailed;
    ring->State[slot] = AGX_WGL_SLOT_FREE;
    AgxWglJobRing = NULL;
    WakeAllConditionVariable(&AgxWglWorkerIdle);
  }
}

static BOOL wgl_worker_start(void) {
  HMODULE self = NULL;
  if (AgxWglWorker) return TRUE;
  /* The worker never exits, so it holds a reference that keeps the ICD
   * loaded for the life of the process. */
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                          (LPCWSTR)(void *)&wgl_present_worker, &self))
    return FALSE;
  AgxWglWorker = CreateThread(NULL, 0, wgl_present_worker, NULL, 0, NULL);
  if (!AgxWglWorker) { FreeLibrary(self); return FALSE; }
  return TRUE;
}

static void wgl_ring_drain(AGX_WGL_PRESENT_RING *ring) {
  AcquireSRWLockExclusive(&AgxWglWorkerLock);
  while (AgxWglJobRing == ring)
    SleepConditionVariableSRW(&AgxWglWorkerIdle, &AgxWglWorkerLock, INFINITE, 0);
  ReleaseSRWLockExclusive(&AgxWglWorkerLock);
}

static void wgl_ring_release(AGX_WGL_PRESENT_RING *ring) {
  wgl_ring_drain(ring);
  for (unsigned i = 0; i < AGX_WGL_RING_SLOTS; ++i) {
    if (ring->Fence[i] && ring->Screen)
      ring->Screen->fence_reference(ring->Screen, &ring->Fence[i], NULL);
    pipe_resource_reference(&ring->Staging[i], NULL);
  }
  if (ring->Cpu) HeapFree(GetProcessHeap(), 0, ring->Cpu);
  ZeroMemory(ring, sizeof(*ring));
}

static AGX_WGL_PRESENT_RING *wgl_ring_get(struct pipe_screen *screen, HWND window,
                                          const struct pipe_resource *res,
                                          BOOL bgra) {
  AGX_WGL_PRESENT_RING *free_ring = NULL;
  for (unsigned i = 0; i < 4; ++i) {
    AGX_WGL_PRESENT_RING *ring = &AgxWglRings[i];
    if (ring->Staging[0] && ring->Window == window) {
      if (ring->Width == res->width0 && ring->Height == res->height0 &&
          ring->Format == res->format) return ring;
      wgl_ring_release(ring);
    }
    if (!ring->Staging[0] && !free_ring) free_ring = ring;
  }
  if (!window || !wgl_worker_start()) return NULL;
  if (!free_ring) { free_ring = &AgxWglRings[0]; wgl_ring_release(free_ring); }
  struct pipe_resource templ;
  memset(&templ, 0, sizeof(templ));
  templ.target = PIPE_TEXTURE_2D;
  templ.format = res->format;
  templ.width0 = res->width0;
  templ.height0 = res->height0;
  templ.depth0 = 1;
  templ.array_size = 1;
  templ.usage = PIPE_USAGE_STAGING;
  free_ring->Screen = screen;
  for (unsigned i = 0; i < AGX_WGL_RING_SLOTS; ++i) {
    free_ring->Staging[i] = screen->resource_create(screen, &templ);
    if (!free_ring->Staging[i]) { wgl_ring_release(free_ring); return NULL; }
  }
  free_ring->Cpu = (BYTE *)HeapAlloc(GetProcessHeap(), 0,
                                     (SIZE_T)res->width0 * res->height0 * 4u);
  if (!free_ring->Cpu) { wgl_ring_release(free_ring); return NULL; }
  free_ring->Window = window;
  free_ring->Width = res->width0;
  free_ring->Height = res->height0;
  free_ring->Format = res->format;
  free_ring->Bgra = bgra;
  return free_ring;
}

/* Synchronous show for a DC without a ring (no window or no worker). */
static void wgl_show_now(struct pipe_context *ctx, struct pipe_resource *image,
                         HDC hdc, unsigned width, unsigned height, BOOL bgra) {
  struct pipe_transfer *transfer = NULL;
  struct pipe_box box;
  u_box_2d(0, 0, (int)width, (int)height, &box);
  const BYTE *map = (const BYTE *)ctx->texture_map(ctx, image, 0, PIPE_MAP_READ,
                                                   &box, &transfer);
  if (!map || !transfer) {
    wgl_note("reject-wgl-present-map", E_FAIL);
    return;
  }
  wgl_blit_to_dc(hdc, map, transfer->stride, width, height, bgra);
  ctx->texture_unmap(ctx, transfer);
}

/* Make the GPU copy into `slot` CPU-visible: the READ map waits for its
 * writer; the linear staging map is the BO's persistent CPU mapping. */
static BOOL wgl_ring_sync(struct pipe_context *ctx, AGX_WGL_PRESENT_RING *ring,
                          unsigned slot) {
  struct pipe_transfer *transfer = NULL;
  struct pipe_box box;
  u_box_2d(0, 0, (int)ring->Width, (int)ring->Height, &box);
  const BYTE *map = (const BYTE *)ctx->texture_map(ctx, ring->Staging[slot], 0,
                                                   PIPE_MAP_READ, &box, &transfer);
  if (!map || !transfer) {
    wgl_note("reject-wgl-present-map", E_FAIL);
    return FALSE;
  }
  ring->Map[slot] = map;
  ring->Stride[slot] = transfer->stride;
  ctx->texture_unmap(ctx, transfer);
  return TRUE;
}

/* EXP1173 receipt-only: kernel time of the last 120 presents, per group of
 * D3DKMT thunks (calls, microseconds): submit, CPU wait, lock/unlock,
 * residency, allocation, GPU VA, escape, signals. With the present on a
 * worker, the game thread's frame is its own work plus these calls. */
static unsigned wgl_kmt_group(unsigned op) {
  switch (op) {
  case AgxKmtGpuvaSubmit: return 1;
  case AgxKmtGpuvaWaitCpu: return 2;
  case AgxKmtGpuvaLock: case AgxKmtGpuvaUnlock: return 3;
  case AgxKmtGpuvaMakeResident: case AgxKmtGpuvaEvict:
  case AgxKmtGpuvaQueryResidency: case AgxKmtGpuvaSetPriority: return 4;
  case AgxKmtGpuvaAllocate: case AgxKmtGpuvaDeallocate: return 5;
  case AgxKmtGpuvaReserve: case AgxKmtGpuvaMap: case AgxKmtGpuvaFree: return 6;
  case AgxKmtGpuvaEscape: return 7;
  case AgxKmtGpuvaSignalGpu2: case AgxKmtGpuvaSignal2: return 8;
  default: return 0;
  }
}

extern "C" IMAGE_DOS_HEADER __ImageBase;  /* this image (linker-provided) */
/* agx_wgl_framebuffer.c */
extern "C" struct stw_winsys_framebuffer *agx_wgl_create_framebuffer(
    struct pipe_screen *screen, HWND window, int pixel_format);
extern "C" void agx_wgl_framebuffer_counters(UINT out[6]);
extern "C" void agx_wgl_pace_counters(UINT out[8]);
extern "C" void agx_wgl_vblank_counters(UINT out[6]);
/* winsys (closure): EXP1202 native draw phase receipt. */
extern "C" void AgxWin32AsahiDrawPhases(unsigned out[6]);

static void wgl_kmt_receipt(LONGLONG us) {
  static UINT last_calls[AgxKmtGpuvaOpCount];
  static LONGLONG last_ticks[AgxKmtGpuvaOpCount];
  UINT calls[8] = {0};
  LONGLONG ticks[8] = {0};
  if (!AgxWgl || !us) return;
  AGX_KMT_GPUVA_RECEIPT *r = &AgxWgl->Bridge.Receipt;
  for (unsigned op = 0; op < AgxKmtGpuvaOpCount; ++op) {
    unsigned g = wgl_kmt_group(op);
    if (g) {
      calls[g - 1] += r->Calls[op] - last_calls[op];
      ticks[g - 1] += r->Ticks[op] - last_ticks[op];
    }
    last_calls[op] = r->Calls[op];
    last_ticks[op] = r->Ticks[op];
  }
  UINT values[16];
  for (unsigned i = 0; i < 8; ++i) {
    values[2 * i] = calls[i];
    values[2 * i + 1] = (UINT)(ticks[i] * 1000000 / us);
  }
  AdmissionUmdDiagnostic("measure-wgl-kmt", S_OK, values, 16u);
  /* EXP1189: per caller chain of the fence waits since the last receipt,
   * the return addresses (image RVAs; EXP1192: eight), waits and
   * microseconds. */
  for (unsigned i = 0; i < AGX_KMT_WAIT_SITES; ++i) {
    AGX_KMT_WAIT_SITE *s = &r->WaitSites[i];
    if (!s->Calls) continue;
    UINT site[AGX_KMT_WAIT_DEPTH + 3u];
    for (unsigned k = 0; k < AGX_KMT_WAIT_DEPTH; ++k)
      site[k] = s->Return[k] ? (UINT)(s->Return[k] - (ULONG_PTR)&__ImageBase) : 0u;
    site[AGX_KMT_WAIT_DEPTH] = s->Calls;
    site[AGX_KMT_WAIT_DEPTH + 1u] = (UINT)(s->Ticks * 1000000 / us);
    site[AGX_KMT_WAIT_DEPTH + 2u] = r->WaitSitesLost;
    AdmissionUmdDiagnostic("measure-wgl-wait", S_OK, site, AGX_KMT_WAIT_DEPTH + 3u);
    /* EXP1193: the site's callers in other modules: chain RVA 0 (to pair
     * the line with its site), position, module RVA, waits, microseconds. */
    for (unsigned k = 0; k < AGX_KMT_WAIT_FOREIGN; ++k) {
      HMODULE module = NULL;
      char path[MAX_PATH], stage[96];
      if (!s->Foreign[k] ||
          !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCSTR)s->Foreign[k], &module) ||
          !GetModuleFileNameA(module, path, MAX_PATH)) continue;
      const char *slash = strrchr(path, '\\');
      if (_snprintf_s(stage, sizeof(stage), _TRUNCATE, "measure-wgl-waitf_%s",
                      slash ? slash + 1 : path) < 0) continue;
      UINT caller[5] = {site[0], k, (UINT)(s->Foreign[k] - (ULONG_PTR)module),
                        s->Calls, site[AGX_KMT_WAIT_DEPTH + 1u]};
      AdmissionUmdDiagnostic(stage, S_OK, caller, 5u);
    }
  }
  ZeroMemory(r->WaitSites, sizeof(r->WaitSites));
  r->WaitSitesLost = 0;
}

/* Receipt (EXP1168/EXP1171): every 120 presents, in microseconds, the mean
 * game-thread present, worker show per shown frame, present-to-present
 * interval, size, game-thread sync (map wait), worker copy and SetDIBits per
 * shown frame, copy slices, frames shown and dropped, and failed GetDC. */
static void wgl_common_receipt(LONGLONG us);

static void wgl_present_receipt(AGX_WGL_PRESENT_RING *ring, LONGLONG start,
                                LONGLONG end) {
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  ring->SumPresent += end - start;
  if (ring->LastTick) ring->SumInterval += start - ring->LastTick;
  ring->LastTick = start;
  if (++ring->Frames % 120 != 0 || !frequency.QuadPart) return;
  LONGLONG us = frequency.QuadPart;
  AcquireSRWLockExclusive(&AgxWglWorkerLock);
  LONGLONG shown = ring->Shown ? ring->Shown : 1;
  UINT values[13] = {
    (UINT)(ring->SumPresent * 1000000 / us / 120),
    (UINT)(ring->SumShow * 1000000 / us / shown),
    (UINT)(ring->SumInterval * 1000000 / us / 120),
    ring->Width, ring->Height,
    (UINT)(ring->SumSync * 1000000 / us / 120),
    (UINT)(ring->SumCopy * 1000000 / us / shown),
    (UINT)(ring->SumDib * 1000000 / us / shown),
    ring->Slices, ring->Shown, ring->Dropped, ring->DibFailed,
    ring->NotReady /* EXP1195 */ };
  ring->SumShow = ring->SumCopy = ring->SumDib = 0;
  ring->Shown = ring->Dropped = ring->DibFailed = ring->NotReady = 0;
  ReleaseSRWLockExclusive(&AgxWglWorkerLock);
  AdmissionUmdDiagnostic("measure-wgl-present", S_OK, values, 13u);
  wgl_common_receipt(us);
  ring->SumPresent = ring->SumInterval = ring->SumSync = 0;
}

/* The receipts of every 120 presents whichever path showed them. */
static void wgl_common_receipt(LONGLONG us) {
  wgl_kmt_receipt(us);
  /* EXP1182: render batches, draws and the largest batch of the 120 presents. */
  UINT draws[3] = {(UINT)InterlockedExchange(&AgxWglBatches, 0),
                   (UINT)InterlockedExchange(&AgxWglDraws, 0),
                   (UINT)InterlockedExchange(&AgxWglMaxDraws, 0)};
  AdmissionUmdDiagnostic("measure-wgl-draws", S_OK, draws, 3u);
  /* EXP1183: draw calls/draws reaching the driver hook, merged calls/draws,
   * POLYGON/QUADS calls converted per draw. */
  UINT merge[AGX_WGL_MERGE_COUNTERS];
  agx_wgl_draw_hook_counters(merge);
  AdmissionUmdDiagnostic("measure-wgl-merge", S_OK, merge, AGX_WGL_MERGE_COUNTERS);
  /* EXP1197: draw_vbo calls/us and flush calls/us below the draw hook. */
  UINT cpu[AGX_WGL_CPU_COUNTERS];
  agx_wgl_draw_hook_cpu(cpu);
  AdmissionUmdDiagnostic("measure-wgl-cpu", S_OK, cpu, AGX_WGL_CPU_COUNTERS);
  wgl_perf_report();  /* EXP1198 */
  /* EXP1202: native draw phases (draws, us per phase). */
  UINT phases[6];
  AgxWin32AsahiDrawPhases(phases);
  AdmissionUmdDiagnostic("measure-wgl-phase", S_OK, phases, 6u);
  /* EXP1203: CPU time of the presenting (game) thread and of the process
   * over the last 120 presents, us (kernel+user): with the frame interval it
   * tells pure CPU work from waiting or preemption. */
  {
    static ULONGLONG last_thread, last_process;
    FILETIME c, e, k, u;
    ULONGLONG thread = 0, process = 0;
    if (GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u))
      thread = (((ULONGLONG)k.dwHighDateTime << 32) | k.dwLowDateTime) +
               (((ULONGLONG)u.dwHighDateTime << 32) | u.dwLowDateTime);
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
      process = (((ULONGLONG)k.dwHighDateTime << 32) | k.dwLowDateTime) +
                (((ULONGLONG)u.dwHighDateTime << 32) | u.dwLowDateTime);
    UINT times[2] = {last_thread ? (UINT)((thread - last_thread) / 10u) : 0u,
                     last_process ? (UINT)((process - last_process) / 10u) : 0u};
    last_thread = thread; last_process = process;
    AdmissionUmdDiagnostic("measure-wgl-threadtime", S_OK, times, 2u);
  }
  /* EXP1192: winsys framebuffers created, refused (last refused pixel
   * format's dwFlags and samples), presents, failed presents. */
  UINT fb[6];
  agx_wgl_framebuffer_counters(fb);
  AdmissionUmdDiagnostic("measure-wgl-winsysfb", S_OK, fb, 6u);
  /* EXP1209: swap pacing (paced swaps, waits, waited us, grid restarts,
   * swap intervals < 3/4, 3/4..5/4, 5/4..7/4, >= 7/4 refresh periods). */
  UINT pace[8];
  agx_wgl_pace_counters(pace);
  AdmissionUmdDiagnostic("measure-wgl-pace", S_OK, pace, 8u);
  /* G3: vblank-paced swaps, vblank waits, waited us, late swaps (no wait),
   * failed waits, last failure status. */
  UINT vblank[6];
  agx_wgl_vblank_counters(vblank);
  AdmissionUmdDiagnostic("measure-wgl-vblank", S_OK, vblank, 6u);
}

/* EXP1174 diagnostic, only when the UMD trace is enabled and the process has
 * APPLE_AGX_ICD_SAMPLE=1: a sampling
 * profiler of the presenting (game) thread. EXP1173 gameplay: ~25 ms of a
 * 27 ms frame is CPU work outside the kernel (game, Mesa and this ICD under
 * x86 emulation) and WPR CPU sampling is unavailable in this guest. Every
 * 2 ms the sampler suspends the game thread, reads its x86 EIP and resumes
 * it (no other call while it is suspended); every 5000 samples it reports
 * the samples per module (measure-sample-module_<file>: index, base, count)
 * and the 48 hottest 16-byte buckets (measure-sample-top: module index, RVA,
 * count; symbolised offline from the PDBs). */
#define AGX_WGL_SAMPLE_SLOTS 8192u
#define AGX_WGL_SAMPLE_REPORT 5000u
typedef struct { ULONG_PTR Bucket; UINT Count; } AGX_WGL_SAMPLE;
static AGX_WGL_SAMPLE AgxWglSamples[AGX_WGL_SAMPLE_SLOTS];
static UINT AgxWglSampleLost;
/* EXP1185: for samples whose EIP is outside any image (the emulator's
 * native-call gate: 18-50% of the game thread in EXP1184), the code
 * addresses found in the top 64 bytes of the x86 stack, i.e. who called. */
static AGX_WGL_SAMPLE AgxWglCallers[AGX_WGL_SAMPLE_SLOTS];
static UINT AgxWglCallerLost;
static HANDLE AgxWglSampler;

static void wgl_sample_put(AGX_WGL_SAMPLE *table, UINT *lost, ULONG_PTR eip) {
  ULONG_PTR bucket = eip & ~(ULONG_PTR)15;
  UINT slot = (UINT)((bucket >> 4) * 2654435761u) & (AGX_WGL_SAMPLE_SLOTS - 1u);
  for (UINT probe = 0; probe < 64u; ++probe) {
    AGX_WGL_SAMPLE *s = &table[(slot + probe) & (AGX_WGL_SAMPLE_SLOTS - 1u)];
    if (s->Bucket == bucket) { ++s->Count; return; }
    if (!s->Count) { s->Bucket = bucket; s->Count = 1; return; }
  }
  ++*lost;
}

static void wgl_sample_add(ULONG_PTR eip) {
  wgl_sample_put(AgxWglSamples, &AgxWglSampleLost, eip);
}

static BOOL wgl_sample_in_image(ULONG_PTR address) {
  MEMORY_BASIC_INFORMATION info;
  return address >= 0x10000u &&
         VirtualQuery((LPCVOID)address, &info, sizeof(info)) &&
         info.Type == MEM_IMAGE && (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                                   PAGE_EXECUTE_READWRITE |
                                                   PAGE_EXECUTE_WRITECOPY));
}

static void wgl_sample_callers(ULONG_PTR eip, const DWORD *stack, SIZE_T words) {
  if (wgl_sample_in_image(eip)) return;
  for (SIZE_T i = 0, found = 0; i < words && found < 3u; ++i)
    if (wgl_sample_in_image(stack[i])) {
      wgl_sample_put(AgxWglCallers, &AgxWglCallerLost, stack[i]);
      ++found;
    }
}

static ULONG_PTR wgl_sample_base(ULONG_PTR address) {
  MEMORY_BASIC_INFORMATION info;
  if (!VirtualQuery((LPCVOID)address, &info, sizeof(info))) return 0;
  return (ULONG_PTR)info.AllocationBase;
}

static void wgl_sample_report_table(AGX_WGL_SAMPLE *table, UINT *lost,
                                    const char *module_tag, const char *top_tag) {
  ULONG_PTR bases[16] = {0};
  UINT counts[16] = {0}, modules = 0;
  UINT order[48], ranked = 0;
  for (UINT i = 0; i < AGX_WGL_SAMPLE_SLOTS; ++i) {
    if (!table[i].Count) continue;
    ULONG_PTR base = wgl_sample_base(table[i].Bucket);
    UINT m = 0;
    while (m < modules && bases[m] != base) ++m;
    if (m == modules && modules < 15u) bases[modules++] = base;
    else if (m == modules) m = 15u;   /* everything else */
    counts[m] += table[i].Count;
    /* keep the 48 hottest buckets (insertion into a small sorted list) */
    UINT at;
    if (ranked < 48u) at = ranked++;
    else if (table[order[47]].Count >= table[i].Count) continue;
    else at = 47u;
    while (at && table[order[at - 1]].Count < table[i].Count) {
      order[at] = order[at - 1];
      --at;
    }
    order[at] = i;
  }
  for (UINT m = 0; m < 16u; ++m) {
    if (!counts[m]) continue;
    char path[MAX_PATH], stage[96];
    const char *name = "other";
    if (m < 15u && bases[m] && GetModuleFileNameA((HMODULE)bases[m], path, MAX_PATH)) {
      const char *slash = strrchr(path, '\\');
      name = slash ? slash + 1 : path;
    } else if (m < 15u) name = "anonymous";
    if (_snprintf_s(stage, sizeof(stage), _TRUNCATE, "%s%s", module_tag, name) < 0) continue;
    UINT values[4] = {m, (UINT)bases[m], counts[m], *lost};
    AdmissionUmdDiagnostic(stage, S_OK, values, 4u);
  }
  for (UINT r = 0; r < ranked; r += 5u) {
    UINT values[15], n = 0;
    for (UINT k = r; k < ranked && k < r + 5u; ++k) {
      const AGX_WGL_SAMPLE *s = &table[order[k]];
      ULONG_PTR base = wgl_sample_base(s->Bucket);
      UINT m = 0;
      while (m < modules && bases[m] != base) ++m;
      if (m == modules) m = 15u;
      values[n++] = m;
      values[n++] = (UINT)(s->Bucket - base);
      values[n++] = s->Count;
    }
    AdmissionUmdDiagnostic(top_tag, S_OK, values, n);
  }
  ZeroMemory(table, sizeof(AGX_WGL_SAMPLE) * AGX_WGL_SAMPLE_SLOTS);
  *lost = 0;
}

static void wgl_sample_report(void) {
  wgl_sample_report_table(AgxWglSamples, &AgxWglSampleLost,
                          "measure-sample-module_", "measure-sample-top");
  wgl_sample_report_table(AgxWglCallers, &AgxWglCallerLost,
                          "measure-sample-cmodule_", "measure-sample-ctop");
}

static DWORD WINAPI wgl_sampler_main(LPVOID parameter) {
  HANDLE game = (HANDLE)parameter;
  HANDLE timer = CreateWaitableTimerExW(NULL, NULL,
      CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!timer) timer = CreateWaitableTimerW(NULL, FALSE, NULL);
  LARGE_INTEGER due;
  due.QuadPart = -20000;   /* 2 ms */
  if (!timer || !SetWaitableTimer(timer, &due, 2, NULL, NULL, FALSE)) return 0;
  for (UINT samples = 0;;) {
    WaitForSingleObject(timer, INFINITE);
    if (SuspendThread(game) == (DWORD)-1) return 0;   /* the game thread exited */
    CONTEXT context;
    ZeroMemory(&context, sizeof(context));
    context.ContextFlags = CONTEXT_CONTROL;
    BOOL ok = GetThreadContext(game, &context);
#if defined(_M_IX86)
    DWORD stack[16];
    SIZE_T got = 0;
    /* Read the top of the x86 stack while the thread is still suspended. */
    if (ok && !ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(ULONG_PTR)context.Esp,
                                 stack, sizeof(stack), &got)) got = 0;
    ResumeThread(game);
    if (ok) {
      wgl_sample_add((ULONG_PTR)context.Eip);
      wgl_sample_callers((ULONG_PTR)context.Eip, stack, got / sizeof(DWORD));
    }
#else
    ResumeThread(game);
    if (ok) wgl_sample_add((ULONG_PTR)context.Pc);
#endif
    if (ok && ++samples % AGX_WGL_SAMPLE_REPORT == 0) wgl_sample_report();
  }
}

static void wgl_sampler_start(void) {
  HMODULE self = NULL;
  HANDLE game = NULL;
  char enabled[4];
  /* EXP1174: 500 suspensions a second cost the emulated game ~20% of its
   * frame rate, so sampling needs APPLE_AGX_ICD_SAMPLE=1 besides the trace. */
  if (AgxWglSampler || !AdmissionUmdDiagnosticEnabled() ||
      GetEnvironmentVariableA("APPLE_AGX_ICD_SAMPLE", enabled, sizeof(enabled)) != 1u ||
      enabled[0] != '1') return;
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                       &game, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0))
    return;
  /* Like the present worker, the sampler never exits and pins the ICD. */
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                          (LPCWSTR)(void *)&wgl_sampler_main, &self)) {
    CloseHandle(game);
    return;
  }
  AgxWglSampler = CreateThread(NULL, 0, wgl_sampler_main, game, 0, NULL);
  if (!AgxWglSampler) { FreeLibrary(self); CloseHandle(game); return; }
  SetThreadPriority(AgxWglSampler, THREAD_PRIORITY_TIME_CRITICAL);
}

/* G2 HW3: present through the kernel. Each window has a present source: one
 * linear A8R8G8B8 GPU-local allocation of the back buffer's size, described
 * like DWM's presentation surfaces (AdmissionUmdDescribePrimary) and adopted
 * as a direct Mesa BO (as the D3D10 glue's attach_presentation_render_
 * resource). A present blits the back buffer into it on the GPU and flushes.
 *
 * G2 HW3b: EXP1218 showed that dxgkrnl refuses the legacy windowed blt
 * (D3DKMTPresent with hWindow, Microsoft's OpenGL ICD example) while DWM
 * owns the VidPN source (STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE). Under DWM an
 * ICD presents through opengl32's redirection protocol (gldrv.h, all of it
 * offered here per measure-wgl-callbacks): the ICD calls PresentBuffers;
 * opengl32 calls the ICD back (DrvPresentBuffers) with the window's shared
 * redirection surface and a present token; the ICD opens that surface on its
 * device and calls PresentToRedirectionSurface with its kernel context, the
 * source, the opened destination, the shared handle and the token; opengl32
 * submits the blt (DxgkDdiPresent on the GL context: the KMD's GPU present
 * job, after the blit -- one kernel context, in order) and tells DWM. No CPU
 * copy. A present that does not complete this way is shown through the ring
 * instead, and after AGX_WGL_KMT_REFUSALS such presents in a row the window
 * keeps the ring until its size changes. */
#define AGX_WGL_KMT_SOURCES 4u
/* EXP1220: PresentBuffers returned TRUE without calling DrvPresentBuffers
 * back for the first eight frames; the protocol may engage only once DWM
 * has made the window's surface, so a window keeps trying for four seconds
 * of frames before it keeps the ring. */
#define AGX_WGL_KMT_REFUSALS 240u
#define AGX_WGL_KMT_PENDING 8u
/* Where a redirected present stopped (measure-wgl-redirect). */
enum {
  AGX_WGL_REDIRECT_SHOWN,         /* PresentToRedirectionSurface accepted it */
  AGX_WGL_REDIRECT_REFUSED,       /* PresentBuffers FALSE, no callback */
  AGX_WGL_REDIRECT_NO_CALLBACK,   /* PresentBuffers TRUE, no callback */
  AGX_WGL_REDIRECT_NO_SURFACE,    /* callback without a surface */
  AGX_WGL_REDIRECT_OPEN,          /* the surface did not open (NTSTATUS) */
  AGX_WGL_REDIRECT_DESCRIPTION,   /* not a description this driver made */
  AGX_WGL_REDIRECT_ADOPT,         /* adopting or importing it failed */
  AGX_WGL_REDIRECT_PRESENT,       /* PresentToRedirectionSurface FALSE */
  AGX_WGL_REDIRECT_RETURN,        /* PresentBuffers FALSE after the present */
  AGX_WGL_REDIRECT_STAGES
};
/* One present in flight through opengl32 (pPrivData of PresentBuffers). The
 * requests are never freed, so a callback for an old request is still
 * recognised as this ICD's and never reaches stw as a resource pointer. */
#define AGX_WGL_REDIRECT_MAGIC 0x52445741u /* "AWDR" */
typedef struct {
  UINT Magic;
  BOOL Active;            /* PresentBuffers is running for it */
  DWORD Thread;           /* the presenting thread */
  void *Source;           /* its AGX_WGL_KMT_SOURCE */
  RECT Rect;
  BOOL Called;            /* DrvPresentBuffers ran for it */
  UINT Stage;
  LONG Status;
} AGX_WGL_REDIRECT_REQUEST;
static AGX_WGL_REDIRECT_REQUEST AgxWglRedirectRequests[AGX_WGL_KMT_SOURCES];
typedef struct {
  HWND Window;
  unsigned Width, Height;          /* the back buffer's */
  D3DKMT_HANDLE Allocation;
  AGX_WIN32_SCREEN_BUFFER Buffer;
  struct pipe_resource *Image;
  unsigned Failures;               /* consecutive refused presents */
  BOOL Refused;
  /* G2 HW3b: the window's redirection surface, opened on this device. */
  AGX_KMT_SHARED_SURFACE Surface;
  ADMISSION_ALLOCATION_DESCRIPTION Destination;
  AGX_WIN32_SCREEN_BUFFER DestinationBuffer;
  struct pipe_resource *DestinationImage;
  UINT RedirectVersion;            /* PresentToRedirectionSurface nVersion */
  UINT DestinationStage;           /* wgl_redirect_open's result for Surface.Shared */
  LONG DestinationStatus;
  /* EXP1221: PresentBuffers attempts, the variant kept (1-4, 0 none), and per
   * variant the calls answered by a callback, TRUE without one, FALSE. */
  UINT PbAttempts, PbVariant, PbCalled[4], PbTrue[4], PbFalse[4];
  UINT Stages[AGX_WGL_REDIRECT_STAGES], LastStage, LastStatus, Async;
  /* Receipt sums (measure-wgl-kmtpresent), microseconds after scaling. */
  LONGLONG Frames, LastTick, SumPresent, SumInterval, SumCopy, SumKernel;
  unsigned Shown, Failed, Empty, LastFailure;
} AGX_WGL_KMT_SOURCE;
static AGX_WGL_KMT_SOURCE AgxWglKmtSources[AGX_WGL_KMT_SOURCES];
/* Released sources and destinations whose BO the winsys still holds (as
 * collect_presentations): own allocations are deallocated, opened shared
 * resources destroyed, once the winsys has dropped them. */
typedef struct { D3DKMT_HANDLE Allocation, Resource; } AGX_WGL_KMT_PENDING_ENTRY;
static AGX_WGL_KMT_PENDING_ENTRY AgxWglKmtPending[AGX_WGL_KMT_PENDING];

static void wgl_kmt_deallocate(D3DKMT_HANDLE allocation) {
  D3DDDICB_DEALLOCATE release;
  ZeroMemory(&release, sizeof(release));
  release.NumAllocations = 1u;
  release.HandleList = &allocation;
  (void)AgxWgl->Bridge.DeviceCallbacks.pfnDeallocateCb(&AgxWgl->Bridge, &release);
}

static void wgl_kmt_collect(void) {
  for (unsigned i = 0; i < AGX_WGL_KMT_PENDING; ++i) {
    AGX_WGL_KMT_PENDING_ENTRY *e = &AgxWglKmtPending[i];
    if (!e->Allocation ||
        AdmissionUmdScreenAllocationRegistered(&AgxWgl->Device, e->Allocation)) continue;
    if (e->Resource) {
      AGX_KMT_SHARED_SURFACE opened;
      ZeroMemory(&opened, sizeof(opened));
      opened.Resource = e->Resource;
      (void)AgxKmtGpuvaCloseShared(&AgxWgl->Bridge, &opened);
    } else {
      wgl_kmt_deallocate(e->Allocation);
    }
    ZeroMemory(e, sizeof(*e));
  }
}

static void wgl_kmt_pend(D3DKMT_HANDLE allocation, D3DKMT_HANDLE resource) {
  unsigned i = 0;
  if (!allocation) return;
  while (i < AGX_WGL_KMT_PENDING && AgxWglKmtPending[i].Allocation) ++i;
  if (i < AGX_WGL_KMT_PENDING) {
    AgxWglKmtPending[i].Allocation = allocation;
    AgxWglKmtPending[i].Resource = resource;
  } else {
    wgl_note("reject-wgl-kmt-release", E_OUTOFMEMORY);   /* kept: never freed under the GPU */
  }
}

/* Drop the opened redirection surface (a new one or a released window). */
static void wgl_redirect_close(AGX_WGL_KMT_SOURCE *s) {
  if (s->DestinationImage) AgxWin32AsahiResourceRelease(&s->DestinationImage);
  if (s->Surface.Resource) {
    if (s->DestinationBuffer.Transport.Token)
      wgl_kmt_pend(s->Surface.Allocation, s->Surface.Resource);
    else
      (void)AgxKmtGpuvaCloseShared(&AgxWgl->Bridge, &s->Surface);
  }
  ZeroMemory(&s->Surface, sizeof(s->Surface));
  ZeroMemory(&s->Destination, sizeof(s->Destination));
  ZeroMemory(&s->DestinationBuffer, sizeof(s->DestinationBuffer));
}

static void wgl_kmt_release(AGX_WGL_KMT_SOURCE *s) {
  wgl_redirect_close(s);
  if (s->Image) AgxWin32AsahiResourceRelease(&s->Image);
  wgl_kmt_pend(s->Allocation, 0);
  ZeroMemory(s, sizeof(*s));
  wgl_kmt_collect();
}

static BOOL wgl_kmt_create(AGX_WGL_KMT_SOURCE *s, HWND window, unsigned width,
                           unsigned height) {
  ADMISSION_ALLOCATION_DESCRIPTION description;
  D3DDDI_ALLOCATIONINFO info;
  D3DDDICB_ALLOCATE allocate;
  HRESULT result;
  if (!AdmissionAllocationDescribe(width, height, 4u, (UINT)D3DKMDT_GDISURFACE_TEXTURE,
                                   (UINT)D3DDDIFMT_A8R8G8B8, 0u, &description)) {
    wgl_note("reject-wgl-kmt-describe", E_INVALIDARG);
    return FALSE;
  }
  description.Size = (description.Size + 127ULL) & ~127ULL;
  ZeroMemory(&info, sizeof(info));
  ZeroMemory(&allocate, sizeof(allocate));
  info.pPrivateDriverData = &description;
  info.PrivateDriverDataSize = sizeof(description);
  allocate.NumAllocations = 1u;
  allocate.pAllocationInfo = &info;
  result = AgxWgl->Bridge.DeviceCallbacks.pfnAllocateCb(&AgxWgl->Bridge, &allocate);
  if (FAILED(result) || !info.hAllocation) {
    wgl_note("reject-wgl-kmt-allocate", FAILED(result) ? result : E_FAIL);
    return FALSE;
  }
  s->Allocation = info.hAllocation;
  result = AdmissionUmdScreenAdoptAllocation(&AgxWgl->Device, info.hAllocation,
      description.Size, 0x10000ULL, AgxWin32BufferClassGeneral,
      AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferCpuWrite |
          AppleAgxWin32BufferGpuRead | AppleAgxWin32BufferGpuWrite,
      FALSE, TRUE, &s->Buffer);
  if (FAILED(result)) {
    wgl_note("reject-wgl-kmt-adopt", result);
    wgl_kmt_deallocate(s->Allocation);
    ZeroMemory(s, sizeof(*s));
    return FALSE;
  }
  s->Image = AgxWin32AsahiImportLinearColor32(AgxWgl->Screen, &s->Buffer,
      description.Width, description.Height, description.Pitch, description.Size,
      AgxWin32AsahiLinearFormatBgra8Unorm);
  if (!s->Image) {
    wgl_note("reject-wgl-kmt-import", E_FAIL);
    (void)AgxWin32ScreenDestroyBuffer(&AgxWgl->Device.Screen, &s->Buffer);
    wgl_kmt_deallocate(s->Allocation);
    ZeroMemory(s, sizeof(*s));
    return FALSE;
  }
  s->Window = window;
  s->Width = width;
  s->Height = height;
  return TRUE;
}

/* The window's source for a back buffer of this size; NULL keeps the ring. */
static AGX_WGL_KMT_SOURCE *wgl_kmt_get(HWND window, unsigned width, unsigned height) {
  AGX_WGL_KMT_SOURCE *free_source = NULL;
  wgl_kmt_collect();
  for (unsigned i = 0; i < AGX_WGL_KMT_SOURCES; ++i) {
    AGX_WGL_KMT_SOURCE *s = &AgxWglKmtSources[i];
    if (s->Window == window) {
      if (s->Width == width && s->Height == height)
        return s->Refused || !s->Image ? NULL : s;
      wgl_kmt_release(s);
    }
    if (!s->Window && !free_source) free_source = s;
  }
  if (!free_source) {
    free_source = &AgxWglKmtSources[0];
    wgl_kmt_release(free_source);
  }
  if (!wgl_kmt_create(free_source, window, width, height)) {
    /* Remember the refusal for this size: no allocation each frame. */
    free_source->Window = window;
    free_source->Width = width;
    free_source->Height = height;
    free_source->Refused = TRUE;
    return NULL;
  }
  return free_source;
}

/* G2 HW3b: open the window's redirection surface (opengl32's hSurface) on
 * this device and adopt it as a direct BO, so it has a GPU VA in this
 * process for the KMD's present job. Returns the stage that failed or
 * AGX_WGL_REDIRECT_SHOWN; *status carries the step's status. */
static UINT wgl_redirect_open(AGX_WGL_KMT_SOURCE *s, HANDLE shared, LONG *status) {
  ADMISSION_ALLOCATION_DESCRIPTION d;
  AGX_WIN32_ASAHI_LINEAR_FORMAT format;
  HRESULT result;
  wgl_redirect_close(s);
  *status = (LONG)AgxKmtGpuvaOpenShared(&AgxWgl->Bridge, shared, &s->Surface);
  {
    /* receipt: the surface as it opened */
    const ADMISSION_ALLOCATION_DESCRIPTION *r =
        (const ADMISSION_ALLOCATION_DESCRIPTION *)s->Surface.AllocationPrivate;
    UINT values[16] = {(UINT)(UINT_PTR)shared, (UINT)*status, s->Surface.Kind,
        s->Surface.NumAllocations, s->Surface.RuntimePrivateBytes,
        s->Surface.ResourcePrivateBytes, s->Surface.AllocationPrivateBytes,
        r->Magic, r->Type, r->Format, r->Width, r->Height, r->Pitch, r->CpuVisible,
        (UINT)s->Surface.GpuVirtualAddress, (UINT)(s->Surface.GpuVirtualAddress >> 32)};
    AdmissionUmdDiagnostic("measure-wgl-redirect-surface", S_OK, values, 16u);
  }
  if (*status < 0) { ZeroMemory(&s->Surface, sizeof(s->Surface)); return AGX_WGL_REDIRECT_OPEN; }
  memcpy(&d, s->Surface.AllocationPrivate, sizeof(d));
  if (s->Surface.AllocationPrivateBytes != sizeof(d) || !AdmissionAllocationDescriptionValid(&d) ||
      d.BytesPerPixel != 4u ||
      (d.Format != (UINT)D3DDDIFMT_A8R8G8B8 && d.Format != (UINT)D3DDDIFMT_A8B8G8R8)) {
    *status = (LONG)d.Format;
    return AGX_WGL_REDIRECT_DESCRIPTION;
  }
  s->Destination = d;
  /* A CPU-visible surface lives in the aperture: no direct BO; its VA is
   * whatever dxgkrnl gives the present (the KMD receipt records it). */
  if (d.CpuVisible) return AGX_WGL_REDIRECT_SHOWN;
  result = AdmissionUmdScreenAdoptAllocation(&AgxWgl->Device, s->Surface.Allocation, d.Size,
      0x10000ULL, AgxWin32BufferClassGeneral,
      AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferCpuWrite |
          AppleAgxWin32BufferGpuRead | AppleAgxWin32BufferGpuWrite,
      FALSE, TRUE, &s->DestinationBuffer);
  if (FAILED(result)) {
    *status = (LONG)result;
    ZeroMemory(&s->DestinationBuffer, sizeof(s->DestinationBuffer));
    return AGX_WGL_REDIRECT_ADOPT;
  }
  format = d.Format == (UINT)D3DDDIFMT_A8R8G8B8 ? AgxWin32AsahiLinearFormatBgra8Unorm
                                               : AgxWin32AsahiLinearFormatRgba8Unorm;
  s->DestinationImage = AgxWin32AsahiImportLinearColor32(AgxWgl->Screen, &s->DestinationBuffer,
      d.Width, d.Height, d.Pitch, d.Size, format);
  if (!s->DestinationImage) {
    *status = (LONG)E_FAIL;
    (void)AgxWin32ScreenDestroyBuffer(&AgxWgl->Device.Screen, &s->DestinationBuffer);
    ZeroMemory(&s->DestinationBuffer, sizeof(s->DestinationBuffer));
    return AGX_WGL_REDIRECT_ADOPT;
  }
  return AGX_WGL_REDIRECT_SHOWN;
}

/* opengl32's DrvPresentBuffers callback for a present started by
 * wgl_redirect_present (agx_wgl_glthread.c routes it here first): open the
 * surface if it changed and hand the blt to PresentToRedirectionSurface.
 * Runs on the presenting thread, which holds AgxWglRingLock. Returns -1 for
 * a call that is not this ICD's, else the BOOL for opengl32. */
/* EXP1221 receipt-only: every DrvPresentBuffers call opengl32 makes (the
 * first 16 one by one: sequence, ours, during its PresentBuffers, on the
 * presenting thread, hSurface, token low and high, microseconds since that
 * PresentBuffers began, pPrivData low; then counts in measure-wgl-redirect). */
static volatile LONG AgxWglDpbCalls, AgxWglDpbForeign;
static LONGLONG AgxWglDpbStartQpc;

extern "C" int agx_wgl_redirect_buffers(HDC hdc, LPPRESENTBUFFERS data) {
  AGX_WGL_REDIRECT_REQUEST *request = data ? (AGX_WGL_REDIRECT_REQUEST *)data->pPrivData : NULL;
  BOOL ours = request && request >= &AgxWglRedirectRequests[0] &&
      request < &AgxWglRedirectRequests[AGX_WGL_KMT_SOURCES] &&
      request->Magic == AGX_WGL_REDIRECT_MAGIC;
  LONG seq = InterlockedIncrement(&AgxWglDpbCalls);
  if (!ours) InterlockedIncrement(&AgxWglDpbForeign);
  if (seq <= 16) {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    UINT values[9] = {(UINT)seq, ours ? 1u : 0u, ours && request->Active ? 1u : 0u,
        ours && request->Thread == GetCurrentThreadId() ? 1u : 0u,
        data ? (UINT)(UINT_PTR)data->hSurface : 0u,
        data ? (UINT)data->ullPresentToken : 0u, data ? (UINT)(data->ullPresentToken >> 32) : 0u,
        frequency.QuadPart && AgxWglDpbStartQpc ?
            (UINT)((now.QuadPart - AgxWglDpbStartQpc) * 1000000 / frequency.QuadPart) : 0u,
        data ? (UINT)(UINT_PTR)data->pPrivData : 0u};
    AdmissionUmdDiagnostic("measure-wgl-dpb", S_OK, values, 9u);
  }
  if (!ours) return -1;
  AGX_WGL_KMT_SOURCE *s = (AGX_WGL_KMT_SOURCE *)request->Source;
  /* Only during its PresentBuffers call and on its thread: a late or
   * foreign-thread callback must not touch the source. */
  if (!request->Active || request->Thread != GetCurrentThreadId() || !s || !AgxWgl) {
    if (s) InterlockedIncrement((volatile LONG *)&s->Async);
    return 0;
  }
  request->Called = TRUE;
  if (!data->hSurface) { request->Stage = AGX_WGL_REDIRECT_NO_SURFACE; return 0; }
  if (s->Surface.Shared != data->hSurface) {
    LONG status = 0;
    s->DestinationStage = wgl_redirect_open(s, data->hSurface, &status);
    s->DestinationStatus = status;
  }
  /* A surface that opened but cannot be used is not reopened each frame;
   * one that did not open is retried (Surface.Shared stays 0). */
  if (s->DestinationStage != AGX_WGL_REDIRECT_SHOWN || !s->Surface.Resource) {
    request->Stage = s->DestinationStage != AGX_WGL_REDIRECT_SHOWN ? s->DestinationStage
                                                                    : AGX_WGL_REDIRECT_OPEN;
    request->Status = s->DestinationStatus;
    return 0;
  }
#if WINVER >= 0xA00
  RECT rect = request->Rect;
  if (rect.right > (LONG)s->Destination.Width) rect.right = (LONG)s->Destination.Width;
  if (rect.bottom > (LONG)s->Destination.Height) rect.bottom = (LONG)s->Destination.Height;
  PRESENTTOREDIRECTIONSURFACECB args;
  ZeroMemory(&args, sizeof(args));
  args.hContext = (UINT)(UINT_PTR)AgxWgl->Device.KernelContext;
  args.hSource = s->Allocation;
  args.hDestination = s->Surface.Allocation;
  args.hSharedHandle = data->hSurface;
  args.updateId = data->ullPresentToken;
  args.updateRect = rect;
  /* gldrv.h gives the structure, not its version: the first that opengl32
   * accepts is kept (measure-wgl-redirect records it). */
  static const UINT versions[] = {1u, 2u};
  BOOL ok = FALSE;
  for (unsigned i = 0; i < 2u && !ok; ++i) {
    if (s->RedirectVersion && versions[i] != s->RedirectVersion) continue;
    args.nVersion = versions[i];
    SetLastError(0);
    ok = stw_dev->callbacks.pfnPresentToRedirectionSurface(hdc, &args);
    if (ok) s->RedirectVersion = versions[i];
  }
  if (!ok) {
    request->Stage = AGX_WGL_REDIRECT_PRESENT;
    request->Status = (LONG)GetLastError();
    return 0;
  }
  request->Stage = AGX_WGL_REDIRECT_SHOWN;
  return 1;
#else
  request->Stage = AGX_WGL_REDIRECT_PRESENT;
  return 0;
#endif
}

/* EXP1222 receipt-only: the modules PresentBuffers loads into the process
 * (EXP1221: nVersion 2 returned TRUE without a callback and left
 * ERROR_MOD_NOT_FOUND). The module list before the first call is kept; after
 * each of the first 32 calls every module not in it is reported once
 * (measure-wgl-pb-module_<file>: attempt, variant, GetLastError). */
#define AGX_WGL_PB_MODULES 256u
static HMODULE AgxWglPbModules[AGX_WGL_PB_MODULES];
static DWORD AgxWglPbModuleCount;
static UINT AgxWglPbModuleCalls;

static void wgl_pb_modules(UINT attempt, UINT variant, DWORD error) {
  HMODULE now[AGX_WGL_PB_MODULES];
  DWORD bytes = 0;
  if (AgxWglPbModuleCalls > 32u) return;
  if (!K32EnumProcessModules(GetCurrentProcess(), now, sizeof(now), &bytes)) return;
  DWORD count = bytes / sizeof(HMODULE);
  if (count > AGX_WGL_PB_MODULES) count = AGX_WGL_PB_MODULES;
  if (AgxWglPbModuleCalls++ == 0u) {
    memcpy(AgxWglPbModules, now, count * sizeof(HMODULE));
    AgxWglPbModuleCount = count;
    UINT values[2] = {count, 0u};
    AdmissionUmdDiagnostic("measure-wgl-pb-modules", S_OK, values, 2u);
    return;
  }
  for (DWORD i = 0; i < count; ++i) {
    DWORD k = 0;
    while (k < AgxWglPbModuleCount && AgxWglPbModules[k] != now[i]) ++k;
    if (k < AgxWglPbModuleCount) continue;
    char path[MAX_PATH], stage[96];
    if (!GetModuleFileNameA(now[i], path, MAX_PATH)) continue;
    const char *slash = strrchr(path, '\\');
    if (_snprintf_s(stage, sizeof(stage), _TRUNCATE, "measure-wgl-pb-module_%s",
                    slash ? slash + 1 : path) < 0) continue;
    UINT values[3] = {attempt, variant, (UINT)error};
    AdmissionUmdDiagnostic(stage, S_OK, values, 3u);
    if (AgxWglPbModuleCount < AGX_WGL_PB_MODULES) AgxWglPbModules[AgxWglPbModuleCount++] = now[i];
  }
}

/* One present through opengl32: PresentBuffers with this source's request;
 * opengl32 calls agx_wgl_redirect_buffers back. Returns the stage. */
static UINT wgl_redirect_present(AGX_WGL_KMT_SOURCE *s, HWND window, const RECT *rect,
                                 LONG *status) {
  AGX_WGL_REDIRECT_REQUEST *request =
      &AgxWglRedirectRequests[s - AgxWglKmtSources];
  PRESENTBUFFERSCB cb;
  HDC dc = GetDC(window);
  BOOL ok;
  *status = 0;
  if (!dc) { *status = (LONG)GetLastError(); return AGX_WGL_REDIRECT_REFUSED; }
  ZeroMemory(request, sizeof(*request));
  request->Magic = AGX_WGL_REDIRECT_MAGIC;
  request->Source = s;
  request->Rect = *rect;
  request->Thread = GetCurrentThreadId();
  request->Active = TRUE;
  ZeroMemory(&cb, sizeof(cb));
  /* EXP1221 measurement of the undocumented half: until a callback comes,
   * attempts 1-8 use nVersion 2 without sync (Mesa stw's call), 9-16 with
   * vsync, 17-24 nVersion 1, 25-32 nVersion 3; then nVersion 2. A variant
   * that is called back is kept. EXP1221: nVersion 3 is called back (with
   * no surface); nVersion 2 returns TRUE without a callback. */
  static const UINT variants[4][2] = {{2u, PRESCB_SYNCTYPE_NONE}, {2u, PRESCB_SYNCTYPE_VSYNC},
                                      {1u, PRESCB_SYNCTYPE_NONE}, {3u, PRESCB_SYNCTYPE_NONE}};
  UINT variant = s->PbVariant ? s->PbVariant - 1u :
      s->PbAttempts < 32u ? s->PbAttempts / 8u : 0u;
  ++s->PbAttempts;
  cb.nVersion = variants[variant][0];
  cb.syncType = variants[variant][1];   /* G3 paces the swaps otherwise */
  cb.luidAdapter = AgxWgl->Luid;
  cb.pPrivData = request;
  cb.updateRect = *rect;
  if (!AgxWglPbModuleCalls) wgl_pb_modules(0u, variant, 0u);   /* the baseline, before the first call */
  SetLastError(0);
  {
    LARGE_INTEGER t; QueryPerformanceCounter(&t); AgxWglDpbStartQpc = t.QuadPart;
  }
  ok = stw_dev->callbacks.pfnPresentBuffers(dc, &cb);
  if (!request->Called) *status = (LONG)GetLastError();
  request->Active = FALSE;
  wgl_pb_modules(s->PbAttempts, variant, (DWORD)*status);
  if (request->Called) {
    ++s->PbCalled[variant];
    if (!s->PbVariant) s->PbVariant = variant + 1u;
  } else {
    ++(ok ? s->PbTrue : s->PbFalse)[variant];
  }
  ReleaseDC(window, dc);
  if (!request->Called) return ok ? AGX_WGL_REDIRECT_NO_CALLBACK : AGX_WGL_REDIRECT_REFUSED;
  if (request->Stage != AGX_WGL_REDIRECT_SHOWN) { *status = request->Status; return request->Stage; }
  return ok ? AGX_WGL_REDIRECT_SHOWN : AGX_WGL_REDIRECT_RETURN;
}

/* Receipt (G2 HW3b), with measure-wgl-kmtpresent: presents per stage
 * (AGX_WGL_REDIRECT_*), last failing stage and status, async callbacks,
 * PresentToRedirectionSurface version, destination format, width, height,
 * CPU-visible, open kind, opened GPU VA low. */
static void wgl_redirect_receipt(AGX_WGL_KMT_SOURCE *s) {
  /* The UMD diagnostic takes at most 16 values (EXP1220 lost a 19-value line). */
  UINT values[16], n = 0;
  for (unsigned i = 0; i < AGX_WGL_REDIRECT_STAGES; ++i) values[n++] = s->Stages[i];
  values[n++] = s->LastStage; values[n++] = s->LastStatus; values[n++] = s->Async;
  values[n++] = s->RedirectVersion; values[n++] = s->Surface.Kind;
  values[n++] = (UINT)AgxWglDpbCalls; values[n++] = (UINT)AgxWglDpbForeign;
  AdmissionUmdDiagnostic("measure-wgl-redirect", S_OK, values, n);
  UINT dest[16] = {s->Destination.Format, s->Destination.Width, s->Destination.Height,
      s->Destination.CpuVisible, (UINT)s->Surface.GpuVirtualAddress,
      (UINT)(s->Surface.GpuVirtualAddress >> 32), s->DestinationStage, (UINT)s->DestinationStatus,
      s->PbAttempts, s->PbVariant, 0u, 0u, 0u, 0u, 0u, 0u};
  AdmissionUmdDiagnostic("measure-wgl-redirect-dest", S_OK, dest, 10u);
  UINT pb[12];
  for (unsigned i = 0; i < 4u; ++i) { pb[3 * i] = s->PbCalled[i]; pb[3 * i + 1] = s->PbTrue[i]; pb[3 * i + 2] = s->PbFalse[i]; }
  AdmissionUmdDiagnostic("measure-wgl-redirect-pb", S_OK, pb, 12u);
  ZeroMemory(s->Stages, sizeof(s->Stages));
  s->Async = 0;
}

/* Receipt (G2 HW3): every 120 kernel presents, in microseconds, the mean
 * present (blit, flush and D3DKMTPresent), interval, GPU blit submission and
 * D3DKMTPresent, the presented size, presents shown, refused and skipped
 * (empty client area), the last refusal status and refused-window state. */
static void wgl_kmt_present_receipt(AGX_WGL_KMT_SOURCE *s, unsigned width,
                                    unsigned height, BOOL now) {
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  if (!frequency.QuadPart || (!now && s->Frames % 120 != 0)) return;
  LONGLONG us = frequency.QuadPart, frames = s->Frames % 120 ? s->Frames % 120 : 120;
  UINT values[11] = {
    (UINT)(s->SumPresent * 1000000 / us / frames),
    (UINT)(s->SumInterval * 1000000 / us / frames),
    (UINT)(s->SumCopy * 1000000 / us / frames),
    (UINT)(s->SumKernel * 1000000 / us / frames),
    width, height, s->Shown, s->Failed, s->Empty, s->LastFailure,
    s->Refused ? 1u : 0u };
  AdmissionUmdDiagnostic("measure-wgl-kmtpresent", S_OK, values, 11u);
  wgl_redirect_receipt(s);
  s->SumPresent = s->SumInterval = s->SumCopy = s->SumKernel = 0;
  s->Shown = s->Failed = s->Empty = 0;
  if (!now) wgl_common_receipt(us);
}

/* TRUE: the frame was presented by the kernel (or there is nothing to show);
 * FALSE: the ring shows it. Called with AgxWglRingLock held. */
static BOOL wgl_kmt_present(struct pipe_context *ctx, struct pipe_resource *res,
                            HWND window, LONGLONG start) {
  static volatile LONG first;
  LARGE_INTEGER copied, end;
  RECT client;
#if WINVER >= 0xA00
  if (!AgxWgl || !stw_dev || !stw_dev->callbacks.pfnPresentBuffers ||
      !stw_dev->callbacks.pfnPresentToRedirectionSurface ||
      !AgxWgl->Device.KernelContext || !ctx->blit || !ctx->flush)
    return FALSE;
#else
  return FALSE;
#endif
  AGX_WGL_KMT_SOURCE *s = wgl_kmt_get(window, res->width0, res->height0);
  if (!s) return FALSE;
  if (s->LastTick) s->SumInterval += start - s->LastTick;
  s->LastTick = start;
  ++s->Frames;
  /* The KMD blits without stretching: present the part of the frame the
   * client area shows (a window being resized or minimized). */
  unsigned width = res->width0, height = res->height0;
  if (GetClientRect(window, &client)) {
    if (client.right < (LONG)width) width = client.right > 0 ? (unsigned)client.right : 0u;
    if (client.bottom < (LONG)height) height = client.bottom > 0 ? (unsigned)client.bottom : 0u;
  }
  if (!width || !height) {
    ++s->Empty;
    QueryPerformanceCounter(&end);
    s->SumPresent += end.QuadPart - start;
    wgl_kmt_present_receipt(s, width, height, FALSE);
    return TRUE;
  }
  struct pipe_blit_info blit;
  memset(&blit, 0, sizeof(blit));
  blit.dst.resource = s->Image;
  blit.dst.format = s->Image->format;
  blit.dst.box.width = (int)width;
  blit.dst.box.height = (int)height;
  blit.dst.box.depth = 1;
  blit.src.resource = res;
  blit.src.format = res->format;
  blit.src.box = blit.dst.box;
  blit.mask = PIPE_MASK_RGBA;
  blit.filter = PIPE_TEX_FILTER_NEAREST;
  ctx->blit(ctx, &blit);
  /* Submitted before the present: the kernel context runs them in order. */
  ctx->flush(ctx, NULL, 0);
  QueryPerformanceCounter(&copied);
  RECT rect = {0, 0, (LONG)width, (LONG)height};
  LONG status = 0;
  UINT stage = wgl_redirect_present(s, window, &rect, &status);
  QueryPerformanceCounter(&end);
  s->SumCopy += copied.QuadPart - start;
  s->SumKernel += end.QuadPart - copied.QuadPart;
  s->SumPresent += end.QuadPart - start;
  ++s->Stages[stage];
  if (stage == AGX_WGL_REDIRECT_SHOWN) {
    s->Failures = 0;
    ++s->Shown;
    if (InterlockedCompareExchange(&first, 1, 0) == 0) {
      UINT values[3] = {width, height, s->RedirectVersion};
      AdmissionUmdDiagnostic("measure-wgl-kmt-present-first", S_OK, values, 3u);
    }
    wgl_kmt_present_receipt(s, width, height, FALSE);
    return TRUE;
  }
  ++s->Failed;
  s->LastStage = stage;
  s->LastStatus = (UINT)status;
  s->LastFailure = (stage << 24) | ((UINT)status & 0xffffffu);
  if (s->Failed == 1u) {
    UINT values[2] = {stage, (UINT)status};
    AdmissionUmdDiagnostic("reject-wgl-kmt-present", E_FAIL, values, 2u);
  }
  if (++s->Failures >= AGX_WGL_KMT_REFUSALS) {
    /* Keep the source until the size changes; the ring shows from now on. */
    s->Refused = TRUE;
    wgl_kmt_present_receipt(s, width, height, TRUE);
  } else {
    wgl_kmt_present_receipt(s, width, height, FALSE);
  }
  return FALSE;
}

/* Present one frame to a window: queue its GPU copy into a ring slot and
 * hand the previous frame to the worker. Called by stw's GDI present (after
 * the frame finished: stw waited) and by the winsys framebuffer (EXP1191:
 * stw flushed without waiting; the GPU copy is ordered after the frame on
 * the same context and wgl_ring_sync waits for the shown slot's copy). */
extern "C" BOOL agx_wgl_present_window(struct pipe_screen *screen,
                                       struct pipe_context *ctx,
                                       struct pipe_resource *res, HWND window) {
  LARGE_INTEGER start, synced, end;
  unsigned width, height;
  BOOL bgra;
  if (!ctx || !res || !window) return FALSE;
  width = res->width0; height = res->height0;
  bgra = res->format == PIPE_FORMAT_B8G8R8A8_UNORM ||
         res->format == PIPE_FORMAT_B8G8R8X8_UNORM;
  if (!bgra && res->format != PIPE_FORMAT_R8G8B8A8_UNORM &&
      res->format != PIPE_FORMAT_R8G8B8X8_UNORM) {
    wgl_note("reject-wgl-present-format", E_NOTIMPL);
    return FALSE;
  }
  QueryPerformanceCounter(&start);
  AcquireSRWLockExclusive(&AgxWglRingLock);
  wgl_sampler_start();
  /* G2 HW3: the kernel presents; the ring shows what it refuses. */
  if (wgl_kmt_present(ctx, res, window, start.QuadPart)) {
    ReleaseSRWLockExclusive(&AgxWglRingLock);
    return TRUE;
  }
  AGX_WGL_PRESENT_RING *ring = wgl_ring_get(screen, window, res, bgra);
  /* EXP1204 diagnostic, only with the UMD trace and APPLE_AGX_PRESENT_SKIP=N
   * (2-9): copy and show every N-th frame only, to measure what the present
   * copy costs the game thread. Off by default. */
  static int skip = -1;
  if (skip < 0) {
    char value[4];
    skip = AdmissionUmdDiagnosticEnabled() &&
           GetEnvironmentVariableA("APPLE_AGX_PRESENT_SKIP", value, sizeof(value)) == 1u &&
           value[0] >= '2' && value[0] <= '9' ? value[0] - '0' : 0;
  }
  if (ring && skip && (ring->Frames + 1) % skip) {
    QueryPerformanceCounter(&end);
    wgl_present_receipt(ring, start.QuadPart, end.QuadPart);
    ReleaseSRWLockExclusive(&AgxWglRingLock);
    return TRUE;
  }
  if (!ring) {
    ReleaseSRWLockExclusive(&AgxWglRingLock);
    HDC dc = GetDC(window);
    if (!dc) return FALSE;
    wgl_show_now(ctx, res, dc, width, height, bgra);
    ReleaseDC(window, dc);
    return TRUE;
  }
  unsigned dropped = 0;
  AcquireSRWLockExclusive(&AgxWglWorkerLock);
  int copy = agx_wgl_slot_for_copy(ring->State, ring->Seq, AGX_WGL_RING_SLOTS,
                                   &dropped);
  ReleaseSRWLockExclusive(&AgxWglWorkerLock);
  if (copy >= 0) {
    struct pipe_box box;
    u_box_2d(0, 0, (int)width, (int)height, &box);
    ctx->resource_copy_region(ctx, ring->Staging[copy], 0, 0, 0, 0, res, 0, &box);
    ctx->flush(ctx, &ring->Fence[copy], 0);
  }
  AcquireSRWLockExclusive(&AgxWglWorkerLock);
  if (copy >= 0) {
    ring->State[copy] = AGX_WGL_SLOT_COPIED;
    ring->Seq[copy] = ring->Frames + 1;
  }
  ring->Dropped += dropped;
  int show = AgxWglJobRing ? -1 :
      agx_wgl_slot_to_show(ring->State, ring->Seq, AGX_WGL_RING_SLOTS, copy);
  ReleaseSRWLockExclusive(&AgxWglWorkerLock);
  /* EXP1195: a frame whose copy has not finished stays copied for a later
   * present (or is replaced by a newer frame); the game thread no longer
   * waits for the GPU here. A slot without a fence (faulted context) is
   * synced as before. */
  if (show >= 0 && ring->Fence[show] &&
      !screen->fence_finish(screen, NULL, ring->Fence[show], 0)) {
    show = -1;
    ++ring->NotReady;
  }
  QueryPerformanceCounter(&synced);
  LONGLONG sync_start = synced.QuadPart;
  if (show >= 0) {
    BOOL ready = wgl_ring_sync(ctx, ring, (unsigned)show);
    QueryPerformanceCounter(&synced);
    AcquireSRWLockExclusive(&AgxWglWorkerLock);
    if (ready) {
      ring->State[show] = AGX_WGL_SLOT_SHOWING;
      ring->Dropped += agx_wgl_drop_older(ring->State, ring->Seq,
                                          AGX_WGL_RING_SLOTS, show);
      AgxWglJobRing = ring;
      AgxWglJobSlot = (unsigned)show;
      WakeConditionVariable(&AgxWglWorkerWake);
    } else {
      ring->State[show] = AGX_WGL_SLOT_FREE;
      ++ring->Dropped;
    }
    ReleaseSRWLockExclusive(&AgxWglWorkerLock);
  }
  ring->SumSync += synced.QuadPart - sync_start;
  QueryPerformanceCounter(&end);
  wgl_present_receipt(ring, start.QuadPart, end.QuadPart);
  ReleaseSRWLockExclusive(&AgxWglRingLock);
  return TRUE;
}

/* G3: one vertical blank of the display the screen's DC showed
 * (D3DKMTWaitForVerticalBlankEvent); negative when it is unavailable. */
extern "C" LONG agx_wgl_wait_vblank(void) {
  if (!AgxWgl || !AgxWgl->Screen) return (LONG)0xc00000bbL;
  return (LONG)AgxKmtGpuvaWaitVerticalBlank(&AgxWgl->Bridge, AgxWgl->VidPnSourceId);
}

/* stw's GDI present (formats without a winsys framebuffer). */
static void wgl_present(struct pipe_screen *screen, struct pipe_context *ctx,
                        struct pipe_resource *res, HDC hdc) {
  HWND window = hdc ? WindowFromDC(hdc) : NULL;
  if (!ctx || !res || !hdc) return;
  if (window) {
    (void)agx_wgl_present_window(screen, ctx, res, window);
    return;
  }
  /* A DC without a window (memory DC): show synchronously. */
  if (res->format == PIPE_FORMAT_B8G8R8A8_UNORM || res->format == PIPE_FORMAT_B8G8R8X8_UNORM)
    wgl_show_now(ctx, res, hdc, res->width0, res->height0, TRUE);
  else if (res->format == PIPE_FORMAT_R8G8B8A8_UNORM || res->format == PIPE_FORMAT_R8G8B8X8_UNORM)
    wgl_show_now(ctx, res, hdc, res->width0, res->height0, FALSE);
}

static bool wgl_get_adapter_luid(struct pipe_screen *screen, HDC hdc, LUID *luid) {
  (void)screen; (void)hdc;
  if (!AgxWgl || !AgxWgl->Screen || !luid) return false;
  *luid = AgxWgl->Luid;
  return true;
}

static const char *wgl_get_name(void) { return "agx"; }


static const struct stw_winsys AgxWglWinsys = {
  &wgl_screen_create,
  &wgl_present,
  &wgl_get_adapter_luid,
  NULL, /* shared_surface_open */
  NULL, /* shared_surface_close */
  NULL, /* compose */
  &agx_wgl_create_framebuffer, /* EXP1191 */
  &wgl_get_name,
};

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  (void)instance;
  switch (reason) {
  case DLL_PROCESS_ATTACH:
    stw_init(&AgxWglWinsys);
    stw_init_thread();
    break;
  case DLL_THREAD_ATTACH:
    stw_init_thread();
    break;
  case DLL_THREAD_DETACH:
    stw_cleanup_thread();
    break;
  case DLL_PROCESS_DETACH:
    if (reserved == NULL) {
      /* FreeLibrary (only before a present worker started, which pins the
       * ICD): no copy callback is pending. */
      if (AgxWglCopyWork) CloseThreadpoolWork(AgxWglCopyWork);
      AgxWglCopyWork = NULL;
      stw_cleanup_thread();
      stw_cleanup();
    } else {
      /* Process exit: as Mesa's wgl.c, never clean up from a torn-down
       * process; the kernel releases the device with the process. */
      stw_dev = NULL;
    }
    break;
  }
  return TRUE;
}
