#define COBJMACROS
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d12.h>
#include <stdio.h>
#include <string.h>

#include "agx_d3d12_probe_cs.h"
#include "agx_d3d12_probe_cs_cb.h"
#include "agx_d3d12_probe_cs_table.h"

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

/* C5 (D3D12-C5-compute.md): a root signature with one root UAV (u0), a
 * compute pipeline (agx_d3d12_probe_cs.hlsl: dword i = i * 3 + 7), Dispatch
 * over a 64 KiB DEFAULT buffer, then a GPU copy to READBACK and a CPU compare. */
static ID3D12Resource *uav_buffer(ID3D12Device *device) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    ID3D12Resource *buffer = NULL;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 65536;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HRESULT hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL,
                                                      &IID_ID3D12Resource, (void **)&buffer);
    printf("C5 CreateCommittedResource uav hr=0x%08lx\n", (unsigned long)hr);
    return SUCCEEDED(hr) ? buffer : NULL;
}
static int compute_dispatch(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence) {
    D3D12_ROOT_PARAMETER parameter = {D3D12_ROOT_PARAMETER_TYPE_UAV};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12Resource *uav = uav_buffer(device);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    unsigned bad = 16384;
    UINT64 value = 0;
    parameter.Descriptor.ShaderRegister = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    HRESULT hr = uav && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    printf("C5 CreateRootSignature hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C5 CreateComputePipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
        barrier.Transition.pResource = uav;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 0, ID3D12Resource_GetGPUVirtualAddress(uav));
        ID3D12GraphicsCommandList_Dispatch(list, 256, 1, 1);
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, uav, 0, 65536);
        hr = ID3D12GraphicsCommandList_Close(list);
        printf("C5 record+close hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        hr = ID3D12CommandQueue_Signal(queue, fence, 4);
        for (int i = 0; i < 300 && (value = ID3D12Fence_GetCompletedValue(fence)) < 4; ++i) Sleep(10);
        printf("C5 execute+signal hr=0x%08lx completed %llu\n", (unsigned long)hr, (unsigned long long)value);
    }
    if (SUCCEEDED(hr) && value == 4) {
        D3D12_RANGE all = {0, 65536};
        UINT *dwords = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&dwords);
        if (SUCCEEDED(hr) && dwords) {
            bad = 0;
            for (unsigned i = 0; i < 16384; ++i) bad += dwords[i] != i * 3u + 7u;
            printf("C5 readback bad=%u first=%u,%u last=%u\n", bad, dwords[0], dwords[1], dwords[16383]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_C5: %s\n", SUCCEEDED(hr) && value == 4 && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (readback) ID3D12Resource_Release(readback);
    if (uav) ID3D12Resource_Release(uav);
    return !(SUCCEEDED(hr) && bad == 0);
}

/* C5b (D3D12-C5-compute.md): constant buffers from the root -- root
 * constants b1 {scale = 5 (SetComputeRoot32BitConstants), bias = 11
 * (SetComputeRoot32BitConstant)} and a root CBV b0 on an UPLOAD buffer
 * whose extra.y = 1000; agx_d3d12_probe_cs_cb.hlsl writes dword i =
 * 5i + 11 + 1000. */
static int compute_constants(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence) {
    D3D12_ROOT_PARAMETER parameters[3] = {{D3D12_ROOT_PARAMETER_TYPE_UAV}, {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS},
                                          {D3D12_ROOT_PARAMETER_TYPE_CBV}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {3, parameters};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12Resource *uav = uav_buffer(device);
    ID3D12Resource *cbv = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    UINT *dwords = NULL;
    unsigned bad = 16384;
    UINT64 value = 0;
    parameters[1].Constants.ShaderRegister = 1;
    parameters[1].Constants.Num32BitValues = 2;
    HRESULT hr = uav && cbv && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(cbv, 0, NULL, (void **)&dwords);
    if (SUCCEEDED(hr)) {
        dwords[0] = 0; dwords[1] = 1000; dwords[2] = 0; dwords[3] = 0;
        ID3D12Resource_Unmap(cbv, 0, NULL);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    printf("C5B CreateRootSignature hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_cb;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_cb);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C5B CreateComputePipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        UINT scale = 5;
        D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
        barrier.Transition.pResource = uav;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 0, ID3D12Resource_GetGPUVirtualAddress(uav));
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list, 1, 1, &scale, 0);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(list, 1, 11, 1);
        ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(list, 2, ID3D12Resource_GetGPUVirtualAddress(cbv));
        ID3D12GraphicsCommandList_Dispatch(list, 256, 1, 1);
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, uav, 0, 65536);
        hr = ID3D12GraphicsCommandList_Close(list);
        printf("C5B record+close hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        hr = ID3D12CommandQueue_Signal(queue, fence, 5);
        for (int i = 0; i < 300 && (value = ID3D12Fence_GetCompletedValue(fence)) < 5; ++i) Sleep(10);
        printf("C5B execute+signal hr=0x%08lx completed %llu\n", (unsigned long)hr, (unsigned long long)value);
    }
    if (SUCCEEDED(hr) && value == 5) {
        D3D12_RANGE all = {0, 65536};
        dwords = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&dwords);
        if (SUCCEEDED(hr) && dwords) {
            bad = 0;
            for (unsigned i = 0; i < 16384; ++i) bad += dwords[i] != i * 5u + 1011u;
            printf("C5B readback bad=%u first=%u,%u last=%u\n", bad, dwords[0], dwords[1], dwords[16383]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_C5B: %s\n", SUCCEEDED(hr) && value == 5 && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (readback) ID3D12Resource_Release(readback);
    if (cbv) ID3D12Resource_Release(cbv);
    if (uav) ID3D12Resource_Release(uav);
    return !(SUCCEEDED(hr) && bad == 0);
}

/* C5c (D3D12-C5-compute.md): a descriptor table {UAV u0 (raw view), CBV
 * b0}: both views written into a CPU descriptor heap, copied with
 * CopyDescriptorsSimple into a shader-visible heap that SetDescriptorHeaps
 * binds; agx_d3d12_probe_cs_table.hlsl writes dword i = 7i + 3 (k from the
 * CBV). */
static int compute_table(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence) {
    D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0},
                                        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND}};
    D3D12_ROOT_PARAMETER parameter = {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter};
    D3D12_DESCRIPTOR_HEAP_DESC cpu_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_DESCRIPTOR_HEAP_DESC gpu_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12DescriptorHeap *cpu_heap = NULL, *gpu_heap = NULL;
    ID3D12Resource *uav = uav_buffer(device);
    ID3D12Resource *cbv = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    UINT *dwords = NULL;
    unsigned bad = 16384;
    UINT64 value = 0;
    parameter.DescriptorTable.NumDescriptorRanges = 2;
    parameter.DescriptorTable.pDescriptorRanges = ranges;
    HRESULT hr = uav && cbv && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(cbv, 0, NULL, (void **)&dwords);
    if (SUCCEEDED(hr)) {
        dwords[0] = 7; dwords[1] = 3; dwords[2] = 0; dwords[3] = 0;
        ID3D12Resource_Unmap(cbv, 0, NULL);
        hr = ID3D12Device_CreateDescriptorHeap(device, &cpu_desc, &IID_ID3D12DescriptorHeap, (void **)&cpu_heap);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateDescriptorHeap(device, &gpu_desc, &IID_ID3D12DescriptorHeap, (void **)&gpu_heap);
    printf("C5C CreateDescriptorHeap x2 hr=0x%08lx\n", (unsigned long)hr);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = {0}, gpu_cpu_start = {0};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start = {0};
    if (SUCCEEDED(hr)) {
        const UINT step = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {DXGI_FORMAT_R32_TYPELESS, D3D12_UAV_DIMENSION_BUFFER};
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {0};
        D3D12_CPU_DESCRIPTOR_HANDLE second;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap, &cpu_start);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu_heap, &gpu_cpu_start);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(gpu_heap, &gpu_start);
        uav_desc.Buffer.NumElements = 16384;
        uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        ID3D12Device_CreateUnorderedAccessView(device, uav, NULL, &uav_desc, cpu_start);
        cbv_desc.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(cbv);
        cbv_desc.SizeInBytes = 256;
        second.ptr = cpu_start.ptr + step;
        ID3D12Device_CreateConstantBufferView(device, &cbv_desc, second);
        ID3D12Device_CopyDescriptorsSimple(device, 2, gpu_cpu_start, cpu_start, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        printf("C5C views step %u gpu 0x%llx\n", step, (unsigned long long)gpu_start.ptr);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    printf("C5C CreateRootSignature hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_table;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_table);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C5C CreateComputePipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
        barrier.Transition.pResource = uav;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &gpu_heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpu_start);
        ID3D12GraphicsCommandList_Dispatch(list, 256, 1, 1);
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, uav, 0, 65536);
        hr = ID3D12GraphicsCommandList_Close(list);
        printf("C5C record+close hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        hr = ID3D12CommandQueue_Signal(queue, fence, 6);
        for (int i = 0; i < 300 && (value = ID3D12Fence_GetCompletedValue(fence)) < 6; ++i) Sleep(10);
        printf("C5C execute+signal hr=0x%08lx completed %llu\n", (unsigned long)hr, (unsigned long long)value);
    }
    if (SUCCEEDED(hr) && value == 6) {
        D3D12_RANGE all = {0, 65536};
        dwords = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&dwords);
        if (SUCCEEDED(hr) && dwords) {
            bad = 0;
            for (unsigned i = 0; i < 16384; ++i) bad += dwords[i] != i * 7u + 3u;
            printf("C5C readback bad=%u first=%u,%u last=%u\n", bad, dwords[0], dwords[1], dwords[16383]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_C5C: %s\n", SUCCEEDED(hr) && value == 6 && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (gpu_heap) ID3D12DescriptorHeap_Release(gpu_heap);
    if (cpu_heap) ID3D12DescriptorHeap_Release(cpu_heap);
    if (readback) ID3D12Resource_Release(readback);
    if (cbv) ID3D12Resource_Release(cbv);
    if (uav) ID3D12Resource_Release(uav);
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
            if (SUCCEEDED(c4) && value == 2 && gpu_copy(device, queue, fence) == 0 &&
                compute_dispatch(device, queue, fence) == 0 && compute_constants(device, queue, fence) == 0)
                (void)compute_table(device, queue, fence);
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
