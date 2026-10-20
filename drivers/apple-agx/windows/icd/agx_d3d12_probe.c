#define COBJMACROS
#include <windows.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <stdio.h>
#include <string.h>

#include "agx_d3d12_probe_gs.h"
#include "agx_d3d12_probe_cs_texture1d_load.h"
#include "agx_d3d12_probe_cs_texture1d_store.h"
#include "agx_d3d12_probe_cs_texture1d_array_load.h"
#include "agx_d3d12_probe_cs_texture1d_array_store.h"
#include "agx_d3d12_probe_cs.h"
#include "agx_d3d12_probe_cs_cb.h"
#include "agx_d3d12_probe_cs_table.h"
#include "agx_d3d12_probe_cs_srv.h"
#include "agx_d3d12_probe_cs_slots.h"
#include "agx_d3d12_probe_vs.h"
#include "agx_d3d12_probe_ps.h"
#include "agx_d3d12_probe_vs_root.h"
#include "agx_d3d12_probe_ps_root.h"
#include "agx_d3d12_probe_vs_tex.h"
#include "agx_d3d12_probe_ps_tex.h"
#include "agx_d3d12_probe_vs_depth.h"
#include "agx_d3d12_probe_cs_img.h"
#include "agx_d3d12_probe_ps_uav.h"
#include "agx_d3d12_probe_cs_spaces.h"
#include "agx_d3d12_probe_ps_spaces.h"
#include "agx_d3d12_probe_tex_spaces.h"
#include "agx_d3d12_probe_ps_cube.h"
#include "agx_d3d12_probe_ps_cube_array.h"
#include "agx_d3d12_probe_ps_cube_array_second.h"
#include "agx_d3d12_probe_cs_typed_extent.h"
#include "agx_d3d12_probe_cs_volume_load.h"
#include "agx_d3d12_probe_cs_volume_store.h"
#include "agx_d3d12_probe_ps_volume.h"

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
static int compute_srv(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, int high_slots) {
    const char *step = high_slots ? "C16A" : "C5D";
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, high_slots ? 126u : 1u, 0, 0};
    D3D12_ROOT_PARAMETER parameters[4] = {{D3D12_ROOT_PARAMETER_TYPE_UAV}, {D3D12_ROOT_PARAMETER_TYPE_SRV},
                                          {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}, {D3D12_ROOT_PARAMETER_TYPE_CBV}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {high_slots ? 4u : 3u, parameters};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12DescriptorHeap *heap = NULL;
    ID3D12Resource *cb = high_slots ? committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ) : NULL;
    ID3D12Resource *uav = uav_buffer(device);
    ID3D12Resource *a = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *b = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    UINT *dwords = NULL;
    unsigned bad = 16384;
    UINT64 value = 0;
    parameters[0].Descriptor.ShaderRegister = high_slots ? 7u : 0u;
    parameters[1].Descriptor.ShaderRegister = high_slots ? 127u : 0u;
    parameters[3].Descriptor.ShaderRegister = 13;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &range;
    HRESULT hr = uav && a && b && readback && (!high_slots || cb) ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(a, 0, NULL, (void **)&dwords);
    if (SUCCEEDED(hr)) {
        for (unsigned i = 0; i < 16384; ++i) dwords[i] = 3u * i;
        ID3D12Resource_Unmap(a, 0, NULL);
        hr = ID3D12Resource_Map(b, 0, NULL, (void **)&dwords);
    }
    if (SUCCEEDED(hr)) {
        for (unsigned i = 0; i < 16384; ++i) dwords[i] = i + 100u;
        ID3D12Resource_Unmap(b, 0, NULL);
    }
    if (SUCCEEDED(hr) && high_slots) {
        hr = ID3D12Resource_Map(cb, 0, NULL, (void **)&dwords);
        if (SUCCEEDED(hr)) {
            dwords[0] = 7;
            ID3D12Resource_Unmap(cb, 0, NULL);
        }
    }
    if (SUCCEEDED(hr)) {
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
    printf("%s CreateRootSignature hr=0x%08lx\n", step, (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = high_slots ? g_agx_d3d12_probe_cs_slots : g_agx_d3d12_probe_cs_srv;
        pso_desc.CS.BytecodeLength = high_slots ? sizeof(g_agx_d3d12_probe_cs_slots) : sizeof(g_agx_d3d12_probe_cs_srv);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("%s CreateComputePipelineState hr=0x%08lx\n", step, (unsigned long)hr);
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
        if (high_slots) ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(list, 3, ID3D12Resource_GetGPUVirtualAddress(cb));
        ID3D12GraphicsCommandList_Dispatch(list, 256, 1, 1);
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, uav, 0, 65536);
        hr = ID3D12GraphicsCommandList_Close(list);
        printf("%s record+close hr=0x%08lx\n", step, (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        hr = ID3D12CommandQueue_Signal(queue, fence, 7);
        for (int i = 0; i < 300 && (value = ID3D12Fence_GetCompletedValue(fence)) < 7; ++i) Sleep(10);
        printf("%s execute+signal hr=0x%08lx completed %llu\n", step, (unsigned long)hr, (unsigned long long)value);
    }
    if (SUCCEEDED(hr) && value == 7) {
        D3D12_RANGE all = {0, 65536};
        dwords = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&dwords);
        if (SUCCEEDED(hr) && dwords) {
            bad = 0;
            for (unsigned i = 0; i < 16384; ++i) bad += dwords[i] != i * 5u + 200u + (high_slots ? 7u : 0u);
            printf("%s readback bad=%u first=%u,%u last=%u\n", step, bad, dwords[0], dwords[1], dwords[16383]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_%s: %s\n", step, SUCCEEDED(hr) && value == 7 && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (heap) ID3D12DescriptorHeap_Release(heap);
    if (readback) ID3D12Resource_Release(readback);
    if (cb) ID3D12Resource_Release(cb);
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
static int draw_triangle(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int mode) {
    /* EXP1369: the indexed mode must build the C6b (empty) root signature */
    const int indexed = mode == 2;
    const int redundant_root = mode == 8;
    int rooted = mode == 1 || redundant_root;
    /* C11 (D3D12-C11-queries.md): mode 3 draws the C6b triangle inside an
     * occlusion, a binary-occlusion and a pipeline-statistics query between
     * two timestamps and resolves all of them */
    const int queried = mode == 3;
    /* C13 (D3D12-C13-graphics-uavs.md): mode 4 counts the shaded pixels
     * with a PS root UAV u1 (InterlockedAdd) */
    /* C14 (D3D12-C14-execute-indirect.md): mode 5 is mode 4 drawn through
     * ExecuteIndirect -- two identical draw records, a count buffer of 1;
     * the counter shows whether the count limits the draws */
    const int indirect = mode == 5 || mode == 6;
    const int indirect_no_count = mode == 6;
    const int spaced = mode == 7;
    const int counted = mode == 4 || indirect || spaced;
    ID3D12CommandSignature *signature = NULL;
    ID3D12Resource *counter = NULL, *counter_back = NULL;
    D3D12_ROOT_PARAMETER uav_parameter = {D3D12_ROOT_PARAMETER_TYPE_UAV};
    ID3D12QueryHeap *occlusion = NULL, *statistics = NULL, *timestamps = NULL;
    ID3D12Resource *results = NULL;
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_INPUT_ELEMENT_DESC elements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_ROOT_PARAMETER parameters[2] = {{D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS}, {D3D12_ROOT_PARAMETER_TYPE_CBV}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {rooted ? 2u : counted ? 1u : 0u, rooted ? parameters : counted ? &uav_parameter : NULL, 0, NULL,
                                           D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    const char *step = redundant_root ? "ROOTG" : spaced ? "C17B_DRAW" : indexed ? "C6D" : rooted ? "C6C" : queried ? "C11" : indirect_no_count ? "C14NC" : indirect ? "C14" : counted ? "C13" : "C6B";
    const unsigned char want[3] = {(unsigned char)(indexed ? 255 : 0), (unsigned char)(rooted ? 0 : 255),
                                   (unsigned char)(rooted ? 255 : 0)};
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
    uav_parameter.Descriptor.ShaderRegister = 1;
    uav_parameter.Descriptor.RegisterSpace = spaced ? 4u : 0u;
    uav_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    if (counted) {
        counter = uav_buffer(device);
        counter_back = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    }
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
        memset(bytes + 1024, 0, 16); /* C13: the counter's initial value */
        {
            /* C14: two D3D12_DRAW_ARGUMENTS records at 2048, the count at 2080 */
            const UINT records[8] = {3, 1, 0, 0, 3, 1, 0, 0}, one = 1;
            memcpy(bytes + 2048, records, sizeof(records));
            memcpy(bytes + 2080, &one, sizeof(one));
        }
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
        pso_desc.PS.pShaderBytecode = rooted ? g_agx_d3d12_probe_ps_root : spaced ? g_agx_d3d12_probe_ps_spaces : counted ? g_agx_d3d12_probe_ps_uav : g_agx_d3d12_probe_ps;
        pso_desc.PS.BytecodeLength = rooted ? sizeof(g_agx_d3d12_probe_ps_root)
                                   : spaced ? sizeof(g_agx_d3d12_probe_ps_spaces) : counted ? sizeof(g_agx_d3d12_probe_ps_uav) : sizeof(g_agx_d3d12_probe_ps);
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
        pso_desc.InputLayout.NumElements = 2; /* EXP1369: every VS input-signature element needs a layout element */
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
        if (counted && counter && counter_back) {
            transition(list, counter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12GraphicsCommandList_CopyBufferRegion(list, counter, 0, vertices, 1024, 16);
            transition(list, counter, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(list, 0, ID3D12Resource_GetGPUVirtualAddress(counter));
        }
        if (rooted) {
            const FLOAT blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list, 0, 4, blue, 0);
            ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list, 1, ID3D12Resource_GetGPUVirtualAddress(vertices) + 256);
            if (redundant_root) {
                ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
                printf("ROOTG repeated identical root after constants+CBV bind\n");
            }
        }
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv);
        if (queried) {
            D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_OCCLUSION, 2, 0};
            HRESULT qh = ID3D12Device_CreateQueryHeap(device, &qd, &IID_ID3D12QueryHeap, (void **)&occlusion);
            qd.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
            qd.Count = 1;
            if (SUCCEEDED(qh)) qh = ID3D12Device_CreateQueryHeap(device, &qd, &IID_ID3D12QueryHeap, (void **)&statistics);
            qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            qd.Count = 2;
            if (SUCCEEDED(qh)) qh = ID3D12Device_CreateQueryHeap(device, &qd, &IID_ID3D12QueryHeap, (void **)&timestamps);
            results = SUCCEEDED(qh) ? committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST) : NULL;
            printf("C11 CreateQueryHeap x3 hr=0x%08lx\n", (unsigned long)qh);
            if (results) {
                /* EXP1375: one occlusion query active at a time (Asahi keeps one
                 * per context): the counter around this draw, binary around a
                 * repeat of it */
                ID3D12GraphicsCommandList_EndQuery(list, timestamps, D3D12_QUERY_TYPE_TIMESTAMP, 0);
                ID3D12GraphicsCommandList_BeginQuery(list, occlusion, D3D12_QUERY_TYPE_OCCLUSION, 0);
                ID3D12GraphicsCommandList_BeginQuery(list, statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
            }
        }
        if (indexed) {
            D3D12_INDEX_BUFFER_VIEW ibv = {ID3D12Resource_GetGPUVirtualAddress(vertices) + 512, 12, DXGI_FORMAT_R16_UINT};
            ID3D12GraphicsCommandList_IASetIndexBuffer(list, &ibv);
            ID3D12GraphicsCommandList_DrawIndexedInstanced(list, 6, 1, 0, 0, 0);
        } else if (indirect) {
            D3D12_INDIRECT_ARGUMENT_DESC arg = {D3D12_INDIRECT_ARGUMENT_TYPE_DRAW};
            D3D12_COMMAND_SIGNATURE_DESC sd = {16, 1, &arg, 0};
            HRESULT sh = ID3D12Device_CreateCommandSignature(device, &sd, NULL, &IID_ID3D12CommandSignature, (void **)&signature);
            printf("%s CreateCommandSignature hr=0x%08lx\n", step, (unsigned long)sh);
            if (SUCCEEDED(sh))
                ID3D12GraphicsCommandList_ExecuteIndirect(list, signature, indirect_no_count ? 1 : 2,
                                                           vertices, 2048, indirect_no_count ? NULL : vertices,
                                                           indirect_no_count ? 0 : 2080);
        } else {
            ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
        }
        if (counted && counter && counter_back) {
            transition(list, counter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ID3D12GraphicsCommandList_CopyBufferRegion(list, counter_back, 0, counter, 0, 16);
        }
        if (queried && results) {
            ID3D12GraphicsCommandList_EndQuery(list, statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
            ID3D12GraphicsCommandList_EndQuery(list, occlusion, D3D12_QUERY_TYPE_OCCLUSION, 0);
            ID3D12GraphicsCommandList_BeginQuery(list, occlusion, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 1);
            ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
            ID3D12GraphicsCommandList_EndQuery(list, occlusion, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 1);
            ID3D12GraphicsCommandList_EndQuery(list, timestamps, D3D12_QUERY_TYPE_TIMESTAMP, 1);
            ID3D12GraphicsCommandList_ResolveQueryData(list, occlusion, D3D12_QUERY_TYPE_OCCLUSION, 0, 1, results, 0);
            ID3D12GraphicsCommandList_ResolveQueryData(list, occlusion, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 1, 1, results, 8);
            ID3D12GraphicsCommandList_ResolveQueryData(list, statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1, results, 64);
            ID3D12GraphicsCommandList_ResolveQueryData(list, timestamps, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, results, 256);
        }
        transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &buf_loc, 0, 0, 0, &tex_loc, NULL);
        hr = run_list(queue, fence, list, base + 1,
                      indexed ? "C6D draw" : rooted ? "C6C draw" : queried ? "C11 draw" : indirect ? "C14 draw"
                                                                           : counted ? "C13 draw" : "C6B draw");
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
    if (queried) {
        const UINT64 *q = NULL;
        D3D12_RANGE all = {0, 512};
        int occlusion_ok = 0, statistics_ok = 0, timestamps_ok = 0;
        UINT64 frequency = 0, gpu_clock = 0, cpu_clock = 0;
        HRESULT fh = ID3D12CommandQueue_GetTimestampFrequency(queue, &frequency);
        HRESULT ch = ID3D12CommandQueue_GetClockCalibration(queue, &gpu_clock, &cpu_clock);
        printf("C11 GetTimestampFrequency hr=0x%08lx frequency=%llu GetClockCalibration hr=0x%08lx gpu=%llu cpu=%llu\n",
               (unsigned long)fh, (unsigned long long)frequency, (unsigned long)ch, (unsigned long long)gpu_clock,
               (unsigned long long)cpu_clock);
        if (SUCCEEDED(hr) && results && SUCCEEDED(ID3D12Resource_Map(results, 0, &all, (void **)&q)) && q) {
            printf("C11 occlusion=%llu binary=%llu drawn=%u\n", (unsigned long long)q[0], (unsigned long long)q[1], green);
            printf("C11 statistics ia_vertices=%llu ia_primitives=%llu vs=%llu c_invocations=%llu c_primitives=%llu ps=%llu\n",
                   (unsigned long long)q[8], (unsigned long long)q[9], (unsigned long long)q[10], (unsigned long long)q[13],
                   (unsigned long long)q[14], (unsigned long long)q[15]);
            printf("C11 timestamps t0=%llu t1=%llu\n", (unsigned long long)q[32], (unsigned long long)q[33]);
            occlusion_ok = q[0] == green && q[1] == 1;
            statistics_ok = q[8] == 3 && q[9] == 1;
            timestamps_ok = q[32] != 0 && q[33] >= q[32];
            ID3D12Resource_Unmap(results, 0, NULL);
        }
        printf("AGX_D3D12_C11A: %s\n", occlusion_ok ? "PASS" : "FAIL");
        printf("AGX_D3D12_C11B: %s\n", statistics_ok ? "PASS" : "FAIL");
        printf("AGX_D3D12_C11C: %s\n", timestamps_ok && SUCCEEDED(fh) && frequency ? "PASS" : "FAIL");
    }
    if (counted) {
        const UINT *c = NULL;
        D3D12_RANGE all = {0, 16};
        int counted_ok = 0;
        if (SUCCEEDED(hr) && counter_back && SUCCEEDED(ID3D12Resource_Map(counter_back, 0, &all, (void **)&c)) && c) {
            printf("C13 counter=%u drawn=%u\n", c[0], green);
            counted_ok = c[0] == green;
            ID3D12Resource_Unmap(counter_back, 0, NULL);
        }
        printf("AGX_D3D12_%s: %s\n", spaced ? "C17B" : indirect_no_count ? "C14NCA" : indirect ? "C14A" : "C13A",
               pass && counted_ok ? "PASS" : "FAIL");
    }
    if (signature) ID3D12CommandSignature_Release(signature);
    if (counter_back) ID3D12Resource_Release(counter_back);
    if (counter) ID3D12Resource_Release(counter);
    if (results) ID3D12Resource_Release(results);
    if (timestamps) ID3D12QueryHeap_Release(timestamps);
    if (statistics) ID3D12QueryHeap_Release(statistics);
    if (occlusion) ID3D12QueryHeap_Release(occlusion);
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
static int draw_textured_impl(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int variant) {
    const int spaces = variant == 1;
    const int mapping = variant >= 2;
    const char *step = variant == 2 ? "C18A" : variant == 3 ? "C18B" : spaces ? "C17C" : "C6E";
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
    const unsigned char mapped_texels[4][4] = {{17,61,109,151}, {29,83,137,173}, {43,97,163,199}, {59,113,181,227}};
    if (mapping) srv_desc_heap.NumDescriptors = 2;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    range.RegisterSpace = spaces ? 5u : 0u;
    sampler.RegisterSpace = spaces ? 6u : 0u;
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
        memcpy(bytes + 512, mapping ? mapped_texels[0] : row0, sizeof(row0));
        memcpy(bytes + 512 + 256, mapping ? mapped_texels[2] : row1, sizeof(row1));
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
        if (mapping) {
            D3D12_SHADER_RESOURCE_VIEW_DESC view = {0};
            view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = 1;
            view.Shader4ComponentMapping = variant == 2 ? D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(2,0,4,5)
                                                       : D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(1,2,3,0);
            ID3D12Device_CreateShaderResourceView(device, texture, &view, srv_cpu);
            UINT stride = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_CPU_DESCRIPTOR_HANDLE copied = {srv_cpu.ptr + stride};
            ID3D12Device_CopyDescriptorsSimple(device, 1, copied, srv_cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            srv_gpu.ptr += stride;
        } else ID3D12Device_CreateShaderResourceView(device, texture, NULL, srv_cpu);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    printf("%s resources/views/root hr=0x%08lx\n", step, (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.VS.pShaderBytecode = g_agx_d3d12_probe_vs_tex;
        pso_desc.VS.BytecodeLength = sizeof(g_agx_d3d12_probe_vs_tex);
        pso_desc.PS.pShaderBytecode = spaces ? g_agx_d3d12_probe_tex_spaces : g_agx_d3d12_probe_ps_tex;
        pso_desc.PS.BytecodeLength = spaces ? sizeof(g_agx_d3d12_probe_tex_spaces) : sizeof(g_agx_d3d12_probe_ps_tex);
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
        printf("%s CreateGraphicsPipelineState hr=0x%08lx\n", step, (unsigned long)hr);
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
        hr = run_list(queue, fence, list, base + 1, step);
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            const unsigned char *tl = bytes + 16 * 256 + 16 * 4, *tr = bytes + 16 * 256 + 48 * 4;
            const unsigned char *bl = bytes + 48 * 256 + 16 * 4, *br = bytes + 48 * 256 + 48 * 4;
            quadrants_ok = tl[0] == 0 && tl[1] == 255 && tl[2] == 0 && tr[0] == 0 && tr[1] == 0 && tr[2] == 255 &&
                           bl[0] == 255 && bl[1] == 255 && bl[2] == 255 && br[0] == 0 && br[1] == 0 && br[2] == 0;
            printf("%s quadrants tl=%u,%u,%u tr=%u,%u,%u bl=%u,%u,%u br=%u,%u,%u\n", step, tl[0], tl[1], tl[2], tr[0], tr[1],
                   tr[2], bl[0], bl[1], bl[2], br[0], br[1], br[2]);
            if (mapping) {
                unsigned bad = 0;
                for (UINT y = 0; y < 64; ++y) for (UINT x = 0; x < 64; ++x) {
                    const unsigned char *source = mapped_texels[(y >= 32 ? 2 : 0) + (x >= 32 ? 1 : 0)];
                    const unsigned char expected[4] = {variant == 2 ? source[2] : source[1],
                        variant == 2 ? source[0] : source[2], variant == 2 ? 0 : source[3],
                        variant == 2 ? 255 : source[0]};
                    if (memcmp(bytes + y * 256 + x * 4, expected, 4)) ++bad;
                }
                quadrants_ok = bad == 0;
                printf("%s mapped copied-SRV pixels=4096 bad=%u first=%u,%u,%u,%u last=%u,%u,%u,%u\n",
                    step, bad, bytes[0], bytes[1], bytes[2], bytes[3], bytes[16380], bytes[16381], bytes[16382], bytes[16383]);
            }
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_%s: %s\n", step, SUCCEEDED(hr) && quadrants_ok ? "PASS" : "FAIL");
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

/* C19: independent cube-view address/dimension/mip checkpoints. */
static int draw_cube(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int variant) {
    /* A: nonarray cube0/mip0. B: one-element cube array0/mip0.
     * C: one-element array at first face6/mip1. D: two-element array,
     * first face0/mip1, sampling array index1. All24 subresources differ. */
    const char *step = variant == 0 ? "C19A" : variant == 1 ? "C19B" : variant == 2 ? "C19C" : "C19D";
    const UINT selected_cube = variant >= 2 ? 1u : 0u;
    const UINT selected_mip = variant >= 2 ? 1u : 0u;
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
    unsigned bad = 4096, first_bad = ~0u;
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
    tex_desc.Width = 4;
    tex_desc.Height = 4;
    tex_desc.DepthOrArraySize = 12;
    tex_desc.MipLevels = 2;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    HRESULT hr = upload && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        const float quad[4][6] = {{-1, -1, 0, 1, 0, 0}, {-1, 1, 0, 0, 0, 0}, {1, -1, 1, 1, 0, 0}, {1, 1, 1, 0, 0, 0}};
        /* Each512-aligned footprint reserves1024 bytes:4rows*pitch256.
         * Mip1 uses2rows, leaving poison padding rather than stale pixels. */
        memset(bytes, 0xcd, 65536);
        memcpy(bytes, quad, sizeof(quad));
        for (UINT face = 0; face < 12; ++face) for (UINT mip = 0; mip < 2; ++mip) {
            const UINT subresource = mip + face * 2;
            const unsigned char color[4] = {(unsigned char)(17 + 23 * (face % 6)),
                (unsigned char)(31 + 67 * (face / 6)), (unsigned char)(43 + 89 * mip),
                (unsigned char)(151 + 7 * (face % 6) + 11 * (face / 6) + 13 * mip)};
            for (UINT y = 0; y < (4u >> mip); ++y)
                for (UINT x = 0; x < (4u >> mip); ++x)
                    memcpy(bytes + 512 + subresource * 1024 + y * 256 + x * 4, color, 4);
        }
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
        D3D12_SHADER_RESOURCE_VIEW_DESC view = {0};
        view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (variant == 0) {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            view.TextureCube.MostDetailedMip = 0;
            view.TextureCube.MipLevels = 1;
        } else {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
            view.TextureCubeArray.MostDetailedMip = selected_mip;
            view.TextureCubeArray.MipLevels = 1;
            view.TextureCubeArray.First2DArrayFace = variant == 2 ? 6u : 0u;
            view.TextureCubeArray.NumCubes = variant == 3 ? 2u : 1u;
        }
        ID3D12Device_CreateShaderResourceView(device, texture, &view, srv_cpu);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    printf("%s resources/views/root hr=0x%08lx\n", step, (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.VS.pShaderBytecode = g_agx_d3d12_probe_vs_tex;
        pso_desc.VS.BytecodeLength = sizeof(g_agx_d3d12_probe_vs_tex);
        pso_desc.PS.pShaderBytecode = variant == 0 ? g_agx_d3d12_probe_ps_cube :
            variant == 3 ? g_agx_d3d12_probe_ps_cube_array_second : g_agx_d3d12_probe_ps_cube_array;
        pso_desc.PS.BytecodeLength = variant == 0 ? sizeof(g_agx_d3d12_probe_ps_cube) :
            variant == 3 ? sizeof(g_agx_d3d12_probe_ps_cube_array_second) : sizeof(g_agx_d3d12_probe_ps_cube_array);
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
        printf("%s CreateGraphicsPipelineState hr=0x%08lx\n", step, (unsigned long)hr);
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
        up.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
        for (UINT face = 0; face < 12; ++face) for (UINT mip = 0; mip < 2; ++mip) {
            const UINT subresource = mip + face * 2;
            up.PlacedFootprint.Offset = 512 + subresource * 1024;
            up.PlacedFootprint.Footprint.Width = 4u >> mip;
            up.PlacedFootprint.Footprint.Height = 4u >> mip;
            tex_loc.SubresourceIndex = subresource;
            ID3D12GraphicsCommandList_CopyTextureRegion(list, &tex_loc, 0, 0, 0, &up, NULL);
        }
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
        hr = run_list(queue, fence, list, base + 1, step);
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            bad = 0;
            for (UINT y = 0; y < 64; ++y) for (UINT x = 0; x < 64; ++x) {
                /* Integer equivalent of floor(3*(x+.5)/64), plus grid row. */
                const UINT face = ((6u * x + 3u) / 128u) + (y >= 32 ? 3u : 0u);
                const unsigned char expected[4] = {(unsigned char)(17 + 23 * face),
                    (unsigned char)(31 + 67 * selected_cube), (unsigned char)(43 + 89 * selected_mip),
                    (unsigned char)(151 + 7 * face + 11 * selected_cube + 13 * selected_mip)};
                const unsigned char *actual = bytes + y * 256 + x * 4;
                if (memcmp(actual, expected, 4)) {
                    if (!bad) {
                        first_bad = y * 64 + x;
                        printf("%s first mismatch xy=%u,%u face=%u got=%u,%u,%u,%u expected=%u,%u,%u,%u\n",
                            step, x, y, face, actual[0], actual[1], actual[2], actual[3],
                            expected[0], expected[1], expected[2], expected[3]);
                    }
                    ++bad;
                }
            }
            printf("%s exactRGBA pixels=4096 bad=%u first_bad=%u cube=%u mip=%u first=%u,%u,%u,%u last=%u,%u,%u,%u\n",
                step, bad, first_bad, selected_cube, selected_mip, bytes[0], bytes[1], bytes[2], bytes[3],
                bytes[16380], bytes[16381], bytes[16382], bytes[16383]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_%s: %s hr=0x%08lx\n", step, SUCCEEDED(hr) && bad == 0 ? "PASS" : "FAIL", (unsigned long)hr);
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
    return !(SUCCEEDED(hr) && bad == 0);
}

/* C6f (D3D12-C6-graphics.md): depth testing -- a D32_FLOAT depth buffer
 * cleared to 1.0, a near green triangle (z 0.25) drawn first, then a far
 * blue full-target quad (z 0.75) with DepthFunc LESS; PASS = centre green
 * (the far quad fails there), a point outside the triangle blue. */
static int draw_textured(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_textured_impl(d, q, f, b, 0); }
static int step_c17c(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_textured_impl(d, q, f, b, 1); }
static int step_c18a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_textured_impl(d, q, f, b, 2); }
static int step_c18b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_textured_impl(d, q, f, b, 3); }

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

/* C8C (placed resources): a 64 KiB buffer placed at offset 64 KiB in a
 * 128 KiB UPLOAD heap, written through Map, copied on the GPU into a
 * committed READBACK buffer and compared. Signals base + 1. */
static int placed_buffer(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_HEAP_DESC heap_desc = {0};
    D3D12_RESOURCE_DESC desc = {0};
    ID3D12Heap *heap = NULL;
    ID3D12Resource *placed = NULL;
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    unsigned char *bytes = NULL;
    unsigned bad = 65536;
    heap_desc.SizeInBytes = 131072;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 65536;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HRESULT hr = readback ? ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&heap) : E_FAIL;
    printf("C8C CreateHeap hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        hr = ID3D12Device_CreatePlacedResource(device, heap, 65536, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                               &IID_ID3D12Resource, (void **)&placed);
        printf("C8C CreatePlacedResource hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) {
        hr = ID3D12Resource_Map(placed, 0, NULL, (void **)&bytes);
        printf("C8C Map hr=0x%08lx gpuva=0x%llx\n", (unsigned long)hr, (unsigned long long)ID3D12Resource_GetGPUVirtualAddress(placed));
    }
    if (SUCCEEDED(hr)) {
        for (unsigned i = 0; i < 65536; ++i) bytes[i] = (unsigned char)(i * 11u + 9u);
        ID3D12Resource_Unmap(placed, 0, NULL);
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, placed, 0, 65536);
        hr = run_list(queue, fence, list, base + 1, "C8C copy");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 65536};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            bad = 0;
            for (unsigned i = 0; i < 65536; ++i) bad += bytes[i] != (unsigned char)(i * 11u + 9u);
            printf("C8C readback bad=%u\n", bad);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_C8C: %s\n", SUCCEEDED(hr) && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (placed) ID3D12Resource_Release(placed);
    if (heap) ID3D12Heap_Release(heap);
    if (readback) ID3D12Resource_Release(readback);
    return !(SUCCEEDED(hr) && bad == 0);
}

/* C9 (D3D12-C9-compute-views.md): one dispatch reads a 2x2 texture SRV
 * through a static point sampler and writes a 64x64 RWTexture2D (RGBA8,
 * ALLOW_UNORDERED_ACCESS) and a typed RWBuffer<uint> (R32_UINT buffer UAV);
 * C9A = the image shows the four texels as quadrants (texture SRV + sampler
 * in compute, texture UAV), C9B = typed[y * 64 + x] == x + 1000 y. */
static int compute_images(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC dst_desc = {0}, tex_desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 1}};
    D3D12_ROOT_PARAMETER parameter = {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE};
    D3D12_STATIC_SAMPLER_DESC sampler = {D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                         D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 1,
                                         D3D12_COMPARISON_FUNC_NEVER, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, 0.0f,
                                         D3D12_FLOAT32_MAX, 0, 0, D3D12_SHADER_VISIBILITY_ALL};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12DescriptorHeap *views = NULL;
    ID3D12Resource *dst = NULL, *texture = NULL;
    ID3D12Resource *typed = uav_buffer(device);
    ID3D12Resource *upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *image_back = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *typed_back = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    unsigned char *bytes = NULL;
    int quadrants_ok = 0;
    unsigned typed_bad = 4096;
    parameter.DescriptorTable.NumDescriptorRanges = 2;
    parameter.DescriptorTable.pDescriptorRanges = ranges;
    dst_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    dst_desc.Width = 64;
    dst_desc.Height = 64;
    dst_desc.DepthOrArraySize = 1;
    dst_desc.MipLevels = 1;
    dst_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst_desc.SampleDesc.Count = 1;
    dst_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    tex_desc = dst_desc;
    tex_desc.Width = 2;
    tex_desc.Height = 2;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    HRESULT hr = typed && upload && image_back && typed_back ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        const unsigned char row0[8] = {0, 255, 0, 255, 0, 0, 255, 255}, row1[8] = {255, 255, 255, 255, 0, 0, 0, 255};
        memcpy(bytes, row0, sizeof(row0));
        memcpy(bytes + 256, row1, sizeof(row1));
        ID3D12Resource_Unmap(upload, 0, NULL);
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &dst_desc,
                                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource,
                                                  (void **)&dst);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &tex_desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
                                                  (void **)&texture);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&views);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start = {0};
    if (SUCCEEDED(hr)) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu;
        D3D12_UNORDERED_ACCESS_VIEW_DESC typed_desc = {DXGI_FORMAT_R32_UINT, D3D12_UAV_DIMENSION_BUFFER};
        const UINT step = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        typed_desc.Buffer.NumElements = 4096;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(views, &cpu);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(views, &gpu_start);
        ID3D12Device_CreateShaderResourceView(device, texture, NULL, cpu);
        cpu.ptr += step;
        ID3D12Device_CreateUnorderedAccessView(device, dst, NULL, NULL, cpu);
        cpu.ptr += step;
        ID3D12Device_CreateUnorderedAccessView(device, typed, NULL, &typed_desc, cpu);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    printf("C9 resources/views/root hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_img;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_img);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C9 CreateComputePipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        D3D12_TEXTURE_COPY_LOCATION up = {0}, tex_loc = {0}, dst_loc = {0}, back = {0};
        up.pResource = upload;
        up.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        up.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        up.PlacedFootprint.Footprint.Width = 2;
        up.PlacedFootprint.Footprint.Height = 2;
        up.PlacedFootprint.Footprint.Depth = 1;
        up.PlacedFootprint.Footprint.RowPitch = 256;
        tex_loc.pResource = texture;
        tex_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_loc.pResource = dst;
        dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        back.pResource = image_back;
        back.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        back.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        back.PlacedFootprint.Footprint.Width = 64;
        back.PlacedFootprint.Footprint.Height = 64;
        back.PlacedFootprint.Footprint.Depth = 1;
        back.PlacedFootprint.Footprint.RowPitch = 256;
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &tex_loc, 0, 0, 0, &up, NULL);
        transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &views);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpu_start);
        ID3D12GraphicsCommandList_Dispatch(list, 8, 8, 1);
        transition(list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(list, typed, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &back, 0, 0, 0, &dst_loc, NULL);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, typed_back, 0, typed, 0, 16384);
        hr = run_list(queue, fence, list, base + 1, "C9 dispatch");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        hr = ID3D12Resource_Map(image_back, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            const unsigned char *tl = bytes + 16 * 256 + 16 * 4, *tr = bytes + 16 * 256 + 48 * 4;
            const unsigned char *bl = bytes + 48 * 256 + 16 * 4, *br = bytes + 48 * 256 + 48 * 4;
            quadrants_ok = tl[0] == 0 && tl[1] == 255 && tl[2] == 0 && tr[0] == 0 && tr[1] == 0 && tr[2] == 255 &&
                           bl[0] == 255 && bl[1] == 255 && bl[2] == 255 && br[0] == 0 && br[1] == 0 && br[2] == 0;
            printf("C9A quadrants tl=%u,%u,%u tr=%u,%u,%u bl=%u,%u,%u br=%u,%u,%u\n", tl[0], tl[1], tl[2], tr[0], tr[1],
                   tr[2], bl[0], bl[1], bl[2], br[0], br[1], br[2]);
            ID3D12Resource_Unmap(image_back, 0, NULL);
        }
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        const UINT *dwords = NULL;
        hr = ID3D12Resource_Map(typed_back, 0, &all, (void **)&dwords);
        if (SUCCEEDED(hr) && dwords) {
            typed_bad = 0;
            for (unsigned i = 0; i < 4096; ++i) typed_bad += dwords[i] != (i % 64u) + (i / 64u) * 1000u;
            printf("C9B typed readback bad=%u first=%u,%u\n", typed_bad, dwords[0], dwords[65]);
            ID3D12Resource_Unmap(typed_back, 0, NULL);
        }
    }
    printf("AGX_D3D12_C9A: %s\n", SUCCEEDED(hr) && quadrants_ok ? "PASS" : "FAIL");
    printf("AGX_D3D12_C9B: %s\n", SUCCEEDED(hr) && typed_bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (views) ID3D12DescriptorHeap_Release(views);
    if (texture) ID3D12Resource_Release(texture);
    if (dst) ID3D12Resource_Release(dst);
    if (typed_back) ID3D12Resource_Release(typed_back);
    if (image_back) ID3D12Resource_Release(image_back);
    if (upload) ID3D12Resource_Release(upload);
    if (typed) ID3D12Resource_Release(typed);
    return !(SUCCEEDED(hr) && quadrants_ok && typed_bad == 0);
}

/* C10 (D3D12-C10-uav-clears.md): ClearUnorderedAccessViewUint of a raw
 * buffer view (all words 0x12345678, then elements [100, 200) = 7) and
 * ClearUnorderedAccessViewFloat of a 64x64 RGBA8 texture UAV (blue, then
 * rect (0,0)-(32,32) red); C10A = buffer words, C10B = texture pixels. */
static int clear_uavs(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC tex_desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC gpu_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_HEAP_DESC cpu_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    ID3D12DescriptorHeap *gpu_heap = NULL, *cpu_heap = NULL;
    ID3D12Resource *texture = NULL;
    ID3D12Resource *buffer = uav_buffer(device);
    ID3D12Resource *buffer_back = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *image_back = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu0 = {0}, cpu1 = {0}, vis0 = {0}, vis1 = {0};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu0 = {0}, gpu1 = {0};
    unsigned buffer_bad = 16384;
    int pixels_ok = 0;
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Width = 64;
    tex_desc.Height = 64;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels = 1;
    tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HRESULT hr = buffer && buffer_back && image_back ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &tex_desc,
                                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource,
                                                  (void **)&texture);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &gpu_desc, &IID_ID3D12DescriptorHeap, (void **)&gpu_heap);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &cpu_desc, &IID_ID3D12DescriptorHeap, (void **)&cpu_heap);
    if (SUCCEEDED(hr)) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC raw = {DXGI_FORMAT_R32_TYPELESS, D3D12_UAV_DIMENSION_BUFFER};
        const UINT step = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        raw.Buffer.NumElements = 16384;
        raw.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap, &cpu0);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu_heap, &vis0);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(gpu_heap, &gpu0);
        cpu1.ptr = cpu0.ptr + step;
        vis1.ptr = vis0.ptr + step;
        gpu1.ptr = gpu0.ptr + step;
        ID3D12Device_CreateUnorderedAccessView(device, buffer, NULL, &raw, cpu0);
        ID3D12Device_CreateUnorderedAccessView(device, buffer, NULL, &raw, vis0);
        ID3D12Device_CreateUnorderedAccessView(device, texture, NULL, NULL, cpu1);
        ID3D12Device_CreateUnorderedAccessView(device, texture, NULL, NULL, vis1);
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    printf("C10 resources/views hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        const UINT word[4] = {0x12345678u, 0, 0, 0}, seven[4] = {7u, 0, 0, 0};
        const FLOAT blue[4] = {0.0f, 0.0f, 1.0f, 1.0f}, red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        const D3D12_RECT range = {100, 0, 200, 1}, corner = {0, 0, 32, 32};
        D3D12_TEXTURE_COPY_LOCATION src = {0}, back = {0};
        src.pResource = texture;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        back.pResource = image_back;
        back.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        back.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        back.PlacedFootprint.Footprint.Width = 64;
        back.PlacedFootprint.Footprint.Height = 64;
        back.PlacedFootprint.Footprint.Depth = 1;
        back.PlacedFootprint.Footprint.RowPitch = 256;
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &gpu_heap);
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(list, gpu0, cpu0, buffer, word, 0, NULL);
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(list, gpu0, cpu0, buffer, seven, 1, &range);
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(list, gpu1, cpu1, texture, blue, 0, NULL);
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(list, gpu1, cpu1, texture, red, 1, &corner);
        transition(list, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(list, texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, buffer_back, 0, buffer, 0, 65536);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &back, 0, 0, 0, &src, NULL);
        hr = run_list(queue, fence, list, base + 1, "C10 clears");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 65536};
        const UINT *dwords = NULL;
        hr = ID3D12Resource_Map(buffer_back, 0, &all, (void **)&dwords);
        if (SUCCEEDED(hr) && dwords) {
            buffer_bad = 0;
            for (unsigned i = 0; i < 16384; ++i) buffer_bad += dwords[i] != (i >= 100 && i < 200 ? 7u : 0x12345678u);
            printf("C10A buffer bad=%u words=0x%08x,0x%08x,0x%08x\n", buffer_bad, dwords[0], dwords[150], dwords[300]);
            ID3D12Resource_Unmap(buffer_back, 0, NULL);
        }
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        const unsigned char *bytes = NULL;
        hr = ID3D12Resource_Map(image_back, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            const unsigned char *in = bytes + 8 * 256 + 8 * 4, *out = bytes + 48 * 256 + 48 * 4;
            pixels_ok = in[0] == 255 && in[1] == 0 && in[2] == 0 && in[3] == 255 && out[0] == 0 && out[1] == 0 &&
                        out[2] == 255 && out[3] == 255;
            printf("C10B texture in=%u,%u,%u,%u out=%u,%u,%u,%u\n", in[0], in[1], in[2], in[3], out[0], out[1], out[2], out[3]);
            ID3D12Resource_Unmap(image_back, 0, NULL);
        }
    }
    printf("AGX_D3D12_C10A: %s\n", SUCCEEDED(hr) && buffer_bad == 0 ? "PASS" : "FAIL");
    printf("AGX_D3D12_C10B: %s\n", SUCCEEDED(hr) && pixels_ok ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (cpu_heap) ID3D12DescriptorHeap_Release(cpu_heap);
    if (gpu_heap) ID3D12DescriptorHeap_Release(gpu_heap);
    if (texture) ID3D12Resource_Release(texture);
    if (image_back) ID3D12Resource_Release(image_back);
    if (buffer_back) ID3D12Resource_Release(buffer_back);
    if (buffer) ID3D12Resource_Release(buffer);
    return !(SUCCEEDED(hr) && buffer_bad == 0 && pixels_ok);
}

