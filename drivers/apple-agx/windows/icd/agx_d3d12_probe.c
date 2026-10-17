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

/* C4b (D3D12-C4-copy.md): UPLOAD -> DEFAULT -> READBACK through two GPU
 * CopyBufferRegion calls in one list, then a CPU compare. */
static ID3D12Resource *committed_buffer(ID3D12Device *device, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap = {type};
    D3D12_RESOURCE_DESC desc = {0};
    ID3D12Resource *buffer = NULL;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 65536;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HRESULT hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc, state, NULL,
                                                      &IID_ID3D12Resource, (void **)&buffer);
    printf("C4B CreateCommittedResource type %d hr=0x%08lx\n", (int)type, (unsigned long)hr);
    return SUCCEEDED(hr) ? buffer : NULL;
}
static int gpu_copy(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence) {
    ID3D12Resource *upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *gpu = committed_buffer(device, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    unsigned char *bytes = NULL;
    unsigned bad = 65536;
    UINT64 value = 0;
    HRESULT hr = upload && gpu && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        for (unsigned i = 0; i < 65536; ++i) bytes[i] = (unsigned char)(i * 13u + 5u);
        ID3D12Resource_Unmap(upload, 0, NULL);
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
        barrier.Transition.pResource = gpu;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ID3D12GraphicsCommandList_CopyBufferRegion(list, gpu, 0, upload, 0, 65536);
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, gpu, 0, 65536);
        hr = ID3D12GraphicsCommandList_Close(list);
        printf("C4B record+close hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        hr = ID3D12CommandQueue_Signal(queue, fence, 3);
        for (int i = 0; i < 300 && (value = ID3D12Fence_GetCompletedValue(fence)) < 3; ++i) Sleep(10);
        printf("C4B execute+signal hr=0x%08lx completed %llu\n", (unsigned long)hr, (unsigned long long)value);
    }
    if (SUCCEEDED(hr) && value == 3) {
        D3D12_RANGE all = {0, 65536};
        bytes = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            bad = 0;
            for (unsigned i = 0; i < 65536; ++i) bad += bytes[i] != (unsigned char)(i * 13u + 5u);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
        printf("C4B readback bad=%u first=%02x\n", bad, bytes ? 0 : 0);
    }
    printf("AGX_D3D12_C4B: %s\n", SUCCEEDED(hr) && value == 3 && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (readback) ID3D12Resource_Release(readback);
    if (gpu) ID3D12Resource_Release(gpu);
    if (upload) ID3D12Resource_Release(upload);
    return !(SUCCEEDED(hr) && bad == 0);
}

/* C3a (D3D12-C3-queue-fences.md): a direct queue, a fence and one Signal;
 * each step prints its HRESULT so the receipts name the DDIs the runtime
 * needs. */
static int queue_fence_signal(ID3D12Device *device) {
    D3D12_COMMAND_QUEUE_DESC desc = {D3D12_COMMAND_LIST_TYPE_DIRECT};
    ID3D12CommandQueue *queue = NULL;
    ID3D12Fence *fence = NULL;
    HRESULT hr = ID3D12Device_CreateCommandQueue(device, &desc, &IID_ID3D12CommandQueue, (void **)&queue);
    printf("C3 CreateCommandQueue hr=0x%08lx\n", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    printf("C3 CreateFence hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        hr = ID3D12CommandQueue_Signal(queue, fence, 1);
        printf("C3 Signal hr=0x%08lx\n", (unsigned long)hr);
        UINT64 value = 0;
        for (int i = 0; i < 200 && (value = ID3D12Fence_GetCompletedValue(fence)) < 1; ++i) Sleep(10);
        printf("C3 completed value %llu\n", (unsigned long long)value);
        printf("AGX_D3D12_C3: %s\n", SUCCEEDED(hr) && value == 1 ? "PASS" : "FAIL");
        if (SUCCEEDED(hr) && value == 1) {
            /* C4a: an empty direct command list executed on the queue, then a
             * fence signal ordered after it. */
            ID3D12CommandAllocator *allocator = NULL;
            ID3D12GraphicsCommandList *list = NULL;
            HRESULT c4 = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                            &IID_ID3D12CommandAllocator, (void **)&allocator);
            printf("C4 CreateCommandAllocator hr=0x%08lx\n", (unsigned long)c4);
            if (SUCCEEDED(c4)) {
                c4 = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                                                    &IID_ID3D12GraphicsCommandList, (void **)&list);
                printf("C4 CreateCommandList hr=0x%08lx\n", (unsigned long)c4);
            }
            if (SUCCEEDED(c4)) {
                c4 = ID3D12GraphicsCommandList_Close(list);
                printf("C4 Close hr=0x%08lx\n", (unsigned long)c4);
            }
            if (SUCCEEDED(c4)) {
                ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
                ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
                c4 = ID3D12CommandQueue_Signal(queue, fence, 2);
                for (int i = 0; i < 200 && (value = ID3D12Fence_GetCompletedValue(fence)) < 2; ++i) Sleep(10);
                printf("C4 execute+signal hr=0x%08lx completed %llu\n", (unsigned long)c4, (unsigned long long)value);
            }
            printf("AGX_D3D12_C4A: %s\n", SUCCEEDED(c4) && value == 2 ? "PASS" : "FAIL");
            if (SUCCEEDED(c4) && value == 2) (void)gpu_copy(device, queue, fence);
            if (list) ID3D12GraphicsCommandList_Release(list);
            if (allocator) ID3D12CommandAllocator_Release(allocator);
        }
        ID3D12Fence_Release(fence);
    }
    ID3D12CommandQueue_Release(queue);
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
                failed |= queue_fence_signal(device);
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
