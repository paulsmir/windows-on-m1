#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <stdio.h>
#include <wchar.h>

using Microsoft::WRL::ComPtr;

static void Stage(const char *name, HRESULT result) {
  printf("FULLSCREEN_STAGE name=%s hr=0x%08lx\n", name, (ULONG)result);
  fflush(stdout);
}

// One public fullscreen workload. Its stages locate an exact refusal; a successful
// client Present does not establish that the desktop compositor works.
struct ProbeWindow {
  HWND Handle = nullptr;
  ~ProbeWindow() { if (Handle) DestroyWindow(Handle); }
};
struct RestoreFullscreen {
  IDXGISwapChain1 *Swap = nullptr;
  ~RestoreFullscreen() {
    if (Swap) Stage("restore-windowed", Swap->SetFullscreenState(FALSE, nullptr));
  }
};
static LRESULT CALLBACK ProbeWindowProc(HWND hwnd, UINT message,
                                       WPARAM wparam, LPARAM lparam) {
  return DefWindowProcW(hwnd, message, wparam, lparam);
}
int wmain(int argc, wchar_t **argv) {
  const bool windowed = argc == 2 && wcscmp(argv[1], L"--windowed") == 0;
  if (argc != 1 && !windowed) {
    fprintf(stderr, "Usage: FullscreenSdkPresentProbe.exe [--windowed]\n");
    return 2;
  }
  DWORD session = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || session == 0 ||
      session != WTSGetActiveConsoleSessionId()) {
    Stage("console-session", E_ACCESSDENIED);
    return 2;
  }
  printf("FULLSCREEN_PROCESS pid=%lu session=%lu\n", GetCurrentProcessId(), session);
  printf("PROBE_MODE windowed=%u\n", static_cast<UINT>(windowed));
  ComPtr<IDXGIFactory2> factory;
  HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
  Stage("factory2", hr);
  if (FAILED(hr)) return 3;
  ComPtr<IDXGIAdapter1> selected;
  DXGI_ADAPTER_DESC1 selectedDesc = {};
  UINT matches = 0;
  for (UINT index = 0; ; ++index) {
    ComPtr<IDXGIAdapter1> adapter;
    hr = factory->EnumAdapters1(index, adapter.GetAddressOf());
    if (hr == DXGI_ERROR_NOT_FOUND) break;
    if (FAILED(hr)) { Stage("enum-adapter", hr); return 4; }
    DXGI_ADAPTER_DESC1 desc = {};
    hr = adapter->GetDesc1(&desc);
    if (FAILED(hr)) { Stage("adapter-desc", hr); return 4; }
    // The APPL0002 ACPI adapter exposes these IDs, not a guessed PCI vendor.
    if (desc.VendorId == 0x4c505041u && desc.DeviceId == 0x32303030u &&
        (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
      selected = adapter;
      selectedDesc = desc;
      ++matches;
    }
  }
  if (matches != 1) { Stage("unique-apple-adapter", DXGI_ERROR_NOT_FOUND); return 5; }
  printf("FULLSCREEN_ADAPTER luid=%08lx:%08lx vendor=%08x device=%08x flags=%x\n",
      (ULONG)selectedDesc.AdapterLuid.HighPart, selectedDesc.AdapterLuid.LowPart,
      selectedDesc.VendorId, selectedDesc.DeviceId, selectedDesc.Flags);
  ComPtr<IDXGIOutput> output;
  hr = selected->EnumOutputs(0, output.GetAddressOf());
  Stage("output0", hr);
  if (FAILED(hr)) return 6;
  DXGI_OUTPUT_DESC outputDesc = {};
  hr = output->GetDesc(&outputDesc);
  Stage("output-desc", hr);
  if (FAILED(hr) || !outputDesc.AttachedToDesktop) return 6;
  printf("FULLSCREEN_OUTPUT attached=%u rect=%ld,%ld,%ld,%ld\n",
      (UINT)outputDesc.AttachedToDesktop, outputDesc.DesktopCoordinates.left,
      outputDesc.DesktopCoordinates.top, outputDesc.DesktopCoordinates.right,
      outputDesc.DesktopCoordinates.bottom);
  if (outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left != 2560 ||
      outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top != 1600) {
    Stage("exact-panel-extent", E_INVALIDARG); return 6;
  }
  WNDCLASSW wc = {};
  wc.lpfnWndProc = ProbeWindowProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"J313FullscreenSdkPresentProbe";
  if (!RegisterClassW(&wc)) {
    Stage("register-window", HRESULT_FROM_WIN32(GetLastError())); return 6;
  }
  ProbeWindow window;
  window.Handle = CreateWindowExW(0, wc.lpszClassName, L"J313 fullscreen SDK probe",
      WS_POPUP, outputDesc.DesktopCoordinates.left, outputDesc.DesktopCoordinates.top,
      2560, 1600, nullptr, nullptr, wc.hInstance, nullptr);
  if (!window.Handle) {
    Stage("create-window", HRESULT_FROM_WIN32(GetLastError())); return 6;
  }
  ShowWindow(window.Handle, SW_SHOW);
  UpdateWindow(window.Handle);
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL obtained = static_cast<D3D_FEATURE_LEVEL>(0);
  hr = D3D11CreateDevice(selected.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, requested, ARRAYSIZE(requested),
      D3D11_SDK_VERSION, device.GetAddressOf(), &obtained, context.GetAddressOf());
  Stage("d3d11-device-fl10", hr);
  if (FAILED(hr)) return 7;
  printf("FULLSCREEN_FEATURE_LEVEL value=0x%x\n", (UINT)obtained);
  if (obtained != D3D_FEATURE_LEVEL_10_0) return 7;
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = 2560;
  desc.Height = 1600;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  ComPtr<IDXGISwapChain1> swap;
  DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen = {};
  fullscreen.RefreshRate.Numerator = 0; // Microsoft: use the native display refresh.
  fullscreen.RefreshRate.Denominator = 1;
  fullscreen.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
  fullscreen.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
  fullscreen.Windowed = FALSE;
  hr = factory->CreateSwapChainForHwnd(device.Get(), window.Handle, &desc,
      windowed ? nullptr : &fullscreen, nullptr, swap.GetAddressOf());
  Stage(windowed ? "windowed-flip-create" : "fullscreen-flip-create", hr);
  RestoreFullscreen restore;
  restore.Swap = windowed ? nullptr : swap.Get();
  Stage("device-removed-after-create", device->GetDeviceRemovedReason());
  if (FAILED(hr)) return 8;
  BOOL isFullscreen = FALSE;
  ComPtr<IDXGIOutput> actualOutput;
  hr = swap->GetFullscreenState(&isFullscreen, actualOutput.GetAddressOf());
  Stage("fullscreen-state", hr);
  if (FAILED(hr) || (isFullscreen != FALSE) != !windowed) return 8;
  if (!windowed) {
    if (!actualOutput) return 8;
    DXGI_OUTPUT_DESC actualDesc = {};
    hr = actualOutput->GetDesc(&actualDesc);
    Stage("fullscreen-output", hr);
    if (FAILED(hr) || actualDesc.Monitor != outputDesc.Monitor) return 8;
  }
  ComPtr<ID3D11Texture2D> buffer;
  hr = swap->GetBuffer(0, IID_PPV_ARGS(buffer.GetAddressOf()));
  Stage("backbuffer0", hr);
  if (FAILED(hr)) return 9;
  D3D11_TEXTURE2D_DESC texture = {};
  buffer->GetDesc(&texture);
  printf("FULLSCREEN_BUFFER width=%u height=%u format=%u array=%u samples=%u bind=0x%x\n",
      texture.Width, texture.Height, (UINT)texture.Format, texture.ArraySize,
      texture.SampleDesc.Count, texture.BindFlags);
  fflush(stdout);
  if (texture.Width != desc.Width || texture.Height != desc.Height ||
      texture.Format != desc.Format || texture.SampleDesc.Count != 1) return 9;
  ComPtr<ID3D11RenderTargetView> view;
  hr = device->CreateRenderTargetView(buffer.Get(), nullptr, view.GetAddressOf());
  Stage("render-target-view", hr);
  if (FAILED(hr)) return 10;
  const FLOAT color[4] = {0.15f, 0.55f, 0.25f, 1.0f};
  context->ClearRenderTargetView(view.Get(), color);
  context->Flush();
  HRESULT removed = device->GetDeviceRemovedReason();
  Stage("device-removed-after-clear", removed);
  if (FAILED(removed)) return 10;
  DXGI_PRESENT_PARAMETERS parameters = {};
  hr = swap->Present1(1, 0, &parameters);
  Stage("present1-once", hr);
  removed = device->GetDeviceRemovedReason();
  Stage("device-removed-after-present", removed);
  if (FAILED(hr) || FAILED(removed)) return 11;
  // Brief message dispatch allows the single completed frame to be inspected.
  const ULONGLONG end = GetTickCount64() + 5000;
  while (GetTickCount64() < end) {
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message); DispatchMessageW(&message);
    }
    Sleep(10);
  }
  context->ClearState();
  view.Reset(); buffer.Reset();
  context->Flush();
  // RestoreFullscreen runs before swap destruction and before HWND destruction.
  return 0;
}