/* C12 (measurement): COPY and COMPUTE queues -- the copy queue uploads a
 * pattern (5i + 1) into a DEFAULT buffer and signals 1; the compute queue
 * waits for 1, runs the C5 dispatch (3i + 7) into a second buffer and
 * signals 2; the direct queue waits for 2 and copies both to READBACK.
 * C12A = copy-queue data, C12B = compute-queue data. */
static int queue_types(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_COMMAND_QUEUE_DESC copy_desc = {D3D12_COMMAND_LIST_TYPE_COPY}, compute_desc = {D3D12_COMMAND_LIST_TYPE_COMPUTE};
    D3D12_ROOT_PARAMETER parameter = {D3D12_ROOT_PARAMETER_TYPE_UAV};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12CommandQueue *copy_queue = NULL, *compute_queue = NULL;
    ID3D12CommandAllocator *allocators[3] = {NULL, NULL, NULL};
    ID3D12GraphicsCommandList *lists[3] = {NULL, NULL, NULL};
    ID3D12Resource *upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    /* EXP1375: a COPY list may only use COMMON/COPY states (Close refused the
     * UAV transition), so the copy target is a plain DEFAULT buffer */
    ID3D12Resource *copied = committed_buffer(device, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    ID3D12Resource *computed = uav_buffer(device);
    ID3D12Resource *readback_a = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *readback_b = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    const D3D12_COMMAND_LIST_TYPE types[3] = {D3D12_COMMAND_LIST_TYPE_COPY, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_DIRECT};
    unsigned bad_a = 16384, bad_b = 16384;
    UINT64 value = 0;
    UINT *dwords = NULL;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    HRESULT hr = upload && copied && computed && readback_a && readback_b ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandQueue(device, &copy_desc, &IID_ID3D12CommandQueue, (void **)&copy_queue);
    printf("C12 CreateCommandQueue COPY hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandQueue(device, &compute_desc, &IID_ID3D12CommandQueue, (void **)&compute_queue);
    printf("C12 CreateCommandQueue COMPUTE hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&dwords);
    if (SUCCEEDED(hr)) {
        for (unsigned i = 0; i < 16384; ++i) dwords[i] = 5u * i + 1u;
        ID3D12Resource_Unmap(upload, 0, NULL);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
    }
    for (int i = 0; i < 3 && SUCCEEDED(hr); ++i) {
        hr = ID3D12Device_CreateCommandAllocator(device, types[i], &IID_ID3D12CommandAllocator, (void **)&allocators[i]);
        if (SUCCEEDED(hr))
            hr = ID3D12Device_CreateCommandList(device, 0, types[i], allocators[i], i == 1 ? pso : NULL,
                                                &IID_ID3D12GraphicsCommandList, (void **)&lists[i]);
    }
    printf("C12 pipeline/allocators/lists hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *one[1];
        ID3D12GraphicsCommandList_CopyBufferRegion(lists[0], copied, 0, upload, 0, 65536); /* COMMON -> COPY_DEST promotes */
        ID3D12GraphicsCommandList_SetComputeRootSignature(lists[1], root);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(lists[1], 0, ID3D12Resource_GetGPUVirtualAddress(computed));
        ID3D12GraphicsCommandList_Dispatch(lists[1], 256, 1, 1);
        transition(lists[1], computed, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        ID3D12GraphicsCommandList_CopyBufferRegion(lists[2], readback_a, 0, copied, 0, 65536);
        ID3D12GraphicsCommandList_CopyBufferRegion(lists[2], readback_b, 0, computed, 0, 65536);
        for (int i = 0; i < 3 && SUCCEEDED(hr); ++i) {
            hr = ID3D12GraphicsCommandList_Close(lists[i]);
            printf("C12 close list %d (type %d) hr=0x%08lx\n", i, (int)types[i], (unsigned long)hr);
        }
        if (SUCCEEDED(hr)) {
            one[0] = (ID3D12CommandList *)lists[0];
            ID3D12CommandQueue_ExecuteCommandLists(copy_queue, 1, one);
            hr = ID3D12CommandQueue_Signal(copy_queue, fence, base + 1);
            printf("C12 copy queue execute+signal hr=0x%08lx\n", (unsigned long)hr);
        }
        if (SUCCEEDED(hr)) {
            hr = ID3D12CommandQueue_Wait(compute_queue, fence, base + 1);
            printf("C12 compute queue Wait hr=0x%08lx\n", (unsigned long)hr);
        }
        if (SUCCEEDED(hr)) {
            one[0] = (ID3D12CommandList *)lists[1];
            ID3D12CommandQueue_ExecuteCommandLists(compute_queue, 1, one);
            hr = ID3D12CommandQueue_Signal(compute_queue, fence, base + 2);
            printf("C12 compute queue execute+signal hr=0x%08lx\n", (unsigned long)hr);
        }
        if (SUCCEEDED(hr)) hr = ID3D12CommandQueue_Wait(queue, fence, base + 2);
        if (SUCCEEDED(hr)) {
            one[0] = (ID3D12CommandList *)lists[2];
            ID3D12CommandQueue_ExecuteCommandLists(queue, 1, one);
            hr = ID3D12CommandQueue_Signal(queue, fence, base + 3);
            for (int i = 0; i < 300 && (value = ID3D12Fence_GetCompletedValue(fence)) < base + 3; ++i) Sleep(10);
            printf("C12 direct queue execute+signal hr=0x%08lx completed %llu\n", (unsigned long)hr, (unsigned long long)value);
        }
    }
    if (SUCCEEDED(hr) && value >= base + 3) {
        D3D12_RANGE all = {0, 65536};
        if (SUCCEEDED(ID3D12Resource_Map(readback_a, 0, &all, (void **)&dwords)) && dwords) {
            bad_a = 0;
            for (unsigned i = 0; i < 16384; ++i) bad_a += dwords[i] != 5u * i + 1u;
            ID3D12Resource_Unmap(readback_a, 0, NULL);
        }
        if (SUCCEEDED(ID3D12Resource_Map(readback_b, 0, &all, (void **)&dwords)) && dwords) {
            bad_b = 0;
            for (unsigned i = 0; i < 16384; ++i) bad_b += dwords[i] != 3u * i + 7u;
            ID3D12Resource_Unmap(readback_b, 0, NULL);
        }
        printf("C12 readback copy bad=%u compute bad=%u\n", bad_a, bad_b);
    }
    printf("AGX_D3D12_C12A: %s\n", bad_a == 0 ? "PASS" : "FAIL");
    printf("AGX_D3D12_C12B: %s\n", bad_b == 0 ? "PASS" : "FAIL");
    for (int i = 0; i < 3; ++i) {
        if (lists[i]) ID3D12GraphicsCommandList_Release(lists[i]);
        if (allocators[i]) ID3D12CommandAllocator_Release(allocators[i]);
    }
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (compute_queue) ID3D12CommandQueue_Release(compute_queue);
    if (copy_queue) ID3D12CommandQueue_Release(copy_queue);
    if (readback_b) ID3D12Resource_Release(readback_b);
    if (readback_a) ID3D12Resource_Release(readback_a);
    if (computed) ID3D12Resource_Release(computed);
    if (copied) ID3D12Resource_Release(copied);
    if (upload) ID3D12Resource_Release(upload);
    return !(bad_a == 0 && bad_b == 0);
}

/* C14B: the C5 dispatch (3i + 7 over 64 KiB) through ExecuteIndirect with
 * D3D12_DISPATCH_ARGUMENTS {256, 1, 1} from an UPLOAD buffer. */
static int compute_indirect(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_ROOT_PARAMETER parameter = {D3D12_ROOT_PARAMETER_TYPE_UAV};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    D3D12_INDIRECT_ARGUMENT_DESC arg = {D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH};
    D3D12_COMMAND_SIGNATURE_DESC sd = {12, 1, &arg, 0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12CommandSignature *signature = NULL;
    ID3D12Resource *uav = uav_buffer(device);
    ID3D12Resource *args = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    UINT *dwords = NULL;
    unsigned bad = 16384;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    HRESULT hr = uav && args && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(args, 0, NULL, (void **)&dwords);
    if (SUCCEEDED(hr)) {
        dwords[16] = 256;
        dwords[17] = 1;
        dwords[18] = 1;
        ID3D12Resource_Unmap(args, 0, NULL);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandSignature(device, &sd, NULL, &IID_ID3D12CommandSignature, (void **)&signature);
    printf("C14B pipeline/signature hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 0, ID3D12Resource_GetGPUVirtualAddress(uav));
        ID3D12GraphicsCommandList_ExecuteIndirect(list, signature, 1, args, 64, NULL, 0);
        transition(list, uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, uav, 0, 65536);
        hr = run_list(queue, fence, list, base + 1, "C14B dispatch");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 65536};
        if (SUCCEEDED(ID3D12Resource_Map(readback, 0, &all, (void **)&dwords)) && dwords) {
            bad = 0;
            for (unsigned i = 0; i < 16384; ++i) bad += dwords[i] != 3u * i + 7u;
            printf("C14B readback bad=%u\n", bad);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_C14B: %s\n", SUCCEEDED(hr) && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (signature) ID3D12CommandSignature_Release(signature);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (readback) ID3D12Resource_Release(readback);
    if (args) ID3D12Resource_Release(args);
    if (uav) ID3D12Resource_Release(uav);
    return !(SUCCEEDED(hr) && bad == 0);
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
                (void)compute_srv(device, queue, fence, 0);
            /* C6a is independent of the compute steps (it needs C4b only) */
            if (SUCCEEDED(c4) && value == 2) {
                UINT64 done = ID3D12Fence_GetCompletedValue(fence);
                if (done >= 3 && texture_clear_copy(device, queue, fence, done) == 0)
                    (void)draw_triangle(device, queue, fence, ID3D12Fence_GetCompletedValue(fence), 0);
            }
            if (list) ID3D12GraphicsCommandList_Release(list);
            if (allocator) ID3D12CommandAllocator_Release(allocator);
        }
        ID3D12Fence_Release(fence);
    }
    ID3D12CommandQueue_Release(queue);
    return FAILED(hr);
}

/* EXP1369: a refused step removes its device, so the later steps run on a
 * fresh device each (device, direct queue, fence from 0). */
/* C17A: equal logical UAV registers in two spaces must never alias. */
static int compute_spaces(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_ROOT_PARAMETER parameters[3] = {{D3D12_ROOT_PARAMETER_TYPE_UAV},
        {D3D12_ROOT_PARAMETER_TYPE_UAV}, {D3D12_ROOT_PARAMETER_TYPE_CBV}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {3, parameters};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    ID3D12Resource *outputs[2] = {uav_buffer(device), uav_buffer(device)};
    ID3D12Resource *backs[2] = {
        committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST),
        committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST)};
    ID3D12Resource *init = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *cb = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    UINT *words = NULL;
    UINT first[2] = {0}, last[2] = {0}, bad = 32768;
    HRESULT hr = outputs[0] && outputs[1] && backs[0] && backs[1] && init && cb ? S_OK : E_FAIL;
    for (UINT p = 0; p < 3; ++p) parameters[p].Descriptor.RegisterSpace = p + 1;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(init, 0, NULL, (void **)&words);
    if (SUCCEEDED(hr)) {
        for (UINT i = 0; i < 16384; ++i) words[i] = 0xdeadbeefu;
        ID3D12Resource_Unmap(init, 0, NULL);
        hr = ID3D12Resource_Map(cb, 0, NULL, (void **)&words);
    }
    if (SUCCEEDED(hr)) {
        words[0] = 17; words[1] = 29; words[2] = words[3] = 0;
        ID3D12Resource_Unmap(cb, 0, NULL);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_spaces;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_spaces);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
    }
    printf("C17A root/pipeline hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
        &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        for (UINT j = 0; j < 2; ++j) {
            transition(list, outputs[j], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12GraphicsCommandList_CopyBufferRegion(list, outputs[j], 0, init, 0, 65536);
            transition(list, outputs[j], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 0, ID3D12Resource_GetGPUVirtualAddress(outputs[0]));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 1, ID3D12Resource_GetGPUVirtualAddress(outputs[1]));
        ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(list, 2, ID3D12Resource_GetGPUVirtualAddress(cb));
        ID3D12GraphicsCommandList_Dispatch(list, 256, 1, 1);
        for (UINT j = 0; j < 2; ++j) {
            transition(list, outputs[j], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ID3D12GraphicsCommandList_CopyBufferRegion(list, backs[j], 0, outputs[j], 0, 65536);
        }
        hr = run_list(queue, fence, list, base + 1, "C17A dispatch");
    }
    if (SUCCEEDED(hr)) {
        const D3D12_RANGE all = {0, 65536};
        bad = 0;
        for (UINT j = 0; j < 2; ++j) {
            words = NULL;
            hr = ID3D12Resource_Map(backs[j], 0, &all, (void **)&words);
            if (FAILED(hr) || !words) { bad = 32768; break; }
            first[j] = words[0]; last[j] = words[16383];
            for (UINT i = 0; i < 16384; ++i) bad += words[i] != (j ? 5u * i + 29u : 3u * i + 17u);
            ID3D12Resource_Unmap(backs[j], 0, NULL);
        }
        printf("C17A readback bad=%u first=%u,%u last=%u,%u\n", bad, first[0], first[1], last[0], last[1]);
    }
    printf("AGX_D3D12_C17A: %s\n", SUCCEEDED(hr) && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (blob) ID3D10Blob_Release(blob);
    if (errors) ID3D10Blob_Release(errors);
    for (UINT j = 0; j < 2; ++j) {
        if (outputs[j]) ID3D12Resource_Release(outputs[j]);
        if (backs[j]) ID3D12Resource_Release(backs[j]);
    }
    if (init) ID3D12Resource_Release(init);
    if (cb) ID3D12Resource_Release(cb);
    return !(SUCCEEDED(hr) && bad == 0);
}

static int step_c19a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_cube(d, q, f, b, 0); }
static int step_c19b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_cube(d, q, f, b, 1); }
static int step_c19c(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_cube(d, q, f, b, 2); }
static int step_c19d(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_cube(d, q, f, b, 3); }

/* C20: exact3-element typed SRVs retain their sub-dword tail. The source
 * view begins64 bytes into initialized data; both neighboring values poison
 * a wrong origin/range. Output uses the established raw-buffer UAV path. */
static int compute_typed_extent(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int wide) {
    const char *step = wide ? "C20B" : "C20A";
    const DXGI_FORMAT format = wide ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R8_UINT;
    const UINT expected[4] = {wide ? 257u : 17u, wide ? 4099u : 83u, wide ? 60013u : 197u, 3u};
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {0};
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER parameters[2] = {{D3D12_ROOT_PARAMETER_TYPE_UAV}, {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {2, parameters};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12DescriptorHeap *heap = NULL;
    ID3D12Resource *upload = NULL, *init = NULL, *input = NULL, *output = NULL, *readback = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = {0};
    unsigned char *bytes = NULL;
    UINT *words = NULL;
    unsigned bad = 4;
    support.Format = format;
    HRESULT hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
    printf("%s format=%u query_hr=0x%08lx support1=0x%08lx support2=0x%08lx\n", step, (unsigned)format,
        (unsigned long)hr, (unsigned long)support.Support1, (unsigned long)support.Support2);
    if (SUCCEEDED(hr) && ((support.Support1 & D3D12_FORMAT_SUPPORT1_BUFFER) == 0 ||
                         (support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) == 0)) hr = E_NOTIMPL;
    if (SUCCEEDED(hr)) {
        upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        init = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        input = committed_buffer(device, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        output = uav_buffer(device);
        readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!upload || !init || !input || !output || !readback) hr = E_FAIL;
    }
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr) && bytes) {
        if (wide) {
            UINT16 *half = (UINT16 *)bytes;
            for (UINT i = 0; i < 32768; ++i) half[i] = 0xa55a;
            half[31] = 0x2bad; half[32] = 257; half[33] = 4099; half[34] = 60013; half[35] = 0xfedc;
        } else {
            memset(bytes, 0xa5, 65536);
            bytes[63] = 0xe1; bytes[64] = 17; bytes[65] = 83; bytes[66] = 197; bytes[67] = 0xd9;
        }
        ID3D12Resource_Unmap(upload, 0, NULL);
    } else if (SUCCEEDED(hr)) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(init, 0, NULL, (void **)&words);
    if (SUCCEEDED(hr) && words) {
        for (UINT i = 0; i < 16384; ++i) words[i] = 0xd15ea5ed;
        ID3D12Resource_Unmap(init, 0, NULL);
    } else if (SUCCEEDED(hr)) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    if (SUCCEEDED(hr)) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = {0};
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {0};
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &cpu);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &gpu);
        srv.Format = format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Buffer.FirstElement = wide ? 32u : 64u;
        srv.Buffer.NumElements = 3;
        ID3D12Device_CreateShaderResourceView(device, input, &srv, cpu);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_typed_extent;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_typed_extent);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
    }
    printf("%s resources/root/PSO hr=0x%08lx FirstElement=%u NumElements=3 sourceByteOffset=64\n", step,
        (unsigned long)hr, wide ? 32u : 64u);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
        &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_UAV};
        barrier.UAV.pResource = output;
        ID3D12GraphicsCommandList_CopyBufferRegion(list, input, 0, upload, 0, 65536);
        transition(list, input, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transition(list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, output, 0, init, 0, 65536);
        transition(list, output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 0, ID3D12Resource_GetGPUVirtualAddress(output));
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 1, gpu);
        ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
        transition(list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, output, 0, 65536);
        hr = run_list(queue, fence, list, base + 1, step);
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 65536};
        words = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&words);
        if (SUCCEEDED(hr) && words) {
            bad = 0;
            for (UINT i = 0; i < 4; ++i) bad += words[i] != expected[i];
            printf("%s readback bad=%u actual=[%u,%u,%u,%u] expected=[%u,%u,%u,%u] fence=%llu\n", step,
                bad, words[0], words[1], words[2], words[3], expected[0], expected[1], expected[2], expected[3],
                (unsigned long long)ID3D12Fence_GetCompletedValue(fence));
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_%s: %s hr=0x%08lx\n", step, SUCCEEDED(hr) && bad == 0 ? "PASS" : "FAIL", (unsigned long)hr);
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (blob) ID3D10Blob_Release(blob);
    if (errors) ID3D10Blob_Release(errors);
    if (heap) ID3D12DescriptorHeap_Release(heap);
    if (readback) ID3D12Resource_Release(readback);
    if (output) ID3D12Resource_Release(output);
    if (input) ID3D12Resource_Release(input);
    if (init) ID3D12Resource_Release(init);
    if (upload) ID3D12Resource_Release(upload);
    return !(SUCCEEDED(hr) && bad == 0);
}

