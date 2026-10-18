#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <stdio.h>

using Microsoft::WRL::ComPtr;

static void Stage(const char *name, HRESULT result) {
  printf("COMPOSITION_STAGE name=%s hr=0x%08lx\n", name, (ULONG)result);
  fflush(stdout);
}

// Creation-only discriminator. Never binds a visual, presents, changes a mode,
// or enables an alternative adapter. Run only in the active console session.
int wmain(void) {
  DWORD session = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || session == 0 ||
      session != WTSGetActiveConsoleSessionId()) {
    Stage("console-session", E_ACCESSDENIED);
    return 2;
  }
  printf("COMPOSITION_PROCESS pid=%lu session=%lu\n", GetCurrentProcessId(), session);
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
  printf("COMPOSITION_ADAPTER luid=%08lx:%08lx vendor=%08x device=%08x flags=%x\n",
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
  printf("COMPOSITION_OUTPUT attached=%u rect=%ld,%ld,%ld,%ld\n",
      (UINT)outputDesc.AttachedToDesktop, outputDesc.DesktopCoordinates.left,
      outputDesc.DesktopCoordinates.top, outputDesc.DesktopCoordinates.right,
      outputDesc.DesktopCoordinates.bottom);
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL obtained = static_cast<D3D_FEATURE_LEVEL>(0);
  hr = D3D11CreateDevice(selected.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, requested, ARRAYSIZE(requested),
      D3D11_SDK_VERSION, device.GetAddressOf(), &obtained, context.GetAddressOf());
  Stage("d3d11-device-fl10", hr);
  if (FAILED(hr)) return 7;
  printf("COMPOSITION_FEATURE_LEVEL value=0x%x\n", (UINT)obtained);
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
  hr = factory->CreateSwapChainForComposition(device.Get(), &desc, nullptr,
      swap.GetAddressOf());
  Stage("composition-flip-create", hr);
  if (FAILED(hr)) return 8;
  ComPtr<ID3D11Texture2D> buffer;
  hr = swap->GetBuffer(0, IID_PPV_ARGS(buffer.GetAddressOf()));
  Stage("backbuffer0", hr);
  if (FAILED(hr)) return 9;
  D3D11_TEXTURE2D_DESC texture = {};
  buffer->GetDesc(&texture);
  printf("COMPOSITION_BUFFER width=%u height=%u format=%u array=%u samples=%u bind=0x%x\n",
      texture.Width, texture.Height, (UINT)texture.Format, texture.ArraySize,
      texture.SampleDesc.Count, texture.BindFlags);
  fflush(stdout);
  if (texture.Width != desc.Width || texture.Height != desc.Height ||
      texture.Format != desc.Format || texture.SampleDesc.Count != 1) return 9;
  // COM resources are released in reverse ownership order. No rendering or Present.
  return 0;
}
