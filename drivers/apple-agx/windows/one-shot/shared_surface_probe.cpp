#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
static void Stage(const char *name, HRESULT hr) {
  printf("SHARED_STAGE name=%s hr=0x%08lx pid=%lu\n", name, (ULONG)hr,
         GetCurrentProcessId());
  fflush(stdout);
}
static HRESULT Device(ComPtr<ID3D11Device> &device,
                      ComPtr<ID3D11DeviceContext> &context) {
  ComPtr<IDXGIFactory1> factory;
  HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
  if (FAILED(hr))
    return hr;
  ComPtr<IDXGIAdapter1> selected;
  UINT matches = 0;
  for (UINT i = 0;; ++i) {
    ComPtr<IDXGIAdapter1> a;
    hr = factory->EnumAdapters1(i, a.GetAddressOf());
    if (hr == DXGI_ERROR_NOT_FOUND)
      break;
    if (FAILED(hr))
      return hr;
    DXGI_ADAPTER_DESC1 d = {};
    hr = a->GetDesc1(&d);
    if (FAILED(hr))
      return hr;
    if (d.VendorId == 0x4c505041u && d.DeviceId == 0x32303030u &&
        !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
      selected = a;
      ++matches;
    }
  }
  if (matches != 1)
    return DXGI_ERROR_NOT_FOUND;
  D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_10_0, obtained = {};
  hr = D3D11CreateDevice(selected.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                         D3D11_CREATE_DEVICE_BGRA_SUPPORT, &requested, 1,
                         D3D11_SDK_VERSION, device.GetAddressOf(), &obtained,
                         context.GetAddressOf());
  return SUCCEEDED(hr) && obtained != requested ? E_FAIL : hr;
}
static HRESULT Complete(ID3D11Device *device, ID3D11DeviceContext *context) {
  D3D11_QUERY_DESC desc = {D3D11_QUERY_EVENT, 0};
  ComPtr<ID3D11Query> query;
  HRESULT hr = device->CreateQuery(&desc, query.GetAddressOf());
  Stage("event-create", hr);
  if (FAILED(hr))
    return hr;
  context->End(query.Get());
  context->Flush();
  BOOL done = FALSE;
  const ULONGLONG end = GetTickCount64() + 2000;
  do {
    hr = context->GetData(query.Get(), &done, sizeof(done),
                          D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (hr != S_FALSE)
      break;
    Sleep(1);
  } while (GetTickCount64() < end);
  if (hr == S_FALSE)
    return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
  return hr == S_OK && done ? S_OK : FAILED(hr) ? hr : E_FAIL;
}
static int Consume(HANDLE handle) {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  HRESULT hr = Device(device, context);
  Stage("consumer-device", hr);
  if (FAILED(hr))
    return 20;
  ComPtr<ID3D11Texture2D> shared;
  hr = device->OpenSharedResource(handle, IID_PPV_ARGS(shared.GetAddressOf()));
  Stage("consumer-open", hr);
  if (FAILED(hr))
    return 21;
  D3D11_TEXTURE2D_DESC desc = {};
  shared->GetDesc(&desc);
  printf("SHARED_DESC width=%u height=%u format=%u usage=%u misc=0x%x\n",
         desc.Width, desc.Height, (UINT)desc.Format, (UINT)desc.Usage,
         desc.MiscFlags);
  fflush(stdout);
  if (desc.Width != 2560 || desc.Height != 1600 ||
      desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.MipLevels != 1 ||
      desc.ArraySize != 1 || desc.SampleDesc.Count != 1)
    return 22;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  hr = device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
  Stage("consumer-staging", hr);
  if (FAILED(hr))
    return 23;
  context->CopyResource(staging.Get(), shared.Get());
  context->Flush();
  hr = device->GetDeviceRemovedReason();
  Stage("consumer-after-copy", hr);
  if (FAILED(hr))
    return 24;
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  Stage("consumer-map", hr);
  if (FAILED(hr))
    return 25;
  uint64_t mismatches = 0;
  uint32_t first = 0, last = 0;
  bool valid = mapped.pData && mapped.RowPitch >= desc.Width * 4u;
  if (valid) {
    for (UINT y = 0; y < desc.Height; ++y) {
      const unsigned char *row =
          (const unsigned char *)mapped.pData + (size_t)y * mapped.RowPitch;
      for (UINT x = 0; x < desc.Width; ++x) {
        uint32_t pixel;
        memcpy(&pixel, row + (size_t)x * 4u, 4);
        if (!x && !y)
          first = pixel;
        last = pixel;
        if (pixel != 0xff00ff00u)
          ++mismatches;
      }
    }
  }
  context->Unmap(staging.Get(), 0);
  hr = device->GetDeviceRemovedReason();
  Stage("consumer-device-final", hr);
  printf("SHARED_PIXELS valid=%u checked=%llu mismatches=%llu first=%08x "
         "last=%08x row_pitch=%u\n",
         (UINT)valid,
         valid ? (unsigned long long)desc.Width * desc.Height : 0ULL,
         (unsigned long long)mismatches, first, last, mapped.RowPitch);
  fflush(stdout);
  return valid && !mismatches && SUCCEEDED(hr) ? 0 : 26;
}
int wmain(int argc, wchar_t **argv) {
  DWORD session = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || session != 1 ||
      session != WTSGetActiveConsoleSessionId())
    return 2;
  if (argc == 3 && wcscmp(argv[1], L"--consume") == 0) {
    wchar_t *end = nullptr;
    unsigned long long value = wcstoull(argv[2], &end, 16);
    if (!value || !end || *end)
      return 3;
    return Consume((HANDLE)(uintptr_t)value);
  }
  if (argc != 1)
    return 3;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  HRESULT hr = Device(device, context);
  Stage("producer-device", hr);
  if (FAILED(hr))
    return 4;
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = 2560;
  desc.Height = 1600;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
  ComPtr<ID3D11Texture2D> texture;
  hr = device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf());
  Stage("producer-shared-texture", hr);
  if (FAILED(hr))
    return 5;
  ComPtr<ID3D11RenderTargetView> view;
  hr = device->CreateRenderTargetView(texture.Get(), nullptr,
                                      view.GetAddressOf());
  Stage("producer-rtv", hr);
  if (FAILED(hr))
    return 6;
  const FLOAT green[4] = {0, 1, 0, 1};
  context->ClearRenderTargetView(view.Get(), green);
  hr = Complete(device.Get(), context.Get());
  Stage("producer-complete", hr);
  if (FAILED(hr) || FAILED(device->GetDeviceRemovedReason()))
    return 7;
  ComPtr<IDXGIResource> resource;
  hr = texture.As(&resource);
  HANDLE shared = nullptr;
  if (SUCCEEDED(hr))
    hr = resource->GetSharedHandle(&shared);
  Stage("producer-shared-handle", hr);
  if (FAILED(hr) || !shared)
    return 8;
  wchar_t path[MAX_PATH] = {}, command[MAX_PATH + 80] = {};
  DWORD chars = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (!chars || chars >= MAX_PATH)
    return 9;
  if (swprintf_s(command, L"\"%s\" --consume %llx", path,
                 (unsigned long long)(uintptr_t)shared) < 0)
    return 9;
  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION child = {};
  if (!CreateProcessW(nullptr, command, nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &child)) {
    Stage("consumer-create", HRESULT_FROM_WIN32(GetLastError()));
    return 10;
  }
  printf("SHARED_CHILD pid=%lu\n", child.dwProcessId);
  fflush(stdout);
  DWORD wait = WaitForSingleObject(child.hProcess, 15000);
  DWORD exit = 27;
  if (wait == WAIT_OBJECT_0) {
    if (!GetExitCodeProcess(child.hProcess, &exit))
      exit = 28;
  } else {
    TerminateProcess(child.hProcess, 29);
    WaitForSingleObject(child.hProcess, 5000);
    exit = 29;
  }
  CloseHandle(child.hThread);
  CloseHandle(child.hProcess);
  printf("SHARED_CONSUMER_EXIT value=%lu\n", exit);
  fflush(stdout);
  return (int)exit;
}
