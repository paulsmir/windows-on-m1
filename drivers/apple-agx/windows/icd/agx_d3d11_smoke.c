/* EXP1225: a 32-bit D3D11 smoke test for the WOW64 user-mode driver
 * (AppleAgxRenderAdmissionUmd32.dll, UserModeDriverNameWow). It finds the
 * AGX adapter through DXGI, creates a hardware D3D11 device and a windowed
 * flip-model swap chain on it, clears the back buffer to a known colour each
 * frame and presents, then copies the back buffer to a staging texture and
 * checks one texel. Every step prints its HRESULT, so a refusal names the
 * step; exit 0 only when every frame presented and the texel matches.
 * EXP1231: "blt" as the second argument makes the swap chain blt-model
 * (DXGI_SWAP_EFFECT_DISCARD, one buffer): DXGI presents through DWM's
 * window redirection surface, the path opengl32 uses for GL windows.
 * Usage: agx_d3d11_smoke.exe [frames] [flip|blt] */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
  return DefWindowProcA(w, m, wp, lp);
}

#define STEP(name, hr) do { HRESULT h_ = (hr); printf("%s 0x%08lx\n", name, (unsigned long)h_); \
  if (FAILED(h_)) { failures++; goto done; } } while (0)

int main(int argc, char **argv) {
  int frames = argc > 1 ? atoi(argv[1]) : 120, failures = 0, presented = 0;
  int blt = argc > 2 && strcmp(argv[2], "blt") == 0;
  IDXGIFactory2 *factory = NULL; IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  ID3D11Device *device = NULL; ID3D11DeviceContext *ctx = NULL;
  IDXGISwapChain1 *chain = NULL; ID3D11Texture2D *back = NULL, *staging = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  D3D_FEATURE_LEVEL level = 0;
  WNDCLASSA wc; HWND wnd;
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "AgxD3d11Smoke";
  RegisterClassA(&wc);
  wnd = CreateWindowA("AgxD3d11Smoke", "AGX D3D11 smoke", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                      120, 120, 656, 519, NULL, NULL, wc.hInstance, NULL);
  if (!wnd) { printf("NO_WINDOW %lu\n", GetLastError()); return 2; }
  STEP("CreateDXGIFactory1", CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&factory));
  for (UINT i = 0; IDXGIFactory2_EnumAdapters1(factory, i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 d;
    IDXGIAdapter1_GetDesc1(adapter, &d);
    printf("ADAPTER %u vendor %04x device %04x flags %x luid %08lx:%08lx %ls\n", i, d.VendorId,
           d.DeviceId, d.Flags, (unsigned long)d.AdapterLuid.HighPart,
           (unsigned long)d.AdapterLuid.LowPart, d.Description);
    if (!pick && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { pick = adapter; continue; }
    IDXGIAdapter1_Release(adapter);
  }
  if (!pick) { printf("NO_HARDWARE_ADAPTER\n"); failures++; goto done; }
  {
    const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    STEP("D3D11CreateDevice", D3D11CreateDevice((IDXGIAdapter *)pick, D3D_DRIVER_TYPE_UNKNOWN, NULL,
         D3D11_CREATE_DEVICE_BGRA_SUPPORT, want, 3, D3D11_SDK_VERSION, &device, &level, &ctx));
    printf("FEATURE_LEVEL 0x%x\n", level);
  }
  {
    DXGI_SWAP_CHAIN_DESC1 sd;
    memset(&sd, 0, sizeof(sd));
    sd.Width = 640; sd.Height = 480; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = blt ? 1 : 2;
    sd.SwapEffect = blt ? DXGI_SWAP_EFFECT_DISCARD : DXGI_SWAP_EFFECT_FLIP_DISCARD;
    printf("SWAP_EFFECT %s\n", blt ? "blt" : "flip");
    STEP("CreateSwapChainForHwnd", IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)device,
         wnd, &sd, NULL, NULL, &chain));
  }
  for (int n = 0; n < frames; ++n) {
    MSG msg;
    const float colour[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    if (FAILED(IDXGISwapChain1_GetBuffer(chain, 0, &IID_ID3D11Texture2D, (void **)&back))) { failures++; break; }
    if (FAILED(ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)back, NULL, &rtv))) {
      printf("CreateRenderTargetView failed\n"); failures++; break;
    }
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, colour);
    if (n == frames - 1) {
      D3D11_TEXTURE2D_DESC td;
      D3D11_MAPPED_SUBRESOURCE map;
      ID3D11Texture2D_GetDesc(back, &td);
      td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      td.MiscFlags = 0;
      if (SUCCEEDED(ID3D11Device_CreateTexture2D(device, &td, NULL, &staging))) {
        ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)back);
        HRESULT h = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
        if (SUCCEEDED(h)) {
          unsigned px = *(const unsigned *)((const unsigned char *)map.pData + 100 * map.RowPitch + 100 * 4);
          printf("TEXEL 0x%08x want 0xff0000ff\n", px);
          if (px != 0xff0000ffu) failures++;
          ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
        } else { printf("Map 0x%08lx\n", (unsigned long)h); failures++; }
      } else { printf("staging CreateTexture2D failed\n"); failures++; }
    }
    ID3D11RenderTargetView_Release(rtv); rtv = NULL;
    ID3D11Texture2D_Release(back); back = NULL;
    HRESULT h = IDXGISwapChain1_Present(chain, 1, 0);
    if (FAILED(h)) { printf("Present 0x%08lx at %d\n", (unsigned long)h, n); failures++; break; }
    ++presented;
  }
  printf("PRESENTED %d of %d\n", presented, frames);
done:
  if (staging) ID3D11Texture2D_Release(staging);
  if (chain) IDXGISwapChain1_Release(chain);
  if (ctx) ID3D11DeviceContext_Release(ctx);
  if (device) ID3D11Device_Release(device);
  if (pick) IDXGIAdapter1_Release(pick);
  if (factory) IDXGIFactory2_Release(factory);
  DestroyWindow(wnd);
  printf(failures ? "AGX_D3D11_SMOKE: FAIL %d\n" : "AGX_D3D11_SMOKE: PASS\n", failures);
  return failures ? 1 : 0;
}
