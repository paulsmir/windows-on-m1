#define COBJMACROS
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d12.h>
#include <stdio.h>
#include <string.h>

/* C2 checkpoint (D3D12-C-implementation-plan.md): a committed upload buffer
 * and a Map/write/Unmap/Map/read round trip. Runs only after the device
 * exists; each step prints its HRESULT so the receipt names the first one
 * the frontend refuses. */
static int committed_buffer_round_trip(ID3D12Device *device) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC desc = {0};
    ID3D12Resource *buffer = NULL;
    D3D12_RANGE none = {0, 0};
    unsigned char *bytes = NULL;
    HRESULT hr;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 65536;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                              &IID_ID3D12Resource, (void **)&buffer);
    printf("C2 CreateCommittedResource upload 65536 hr=0x%08lx\n", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    hr = ID3D12Resource_Map(buffer, 0, &none, (void **)&bytes);
    printf("C2 Map hr=0x%08lx ptr=%p\n", (unsigned long)hr, (void *)bytes);
    if (SUCCEEDED(hr) && bytes) {
        for (unsigned i = 0; i < 65536; ++i) bytes[i] = (unsigned char)(i * 7u + 3u);
        ID3D12Resource_Unmap(buffer, 0, NULL);
        bytes = NULL;
        hr = ID3D12Resource_Map(buffer, 0, NULL, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            unsigned bad = 0;
            for (unsigned i = 0; i < 65536; ++i) bad += bytes[i] != (unsigned char)(i * 7u + 3u);
            printf("C2 round trip bad=%u gpuva=0x%llx\n", bad,
                   (unsigned long long)ID3D12Resource_GetGPUVirtualAddress(buffer));
            ID3D12Resource_Unmap(buffer, 0, &none);
            hr = bad ? E_FAIL : S_OK;
        }
    }
    ID3D12Resource_Release(buffer);
    printf("AGX_D3D12_C2: %s\n", SUCCEEDED(hr) ? "PASS" : "FAIL");
    return FAILED(hr);
}

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
            int failed;
            hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
                                   &IID_ID3D12Device, (void **)&device);
            printf("D3D12CreateDevice hr=0x%08lx\n", (unsigned long)hr);
            failed = FAILED(hr);
            if (device) {
                failed |= committed_buffer_round_trip(device);
                ID3D12Device_Release(device);
            }
            IDXGIAdapter1_Release(adapter);
            IDXGIFactory1_Release(factory);
            return failed;
        }
        IDXGIAdapter1_Release(adapter);
        adapter = NULL;
    }
    puts("AGX adapter not found");
    IDXGIFactory1_Release(factory);
    return 2;
}