static int step_c20a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return compute_typed_extent(d, q, f, b, 0); }
static int step_c20b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return compute_typed_extent(d, q, f, b, 1); }

/* C21: volumes are mip subresources, never arrays of Z subresources.
 * Expected native allocation extents below were obtained from the actual
 * Screen-free native/AIl query (see /tmp/agx-c21-probe-layout.log):
 * R32_UINT4x4x8/2mips=131072; RGBA8 32x32x4/2mips=65536 after64KiB rounding.
 * Every transfer uses the API's returned footprints, not a guessed slice pitch. */
typedef struct {
    D3D12_RESOURCE_DESC desc;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2];
    UINT rows[2];
    UINT64 row_bytes[2], total, capacity;
    ID3D12Resource *texture, *upload, *init, *readback;
} C21Volume;

static UINT c21_extent(UINT value, UINT mip) { return value >> mip ? value >> mip : 1u; }
static UINT c21_input(UINT mip, UINT x, UINT y, UINT z) {
    return 0x21000000u + mip * 0x00100000u + z * 0x1000u + y * 0x40u + x;
}
static UINT c21_output(UINT mip, UINT x, UINT y, UINT z) {
    return 0x71000000u + mip * 0x00100000u + z * 0x1000u + y * 0x40u + x;
}
static UINT64 c21_offset(const C21Volume *v, UINT mip, UINT x, UINT y, UINT z) {
    return v->fp[mip].Offset + (UINT64)z * v->fp[mip].Footprint.RowPitch * v->rows[mip] +
           (UINT64)y * v->fp[mip].Footprint.RowPitch + (UINT64)x * 4u;
}
static ID3D12Resource *c21_buffer(ID3D12Device *device, D3D12_HEAP_TYPE type, UINT64 bytes,
                                 D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap = {type};
    D3D12_RESOURCE_DESC desc = {0};
    ID3D12Resource *resource = NULL;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes; desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; desc.Flags = flags;
    HRESULT hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc, state, NULL,
                                                       &IID_ID3D12Resource, (void **)&resource);
    return SUCCEEDED(hr) ? resource : NULL;
}
static HRESULT c21_fill(ID3D12Resource *buffer, UINT64 capacity, UINT value) {
    UINT *words = NULL; D3D12_RANGE none = {0, 0};
    HRESULT hr = ID3D12Resource_Map(buffer, 0, &none, (void **)&words);
    if (SUCCEEDED(hr) && !words) hr = E_FAIL;
    if (SUCCEEDED(hr)) {
        for (UINT64 i = 0; i < capacity / 4; ++i) words[i] = value;
        ID3D12Resource_Unmap(buffer, 0, NULL);
    }
    return hr;
}
static void c21_release(C21Volume *v) {
    if (v->readback) ID3D12Resource_Release(v->readback);
    if (v->init) ID3D12Resource_Release(v->init);
    if (v->upload) ID3D12Resource_Release(v->upload);
    if (v->texture) ID3D12Resource_Release(v->texture);
}
static HRESULT c21_create(ID3D12Device *device, C21Volume *v, UINT width, UINT height, UINT depth,
                            DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, UINT64 allocation_bytes, const char *step) {
    D3D12_RESOURCE_ALLOCATION_INFO allocation = {0};
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    memset(v, 0, sizeof(*v));
    v->desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    v->desc.Width = width; v->desc.Height = height; v->desc.DepthOrArraySize = (UINT16)depth;
    v->desc.MipLevels = 2; v->desc.Format = format; v->desc.SampleDesc.Count = 1; v->desc.Flags = flags;
    ID3D12Device_GetResourceAllocationInfo(device, &allocation, 0, 1, &v->desc);
    printf("%s allocation size=%llu alignment=%llu expected_size=%llu expected_alignment=65536\n", step,
        (unsigned long long)allocation.SizeInBytes, (unsigned long long)allocation.Alignment, (unsigned long long)allocation_bytes);
    if (allocation.SizeInBytes != allocation_bytes || allocation.Alignment != 65536) return E_FAIL;
    ID3D12Device_GetCopyableFootprints(device, &v->desc, 0, 2, 0, v->fp, v->rows, v->row_bytes, &v->total);
    if (v->total < 4 || v->total > ~0ULL - 65535u) return E_FAIL;
    v->capacity = (v->total + 65535u) & ~65535ULL;
    if (v->capacity > (UINT64)(~(SIZE_T)0)) return E_FAIL;
    for (UINT mip = 0; mip < 2; ++mip) {
        const UINT w = c21_extent(width, mip), h = c21_extent(height, mip), d = c21_extent(depth, mip);
        const D3D12_SUBRESOURCE_FOOTPRINT *f = &v->fp[mip].Footprint;
        if (f->Format != format || f->Width != w || f->Height != h || f->Depth != d || v->rows[mip] != h ||
            v->row_bytes[mip] != (UINT64)w * 4 || f->RowPitch < w * 4 || f->RowPitch % 256 || v->fp[mip].Offset % 512)
            return E_FAIL;
        const UINT64 span = (UINT64)(d - 1) * f->RowPitch * v->rows[mip] + (UINT64)(h - 1) * f->RowPitch + (UINT64)w * 4;
        if (v->fp[mip].Offset > v->total || span > v->total - v->fp[mip].Offset) return E_FAIL;
        printf("%s footprint mip=%u offset=%llu extent=%ux%ux%u row_pitch=%u rows=%u row_bytes=%llu slice_pitch=%llu total=%llu capacity=%llu\n",
            step, mip, (unsigned long long)v->fp[mip].Offset, w, h, d, f->RowPitch, v->rows[mip],
            (unsigned long long)v->row_bytes[mip], (unsigned long long)((UINT64)f->RowPitch * v->rows[mip]),
            (unsigned long long)v->total, (unsigned long long)v->capacity);
    }
    HRESULT hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &v->desc,
        D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&v->texture);
    if (SUCCEEDED(hr)) {
        v->upload = c21_buffer(device, D3D12_HEAP_TYPE_UPLOAD, v->capacity, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        v->init = c21_buffer(device, D3D12_HEAP_TYPE_UPLOAD, v->capacity, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        v->readback = c21_buffer(device, D3D12_HEAP_TYPE_READBACK, v->capacity, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!v->upload || !v->init || !v->readback) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) hr = c21_fill(v->init, v->capacity, 0xccccccccu);
    printf("%s CreateCommittedResource volume/buffers hr=0x%08lx\n", step, (unsigned long)hr);
    return hr;
}
static HRESULT c21_source(C21Volume *v, int unique) {
    unsigned char *bytes = NULL; D3D12_RANGE none = {0, 0};
    HRESULT hr = ID3D12Resource_Map(v->upload, 0, &none, (void **)&bytes);
    if (SUCCEEDED(hr) && !bytes) hr = E_FAIL;
    if (SUCCEEDED(hr)) {
        memset(bytes, unique ? 0xcd : 0xa5, (SIZE_T)v->capacity);
        if (unique) for (UINT mip = 0; mip < 2; ++mip)
            for (UINT z = 0; z < v->fp[mip].Footprint.Depth; ++z)
                for (UINT y = 0; y < v->fp[mip].Footprint.Height; ++y)
                    for (UINT x = 0; x < v->fp[mip].Footprint.Width; ++x) {
                        const UINT word = c21_input(mip, x, y, z);
                        memcpy(bytes + c21_offset(v, mip, x, y, z), &word, 4);
                    }
        ID3D12Resource_Unmap(v->upload, 0, NULL);
    }
    return hr;
}
static void c21_transfer(ID3D12GraphicsCommandList *list, C21Volume *v, int upload) {
    for (UINT mip = 0; mip < 2; ++mip) {
        D3D12_TEXTURE_COPY_LOCATION tex = {0}, buffer = {0};
        tex.pResource = v->texture; tex.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; tex.SubresourceIndex = mip;
        buffer.pResource = upload ? v->upload : v->readback;
        buffer.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; buffer.PlacedFootprint = v->fp[mip];
        if (upload) ID3D12GraphicsCommandList_CopyTextureRegion(list, &tex, 0, 0, 0, &buffer, NULL);
        else ID3D12GraphicsCommandList_CopyTextureRegion(list, &buffer, 0, 0, 0, &tex, NULL);
    }
}
static HRESULT c21_commands(ID3D12Device *device, ID3D12PipelineState *pso,
                              ID3D12CommandAllocator **allocator, ID3D12GraphicsCommandList **list) {
    HRESULT hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void **)allocator);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        *allocator, pso, &IID_ID3D12GraphicsCommandList, (void **)list);
    return hr;
}
static void c21_command_release(ID3D12CommandAllocator **allocator, ID3D12GraphicsCommandList **list) {
    if (*list) ID3D12GraphicsCommandList_Release(*list);
    if (*allocator) ID3D12CommandAllocator_Release(*allocator);
    *list = NULL; *allocator = NULL;
}
/* mode0 input pattern; mode1 one UAV-written mip and other mip poison;
 * mode2 selected RTV plane red/green, every other voxel poison. Padding is
 * initialized independently to0xcc and checked across the entire buffer. */
