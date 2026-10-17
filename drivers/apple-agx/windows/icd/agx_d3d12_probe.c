#define COBJMACROS
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d12.h>
#include <stdio.h>
#include <string.h>

#include "agx_d3d12_probe_cs.h"
#include "agx_d3d12_probe_cs_cb.h"
#include "agx_d3d12_probe_cs_table.h"
#include "agx_d3d12_probe_cs_srv.h"
#include "agx_d3d12_probe_vs.h"
#include "agx_d3d12_probe_ps.h"
#include "agx_d3d12_probe_vs_root.h"
#include "agx_d3d12_probe_ps_root.h"
#include "agx_d3d12_probe_vs_tex.h"
#include "agx_d3d12_probe_ps_tex.h"
#include "agx_d3d12_probe_vs_depth.h"

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

/* C5d (D3D12-C5-compute.md): buffer SRVs -- a root SRV t0 (raw, A[i] = 3i)
 * and a table {SRV t1, structured uint, B[i] = i + 100} written directly
 * into the shader-visible heap; agx_d3d12_probe_cs_srv.hlsl writes
 * A[i] + 2 B[i] = 5i + 200. */
static int compute_srv(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence) {
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0};
    D3D12_ROOT_PARAMETER parameters[3] = {{D3D12_ROOT_PARAMETER_TYPE_UAV}, {D3D12_ROOT_PARAMETER_TYPE_SRV},
                                          {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {3, parameters};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12DescriptorHeap *heap = NULL;
    ID3D12Resource *uav = uav_buffer(device);
    ID3D12Resource *a = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *b = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    UINT *dwords = NULL;
    unsigned bad = 16384;
    UINT64 value = 0;
    parameters[1].Descriptor.ShaderRegister = 0;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &range;
    HRESULT hr = uav && a && b && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(a, 0, NULL, (void **)&dwords);
    if (SUCCEEDED(hr)) {
        for (unsigned i = 0; i < 16384; ++i) dwords[i] = 3u * i;
        ID3D12Resource_Unmap(a, 0, NULL);
        hr = ID3D12Resource_Map(b, 0, NULL, (void **)&dwords);
    }
    if (SUCCEEDED(hr)) {
        for (unsigned i = 0; i < 16384; ++i) dwords[i] = i + 100u;
        ID3D12Resource_Unmap(b, 0, NULL);
        hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start = {0};
    if (SUCCEEDED(hr)) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {DXGI_FORMAT_UNKNOWN, D3D12_SRV_DIMENSION_BUFFER,
                                                    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_start;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &cpu_start);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &gpu_start);
        srv_desc.Buffer.NumElements = 16384;
        srv_desc.Buffer.StructureByteStride = 4;
        ID3D12Device_CreateShaderResourceView(device, b, &srv_desc, cpu_start);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    printf("C5D CreateRootSignature hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_srv;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_srv);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C5D CreateComputePipelineState hr=0x%08lx\n", (unsigned long)hr);
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
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 0, ID3D12Resource_GetGPUVirtualAddress(uav));
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(list, 1, ID3D12Resource_GetGPUVirtualAddress(a));
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 2, gpu_start);
        ID3D12GraphicsCommandList_Dispatch(list, 256, 1, 1);
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, uav, 0, 65536);
        hr = ID3D12GraphicsCommandList_Close(list);
        printf("C5D record+close hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        hr = ID3D12CommandQueue_Signal(queue, fence, 7);
        for (int i = 0; i < 300 && (value = ID3D12Fence_GetCompletedValue(fence)) < 7; ++i) Sleep(10);
        printf("C5D execute+signal hr=0x%08lx completed %llu\n", (unsigned long)hr, (unsigned long long)value);
    }
    if (SUCCEEDED(hr) && value == 7) {
        D3D12_RANGE all = {0, 65536};
        dwords = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&dwords);
        if (SUCCEEDED(hr) && dwords) {
            bad = 0;
            for (unsigned i = 0; i < 16384; ++i) bad += dwords[i] != i * 5u + 200u;
            printf("C5D readback bad=%u first=%u,%u last=%u\n", bad, dwords[0], dwords[1], dwords[16383]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_C5D: %s\n", SUCCEEDED(hr) && value == 7 && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (heap) ID3D12DescriptorHeap_Release(heap);
    if (readback) ID3D12Resource_Release(readback);
    if (b) ID3D12Resource_Release(b);
    if (a) ID3D12Resource_Release(a);
    if (uav) ID3D12Resource_Release(uav);
    return !(SUCCEEDED(hr) && bad == 0);
}

/* C6a (D3D12-C6-graphics.md): a 64x64 RGBA8 render-target texture cleared
 * through an RTV and read back with CopyTextureRegion; then an UPLOAD
 * footprint copied into the texture and read back again. Signals base + 1
 * and base + 2 (it runs whatever the compute steps returned). */
static HRESULT run_list(ID3D12CommandQueue *queue, ID3D12Fence *fence, ID3D12GraphicsCommandList *list, UINT64 signal,
                        const char *step) {
    UINT64 value = 0;
    HRESULT hr = ID3D12GraphicsCommandList_Close(list);
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        hr = ID3D12CommandQueue_Signal(queue, fence, signal);
        for (int i = 0; i < 300 && (value = ID3D12Fence_GetCompletedValue(fence)) < signal; ++i) Sleep(10);
    }
    printf("%s close+execute+signal hr=0x%08lx completed %llu\n", step, (unsigned long)hr, (unsigned long long)value);
    return SUCCEEDED(hr) && value == signal ? S_OK : E_FAIL;
}
static void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
}
static int texture_clear_copy(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    ID3D12Resource *texture = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    ID3D12Resource *upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0};
    D3D12_TEXTURE_COPY_LOCATION tex_loc = {0}, buf_loc = {0};
    unsigned char *bytes = NULL;
    unsigned bad_clear = 4096, bad_copy = 4096;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HRESULT hr = upload && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
                                                  (void **)&texture);
    printf("C6A CreateCommittedResource texture hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    if (SUCCEEDED(hr)) {
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
        ID3D12Device_CreateRenderTargetView(device, texture, NULL, rtv);
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    printf("C6A RTV + list hr=0x%08lx\n", (unsigned long)hr);
    tex_loc.pResource = texture;
    tex_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    buf_loc.pResource = readback;
    buf_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    buf_loc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    buf_loc.PlacedFootprint.Footprint.Width = 64;
    buf_loc.PlacedFootprint.Footprint.Height = 64;
    buf_loc.PlacedFootprint.Footprint.Depth = 1;
    buf_loc.PlacedFootprint.Footprint.RowPitch = 256;
    if (SUCCEEDED(hr)) {
        const FLOAT color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, color, 0, NULL);
        transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &buf_loc, 0, 0, 0, &tex_loc, NULL);
        hr = run_list(queue, fence, list, base + 1, "C6A clear");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            bad_clear = 0;
            for (unsigned i = 0; i < 4096; ++i)
                bad_clear += bytes[4 * i] != 64 || bytes[4 * i + 1] != 128 || bytes[4 * i + 2] != 191 || bytes[4 * i + 3] != 255;
            printf("C6A clear readback bad=%u first=%u,%u,%u,%u\n", bad_clear, bytes[0], bytes[1], bytes[2], bytes[3]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_C6A_CLEAR: %s\n", SUCCEEDED(hr) && bad_clear == 0 ? "PASS" : "FAIL");
    if (SUCCEEDED(hr) && bad_clear == 0) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr) && bad_clear == 0) {
        for (unsigned i = 0; i < 16384; ++i) bytes[i] = (unsigned char)(i * 7u + 1u);
        ID3D12Resource_Unmap(upload, 0, NULL);
        hr = ID3D12CommandAllocator_Reset(allocator);
        if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Reset(list, allocator, NULL);
        if (SUCCEEDED(hr)) {
            D3D12_TEXTURE_COPY_LOCATION up_loc = buf_loc;
            up_loc.pResource = upload;
            transition(list, texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12GraphicsCommandList_CopyTextureRegion(list, &tex_loc, 0, 0, 0, &up_loc, NULL);
            transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ID3D12GraphicsCommandList_CopyTextureRegion(list, &buf_loc, 0, 0, 0, &tex_loc, NULL);
            hr = run_list(queue, fence, list, base + 2, "C6A copy");
        }
        if (SUCCEEDED(hr)) {
            D3D12_RANGE all = {0, 16384};
            hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
            if (SUCCEEDED(hr) && bytes) {
                bad_copy = 0;
                for (unsigned i = 0; i < 16384; ++i) bad_copy += bytes[i] != (unsigned char)(i * 7u + 1u);
                printf("C6A copy readback bad=%u first=%u,%u\n", bad_copy, bytes[0], bytes[1]);
                ID3D12Resource_Unmap(readback, 0, NULL);
            }
        }
        printf("AGX_D3D12_C6A_COPY: %s\n", SUCCEEDED(hr) && bad_copy == 0 ? "PASS" : "FAIL");
    }
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (texture) ID3D12Resource_Release(texture);
    if (readback) ID3D12Resource_Release(readback);
    if (upload) ID3D12Resource_Release(upload);
    return !(SUCCEEDED(hr) && bad_clear == 0 && bad_copy == 0);
}

