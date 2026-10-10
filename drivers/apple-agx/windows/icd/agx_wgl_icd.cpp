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

static void wgl_vdm_trace(uint64_t va, const uint32_t *words, unsigned count,
                          unsigned draws) {
  static volatile LONG records;
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
    }
  }
  a->Failure = result;
  wgl_note(SUCCEEDED(result) ? "wgl-screen-create" : "reject-wgl-screen-create", result);
  /* A failed adapter stays recorded (no second attempt with half-owned
   * kernel objects); stw reports the missing screen to the application. */
  return a->Screen;
}

/* Copy the finished colour buffer into the window: map it for read (the
 * winsys flushes, waits for the job and downloads its staging copy), then
 * SetDIBitsToDevice with a top-down 32-bit BGRX DIB. */
static void wgl_present(struct pipe_screen *screen, struct pipe_context *ctx,
                        struct pipe_resource *res, HDC hdc) {
  struct pipe_transfer *transfer = NULL;
  struct pipe_box box;
  BITMAPINFO info;
  const BYTE *map;
  unsigned width, height;
  BOOL bgra;
  (void)screen;
  if (!ctx || !res || !hdc) return;
  width = res->width0; height = res->height0;
  bgra = res->format == PIPE_FORMAT_B8G8R8A8_UNORM ||
         res->format == PIPE_FORMAT_B8G8R8X8_UNORM;
  if (!bgra && res->format != PIPE_FORMAT_R8G8B8A8_UNORM &&
      res->format != PIPE_FORMAT_R8G8B8X8_UNORM) {
    wgl_note("reject-wgl-present-format", E_NOTIMPL);
    return;
  }
  u_box_2d(0, 0, (int)width, (int)height, &box);
  map = (const BYTE *)ctx->texture_map(ctx, res, 0, PIPE_MAP_READ, &box, &transfer);
  if (!map || !transfer) {
    wgl_note("reject-wgl-present-map", E_FAIL);
    return;
  }
  ZeroMemory(&info, sizeof(info));
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = (LONG)width;
  info.bmiHeader.biHeight = -(LONG)height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  if (bgra && transfer->stride == width * 4u) {
    SetDIBitsToDevice(hdc, 0, 0, width, height, 0, 0, 0, height, map, &info,
                      DIB_RGB_COLORS);
  } else {
    /* Pitch or channel order differs from a packed BGRX DIB: repack. */
    BYTE *packed = (BYTE *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)width * height * 4u);
    if (packed) {
      for (unsigned y = 0; y < height; ++y) {
        const BYTE *src = map + (SIZE_T)y * transfer->stride;
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
  }
  ctx->texture_unmap(ctx, transfer);
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