static HRESULT c21_verify(C21Volume *v, int mode, UINT selected_mip, const char *step) {
    unsigned char *bytes = NULL, *expected = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)v->capacity);
    D3D12_RANGE all = {0, (SIZE_T)v->capacity}; UINT64 bad = 0, first = ~0ULL;
    UINT green = 0, red = 0, invalid = 0, voxels = 0;
    if (!expected) return E_OUTOFMEMORY;
    memset(expected, 0xcc, (SIZE_T)v->capacity);
    HRESULT hr = ID3D12Resource_Map(v->readback, 0, &all, (void **)&bytes);
    if (SUCCEEDED(hr) && !bytes) hr = E_FAIL;
    if (SUCCEEDED(hr)) {
        for (UINT mip = 0; mip < 2; ++mip) for (UINT z = 0; z < v->fp[mip].Footprint.Depth; ++z)
            for (UINT y = 0; y < v->fp[mip].Footprint.Height; ++y) for (UINT x = 0; x < v->fp[mip].Footprint.Width; ++x) {
                const UINT64 at = c21_offset(v, mip, x, y, z);
                UINT want = mode == 0 ? c21_input(mip, x, y, z) :
                    mode == 1 && mip == selected_mip ? c21_output(mip, x, y, z) : 0xa5a5a5a5u;
                ++voxels;
                if (mode == 2 && mip == 1 && z == 1) {
                    memcpy(&want, bytes + at, 4);
                    if (want == 0xff00ff00u) ++green;
                    else if (want == 0xff0000ffu) ++red;
                    else ++invalid;
                }
                memcpy(expected + at, &want, 4);
            }
        for (UINT64 i = 0; i < v->capacity; ++i) if (bytes[i] != expected[i]) { if (!bad) first = i; ++bad; }
        printf("%s readback mip=%u voxels=%u bad_bytes=%llu first_bad=%llu green=%u red=%u invalid=%u padding_checked=%llu\n",
            step, selected_mip, voxels, (unsigned long long)bad, (unsigned long long)first,
            green, red, invalid, (unsigned long long)(v->capacity - (UINT64)voxels * 4));
        if (bad || (mode == 2 && (green != 32 || red != 224 || invalid))) hr = E_FAIL;
        ID3D12Resource_Unmap(v->readback, 0, NULL);
    }
    HeapFree(GetProcessHeap(), 0, expected);
    return hr;
}
static int volume_copy(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    C21Volume v; ID3D12CommandAllocator *allocator = NULL; ID3D12GraphicsCommandList *list = NULL;
    HRESULT hr = c21_create(device, &v, 4, 4, 8, DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_FLAG_NONE, 131072, "C21A");
    if (SUCCEEDED(hr)) hr = c21_source(&v, 1);
    if (SUCCEEDED(hr)) hr = c21_commands(device, NULL, &allocator, &list);
    if (SUCCEEDED(hr)) {
        ID3D12GraphicsCommandList_CopyBufferRegion(list, v.readback, 0, v.init, 0, v.capacity);
        c21_transfer(list, &v, 1);
        transition(list, v.texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        c21_transfer(list, &v, 0);
        hr = run_list(queue, fence, list, base + 1, "C21A");
    }
    if (SUCCEEDED(hr)) hr = c21_verify(&v, 0, 0, "C21A");
    printf("AGX_D3D12_C21A: %s hr=0x%08lx\n", SUCCEEDED(hr) ? "PASS" : "FAIL", (unsigned long)hr);
    c21_command_release(&allocator, &list); c21_release(&v); return FAILED(hr);
}
static int volume_compute(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int store) {
    const char *step = store ? "C21C" : "C21B";
    C21Volume v; ID3D12CommandAllocator *allocator = NULL; ID3D12GraphicsCommandList *list = NULL;
    ID3D12DescriptorHeap *heap = NULL; ID3D12RootSignature *root = NULL; ID3D12PipelineState *pso = NULL;
    ID3DBlob *blob = NULL, *errors = NULL; ID3D12Resource *output = NULL;
    D3D12_DESCRIPTOR_RANGE range = {store ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER parameters[2] = {{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}, {store ? D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS : D3D12_ROOT_PARAMETER_TYPE_UAV}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {2, parameters};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {0}; D3D12_GPU_DESCRIPTOR_HANDLE gpu = {0};
    HRESULT hr = c21_create(device, &v, 4, 4, 8, DXGI_FORMAT_R32_UINT,
        store ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE, 131072, step);
    parameters[0].DescriptorTable.NumDescriptorRanges = 1; parameters[0].DescriptorTable.pDescriptorRanges = &range;
    if (store) { parameters[1].Constants.ShaderRegister = 0; parameters[1].Constants.Num32BitValues = 4; }
    else parameters[1].Descriptor.ShaderRegister = 0;
    if (SUCCEEDED(hr)) hr = c21_source(&v, !store);
    if (SUCCEEDED(hr) && !store) {
        output = c21_buffer(device, D3D12_HEAP_TYPE_DEFAULT, v.capacity, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (!output) hr = E_FAIL;
        if (SUCCEEDED(hr)) hr = c21_fill(v.init, v.capacity, 0xd15ea5edu);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    if (SUCCEEDED(hr)) {
        UINT stride = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &cpu);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &gpu);
        for (UINT mip = 0; mip < 2; ++mip) {
            D3D12_CPU_DESCRIPTOR_HANDLE at = {cpu.ptr + (SIZE_T)mip * stride};
            if (store) {
                D3D12_UNORDERED_ACCESS_VIEW_DESC view = {0}; view.Format = DXGI_FORMAT_R32_UINT;
                view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                view.Texture3D.MipSlice = mip; view.Texture3D.FirstWSlice = 0; view.Texture3D.WSize = ~0u;
                ID3D12Device_CreateUnorderedAccessView(device, v.texture, NULL, &view, at);
            } else {
                D3D12_SHADER_RESOURCE_VIEW_DESC view = {0}; view.Format = DXGI_FORMAT_R32_UINT;
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D; view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                view.Texture3D.MostDetailedMip = mip; view.Texture3D.MipLevels = 1;
                ID3D12Device_CreateShaderResourceView(device, v.texture, &view, at);
            }
        }
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = store ? g_agx_d3d12_probe_cs_volume_store : g_agx_d3d12_probe_cs_volume_load;
        pso_desc.CS.BytecodeLength = store ? sizeof(g_agx_d3d12_probe_cs_volume_store) : sizeof(g_agx_d3d12_probe_cs_volume_load);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
    }
    printf("%s resources/views/root/PSO hr=0x%08lx\n", step, (unsigned long)hr);
    for (UINT mip = 0; mip < 2 && SUCCEEDED(hr); ++mip) {
        const UINT w = c21_extent(4, mip), h = c21_extent(4, mip), d = c21_extent(8, mip);
        const UINT params[4] = {w, h, d, mip};
        const UINT stride = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_GPU_DESCRIPTOR_HANDLE selected = {gpu.ptr + (UINT64)mip * stride};
        hr = c21_commands(device, pso, &allocator, &list);
        if (SUCCEEDED(hr)) {
            if (store || mip == 0) {
                if (store && mip) transition(list, v.texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                c21_transfer(list, &v, 1);
                transition(list, v.texture, D3D12_RESOURCE_STATE_COPY_DEST,
                    store ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            if (store) ID3D12GraphicsCommandList_CopyBufferRegion(list, v.readback, 0, v.init, 0, v.capacity);
            else {
                transition(list, output, mip ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
                ID3D12GraphicsCommandList_CopyBufferRegion(list, output, 0, v.init, 0, v.capacity);
                transition(list, output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &heap);
            ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, selected);
            if (store) ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list, 1, 4, params, 0);
            else ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 1, ID3D12Resource_GetGPUVirtualAddress(output));
            ID3D12GraphicsCommandList_Dispatch(list, 1, h, d);
            D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_UAV}; barrier.UAV.pResource = store ? v.texture : output;
            ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
            transition(list, store ? v.texture : output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            if (store) c21_transfer(list, &v, 0);
            else ID3D12GraphicsCommandList_CopyBufferRegion(list, v.readback, 0, output, 0, v.capacity);
            hr = run_list(queue, fence, list, base + 1 + mip, step);
        }
        if (SUCCEEDED(hr) && store) hr = c21_verify(&v, 1, mip, step);
        else if (SUCCEEDED(hr)) {
            UINT *words = NULL, bad = 0, first = ~0u; D3D12_RANGE all = {0, (SIZE_T)v.capacity};
            hr = ID3D12Resource_Map(v.readback, 0, &all, (void **)&words);
            if (SUCCEEDED(hr) && !words) hr = E_FAIL;
            if (SUCCEEDED(hr)) {
                const UINT dimensions[4] = {w, h, d, 1};
                for (UINT64 i = 0; i < v.capacity / 4; ++i) {
                    UINT want = 0xd15ea5edu;
                    if (i < 4) want = dimensions[i];
                    else if (i - 4 < (UINT64)w * h * d) {
                        UINT index = (UINT)i - 4, x = index % w, y = (index / w) % h, z = index / (w * h);
                        want = c21_input(mip, x, y, z);
                    }
                    if (words[i] != want) { if (!bad) first = (UINT)i; ++bad; }
                }
                printf("C21B mip=%u dims=[%u,%u,%u,%u] expected=[%u,%u,%u,1] voxels=%u bad_words=%u first_bad=%u\n",
                    mip, words[0], words[1], words[2], words[3], w, h, d, w * h * d, bad, first);
                ID3D12Resource_Unmap(v.readback, 0, NULL);
                if (bad) hr = E_FAIL;
            }
        }
        c21_command_release(&allocator, &list);
    }
    printf("AGX_D3D12_%s: %s hr=0x%08lx\n", step, SUCCEEDED(hr) ? "PASS" : "FAIL", (unsigned long)hr);
    c21_command_release(&allocator, &list);
    if (pso) ID3D12PipelineState_Release(pso); if (root) ID3D12RootSignature_Release(root);
    if (blob) ID3D10Blob_Release(blob); if (errors) ID3D10Blob_Release(errors);
    if (heap) ID3D12DescriptorHeap_Release(heap); if (output) ID3D12Resource_Release(output);
    c21_release(&v); return FAILED(hr);
}
static HRESULT c23_create(ID3D12Device *device, C21Volume *v, UINT width, UINT height, UINT depth,
                            DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, UINT64 allocation_bytes, const char *step) {
    D3D12_RESOURCE_ALLOCATION_INFO allocation = {0};
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    memset(v, 0, sizeof(*v));
    v->desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    v->desc.Width = width; v->desc.Height = height; v->desc.DepthOrArraySize = (UINT16)depth;
    v->desc.MipLevels = 2; v->desc.Format = format; v->desc.SampleDesc.Count = 1; v->desc.Flags = flags;
    ID3D12Device_GetResourceAllocationInfo(device, &allocation, 0, 1, &v->desc);
    printf("%s allocation size=%llu alignment=%llu expected_size=%llu expected_alignment=65536\n", step,
        (unsigned long long)allocation.SizeInBytes, (unsigned long long)allocation.Alignment, (unsigned long long)allocation_bytes);
    if (allocation.SizeInBytes != allocation_bytes || allocation.Alignment != 65536) return E_FAIL;
    ID3D12Device_GetCopyableFootprints(device, &v->desc, 0, 2, 0, v->fp, v->rows, v->row_bytes, &v->total);
    if (v->total < 4 || v->total > ~0ULL - 65535u) return E_FAIL;
    v->capacity = (v->total + 65535u) & ~65535ULL;
    if (v->capacity > (UINT64)(~(SIZE_T)0)) return E_FAIL;
    for (UINT mip = 0; mip < 2; ++mip) {
        const UINT w = c21_extent(width, mip), h = c21_extent(height, mip), d = c21_extent(depth, mip);
        const D3D12_SUBRESOURCE_FOOTPRINT *f = &v->fp[mip].Footprint;
        if (f->Format != format || f->Width != w || f->Height != h || f->Depth != d || v->rows[mip] != h ||
            v->row_bytes[mip] != (UINT64)w * 4 || f->RowPitch < w * 4 || f->RowPitch % 256 || v->fp[mip].Offset % 512)
            return E_FAIL;
        const UINT64 span = (UINT64)(d - 1) * f->RowPitch * v->rows[mip] + (UINT64)(h - 1) * f->RowPitch + (UINT64)w * 4;
        if (v->fp[mip].Offset > v->total || span > v->total - v->fp[mip].Offset) return E_FAIL;
        printf("%s footprint mip=%u offset=%llu extent=%ux%ux%u row_pitch=%u rows=%u row_bytes=%llu slice_pitch=%llu total=%llu capacity=%llu\n",
            step, mip, (unsigned long long)v->fp[mip].Offset, w, h, d, f->RowPitch, v->rows[mip],
            (unsigned long long)v->row_bytes[mip], (unsigned long long)((UINT64)f->RowPitch * v->rows[mip]),
            (unsigned long long)v->total, (unsigned long long)v->capacity);
    }
    HRESULT hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &v->desc,
        D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&v->texture);
    if (SUCCEEDED(hr)) {
        v->upload = c21_buffer(device, D3D12_HEAP_TYPE_UPLOAD, v->capacity, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        v->init = c21_buffer(device, D3D12_HEAP_TYPE_UPLOAD, v->capacity, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        v->readback = c21_buffer(device, D3D12_HEAP_TYPE_READBACK, v->capacity, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!v->upload || !v->init || !v->readback) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) hr = c21_fill(v->init, v->capacity, 0xccccccccu);
    printf("%s CreateCommittedResource volume/buffers hr=0x%08lx\n", step, (unsigned long)hr);
    return hr;
}
static int texture1d_copy(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    C21Volume v; ID3D12CommandAllocator *allocator = NULL; ID3D12GraphicsCommandList *list = NULL;
    HRESULT hr = c23_create(device, &v, 37, 1, 1, DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_FLAG_NONE, 65536, "C23A");
    if (SUCCEEDED(hr)) hr = c21_source(&v, 1);
    if (SUCCEEDED(hr)) hr = c21_commands(device, NULL, &allocator, &list);
    if (SUCCEEDED(hr)) {
        ID3D12GraphicsCommandList_CopyBufferRegion(list, v.readback, 0, v.init, 0, v.capacity);
        c21_transfer(list, &v, 1);
        transition(list, v.texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        c21_transfer(list, &v, 0);
        hr = run_list(queue, fence, list, base + 1, "C23A");
        if(FAILED(hr)){printf("AGX_D3D12_C23A: FAIL submitted-list hr=0x%08lx\n",(unsigned long)hr);fflush(stdout);ExitProcess(1);}
    }
    if (SUCCEEDED(hr)) hr = c21_verify(&v, 0, 0, "C23A");
    printf("AGX_D3D12_C23A: %s hr=0x%08lx\n", SUCCEEDED(hr) ? "PASS" : "FAIL", (unsigned long)hr);
    c21_command_release(&allocator, &list); c21_release(&v); return FAILED(hr);
}
static int texture1d_compute(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int store) {
    const char *step = store ? "C23C" : "C23B";
    C21Volume v; ID3D12CommandAllocator *allocator = NULL; ID3D12GraphicsCommandList *list = NULL;
    ID3D12DescriptorHeap *heap = NULL; ID3D12RootSignature *root = NULL; ID3D12PipelineState *pso = NULL;
    ID3DBlob *blob = NULL, *errors = NULL; ID3D12Resource *output = NULL;
    D3D12_DESCRIPTOR_RANGE range = {store ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER parameters[2] = {{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}, {store ? D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS : D3D12_ROOT_PARAMETER_TYPE_UAV}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {2, parameters};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {0}; D3D12_GPU_DESCRIPTOR_HANDLE gpu = {0};
    HRESULT hr = c23_create(device, &v, 37, 1, 1, DXGI_FORMAT_R32_UINT,
        store ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE, 65536, step);
    parameters[0].DescriptorTable.NumDescriptorRanges = 1; parameters[0].DescriptorTable.pDescriptorRanges = &range;
    if (store) { parameters[1].Constants.ShaderRegister = 0; parameters[1].Constants.Num32BitValues = 4; }
    else parameters[1].Descriptor.ShaderRegister = 0;
    if (SUCCEEDED(hr)) hr = c21_source(&v, !store);
    if (SUCCEEDED(hr) && !store) {
        output = c21_buffer(device, D3D12_HEAP_TYPE_DEFAULT, v.capacity, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (!output) hr = E_FAIL;
        if (SUCCEEDED(hr)) hr = c21_fill(v.init, v.capacity, 0xd15ea5edu);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    if (SUCCEEDED(hr)) {
        UINT stride = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &cpu);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &gpu);
        for (UINT mip = 0; mip < 2; ++mip) {
            D3D12_CPU_DESCRIPTOR_HANDLE at = {cpu.ptr + (SIZE_T)mip * stride};
            if (store) {
                D3D12_UNORDERED_ACCESS_VIEW_DESC view = {0}; view.Format = DXGI_FORMAT_R32_UINT;
                view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
                view.Texture1D.MipSlice = mip;
                ID3D12Device_CreateUnorderedAccessView(device, v.texture, NULL, &view, at);
            } else {
                D3D12_SHADER_RESOURCE_VIEW_DESC view = {0}; view.Format = DXGI_FORMAT_R32_UINT;
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D; view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                view.Texture1D.MostDetailedMip = mip; view.Texture1D.MipLevels = 1;
                ID3D12Device_CreateShaderResourceView(device, v.texture, &view, at);
            }
        }
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = store ? g_agx_d3d12_probe_cs_texture1d_store : g_agx_d3d12_probe_cs_texture1d_load;
        pso_desc.CS.BytecodeLength = store ? sizeof(g_agx_d3d12_probe_cs_texture1d_store) : sizeof(g_agx_d3d12_probe_cs_texture1d_load);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
    }
    printf("%s resources/views/root/PSO hr=0x%08lx\n", step, (unsigned long)hr);
    for (UINT mip = 0; mip < 2 && SUCCEEDED(hr); ++mip) {
        const UINT w = c21_extent(37, mip), h = 1, d = 1;
        const UINT params[4] = {w, h, d, mip};
        const UINT stride = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_GPU_DESCRIPTOR_HANDLE selected = {gpu.ptr + (UINT64)mip * stride};
        hr = c21_commands(device, pso, &allocator, &list);
        if (SUCCEEDED(hr)) {
            if (store || mip == 0) {
                if (store && mip) transition(list, v.texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                c21_transfer(list, &v, 1);
                transition(list, v.texture, D3D12_RESOURCE_STATE_COPY_DEST,
                    store ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            if (store) ID3D12GraphicsCommandList_CopyBufferRegion(list, v.readback, 0, v.init, 0, v.capacity);
            else {
                transition(list, output, mip ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
                ID3D12GraphicsCommandList_CopyBufferRegion(list, output, 0, v.init, 0, v.capacity);
                transition(list, output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &heap);
            ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, selected);
            if (store) ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list, 1, 4, params, 0);
            else ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 1, ID3D12Resource_GetGPUVirtualAddress(output));
            ID3D12GraphicsCommandList_Dispatch(list, 1, h, d);
            D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_UAV}; barrier.UAV.pResource = store ? v.texture : output;
            ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
            transition(list, store ? v.texture : output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            if (store) c21_transfer(list, &v, 0);
            else ID3D12GraphicsCommandList_CopyBufferRegion(list, v.readback, 0, output, 0, v.capacity);
            hr = run_list(queue, fence, list, base + 1 + mip, step);
            if(FAILED(hr)){printf("AGX_D3D12_%s: FAIL submitted-list hr=0x%08lx\n",step,(unsigned long)hr);fflush(stdout);ExitProcess(1);}
        }
        if (SUCCEEDED(hr) && store) hr = c21_verify(&v, 1, mip, step);
        else if (SUCCEEDED(hr)) {
            UINT *words = NULL, bad = 0, first = ~0u; D3D12_RANGE all = {0, (SIZE_T)v.capacity};
            hr = ID3D12Resource_Map(v.readback, 0, &all, (void **)&words);
            if (SUCCEEDED(hr) && !words) hr = E_FAIL;
            if (SUCCEEDED(hr)) {
                const UINT dimensions[4] = {w, h, d, 1};
                for (UINT64 i = 0; i < v.capacity / 4; ++i) {
                    UINT want = 0xd15ea5edu;
                    if (i < 4) want = dimensions[i];
                    else if (i - 4 < (UINT64)w * h * d) {
                        UINT index = (UINT)i - 4, x = index % w, y = (index / w) % h, z = index / (w * h);
                        want = c21_input(mip, x, y, z);
                    }
                    if (words[i] != want) { if (!bad) first = (UINT)i; ++bad; }
                }
                printf("C23B mip=%u dims=[%u,%u,%u,%u] expected=[%u,%u,%u,1] voxels=%u bad_words=%u first_bad=%u\n",
                    mip, words[0], words[1], words[2], words[3], w, h, d, w * h * d, bad, first);
                ID3D12Resource_Unmap(v.readback, 0, NULL);
                if (bad) hr = E_FAIL;
            }
        }
        c21_command_release(&allocator, &list);
    }
    printf("AGX_D3D12_%s: %s hr=0x%08lx\n", step, SUCCEEDED(hr) ? "PASS" : "FAIL", (unsigned long)hr);
    c21_command_release(&allocator, &list);
    if (pso) ID3D12PipelineState_Release(pso); if (root) ID3D12RootSignature_Release(root);
    if (blob) ID3D10Blob_Release(blob); if (errors) ID3D10Blob_Release(errors);
    if (heap) ID3D12DescriptorHeap_Release(heap); if (output) ID3D12Resource_Release(output);
    c21_release(&v); return FAILED(hr);
}
static int step_c23b(ID3D12Device*d,ID3D12CommandQueue*q,ID3D12Fence*f,UINT64 b){return texture1d_compute(d,q,f,b,0);}
static int step_c23c(ID3D12Device*d,ID3D12CommandQueue*q,ID3D12Fence*f,UINT64 b){return texture1d_compute(d,q,f,b,1);}
/* C24: 3 array slices x 2 mip subresources; array slices never minify. */
typedef struct {
    D3D12_RESOURCE_DESC desc;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[6];
    UINT rows[6]; UINT64 row_bytes[6],total,capacity;
    ID3D12Resource *texture,*upload,*init,*readback;
} C24Array;
static UINT c24_input(UINT slice,UINT mip,UINT x){return 0x24000000u+(slice<<20)+(mip<<16)+x;}
static void c24_release(C24Array*v){
    if(v->texture)ID3D12Resource_Release(v->texture);if(v->upload)ID3D12Resource_Release(v->upload);
    if(v->init)ID3D12Resource_Release(v->init);if(v->readback)ID3D12Resource_Release(v->readback);
}
static HRESULT c24_create(ID3D12Device*d,C24Array*v,int store,const char*step){
    D3D12_HEAP_PROPERTIES heap={D3D12_HEAP_TYPE_DEFAULT};D3D12_RESOURCE_ALLOCATION_INFO a={0};
    memset(v,0,sizeof(*v));v->desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    v->desc.Width=37;v->desc.Height=1;v->desc.DepthOrArraySize=3;v->desc.MipLevels=2;
    v->desc.Format=DXGI_FORMAT_R32_UINT;v->desc.SampleDesc.Count=1;
    v->desc.Flags=store?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
    ID3D12Device_GetResourceAllocationInfo(d,&a,0,1,&v->desc);
    /* Independent actual-query/AIL gate must confirm 3*16384 rounded64K before hardware. */
    printf("%s allocation=%llu alignment=%llu expected=65536/65536\n",step,(unsigned long long)a.SizeInBytes,(unsigned long long)a.Alignment);
    if(a.SizeInBytes!=65536 || a.Alignment!=65536)return E_FAIL;
    ID3D12Device_GetCopyableFootprints(d,&v->desc,0,6,0,v->fp,v->rows,v->row_bytes,&v->total);
    if(v->total<4 || v->total>~0ULL-65535)return E_FAIL;v->capacity=(v->total+65535)&~65535ULL;
    if(v->capacity>(UINT64)(~(SIZE_T)0))return E_FAIL;
    for(UINT s=0;s<6;s++){
        UINT w=c21_extent(37,s%2);const D3D12_SUBRESOURCE_FOOTPRINT*f=&v->fp[s].Footprint;
        if(f->Format!=DXGI_FORMAT_R32_UINT || f->Width!=w || f->Height!=1 || f->Depth!=1 ||
           v->rows[s]!=1 || v->row_bytes[s]!=4ull*w || f->RowPitch<4*w || f->RowPitch%256 || v->fp[s].Offset%512 ||
           v->fp[s].Offset>v->total || 4ull*w>v->total-v->fp[s].Offset ||
           (s && v->fp[s].Offset<v->fp[s-1].Offset+v->row_bytes[s-1]))return E_FAIL;
        printf("%s sub=%u slice=%u mip=%u width=%u pitch=%u offset=%llu total=%llu\n",step,s,s/2,s%2,w,f->RowPitch,(unsigned long long)v->fp[s].Offset,(unsigned long long)v->total);
    }
    HRESULT hr=ID3D12Device_CreateCommittedResource(d,&heap,D3D12_HEAP_FLAG_NONE,&v->desc,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&v->texture);
    if(SUCCEEDED(hr)){
        v->upload=c21_buffer(d,D3D12_HEAP_TYPE_UPLOAD,v->capacity,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);
        v->init=c21_buffer(d,D3D12_HEAP_TYPE_UPLOAD,v->capacity,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);
        v->readback=c21_buffer(d,D3D12_HEAP_TYPE_READBACK,v->capacity,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
        if(!v->upload||!v->init||!v->readback)hr=E_FAIL;
    }
    if(SUCCEEDED(hr))hr=c21_fill(v->init,v->capacity,0xccccccccu);
    if(SUCCEEDED(hr))hr=c21_fill(v->upload,v->capacity,0xccccccccu);
    if(SUCCEEDED(hr)){
        UINT*words=NULL;D3D12_RANGE none={0,0};hr=ID3D12Resource_Map(v->upload,0,&none,(void**)&words);
        if(SUCCEEDED(hr)&&!words)hr=E_FAIL;
        if(SUCCEEDED(hr)){
            for(UINT s=0;s<6;s++)for(UINT x=0;x<c21_extent(37,s%2);x++)
                words[v->fp[s].Offset/4+x]=store?0xa5a5a5a5u:c24_input(s/2,s%2,x);
            ID3D12Resource_Unmap(v->upload,0,NULL);
        }
    }
    return hr;
}
static void c24_transfer(ID3D12GraphicsCommandList*l,C24Array*v,int upload){
    for(UINT s=0;s<6;s++){
        D3D12_TEXTURE_COPY_LOCATION t={v->texture,D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};t.SubresourceIndex=s;
        D3D12_TEXTURE_COPY_LOCATION b={upload?v->upload:v->readback,D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};b.PlacedFootprint=v->fp[s];
        ID3D12GraphicsCommandList_CopyTextureRegion(l,upload?&t:&b,0,0,0,upload?&b:&t,NULL);
    }
}
static HRESULT c24_verify(C24Array*v,int store,const char*step){
    UINT*got=NULL;D3D12_RANGE all={0,(SIZE_T)v->capacity};HRESULT hr=ID3D12Resource_Map(v->readback,0,&all,(void**)&got);
    if(SUCCEEDED(hr)&&!got)hr=E_FAIL;
    if(SUCCEEDED(hr)){
        UINT bad=0;UINT64 first=~0ULL;
        for(UINT64 i=0;i<v->capacity/4;i++){
            UINT want=0xccccccccu;
            for(UINT s=0;s<6;s++){
                UINT64 begin=v->fp[s].Offset/4;UINT w=c21_extent(37,s%2);
                if(i>=begin&&i-begin<w){UINT x=(UINT)(i-begin);
                    want=store?(s==3?0x74110000u+x:0xa5a5a5a5u):c24_input(s/2,s%2,x);break;}
            }
            if(got[i]!=want){if(!bad)first=i;bad++;}
        }
        printf("%s all6 subresources/165texels/padding bad=%u first=%llu\n",step,bad,(unsigned long long)first);
        ID3D12Resource_Unmap(v->readback,0,NULL);if(bad)hr=E_FAIL;
    }
    return hr;
}
static int c24_array(ID3D12Device*d,ID3D12CommandQueue*q,ID3D12Fence*f,UINT64 base,int mode){
    const char*step=mode==0?"C24A":mode==1?"C24B":"C24C";int store=mode==2;
    C24Array v;ID3D12CommandAllocator*a=NULL;ID3D12GraphicsCommandList*l=NULL;
    ID3D12DescriptorHeap*heap=NULL;ID3D12RootSignature*root=NULL;ID3D12PipelineState*pso=NULL;
    ID3DBlob*blob=NULL,*errors=NULL;ID3D12Resource*out=NULL;D3D12_GPU_DESCRIPTOR_HANDLE gpu={0};
    HRESULT hr=c24_create(d,&v,store,step);
    if(SUCCEEDED(hr)&&mode){
        D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
        hr=ID3D12Device_CreateDescriptorHeap(d,&hd,&IID_ID3D12DescriptorHeap,(void**)&heap);
        if(SUCCEEDED(hr)){
            D3D12_CPU_DESCRIPTOR_HANDLE cpu={0};ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap,&cpu);
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap,&gpu);
            if(store){D3D12_UNORDERED_ACCESS_VIEW_DESC u={0};u.Format=DXGI_FORMAT_R32_UINT;u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
                u.Texture1DArray.MipSlice=1;u.Texture1DArray.FirstArraySlice=1;u.Texture1DArray.ArraySize=1;
                ID3D12Device_CreateUnorderedAccessView(d,v.texture,NULL,&u,cpu);
            }else{D3D12_SHADER_RESOURCE_VIEW_DESC s={0};s.Format=DXGI_FORMAT_R32_UINT;s.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
                s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;s.Texture1DArray.MostDetailedMip=1;
                s.Texture1DArray.MipLevels=1;s.Texture1DArray.FirstArraySlice=1;s.Texture1DArray.ArraySize=2;
                ID3D12Device_CreateShaderResourceView(d,v.texture,&s,cpu);}
            D3D12_DESCRIPTOR_RANGE range={store?D3D12_DESCRIPTOR_RANGE_TYPE_UAV:D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
            D3D12_ROOT_PARAMETER params[2]={{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE},{store?D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:D3D12_ROOT_PARAMETER_TYPE_UAV}};
            params[0].DescriptorTable.NumDescriptorRanges=1;params[0].DescriptorTable.pDescriptorRanges=&range;
            if(store){params[1].Constants.ShaderRegister=0;params[1].Constants.Num32BitValues=4;}else params[1].Descriptor.ShaderRegister=0;
            D3D12_ROOT_SIGNATURE_DESC rd={2,params};hr=D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors);
        }
        if(SUCCEEDED(hr))hr=ID3D12Device_CreateRootSignature(d,0,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),&IID_ID3D12RootSignature,(void**)&root);
        if(SUCCEEDED(hr)){D3D12_COMPUTE_PIPELINE_STATE_DESC ps={0};ps.pRootSignature=root;
            ps.CS.pShaderBytecode=store?g_agx_d3d12_probe_cs_texture1d_array_store:g_agx_d3d12_probe_cs_texture1d_array_load;
            ps.CS.BytecodeLength=store?sizeof(g_agx_d3d12_probe_cs_texture1d_array_store):sizeof(g_agx_d3d12_probe_cs_texture1d_array_load);
            hr=ID3D12Device_CreateComputePipelineState(d,&ps,&IID_ID3D12PipelineState,(void**)&pso);}
        if(SUCCEEDED(hr)&&!store){out=c21_buffer(d,D3D12_HEAP_TYPE_DEFAULT,v.capacity,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
            if(!out)hr=E_FAIL;else hr=c21_fill(v.init,v.capacity,0xd15ea5edu);}
    }
    if(SUCCEEDED(hr))hr=c21_commands(d,pso,&a,&l);
    if(SUCCEEDED(hr)){
        ID3D12GraphicsCommandList_CopyBufferRegion(l,v.readback,0,v.init,0,v.capacity);c24_transfer(l,&v,1);
        transition(l,v.texture,D3D12_RESOURCE_STATE_COPY_DEST,mode?(store?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE):D3D12_RESOURCE_STATE_COPY_SOURCE);
        if(mode){
            if(!store){ID3D12GraphicsCommandList_CopyBufferRegion(l,out,0,v.init,0,v.capacity);transition(l,out,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
            ID3D12GraphicsCommandList_SetDescriptorHeaps(l,1,&heap);ID3D12GraphicsCommandList_SetComputeRootSignature(l,root);
            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(l,0,gpu);
            if(store){const UINT shape[4]={18,1,1,1};ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(l,1,4,shape,0);}
            else ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(l,1,ID3D12Resource_GetGPUVirtualAddress(out));
            ID3D12GraphicsCommandList_Dispatch(l,1,store?1:2,1);
            D3D12_RESOURCE_BARRIER b={D3D12_RESOURCE_BARRIER_TYPE_UAV};b.UAV.pResource=store?v.texture:out;ID3D12GraphicsCommandList_ResourceBarrier(l,1,&b);
            transition(l,store?v.texture:out,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        }
        if(!mode||store)c24_transfer(l,&v,0);else ID3D12GraphicsCommandList_CopyBufferRegion(l,v.readback,0,out,0,v.capacity);
        hr=run_list(q,f,l,base+1,step);if(FAILED(hr)){printf("AGX_D3D12_%s: FAIL submitted-list\n",step);fflush(stdout);ExitProcess(1);}
    }
    if(SUCCEEDED(hr)&&(!mode||store))hr=c24_verify(&v,store,step);
    if(SUCCEEDED(hr)&&mode==1){
        UINT*got=NULL;D3D12_RANGE all={0,(SIZE_T)v.capacity};hr=ID3D12Resource_Map(v.readback,0,&all,(void**)&got);if(SUCCEEDED(hr)&&!got)hr=E_FAIL;
        if(SUCCEEDED(hr)){UINT bad=0;for(UINT64 i=0;i<v.capacity/4;i++){
            UINT want=0xd15ea5edu;if(i<4){const UINT h[4]={18,2,1,0xc2400001u};want=h[i];}
            else if(i<40){UINT j=(UINT)i-4;want=c24_input(1+j/18,1,j%18);}if(got[i]!=want)bad++;}
            printf("C24B dimensions=[%u,%u,%u,%08x] bad_words=%u\n",got[0],got[1],got[2],got[3],bad);
            ID3D12Resource_Unmap(v.readback,0,NULL);if(bad)hr=E_FAIL;}
    }
    printf("AGX_D3D12_%s: %s hr=0x%08lx\n",step,SUCCEEDED(hr)?"PASS":"FAIL",(unsigned long)hr);
    c21_command_release(&a,&l);if(pso)ID3D12PipelineState_Release(pso);if(root)ID3D12RootSignature_Release(root);
    if(blob)ID3D10Blob_Release(blob);if(errors)ID3D10Blob_Release(errors);if(heap)ID3D12DescriptorHeap_Release(heap);if(out)ID3D12Resource_Release(out);c24_release(&v);return FAILED(hr);
}
static int step_c24a(ID3D12Device*d,ID3D12CommandQueue*q,ID3D12Fence*f,UINT64 b){return c24_array(d,q,f,b,0);}
static int step_c24b(ID3D12Device*d,ID3D12CommandQueue*q,ID3D12Fence*f,UINT64 b){return c24_array(d,q,f,b,1);}
static int step_c24c(ID3D12Device*d,ID3D12CommandQueue*q,ID3D12Fence*f,UINT64 b){return c24_array(d,q,f,b,2);}

static HRESULT c21_graphics_pipeline(ID3D12Device *device, ID3D12RootSignature *root, const void *ps, SIZE_T ps_bytes, ID3D12PipelineState **pso) {
    D3D12_INPUT_ELEMENT_DESC elements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {0};
    desc.pRootSignature = root; desc.VS.pShaderBytecode = g_agx_d3d12_probe_vs; desc.VS.BytecodeLength = sizeof(g_agx_d3d12_probe_vs);
    desc.PS.pShaderBytecode = ps; desc.PS.BytecodeLength = ps_bytes;
    desc.BlendState.RenderTarget[0].SrcBlend = desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[0].DestBlend = desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOp = desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = ~0u; desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.DepthStencilState.FrontFace.StencilFailOp = desc.DepthStencilState.FrontFace.StencilDepthFailOp = desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
    desc.InputLayout.pInputElementDescs = elements; desc.InputLayout.NumElements = 2;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    return ID3D12Device_CreateGraphicsPipelineState(device, &desc, &IID_ID3D12PipelineState, (void **)pso);
}
static int volume_rtv(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    C21Volume v; ID3D12CommandAllocator *allocator = NULL; ID3D12GraphicsCommandList *list = NULL;
    ID3D12DescriptorHeap *heap = NULL; ID3D12RootSignature *root = NULL; ID3D12PipelineState *pso = NULL;
    ID3DBlob *blob = NULL, *errors = NULL; ID3D12Resource *vertices = NULL;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {0}; root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0};
    HRESULT hr = c21_create(device, &v, 32, 32, 4, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 65536, "C21D");
    if (SUCCEEDED(hr)) hr = c21_source(&v, 0);
    if (SUCCEEDED(hr)) {
        const float data[3][6] = {{-0.5f, -0.5f, 0, 1, 0, 1}, {0, 0.5f, 0, 1, 0, 1}, {0.5f, -0.5f, 0, 1, 0, 1}};
        void *mapped = NULL; vertices = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!vertices) hr = E_FAIL;
        if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(vertices, 0, NULL, &mapped);
        if (SUCCEEDED(hr) && !mapped) hr = E_FAIL;
        if (SUCCEEDED(hr)) { memcpy(mapped, data, sizeof(data)); ID3D12Resource_Unmap(vertices, 0, NULL); }
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    if (SUCCEEDED(hr)) {
        D3D12_RENDER_TARGET_VIEW_DESC view = {0}; view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        view.Texture3D.MipSlice = 1; view.Texture3D.FirstWSlice = 1; view.Texture3D.WSize = 1;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &rtv);
        ID3D12Device_CreateRenderTargetView(device, v.texture, &view, rtv);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) hr = c21_graphics_pipeline(device, root, g_agx_d3d12_probe_ps, sizeof(g_agx_d3d12_probe_ps), &pso);
    if (SUCCEEDED(hr)) hr = c21_commands(device, pso, &allocator, &list);
    if (SUCCEEDED(hr)) {
        D3D12_VIEWPORT viewport = {0, 0, 16, 16, 0, 1}; D3D12_RECT scissor = {0, 0, 16, 16};
        const FLOAT red[4] = {1, 0, 0, 1};
        D3D12_VERTEX_BUFFER_VIEW vb = {ID3D12Resource_GetGPUVirtualAddress(vertices), sizeof(float) * 18, sizeof(float) * 6};
        ID3D12GraphicsCommandList_CopyBufferRegion(list, v.readback, 0, v.init, 0, v.capacity);
        c21_transfer(list, &v, 1); transition(list, v.texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, red, 0, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport); ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vb); ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
        transition(list, v.texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        c21_transfer(list, &v, 0); hr = run_list(queue, fence, list, base + 1, "C21D");
    }
    if (SUCCEEDED(hr)) hr = c21_verify(&v, 2, 1, "C21D");
    printf("AGX_D3D12_C21D: %s hr=0x%08lx\n", SUCCEEDED(hr) ? "PASS" : "FAIL", (unsigned long)hr);
    c21_command_release(&allocator, &list);
    if (pso) ID3D12PipelineState_Release(pso); if (root) ID3D12RootSignature_Release(root);
    if (blob) ID3D10Blob_Release(blob); if (errors) ID3D10Blob_Release(errors);
    if (heap) ID3D12DescriptorHeap_Release(heap); if (vertices) ID3D12Resource_Release(vertices);
    c21_release(&v); return FAILED(hr);
}
/* C21E: graphics point sampling from all16 mip1 volume voxels. */
static int volume_sample(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    C21Volume v; ID3D12CommandAllocator *allocator = NULL; ID3D12GraphicsCommandList *list = NULL;
    ID3D12DescriptorHeap *heap = NULL, *rtv_heap = NULL; ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL; ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12Resource *target = NULL, *vertices = NULL;
    D3D12_RESOURCE_DESC target_desc = {0}; D3D12_HEAP_PROPERTIES default_heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER parameter = {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE};
    D3D12_STATIC_SAMPLER_DESC sampler = {D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        0, 1, D3D12_COMPARISON_FUNC_NEVER, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, 0, D3D12_FLOAT32_MAX,
        0, 0, D3D12_SHADER_VISIBILITY_PIXEL};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {0}, rtv = {0}; D3D12_GPU_DESCRIPTOR_HANDLE gpu = {0};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT target_fp = {0}; UINT target_rows = 0;
    UINT64 target_row_bytes = 0, target_bytes = 0;
    HRESULT hr = c21_create(device, &v, 4, 4, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE, 131072, "C21E");
    parameter.DescriptorTable.NumDescriptorRanges = 1; parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    if (SUCCEEDED(hr)) hr = c21_source(&v, 1);
    target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; target_desc.Width = target_desc.Height = 64;
    target_desc.DepthOrArraySize = target_desc.MipLevels = 1; target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    target_desc.SampleDesc.Count = 1; target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (SUCCEEDED(hr)) {
        ID3D12Device_GetCopyableFootprints(device, &target_desc, 0, 1, 0, &target_fp, &target_rows, &target_row_bytes, &target_bytes);
        if (!target_bytes || target_bytes > v.capacity || target_fp.Footprint.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
            target_fp.Footprint.Width != 64 || target_fp.Footprint.Height != 64 || target_fp.Footprint.Depth != 1 ||
            target_rows != 64 || target_row_bytes != 256 || target_fp.Footprint.RowPitch < 256 ||
            target_fp.Footprint.RowPitch % 256 || target_fp.Offset % 512 || target_fp.Offset > target_bytes ||
            (UINT64)63 * target_fp.Footprint.RowPitch + 256 > target_bytes - target_fp.Offset)
            hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommittedResource(device, &default_heap, D3D12_HEAP_FLAG_NONE, &target_desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&target);
    if (SUCCEEDED(hr)) {
        const float quad[4][6] = {{-1, -1, 0, 0, 0, 1}, {-1, 1, 0, 0, 0, 1}, {1, -1, 0, 0, 0, 1}, {1, 1, 0, 0, 0, 1}};
        void *mapped = NULL; vertices = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!vertices) hr = E_FAIL;
        if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(vertices, 0, NULL, &mapped);
        if (SUCCEEDED(hr) && !mapped) hr = E_FAIL;
        if (SUCCEEDED(hr)) { memcpy(mapped, quad, sizeof(quad)); ID3D12Resource_Unmap(vertices, 0, NULL); }
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    if (SUCCEEDED(hr)) {
        D3D12_SHADER_RESOURCE_VIEW_DESC view = {0}; view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D; view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture3D.MostDetailedMip = 1; view.Texture3D.MipLevels = 1;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &cpu); ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &gpu);
        ID3D12Device_CreateShaderResourceView(device, v.texture, &view, cpu);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv); ID3D12Device_CreateRenderTargetView(device, target, NULL, rtv);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) hr = c21_graphics_pipeline(device, root, g_agx_d3d12_probe_ps_volume, sizeof(g_agx_d3d12_probe_ps_volume), &pso);
    if (SUCCEEDED(hr)) hr = c21_commands(device, pso, &allocator, &list);
    if (SUCCEEDED(hr)) {
        D3D12_VIEWPORT viewport = {0, 0, 64, 64, 0, 1}; D3D12_RECT scissor = {0, 0, 64, 64};
        const FLOAT poison[4] = {1, 0, 1, 1};
        D3D12_VERTEX_BUFFER_VIEW vb = {ID3D12Resource_GetGPUVirtualAddress(vertices), sizeof(float) * 24, sizeof(float) * 6};
        D3D12_TEXTURE_COPY_LOCATION src = {0}, dst = {0};
        src.pResource = target; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = v.readback; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = target_fp;
        ID3D12GraphicsCommandList_CopyBufferRegion(list, v.readback, 0, v.init, 0, v.capacity);
        c21_transfer(list, &v, 1); transition(list, v.texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, poison, 0, NULL);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &heap);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root); ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list, 0, gpu);
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport); ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vb); ID3D12GraphicsCommandList_DrawInstanced(list, 4, 1, 0, 0);
        transition(list, target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL);
        hr = run_list(queue, fence, list, base + 1, "C21E");
    }
    if (SUCCEEDED(hr)) {
        unsigned char *bytes = NULL, *expected = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)v.capacity);
        UINT64 bad = 0, first = ~0ULL; D3D12_RANGE all = {0, (SIZE_T)v.capacity};
        if (!expected) hr = E_OUTOFMEMORY;
        if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(v.readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && !bytes) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            memset(expected, 0xcc, (SIZE_T)v.capacity);
            for (UINT y = 0; y < 64; ++y) for (UINT x = 0; x < 64; ++x) {
                const UINT want = c21_input(1, x & 1u, (x >> 1) & 1u, y & 3u);
                const UINT64 at = target_fp.Offset + (UINT64)y * target_fp.Footprint.RowPitch + x * 4u;
                memcpy(expected + at, &want, 4);
            }
            for (UINT64 i = 0; i < v.capacity; ++i) if (bytes[i] != expected[i]) { if (!bad) first = i; ++bad; }
            printf("C21E exactRGBA pixels=4096 mip1voxels=16 bad_bytes=%llu first_bad=%llu pitch=%u\n",
                (unsigned long long)bad, (unsigned long long)first, target_fp.Footprint.RowPitch);
            ID3D12Resource_Unmap(v.readback, 0, NULL); if (bad) hr = E_FAIL;
        }
        if (expected) HeapFree(GetProcessHeap(), 0, expected);
    }
    printf("AGX_D3D12_C21E: %s hr=0x%08lx\n", SUCCEEDED(hr) ? "PASS" : "FAIL", (unsigned long)hr);
    c21_command_release(&allocator, &list);
    if (pso) ID3D12PipelineState_Release(pso); if (root) ID3D12RootSignature_Release(root);
    if (blob) ID3D10Blob_Release(blob); if (errors) ID3D10Blob_Release(errors);
    if (heap) ID3D12DescriptorHeap_Release(heap); if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (target) ID3D12Resource_Release(target); if (vertices) ID3D12Resource_Release(vertices);
    c21_release(&v); return FAILED(hr);
}

/* C22: distinct recording threads first use one shared, still-lazy PSO.
 * Resources and owner creation finish before either thread starts. */
typedef struct C22Record {
    ID3D12GraphicsCommandList *list;
    ID3D12CommandAllocator *allocator;
    ID3D12Resource *target, *upload, *readback;
    ID3D12RootSignature *root;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    HANDLE ready, start;
    HRESULT hr;
    int graphics;
} C22Record;

static DWORD WINAPI c22_record(void *argument) {
    C22Record *r = (C22Record *)argument;
    SetEvent(r->ready);
    if (WaitForSingleObject(r->start, 15000) != WAIT_OBJECT_0) { r->hr = E_FAIL; return 1; }
    if (r->graphics) {
        D3D12_VIEWPORT viewport = {0, 0, 64, 64, 0, 1};
        D3D12_RECT scissor = {0, 0, 64, 64};
        D3D12_VERTEX_BUFFER_VIEW vertices = {ID3D12Resource_GetGPUVirtualAddress(r->upload), 72, 24};
        const FLOAT red[4] = {1, 0, 0, 1};
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(r->list, r->root);
        ID3D12GraphicsCommandList_RSSetViewports(r->list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(r->list, 1, &scissor);
        ID3D12GraphicsCommandList_OMSetRenderTargets(r->list, 1, &r->rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(r->list, r->rtv, red, 0, NULL);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(r->list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(r->list, 0, 1, &vertices);
        ID3D12GraphicsCommandList_DrawInstanced(r->list, 3, 1, 0, 0);
        transition(r->list, r->target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        {
            D3D12_TEXTURE_COPY_LOCATION src = {0}, dst = {0};
            src.pResource = r->target; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.pResource = r->readback; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            dst.PlacedFootprint.Footprint.Width = dst.PlacedFootprint.Footprint.Height = 64;
            dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = 256;
            ID3D12GraphicsCommandList_CopyTextureRegion(r->list, &dst, 0, 0, 0, &src, NULL);
        }
    } else {
        transition(r->list, r->target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyBufferRegion(r->list, r->target, 0, r->upload, 0, 65536);
        transition(r->list, r->target, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetComputeRootSignature(r->list, r->root);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(r->list, 0, ID3D12Resource_GetGPUVirtualAddress(r->target));
        ID3D12GraphicsCommandList_Dispatch(r->list, 256, 1, 1);
        transition(r->list, r->target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(r->list, r->readback, 0, r->target, 0, 65536);
    }
    r->hr = ID3D12GraphicsCommandList_Close(r->list);
    return FAILED(r->hr);
}

static int c22_parallel_pso(ID3D12Device *device, ID3D12CommandQueue *queue,
                            ID3D12Fence *fence, UINT64 base, int graphics) {
    const char *step = graphics ? "C22B" : "C22A";
    D3D12_ROOT_PARAMETER parameter = {D3D12_ROOT_PARAMETER_TYPE_UAV};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    C22Record records[2] = {{0}};
    HANDLE threads[2] = {NULL, NULL}, ready[2] = {NULL, NULL};
    HANDLE start = CreateEventW(NULL, TRUE, FALSE, NULL);
    unsigned started = 0, bad = 0;
    HRESULT hr = start ? S_OK : E_FAIL;
    root_desc.NumParameters = graphics ? 0 : 1;
    root_desc.pParameters = graphics ? NULL : &parameter;
    root_desc.Flags = graphics ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT : D3D12_ROOT_SIGNATURE_FLAG_NONE;
    if (SUCCEEDED(hr)) hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr) && graphics) {
        D3D12_DESCRIPTOR_HEAP_DESC heap = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
        hr = ID3D12Device_CreateDescriptorHeap(device, &heap, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
        if (SUCCEEDED(hr)) hr = c21_graphics_pipeline(device, root, g_agx_d3d12_probe_ps, sizeof(g_agx_d3d12_probe_ps), &pso);
    } else if (SUCCEEDED(hr)) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {0};
        desc.pRootSignature = root; desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs;
        desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs);
        hr = ID3D12Device_CreateComputePipelineState(device, &desc, &IID_ID3D12PipelineState, (void **)&pso);
    }
    for (unsigned i = 0; i < 2 && SUCCEEDED(hr); ++i) {
        C22Record *r = &records[i];
        r->root = root; r->start = start; r->graphics = graphics; r->hr = E_PENDING;
        r->ready = ready[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
        r->upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        r->readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!r->ready || !r->upload || !r->readback) { hr = E_FAIL; break; }
        hr = c21_fill(r->upload, 65536, 0xcdcdcdcdu);
        if (SUCCEEDED(hr)) hr = c21_fill(r->readback, 65536, 0xcdcdcdcdu);
        if (SUCCEEDED(hr) && graphics) {
            D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
            D3D12_RESOURCE_DESC desc = {0};
            float vertices[3][6] = {{-.5f, -.5f, 0, 0, 0, 1}, {0, .5f, 0, 0, 0, 1}, {.5f, -.5f, 0, 0, 0, 1}};
            void *mapped = NULL;
            for (unsigned v = 0; v < 3; ++v) vertices[v][i ? 4 : 3] = 1;
            hr = ID3D12Resource_Map(r->upload, 0, NULL, &mapped);
            if (SUCCEEDED(hr) && !mapped) hr = E_FAIL;
            if (SUCCEEDED(hr)) { memcpy(mapped, vertices, sizeof(vertices)); ID3D12Resource_Unmap(r->upload, 0, NULL); }
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = desc.Height = 64;
            desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&r->target);
            if (SUCCEEDED(hr)) {
                ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &r->rtv);
                r->rtv.ptr += i * ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                ID3D12Device_CreateRenderTargetView(device, r->target, NULL, r->rtv);
            }
        } else if (SUCCEEDED(hr)) r->target = uav_buffer(device);
        if (SUCCEEDED(hr) && !r->target) hr = E_FAIL;
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&r->allocator);
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, r->allocator, pso,
            &IID_ID3D12GraphicsCommandList, (void **)&r->list);
    }
    printf("%s setup hr=0x%08lx owner_resources_ready=%u shared_pso=%p\n", step, (unsigned long)hr, SUCCEEDED(hr) ? 2u : 0u, (void *)pso);
    for (unsigned i = 0; i < 2 && SUCCEEDED(hr); ++i) {
        threads[i] = CreateThread(NULL, 0, c22_record, &records[i], 0, NULL);
        if (!threads[i]) hr = E_FAIL; else ++started;
    }
    if (SUCCEEDED(hr) && WaitForMultipleObjects(2, ready, TRUE, 15000) != WAIT_OBJECT_0) hr = E_FAIL;
    if (start) SetEvent(start);
    if (started && WaitForMultipleObjects(started, threads, TRUE, 15000) != WAIT_OBJECT_0) {
        printf("AGX_D3D12_%s: FAIL recording threads timeout; process exits before releasing live resources\n", step);
        ExitProcess(1);
    }
    for (unsigned i = 0; i < started; ++i) {
        printf("%s thread=%u close_hr=0x%08lx\n", step, i, (unsigned long)records[i].hr);
        if (FAILED(records[i].hr)) hr = records[i].hr;
    }
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *lists[2] = {(ID3D12CommandList *)records[0].list, (ID3D12CommandList *)records[1].list};
        UINT64 completed = 0;
        ID3D12CommandQueue_ExecuteCommandLists(queue, 2, lists);
        hr = ID3D12CommandQueue_Signal(queue, fence, base + 1);
        for (unsigned i = 0; i < 300 && (completed = ID3D12Fence_GetCompletedValue(fence)) < base + 1; ++i) Sleep(10);
        printf("%s execute hr=0x%08lx fence=%llu\n", step, (unsigned long)hr, (unsigned long long)completed);
        if (FAILED(hr) || completed != base + 1) {
            printf("AGX_D3D12_%s: FAIL submitted work completion unproven; process exits before resource cleanup\n", step);
            ExitProcess(1);
        }
    }
    for (unsigned i = 0; i < 2 && SUCCEEDED(hr); ++i) {
        UINT *words = NULL, local_bad = 0, colored = 0;
        const UINT color = i ? 0xffff0000u : 0xff00ff00u;
        D3D12_RANGE range = {0, 65536};
        hr = ID3D12Resource_Map(records[i].readback, 0, &range, (void **)&words);
        if (SUCCEEDED(hr) && !words) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            for (unsigned j = 0; j < 16384; ++j) {
                if (!graphics) local_bad += words[j] != j * 3u + 7u;
                else if (j < 4096) {
                    const unsigned x = j % 64, y = j / 64;
                    /* Screen vertices (16,48),(32,16),(48,48), pixel centers.
                     * No half-integer sample lies exactly on these edges. */
                    const int inside = y < 48 && 2 * x + y >= 79 && 2 * x <= y + 47;
                    colored += words[j] == color;
                    local_bad += words[j] != (inside ? color : 0xff0000ffu);
                }
                else local_bad += words[j] != 0xcdcdcdcdu;
            }
            if (graphics) local_bad += colored != 512;
            printf("%s output=%u bad=%u colored=%u first=%08x center=%08x last=%08x\n", step, i, local_bad, colored, words[0], words[32 * 64 + 32], words[16383]);
            bad += local_bad;
            ID3D12Resource_Unmap(records[i].readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_%s: %s hr=0x%08lx bad=%u\n", step, SUCCEEDED(hr) && !bad ? "PASS" : "FAIL", (unsigned long)hr, bad);
    for (unsigned i = 0; i < 2; ++i) {
        if (threads[i]) CloseHandle(threads[i]);
        if (records[i].ready) CloseHandle(records[i].ready);
        if (records[i].list) ID3D12GraphicsCommandList_Release(records[i].list);
        if (records[i].allocator) ID3D12CommandAllocator_Release(records[i].allocator);
        if (records[i].readback) ID3D12Resource_Release(records[i].readback);
        if (records[i].upload) ID3D12Resource_Release(records[i].upload);
        if (records[i].target) ID3D12Resource_Release(records[i].target);
    }
    if (start) CloseHandle(start);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    return FAILED(hr) || bad;
}
static int step_c22a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c22_parallel_pso(d, q, f, b, 0); }
static int step_c22b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c22_parallel_pso(d, q, f, b, 1); }