/* C6b (D3D12-C6-graphics.md): the first draw -- a green triangle
 * (agx_d3d12_probe_tri.hlsl, POSITION float2 + COLOR float4 from an UPLOAD
 * vertex buffer) on a red-cleared 64x64 RGBA8 target, empty root signature;
 * PASS = centre pixel green, corner pixel red, green coverage within the
 * triangle's area bounds. Signals base + 1. */
/* rooted = 1 (C6c): agx_d3d12_probe_tri_root.hlsl -- VS scale (1, 1) from
 * a root CBV visible to the VS only, PS colour blue from root constants
 * visible to the PS only; PASS = centre blue. rooted = 2 (C6d): a yellow
 * quad (-0.5..0.5) from four vertices and six R16 indices through
 * DrawIndexedInstanced; PASS = centre yellow, 960..1088 pixels. */
static int draw_triangle(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int rooted) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_INPUT_ELEMENT_DESC elements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_ROOT_PARAMETER parameters[2] = {{D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS}, {D3D12_ROOT_PARAMETER_TYPE_CBV}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {rooted ? 2u : 0u, rooted ? parameters : NULL, 0, NULL,
                                           D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    const int indexed = rooted == 2;
    const char *step = indexed ? "C6D" : rooted ? "C6C" : "C6B";
    const unsigned char want[3] = {(unsigned char)(indexed ? 255 : 0), (unsigned char)(rooted == 1 ? 0 : 255),
                                   (unsigned char)(rooted == 1 ? 255 : 0)};
    if (indexed) rooted = 0; /* the C6b pipeline and root signature */
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12Resource *texture = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    ID3D12Resource *vertices = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0};
    unsigned char *bytes = NULL;
    unsigned green = 0, red = 0, other = 4096;
    int centre_ok = 0, corner_ok = 0;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    parameters[0].Constants.ShaderRegister = 0;
    parameters[0].Constants.Num32BitValues = 4;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].Descriptor.ShaderRegister = 1;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    HRESULT hr = vertices && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(vertices, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        const float data[3][6] = {{-0.5f, -0.5f, 0, 1, 0, 1}, {0.0f, 0.5f, 0, 1, 0, 1}, {0.5f, -0.5f, 0, 1, 0, 1}};
        const float quad[4][6] = {{-0.5f, -0.5f, 1, 1, 0, 1}, {-0.5f, 0.5f, 1, 1, 0, 1},
                                  {0.5f, -0.5f, 1, 1, 0, 1}, {0.5f, 0.5f, 1, 1, 0, 1}};
        const unsigned short indices[6] = {0, 1, 2, 2, 1, 3};
        const float scale[4] = {1.0f, 1.0f, 0.0f, 0.0f};
        if (indexed) memcpy(bytes, quad, sizeof(quad));
        else memcpy(bytes, data, sizeof(data));
        memcpy(bytes + 512, indices, sizeof(indices));
        memcpy(bytes + 256, scale, sizeof(scale)); /* the root CBV (256-byte aligned) */
        ID3D12Resource_Unmap(vertices, 0, NULL);
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
                                                  (void **)&texture);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    if (SUCCEEDED(hr)) {
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
        ID3D12Device_CreateRenderTargetView(device, texture, NULL, rtv);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    printf("%s texture/RTV/root hr=0x%08lx\n", step, (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.VS.pShaderBytecode = rooted ? g_agx_d3d12_probe_vs_root : g_agx_d3d12_probe_vs;
        pso_desc.VS.BytecodeLength = rooted ? sizeof(g_agx_d3d12_probe_vs_root) : sizeof(g_agx_d3d12_probe_vs);
        pso_desc.PS.pShaderBytecode = rooted ? g_agx_d3d12_probe_ps_root : g_agx_d3d12_probe_ps;
        pso_desc.PS.BytecodeLength = rooted ? sizeof(g_agx_d3d12_probe_ps_root) : sizeof(g_agx_d3d12_probe_ps);
        pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pso_desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
        pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso_desc.SampleMask = 0xffffffffu;
        pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso_desc.RasterizerState.DepthClipEnable = TRUE;
        pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        pso_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        pso_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso_desc.DepthStencilState.BackFace = pso_desc.DepthStencilState.FrontFace;
        pso_desc.InputLayout.pInputElementDescs = elements;
        pso_desc.InputLayout.NumElements = rooted ? 1 : 2; /* the rooted VS reads POSITION only */
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso_desc.SampleDesc.Count = 1;
        hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("%s CreateGraphicsPipelineState hr=0x%08lx\n", step, (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        const FLOAT red_color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        D3D12_VIEWPORT viewport = {0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
        D3D12_RECT scissor = {0, 0, 64, 64};
        D3D12_VERTEX_BUFFER_VIEW vbv = {0};
        D3D12_TEXTURE_COPY_LOCATION tex_loc = {0}, buf_loc = {0};
        vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vertices);
        vbv.SizeInBytes = sizeof(float) * 24;
        vbv.StrideInBytes = sizeof(float) * 6;
        tex_loc.pResource = texture;
        tex_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        buf_loc.pResource = readback;
        buf_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        buf_loc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        buf_loc.PlacedFootprint.Footprint.Width = 64;
        buf_loc.PlacedFootprint.Footprint.Height = 64;
        buf_loc.PlacedFootprint.Footprint.Depth = 1;
        buf_loc.PlacedFootprint.Footprint.RowPitch = 256;
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, red_color, 0, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
        if (rooted) {
            const FLOAT blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list, 0, 4, blue, 0);
            ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list, 1, ID3D12Resource_GetGPUVirtualAddress(vertices) + 256);
        }
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv);
        if (indexed) {
            D3D12_INDEX_BUFFER_VIEW ibv = {ID3D12Resource_GetGPUVirtualAddress(vertices) + 512, 12, DXGI_FORMAT_R16_UINT};
            ID3D12GraphicsCommandList_IASetIndexBuffer(list, &ibv);
            ID3D12GraphicsCommandList_DrawIndexedInstanced(list, 6, 1, 0, 0, 0);
        } else {
            ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
        }
        transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &buf_loc, 0, 0, 0, &tex_loc, NULL);
        hr = run_list(queue, fence, list, base + 1, indexed ? "C6D draw" : rooted ? "C6C draw" : "C6B draw");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            other = 0;
            for (unsigned i = 0; i < 4096; ++i) {
                const unsigned char *px = bytes + 4 * i;
                if (px[0] == want[0] && px[1] == want[1] && px[2] == want[2] && px[3] == 255) ++green;
                else if (px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255) ++red;
                else ++other;
            }
            const unsigned char *centre = bytes + 32 * 256 + 32 * 4, *corner = bytes;
            centre_ok = centre[0] == want[0] && centre[1] == want[1] && centre[2] == want[2];
            corner_ok = corner[0] == 255 && corner[1] == 0 && corner[2] == 0;
            printf("%s readback drawn=%u red=%u other=%u centre=%u,%u,%u corner=%u,%u,%u\n", step, green, red, other,
                   centre[0], centre[1], centre[2], corner[0], corner[1], corner[2]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    /* the triangle covers 32x32/2 = 512 pixels; edge rules move a row or two */
    const unsigned low = indexed ? 960u : 448u, high = indexed ? 1088u : 576u;
    const int pass = SUCCEEDED(hr) && centre_ok && corner_ok && other == 0 && green >= low && green <= high;
    printf("AGX_D3D12_%s: %s\n", step, pass ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (texture) ID3D12Resource_Release(texture);
    if (readback) ID3D12Resource_Release(readback);
    if (vertices) ID3D12Resource_Release(vertices);
    return !pass;
}

/* C6e (D3D12-C6-graphics.md): a textured full-screen quad (triangle strip)
 * sampling a 2x2 RGBA8 texture (green, blue / white, black) uploaded with
 * CopyTextureRegion, SRV t0 from a shader-visible table, static point-clamp
 * sampler s0, both visible to the PS; PASS = the four 32x32 quadrants. */
static int draw_textured(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0}, tex_desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc_heap = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_INPUT_ELEMENT_DESC elements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER parameter = {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE};
    D3D12_STATIC_SAMPLER_DESC sampler = {D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                         D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 1,
                                         D3D12_COMPARISON_FUNC_NEVER, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, 0.0f,
                                         D3D12_FLOAT32_MAX, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12Resource *target = NULL, *texture = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL, *srv_heap = NULL;
    ID3D12Resource *upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0}, srv_cpu = {0};
    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu = {0};
    unsigned char *bytes = NULL;
    int quadrants_ok = 0;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    tex_desc = desc;
    tex_desc.Width = 2;
    tex_desc.Height = 2;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    HRESULT hr = upload && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        /* vertices (pos, uv) at 0 for a strip; texels at 512 (pitch 256) */
        const float quad[4][6] = {{-1, -1, 0, 1, 0, 0}, {-1, 1, 0, 0, 0, 0}, {1, -1, 1, 1, 0, 0}, {1, 1, 1, 0, 0, 0}};
        const unsigned char row0[8] = {0, 255, 0, 255, 0, 0, 255, 255}, row1[8] = {255, 255, 255, 255, 0, 0, 0, 255};
        memcpy(bytes, quad, sizeof(quad));
        memcpy(bytes + 512, row0, sizeof(row0));
        memcpy(bytes + 512 + 256, row1, sizeof(row1));
        ID3D12Resource_Unmap(upload, 0, NULL);
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
                                                  (void **)&target);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &tex_desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
                                                  (void **)&texture);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &srv_desc_heap, &IID_ID3D12DescriptorHeap, (void **)&srv_heap);
    if (SUCCEEDED(hr)) {
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
        ID3D12Device_CreateRenderTargetView(device, target, NULL, rtv);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap, &srv_cpu);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap, &srv_gpu);
        ID3D12Device_CreateShaderResourceView(device, texture, NULL, srv_cpu);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    printf("C6E resources/views/root hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.VS.pShaderBytecode = g_agx_d3d12_probe_vs_tex;
        pso_desc.VS.BytecodeLength = sizeof(g_agx_d3d12_probe_vs_tex);
        pso_desc.PS.pShaderBytecode = g_agx_d3d12_probe_ps_tex;
        pso_desc.PS.BytecodeLength = sizeof(g_agx_d3d12_probe_ps_tex);
        pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pso_desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
        pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso_desc.SampleMask = 0xffffffffu;
        pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso_desc.RasterizerState.DepthClipEnable = TRUE;
        pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        pso_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        pso_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso_desc.DepthStencilState.BackFace = pso_desc.DepthStencilState.FrontFace;
        pso_desc.InputLayout.pInputElementDescs = elements;
        pso_desc.InputLayout.NumElements = 2;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso_desc.SampleDesc.Count = 1;
        hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C6E CreateGraphicsPipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        const FLOAT red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        D3D12_VIEWPORT viewport = {0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
        D3D12_RECT scissor = {0, 0, 64, 64};
        D3D12_VERTEX_BUFFER_VIEW vbv = {0};
        D3D12_TEXTURE_COPY_LOCATION up = {0}, tex_loc = {0}, target_loc = {0}, back = {0};
        vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(upload);
        vbv.SizeInBytes = sizeof(float) * 24;
        vbv.StrideInBytes = sizeof(float) * 6;
        up.pResource = upload;
        up.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        up.PlacedFootprint.Offset = 512;
        up.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        up.PlacedFootprint.Footprint.Width = 2;
        up.PlacedFootprint.Footprint.Height = 2;
        up.PlacedFootprint.Footprint.Depth = 1;
        up.PlacedFootprint.Footprint.RowPitch = 256;
        tex_loc.pResource = texture;
        tex_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        target_loc.pResource = target;
        target_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        back.pResource = readback;
        back.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        back.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        back.PlacedFootprint.Footprint.Width = 64;
        back.PlacedFootprint.Footprint.Height = 64;
        back.PlacedFootprint.Footprint.Depth = 1;
        back.PlacedFootprint.Footprint.RowPitch = 256;
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &tex_loc, 0, 0, 0, &up, NULL);
        transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, red, 0, NULL);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &srv_heap);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list, 0, srv_gpu);
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_DrawInstanced(list, 4, 1, 0, 0);
        transition(list, target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &back, 0, 0, 0, &target_loc, NULL);
        hr = run_list(queue, fence, list, base + 1, "C6E draw");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            const unsigned char *tl = bytes + 16 * 256 + 16 * 4, *tr = bytes + 16 * 256 + 48 * 4;
            const unsigned char *bl = bytes + 48 * 256 + 16 * 4, *br = bytes + 48 * 256 + 48 * 4;
            quadrants_ok = tl[0] == 0 && tl[1] == 255 && tl[2] == 0 && tr[0] == 0 && tr[1] == 0 && tr[2] == 255 &&
                           bl[0] == 255 && bl[1] == 255 && bl[2] == 255 && br[0] == 0 && br[1] == 0 && br[2] == 0;
            printf("C6E quadrants tl=%u,%u,%u tr=%u,%u,%u bl=%u,%u,%u br=%u,%u,%u\n", tl[0], tl[1], tl[2], tr[0], tr[1],
                   tr[2], bl[0], bl[1], bl[2], br[0], br[1], br[2]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_C6E: %s\n", SUCCEEDED(hr) && quadrants_ok ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (srv_heap) ID3D12DescriptorHeap_Release(srv_heap);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (texture) ID3D12Resource_Release(texture);
    if (target) ID3D12Resource_Release(target);
    if (readback) ID3D12Resource_Release(readback);
    if (upload) ID3D12Resource_Release(upload);
    return !(SUCCEEDED(hr) && quadrants_ok);
}

