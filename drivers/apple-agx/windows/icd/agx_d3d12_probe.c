#define COBJMACROS
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d12.h>
#include <stdio.h>

int main(void) {
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    ID3D12Device *device = NULL;
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
    if (FAILED(hr)) { printf("CreateDXGIFactory1 hr=0x%08lx\n", (unsigned long)hr); return 1; }
    for (UINT i = 0; IDXGIFactory1_EnumAdapters1(factory, i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        hr = IDXGIAdapter1_GetDesc1(adapter, &desc);
        if (SUCCEEDED(hr) && desc.VendorId == 0x4c505041) {
            hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
                                   &IID_ID3D12Device, (void **)&device);
            printf("D3D12CreateDevice hr=0x%08lx\n", (unsigned long)hr);
            if (device) ID3D12Device_Release(device);
            IDXGIAdapter1_Release(adapter);
            IDXGIFactory1_Release(factory);
            return FAILED(hr) ? 1 : 0;
        }
        IDXGIAdapter1_Release(adapter);
        adapter = NULL;
    }
    puts("AGX adapter not found");
    IDXGIFactory1_Release(factory);
    return 2;
}