static int step_c21b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return volume_compute(d, q, f, b, 0); }
static int step_c21c(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return volume_compute(d, q, f, b, 1); }

/* C27 (D3D12-C27-geometry-shaders.md): one point at the centre, expanded
 * by a geometry shader to a 32x32 quad whose colour and size come from a
 * root CBV b0 visible to the geometry stage only (C27A). C27B then draws the
 * same point through a pipeline without a GS: it must rasterise as a
 * single green pixel, so a GS left bound from the previous draw fails it. */
static int c27_geometry(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int then_plain) {
    const char *step = then_plain ? "C27B" : "C27A";
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_INPUT_ELEMENT_DESC elements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_ROOT_PARAMETER parameter = {D3D12_ROOT_PARAMETER_TYPE_CBV};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL, *plain = NULL;
    ID3D12Resource *texture = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    ID3D12Resource *vertices = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0};
    unsigned char *bytes = NULL;
    unsigned blue = 0, green = 0, red = 0, other = 4096;
    int inside_ok = 0, outside_ok = 0, centre_ok = 0;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    parameter.Descriptor.ShaderRegister = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_GEOMETRY;
    HRESULT hr = vertices && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(vertices, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        const float point[6] = {0.0f, 0.0f, 0, 1, 0, 1};
        const float quad[8] = {0.5f, 0.5f, 0, 0, 0, 0, 1, 1}; /* half_size, then blue */
        memcpy(bytes, point, sizeof(point));
        memcpy(bytes + 256, quad, sizeof(quad));
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
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.VS.pShaderBytecode = g_agx_d3d12_probe_vs;
        pso_desc.VS.BytecodeLength = sizeof(g_agx_d3d12_probe_vs);
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
        pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        pso_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        pso_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso_desc.DepthStencilState.BackFace = pso_desc.DepthStencilState.FrontFace;
        pso_desc.InputLayout.pInputElementDescs = elements;
        pso_desc.InputLayout.NumElements = 2;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso_desc.SampleDesc.Count = 1;
        if (then_plain) hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&plain);
        pso_desc.GS.pShaderBytecode = g_agx_d3d12_probe_gs;
        pso_desc.GS.BytecodeLength = sizeof(g_agx_d3d12_probe_gs);
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("%s CreateGraphicsPipelineState (GS%s) hr=0x%08lx\n", step, then_plain ? " + plain" : "", (unsigned long)hr);
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
        vbv.SizeInBytes = sizeof(float) * 6;
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
        ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list, 0, ID3D12Resource_GetGPUVirtualAddress(vertices) + 256);
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_DrawInstanced(list, 1, 1, 0, 0);
        if (then_plain) {
            ID3D12GraphicsCommandList_SetPipelineState(list, plain);
            ID3D12GraphicsCommandList_DrawInstanced(list, 1, 1, 0, 0);
        }
        transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &buf_loc, 0, 0, 0, &tex_loc, NULL);
        hr = run_list(queue, fence, list, base + 1, then_plain ? "C27B draw" : "C27A draw");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            other = 0;
            for (unsigned i = 0; i < 4096; ++i) {
                const unsigned char *px = bytes + 4 * i;
                if (px[0] == 0 && px[1] == 0 && px[2] == 255 && px[3] == 255) ++blue;
                else if (px[0] == 0 && px[1] == 255 && px[2] == 0 && px[3] == 255) ++green;
                else if (px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255) ++red;
                else ++other;
            }
            const unsigned char *inside = bytes + 16 * 256 + 16 * 4, *outside = bytes + 15 * 256 + 15 * 4;
            const unsigned char *centre = bytes + 32 * 256 + 32 * 4;
            inside_ok = inside[0] == 0 && inside[1] == 0 && inside[2] == 255;
            outside_ok = outside[0] == 255 && outside[1] == 0 && outside[2] == 0;
            /* the one-pixel point covers pixel (31 or 32, 31 or 32): green
             * somewhere in that 2x2, blue at least at its other pixels */
            centre_ok = then_plain ? green == 1 : centre[2] == 255;
            printf("%s readback blue=%u green=%u red=%u other=%u inside=%u,%u,%u outside=%u,%u,%u\n", step, blue, green,
                   red, other, inside[0], inside[1], inside[2], outside[0], outside[1], outside[2]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    const unsigned quad_pixels = blue + green;
    const int pass = SUCCEEDED(hr) && inside_ok && outside_ok && centre_ok && other == 0 && quad_pixels == 1024u &&
                     red == 4096u - 1024u;
    printf("AGX_D3D12_%s: %s\n", step, pass ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (plain) ID3D12PipelineState_Release(plain);
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
static int step_c27a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c27_geometry(d, q, f, b, 0); }
static int step_c27b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c27_geometry(d, q, f, b, 1); }