/* C6f (D3D12-C6-graphics.md): depth testing -- a D32_FLOAT depth buffer
 * cleared to 1.0, a near green triangle (z 0.25) drawn first, then a far
 * blue full-target quad (z 0.75) with DepthFunc LESS; PASS = centre green
 * (the far quad fails there), a point outside the triangle blue. */
static int draw_depth(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0}, depth_desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_DESCRIPTOR_HEAP_DESC dsv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_INPUT_ELEMENT_DESC elements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {0, NULL, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12Resource *target = NULL, *depth = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL, *dsv_heap = NULL;
    ID3D12Resource *vertices = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0}, dsv = {0};
    unsigned char *bytes = NULL;
    int ok = 0;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    depth_desc = desc;
    depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    HRESULT hr = vertices && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(vertices, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        /* triangle (near, green) then a quad as a strip (far, blue); 7 floats per vertex */
        const float data[7][7] = {{-0.5f, -0.5f, 0.25f, 0, 1, 0, 1}, {0.0f, 0.5f, 0.25f, 0, 1, 0, 1},
                                  {0.5f, -0.5f, 0.25f, 0, 1, 0, 1}, {-1, -1, 0.75f, 0, 0, 1, 1},
                                  {-1, 1, 0.75f, 0, 0, 1, 1}, {1, -1, 0.75f, 0, 0, 1, 1}, {1, 1, 0.75f, 0, 0, 1, 1}};
        memcpy(bytes, data, sizeof(data));
        ID3D12Resource_Unmap(vertices, 0, NULL);
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
                                                  (void **)&target);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &depth_desc,
                                                  D3D12_RESOURCE_STATE_DEPTH_WRITE, NULL, &IID_ID3D12Resource,
                                                  (void **)&depth);
    printf("C6F CreateCommittedResource depth hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &dsv_desc, &IID_ID3D12DescriptorHeap, (void **)&dsv_heap);
    if (SUCCEEDED(hr)) {
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
        ID3D12Device_CreateRenderTargetView(device, target, NULL, rtv);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsv_heap, &dsv);
        ID3D12Device_CreateDepthStencilView(device, depth, NULL, dsv);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.VS.pShaderBytecode = g_agx_d3d12_probe_vs_depth;
        pso_desc.VS.BytecodeLength = sizeof(g_agx_d3d12_probe_vs_depth);
        pso_desc.PS.pShaderBytecode = g_agx_d3d12_probe_ps;
        pso_desc.PS.BytecodeLength = sizeof(g_agx_d3d12_probe_ps);
        pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pso_desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
        pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso_desc.SampleMask = 0xffffffffu;
        pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso_desc.RasterizerState.DepthClipEnable = TRUE;
        pso_desc.DepthStencilState.DepthEnable = TRUE;
        pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        pso_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        pso_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        pso_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso_desc.DepthStencilState.BackFace = pso_desc.DepthStencilState.FrontFace;
        pso_desc.InputLayout.pInputElementDescs = elements;
        pso_desc.InputLayout.NumElements = 2;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pso_desc.SampleDesc.Count = 1;
        hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C6F CreateGraphicsPipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        const FLOAT red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        D3D12_VIEWPORT viewport = {0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
        D3D12_RECT scissor = {0, 0, 64, 64};
        D3D12_VERTEX_BUFFER_VIEW vbv = {0};
        D3D12_TEXTURE_COPY_LOCATION target_loc = {0}, back = {0};
        vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vertices);
        vbv.SizeInBytes = sizeof(float) * 49;
        vbv.StrideInBytes = sizeof(float) * 7;
        target_loc.pResource = target;
        target_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        back.pResource = readback;
        back.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        back.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        back.PlacedFootprint.Footprint.Width = 64;
        back.PlacedFootprint.Footprint.Height = 64;
        back.PlacedFootprint.Footprint.Depth = 1;
        back.PlacedFootprint.Footprint.RowPitch = 256;
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, red, 0, NULL);
        ID3D12GraphicsCommandList_ClearDepthStencilView(list, dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, &dsv);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D12GraphicsCommandList_DrawInstanced(list, 4, 1, 3, 0);
        transition(list, target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &back, 0, 0, 0, &target_loc, NULL);
        hr = run_list(queue, fence, list, base + 1, "C6F draw");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            const unsigned char *centre = bytes + 32 * 256 + 32 * 4, *corner = bytes + 4 * 256 + 4 * 4;
            ok = centre[0] == 0 && centre[1] == 255 && centre[2] == 0 && corner[0] == 0 && corner[1] == 0 && corner[2] == 255;
            printf("C6F centre=%u,%u,%u corner=%u,%u,%u\n", centre[0], centre[1], centre[2], corner[0], corner[1], corner[2]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_C6F: %s\n", SUCCEEDED(hr) && ok ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (dsv_heap) ID3D12DescriptorHeap_Release(dsv_heap);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (depth) ID3D12Resource_Release(depth);
    if (target) ID3D12Resource_Release(target);
    if (readback) ID3D12Resource_Release(readback);
    if (vertices) ID3D12Resource_Release(vertices);
    return !(SUCCEEDED(hr) && ok);
}

