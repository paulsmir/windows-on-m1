/* CS 1.6 ICD plan, phase 2/4: the WGL target of the AGX OpenGL ICD.
 *
 * Replaces Mesa's targets/wgl/wgl.c (softpipe/llvmpipe) in libgallium_wgl:
 * stw_winsys::create_screen opens the AGX adapter behind the window's DC,
 * creates a D3DKMT device, drives the validated umd_* device/screen code
 * through the GPUVA D3DKMT bridge and returns the Windows Asahi pipe_screen;
 * present reads the finished colour buffer back and draws it into the DC
 * with GDI (DWM composes the window's redirection surface). One screen per
 * process, created on the first DC, as Mesa's stw device expects. */
#include <windows.h>
#include <string.h>
#include "agx_kmt_gpuva_bridge.h"
#include "agx_wgl_present_copy.h"
#include "agx_wgl_present_ring.h"
#include "agx_wgl_draw_hook.h"
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
static void wgl_perf_note(const char *message) {
  static volatile LONG records;
  char stage[96];
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
  a->Bridge.Receipt.ScanWaits = AdmissionUmdDiagnosticEnabled();
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
  AgxWin32PerfHook = wgl_perf_note;
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
 * the window. One ring per window DC, size and format. */
typedef struct {
  HDC Dc;
  HWND Window;
  unsigned Width, Height;
  enum pipe_format Format;
  BOOL Bgra;
  struct pipe_resource *Staging[AGX_WGL_RING_SLOTS];
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
  for (unsigned i = 0; i < AGX_WGL_RING_SLOTS; ++i)
    pipe_resource_reference(&ring->Staging[i], NULL);
  if (ring->Cpu) HeapFree(GetProcessHeap(), 0, ring->Cpu);
  ZeroMemory(ring, sizeof(*ring));
}

static AGX_WGL_PRESENT_RING *wgl_ring_get(struct pipe_screen *screen, HDC hdc,
                                          const struct pipe_resource *res,
                                          BOOL bgra) {
  AGX_WGL_PRESENT_RING *free_ring = NULL;
  for (unsigned i = 0; i < 4; ++i) {
    AGX_WGL_PRESENT_RING *ring = &AgxWglRings[i];
    if (ring->Staging[0] && ring->Dc == hdc) {
      if (ring->Width == res->width0 && ring->Height == res->height0 &&
          ring->Format == res->format) return ring;
      wgl_ring_release(ring);
    }
    if (!ring->Staging[0] && !free_ring) free_ring = ring;
  }
  HWND window = WindowFromDC(hdc);
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
  for (unsigned i = 0; i < AGX_WGL_RING_SLOTS; ++i) {
    free_ring->Staging[i] = screen->resource_create(screen, &templ);
    if (!free_ring->Staging[i]) { wgl_ring_release(free_ring); return NULL; }
  }
  free_ring->Cpu = (BYTE *)HeapAlloc(GetProcessHeap(), 0,
                                     (SIZE_T)res->width0 * res->height0 * 4u);
  if (!free_ring->Cpu) { wgl_ring_release(free_ring); return NULL; }
  free_ring->Dc = hdc;
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
   * the four return addresses (image RVAs), waits and microseconds. */
  for (unsigned i = 0; i < AGX_KMT_WAIT_SITES; ++i) {
    AGX_KMT_WAIT_SITE *s = &r->WaitSites[i];
    if (!s->Calls) continue;
    UINT site[7];
    for (unsigned k = 0; k < AGX_KMT_WAIT_DEPTH; ++k)
      site[k] = s->Return[k] ? (UINT)(s->Return[k] - (ULONG_PTR)&__ImageBase) : 0u;
    site[4] = s->Calls;
    site[5] = (UINT)(s->Ticks * 1000000 / us);
    site[6] = r->WaitSitesLost;
    AdmissionUmdDiagnostic("measure-wgl-wait", S_OK, site, 7u);
  }
  ZeroMemory(r->WaitSites, sizeof(r->WaitSites));
  r->WaitSitesLost = 0;
}

/* Receipt (EXP1168/EXP1171): every 120 presents, in microseconds, the mean
 * game-thread present, worker show per shown frame, present-to-present
 * interval, size, game-thread sync (map wait), worker copy and SetDIBits per
 * shown frame, copy slices, frames shown and dropped, and failed GetDC. */
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
  UINT values[12] = {
    (UINT)(ring->SumPresent * 1000000 / us / 120),
    (UINT)(ring->SumShow * 1000000 / us / shown),
    (UINT)(ring->SumInterval * 1000000 / us / 120),
    ring->Width, ring->Height,
    (UINT)(ring->SumSync * 1000000 / us / 120),
    (UINT)(ring->SumCopy * 1000000 / us / shown),
    (UINT)(ring->SumDib * 1000000 / us / shown),
    ring->Slices, ring->Shown, ring->Dropped, ring->DibFailed };
  ring->SumShow = ring->SumCopy = ring->SumDib = 0;
  ring->Shown = ring->Dropped = ring->DibFailed = 0;
  ReleaseSRWLockExclusive(&AgxWglWorkerLock);
  AdmissionUmdDiagnostic("measure-wgl-present", S_OK, values, 12u);
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
  ring->SumPresent = ring->SumInterval = ring->SumSync = 0;
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

static void wgl_present(struct pipe_screen *screen, struct pipe_context *ctx,
                        struct pipe_resource *res, HDC hdc) {
  LARGE_INTEGER start, synced, end;
  unsigned width, height;
  BOOL bgra;
  if (!ctx || !res || !hdc) return;
  width = res->width0; height = res->height0;
  bgra = res->format == PIPE_FORMAT_B8G8R8A8_UNORM ||
         res->format == PIPE_FORMAT_B8G8R8X8_UNORM;
  if (!bgra && res->format != PIPE_FORMAT_R8G8B8A8_UNORM &&
      res->format != PIPE_FORMAT_R8G8B8X8_UNORM) {
    wgl_note("reject-wgl-present-format", E_NOTIMPL);
    return;
  }
  QueryPerformanceCounter(&start);
  AcquireSRWLockExclusive(&AgxWglRingLock);
  wgl_sampler_start();
  AGX_WGL_PRESENT_RING *ring = wgl_ring_get(screen, hdc, res, bgra);
  if (!ring) {
    ReleaseSRWLockExclusive(&AgxWglRingLock);
    wgl_show_now(ctx, res, hdc, width, height, bgra);
    return;
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
    ctx->flush(ctx, NULL, 0);
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
  NULL, /* create_framebuffer */
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