typedef int (*probe_step)(ID3D12Device *, ID3D12CommandQueue *, ID3D12Fence *, UINT64);
static int step_c6c(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 1); }
static int step_c6d(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 2); }
static int step_c11(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 3); }
static int step_c13(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 4); }
static int step_c14(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 5); }
static int step_c17b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 7); }
static int step_c16a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { (void)b; return compute_srv(d, q, f, 1); }
static int step_c14nc(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 6); }
static int run_isolated(IDXGIAdapter1 *adapter, const char *name, probe_step step) {
    ID3D12Device *device = NULL;
    ID3D12CommandQueue *queue = NULL;
    ID3D12Fence *fence = NULL;
    D3D12_COMMAND_QUEUE_DESC desc = {D3D12_COMMAND_LIST_TYPE_DIRECT};
    HRESULT hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandQueue(device, &desc, &IID_ID3D12CommandQueue, (void **)&queue);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    printf("%s fresh device/queue/fence hr=0x%08lx\n", name, (unsigned long)hr);
    int failed = FAILED(hr) ? 1 : step(device, queue, fence, 0);
    if (fence) ID3D12Fence_Release(fence);
    if (queue) ID3D12CommandQueue_Release(queue);
    if (device) ID3D12Device_Release(device);
    return failed;
}