/* C7A (D3D12-C7-present.md, measurement): a flip-model swapchain on the
 * direct queue -- window, CreateSwapChainForHwnd (2 x 64x64 RGBA8,
 * FLIP_DISCARD), back buffer 0 cleared green through an RTV, transition to
 * PRESENT, Present(0, 0); every step prints its HRESULT and the UMD
 * receipts name the DDIs the runtime and DXGI call or refuse. */
static LRESULT CALLBACK probe_window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    return DefWindowProcW(window, message, w, l);
}
static int swapchain_present(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    WNDCLASSW wc = {0};
    HWND window = NULL;
    IDXGIFactory2 *factory = NULL;
    IDXGISwapChain1 *swapchain = NULL;
    ID3D12Resource *buffer = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    DXGI_SWAP_CHAIN_DESC1 desc = {0};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0};
    HRESULT hr;
    wc.lpfnWndProc = probe_window_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"AgxD3d12Probe";
    RegisterClassW(&wc);
    window = CreateWindowExW(0, wc.lpszClassName, L"AGX D3D12 C7A", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 160, 160,
                             NULL, NULL, wc.hInstance, NULL);
    hr = window ? CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&factory) : E_FAIL;
    printf("C7A window %p factory hr=0x%08lx\n", (void *)window, (unsigned long)hr);
    desc.Width = 64;
    desc.Height = 64;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (SUCCEEDED(hr)) {
        hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)queue, window, &desc, NULL, NULL, &swapchain);
        printf("C7A CreateSwapChainForHwnd hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) {
        hr = IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_ID3D12Resource, (void **)&buffer);
        printf("C7A GetBuffer hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        const FLOAT green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
        ID3D12Device_CreateRenderTargetView(device, buffer, NULL, rtv);
        transition(list, buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, green, 0, NULL);
        transition(list, buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        hr = run_list(queue, fence, list, base + 1, "C7A clear");
    }
    if (SUCCEEDED(hr)) {
        hr = IDXGISwapChain1_Present(swapchain, 0, 0);
        printf("C7A Present hr=0x%08lx\n", (unsigned long)hr);
        if (SUCCEEDED(hr)) {
            hr = ID3D12CommandQueue_Signal(queue, fence, base + 2);
            UINT64 value = 0;
            for (int i = 0; i < 300 && (value = ID3D12Fence_GetCompletedValue(fence)) < base + 2; ++i) Sleep(10);
            printf("C7A signal after present hr=0x%08lx completed %llu\n", (unsigned long)hr, (unsigned long long)value);
            if (value < base + 2) hr = E_FAIL;
        }
    }
    printf("AGX_D3D12_C7A: %s\n", SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (buffer) ID3D12Resource_Release(buffer);
    if (swapchain) IDXGISwapChain1_Release(swapchain);
    if (factory) IDXGIFactory2_Release(factory);
    if (window) DestroyWindow(window);
    return FAILED(hr);
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
                compute_dispatch(device, queue, fence) == 0 && compute_constants(device, queue, fence) == 0 &&
                compute_table(device, queue, fence) == 0)
                (void)compute_srv(device, queue, fence);
            /* C6a is independent of the compute steps (it needs C4b only) */
            if (SUCCEEDED(c4) && value == 2) {
                UINT64 done = ID3D12Fence_GetCompletedValue(fence);
                if (done >= 3 && texture_clear_copy(device, queue, fence, done) == 0)
                    if (draw_triangle(device, queue, fence, ID3D12Fence_GetCompletedValue(fence), 0) == 0) {
                        (void)draw_triangle(device, queue, fence, ID3D12Fence_GetCompletedValue(fence), 1);
                        (void)draw_triangle(device, queue, fence, ID3D12Fence_GetCompletedValue(fence), 2);
                        (void)draw_textured(device, queue, fence, ID3D12Fence_GetCompletedValue(fence));
                        (void)draw_depth(device, queue, fence, ID3D12Fence_GetCompletedValue(fence));
                        /* C7A last: a refused swapchain may remove the device */
                        (void)swapchain_present(device, queue, fence, ID3D12Fence_GetCompletedValue(fence));
                    }
            }
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