/* Original C25: public DXGI flip/resize lifetime, no driver/capability change. */
static int c25_present_resize(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    WNDCLASSW wc = {0}; HWND window = NULL; IDXGIFactory2 *factory = NULL;
    IDXGISwapChain1 *swap1 = NULL; IDXGISwapChain3 *swap = NULL;
    DXGI_SWAP_CHAIN_DESC1 desc = {0}; HRESULT hr = S_OK; UINT frames = 0, resizes = 0;
    wc.lpfnWndProc = probe_window_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"AgxD3d12C25";
    RegisterClassW(&wc);
    window = CreateWindowExW(0, wc.lpszClassName, L"AGX C25", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 120, 120, 192, 160, NULL, NULL, wc.hInstance, NULL);
    hr = window ? CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&factory) : E_FAIL;
    desc.Width = 64; desc.Height = 64; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (SUCCEEDED(hr)) hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)queue, window, &desc, NULL, NULL, &swap1);
    if (SUCCEEDED(hr)) hr = IDXGISwapChain1_QueryInterface(swap1, &IID_IDXGISwapChain3, (void **)&swap);
    for (UINT epoch = 0; epoch < 2 && SUCCEEDED(hr); ++epoch) {
        const UINT w = epoch ? 96 : 64, h = epoch ? 48 : 64; UINT seen = 0;
        if (epoch) {
            /* Every prior command list/allocator/RTV/resource was released below,
             * after render and post-Present queue fences completed. */
            hr = IDXGISwapChain3_ResizeBuffers(swap, 2, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
            printf("C25 resize old_refs=0 prior_frames=%u size=%ux%u hr=0x%08lx\n", frames, w, h, (unsigned long)hr);
            if (SUCCEEDED(hr)) ++resizes;
        }
        for (UINT f = 0; f < 3 && SUCCEEDED(hr); ++f) {
            ID3D12Resource *buffer = NULL, *readback = NULL; ID3D12DescriptorHeap *heap = NULL;
            ID3D12CommandAllocator *allocator = NULL; ID3D12GraphicsCommandList *list = NULL;
            D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0}; D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {0};
            D3D12_RESOURCE_DESC rd = {0}; UINT rows = 0, bad = 0; UINT64 row_bytes = 0, total = 0;
            const UINT index = IDXGISwapChain3_GetCurrentBackBufferIndex(swap); const UINT channel = (epoch * 3 + f) % 3;
            FLOAT color[4] = {0,0,0,1}; color[channel] = 1;
            hr = IDXGISwapChain3_GetBuffer(swap, index, &IID_ID3D12Resource, (void **)&buffer);
            if (SUCCEEDED(hr)) { ID3D12Resource_GetDesc(buffer, &rd); if (rd.Width != w || rd.Height != h || index > 1) hr = E_FAIL; }
            if (SUCCEEDED(hr)) { ID3D12Device_GetCopyableFootprints(device, &rd, 0, 1, 0, &fp, &rows, &row_bytes, &total); if (rows != h || !rows || row_bytes != 4ull*w || !total || !fp.Footprint.RowPitch || fp.Footprint.RowPitch < row_bytes || fp.Offset > total || row_bytes > total-fp.Offset || (UINT64)(rows-1) > (total-fp.Offset-row_bytes)/fp.Footprint.RowPitch) hr = E_FAIL; }
            if (SUCCEEDED(hr)) { readback = c21_buffer(device, D3D12_HEAP_TYPE_READBACK, total, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST); if (!readback) hr = E_FAIL; }
            if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &hd, &IID_ID3D12DescriptorHeap, (void **)&heap);
            if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator);
            if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&list);
            if (SUCCEEDED(hr)) {
                D3D12_TEXTURE_COPY_LOCATION src = {0}, dst = {0};
                ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &rtv); ID3D12Device_CreateRenderTargetView(device, buffer, NULL, rtv);
                transition(list, buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
                ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, color, 0, NULL);
                transition(list, buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
                src.pResource = buffer; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.pResource = readback; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
                ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL);
                transition(list, buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
                hr = run_list(queue, fence, list, base + 2*(epoch*3+f) + 1, "C25 clear-copy");
                if (FAILED(hr)) { printf("AGX_D3D12_C25: FAIL work completion unproven\n"); fflush(stdout); ExitProcess(1); }
            }
            if (SUCCEEDED(hr)) {
                unsigned char *bytes = NULL; D3D12_RANGE range = {0, (SIZE_T)total};
                hr = ID3D12Resource_Map(readback, 0, &range, (void **)&bytes);
                if (SUCCEEDED(hr) && !bytes) hr = E_FAIL;
                if (SUCCEEDED(hr)) { for (UINT y=0;y<h;++y) for (UINT x=0;x<w;++x) for (UINT c=0;c<4;++c) bad += bytes[fp.Offset+(UINT64)y*fp.Footprint.RowPitch+4*x+c] != ((c==3||c==channel)?255:0); ID3D12Resource_Unmap(readback, 0, NULL); if (bad) hr=E_FAIL; }
            }
            if (SUCCEEDED(hr)) {
                const HRESULT present_hr = IDXGISwapChain3_Present(swap, 0, 0);
                const UINT64 target = base + 2*(epoch*3+f) + 2; UINT64 done = 0;
                hr = ID3D12CommandQueue_Signal(queue, fence, target);
                for (UINT wait=0;SUCCEEDED(hr)&&wait<300&&(done=ID3D12Fence_GetCompletedValue(fence))<target;++wait) Sleep(10);
                if (FAILED(hr)||done!=target) { printf("AGX_D3D12_C25: FAIL post-present completion unproven\n"); fflush(stdout); ExitProcess(1); }
                hr = present_hr;
                if (SUCCEEDED(hr)) { seen |= 1u<<index; ++frames; }
            }
            printf("C25 epoch=%u frame=%u index=%u size=%ux%u rgba_channel=%u pixels=%u bad=%u hr=0x%08lx\n",epoch,f,index,w,h,channel,w*h,bad,(unsigned long)hr); fflush(stdout);
            if (list) ID3D12GraphicsCommandList_Release(list);
            if (allocator) ID3D12CommandAllocator_Release(allocator);
            if (heap) ID3D12DescriptorHeap_Release(heap);
            if (readback) ID3D12Resource_Release(readback);
            if (buffer) ID3D12Resource_Release(buffer);
        }
        if (SUCCEEDED(hr)&&seen!=3) hr=E_FAIL;
    }
    printf("AGX_D3D12_C25: %s frames=%u resizes=%u hr=0x%08lx\n",SUCCEEDED(hr)&&frames==6&&resizes==1?"PASS":"FAIL",frames,resizes,(unsigned long)hr);
    if (swap) IDXGISwapChain3_Release(swap); if (swap1) IDXGISwapChain1_Release(swap1);
    if (factory) IDXGIFactory2_Release(factory); if (window) DestroyWindow(window);
    return FAILED(hr)||frames!=6||resizes!=1;
}

/* Original C26 variant of projectC5C: closedlist replay+volatileCBVreplacement. */
static int c26_replay_table(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
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
    ID3D12Resource *cbv2 = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    UINT *dwords = NULL;
    unsigned bad = 16384;
    UINT64 value = 0;
    parameter.DescriptorTable.NumDescriptorRanges = 2;
    parameter.DescriptorTable.pDescriptorRanges = ranges;
    HRESULT hr = uav && cbv && cbv2 && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(cbv, 0, NULL, (void **)&dwords);
    if (SUCCEEDED(hr)) {
        dwords[0] = 7; dwords[1] = 3; dwords[2] = 0; dwords[3] = 0;
        ID3D12Resource_Unmap(cbv, 0, NULL);
        hr = ID3D12Resource_Map(cbv2, 0, NULL, (void **)&dwords);
        if (SUCCEEDED(hr) && !dwords) hr = E_FAIL;
        if (SUCCEEDED(hr)) { dwords[0] = 11; dwords[1] = 3; dwords[2] = dwords[3] = 0; ID3D12Resource_Unmap(cbv2, 0, NULL); }
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &cpu_desc, &IID_ID3D12DescriptorHeap, (void **)&cpu_heap);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateDescriptorHeap(device, &gpu_desc, &IID_ID3D12DescriptorHeap, (void **)&gpu_heap);
    printf("C26 CreateDescriptorHeap x2 hr=0x%08lx\n", (unsigned long)hr);
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
        printf("C26 views step %u gpu 0x%llx\n", step, (unsigned long long)gpu_start.ptr);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    printf("C26 CreateRootSignature hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_table;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_table);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C26 CreateComputePipelineState hr=0x%08lx\n", (unsigned long)hr);
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
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
        hr = ID3D12GraphicsCommandList_Close(list);
        printf("C26 record+close hr=0x%08lx\n", (unsigned long)hr);
    }
    unsigned passes = 0;
    for (UINT pass = 0; pass < 2 && SUCCEEDED(hr); ++pass) {
        const UINT coefficient = pass ? 11 : 7;
        const UINT64 target = base + 1 + pass;
        if (pass) {
            D3D12_CONSTANT_BUFFER_VIEW_DESC replacement = {0};
            D3D12_CPU_DESCRIPTOR_HANDLE slot = gpu_cpu_start;
            slot.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            replacement.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(cbv2);
            replacement.SizeInBytes = 256;
            /* Previousexecutionfenced; changeonlyvolatileCBVdescriptor, notlist/root/PSO. */
            ID3D12Device_CreateConstantBufferView(device, &replacement, slot);
        }
        ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        hr = ID3D12CommandQueue_Signal(queue, fence, target);
        value = 0;
        for (UINT wait = 0; SUCCEEDED(hr) && wait < 300 && (value = ID3D12Fence_GetCompletedValue(fence)) < target; ++wait) Sleep(10);
        if (FAILED(hr) || value != target) {
            printf("AGX_D3D12_C26: FAIL completion unproven pass=%u hr=0x%08lx completed=%llu target=%llu\n", pass, (unsigned long)hr, (unsigned long long)value, (unsigned long long)target);
            fflush(stdout); ExitProcess(1);
        }
        D3D12_RANGE all = {0, 65536}; dwords = NULL; bad = 16384;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&dwords);
        if (SUCCEEDED(hr) && !dwords) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            bad = 0;
            for (unsigned i = 0; i < 16384; ++i) bad += dwords[i] != i * coefficient + 3u;
            printf("C26 replay=%u closed_list=%p coefficient=%u bad=%u words=16384 first=%u last=%u completed=%llu\n", pass, (void *)list, coefficient, bad, dwords[0], dwords[16383], (unsigned long long)value);
            ID3D12Resource_Unmap(readback, 0, NULL);
            if (bad) hr = E_FAIL; else ++passes;
        }
    }
    printf("AGX_D3D12_C26: %s\n", SUCCEEDED(hr) && passes == 2 && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (gpu_heap) ID3D12DescriptorHeap_Release(gpu_heap);
    if (cpu_heap) ID3D12DescriptorHeap_Release(cpu_heap);
    if (readback) ID3D12Resource_Release(readback);
    if (cbv2) ID3D12Resource_Release(cbv2);
    if (cbv) ID3D12Resource_Release(cbv);
    if (uav) ID3D12Resource_Release(uav);
    return !(SUCCEEDED(hr) && passes == 2 && bad == 0);
}

/* Original same-root compute probe; existing C26 remains unchanged. */
static int root_identity_compute(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
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
    ID3D12Resource *cbv2 = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    UINT *dwords = NULL;
    unsigned bad = 16384;
    UINT64 value = 0;
    parameter.DescriptorTable.NumDescriptorRanges = 2;
    parameter.DescriptorTable.pDescriptorRanges = ranges;
    HRESULT hr = uav && cbv && cbv2 && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(cbv, 0, NULL, (void **)&dwords);
    if (SUCCEEDED(hr)) {
        dwords[0] = 7; dwords[1] = 3; dwords[2] = 0; dwords[3] = 0;
        ID3D12Resource_Unmap(cbv, 0, NULL);
        hr = ID3D12Resource_Map(cbv2, 0, NULL, (void **)&dwords);
        if (SUCCEEDED(hr) && !dwords) hr = E_FAIL;
        if (SUCCEEDED(hr)) { dwords[0] = 11; dwords[1] = 3; dwords[2] = dwords[3] = 0; ID3D12Resource_Unmap(cbv2, 0, NULL); }
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &cpu_desc, &IID_ID3D12DescriptorHeap, (void **)&cpu_heap);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateDescriptorHeap(device, &gpu_desc, &IID_ID3D12DescriptorHeap, (void **)&gpu_heap);
    printf("ROOTC CreateDescriptorHeap x2 hr=0x%08lx\n", (unsigned long)hr);
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
        printf("ROOTC views step %u gpu 0x%llx\n", step, (unsigned long long)gpu_start.ptr);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    printf("ROOTC CreateRootSignature hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_table;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_table);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("ROOTC CreateComputePipelineState hr=0x%08lx\n", (unsigned long)hr);
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
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        printf("ROOTC repeated identical root after volatile table bind\n");
        ID3D12GraphicsCommandList_Dispatch(list, 256, 1, 1);
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, uav, 0, 65536);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
        hr = ID3D12GraphicsCommandList_Close(list);
        printf("ROOTC record+close hr=0x%08lx\n", (unsigned long)hr);
    }
    unsigned passes = 0;
    for (UINT pass = 0; pass < 2 && SUCCEEDED(hr); ++pass) {
        const UINT coefficient = pass ? 11 : 7;
        const UINT64 target = base + 1 + pass;
        if (pass) {
            D3D12_CONSTANT_BUFFER_VIEW_DESC replacement = {0};
            D3D12_CPU_DESCRIPTOR_HANDLE slot = gpu_cpu_start;
            slot.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            replacement.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(cbv2);
            replacement.SizeInBytes = 256;
            /* Previousexecutionfenced; changeonlyvolatileCBVdescriptor, notlist/root/PSO. */
            ID3D12Device_CreateConstantBufferView(device, &replacement, slot);
        }
        ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        hr = ID3D12CommandQueue_Signal(queue, fence, target);
        value = 0;
        for (UINT wait = 0; SUCCEEDED(hr) && wait < 300 && (value = ID3D12Fence_GetCompletedValue(fence)) < target; ++wait) Sleep(10);
        if (FAILED(hr) || value != target) {
            printf("AGX_D3D12_ROOTC: FAIL completion unproven pass=%u hr=0x%08lx completed=%llu target=%llu\n", pass, (unsigned long)hr, (unsigned long long)value, (unsigned long long)target);
            fflush(stdout); ExitProcess(1);
        }
        D3D12_RANGE all = {0, 65536}; dwords = NULL; bad = 16384;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&dwords);
        if (SUCCEEDED(hr) && !dwords) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            bad = 0;
            for (unsigned i = 0; i < 16384; ++i) bad += dwords[i] != i * coefficient + 3u;
            printf("ROOTC replay=%u closed_list=%p coefficient=%u bad=%u words=16384 first=%u last=%u completed=%llu\n", pass, (void *)list, coefficient, bad, dwords[0], dwords[16383], (unsigned long long)value);
            ID3D12Resource_Unmap(readback, 0, NULL);
            if (bad) hr = E_FAIL; else ++passes;
        }
    }
    printf("AGX_D3D12_ROOTC: %s\n", SUCCEEDED(hr) && passes == 2 && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (gpu_heap) ID3D12DescriptorHeap_Release(gpu_heap);
    if (cpu_heap) ID3D12DescriptorHeap_Release(cpu_heap);
    if (readback) ID3D12Resource_Release(readback);
    if (cbv2) ID3D12Resource_Release(cbv2);
    if (cbv) ID3D12Resource_Release(cbv);
    if (uav) ID3D12Resource_Release(uav);
    return !(SUCCEEDED(hr) && passes == 2 && bad == 0);
}
static int step_root_identity_graphics(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 8); }

/* EXP1820: nBody ARM64 hit a fail-closed draw, the runtime removed the
 * device, the process died through __fastfail and the whole J313 reset
 * seconds later. Each teardown mode runs alone in its own process after one
 * real draw: "removed-release" (ID3D12Device5::RemoveDevice, ordered
 * release), "kill-live" (TerminateProcess with live objects) and
 * "listerror-kill" (a fail-closed ResolveSubresource removes the device,
 * then TerminateProcess). Prints AGX_D3D12_TEARDOWN_<mode>: REACHED before
 * the exit; the verdict is whether the guest survives it. */
static int teardown(IDXGIAdapter1 *adapter, const char *mode) {
    ID3D12Device *device = NULL;
    ID3D12CommandQueue *queue = NULL;
    ID3D12Fence *fence = NULL;
    D3D12_COMMAND_QUEUE_DESC desc = {D3D12_COMMAND_LIST_TYPE_DIRECT};
    HRESULT hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandQueue(device, &desc, &IID_ID3D12CommandQueue, (void **)&queue);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    const int drew = SUCCEEDED(hr) ? !draw_triangle(device, queue, fence, 0, 1) : 0;
    printf("TEARDOWN %s device hr=0x%08lx draw %s\n", mode, (unsigned long)hr, drew ? "PASS" : "FAIL");
    if (FAILED(hr)) return 1;
    if (!strcmp(mode, "removed-release")) {
        ID3D12Device5 *device5 = NULL;
        hr = ID3D12Device_QueryInterface(device, &IID_ID3D12Device5, (void **)&device5);
        if (SUCCEEDED(hr)) {
            ID3D12Device5_RemoveDevice(device5);
            ID3D12Device5_Release(device5);
        }
        printf("TEARDOWN %s removed reason=0x%08lx\n", mode, (unsigned long)ID3D12Device_GetDeviceRemovedReason(device));
        printf("AGX_D3D12_TEARDOWN_%s: REACHED\n", mode);
        ID3D12Fence_Release(fence);
        ID3D12CommandQueue_Release(queue);
        ID3D12Device_Release(device);
        return 0;
    }
    if (!strcmp(mode, "listerror-kill")) {
        ID3D12CommandAllocator *allocator = NULL;
        ID3D12GraphicsCommandList *list = NULL;
        ID3D12Resource *a = committed_buffer(device, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
        ID3D12Resource *b = committed_buffer(device, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
        hr = a && b ? ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                          &IID_ID3D12CommandAllocator, (void **)&allocator) : E_FAIL;
        if (SUCCEEDED(hr))
            hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                                                &IID_ID3D12GraphicsCommandList, (void **)&list);
        if (SUCCEEDED(hr)) {
            /* fail-closed in the UMD (pfnResourceResolveSubresource) */
            ID3D12GraphicsCommandList_ResolveSubresource(list, a, 0, b, 0, DXGI_FORMAT_R32_UINT);
            hr = ID3D12GraphicsCommandList_Close(list);
            ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
            ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        }
        printf("TEARDOWN %s close hr=0x%08lx removed reason=0x%08lx\n", mode, (unsigned long)hr,
               (unsigned long)ID3D12Device_GetDeviceRemovedReason(device));
    } else if (strcmp(mode, "kill-live")) {
        printf("TEARDOWN unknown mode %s\n", mode);
        return 2;
    }
    printf("AGX_D3D12_TEARDOWN_%s: REACHED\n", mode);
    TerminateProcess(GetCurrentProcess(), 7);
    return 7;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0); /* EXP1368: a crash lost every buffered line */
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
            if (argc > 2 && !strcmp(argv[1], "teardown")) return teardown(adapter, argv[2]);
            hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
                                   &IID_ID3D12Device, (void **)&device);
            printf("D3D12CreateDevice hr=0x%08lx\n", (unsigned long)hr);
            failed = FAILED(hr);
            if (device) {
                failed |= committed_buffer_round_trip(device);
                failed |= queue_fence_signal(device);
                ID3D12Device_Release(device);
                (void)run_isolated(adapter, "C6C", step_c6c);
                (void)run_isolated(adapter, "C6D", step_c6d);
                (void)run_isolated(adapter, "C6E", draw_textured);
                (void)run_isolated(adapter, "C6F", draw_depth);
                (void)run_isolated(adapter, "C8C", placed_buffer);
                (void)run_isolated(adapter, "C9", compute_images);
                (void)run_isolated(adapter, "C10", clear_uavs);
                (void)run_isolated(adapter, "C11", step_c11);
                (void)run_isolated(adapter, "C12", queue_types);
                (void)run_isolated(adapter, "C13", step_c13);
                (void)run_isolated(adapter, "C14", step_c14);
                (void)run_isolated(adapter, "C14NC", step_c14nc);
                (void)run_isolated(adapter, "C14B", compute_indirect);
                (void)run_isolated(adapter, "C16A", step_c16a);
                (void)run_isolated(adapter, "C17A", compute_spaces);
                (void)run_isolated(adapter, "C17B", step_c17b);
                (void)run_isolated(adapter, "C17C", step_c17c);
                (void)run_isolated(adapter, "C18A", step_c18a);
                (void)run_isolated(adapter, "C18B", step_c18b);
                (void)run_isolated(adapter, "C19A", step_c19a);
                (void)run_isolated(adapter, "C19B", step_c19b);
                (void)run_isolated(adapter, "C19C", step_c19c);
                (void)run_isolated(adapter, "C19D", step_c19d);
                (void)run_isolated(adapter, "C20A", step_c20a);
                (void)run_isolated(adapter, "C20B", step_c20b);
                (void)run_isolated(adapter, "C21A", volume_copy);
                (void)run_isolated(adapter, "C21B", step_c21b);
                (void)run_isolated(adapter, "C21C", step_c21c);
                (void)run_isolated(adapter, "C21D", volume_rtv);
                (void)run_isolated(adapter, "C22A", step_c22a);
                (void)run_isolated(adapter, "C22B", step_c22b);
                (void)run_isolated(adapter,"C23A",texture1d_copy);
                (void)run_isolated(adapter,"C23B",step_c23b);
                (void)run_isolated(adapter,"C23C",step_c23c);
                (void)run_isolated(adapter,"C24A",step_c24a);
                (void)run_isolated(adapter,"C24B",step_c24b);
                (void)run_isolated(adapter,"C24C",step_c24c);
                (void)run_isolated(adapter, "C27A", step_c27a);
                (void)run_isolated(adapter, "C27B", step_c27b);
                (void)run_isolated(adapter, "C7A", swapchain_present);
                (void)run_isolated(adapter, "C25", c25_present_resize);
                (void)run_isolated(adapter, "C26", c26_replay_table);
                (void)run_isolated(adapter, "ROOTC", root_identity_compute);
                (void)run_isolated(adapter, "ROOTG", step_root_identity_graphics);
                printf("AGX_D3D12_C21E: SKIP known UNCOMPLETED baseline; no new hypothesis\n");
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
