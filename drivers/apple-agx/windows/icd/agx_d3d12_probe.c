#define COBJMACROS
#include <windows.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <d3dkmthk.h>
#include "agx_d3d12_probe_completion.h"
#include <d3d12sdklayers.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>
#include <wchar.h>

#include "agx_d3d12_probe_gs.h"
#include "agx_d3d12_probe_hs.h"
#include "agx_d3d12_probe_ds.h"
#include "agx_d3d12_probe_hs_const.h"
#include "agx_d3d12_probe_hs_const_ccw.h"
#include "agx_d3d12_probe_ds_vcolor.h"
#include "agx_d3d12_probe_cs_nbody.h"
#include "agx_d3d12_probe_cs_tgsm_loop.h"
#include "agx_d3d12_probe_cs_bc1_load.h"
#include "agx_d3d12_probe_cs_append.h"
#include "agx_d3d12_probe_cs_ranges.h"
#include "agx_d3d12_probe_cs_c47b.h"
#include "agx_d3d12_probe_cs_cb_dxil.h"
#include "agx_d3d12_probe_cs_wave_dxil.h"
#include "agx_d3d12_probe_cs_c49.h"
#include "agx_d3d12_probe_cs_membw.h"
#include "agx_d3d12_probe_cs_membw_uav.h"
#include "agx_d3d12_probe_cs_membw_atom.h"
#include "agx_d3d12_probe_vs_dxil.h"
#include "agx_d3d12_probe_ps_dxil.h"
#include "agx_d3d12_probe_ps_c57.h"
#include "agx_d3d12_probe_vs_c59.h"
#include "agx_d3d12_probe_ps_c59.h"
#include "agx_d3d12_probe_ps_c59b.h"
#include "agx_d3d12_probe_vs_c60.h"
#include "agx_d3d12_probe_ps_c60.h"
#include "agx_d3d12_probe_ps_c61.h"
#include "agx_d3d12_probe_hs_c61b.h"
#include "agx_d3d12_probe_ds_c61b.h"
#include "agx_d3d12_probe_gs_c61c.h"
#include "agx_d3d12_probe_ps_c61d.h"
#include "dxbc_tir.h"
#include "agx_d3d12_probe_ps_view_dxil.h"
#include "agx_d3d12_probe_cs_cb_dxil62.h"
#include "agx_d3d12_probe_cs_sm65_dxil.h"
#include "agx_d3d12_probe_cs_texture1d_load.h"
#include "agx_d3d12_probe_cs_texture1d_store.h"
#include "agx_d3d12_probe_cs_texture1d_array_load.h"
#include "agx_d3d12_probe_cs_texture1d_array_store.h"
#include "agx_d3d12_probe_cs.h"
#include "agx_d3d12_probe_q3_compute.h"
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
#include "agx_d3d12_probe_cs_c41a.h"
#include "agx_d3d12_probe_cs_c41b.h"
#include "agx_d3d12_probe_cs_c42c.h"
#include "agx_d3d12_probe_cs_c42d.h"
#include "agx_d3d12_probe_cs_c42e.h"
#include "agx_d3d12_probe_cs_c42j.h"
#include "agx_d3d12_probe_cs_c42k.h"
#include "agx_d3d12_probe_ps_c42g.h"
#include "agx_d3d12_probe_ps_c42h.h"

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
/* rc82 (RC82A): a 64 KiB reserved buffer, one 64 KiB tile left NULL-mapped. */
#define RC82_SOFT_PROBE_OFFSET 0x8000u
static ID3D12Resource *reserved_buffer(ID3D12Device *device) {
    D3D12_RESOURCE_DESC desc = {0};
    ID3D12Resource *buffer = NULL;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 65536;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; /* a buffer, also reserved */
    HRESULT hr = ID3D12Device_CreateReservedResource(device, &desc, D3D12_RESOURCE_STATE_COMMON, NULL,
                                                     &IID_ID3D12Resource, (void **)&buffer);
    printf("RC82A CreateReservedResource hr=0x%08lx\n", (unsigned long)hr);
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
 * 5i + 11 + 1000; a non-zero 'every' expects that word in every dword
 * instead (C48A). */
static int compute_constants_code(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, const void *code,
                                  SIZE_T code_bytes, const char *tag, UINT every) {
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
    printf("%s CreateRootSignature hr=0x%08lx\n", tag, (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = code;
        pso_desc.CS.BytecodeLength = code_bytes;
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("%s CreateComputePipelineState hr=0x%08lx\n", tag, (unsigned long)hr);
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
        printf("%s record+close hr=0x%08lx\n", tag, (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        hr = ID3D12CommandQueue_Signal(queue, fence, 5);
        for (int i = 0; i < 300 && (value = ID3D12Fence_GetCompletedValue(fence)) < 5; ++i) Sleep(10);
        printf("%s execute+signal hr=0x%08lx completed %llu\n", tag, (unsigned long)hr, (unsigned long long)value);
    }
    if (SUCCEEDED(hr) && value == 5) {
        D3D12_RANGE all = {0, 65536};
        dwords = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&dwords);
        if (SUCCEEDED(hr) && dwords) {
            bad = 0;
            for (unsigned i = 0; i < 16384; ++i) bad += dwords[i] != (every ? every : i * 5u + 1011u);
            printf("%s readback bad=%u first=%u,%u last=%u\n", tag, bad, dwords[0], dwords[1], dwords[16383]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_%s: %s\n", tag, SUCCEEDED(hr) && value == 5 && bad == 0 ? "PASS" : "FAIL");
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
static int compute_constants(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence) {
    return compute_constants_code(device, queue, fence, g_agx_d3d12_probe_cs_cb, sizeof(g_agx_d3d12_probe_cs_cb), "C5B", 0u);
}
/* C44A (D3D12-C44-dxil.md): C5B's program compiled by DXC as cs_6_0 -- the
 * DXIL frontend (dxil-spirv, spirv_to_nir) under the R1 interface the
 * driver offers APPLE_AGX_D3D12_SM6=1 processes; same root signature and
 * expected words (5i + 1011). */
static int shader_model_6(ID3D12Device *device, const char *tag) {
    D3D12_FEATURE_DATA_SHADER_MODEL model = {D3D_SHADER_MODEL_6_0};
    const HRESULT hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model));
    printf("%s HighestShaderModel 0x%x hr=0x%08lx\n", tag, (unsigned)model.HighestShaderModel, (unsigned long)hr);
    return SUCCEEDED(hr) && model.HighestShaderModel >= D3D_SHADER_MODEL_6_0;
}
static int step_c44a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) {
    (void)b;
    if (!shader_model_6(d, "C44A")) {
        printf("AGX_D3D12_C44A: FAIL\n");
        return 1;
    }
    return compute_constants_code(d, q, f, g_agx_d3d12_probe_cs_cb_dxil, sizeof(g_agx_d3d12_probe_cs_cb_dxil), "C44A", 0u);
}
/* C48A (D3D12 wave operations): the driver reports WaveOps with one wave
 * size (OPTIONS1, interface 0012; AGX: 32) and agx_d3d12_probe_cs_wave_dxil.hlsl's
 * fourteen wave checks hold in every thread (every dword 0x3fff; WARP's
 * 4-lane waves fail only the 32-lane check, 0x3ffe). */
static int step_c48a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options = {0};
    const HRESULT hr = ID3D12Device_CheckFeatureSupport(d, D3D12_FEATURE_D3D12_OPTIONS1, &options, sizeof(options));
    (void)b;
    printf("C48A WaveOps %d lanes %u..%u total %u hr=0x%08lx\n", (int)options.WaveOps, options.WaveLaneCountMin,
           options.WaveLaneCountMax, options.TotalLaneCount, (unsigned long)hr);
    if (!shader_model_6(d, "C48A") || FAILED(hr) || !options.WaveOps ||
        options.WaveLaneCountMin != options.WaveLaneCountMax) {
        printf("AGX_D3D12_C48A: FAIL\n");
        return 1;
    }
    return compute_constants_code(d, q, f, g_agx_d3d12_probe_cs_wave_dxil, sizeof(g_agx_d3d12_probe_cs_wave_dxil), "C48A",
                                  options.WaveLaneCountMin == 32 ? 0x3fffu : 0x3ffeu);
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
/* rc77 (fault_va != 0, step RC77D, opt-in only): the C5D dispatch with its
 * root SRV t0 at an unmapped GPU VA. D3D12 does not validate root descriptor
 * addresses and rc75 compiles root-descriptor loads without bounds checks, so
 * this is the shader data-read fault a buggy application can make (unlike
 * the rc37 drill's CDM control-stream fault). PASS = this device is removed
 * and the process is alive; run a fresh step (C5D) afterwards. */
/* rc82 (soft_probe, step RC82A): the soft-fault discriminator. RC77D never
 * reached the GPU (the UMD refuses a root VA outside every heap at record,
 * removed=0x887a0020, EXP2009), so here the root SRV t0 names a real
 * resource: a 64 KiB reserved buffer (one tile, NULL-mapped: the winsys maps
 * its tile scratch) at its VA + 32 KiB. Root views are unchecked (rc75), so
 * threads 8192..16383 read the 64 KiB after the reservation, the
 * never-mapped guard tile every winsys VA range carries
 * (agx_win32_gpuva.h AGX_WIN32_GPUVA_RANGE_GUARD): a shader data read of an
 * unmapped page inside a valid-looking range. With soft faults (RegionC
 * fault_control 0xb; Asahi asahi_drm.h: loads of unmapped memory return
 * zero) those threads write 0 + 2 * (i + 100), the fence completes, the
 * device is not removed and the KMD records no Wom1GpuFault event. Hard
 * faults instead raise a fault event (device removal or a TDR). Threads
 * below 8192 read the scratch tile (contents not checked, first printed). */
static int compute_srv_va(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, int high_slots,
                          UINT64 fault_va, int soft_probe) {
    const char *step = soft_probe ? "RC82A" : fault_va ? "RC77D" : high_slots ? "C16A" : "C5D";
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
    ID3D12Resource *a = soft_probe ? reserved_buffer(device)
                                   : committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *b = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    UINT *dwords = NULL;
    unsigned bad = 16384;
    UINT64 value = 0;
    const int faulting = fault_va != 0 || soft_probe;
    parameters[0].Descriptor.ShaderRegister = high_slots ? 7u : 0u;
    parameters[1].Descriptor.ShaderRegister = high_slots ? 127u : 0u;
    parameters[3].Descriptor.ShaderRegister = 13;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &range;
    HRESULT hr = uav && a && b && readback && (!high_slots || cb) ? S_OK : E_FAIL;
    if (SUCCEEDED(hr) && !soft_probe) {
        hr = ID3D12Resource_Map(a, 0, NULL, (void **)&dwords);
        if (SUCCEEDED(hr)) {
            for (unsigned i = 0; i < 16384; ++i) dwords[i] = 3u * i;
            ID3D12Resource_Unmap(a, 0, NULL);
        }
    }
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(b, 0, NULL, (void **)&dwords);
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
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(
            list, 1, fault_va ? fault_va
                     : ID3D12Resource_GetGPUVirtualAddress(a) + (soft_probe ? RC82_SOFT_PROBE_OFFSET : 0u));
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
        if (fault_va) printf("%s FAULT_DISPATCH va=0x%llx\n", step, (unsigned long long)fault_va);
        if (soft_probe)
            printf("%s SOFT_DISPATCH va=0x%llx guard=0x%llx\n", step,
                   (unsigned long long)(ID3D12Resource_GetGPUVirtualAddress(a) + RC82_SOFT_PROBE_OFFSET),
                   (unsigned long long)(ID3D12Resource_GetGPUVirtualAddress(a) + 65536u));
        for (int i = 0; i < (faulting ? 1500 : 300) && (value = ID3D12Fence_GetCompletedValue(fence)) < 7; ++i) {
            if (faulting && FAILED(ID3D12Device_GetDeviceRemovedReason(device))) break;
            Sleep(10);
        }
        printf("%s execute+signal hr=0x%08lx completed %llu\n", step, (unsigned long)hr, (unsigned long long)value);
    }
    if (fault_va) {
        const HRESULT removed = device ? ID3D12Device_GetDeviceRemovedReason(device) : E_FAIL;
        printf("AGX_D3D12_RC77D: %s removed=0x%08lx completed=%llu\n",
               SUCCEEDED(hr) && FAILED(removed) ? "PASS" : "FAIL", (unsigned long)removed,
               (unsigned long long)value);
        bad = SUCCEEDED(hr) && FAILED(removed) ? 0u : 1u;
        hr = E_ABORT; /* skip the readback and the C5D verdict below */
    }
    if (soft_probe) {
        const HRESULT removed = device ? ID3D12Device_GetDeviceRemovedReason(device) : E_FAIL;
        unsigned guard_bad = 8192u;
        UINT scratch_first = 0u, guard_first = 0u, guard_last = 0u;
        if (SUCCEEDED(hr) && value == 7 && SUCCEEDED(removed)) {
            D3D12_RANGE all = {0, 65536};
            dwords = NULL;
            if (SUCCEEDED(ID3D12Resource_Map(readback, 0, &all, (void **)&dwords)) && dwords) {
                /* a = 0 from the unmapped guard tile: 2 * B[i] = 2i + 200 */
                guard_bad = 0u;
                for (unsigned i = 8192; i < 16384; ++i) guard_bad += dwords[i] != 2u * i + 200u;
                scratch_first = dwords[0] - 200u;
                guard_first = dwords[8192];
                guard_last = dwords[16383];
                ID3D12Resource_Unmap(readback, 0, NULL);
            }
        }
        printf("RC82A readback guard_bad=%u guard_first=%u guard_last=%u scratch_a0=%u\n", guard_bad, guard_first,
               guard_last, scratch_first);
        printf("AGX_D3D12_RC82A: %s removed=0x%08lx completed=%llu guard_bad=%u\n",
               SUCCEEDED(hr) && value == 7 && SUCCEEDED(removed) && guard_bad == 0u ? "PASS" : "FAIL",
               (unsigned long)removed, (unsigned long long)value, guard_bad);
        bad = SUCCEEDED(hr) && value == 7 && SUCCEEDED(removed) && guard_bad == 0u ? 0u : 1u;
        hr = E_ABORT; /* skip the C5D readback and verdict below */
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
    if (!faulting)
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
    return faulting ? bad != 0u : !(SUCCEEDED(hr) && bad == 0);
}
static int compute_srv(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, int high_slots) {
    return compute_srv_va(device, queue, fence, high_slots, 0, 0);
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
/* C76A: cross-process OpenSharedHandle; one import observes two fenced writes.
 * Events transfer CPU ownership only after run_list proves GPU completion. */
static int g_warp;
static HANDLE c76_shared, c76_ready, c76_go, c76_done, c76_parent;
/* The presentation receipt is from an internal helper also used by D3D12;
 * it does not identify the runtime DDI. Keep all three boundaries distinct. */
static unsigned c76_open_receipt_kind(const char *line) {
    if (!strncmp(line, "d3d12-open-heap-resource ", sizeof("d3d12-open-heap-resource ") - 1)) return 1;
    if (!strncmp(line, "measure-d3d12-shared-import ", sizeof("measure-d3d12-shared-import ") - 1)) return 2;
    if (!strncmp(line, "presentation-open ", sizeof("presentation-open ") - 1)) return 3;
    return 0;
}
static HRESULT c76_wait(HANDLE event, HANDLE peer, const char *stage) {
    HANDLE handles[2] = {event, peer};
    DWORD result = WaitForMultipleObjects(2, handles, FALSE, 15000);
    if (result != WAIT_OBJECT_0) printf("C76A %s wait=0x%08lx\n", stage, (unsigned long)result);
    return result == WAIT_OBJECT_0 ? S_OK : E_FAIL;
}
static D3D12_RESOURCE_DESC c76_description(void) {
    D3D12_RESOURCE_DESC desc = {0};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 37; desc.Height = 29; desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    return desc;
}
static D3D12_TEXTURE_COPY_LOCATION c76_footprint(ID3D12Resource *buffer) {
    D3D12_TEXTURE_COPY_LOCATION location = {0};
    location.pResource = buffer;
    location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    location.PlacedFootprint.Footprint.Width = 37; location.PlacedFootprint.Footprint.Height = 29;
    location.PlacedFootprint.Footprint.Depth = 1; location.PlacedFootprint.Footprint.RowPitch = 256;
    return location;
}
static int c76_child_read(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    ID3D12Resource *opened = NULL, *readback = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    unsigned passes = 0;
    HRESULT hr = ID3D12Device_OpenSharedHandle(device, c76_shared, &IID_ID3D12Resource, (void **)&opened);
    printf("C76A route=child-process pid=%lu OpenSharedHandle hr=0x%08lx\n", GetCurrentProcessId(), (unsigned long)hr);
    CloseHandle(c76_shared); c76_shared = NULL;
    if (SUCCEEDED(hr)) {
        D3D12_RESOURCE_DESC actual, desc = c76_description();
        ID3D12Resource_GetDesc(opened, &actual);
        if (actual.Dimension != desc.Dimension || actual.Width != desc.Width || actual.Height != desc.Height ||
            actual.Format != desc.Format || actual.MipLevels != 1 || actual.DepthOrArraySize != 1 ||
            actual.SampleDesc.Count != 1 || actual.SampleDesc.Quality != 0 || actual.Flags != desc.Flags) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) {
        readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!readback) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                                                       &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr) && !SetEvent(c76_ready)) hr = HRESULT_FROM_WIN32(GetLastError());
    for (unsigned pass = 0; SUCCEEDED(hr) && pass < 2; ++pass) {
        hr = c76_wait(c76_go, c76_parent, "consumer-go");
        if (SUCCEEDED(hr) && pass) {
            hr = ID3D12CommandAllocator_Reset(allocator);
            if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Reset(list, allocator, NULL);
        }
        if (SUCCEEDED(hr)) {
            D3D12_TEXTURE_COPY_LOCATION target = c76_footprint(readback), source = {0};
            source.pResource = opened; source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            transition(list, opened, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ID3D12GraphicsCommandList_CopyTextureRegion(list, &target, 0, 0, 0, &source, NULL);
            transition(list, opened, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
            hr = run_list(queue, fence, list, base + pass + 1, "C76A consumer");
            if (FAILED(hr)) { printf("C76A FAIL consumer completion unproven\n"); fflush(stdout); ExitProcess(1); }
        }
        if (SUCCEEDED(hr)) {
            D3D12_RANGE range = {0, 29 * 256};
            unsigned char *bytes = NULL;
            hr = ID3D12Resource_Map(readback, 0, &range, (void **)&bytes);
            if (SUCCEEDED(hr) && !bytes) hr = E_FAIL;
            if (SUCCEEDED(hr)) {
                unsigned bad = 0;
                for (unsigned y = 0; y < 29; ++y) for (unsigned x = 0; x < 37 * 4; ++x)
                    bad += bytes[y * 256 + x] != (unsigned char)(x * 7 + y * 11 + pass * 83);
                ID3D12Resource_Unmap(readback, 0, NULL);
                printf("C76A child pass=%u mismatches=%u\n", pass, bad);
                if (bad) hr = E_FAIL; else ++passes;
            }
        }
        if (SUCCEEDED(hr) && !SetEvent(c76_done)) hr = HRESULT_FROM_WIN32(GetLastError());
    }
    printf("C76A child result=%s passes=%u hr=0x%08lx\n", SUCCEEDED(hr) && passes == 2 ? "PASS" : "FAIL", passes, (unsigned long)hr);
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (readback) ID3D12Resource_Release(readback);
    if (opened) ID3D12Resource_Release(opened);
    return !(SUCCEEDED(hr) && passes == 2);
}
static int c76_shared_open(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    ID3D12Resource *created = NULL, *upload = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE shared = NULL, ready = NULL, go = NULL, done = NULL, parent = NULL, log = INVALID_HANDLE_VALUE, input = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION process = {0};
    STARTUPINFOEXW startup = {0};
    WCHAR exe[MAX_PATH], command[2 * MAX_PATH], temp[MAX_PATH], log_path[MAX_PATH] = {0}, trace_path[MAX_PATH] = {0};
    WCHAR saved_trace[32768];
    DWORD saved_length = 0, exit_code = 1;
    BOOL attributes = FALSE, environment_changed = FALSE, child_exited = FALSE;
    SIZE_T attribute_bytes = 0;
    unsigned passes = 0;
    LUID luid;
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = c76_description();
    HRESULT hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_SHARED, &desc,
        D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void **)&created);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateSharedHandle(device, (ID3D12DeviceChild *)created, &sa, GENERIC_ALL, NULL, &shared);
    printf("C76A route=child-process create+share hr=0x%08lx\n", (unsigned long)hr);
    if (FAILED(hr)) goto cleanup;
    ready = CreateEventW(&sa, FALSE, FALSE, NULL);
    go = CreateEventW(&sa, FALSE, FALSE, NULL);
    done = CreateEventW(&sa, FALSE, FALSE, NULL);
    if (!ready || !go || !done || !DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(),
        &parent, SYNCHRONIZE, TRUE, 0)) { hr = E_FAIL; goto cleanup; }
    DWORD length = GetModuleFileNameW(NULL, exe, MAX_PATH);
    DWORD temp_length = GetTempPathW(MAX_PATH, temp);
    if (!length || length >= MAX_PATH || !temp_length || temp_length >= MAX_PATH ||
        !GetTempFileNameW(temp, L"c76", 0, log_path) || !GetTempFileNameW(temp, L"c76", 0, trace_path)) { hr = E_FAIL; goto cleanup; }
    log = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    if (log == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) { hr = E_FAIL; goto cleanup; }
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = log;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_bytes);
    startup.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, attribute_bytes);
    if (!startup.lpAttributeList) { hr = E_OUTOFMEMORY; goto cleanup; }
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_bytes)) { hr = E_FAIL; goto cleanup; }
    attributes = TRUE;
    HANDLE inherited[] = {shared, ready, go, done, parent, log, input};
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited, sizeof(inherited), NULL, NULL)) { hr = E_FAIL; goto cleanup; }
    ID3D12Device_GetAdapterLuid(device, &luid);
    if (swprintf_s(command, ARRAYSIZE(command), L"\"%ls\" --c76-child %llu %llu %llu %llu %llu %lu %lu %u",
        exe, (unsigned long long)(UINT_PTR)shared, (unsigned long long)(UINT_PTR)ready,
        (unsigned long long)(UINT_PTR)go, (unsigned long long)(UINT_PTR)done, (unsigned long long)(UINT_PTR)parent,
        luid.LowPart, (DWORD)luid.HighPart, (unsigned)g_warp) < 0) { hr = E_FAIL; goto cleanup; }
    saved_length = GetEnvironmentVariableW(L"APPLE_AGX_UMD_TRACE_FILE", saved_trace, ARRAYSIZE(saved_trace));
    if (saved_length >= ARRAYSIZE(saved_trace) || !SetEnvironmentVariableW(L"APPLE_AGX_UMD_TRACE_FILE", trace_path)) { hr = E_FAIL; goto cleanup; }
    environment_changed = TRUE;
    if (!CreateProcessW(exe, command, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &startup.StartupInfo, &process))
        hr = HRESULT_FROM_WIN32(GetLastError());
    SetEnvironmentVariableW(L"APPLE_AGX_UMD_TRACE_FILE", saved_length ? saved_trace : NULL);
    environment_changed = FALSE;
    if (FAILED(hr)) goto cleanup;
    printf("C76A child pid=%lu log=%ls trace=%ls\n", process.dwProcessId, log_path, trace_path);
    CloseHandle(process.hThread); process.hThread = NULL;
    CloseHandle(log); log = INVALID_HANDLE_VALUE;
    hr = c76_wait(ready, process.hProcess, "opened");
    if (FAILED(hr)) goto cleanup;
    CloseHandle(shared); shared = NULL;
    upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!upload) { hr = E_FAIL; goto cleanup; }
    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                                                       &IID_ID3D12GraphicsCommandList, (void **)&list);
    for (unsigned pass = 0; SUCCEEDED(hr) && pass < 2; ++pass) {
        unsigned char *bytes = NULL;
        hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
        if (SUCCEEDED(hr) && !bytes) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            for (unsigned y = 0; y < 29; ++y) for (unsigned x = 0; x < 37 * 4; ++x)
                bytes[y * 256 + x] = (unsigned char)(x * 7 + y * 11 + pass * 83);
            ID3D12Resource_Unmap(upload, 0, NULL);
            if (pass) {
                hr = ID3D12CommandAllocator_Reset(allocator);
                if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Reset(list, allocator, NULL);
            }
        }
        if (SUCCEEDED(hr)) {
            D3D12_TEXTURE_COPY_LOCATION source = c76_footprint(upload), target = {0};
            target.pResource = created; target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            transition(list, created, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12GraphicsCommandList_CopyTextureRegion(list, &target, 0, 0, 0, &source, NULL);
            transition(list, created, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
            hr = run_list(queue, fence, list, base + pass + 1, "C76A producer");
            if (FAILED(hr)) { printf("AGX_D3D12_C76A: FAIL producer completion unproven\n"); fflush(stdout); TerminateProcess(process.hProcess, 1); ExitProcess(1); }
            if (pass) { ID3D12Resource_Release(created); created = NULL; }
            if (!SetEvent(go)) hr = HRESULT_FROM_WIN32(GetLastError());
            if (SUCCEEDED(hr)) hr = c76_wait(done, process.hProcess, "readback");
            if (SUCCEEDED(hr)) ++passes;
        }
    }
cleanup:
    if (environment_changed) SetEnvironmentVariableW(L"APPLE_AGX_UMD_TRACE_FILE", saved_length ? saved_trace : NULL);
    if (process.hProcess) {
        DWORD wait = WaitForSingleObject(process.hProcess, FAILED(hr) ? 0 : 15000);
        if (wait != WAIT_OBJECT_0) {
            hr = E_FAIL;
            TerminateProcess(process.hProcess, 1);
            wait = WaitForSingleObject(process.hProcess, 5000);
        }
        child_exited = wait == WAIT_OBJECT_0;
        if (!child_exited || !GetExitCodeProcess(process.hProcess, &exit_code) || exit_code != 0) hr = E_FAIL;
        CloseHandle(process.hProcess);
    }
    if (process.hThread) CloseHandle(process.hThread);
    if (attributes) DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (startup.lpAttributeList) HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (shared) CloseHandle(shared);
    if (ready) CloseHandle(ready);
    if (go) CloseHandle(go);
    if (done) CloseHandle(done);
    if (parent) CloseHandle(parent);
    if (child_exited) {
        char line[1024]; unsigned open_receipts = 0, import_receipts = 0, presentation_receipts = 0;
        FILE *file = NULL;
        (void)_wfopen_s(&file, log_path, L"r");
        if (file) { while (fgets(line, sizeof(line), file)) fputs(line, stdout); fclose(file); }
        file = NULL;
        (void)_wfopen_s(&file, trace_path, L"r");
        if (file) {
            while (fgets(line, sizeof(line), file)) {
                const unsigned kind = c76_open_receipt_kind(line);
                if (kind == 1) ++open_receipts;
                if (kind == 2) ++import_receipts;
                if (kind == 3) ++presentation_receipts;
                if (kind) fputs(line, stdout);
            }
            fclose(file);
        }
        printf("C76A umd_open=%s d3d12-open-receipts=%u shared-import-receipts=%u presentation-helper-receipts=%u kmd_open=UNVERIFIED (collect KMD trace)\n",
            open_receipts || import_receipts ? "OBSERVED" : "UNVERIFIED", open_receipts, import_receipts, presentation_receipts);
    } else printf("C76A umd_open=UNVERIFIED kmd_open=UNVERIFIED child did not exit normally\n");
    printf("AGX_D3D12_C76A: %s route=child-process hr=0x%08lx passes=%u child_exit=%lu\n",
           SUCCEEDED(hr) && passes == 2 && child_exited && exit_code == 0 ? "PASS" : "FAIL", (unsigned long)hr, passes, exit_code);
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (upload) ID3D12Resource_Release(upload);
    if (created) ID3D12Resource_Release(created);
    return !(SUCCEEDED(hr) && passes == 2 && child_exited && exit_code == 0);
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
static ID3D12Resource *c21_buffer(ID3D12Device *device, D3D12_HEAP_TYPE type, UINT64 bytes,
                                 D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state);
static int draw_triangle(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int mode) {
    /* EXP1369: the indexed mode must build the C6b (empty) root signature */
    /* RC38: bounded zero/one GPU stream, then an ordinary draw using the
     * same state. Named opt-in steps only; not part of the default suite. */
    const int stream_large = mode >= 23 && mode <= 25;
    /* RC59G (GravityMark HD50k loading, EXP1976-1980 UMD logs): one R32
     * indexed counted ExecuteIndirect with MaxCount 60000 whose records and
     * count a GPU copy writes into a DEFAULT buffer in the same list, then
     * an INDIRECT_ARGUMENT barrier; 2400 records are live. No query. */
    const int stream_gpu_args = mode == 26;
    const UINT stream_max = stream_gpu_args ? 60000u : stream_large ? 2400u : 2u;
    const UINT stream_calls = mode == 25 ? 7u : 1u;
    const UINT stream_count_offset = stream_large || stream_gpu_args ? 2048u + 2400u * 20u : 2112u;
    const UINT stream_gpu_count_offset = 60000u * 20u; /* past every record */
    const int stream_matrix = mode >= 17 && mode <= 22;
    const int stream_probe = (mode >= 14 && mode <= 16) || stream_matrix || stream_large || stream_gpu_args;
    const UINT stream_count = stream_large || stream_gpu_args ? 2400u : stream_matrix ? (UINT)(mode - 17) % 3u : mode == 14 ? 0u : 1u;
    const int stream_index32 = (mode >= 20 && mode <= 22) || stream_large || stream_gpu_args;
    const int indexed = mode == 2 || mode == 16 || stream_matrix || stream_large || stream_gpu_args;
    const int redundant_root = mode == 8;
    /* C30 (EXP1825 HelloBundles/Bundles): mode 9 is mode 1 with the draw in
     * a bundle -- the bundle sets the same root signature, the PSO, topology
     * and vertex buffer; the root constants, CBV, render target, viewport and
     * scissor come from the calling list */
    const int bundled = mode == 9;
    /* C31 (EXP1825 ExecuteIndirect): mode 10 is mode 1 drawn by
     * ExecuteIndirect through a root-constants + root-CBV + draw signature
     * (stride 40, two records, count 1; root parameters in increasing
     * order, EXP1826: the runtime refuses others with debug message 743);
     * the list binds a zero scale and green, so only the record's
     * constants and CBV draw the blue triangle */
    const int expanded = mode == 10;
    /* C32: mode 11 is mode 0 on a 4x multisampled target resolved into a
     * single-sampled texture: interior pixels exact, edge pixels blended */
    const int msaa = mode == 11;
    /* C44B: mode 12 is mode 0 with the same VS/PS compiled by DXC (SM 6.0);
     * C50A: mode 13 is mode 12 with the SM 6.1 SV_ViewID pixel shader */
    const int viewed = mode == 13;
    const int dxil = mode == 12 || viewed;
    int rooted = mode == 1 || redundant_root || bundled || expanded;
    /* C11 (D3D12-C11-queries.md): mode 3 draws the C6b triangle inside an
     * occlusion, a binary-occlusion and a pipeline-statistics query between
     * two timestamps and resolves all of them */
    const int queried = mode == 3;
    /* C13 (D3D12-C13-graphics-uavs.md): mode 4 counts the shaded pixels
     * with a PS root UAV u1 (InterlockedAdd) */
    /* C14 (D3D12-C14-execute-indirect.md): mode 5 is mode 4 drawn through
     * ExecuteIndirect -- two identical draw records, a count buffer of 1;
     * the counter shows whether the count limits the draws */
    const int indirect = mode == 5 || mode == 6 || stream_probe;
    const int indirect_no_count = mode == 6 || mode == 24;
    const int spaced = mode == 7;
    const int counted = mode == 4 || indirect || spaced;
    ID3D12CommandSignature *signature = NULL;
    ID3D12Resource *gpu_args = NULL;
    ID3D12GraphicsCommandList *bundle = NULL;
    ID3D12Resource *resolved = NULL;
    ID3D12CommandAllocator *bundle_allocator = NULL;
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
    static const char *stream_steps[] = {"RC43I16Z", "RC43I16O", "RC43I16N", "RC43I32Z", "RC43I32O", "RC43I32N"};
    const char *step = stream_gpu_args ? "RC59G" : stream_large ? (mode == 23 ? "RC57C" : mode == 24 ? "RC57U" : "RC57M") : stream_matrix ? stream_steps[mode - 17] : stream_probe ? (!stream_count ? "RC38Z" : indexed ? "RC38I" : "RC38N") : viewed ? "C50A" : dxil ? "C44B" : msaa ? "C32A" : expanded ? "C31A" : bundled ? "C30A" : redundant_root ? "ROOTG" : spaced ? "C17B_DRAW" : indexed ? "C6D" : rooted ? "C6C" : queried ? "C11" : indirect_no_count ? "C14NC" : indirect ? "C14" : counted ? "C13" : "C6B";
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
    int centre_ok = 0, corner_ok = 0, stream_counter_ok = 0, stream_statistics_ok = 0;
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
        if (stream_index32) {
            const UINT indices32[6] = {0, 1, 2, 2, 1, 3};
            memcpy(bytes + 512, indices32, sizeof(indices32));
        }
        memcpy(bytes + 256, scale, sizeof(scale)); /* the root CBV (256-byte aligned) */
        memset(bytes + 1024, 0, 16); /* C13: the counter's initial value */
        {
            /* C14: two D3D12_DRAW_ARGUMENTS records at 2048, the count at 2080 */
            const UINT records[8] = {3, 1, 0, 0, 3, 1, 0, 0}, one = 1;
            memcpy(bytes + 2048, records, sizeof(records));
            memcpy(bytes + 2080, &one, sizeof(one));
            if (stream_probe) {
                const UINT indexed_records[10] = {6, 1, 0, 0, 0, 6, 1, 0, 0, 0};
                const UINT count = stream_count;
                if (indexed) memcpy(bytes + 2048, indexed_records, sizeof(indexed_records));
                if (stream_large || stream_gpu_args)
                    for (UINT r = 0; r < stream_count; ++r)
                        memcpy(bytes + 2048 + r * 20u, indexed_records, 20u);
                /* Outside every indexed record, within the 64 KiB upload. */
                memcpy(bytes + stream_count_offset, &count, sizeof(count));
                printf("%s MaxCount=%u count=%u count-buffer=%u calls=%u index-bytes=%u ordinary-followup=1\n",
                       step, stream_max, count, !indirect_no_count, stream_calls,
                       indexed ? (stream_index32 ? 4u : 2u) : 0u);
            }
        }
        if (expanded) {
            const D3D12_GPU_VIRTUAL_ADDRESS cbv = ID3D12Resource_GetGPUVirtualAddress(vertices) + 256;
            const float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            const UINT draw[4] = {3, 1, 0, 0}, one = 1;
            for (UINT r = 0; r < 2; ++r) {
                memcpy(bytes + 3072 + 40 * r, blue, 16);
                memcpy(bytes + 3072 + 40 * r + 16, &cbv, 8);
                memcpy(bytes + 3072 + 40 * r + 24, draw, 16);
            }
            memcpy(bytes + 3200, &one, sizeof(one));
            memset(bytes + 768, 0, 16); /* the list's own CBV: a zero scale */
        }
        ID3D12Resource_Unmap(vertices, 0, NULL);
        if (msaa) {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q4 = {DXGI_FORMAT_R8G8B8A8_UNORM, 4}, q8 = {DXGI_FORMAT_R8G8B8A8_UNORM, 8};
            HRESULT qh = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q4, sizeof(q4));
            ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q8, sizeof(q8));
            printf("C32A quality levels 4x=%u 8x=%u hr=0x%08lx\n", q4.NumQualityLevels, q8.NumQualityLevels, (unsigned long)qh);
            D3D12_RESOURCE_DESC single = desc;
            single.Flags = D3D12_RESOURCE_FLAG_NONE;
            hr = q4.NumQualityLevels == 1 ? ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &single,
                                                                                 D3D12_RESOURCE_STATE_RESOLVE_DEST, NULL,
                                                                                 &IID_ID3D12Resource, (void **)&resolved)
                                          : E_FAIL;
            desc.SampleDesc.Count = 4;
        }
        if (SUCCEEDED(hr))
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
        pso_desc.VS.pShaderBytecode = rooted ? g_agx_d3d12_probe_vs_root : dxil ? g_agx_d3d12_probe_vs_dxil : g_agx_d3d12_probe_vs;
        pso_desc.VS.BytecodeLength = rooted ? sizeof(g_agx_d3d12_probe_vs_root) : dxil ? sizeof(g_agx_d3d12_probe_vs_dxil)
                                                                                       : sizeof(g_agx_d3d12_probe_vs);
        pso_desc.PS.pShaderBytecode = rooted ? g_agx_d3d12_probe_ps_root : spaced ? g_agx_d3d12_probe_ps_spaces : counted ? g_agx_d3d12_probe_ps_uav
                                    : viewed ? g_agx_d3d12_probe_ps_view_dxil : dxil ? g_agx_d3d12_probe_ps_dxil : g_agx_d3d12_probe_ps;
        pso_desc.PS.BytecodeLength = rooted ? sizeof(g_agx_d3d12_probe_ps_root)
                                   : spaced ? sizeof(g_agx_d3d12_probe_ps_spaces) : counted ? sizeof(g_agx_d3d12_probe_ps_uav)
                                   : viewed ? sizeof(g_agx_d3d12_probe_ps_view_dxil)
                                   : dxil ? sizeof(g_agx_d3d12_probe_ps_dxil) : sizeof(g_agx_d3d12_probe_ps);
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
        pso_desc.SampleDesc.Count = msaa ? 4 : 1;
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
            const FLOAT blue[4] = {0.0f, 0.0f, 1.0f, 1.0f}, green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
            ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list, 0, 4, expanded ? green : blue, 0);
            ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list, 1, ID3D12Resource_GetGPUVirtualAddress(vertices) +
                                                                                     (expanded ? 768 : 256));
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
        if (stream_large) {
            D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, 1, 0};
            hr = ID3D12Device_CreateQueryHeap(device, &qd, &IID_ID3D12QueryHeap, (void **)&statistics);
            results = SUCCEEDED(hr) ? committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST) : NULL;
            if (!results) hr = E_FAIL;
            if (SUCCEEDED(hr))
                ID3D12GraphicsCommandList_BeginQuery(list, statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
        }
        if (indexed) {
            D3D12_INDEX_BUFFER_VIEW ibv = {ID3D12Resource_GetGPUVirtualAddress(vertices) + 512,
                                         stream_index32 ? 24u : 12u,
                                         stream_index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT};
            ID3D12GraphicsCommandList_IASetIndexBuffer(list, &ibv);
            if (!indirect) ID3D12GraphicsCommandList_DrawIndexedInstanced(list, 6, 1, 0, 0, 0);
        }
        if (indirect) {
            D3D12_INDIRECT_ARGUMENT_DESC arg = {indexed ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED : D3D12_INDIRECT_ARGUMENT_TYPE_DRAW};
            D3D12_COMMAND_SIGNATURE_DESC sd = {indexed ? 20u : 16u, 1, &arg, 0};
            HRESULT sh = ID3D12Device_CreateCommandSignature(device, &sd, NULL, &IID_ID3D12CommandSignature, (void **)&signature);
            printf("%s CreateCommandSignature hr=0x%08lx\n", step, (unsigned long)sh);
            if (SUCCEEDED(sh) && SUCCEEDED(hr) && stream_gpu_args) {
                gpu_args = c21_buffer(device, D3D12_HEAP_TYPE_DEFAULT, stream_gpu_count_offset + 256u,
                                      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
                if (!gpu_args) {
                    hr = E_FAIL;
                } else {
                    ID3D12GraphicsCommandList_CopyBufferRegion(list, gpu_args, 0, vertices, 2048, stream_count * 20u);
                    ID3D12GraphicsCommandList_CopyBufferRegion(list, gpu_args, stream_gpu_count_offset, vertices,
                                                               stream_count_offset, 4);
                    transition(list, gpu_args, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
                }
                printf("%s DEFAULT arguments+count by GPU copy bytes=%u hr=0x%08lx\n", step,
                       stream_gpu_count_offset + 256u, (unsigned long)hr);
            }
            if (SUCCEEDED(sh) && SUCCEEDED(hr)) {
                for (UINT call = 0; call < stream_calls; ++call)
                    ID3D12GraphicsCommandList_ExecuteIndirect(list, signature, stream_large || stream_gpu_args ? stream_max
                                                               : indirect_no_count ? 1 : 2,
                                                               stream_gpu_args ? gpu_args : vertices, stream_gpu_args ? 0 : 2048,
                                                               indirect_no_count ? NULL : stream_gpu_args ? gpu_args : vertices,
                                                               indirect_no_count ? 0 : stream_gpu_args ? stream_gpu_count_offset
                                                               : stream_probe ? stream_count_offset : 2080);
                if (stream_large)
                    ID3D12GraphicsCommandList_EndQuery(list, statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
                if (stream_probe) {
                    /* No VS rebind: require the continuation to reach this draw. */
                    if (indexed) ID3D12GraphicsCommandList_DrawIndexedInstanced(list, 6, 1, 0, 0, 0);
                    else ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
                }
            } else if (stream_probe && FAILED(sh)) {
                hr = sh;
            }
        } else if (expanded) {
            D3D12_INDIRECT_ARGUMENT_DESC args[3] = {{D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT},
                                                    {D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW},
                                                    {D3D12_INDIRECT_ARGUMENT_TYPE_DRAW}};
            D3D12_COMMAND_SIGNATURE_DESC sd = {40, 3, args, 0};
            args[0].Constant.RootParameterIndex = 0;
            args[0].Constant.DestOffsetIn32BitValues = 0;
            args[0].Constant.Num32BitValuesToSet = 4;
            args[1].ConstantBufferView.RootParameterIndex = 1;
            HRESULT sh = ID3D12Device_CreateCommandSignature(device, &sd, root, &IID_ID3D12CommandSignature, (void **)&signature);
            printf("C31A CreateCommandSignature hr=0x%08lx\n", (unsigned long)sh);
            if (SUCCEEDED(sh)) ID3D12GraphicsCommandList_ExecuteIndirect(list, signature, 2, vertices, 3072, vertices, 3200);
        } else if (bundled) {
            HRESULT bh = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_BUNDLE,
                                                             &IID_ID3D12CommandAllocator, (void **)&bundle_allocator);
            if (SUCCEEDED(bh))
                bh = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_BUNDLE, bundle_allocator, pso,
                                                    &IID_ID3D12GraphicsCommandList, (void **)&bundle);
            if (SUCCEEDED(bh)) {
                ID3D12GraphicsCommandList_SetGraphicsRootSignature(bundle, root);
                ID3D12GraphicsCommandList_IASetPrimitiveTopology(bundle, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                ID3D12GraphicsCommandList_IASetVertexBuffers(bundle, 0, 1, &vbv);
                ID3D12GraphicsCommandList_DrawInstanced(bundle, 3, 1, 0, 0);
                bh = ID3D12GraphicsCommandList_Close(bundle);
            }
            printf("C30A bundle hr=0x%08lx\n", (unsigned long)bh);
            if (SUCCEEDED(bh)) ID3D12GraphicsCommandList_ExecuteBundle(list, bundle);
        } else if (!indexed) {
            ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
        }
        if (counted && counter && counter_back) {
            transition(list, counter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ID3D12GraphicsCommandList_CopyBufferRegion(list, counter_back, 0, counter, 0, 16);
        }
        if (stream_large && results && SUCCEEDED(hr))
            ID3D12GraphicsCommandList_ResolveQueryData(list, statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1, results, 0);
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
        if (msaa) {
            transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            ID3D12GraphicsCommandList_ResolveSubresource(list, resolved, 0, texture, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
            transition(list, resolved, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            tex_loc.pResource = resolved;
        } else {
            transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        }
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &buf_loc, 0, 0, 0, &tex_loc, NULL);
        if (SUCCEEDED(hr)) hr = run_list(queue, fence, list, base + 1,
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
    /* C32A: partially covered edge pixels blend the triangle and the clear
     * colour, about one row of the 3 x ~45 pixel edges */
    const int pass = SUCCEEDED(hr) && centre_ok && corner_ok && (msaa ? other >= 16 && other <= 256 : other == 0) &&
                     green >= (msaa ? low - 128u : low) && green <= high;
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
    if (stream_large) {
        const D3D12_QUERY_DATA_PIPELINE_STATISTICS *q = NULL;
        D3D12_RANGE all = {0, sizeof(*q)};
        if (SUCCEEDED(hr) && results && SUCCEEDED(ID3D12Resource_Map(results, 0, &all, (void **)&q)) && q) {
            const UINT64 draws = (UINT64)stream_count * stream_calls;
            stream_statistics_ok = q->IAVertices == 6u * draws && q->IAPrimitives == 2u * draws &&
                q->VSInvocations == 6u * draws && q->CInvocations == 2u * draws &&
                q->CPrimitives == 2u * draws && q->PSInvocations != 0 &&
                !q->GSInvocations && !q->GSPrimitives && !q->HSInvocations &&
                !q->DSInvocations && !q->CSInvocations;
            printf("%s statistics IA=%llu/%llu VS=%llu clip=%llu/%llu PS=%llu CS=%llu expected-draws=%llu\n",
                   step, q->IAVertices, q->IAPrimitives, q->VSInvocations, q->CInvocations,
                   q->CPrimitives, q->PSInvocations, q->CSInvocations, draws);
            ID3D12Resource_Unmap(results, 0, NULL);
        }
        printf("AGX_D3D12_%s_STATS: %s\n", step, stream_statistics_ok ? "PASS" : "FAIL");
    }
    if (counted) {
        const UINT *c = NULL;
        D3D12_RANGE all = {0, 16};
        int counted_ok = 0;
        if (SUCCEEDED(hr) && counter_back && SUCCEEDED(ID3D12Resource_Map(counter_back, 0, &all, (void **)&c)) && c) {
            printf("C13 counter=%u drawn=%u\n", c[0], green);
            counted_ok = c[0] == green * (stream_probe ? stream_count * stream_calls + 1u : 1u);
            stream_counter_ok = counted_ok;
            ID3D12Resource_Unmap(counter_back, 0, NULL);
        }
        if (stream_probe)
            printf("AGX_D3D12_%s_COUNT: %s\n", step, pass && counted_ok ? "PASS" : "FAIL");
        else
            printf("AGX_D3D12_%s: %s\n", spaced ? "C17B" : indirect_no_count ? "C14NCA" : indirect ? "C14A" : "C13A",
                   pass && counted_ok ? "PASS" : "FAIL");
    }
    if (signature) ID3D12CommandSignature_Release(signature);
    if (gpu_args) ID3D12Resource_Release(gpu_args);
    if (bundle) ID3D12GraphicsCommandList_Release(bundle);
    if (bundle_allocator) ID3D12CommandAllocator_Release(bundle_allocator);
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
    if (resolved) ID3D12Resource_Release(resolved);
    if (readback) ID3D12Resource_Release(readback);
    if (vertices) ID3D12Resource_Release(vertices);
    return !pass || (stream_probe && !stream_counter_ok) || (stream_large && !stream_statistics_ok);
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
static int draw_depth_impl(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int depth_only);
static int draw_depth(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_depth_impl(d, q, f, b, 0); }
static int step_c35a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_depth_impl(d, q, f, b, 1); }
static int draw_textured(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_textured_impl(d, q, f, b, 0); }
static int step_c17c(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_textured_impl(d, q, f, b, 1); }
static int step_c18a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_textured_impl(d, q, f, b, 2); }
static int step_c18b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_textured_impl(d, q, f, b, 3); }

/* C35 (EXP1829: Multithreading's shadow pass draws with PS = NULL and
 * crashed in Asahi's draw, which always reads the bound FS): depth_only
 * draws the near triangle with a second PSO that has no pixel shader and no
 * render target, then the far blue quad with the normal one; PASS (C35A) =
 * centre red (the clear; the quad fails the depth the triangle wrote),
 * corner blue. */
static int draw_depth_impl(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int depth_only) {
    const char *step = depth_only ? "C35A" : "C6F";
    ID3D12PipelineState *depth_pso = NULL;
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
        printf("%s CreateGraphicsPipelineState hr=0x%08lx\n", step, (unsigned long)hr);
        if (SUCCEEDED(hr) && depth_only) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC depth_desc_pso = pso_desc;
            depth_desc_pso.PS.pShaderBytecode = NULL;
            depth_desc_pso.PS.BytecodeLength = 0;
            depth_desc_pso.NumRenderTargets = 0;
            depth_desc_pso.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
            hr = ID3D12Device_CreateGraphicsPipelineState(device, &depth_desc_pso, &IID_ID3D12PipelineState, (void **)&depth_pso);
            printf("C35A depth-only CreateGraphicsPipelineState hr=0x%08lx\n", (unsigned long)hr);
        }
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
        if (depth_only) {
            ID3D12GraphicsCommandList_OMSetRenderTargets(list, 0, NULL, FALSE, &dsv);
            ID3D12GraphicsCommandList_SetPipelineState(list, depth_pso);
        }
        ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
        if (depth_only) {
            ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, &dsv);
            ID3D12GraphicsCommandList_SetPipelineState(list, pso);
        }
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
            ok = centre[0] == (depth_only ? 255 : 0) && centre[1] == (depth_only ? 0 : 255) && centre[2] == 0 &&
                 corner[0] == 0 && corner[1] == 0 && corner[2] == 255;
            printf("%s centre=%u,%u,%u corner=%u,%u,%u\n", step, centre[0], centre[1], centre[2], corner[0], corner[1], corner[2]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_%s: %s\n", step, SUCCEEDED(hr) && ok ? "PASS" : "FAIL");
    if (depth_pso) ID3D12PipelineState_Release(depth_pso);
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

/* C49A (typed UAV loads, TypedUAVLoadAdditionalFormats): phase 0 stores known
 * values into 4x4 RGBA16F, RGBA8_UNORM, R16_FLOAT and R8_UINT texture UAVs,
 * phase 1 (after a UAV barrier) loads them back (agx_d3d12_probe_cs_c49.hlsl);
 * every one of the 16 dwords must be 0xf. The runtime refuses the PSO when
 * the cap is not reported. */
static int c49_typed_uav_loads(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    static const DXGI_FORMAT formats[4] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM,
                                           DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R8_UINT};
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 5, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0, 0, 0};
    D3D12_ROOT_PARAMETER parameters[2] = {{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}, {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {2, parameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12DescriptorHeap *views = NULL;
    ID3D12Resource *textures[4] = {NULL, NULL, NULL, NULL};
    ID3D12Resource *outv = uav_buffer(device);
    ID3D12Resource *back = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start = {0};
    UINT *words = NULL, bad = 16, first = 0;
    HRESULT hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    printf("C49A TypedUAVLoadAdditionalFormats %d hr=0x%08lx\n", (int)options.TypedUAVLoadAdditionalFormats, (unsigned long)hr);
    for (unsigned i = 0; i < 4 && SUCCEEDED(hr); ++i) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {formats[i]};
        D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D};
        ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
        printf("C49A format %u support2 0x%08x typed-load %d\n", (unsigned)formats[i], (unsigned)support.Support2,
               (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0);
        desc.Width = 4;
        desc.Height = 4;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = formats[i];
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource,
                                                  (void **)&textures[i]);
    }
    if (SUCCEEDED(hr) && !(outv && back)) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&views);
    if (SUCCEEDED(hr)) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu;
        D3D12_UNORDERED_ACCESS_VIEW_DESC out_desc = {DXGI_FORMAT_R32_UINT, D3D12_UAV_DIMENSION_BUFFER};
        const UINT step = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        out_desc.Buffer.NumElements = 16;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(views, &cpu);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(views, &gpu_start);
        ID3D12Device_CreateUnorderedAccessView(device, outv, NULL, &out_desc, cpu);
        for (unsigned i = 0; i < 4; ++i) {
            cpu.ptr += step;
            ID3D12Device_CreateUnorderedAccessView(device, textures[i], NULL, NULL, cpu);
        }
        parameters[0].DescriptorTable.NumDescriptorRanges = 1;
        parameters[0].DescriptorTable.pDescriptorRanges = &range;
        parameters[1].Constants.ShaderRegister = 0;
        parameters[1].Constants.Num32BitValues = 1;
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_c49;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_c49);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C49A CreateComputePipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        D3D12_RESOURCE_BARRIER uav = {D3D12_RESOURCE_BARRIER_TYPE_UAV};
        UINT phase = 0;
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &views);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpu_start);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list, 1, 1, &phase, 0);
        ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &uav);
        phase = 1;
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list, 1, 1, &phase, 0);
        ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
        transition(list, outv, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, back, 0, outv, 0, 64);
        hr = run_list(queue, fence, list, base + 1, "C49A dispatch");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 64};
        hr = ID3D12Resource_Map(back, 0, &all, (void **)&words);
        if (SUCCEEDED(hr) && words) {
            bad = 0;
            first = words[0];
            for (unsigned i = 0; i < 16; ++i) bad += words[i] != 0xfu;
            printf("C49A readback bad=%u first=0x%x last=0x%x\n", bad, first, words[15]);
            ID3D12Resource_Unmap(back, 0, NULL);
        }
    }
    printf("AGX_D3D12_C49A: %s\n", SUCCEEDED(hr) && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (views) ID3D12DescriptorHeap_Release(views);
    for (unsigned i = 0; i < 4; ++i)
        if (textures[i]) ID3D12Resource_Release(textures[i]);
    if (back) ID3D12Resource_Release(back);
    if (outv) ID3D12Resource_Release(outv);
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
/* C40 (D3D12-C40-10bit-swapchains.md): with format R10G10B10A2 the back
 * buffer is also read back after the clear: green is 0xC00FFC00 */
static int swapchain_present_format(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base,
                                    DXGI_FORMAT format, const char *step) {
    WNDCLASSW wc = {0};
    HWND window = NULL;
    IDXGIFactory2 *factory = NULL;
    IDXGISwapChain1 *swapchain = NULL;
    ID3D12Resource *buffer = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12Resource *readback = NULL;
    UINT texel = 0;
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
    printf("%s window %p factory hr=0x%08lx\n", step, (void *)window, (unsigned long)hr);
    desc.Width = 64;
    desc.Height = 64;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (SUCCEEDED(hr)) {
        hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)queue, window, &desc, NULL, NULL, &swapchain);
        printf("%s CreateSwapChainForHwnd hr=0x%08lx\n", step, (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) {
        hr = IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_ID3D12Resource, (void **)&buffer);
        printf("%s GetBuffer hr=0x%08lx\n", step, (unsigned long)hr);
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
        if (format != DXGI_FORMAT_R8G8B8A8_UNORM &&
            (readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST))) {
            D3D12_TEXTURE_COPY_LOCATION tex_loc = {0}, buf_loc = {0};
            tex_loc.pResource = buffer;
            tex_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            buf_loc.pResource = readback;
            buf_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            buf_loc.PlacedFootprint.Footprint.Format = format;
            buf_loc.PlacedFootprint.Footprint.Width = 64;
            buf_loc.PlacedFootprint.Footprint.Height = 64;
            buf_loc.PlacedFootprint.Footprint.Depth = 1;
            buf_loc.PlacedFootprint.Footprint.RowPitch = 256;
            transition(list, buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ID3D12GraphicsCommandList_CopyTextureRegion(list, &buf_loc, 0, 0, 0, &tex_loc, NULL);
            transition(list, buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        transition(list, buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        hr = run_list(queue, fence, list, base + 1, "C7A clear");
    }
    if (SUCCEEDED(hr) && readback) {
        const unsigned char *bytes = NULL;
        D3D12_RANGE all = {0, 16384};
        if (SUCCEEDED(ID3D12Resource_Map(readback, 0, &all, (void **)&bytes)) && bytes) {
            memcpy(&texel, bytes + 32 * 256 + 32 * 4, sizeof(texel));
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
        printf("%s back buffer texel 0x%08x\n", step, texel);
        if (texel != 0xC00FFC00u) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) {
        hr = IDXGISwapChain1_Present(swapchain, 0, 0);
        printf("%s Present hr=0x%08lx\n", step, (unsigned long)hr);
        if (SUCCEEDED(hr)) {
            hr = ID3D12CommandQueue_Signal(queue, fence, base + 2);
            UINT64 value = 0;
            for (int i = 0; i < 300 && (value = ID3D12Fence_GetCompletedValue(fence)) < base + 2; ++i) Sleep(10);
            printf("%s signal after present hr=0x%08lx completed %llu\n", step, (unsigned long)hr, (unsigned long long)value);
            if (value < base + 2) hr = E_FAIL;
        }
    }
    printf("AGX_D3D12_%s: %s\n", step, SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (readback) ID3D12Resource_Release(readback);
    if (buffer) ID3D12Resource_Release(buffer);
    if (swapchain) IDXGISwapChain1_Release(swapchain);
    if (factory) IDXGIFactory2_Release(factory);
    if (window) DestroyWindow(window);
    return FAILED(hr);
}

static int swapchain_present(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    return swapchain_present_format(device, queue, fence, base, DXGI_FORMAT_R8G8B8A8_UNORM, "C7A");
}
static int c40_swapchain_10bit(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    return swapchain_present_format(device, queue, fence, base, DXGI_FORMAT_R10G10B10A2_UNORM, "C40A");
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

/* C41 (EXP1839 MiniEngine): C41A runs a loop with continue/continuec (the
 * translator once emitted NIR continues without a continue construct);
 * C41B reads GetDimensions of a 37x19 RWTexture2D and a 5x7x3
 * RWTexture2DArray (resinfo on u#: view sizes, uint and float). Root UAV u0
 * receives the results; u1/u2 come from a descriptor table. */
static ID3D12Resource *c41_uav_texture(ID3D12Device *device, UINT width, UINT height, UINT16 layers,
                                       DXGI_FORMAT format) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    ID3D12Resource *texture = NULL;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = layers;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HRESULT hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL,
                                                      &IID_ID3D12Resource, (void **)&texture);
    printf("C41 texture %ux%ux%u format %u hr=0x%08lx\n", width, height, (unsigned)layers, (unsigned)format,
        (unsigned long)hr);
    return SUCCEEDED(hr) ? texture : NULL;
}
static int c41_compute(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base,
                       int uavinfo) {
    const char *step = uavinfo ? "C41B" : "C41A";
    const UINT count = uavinfo ? 8u : 64u;
    UINT expected[64] = {0};
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 1, 0, 0};
    D3D12_ROOT_PARAMETER parameters[2] = {{D3D12_ROOT_PARAMETER_TYPE_UAV}, {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {uavinfo ? 2u : 1u, parameters};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12DescriptorHeap *heap = NULL;
    ID3D12Resource *output = uav_buffer(device), *readback = NULL, *plain = NULL, *array = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = {0};
    UINT *words = NULL;
    unsigned bad = count;
    float fw = 37.0f, fh = 19.0f;
    if (uavinfo) {
        expected[0] = 37; expected[1] = 19; expected[2] = 5; expected[3] = 7; expected[4] = 3;
        memcpy(&expected[5], &fw, 4); memcpy(&expected[6], &fh, 4);
    } else {
        for (UINT x = 0; x < 64; ++x)
            for (UINT i = 0; i < 40; ++i)
                if (i % 3 != x % 3) expected[x] += (i & 7) == 5 ? 1000u : i;
    }
    readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    if (uavinfo) {
        plain = c41_uav_texture(device, 37, 19, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
        array = c41_uav_texture(device, 5, 7, 3, DXGI_FORMAT_R32_UINT);
    }
    HRESULT hr = output && readback && (!uavinfo || (plain && array)) ? S_OK : E_FAIL;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    if (SUCCEEDED(hr) && uavinfo) {
        hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
        if (SUCCEEDED(hr)) {
            D3D12_CPU_DESCRIPTOR_HANDLE cpu = {0};
            D3D12_UNORDERED_ACCESS_VIEW_DESC view = {DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_UAV_DIMENSION_TEXTURE2D};
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &cpu);
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &gpu);
            ID3D12Device_CreateUnorderedAccessView(device, plain, NULL, &view, cpu);
            view.Format = DXGI_FORMAT_R32_UINT;
            view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.ArraySize = 3;
            cpu.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            ID3D12Device_CreateUnorderedAccessView(device, array, NULL, &view, cpu);
        }
    }
    if (SUCCEEDED(hr)) hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = uavinfo ? (const void *)g_agx_d3d12_probe_cs_c41b : (const void *)g_agx_d3d12_probe_cs_c41a;
        pso_desc.CS.BytecodeLength = uavinfo ? sizeof(g_agx_d3d12_probe_cs_c41b) : sizeof(g_agx_d3d12_probe_cs_c41a);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
    }
    printf("%s resources/root/PSO hr=0x%08lx\n", step, (unsigned long)hr);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
        &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_UAV};
        barrier.UAV.pResource = output;
        if (uavinfo) ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 0, ID3D12Resource_GetGPUVirtualAddress(output));
        if (uavinfo) ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 1, gpu);
        ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
        transition(list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, output, 0, 65536);
        hr = run_list(queue, fence, list, base + 1, step);
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 65536};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&words);
        if (SUCCEEDED(hr) && words) {
            bad = 0;
            for (UINT i = 0; i < count; ++i) bad += words[i] != expected[i];
            printf("%s readback bad=%u actual=[%u,%u,%u,%u,%u,0x%08x,0x%08x,%u] expected=[%u,%u,%u,%u,%u,0x%08x,0x%08x,%u]\n",
                step, bad, words[0], words[1], words[2], words[3], words[4], words[5], words[6], words[7],
                expected[0], expected[1], expected[2], expected[3], expected[4], expected[5], expected[6], expected[7]);
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
    if (array) ID3D12Resource_Release(array);
    if (plain) ID3D12Resource_Release(plain);
    if (readback) ID3D12Resource_Release(readback);
    if (output) ID3D12Resource_Release(output);
    return !(SUCCEEDED(hr) && bad == 0);
}
static int step_c41a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c41_compute(d, q, f, b, 0); }
static int step_c41b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c41_compute(d, q, f, b, 1); }

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

static HRESULT c21_graphics_pipeline_targets(ID3D12Device *device, ID3D12RootSignature *root, const void *ps, SIZE_T ps_bytes,
                                             const DXGI_FORMAT *formats, UINT targets, ID3D12PipelineState **pso) {
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
    desc.NumRenderTargets = targets;
    for (UINT t = 0; t < targets; ++t) {
        desc.RTVFormats[t] = formats[t];
        desc.BlendState.RenderTarget[t] = desc.BlendState.RenderTarget[0];
    }
    desc.SampleDesc.Count = 1;
    return ID3D12Device_CreateGraphicsPipelineState(device, &desc, &IID_ID3D12PipelineState, (void **)pso);
}
static HRESULT c21_graphics_pipeline_format(ID3D12Device *device, ID3D12RootSignature *root, const void *ps, SIZE_T ps_bytes,
                                            DXGI_FORMAT format, ID3D12PipelineState **pso) {
    return c21_graphics_pipeline_targets(device, root, ps, ps_bytes, &format, 1, pso);
}
static HRESULT c21_graphics_pipeline(ID3D12Device *device, ID3D12RootSignature *root, const void *ps, SIZE_T ps_bytes, ID3D12PipelineState **pso) {
    return c21_graphics_pipeline_format(device, root, ps, ps_bytes, DXGI_FORMAT_R8G8B8A8_UNORM, pso);
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
/* C42 (EXP1841 MiniEngine: the scene rendered yellow): R11G11B10_FLOAT, the
 * engine's HDR colour format, stage by stage. C42A clears a 16x16 render
 * target to (0.25, 0.5, 0.75) and reads the texel bits back (copy); C42B
 * draws (0.5, 0.25, 1.0) through the PS; C42C loads that texel in a compute
 * shader through an SRV; C42D runs MiniEngine's Pack/Unpack_R11G11B10_FLOAT
 * ALU on root constants. Every value is exact in the format. */
static int c42_r11g11b10(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int mode) {
    static const char *steps[10] = {"C42A", "C42B", "C42C", "C42D", "C42G", "C42H", "C42J", "C42K", "C42M", "C42N"};
    /* C42M/C42N: the C42J gathers from RGBA8 and RGBA16F targets (scope of the
     * EXP1847 R11G11B10 GatherBlue zero); only the blue lanes compare */
    const DXGI_FORMAT target_format = mode == 8 ? DXGI_FORMAT_R8G8B8A8_UNORM : mode == 9 ? DXGI_FORMAT_R16G16B16A16_FLOAT
                                                                         : DXGI_FORMAT_R11G11B10_FLOAT;
    const int gather = mode == 6 || mode >= 8;
    /* C42G/C42H: the draw of C42B through a sample-frequency PS ("linear sample"
     * input, float3 output), C42H with a second R16G16B16A16_FLOAT target */
    const int draws = mode == 1 || mode == 2 || mode >= 4, copies = mode < 2 || mode == 4 || mode == 5,
              computes = mode == 2 || mode == 3 || mode >= 6; /* C42J: C42C through GatherRed/Green/Blue; C42K: SampleLevel */
    const int table = mode == 2 || mode >= 6;
    const char *step = steps[mode];
    const UINT clear_bits = 0x340u | (0x380u << 11) | (0x1d0u << 22), draw_bits = 0x380u | (0x340u << 11) | (0x1e0u << 22);
    const float half_f = 0.5f, quarter_f = 0.25f, one_f = 1.0f, three_q = 0.75f;
    UINT expected[8] = {0};
    D3D12_HEAP_PROPERTIES heap_props = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC tex_desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {0};
    ID3D12Resource *texture = NULL, *vertices = NULL, *output = NULL, *normal = NULL;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp1 = {0};
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {{0}, {0}};
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12DescriptorHeap *rtv_heap = NULL, *srv_heap = NULL;
    ID3D12RootSignature *groot = NULL, *croot = NULL;
    ID3D12PipelineState *gpso = NULL, *cpso = NULL;
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0};
    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu = {0};
    UINT *words = NULL;
    unsigned bad = 4, count = mode == 7 ? 6u : gather ? 8u : mode == 3 ? 4u : mode == 2 ? 3u : 1u;
    const UINT16 normal_expected[3] = {0x3c00, 0x3400, 0x3800}; /* (1.0, 0.25, 0.5) */
    HRESULT hr = readback ? S_OK : E_FAIL;
    if (mode == 0) expected[0] = clear_bits;
    else if (mode == 7) {
        /* point and bilinear samples of the uniform (0.5, 0.25, 1.0) target */
        const float g[6] = {0.5f, 0.25f, 1.0f, 0.5f, 0.25f, 1.0f};
        memcpy(expected, g, sizeof(g));
    }
    else if (gather) {
        const float g[8] = {0.5f, 0.25f, 1.0f, 0.5f, 0.5f, 0.25f, 1.0f, 1.0f};
        memcpy(expected, g, sizeof(g));
    }
    else if (mode == 1 || mode >= 4) expected[0] = draw_bits;
    else if (mode == 2) { memcpy(&expected[0], &half_f, 4); memcpy(&expected[1], &quarter_f, 4); memcpy(&expected[2], &one_f, 4); }
    else { expected[0] = clear_bits; memcpy(&expected[1], &quarter_f, 4); memcpy(&expected[2], &half_f, 4); memcpy(&expected[3], &three_q, 4); }
    if (SUCCEEDED(hr) && mode != 3) {
        D3D12_CLEAR_VALUE clear = {target_format, {0.25f, 0.5f, 0.75f, 1.0f}};
        tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tex_desc.Width = 16; tex_desc.Height = 16; tex_desc.DepthOrArraySize = 1; tex_desc.MipLevels = 1;
        tex_desc.Format = target_format; tex_desc.SampleDesc.Count = 1;
        tex_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        hr = ID3D12Device_CreateCommittedResource(device, &heap_props, D3D12_HEAP_FLAG_NONE, &tex_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, &IID_ID3D12Resource, (void **)&texture);
        if (SUCCEEDED(hr)) ID3D12Device_GetCopyableFootprints(device, &tex_desc, 0, 1, 0, &fp, NULL, NULL, NULL);
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
        if (SUCCEEDED(hr)) {
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
            ID3D12Device_CreateRenderTargetView(device, texture, NULL, rtv);
            rtvs[0] = rtv;
        }
        if (SUCCEEDED(hr) && mode == 5) {
            D3D12_CLEAR_VALUE nclear = {DXGI_FORMAT_R16G16B16A16_FLOAT, {0, 0, 0, 0}};
            D3D12_RESOURCE_DESC ndesc = tex_desc;
            ndesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            hr = ID3D12Device_CreateCommittedResource(device, &heap_props, D3D12_HEAP_FLAG_NONE, &ndesc,
                D3D12_RESOURCE_STATE_RENDER_TARGET, &nclear, &IID_ID3D12Resource, (void **)&normal);
            if (SUCCEEDED(hr)) {
                ID3D12Device_GetCopyableFootprints(device, &ndesc, 0, 1, 32768, &fp1, NULL, NULL, NULL);
                rtvs[1].ptr = rtv.ptr + ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                ID3D12Device_CreateRenderTargetView(device, normal, NULL, rtvs[1]);
            }
        }
    }
    if (SUCCEEDED(hr) && draws) {
        const float data[3][6] = {{-1, -1, 0.5f, 0.25f, 1, 1}, {-1, 3, 0.5f, 0.25f, 1, 1}, {3, -1, 0.5f, 0.25f, 1, 1}};
        D3D12_ROOT_SIGNATURE_DESC root_desc = {0};
        void *mapped = NULL;
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        vertices = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        hr = vertices ? ID3D12Resource_Map(vertices, 0, NULL, &mapped) : E_FAIL;
        if (SUCCEEDED(hr) && !mapped) hr = E_FAIL;
        if (SUCCEEDED(hr)) { memcpy(mapped, data, sizeof(data)); ID3D12Resource_Unmap(vertices, 0, NULL); }
        if (SUCCEEDED(hr)) hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
            ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&groot);
        if (blob) { ID3D10Blob_Release(blob); blob = NULL; }
        if (SUCCEEDED(hr)) {
            const DXGI_FORMAT formats[2] = {target_format, DXGI_FORMAT_R16G16B16A16_FLOAT};
            const void *ps = mode == 4 ? (const void *)g_agx_d3d12_probe_ps_c42g : mode == 5 ? (const void *)g_agx_d3d12_probe_ps_c42h
                                                                                         : (const void *)g_agx_d3d12_probe_ps;
            const SIZE_T ps_bytes = mode == 4 ? sizeof(g_agx_d3d12_probe_ps_c42g) : mode == 5 ? sizeof(g_agx_d3d12_probe_ps_c42h)
                                                                                         : sizeof(g_agx_d3d12_probe_ps);
            hr = c21_graphics_pipeline_targets(device, groot, ps, ps_bytes, formats, mode == 5 ? 2u : 1u, &gpso);
        }
    }
    if (SUCCEEDED(hr) && computes) {
        D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER parameters[2] = {{D3D12_ROOT_PARAMETER_TYPE_UAV}, {table ? D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE : D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS}};
        D3D12_STATIC_SAMPLER_DESC point = {D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0, 1, D3D12_COMPARISON_FUNC_NEVER,
            D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, 0, D3D12_FLOAT32_MAX, 0, 0, D3D12_SHADER_VISIBILITY_ALL};
        D3D12_STATIC_SAMPLER_DESC samplers[2];
        samplers[0] = point;
        samplers[1] = point;
        samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[1].ShaderRegister = 1;
        D3D12_ROOT_SIGNATURE_DESC root_desc = {2, parameters, mode == 7 ? 2u : gather ? 1u : 0u, samplers};
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
        if (table) { parameters[1].DescriptorTable.NumDescriptorRanges = 1; parameters[1].DescriptorTable.pDescriptorRanges = &range; }
        else { parameters[1].Constants.ShaderRegister = 0; parameters[1].Constants.Num32BitValues = 3; }
        output = uav_buffer(device);
        if (!output) hr = E_FAIL;
        if (SUCCEEDED(hr) && table) {
            hr = ID3D12Device_CreateDescriptorHeap(device, &srv_desc, &IID_ID3D12DescriptorHeap, (void **)&srv_heap);
            if (SUCCEEDED(hr)) {
                D3D12_CPU_DESCRIPTOR_HANDLE cpu = {0};
                ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap, &cpu);
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap, &srv_gpu);
                ID3D12Device_CreateShaderResourceView(device, texture, NULL, cpu);
            }
        }
        if (SUCCEEDED(hr)) hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
            ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&croot);
        if (SUCCEEDED(hr)) {
            pso_desc.pRootSignature = croot;
            pso_desc.CS.pShaderBytecode = mode == 2 ? (const void *)g_agx_d3d12_probe_cs_c42c : gather ? (const void *)g_agx_d3d12_probe_cs_c42j
                                                                                    : mode == 7 ? (const void *)g_agx_d3d12_probe_cs_c42k
                                                                                    : (const void *)g_agx_d3d12_probe_cs_c42d;
            pso_desc.CS.BytecodeLength = mode == 2 ? sizeof(g_agx_d3d12_probe_cs_c42c) : gather ? sizeof(g_agx_d3d12_probe_cs_c42j)
                                                                                    : mode == 7 ? sizeof(g_agx_d3d12_probe_cs_c42k)
                                                                                    : sizeof(g_agx_d3d12_probe_cs_c42d);
            hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&cpso);
        }
    }
    printf("%s resources/root/PSO hr=0x%08lx rowpitch=%u\n", step, (unsigned long)hr, (unsigned)fp.Footprint.RowPitch);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
        &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        if (mode != 3) {
            const FLOAT color[4] = {0.25f, 0.5f, 0.75f, 1.0f}, black[4] = {0, 0, 0, 0};
            ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, mode == 0 ? color : black, 0, NULL);
            if (normal) ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtvs[1], black, 0, NULL);
        }
        if (draws) {
            D3D12_VIEWPORT viewport = {0, 0, 16, 16, 0, 1};
            D3D12_RECT scissor = {0, 0, 16, 16};
            D3D12_VERTEX_BUFFER_VIEW vb = {ID3D12Resource_GetGPUVirtualAddress(vertices), sizeof(float) * 18, sizeof(float) * 6};
            ID3D12GraphicsCommandList_SetPipelineState(list, gpso);
            ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, groot);
            ID3D12GraphicsCommandList_OMSetRenderTargets(list, normal ? 2u : 1u, rtvs, FALSE, NULL);
            ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
            ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
            ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vb);
            ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
        }
        if (copies) {
            D3D12_TEXTURE_COPY_LOCATION dst = {readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
            D3D12_TEXTURE_COPY_LOCATION src = {texture, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
            dst.PlacedFootprint = fp;
            src.SubresourceIndex = 0;
            transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL);
            if (normal) {
                dst.PlacedFootprint = fp1;
                src.pResource = normal;
                transition(list, normal, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
                ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL);
            }
        } else {
            D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_UAV};
            const float constants[3] = {0.25f, 0.5f, 0.75f};
            barrier.UAV.pResource = output;
            if (table) transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            ID3D12GraphicsCommandList_SetPipelineState(list, cpso);
            if (table) ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &srv_heap);
            ID3D12GraphicsCommandList_SetComputeRootSignature(list, croot);
            ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 0, ID3D12Resource_GetGPUVirtualAddress(output));
            if (table) ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 1, srv_gpu);
            else ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list, 1, 3, constants, 0);
            ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
            ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
            transition(list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, output, 0, 65536);
        }
        hr = run_list(queue, fence, list, base + 1, step);
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 65536};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&words);
        if (SUCCEEDED(hr) && words) {
            const UINT *texel = copies ? (const UINT *)((const BYTE *)words + fp.Offset + 8u * fp.Footprint.RowPitch + 8u * 4u) : words;
            bad = 0;
            for (UINT i = 0; i < count; ++i)
                if (mode < 8 || i == 2 || i == 6) bad += texel[i] != expected[i];
            if (normal) {
                const UINT16 *n = (const UINT16 *)((const BYTE *)words + fp1.Offset + 8u * fp1.Footprint.RowPitch + 8u * 8u);
                for (UINT i = 0; i < 3; ++i) bad += n[i] != normal_expected[i];
                printf("%s normal target (8,8) = [0x%04x,0x%04x,0x%04x] expected [0x3c00,0x3400,0x3800]\n", step, n[0], n[1], n[2]);
            }
            printf("%s readback bad=%u actual=[0x%08x,0x%08x,0x%08x,0x%08x] expected=[0x%08x,0x%08x,0x%08x,0x%08x]\n", step, bad,
                texel[0], count > 1 ? texel[1] : 0u, count > 2 ? texel[2] : 0u, count > 3 ? texel[3] : 0u,
                expected[0], expected[1], expected[2], expected[3]);
            if (count > 4)
                printf("%s readback words 4-7 actual=[0x%08x,0x%08x,0x%08x,0x%08x] expected=[0x%08x,0x%08x,0x%08x,0x%08x]\n", step,
                    texel[4], texel[5], texel[6], texel[7], expected[4], expected[5], expected[6], expected[7]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_%s: %s hr=0x%08lx\n", step, SUCCEEDED(hr) && bad == 0 ? "PASS" : "FAIL", (unsigned long)hr);
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (gpso) ID3D12PipelineState_Release(gpso);
    if (cpso) ID3D12PipelineState_Release(cpso);
    if (groot) ID3D12RootSignature_Release(groot);
    if (croot) ID3D12RootSignature_Release(croot);
    if (blob) ID3D10Blob_Release(blob);
    if (errors) ID3D10Blob_Release(errors);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (srv_heap) ID3D12DescriptorHeap_Release(srv_heap);
    if (vertices) ID3D12Resource_Release(vertices);
    if (output) ID3D12Resource_Release(output);
    if (texture) ID3D12Resource_Release(texture);
    if (normal) ID3D12Resource_Release(normal);
    if (readback) ID3D12Resource_Release(readback);
    return !(SUCCEEDED(hr) && bad == 0);
}
static int step_c42a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c42_r11g11b10(d, q, f, b, 0); }
static int step_c42b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c42_r11g11b10(d, q, f, b, 1); }
static int step_c42c(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c42_r11g11b10(d, q, f, b, 2); }
static int step_c42d(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c42_r11g11b10(d, q, f, b, 3); }
static int step_c42g(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c42_r11g11b10(d, q, f, b, 4); }
static int step_c42h(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c42_r11g11b10(d, q, f, b, 5); }
static int step_c42j(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c42_r11g11b10(d, q, f, b, 6); }
static int step_c42k(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c42_r11g11b10(d, q, f, b, 7); }
static int step_c42m(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c42_r11g11b10(d, q, f, b, 8); }
static int step_c42n(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c42_r11g11b10(d, q, f, b, 9); }
/* C42E: MiniEngine's CopyBackPostBufferCS -- an R32_UINT texture of packed
 * R11G11B10 values (0x781a0380 = (0.5, 0.25, 1.0)) unpacked into an
 * R11G11B10_FLOAT typed UAV with a float3 store, read back as texel bits. */
static int c42_copyback(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int render_target) {
    /* C42L: the scene colour buffer of MiniEngine is a render target and a UAV:
     * cleared through an RTV (black) before the typed UAV copy-back */
    const char *step = render_target ? "C42L" : "C42E";
    const UINT packed = 0x380u | (0x340u << 11) | (0x1e0u << 22);
    D3D12_HEAP_PROPERTIES heap_props = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {0};
    D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, 1}};
    D3D12_ROOT_PARAMETER parameter = {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3D12Resource *post = NULL, *scene = NULL;
    ID3D12Resource *upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12DescriptorHeap *heap = NULL, *rtv_heap = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0};
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = {0};
    UINT *words = NULL;
    unsigned bad = 256;
    HRESULT hr = upload && readback ? S_OK : E_FAIL;
    parameter.DescriptorTable.NumDescriptorRanges = 2;
    parameter.DescriptorTable.pDescriptorRanges = ranges;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 16; desc.Height = 16; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_UINT; desc.SampleDesc.Count = 1;
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommittedResource(device, &heap_props, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&post);
    if (SUCCEEDED(hr)) ID3D12Device_GetCopyableFootprints(device, &desc, 0, 1, 0, &fp, NULL, NULL, NULL);
    if (SUCCEEDED(hr)) {
        D3D12_CLEAR_VALUE clear = {DXGI_FORMAT_R11G11B10_FLOAT, {0, 0, 0, 0}};
        desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                     (render_target ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE);
        hr = ID3D12Device_CreateCommittedResource(device, &heap_props, D3D12_HEAP_FLAG_NONE, &desc,
            render_target ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            render_target ? &clear : NULL, &IID_ID3D12Resource, (void **)&scene);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&words);
    if (SUCCEEDED(hr) && words) {
        for (UINT y = 0; y < 16; ++y)
            for (UINT x = 0; x < 16; ++x) ((UINT *)((BYTE *)words + fp.Offset + y * fp.Footprint.RowPitch))[x] = packed;
        ID3D12Resource_Unmap(upload, 0, NULL);
    } else if (SUCCEEDED(hr)) hr = E_FAIL;
    if (SUCCEEDED(hr) && render_target) {
        D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
        hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
        if (SUCCEEDED(hr)) {
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
            ID3D12Device_CreateRenderTargetView(device, scene, NULL, rtv);
        }
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    if (SUCCEEDED(hr)) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = {0};
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &cpu);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &gpu);
        ID3D12Device_CreateShaderResourceView(device, post, NULL, cpu);
        cpu.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12Device_CreateUnorderedAccessView(device, scene, NULL, NULL, cpu);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_c42e;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_c42e);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
    }
    printf("%s resources/root/PSO hr=0x%08lx rowpitch=%u\n", step, (unsigned long)hr, (unsigned)fp.Footprint.RowPitch);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
        &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        D3D12_TEXTURE_COPY_LOCATION buffer_loc = {upload, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        D3D12_TEXTURE_COPY_LOCATION tex_loc = {post, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        buffer_loc.PlacedFootprint = fp;
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &tex_loc, 0, 0, 0, &buffer_loc, NULL);
        transition(list, post, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (render_target) {
            const FLOAT black[4] = {0, 0, 0, 0};
            ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, black, 0, NULL);
            transition(list, scene, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpu);
        ID3D12GraphicsCommandList_Dispatch(list, 2, 2, 1);
        transition(list, scene, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        buffer_loc.pResource = readback;
        buffer_loc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        tex_loc.pResource = scene;
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &buffer_loc, 0, 0, 0, &tex_loc, NULL);
        hr = run_list(queue, fence, list, base + 1, step);
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 65536};
        words = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&words);
        if (SUCCEEDED(hr) && words) {
            const UINT *texel = (const UINT *)((const BYTE *)words + fp.Offset + 8u * fp.Footprint.RowPitch) + 8;
            bad = 0;
            for (UINT y = 0; y < 16; ++y)
                for (UINT x = 0; x < 16; ++x) bad += ((const UINT *)((const BYTE *)words + fp.Offset + y * fp.Footprint.RowPitch))[x] != packed;
            printf("%s readback bad=%u texel(8,8)=0x%08x expected=0x%08x\n", step, bad, *texel, packed);
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
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (scene) ID3D12Resource_Release(scene);
    if (post) ID3D12Resource_Release(post);
    if (readback) ID3D12Resource_Release(readback);
    if (upload) ID3D12Resource_Release(upload);
    return !(SUCCEEDED(hr) && bad == 0);
}
static int step_c42e(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c42_copyback(d, q, f, b, 0); }
static int step_c42l(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c42_copyback(d, q, f, b, 1); }
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

/* C37 (D3D12-C37-tessellation.md): one triangle patch (3 control points)
 * through a hull shader whose factors come from a root CBV b0 visible to the
 * hull stage only (factor 0 would cull the patch) and a domain shader whose
 * colour comes from a root CBV b1 visible to the domain stage only: the
 * triangle must cover the centre in blue and leave the corners red (C37A).
 * C37B then draws a small green triangle in the top-left corner through a
 * pipeline without tessellation (TRIANGLELIST): a hull or domain shader left
 * bound from the previous draw would recolour or drop it. C37C draws the
 * patch with constant factors and control point 0's vertex colour (green):
 * the same tessellation without root CBVs. C37D and C37E draw C37C's
 * patch -- clockwise on screen, front-facing by default -- with back-face
 * culling: C37D's triangle_cw output keeps the patch (centre green), C37E's
 * triangle_ccw output faces away and culls (centre red). */
static int c37_tessellation(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int mode) {
    const int then_plain = mode == 1, no_cbv = mode >= 2, culled = mode == 4;
    const char *step = mode == 4 ? "C37E" : mode == 3 ? "C37D" : mode == 2 ? "C37C" : then_plain ? "C37B" : "C37A";
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_INPUT_ELEMENT_DESC elements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_ROOT_PARAMETER parameters[2] = {{D3D12_ROOT_PARAMETER_TYPE_CBV}, {D3D12_ROOT_PARAMETER_TYPE_CBV}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {2, parameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
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
    int centre_ok = 0, corners_ok = 0, plain_ok = 0;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_HULL;
    parameters[1].Descriptor.ShaderRegister = 1;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_DOMAIN;
    HRESULT hr = vertices && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(vertices, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        /* the patch (vertex colour green: the domain shader replaces it),
         * then the plain draw's corner triangle (green) */
        const float triangles[36] = {-0.8f, -0.8f, 0, 1, 0, 1, 0.0f, 0.8f, 0, 1, 0, 1, 0.8f, -0.8f, 0, 1, 0, 1,
                                     -1.0f, 1.0f, 0, 1, 0, 1, -0.5f, 1.0f, 0, 1, 0, 1, -1.0f, 0.5f, 0, 1, 0, 1};
        const float factors[4] = {4.0f, 4.0f, 0, 0}, tint[4] = {0, 0, 1, 1};
        memcpy(bytes, triangles, sizeof(triangles));
        memcpy(bytes + 256, factors, sizeof(factors));
        memcpy(bytes + 512, tint, sizeof(tint));
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
        pso_desc.RasterizerState.CullMode = mode >= 3 ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
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
        if (then_plain) hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&plain);
        pso_desc.HS.pShaderBytecode = culled ? g_agx_d3d12_probe_hs_const_ccw : no_cbv ? g_agx_d3d12_probe_hs_const : g_agx_d3d12_probe_hs;
        pso_desc.HS.BytecodeLength = culled ? sizeof(g_agx_d3d12_probe_hs_const_ccw)
                                   : no_cbv ? sizeof(g_agx_d3d12_probe_hs_const) : sizeof(g_agx_d3d12_probe_hs);
        pso_desc.DS.pShaderBytecode = no_cbv ? g_agx_d3d12_probe_ds_vcolor : g_agx_d3d12_probe_ds;
        pso_desc.DS.BytecodeLength = no_cbv ? sizeof(g_agx_d3d12_probe_ds_vcolor) : sizeof(g_agx_d3d12_probe_ds);
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("%s CreateGraphicsPipelineState (HS+DS%s) hr=0x%08lx\n", step, then_plain ? " + plain" : "", (unsigned long)hr);
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
        const D3D12_GPU_VIRTUAL_ADDRESS va = ID3D12Resource_GetGPUVirtualAddress(vertices);
        vbv.BufferLocation = va;
        vbv.SizeInBytes = sizeof(float) * 36;
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
        ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list, 0, va + 256);
        ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(list, 1, va + 512);
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
        if (then_plain) {
            ID3D12GraphicsCommandList_SetPipelineState(list, plain);
            ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 3, 0);
        }
        transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &buf_loc, 0, 0, 0, &tex_loc, NULL);
        hr = run_list(queue, fence, list, base + 1, then_plain ? "C37B draw" : "C37A draw");
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
            const unsigned char *centre = bytes + 32 * 256 + 32 * 4, *top_left = bytes + 1 * 256 + 1 * 4;
            const unsigned char *bottom_right = bytes + 62 * 256 + 62 * 4, *top_right = bytes + 1 * 256 + 62 * 4;
            centre_ok = centre[0] == 0 && centre[1] == (no_cbv ? 255 : 0) && centre[2] == (no_cbv ? 0 : 255);
            corners_ok = bottom_right[0] == 255 && bottom_right[2] == 0 && top_right[0] == 255 && top_right[2] == 0;
            plain_ok = then_plain ? top_left[1] == 255 && top_left[2] == 0 && green >= 20u
                     : no_cbv ? top_left[0] == 255 && blue == 0u : top_left[0] == 255 && green == 0u;
            printf("%s readback blue=%u green=%u red=%u other=%u centre=%u,%u,%u top_left=%u,%u,%u\n", step, blue, green, red,
                   other, centre[0], centre[1], centre[2], top_left[0], top_left[1], top_left[2]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    /* the patch covers 0.5 * 51.2 * 51.2 ~ 1311 pixels */
    const unsigned patch = no_cbv ? green : blue;
    const int pass = culled ? SUCCEEDED(hr) && corners_ok && plain_ok && other == 0 && green == 0u && red == 4096u
                            : SUCCEEDED(hr) && centre_ok && corners_ok && plain_ok && other == 0 && patch >= 1150u && patch <= 1450u;
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
static int step_c37a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c37_tessellation(d, q, f, b, 0); }
static int step_c37b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c37_tessellation(d, q, f, b, 1); }
static int step_c37c(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c37_tessellation(d, q, f, b, 2); }
static int step_c37d(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c37_tessellation(d, q, f, b, 3); }
static int step_c37e(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c37_tessellation(d, q, f, b, 4); }

/* C39 (D3D12-C39-stream-output.md): the vertex shader's SV_Position and
 * COLOR captured to a stream-output buffer (stride 32) by two draws of the
 * same triangle: the second appends after the first, so the filled size is
 * 192 bytes and the six vertices are the inputs (positions (x, y, 0, 1),
 * green). The triangle is also rasterized (stream 0). */
static int c39_stream_output(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_INPUT_ELEMENT_DESC elements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_SO_DECLARATION_ENTRY so_entries[2] = {{0, "SV_Position", 0, 0, 4, 0}, {0, "COLOR", 0, 0, 4, 0}};
    const UINT so_stride = 32;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {0, NULL, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                                             D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12Resource *texture = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    ID3D12Resource *vertices = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *so_buffer = committed_buffer(device, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *filled = committed_buffer(device, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0};
    unsigned char *bytes = NULL;
    const float triangle[18] = {-0.8f, -0.8f, 0, 1, 0, 1, 0.0f, 0.8f, 0, 1, 0, 1, 0.8f, -0.8f, 0, 1, 0, 1};
    unsigned long long filled_bytes = ~0ull;
    unsigned bad = 64, centre_green = 0;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HRESULT hr = vertices && so_buffer && filled && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(vertices, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        memset(bytes, 0, 1024);
        memcpy(bytes, triangle, sizeof(triangle)); /* the filled-size source (zero) is at 512 */
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
        pso_desc.StreamOutput.pSODeclaration = so_entries;
        pso_desc.StreamOutput.NumEntries = 2;
        pso_desc.StreamOutput.pBufferStrides = &so_stride;
        pso_desc.StreamOutput.NumStrides = 1;
        pso_desc.StreamOutput.RasterizedStream = 0;
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
        printf("C39A CreateGraphicsPipelineState (stream output) hr=0x%08lx\n", (unsigned long)hr);
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
        D3D12_STREAM_OUTPUT_BUFFER_VIEW sov = {0};
        D3D12_TEXTURE_COPY_LOCATION tex_loc = {0}, buf_loc = {0};
        vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vertices);
        vbv.SizeInBytes = sizeof(triangle);
        vbv.StrideInBytes = sizeof(float) * 6;
        sov.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(so_buffer);
        sov.SizeInBytes = 4096;
        sov.BufferFilledSizeLocation = ID3D12Resource_GetGPUVirtualAddress(filled);
        tex_loc.pResource = texture;
        tex_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        buf_loc.pResource = readback;
        buf_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        buf_loc.PlacedFootprint.Offset = 8192;
        buf_loc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        buf_loc.PlacedFootprint.Footprint.Width = 64;
        buf_loc.PlacedFootprint.Footprint.Height = 64;
        buf_loc.PlacedFootprint.Footprint.Depth = 1;
        buf_loc.PlacedFootprint.Footprint.RowPitch = 256;
        /* the filled size starts at zero */
        ID3D12GraphicsCommandList_CopyBufferRegion(list, filled, 0, vertices, 512, 8);
        transition(list, filled, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_STREAM_OUT);
        transition(list, so_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_STREAM_OUT);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, red_color, 0, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_SOSetTargets(list, 0, 1, &sov);
        ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
        ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
        transition(list, so_buffer, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(list, filled, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, so_buffer, 0, 256);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 4096, filled, 0, 8);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &buf_loc, 0, 0, 0, &tex_loc, NULL);
        hr = run_list(queue, fence, list, base + 1, "C39A draw");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 65536};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            const float *captured = (const float *)bytes;
            memcpy(&filled_bytes, bytes + 4096, sizeof(filled_bytes));
            bad = 0;
            for (unsigned v = 0; v < 6; ++v) {
                const float *in = triangle + 6 * (v % 3), *out = captured + 8 * v;
                const float expect[8] = {in[0], in[1], 0, 1, in[2], in[3], in[4], in[5]};
                for (unsigned c = 0; c < 8; ++c) bad += out[c] != expect[c];
            }
            const unsigned char *centre = bytes + 8192 + 32 * 256 + 32 * 4;
            centre_green = centre[0] == 0 && centre[1] == 255 && centre[2] == 0;
            printf("C39A filled=%llu bad_floats=%u first=%.3f,%.3f,%.3f,%.3f centre=%u,%u,%u\n", filled_bytes, bad,
                   captured[0], captured[1], captured[2], captured[3], centre[0], centre[1], centre[2]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    const int pass = SUCCEEDED(hr) && filled_bytes == 192u && bad == 0 && centre_green;
    printf("AGX_D3D12_C39A: %s\n", pass ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (texture) ID3D12Resource_Release(texture);
    if (readback) ID3D12Resource_Release(readback);
    if (filled) ID3D12Resource_Release(filled);
    if (so_buffer) ID3D12Resource_Release(so_buffer);
    if (vertices) ID3D12Resource_Release(vertices);
    return !pass;
}

/* C38 (D3D12-C38-pso-serialization.md): a triangle pipeline's cached blob
 * (GetCachedBlob -> pfnSerializeObject) recreates the pipeline (CachedPSO
 * -> pfnCreateDeserializedObject); the recreated pipeline draws the green
 * triangle of C37's plain draw region test: centre green, corners red. */
static int c38_cached_pso(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_INPUT_ELEMENT_DESC elements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {0, NULL, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL, *cached = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL, *recreated = NULL;
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
    SIZE_T cached_bytes = 0;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HRESULT hr = vertices && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(vertices, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        const float triangle[18] = {-0.8f, -0.8f, 0, 1, 0, 1, 0.0f, 0.8f, 0, 1, 0, 1, 0.8f, -0.8f, 0, 1, 0, 1};
        memcpy(bytes, triangle, sizeof(triangle));
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
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso_desc.SampleDesc.Count = 1;
        hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C38A CreateGraphicsPipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) {
        hr = ID3D12PipelineState_GetCachedBlob(pso, &cached);
        if (SUCCEEDED(hr)) cached_bytes = ID3D10Blob_GetBufferSize(cached);
        printf("C38A GetCachedBlob hr=0x%08lx bytes=%lu\n", (unsigned long)hr, (unsigned long)cached_bytes);
    }
    if (SUCCEEDED(hr)) {
        pso_desc.CachedPSO.pCachedBlob = ID3D10Blob_GetBufferPointer(cached);
        pso_desc.CachedPSO.CachedBlobSizeInBytes = cached_bytes;
        hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&recreated);
        printf("C38A CreateGraphicsPipelineState (CachedPSO) hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, recreated,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        const FLOAT red_color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        D3D12_VIEWPORT viewport = {0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
        D3D12_RECT scissor = {0, 0, 64, 64};
        D3D12_VERTEX_BUFFER_VIEW vbv = {0};
        D3D12_TEXTURE_COPY_LOCATION tex_loc = {0}, buf_loc = {0};
        vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vertices);
        vbv.SizeInBytes = sizeof(float) * 18;
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
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
        transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &buf_loc, 0, 0, 0, &tex_loc, NULL);
        hr = run_list(queue, fence, list, base + 1, "C38A draw");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            other = 0;
            for (unsigned i = 0; i < 4096; ++i) {
                const unsigned char *px = bytes + 4 * i;
                if (px[0] == 0 && px[1] == 255 && px[2] == 0 && px[3] == 255) ++green;
                else if (px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255) ++red;
                else ++other;
            }
            const unsigned char *centre = bytes + 32 * 256 + 32 * 4, *corner = bytes + 1 * 256 + 1 * 4;
            centre_ok = centre[0] == 0 && centre[1] == 255 && centre[2] == 0;
            corner_ok = corner[0] == 255 && corner[1] == 0;
            printf("C38A readback green=%u red=%u other=%u\n", green, red, other);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    const int pass = SUCCEEDED(hr) && cached_bytes && centre_ok && corner_ok && other == 0 && green >= 1150u && green <= 1450u;
    printf("AGX_D3D12_C38A: %s\n", pass ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (recreated) ID3D12PipelineState_Release(recreated);
    if (cached) ID3D10Blob_Release(cached);
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

/* C28 (EXP1822: nBody's compute submission never retired, DEVICE_HUNG):
 * mode 0 (C28A) a groupshared tile loop with two barriers and a root-CBV
 * bound (exact sums); modes 1/2 (C28B/C28C) the sample's own nBodyGravityCS
 * bytecode over 1 or 8 groups against a CPU reference, timed. Root CBV b0,
 * root SRV t0 (the upload buffer), root UAV u0. */
static int c28_compute(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int mode) {
    const char *step = mode == 0 ? "C28A" : mode == 1 ? "C28B" : "C28C";
    const UINT groups = mode == 0 ? 2u : mode == 1 ? 1u : 8u;
    const UINT n = groups * (mode == 0 ? 64u : 128u);
    D3D12_ROOT_PARAMETER params[3] = {{D3D12_ROOT_PARAMETER_TYPE_CBV}, {D3D12_ROOT_PARAMETER_TYPE_UAV}, {D3D12_ROOT_PARAMETER_TYPE_SRV}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {mode == 0 ? 2u : 3u, params, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12Resource *upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *uav = uav_buffer(device);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    static float particles[8 * 128][8];
    unsigned char *bytes = NULL;
    unsigned bad = n;
    double elapsed_ms = -1.0, worst = 0.0;
    params[0].Descriptor.ShaderRegister = 0;
    params[1].Descriptor.ShaderRegister = 0;
    params[2].Descriptor.ShaderRegister = 0;
    HRESULT hr = upload && uav && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        if (mode == 0) {
            const UINT param[4] = {0, 4, 0, 0};
            memcpy(bytes, param, sizeof(param));
        } else {
            const UINT param[4] = {n, groups, 0, 0};
            const float paramf[4] = {0.1f, 1.0f, 0.0f, 0.0f};
            UINT seed = 12345u;
            memcpy(bytes, param, sizeof(param));
            memcpy(bytes + 16, paramf, sizeof(paramf));
            for (UINT i = 0; i < n; ++i) {
                for (UINT c = 0; c < 3; ++c) {
                    seed = seed * 1664525u + 1013904223u;
                    particles[i][c] = (float)(seed >> 8) / 16777216.0f * 2.0f - 1.0f;
                }
                particles[i][3] = 1.0f;
                particles[i][4] = particles[i][5] = particles[i][6] = particles[i][7] = 0.0f;
            }
            memcpy(bytes + 1024, particles, n * 32u);
        }
        ID3D12Resource_Unmap(upload, 0, NULL);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                              &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = mode == 0 ? (const void *)g_agx_d3d12_probe_cs_tgsm_loop : (const void *)g_agx_d3d12_probe_cs_nbody;
        pso_desc.CS.BytecodeLength = mode == 0 ? sizeof(g_agx_d3d12_probe_cs_tgsm_loop) : sizeof(g_agx_d3d12_probe_cs_nbody);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("%s CreateComputePipelineState hr=0x%08lx\n", step, (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        LARGE_INTEGER t0, t1, f;
        const D3D12_GPU_VIRTUAL_ADDRESS va = ID3D12Resource_GetGPUVirtualAddress(upload);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(list, 0, va);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 1, ID3D12Resource_GetGPUVirtualAddress(uav));
        if (mode) ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(list, 2, va + 1024);
        ID3D12GraphicsCommandList_Dispatch(list, groups, 1, 1);
        transition(list, uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, uav, 0, 65536);
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);
        hr = run_list(queue, fence, list, base + 1, step);
        QueryPerformanceCounter(&t1);
        elapsed_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 65536};
        const float *out = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&out);
        if (SUCCEEDED(hr) && out) {
            bad = 0;
            for (UINT i = 0; i < n; ++i) {
                if (mode == 0) {
                    if (((const UINT *)out)[i] != 32640u) ++bad;
                    continue;
                }
                double ax = 0, ay = 0, az = 0;
                for (UINT j = 0; j < n; ++j) {
                    const double rx = (double)particles[j][0] - particles[i][0], ry = (double)particles[j][1] - particles[i][1],
                                 rz = (double)particles[j][2] - particles[i][2];
                    const double d2 = rx * rx + ry * ry + rz * rz + 0.00125 * 0.00125;
                    const double inv = 1.0 / sqrt(d2), s3 = 66.73 * inv * inv * inv;
                    ax += rx * s3; ay += ry * s3; az += rz * s3;
                }
                const double want[3] = {particles[i][0] + ax * 0.01, particles[i][1] + ay * 0.01, particles[i][2] + az * 0.01};
                for (UINT c = 0; c < 3; ++c) {
                    const double got = out[i * 8 + c], err = fabs(got - want[c]) / (1.0 + fabs(want[c]));
                    if (err > worst) worst = err;
                    if (!(err <= 1e-3)) { ++bad; break; }
                }
            }
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("%s groups=%u threads=%u elapsed_ms=%.1f bad=%u worst=%.3g hr=0x%08lx\n", step, groups, n, elapsed_ms, bad,
           worst, (unsigned long)hr);
    printf("AGX_D3D12_%s: %s\n", step, SUCCEEDED(hr) && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (readback) ID3D12Resource_Release(readback);
    if (uav) ID3D12Resource_Release(uav);
    if (upload) ID3D12Resource_Release(upload);
    return !(SUCCEEDED(hr) && bad == 0);
}
static int step_c28a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c28_compute(d, q, f, b, 0); }
static int step_c28b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c28_compute(d, q, f, b, 1); }
static int step_c28c(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c28_compute(d, q, f, b, 2); }

/* C36 (engines sub-allocate textures from heaps): two 64x64 RGBA8 render
 * targets placed at 0 and 64 KiB of one RT/DS heap (resource heap tier 1),
 * cleared red and green, both read back; PASS = each its own colour. */
static int c36_placed_textures(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_HEAP_DESC heap_desc = {131072, {D3D12_HEAP_TYPE_DEFAULT}, 0, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    ID3D12Heap *heap = NULL;
    ID3D12Resource *textures[2] = {0};
    ID3D12DescriptorHeap *rtv_heap = NULL;
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    UINT bad = 2;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = desc.Height = 64;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HRESULT hr = readback ? ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&heap) : E_FAIL;
    printf("C36A CreateHeap hr=0x%08lx\n", (unsigned long)hr);
    for (UINT i = 0; SUCCEEDED(hr) && i < 2; ++i) {
        hr = ID3D12Device_CreatePlacedResource(device, heap, 65536ull * i, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL,
                                               &IID_ID3D12Resource, (void **)&textures[i]);
        printf("C36A CreatePlacedResource %u hr=0x%08lx\n", i, (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        const FLOAT colours[2][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}};
        const UINT inc = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
        for (UINT i = 0; i < 2; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE at = {rtv.ptr + i * inc};
            D3D12_TEXTURE_COPY_LOCATION src = {0}, dst = {0};
            ID3D12Device_CreateRenderTargetView(device, textures[i], NULL, at);
            ID3D12GraphicsCommandList_ClearRenderTargetView(list, at, colours[i], 0, NULL);
            transition(list, textures[i], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            src.pResource = textures[i];
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.pResource = readback;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Offset = 16384ull * i;
            dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            dst.PlacedFootprint.Footprint.Width = dst.PlacedFootprint.Footprint.Height = 64;
            dst.PlacedFootprint.Footprint.Depth = 1;
            dst.PlacedFootprint.Footprint.RowPitch = 256;
            ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL);
        }
        hr = run_list(queue, fence, list, base + 1, "C36A");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 32768};
        const unsigned char *out = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&out);
        if (SUCCEEDED(hr) && out) {
            bad = 0;
            for (UINT i = 0; i < 2; ++i) {
                const unsigned char *px = out + 16384 * i + 32 * 256 + 32 * 4;
                printf("C36A texture %u centre=%u,%u,%u,%u\n", i, px[0], px[1], px[2], px[3]);
                if (px[0] != (i ? 0 : 255) || px[1] != (i ? 255 : 0) || px[2] != 0 || px[3] != 255) ++bad;
            }
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_C36A: %s\n", SUCCEEDED(hr) && !bad ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    for (UINT i = 0; i < 2; ++i)
        if (textures[i]) ID3D12Resource_Release(textures[i]);
    if (heap) ID3D12Heap_Release(heap);
    if (readback) ID3D12Resource_Release(readback);
    return !(SUCCEEDED(hr) && !bad);
}
/* C47B (D3D12 resource binding tier 2, D3D12-C47-binding-tier2.md): a cs_5_1
 * reading SRV 300 of a 512-entry table (1x1 (10,20,30,255)), point-sampling
 * SRV 301 (2x2: red, green / blue, white) at (0.75, 0.25) through sampler 20
 * of 32 (every other sampler is linear) and writing twelve UAV views of one
 * buffer (256 bytes apart; u11 an append buffer whose counter is at 32768).
 * PASS = u0..u9 word 0 = 1..10; u10 = 11, the texel, green, 5; u11[0] =
 * 0x1234 with counter 1. Needs resource binding tier 2 (512 SRVs, 32
 * samplers and 12 UAVs in tables). */
static int c47_binding_tier2(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_HEAP_PROPERTIES heap_props = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 512 + 12, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_HEAP_DESC sampler_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 32, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_RANGE ranges[3] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 512, 0, 0, 0},
                                        {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 32, 0, 0, 0},
                                        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 12, 0, 0, 0}};
    D3D12_ROOT_PARAMETER params[4] = {{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}, {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE},
                                      {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}, {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {4, params, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12DescriptorHeap *heap = NULL, *samplers = NULL;
    ID3D12Resource *textures[2] = {0};
    ID3D12Resource *upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *uav = uav_buffer(device);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    unsigned char *bytes = NULL;
    UINT got[16] = {0}, bad = 16;
    for (UINT i = 0; i < 3; ++i) {
        params[i].DescriptorTable.NumDescriptorRanges = 1;
        params[i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    params[3].Constants.ShaderRegister = 0;
    params[3].Constants.Num32BitValues = 1;
    HRESULT hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    printf("C47B ResourceBindingTier %d hr=0x%08lx\n", (int)options.ResourceBindingTier, (unsigned long)hr);
    if (SUCCEEDED(hr) && options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_2) hr = E_NOTIMPL;
    if (SUCCEEDED(hr) && !(upload && uav && readback)) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        static const unsigned char texel[4] = {10, 20, 30, 255};
        static const unsigned char quad[2][8] = {{255, 0, 0, 255, 0, 255, 0, 255}, {0, 0, 255, 255, 255, 255, 255, 255}};
        memcpy(bytes, texel, 4);
        memcpy(bytes + 512, quad[0], 8);
        memcpy(bytes + 768, quad[1], 8);
        ID3D12Resource_Unmap(upload, 0, NULL);
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        for (UINT i = 0; SUCCEEDED(hr) && i < 2; ++i) {
            desc.Width = desc.Height = i + 1;
            hr = ID3D12Device_CreateCommittedResource(device, &heap_props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                      NULL, &IID_ID3D12Resource, (void **)&textures[i]);
        }
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &sampler_desc, &IID_ID3D12DescriptorHeap, (void **)&samplers);
    if (SUCCEEDED(hr)) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu, scpu;
        const UINT inc = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT sinc = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &cpu);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(samplers, &scpu);
        for (UINT i = 0; i < 2; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE at = {cpu.ptr + (300 + i) * inc};
            ID3D12Device_CreateShaderResourceView(device, textures[i], NULL, at);
        }
        for (UINT u = 0; u < 12; ++u) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC view = {DXGI_FORMAT_R32_TYPELESS, D3D12_UAV_DIMENSION_BUFFER};
            D3D12_CPU_DESCRIPTOR_HANDLE at = {cpu.ptr + (512 + u) * inc};
            view.Buffer.FirstElement = 64 * u;
            view.Buffer.NumElements = 64;
            view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            if (u == 11) {
                view.Format = DXGI_FORMAT_UNKNOWN;
                view.Buffer.StructureByteStride = 4;
                view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
                view.Buffer.CounterOffsetInBytes = 32768;
            }
            ID3D12Device_CreateUnorderedAccessView(device, uav, u == 11 ? uav : NULL, &view, at);
        }
        for (UINT i = 0; i < 32; ++i) {
            D3D12_SAMPLER_DESC sampler = {i == 20 ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                                          D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                          D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 1, D3D12_COMPARISON_FUNC_NEVER,
                                          {0, 0, 0, 0}, 0.0f, D3D12_FLOAT32_MAX};
            D3D12_CPU_DESCRIPTOR_HANDLE at = {scpu.ptr + i * sinc};
            ID3D12Device_CreateSampler(device, &sampler, at);
        }
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
        printf("C47B D3D12SerializeRootSignature hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                              &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_c47b;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_c47b);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C47B CreateComputePipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        D3D12_GPU_DESCRIPTOR_HANDLE gpu, sgpu, ugpu;
        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
        ID3D12DescriptorHeap *heaps[2] = {heap, samplers};
        src.pResource = upload;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = 256;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        for (UINT i = 0; i < 2; ++i) {
            dst.pResource = textures[i];
            src.PlacedFootprint.Offset = 512 * i;
            src.PlacedFootprint.Footprint.Width = src.PlacedFootprint.Footprint.Height = i + 1;
            ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL);
            transition(list, textures[i], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 2, heaps);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &gpu);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(samplers, &sgpu);
        ugpu.ptr = gpu.ptr + 512ull * ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpu);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 1, sgpu);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 2, ugpu);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(list, 3, 0, 0);
        ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
        transition(list, uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, uav, 0, 65536);
        hr = run_list(queue, fence, list, base + 1, "C47B");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 65536};
        const UINT *out = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&out);
        if (SUCCEEDED(hr) && out) {
            UINT want[16];
            for (UINT u = 0; u < 10; ++u) {
                want[u] = u + 1;
                got[u] = out[64 * u];
            }
            want[10] = 11; want[11] = 0xff1e140au; want[12] = 0xff00ff00u; want[13] = 5;
            want[14] = 0x1234; want[15] = 1;
            for (UINT i = 0; i < 4; ++i) got[10 + i] = out[640 + i];
            got[14] = out[704];
            got[15] = out[32768 / 4];
            bad = 0;
            for (UINT i = 0; i < 16; ++i)
                if (got[i] != want[i]) ++bad;
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("C47B words=");
    for (UINT i = 0; i < 16; ++i) printf("%s%08x", i ? "," : "", got[i]);
    printf(" bad=%u hr=0x%08lx\n", bad, (unsigned long)hr);
    printf("AGX_D3D12_C47B: %s\n", SUCCEEDED(hr) && !bad ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (heap) ID3D12DescriptorHeap_Release(heap);
    if (samplers) ID3D12DescriptorHeap_Release(samplers);
    for (UINT i = 0; i < 2; ++i)
        if (textures[i]) ID3D12Resource_Release(textures[i]);
    if (upload) ID3D12Resource_Release(upload);
    if (uav) ID3D12Resource_Release(uav);
    if (readback) ID3D12Resource_Release(readback);
    return !(SUCCEEDED(hr) && !bad);
}
/* C34 (EXP1826: DynamicIndexing's ps_5_1 g_txMats[] : register(t1) read
 * as g_txMats[index]): four 1x1 RGBA8 textures in a descriptor table, read
 * by a cs_5_1 loop over materials[i]; PASS = each word is its texture's
 * colour; the shader's T0[0:*] registers past the root range's four stay
 * unbound. Root UAV u0, root constant b0 = 4. */
static int c34_ranges(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_HEAP_PROPERTIES heap_props = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, 0}; /* bounded: binding tier 1 */
    D3D12_ROOT_PARAMETER params[3] = {{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}, {D3D12_ROOT_PARAMETER_TYPE_UAV},
                                      {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {3, params, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12DescriptorHeap *heap = NULL;
    ID3D12Resource *textures[4] = {0};
    ID3D12Resource *upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *uav = uav_buffer(device);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    unsigned char *bytes = NULL;
    UINT got[4] = {0}, bad = 4;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &range;
    params[1].Descriptor.ShaderRegister = 0;
    params[2].Constants.ShaderRegister = 0;
    params[2].Constants.Num32BitValues = 1;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = desc.Height = 1;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    HRESULT hr = upload && uav && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        for (UINT i = 0; i < 4; ++i) {
            const unsigned char texel[4] = {(unsigned char)(10 + 40 * i), (unsigned char)(20 + 40 * i), (unsigned char)(30 + 40 * i), 255};
            memcpy(bytes + 512 * i, texel, 4);
        }
        ID3D12Resource_Unmap(upload, 0, NULL);
        for (UINT i = 0; SUCCEEDED(hr) && i < 4; ++i)
            hr = ID3D12Device_CreateCommittedResource(device, &heap_props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                      NULL, &IID_ID3D12Resource, (void **)&textures[i]);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    if (SUCCEEDED(hr)) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu;
        const UINT inc = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &cpu);
        for (UINT i = 0; i < 4; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE at = {cpu.ptr + i * inc};
            ID3D12Device_CreateShaderResourceView(device, textures[i], NULL, at);
        }
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                              &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_ranges;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_ranges);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C34A CreateComputePipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        D3D12_GPU_DESCRIPTOR_HANDLE gpu;
        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
        src.pResource = upload;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = src.PlacedFootprint.Footprint.Height = src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = 256;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        for (UINT i = 0; i < 4; ++i) {
            dst.pResource = textures[i];
            src.PlacedFootprint.Offset = 512 * i;
            ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL);
            transition(list, textures[i], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &gpu);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpu);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 1, ID3D12Resource_GetGPUVirtualAddress(uav));
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(list, 2, 4, 0);
        ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
        transition(list, uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, uav, 0, 16);
        hr = run_list(queue, fence, list, base + 1, "C34A");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16};
        const UINT *out = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&out);
        if (SUCCEEDED(hr) && out) {
            bad = 0;
            for (UINT i = 0; i < 4; ++i) {
                const UINT want = (10u + 40u * i) | ((20u + 40u * i) << 8) | ((30u + 40u * i) << 16) | 0xff000000u;
                got[i] = out[i];
                if (out[i] != want) ++bad;
            }
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("C34A words=%08x,%08x,%08x,%08x bad=%u hr=0x%08lx\n", got[0], got[1], got[2], got[3], bad, (unsigned long)hr);
    printf("AGX_D3D12_C34A: %s\n", SUCCEEDED(hr) && !bad ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (heap) ID3D12DescriptorHeap_Release(heap);
    for (UINT i = 0; i < 4; ++i)
        if (textures[i]) ID3D12Resource_Release(textures[i]);
    if (readback) ID3D12Resource_Release(readback);
    if (uav) ID3D12Resource_Release(uav);
    if (upload) ID3D12Resource_Release(upload);
    return !(SUCCEEDED(hr) && !bad);
}
/* C33 (EXP1826: the ExecuteIndirect sample's culling pass appends to a
 * structured UAV with a counter, which was refused): 128 threads, the even
 * ones Append their id through a table UAV whose counter is word 0 of a
 * separate buffer, set to 5 by a copy first; PASS = counter 69 and slots
 * 5..68 hold every even id 0..126 exactly once. */
static int c33_append(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER param = {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &param, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12DescriptorHeap *heap = NULL;
    ID3D12Resource *upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *data = uav_buffer(device), *counter = uav_buffer(device);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    unsigned char *bytes = NULL;
    UINT count = 0, bad = 64;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;
    HRESULT hr = upload && data && counter && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        const UINT five = 5;
        memcpy(bytes, &five, sizeof(five));
        ID3D12Resource_Unmap(upload, 0, NULL);
        hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    }
    if (SUCCEEDED(hr)) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view = {DXGI_FORMAT_UNKNOWN, D3D12_UAV_DIMENSION_BUFFER};
        D3D12_CPU_DESCRIPTOR_HANDLE cpu;
        view.Buffer.NumElements = 256;
        view.Buffer.StructureByteStride = 4;
        view.Buffer.CounterOffsetInBytes = 0;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &cpu);
        ID3D12Device_CreateUnorderedAccessView(device, data, counter, &view, cpu);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                              &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_append;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_append);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C33A CreateComputePipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        D3D12_GPU_DESCRIPTOR_HANDLE gpu;
        transition(list, counter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, counter, 0, upload, 0, 4);
        transition(list, counter, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &gpu);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpu);
        ID3D12GraphicsCommandList_Dispatch(list, 2, 1, 1);
        transition(list, data, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(list, counter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, data, 0, 1024);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 4096, counter, 0, 4);
        hr = run_list(queue, fence, list, base + 1, "C33A");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 8192};
        const UINT *out = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&out);
        if (SUCCEEDED(hr) && out) {
            unsigned char seen[128] = {0};
            count = out[1024];
            bad = 0;
            for (UINT i = 5; i < 69; ++i) {
                const UINT v = out[i];
                if (v >= 128 || (v & 1u) || seen[v]++) ++bad;
            }
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("C33A counter=%u (want 69) bad_slots=%u hr=0x%08lx\n", count, bad, (unsigned long)hr);
    printf("AGX_D3D12_C33A: %s\n", SUCCEEDED(hr) && count == 69 && !bad ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (heap) ID3D12DescriptorHeap_Release(heap);
    if (readback) ID3D12Resource_Release(readback);
    if (counter) ID3D12Resource_Release(counter);
    if (data) ID3D12Resource_Release(data);
    if (upload) ID3D12Resource_Release(upload);
    return !(SUCCEEDED(hr) && count == 69 && !bad);
}
/* C29 (EXP1825: Bundles, DynamicIndexing and Multithreading stop on their
 * first BC1 texture): a BC1_UNORM 8x8 texture with three mips uploaded
 * through the runtime's GetCopyableFootprints layout (the 2x2 mip has a
 * 4x4 block footprint), decoded by a compute Load into packed RGBA8 words,
 * and mip 0 copied back to a buffer as raw blocks. Each block holds two
 * exact 5:6:5 colours (c0 > c1, four-colour mode) in a checkerboard of
 * indices 0/1, so the decoded texels are exact. */
static UINT c29_expand(UINT16 c) {
    const UINT r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    return ((r << 3) | (r >> 2)) | (((g << 2) | (g >> 4)) << 8) | (((b << 3) | (b >> 2)) << 16) | 0xff000000u;
}
static int c29_bc1_format(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base,
                          DXGI_FORMAT format, const char *step) {
    static const UINT16 colours[6][2] = {{0xF800, 0x07E0}, {0xFFFF, 0x001F}, {0x07E0, 0x001F},
                                         {0xFFFF, 0x0000}, {0xF81F, 0x07E0}, {0xFFE0, 0x001F}};
    static const UINT block_base[3] = {0, 4, 5}, blocks_across[3] = {2, 1, 1};
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[3] = {0};
    UINT rows[3] = {0};
    UINT64 row_bytes[3] = {0}, total = 0;
    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER params[2] = {{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}, {D3D12_ROOT_PARAMETER_TYPE_UAV}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {2, params, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12DescriptorHeap *srv_heap = NULL;
    ID3D12Resource *texture = NULL;
    ID3D12Resource *upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *uav = uav_buffer(device);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    unsigned char *bytes = NULL;
    unsigned bad_texels = 84, bad_blocks = 4;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &range;
    params[1].Descriptor.ShaderRegister = 0;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 8;
    desc.Height = 8;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 3;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    HRESULT hr = upload && uav && readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) {
        ID3D12Device_GetCopyableFootprints(device, &desc, 0, 3, 0, layouts, rows, row_bytes, &total);
        printf("%s footprints total=%llu mip2=%ux%u pitch=%u rows=%u row_bytes=%llu\n", step, (unsigned long long)total,
               layouts[2].Footprint.Width, layouts[2].Footprint.Height, layouts[2].Footprint.RowPitch, rows[2],
               (unsigned long long)row_bytes[2]);
        if (!total || total > 32768u) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        for (UINT m = 0; m < 3; ++m)
            for (UINT by = 0; by < rows[m]; ++by)
                for (UINT bx = 0; bx < blocks_across[m]; ++bx) {
                    const UINT k = block_base[m] + by * blocks_across[m] + bx;
                    unsigned char *block = bytes + layouts[m].Offset + by * layouts[m].Footprint.RowPitch + bx * 8u;
                    UINT indices = 0;
                    for (UINT t = 0; t < 16; ++t) indices |= (((t >> 2) + (t & 3)) & 1u) << (2 * t);
                    memcpy(block, colours[k], 4);
                    memcpy(block + 4, &indices, 4);
                }
        ID3D12Resource_Unmap(upload, 0, NULL);
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                  NULL, &IID_ID3D12Resource, (void **)&texture);
        printf("%s CreateCommittedResource BC1 hr=0x%08lx\n", step, (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &srv_heap_desc, &IID_ID3D12DescriptorHeap, (void **)&srv_heap);
    if (SUCCEEDED(hr)) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap, &cpu);
        ID3D12Device_CreateShaderResourceView(device, texture, NULL, cpu);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                              &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_bc1_load;
        pso_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_bc1_load);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("%s CreateComputePipelineState hr=0x%08lx\n", step, (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        D3D12_TEXTURE_COPY_LOCATION tex_loc = {0}, buf_loc = {0};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu;
        tex_loc.pResource = texture;
        tex_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        buf_loc.pResource = upload;
        buf_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        for (UINT m = 0; m < 3; ++m) {
            tex_loc.SubresourceIndex = m;
            buf_loc.PlacedFootprint = layouts[m];
            ID3D12GraphicsCommandList_CopyTextureRegion(list, &tex_loc, 0, 0, 0, &buf_loc, NULL);
        }
        transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &srv_heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap, &gpu);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpu);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 1, ID3D12Resource_GetGPUVirtualAddress(uav));
        ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
        transition(list, uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(list, texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, uav, 0, 4096);
        tex_loc.SubresourceIndex = 0;
        buf_loc.pResource = readback;
        buf_loc.PlacedFootprint = layouts[0];
        buf_loc.PlacedFootprint.Offset = 32768;
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &buf_loc, 0, 0, 0, &tex_loc, NULL);
        hr = run_list(queue, fence, list, base + 1, step);
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 65536};
        const unsigned char *out = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&out);
        if (SUCCEEDED(hr) && out) {
            UINT word = 0, first_bad = ~0u, got_bad = 0, want_bad = 0;
            bad_texels = 0;
            for (UINT m = 0; m < 3; ++m) {
                const UINT side = 8u >> m;
                for (UINT y = 0; y < side; ++y)
                    for (UINT x = 0; x < side; ++x, ++word) {
                        const UINT k = block_base[m] + (y / 4) * blocks_across[m] + x / 4;
                        const UINT want = c29_expand(colours[k][((y & 3) + (x & 3)) & 1u]);
                        UINT got;
                        memcpy(&got, out + word * 4u, 4);
                        if (got != want) {
                            if (first_bad == ~0u) { first_bad = word; got_bad = got; want_bad = want; }
                            ++bad_texels;
                        }
                    }
            }
            bad_blocks = 0;
            for (UINT by = 0; by < 2; ++by)
                for (UINT bx = 0; bx < 2; ++bx) {
                    UINT indices = 0;
                    unsigned char want[8];
                    for (UINT t = 0; t < 16; ++t) indices |= (((t >> 2) + (t & 3)) & 1u) << (2 * t);
                    memcpy(want, colours[by * 2 + bx], 4);
                    memcpy(want + 4, &indices, 4);
                    if (memcmp(out + 32768 + by * layouts[0].Footprint.RowPitch + bx * 8u, want, 8)) ++bad_blocks;
                }
            printf("%s texels=84 bad=%u first_bad=%u got=%08x want=%08x readback_blocks=4 bad=%u\n", step, bad_texels,
                   first_bad, got_bad, want_bad, bad_blocks);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("%s hr=0x%08lx\n", step, (unsigned long)hr);
    printf("AGX_D3D12_%s: %s\n", step, SUCCEEDED(hr) && !bad_texels && !bad_blocks ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (srv_heap) ID3D12DescriptorHeap_Release(srv_heap);
    if (texture) ID3D12Resource_Release(texture);
    if (readback) ID3D12Resource_Release(readback);
    if (uav) ID3D12Resource_Release(uav);
    if (upload) ID3D12Resource_Release(upload);
    return !(SUCCEEDED(hr) && !bad_texels && !bad_blocks);
}

static int c29_bc1(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    return c29_bc1_format(device, queue, fence, base, DXGI_FORMAT_BC1_UNORM, "C29A");
}
/* C42F (EXP1842 MiniEngine yellow): the engine's diffuse textures are
 * BC1_UNORM_SRGB; endpoint-only blocks of 0/full channels decode to the same
 * values in sRGB and linear, so C29A's expectations hold. */
static int step_c42f(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    return c29_bc1_format(device, queue, fence, base, DXGI_FORMAT_BC1_UNORM_SRGB, "C42F");
}

typedef int (*probe_step)(ID3D12Device *, ID3D12CommandQueue *, ID3D12Fence *, UINT64);
static int step_c6c(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 1); }
static int step_c30a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 9); }
static int step_c31a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 10); }
static int step_c32a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 11); }
static int step_c44b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) {
    if (!shader_model_6(d, "C44B")) {
        printf("AGX_D3D12_C44B: FAIL\n");
        return 1;
    }
    return draw_triangle(d, q, f, b, 12);
}
/* C50A (interface 0033, D3D12-C50-interface-0033.md): HighestShaderModel 6.1
 * and an SV_ViewID pixel shader in a PSO without view instancing: view 0,
 * so the triangle is green (ViewInstancingTier stays NOT_SUPPORTED). */
static int step_c50a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) {
    D3D12_FEATURE_DATA_SHADER_MODEL model = {D3D_SHADER_MODEL_6_1};
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options = {0};
    const HRESULT hr = ID3D12Device_CheckFeatureSupport(d, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model));
    const HRESULT ho = ID3D12Device_CheckFeatureSupport(d, D3D12_FEATURE_D3D12_OPTIONS3, &options, sizeof(options));
    printf("C50A HighestShaderModel 0x%x hr=0x%08lx ViewInstancingTier %d Barycentrics %d hr=0x%08lx\n",
           (unsigned)model.HighestShaderModel, (unsigned long)hr, (int)options.ViewInstancingTier,
           (int)options.BarycentricsSupported, (unsigned long)ho);
    if (FAILED(hr) || model.HighestShaderModel < D3D_SHADER_MODEL_6_1) {
        printf("AGX_D3D12_C50A: FAIL\n");
        return 1;
    }
    return draw_triangle(d, q, f, b, 13);
}
/* C51A (interface 0042, D3D12-C51-interface-plan.md): HighestShaderModel 6.2
 * and C44A's root-constant compute shader as cs_6_2 with fp32 denormals
 * preserved (AGX flushes them; the values are normal). */
static int step_c51a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) {
    D3D12_FEATURE_DATA_SHADER_MODEL model = {D3D_SHADER_MODEL_6_2};
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options = {0};
    const HRESULT hr = ID3D12Device_CheckFeatureSupport(d, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model));
    const HRESULT ho = ID3D12Device_CheckFeatureSupport(d, D3D12_FEATURE_D3D12_OPTIONS4, &options, sizeof(options));
    (void)b;
    printf("C51A HighestShaderModel 0x%x hr=0x%08lx Native16BitShaderOps %d hr=0x%08lx\n", (unsigned)model.HighestShaderModel,
           (unsigned long)hr, (int)options.Native16BitShaderOpsSupported, (unsigned long)ho);
    if (FAILED(hr) || model.HighestShaderModel < D3D_SHADER_MODEL_6_2) {
        printf("AGX_D3D12_C51A: FAIL\n");
        return 1;
    }
    return compute_constants_code(d, q, f, g_agx_d3d12_probe_cs_cb_dxil62, sizeof(g_agx_d3d12_probe_cs_cb_dxil62), "C51A", 0u);
}
/* C58A (EXP1882): D3D9On12 clears with one rect covering the back buffer; the
 * Windows agx_clear refuses scissored clears, so a whole-surface rect must
 * clear without one. */
static int c53_clear(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int rect);
static int step_c53a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c53_clear(d, q, f, b, 0); }
static int step_c58a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c53_clear(d, q, f, b, 1); }
/* C57A: an XOR logic op (OutputMergerLogicOp) on an R8G8B8A8_UINT render
 * target cleared to 0x33: the constant 0x0f,0xf0,0x55,0xaa draws 0x3c,0xc3,0x66,0x99. */
static int step_c57a(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {0};
    D3D12_INPUT_ELEMENT_DESC elements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
    ID3D12Resource *texture = NULL, *vertices = NULL;
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12DescriptorHeap *rtv_heap = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0};
    D3D12_TEXTURE_COPY_LOCATION tex_loc = {0}, buf_loc = {0};
    unsigned char *bytes = NULL;
    unsigned bad = 4096;
    HRESULT hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    printf("C57A OutputMergerLogicOp %d hr=0x%08lx\n", options.OutputMergerLogicOp, (unsigned long)hr);
    if (SUCCEEDED(hr) && !options.OutputMergerLogicOp) hr = E_NOTIMPL;
    if (SUCCEEDED(hr) && !readback) hr = E_FAIL;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
                                                  (void **)&texture);
    if (SUCCEEDED(hr)) {
        const float data[3][6] = {{-1, -1, 0, 0, 0, 0}, {-1, 3, 0, 0, 0, 0}, {3, -1, 0, 0, 0, 0}};
        void *mapped = NULL;
        vertices = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        hr = vertices ? ID3D12Resource_Map(vertices, 0, NULL, &mapped) : E_FAIL;
        if (SUCCEEDED(hr) && !mapped) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            memcpy(mapped, data, sizeof(data));
            ID3D12Resource_Unmap(vertices, 0, NULL);
        }
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    if (SUCCEEDED(hr)) {
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
        ID3D12Device_CreateRenderTargetView(device, texture, NULL, rtv);
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                              &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.VS.pShaderBytecode = g_agx_d3d12_probe_vs;
        pso_desc.VS.BytecodeLength = sizeof(g_agx_d3d12_probe_vs);
        pso_desc.PS.pShaderBytecode = g_agx_d3d12_probe_ps_c57;
        pso_desc.PS.BytecodeLength = sizeof(g_agx_d3d12_probe_ps_c57);
        pso_desc.BlendState.RenderTarget[0].LogicOpEnable = TRUE;
        pso_desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_XOR;
        pso_desc.BlendState.RenderTarget[0].SrcBlend = pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pso_desc.BlendState.RenderTarget[0].DestBlend = pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        pso_desc.BlendState.RenderTarget[0].BlendOp = pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso_desc.SampleMask = ~0u;
        pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso_desc.RasterizerState.DepthClipEnable = TRUE;
        pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso_desc.InputLayout.pInputElementDescs = elements;
        pso_desc.InputLayout.NumElements = 2;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UINT;
        pso_desc.SampleDesc.Count = 1;
        hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("C57A CreateGraphicsPipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        const FLOAT clear[4] = {51.0f, 51.0f, 51.0f, 51.0f};
        D3D12_VIEWPORT viewport = {0, 0, 64, 64, 0, 1};
        D3D12_RECT scissor = {0, 0, 64, 64};
        D3D12_VERTEX_BUFFER_VIEW vbv = {ID3D12Resource_GetGPUVirtualAddress(vertices), 72, 24};
        tex_loc.pResource = texture;
        tex_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        buf_loc.pResource = readback;
        buf_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        buf_loc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UINT;
        buf_loc.PlacedFootprint.Footprint.Width = 64;
        buf_loc.PlacedFootprint.Footprint.Height = 64;
        buf_loc.PlacedFootprint.Footprint.Depth = 1;
        buf_loc.PlacedFootprint.Footprint.RowPitch = 256;
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, clear, 0, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
        transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &buf_loc, 0, 0, 0, &tex_loc, NULL);
        hr = run_list(queue, fence, list, base + 1, "C57A xor");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            bad = 0;
            for (unsigned i = 0; i < 4096; ++i)
                bad += bytes[4 * i] != 0x3c || bytes[4 * i + 1] != 0xc3 || bytes[4 * i + 2] != 0x66 || bytes[4 * i + 3] != 0x99;
            printf("C57A xor readback bad=%u first=%u,%u,%u,%u\n", bad, bytes[0], bytes[1], bytes[2], bytes[3]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_C57A: %s\n", SUCCEEDED(hr) && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (blob) ID3D10Blob_Release(blob);
    if (errors) ID3D10Blob_Release(errors);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (vertices) ID3D12Resource_Release(vertices);
    if (texture) ID3D12Resource_Release(texture);
    if (readback) ID3D12Resource_Release(readback);
    return FAILED(hr) || bad != 0;
}
/* C59A (12_1 ROVs, D3D12-FL-program.md "ROV path"): 32 viewport-covering
 * triangles in one draw read-modify-write each pixel of a rasterizer-ordered
 * byte-address buffer, v = v * 5 + SV_PrimitiveID from 0; only primitive
 * order gives sum(i * 5^(31 - i)). ROVsSupported is reported only with
 * APPLE_AGX_D3D12_ROV=1 (SKIP otherwise). */
/* C59B (EXP1892: C59A read 4096/4096 wrong): the same draw with the order
 * log shader -- per corner pixel the arrival order of the primitive IDs. */
static int c59_rov(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int log) {
    const char *step = log ? "C59B" : "C59A";
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    D3D12_ROOT_PARAMETER parameter = {D3D12_ROOT_PARAMETER_TYPE_UAV};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
    ID3D12Resource *order = uav_buffer(device);
    ID3D12Resource *zeros = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    unsigned want = 0, bad = 4096, first = 0;
    HRESULT hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    printf("%s ROVsSupported %d hr=0x%08lx\n", step, options.ROVsSupported, (unsigned long)hr);
    if (SUCCEEDED(hr) && !options.ROVsSupported) {
        printf("AGX_D3D12_%s: SKIP (ROVsSupported 0)\n", step);
        if (order) ID3D12Resource_Release(order);
        if (zeros) ID3D12Resource_Release(zeros);
        if (readback) ID3D12Resource_Release(readback);
        return 0;
    }
    for (unsigned i = 0; i < 32; ++i) want = want * 5u + i;
    if (SUCCEEDED(hr) && (!order || !zeros || !readback)) hr = E_FAIL;
    if (SUCCEEDED(hr)) {
        void *mapped = NULL;
        hr = ID3D12Resource_Map(zeros, 0, NULL, &mapped);
        if (SUCCEEDED(hr) && !mapped) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            memset(mapped, 0, 65536);
            ID3D12Resource_Unmap(zeros, 0, NULL);
        }
    }
    if (SUCCEEDED(hr)) {
        parameter.Descriptor.ShaderRegister = 1;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                              &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.VS.pShaderBytecode = g_agx_d3d12_probe_vs_c59;
        pso_desc.VS.BytecodeLength = sizeof(g_agx_d3d12_probe_vs_c59);
        pso_desc.PS.pShaderBytecode = log ? g_agx_d3d12_probe_ps_c59b : g_agx_d3d12_probe_ps_c59;
        pso_desc.PS.BytecodeLength = log ? sizeof(g_agx_d3d12_probe_ps_c59b) : sizeof(g_agx_d3d12_probe_ps_c59);
        pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso_desc.SampleMask = ~0u;
        pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso_desc.RasterizerState.DepthClipEnable = TRUE;
        pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 0;
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
        D3D12_VIEWPORT viewport = {0, 0, 64, 64, 0, 1};
        D3D12_RECT scissor = {0, 0, 64, 64};
        transition(list, order, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, order, 0, zeros, 0, 16384);
        transition(list, order, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
        ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(list, 0, ID3D12Resource_GetGPUVirtualAddress(order));
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 0, NULL, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_DrawInstanced(list, 96, 1, 0, 0);
        transition(list, order, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, order, 0, 16384);
        hr = run_list(queue, fence, list, base + 1, log ? "C59B rov log" : "C59A rov");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        unsigned *words = NULL;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&words);
        if (SUCCEEDED(hr) && words) {
            bad = 0;
            first = words[0];
            if (log) {
                /* 64 pixels x {count, ids[32]}: lost updates show as a count
                 * below 32, a wrong order or wrong IDs in the sequence */
                unsigned lost = 0, order_bad = 0;
                for (unsigned px = 0; px < 64; ++px) {
                    const unsigned *e = words + px * 33;
                    unsigned ok = e[0] == 32;
                    for (unsigned i = 0; ok && i < 32; ++i) ok = e[1 + i] == i;
                    lost += e[0] != 32;
                    order_bad += e[0] == 32 && !ok;
                    bad += !ok;
                    if (!ok && (px < 2 || px == 63)) {
                        printf("C59B pixel %u count %u ids", px, e[0]);
                        for (unsigned i = 0; i < 32 && i < e[0]; ++i) printf(" %u", e[1 + i]);
                        printf("\n");
                    }
                }
                printf("C59B rov log bad=%u of 64 (count!=32 %u, order/ids %u)\n", bad, lost, order_bad);
            } else {
                unsigned distinct = 0, seen[8] = {0};
                for (unsigned i = 0; i < 4096; ++i) {
                    bad += words[i] != want;
                    unsigned j = 0;
                    while (j < distinct && seen[j] != words[i]) ++j;
                    if (j == distinct && distinct < 8) seen[distinct++] = words[i];
                }
                printf("C59A rov readback bad=%u first=0x%08x want=0x%08x distinct>=%u\n", bad, first, want, distinct);
            }
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_%s: %s\n", step, SUCCEEDED(hr) && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (blob) ID3D10Blob_Release(blob);
    if (errors) ID3D10Blob_Release(errors);
    if (readback) ID3D12Resource_Release(readback);
    if (zeros) ID3D12Resource_Release(zeros);
    if (order) ID3D12Resource_Release(order);
    return FAILED(hr) || bad != 0;
}
static int step_c59a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c59_rov(d, q, f, b, 0); }
static int step_c59b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c59_rov(d, q, f, b, 1); }
/* C60A (FL 11_0 UAV-only rendering with ForcedSampleCount 8, D3D11-FL111-TIR.md):
 * six triangles (an edge-sharing quad, a sliver, a sub-pixel triangle) add
 * countbits(SV_Coverage) per pixel into a UAV, which must equal the
 * reference coverage of the D3D standard 8x pattern and the top-left rule
 * (dxbc_tir.h). */
/* conservative: 0 forced sample count 8 (C60A), 1 conservative
 * rasterization (C61A), 2 the same behind pass-through tessellation (C61B),
 * 3 behind a pass-through application geometry shader (C61C), 4 into a 4x
 * render target with SV_Coverage counted (C61D: every sample covered) */
static int c60_coverage(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int conservative) {
    const int tess = conservative == 2, gs = conservative == 3, msaa = conservative == 4;
    static const float tris[][6] = {
        {4.0f, 4.0f, 28.0f, 4.0f, 28.0f, 20.0f}, {4.0f, 4.0f, 28.0f, 20.0f, 4.0f, 20.0f},
        {32.25f, 2.5f, 60.0f, 3.125f, 33.0f, 4.0f},
        {40.3125f, 30.6875f, 40.875f, 30.5f, 40.5f, 31.25f},
        {8.5625f, 40.6875f, 30.4375f, 40.6875f, 8.5625f, 60.5f},
        {50.0f, 40.0f, 44.0f, 60.0f, 62.0f, 52.0f}};
    enum { TRIS = sizeof(tris) / sizeof(tris[0]), SAMPLES = 8 };
    const char *step = msaa ? "C61D" : gs ? "C61C" : tess ? "C61B" : conservative ? "C61A" : "C60A";
    ID3D12Resource *target = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0};
    D3D12_ROOT_PARAMETER parameter = {D3D12_ROOT_PARAMETER_TYPE_UAV};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter};
    D3D12_INPUT_ELEMENT_DESC element = {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
                                        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
    ID3D12Resource *counts = uav_buffer(device);
    ID3D12Resource *upload = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *vertices = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    static unsigned want[64 * 64];
    unsigned bad = 4096, total = 0;
    HRESULT hr = counts && upload && vertices && readback ? S_OK : E_FAIL;
    memset(want, 0, sizeof(want));
    if (SUCCEEDED(hr)) {
        float *v = NULL;
        void *z = NULL;
        hr = ID3D12Resource_Map(vertices, 0, NULL, (void **)&v);
        if (SUCCEEDED(hr) && v) {
            for (unsigned i = 0; i < TRIS; ++i) {
                int32_t x[3], y[3];
                struct dxbc_tir_edges e;
                for (unsigned k = 0; k < 3; ++k) {
                    v[i * 6 + k * 2] = tris[i][k * 2] / 32.0f - 1.0f;
                    v[i * 6 + k * 2 + 1] = 1.0f - tris[i][k * 2 + 1] / 32.0f;
                    x[k] = (int32_t)(tris[i][k * 2] * 256.0f);
                    y[k] = (int32_t)(tris[i][k * 2 + 1] * 256.0f);
                }
                if (!dxbc_tir_setup(x, y, &e)) continue;
                for (int py = 0; py < 64; ++py)
                    for (int px = 0; px < 64; ++px) {
                        unsigned m = dxbc_tir_mask(&e, px, py, SAMPLES), n = 0;
                        for (; m; m &= m - 1) ++n;
                        /* C61A: one invocation per touched pixel */
                        if (conservative) n = (unsigned)dxbc_tir_touched(&e, px, py) * (msaa ? 4u : 1u);
                        want[py * 64 + px] += n;
                        total += n;
                    }
            }
            ID3D12Resource_Unmap(vertices, 0, NULL);
        } else if (SUCCEEDED(hr)) hr = E_FAIL;
        if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, &z);
        if (SUCCEEDED(hr) && z) {
            memset(z, 0, 65536);
            ID3D12Resource_Unmap(upload, 0, NULL);
        } else if (SUCCEEDED(hr)) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) {
        parameter.Descriptor.ShaderRegister = 0;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                              &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.VS.pShaderBytecode = g_agx_d3d12_probe_vs_c60;
        pso_desc.VS.BytecodeLength = sizeof(g_agx_d3d12_probe_vs_c60);
        if (msaa) {
            D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
            D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                        {4, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET};
            D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
            hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                      D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
                                                      (void **)&target);
            if (SUCCEEDED(hr))
                hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
            if (SUCCEEDED(hr)) {
                ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
                ID3D12Device_CreateRenderTargetView(device, target, NULL, rtv);
            }
        }
        if (gs) {
            pso_desc.GS.pShaderBytecode = g_agx_d3d12_probe_gs_c61c;
            pso_desc.GS.BytecodeLength = sizeof(g_agx_d3d12_probe_gs_c61c);
        }
        if (tess) {
            pso_desc.HS.pShaderBytecode = g_agx_d3d12_probe_hs_c61b;
            pso_desc.HS.BytecodeLength = sizeof(g_agx_d3d12_probe_hs_c61b);
            pso_desc.DS.pShaderBytecode = g_agx_d3d12_probe_ds_c61b;
            pso_desc.DS.BytecodeLength = sizeof(g_agx_d3d12_probe_ds_c61b);
        }
        pso_desc.PS.pShaderBytecode = conservative ? g_agx_d3d12_probe_ps_c61 : g_agx_d3d12_probe_ps_c60;
        pso_desc.PS.BytecodeLength = conservative ? sizeof(g_agx_d3d12_probe_ps_c61) : sizeof(g_agx_d3d12_probe_ps_c60);
        pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso_desc.SampleMask = ~0u;
        pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso_desc.RasterizerState.DepthClipEnable = TRUE;
        pso_desc.RasterizerState.ForcedSampleCount = conservative ? 0 : SAMPLES;
        pso_desc.RasterizerState.ConservativeRaster =
            conservative ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso_desc.InputLayout.pInputElementDescs = &element;
        pso_desc.InputLayout.NumElements = 1;
        pso_desc.PrimitiveTopologyType = tess ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 0;
        pso_desc.SampleDesc.Count = 1;
        if (msaa) {
            pso_desc.PS.pShaderBytecode = g_agx_d3d12_probe_ps_c61d;
            pso_desc.PS.BytecodeLength = sizeof(g_agx_d3d12_probe_ps_c61d);
            pso_desc.NumRenderTargets = 1;
            pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            pso_desc.SampleDesc.Count = 4;
        }
        hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("%s CreateGraphicsPipelineState (%s) hr=0x%08lx\n", step,
               gs ? "conservative, geometry shader" : tess ? "conservative, tessellated" : conservative ? "conservative" : "ForcedSampleCount 8",
               (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        D3D12_VIEWPORT viewport = {0, 0, 64, 64, 0, 1};
        D3D12_RECT scissor = {0, 0, 64, 64};
        D3D12_VERTEX_BUFFER_VIEW vbv = {ID3D12Resource_GetGPUVirtualAddress(vertices), (UINT)(TRIS * 24), 8};
        transition(list, counts, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, counts, 0, upload, 0, 16384);
        transition(list, counts, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
        ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(list, 0, ID3D12Resource_GetGPUVirtualAddress(counts));
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, msaa ? 1 : 0, msaa ? &rtv : NULL, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(
            list, tess ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_DrawInstanced(list, (UINT)(TRIS * 3), 1, 0, 0);
        transition(list, counts, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, counts, 0, 16384);
        hr = run_list(queue, fence, list, base + 1, step);
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        unsigned *words = NULL, got = 0, shown = 0;
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&words);
        if (SUCCEEDED(hr) && words) {
            bad = 0;
            for (unsigned i = 0; i < 4096; ++i) {
                got += words[i];
                if (words[i] != want[i]) {
                    if (shown++ < 6) printf("%s pixel %u,%u got %u want %u\n", step, i % 64, i / 64, words[i], want[i]);
                    ++bad;
                }
            }
            printf("%s counts got %u want %u bad=%u\n", step, got, total, bad);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_%s: %s\n", step, SUCCEEDED(hr) && bad == 0 ? "PASS" : "FAIL");
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (target) ID3D12Resource_Release(target);
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (blob) ID3D10Blob_Release(blob);
    if (errors) ID3D10Blob_Release(errors);
    if (readback) ID3D12Resource_Release(readback);
    if (vertices) ID3D12Resource_Release(vertices);
    if (upload) ID3D12Resource_Release(upload);
    if (counts) ID3D12Resource_Release(counts);
    return FAILED(hr) || bad != 0;
}
static int step_c60a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return c60_coverage(d, q, f, b, 0); }
/* C61A (12_1 conservative rasterization Tier 1, ConservativeRasterizationTier
 * reported only with APPLE_AGX_D3D12_CR=1; SKIP otherwise): the C60A triangles
 * with ConservativeRaster ON; one invocation per pixel each triangle touches
 * (dxbc_tir_touched), none elsewhere. */
static int step_c61a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    HRESULT hr = ID3D12Device_CheckFeatureSupport(d, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    printf("C61A ConservativeRasterizationTier %d hr=0x%08lx\n", (int)options.ConservativeRasterizationTier, (unsigned long)hr);
    if (FAILED(hr) || options.ConservativeRasterizationTier < D3D12_CONSERVATIVE_RASTERIZATION_TIER_1) {
        printf("AGX_D3D12_C61A: SKIP (tier 0)\n");
        return 0;
    }
    return c60_coverage(d, q, f, b, 1);
}
/* C61B: C61A behind a pass-through tessellation (factor 1) -- the expansion
 * GS follows the domain shader */
static int step_c61b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    HRESULT hr = ID3D12Device_CheckFeatureSupport(d, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    if (FAILED(hr) || options.ConservativeRasterizationTier < D3D12_CONSERVATIVE_RASTERIZATION_TIER_1) {
        printf("AGX_D3D12_C61B: SKIP (tier 0)\n");
        return 0;
    }
    return c60_coverage(d, q, f, b, 2);
}
/* C61C: C61A behind a pass-through application GS (the wrapped expansion) */
static int step_c61c(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    HRESULT hr = ID3D12Device_CheckFeatureSupport(d, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    if (FAILED(hr) || options.ConservativeRasterizationTier < D3D12_CONSERVATIVE_RASTERIZATION_TIER_1) {
        printf("AGX_D3D12_C61C: SKIP (tier 0)\n");
        return 0;
    }
    return c60_coverage(d, q, f, b, 3);
}
/* C61D: C61A into a 4x render target, SV_Coverage counted (all samples) */
static int step_c61d(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    HRESULT hr = ID3D12Device_CheckFeatureSupport(d, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    if (FAILED(hr) || options.ConservativeRasterizationTier < D3D12_CONSERVATIVE_RASTERIZATION_TIER_1) {
        printf("AGX_D3D12_C61D: SKIP (tier 0)\n");
        return 0;
    }
    return c60_coverage(d, q, f, b, 4);
}
/* C62A: the reported maximum feature level follows the driver's variables,
 * through the runtime's FEATURE_LEVELS query: 11_0 with
 * APPLE_AGX_D3D12_FL111=0; 11_1 with APPLE_AGX_D3D12_FL12=0 or
 * APPLE_AGX_D3D12_TILED=0/1; 12_0 with ROV=0 or CR=0; else 12_1 (C68). */
static int env_is(const char *name, char c) {
    char value[2] = {0};
    return GetEnvironmentVariableA(name, value, sizeof(value)) == 1 && value[0] == c;
}
static int step_c62a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) {
    static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_12_0,
                                               D3D_FEATURE_LEVEL_12_1};
    D3D12_FEATURE_DATA_FEATURE_LEVELS data = {4, levels};
    const int fl12 = !env_is("APPLE_AGX_D3D12_FL12", '0') && !env_is("APPLE_AGX_D3D12_TILED", '0') &&
                     !env_is("APPLE_AGX_D3D12_TILED", '1');
    const int fl121 = fl12 && !env_is("APPLE_AGX_D3D12_ROV", '0') && !env_is("APPLE_AGX_D3D12_CR", '0');
    const D3D_FEATURE_LEVEL expected = env_is("APPLE_AGX_D3D12_FL111", '0') ? D3D_FEATURE_LEVEL_11_0
                                     : fl121 ? D3D_FEATURE_LEVEL_12_1
                                     : fl12  ? D3D_FEATURE_LEVEL_12_0
                                             : D3D_FEATURE_LEVEL_11_1;
    HRESULT hr = ID3D12Device_CheckFeatureSupport(d, D3D12_FEATURE_FEATURE_LEVELS, &data, sizeof(data));
    (void)q; (void)f; (void)b;
    printf("C62A MaxSupportedFeatureLevel 0x%x expected 0x%x hr=0x%08lx\n", (unsigned)data.MaxSupportedFeatureLevel,
           (unsigned)expected, (unsigned long)hr);
    printf("AGX_D3D12_C62A: %s\n", SUCCEEDED(hr) && data.MaxSupportedFeatureLevel == expected ? "PASS" : "FAIL");
    return SUCCEEDED(hr) && data.MaxSupportedFeatureLevel == expected ? 0 : 1;
}
/* C53A (EXP1875): D3D9On12 renders a D3D9 X8R8G8B8 back buffer as a committed
 * DXGI_FORMAT_B8G8R8X8_UNORM render target, and its first batch (clear, copy
 * to a readback buffer) faulted the GPU; the same clear and copy in that
 * format. Bytes B,G,R = 191,128,64; the X byte is undefined. */
static int c53_clear(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, int rect) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    ID3D12Resource *texture = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0};
    D3D12_TEXTURE_COPY_LOCATION tex_loc = {0}, buf_loc = {0};
    unsigned char *bytes = NULL;
    unsigned bad = 4096;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HRESULT hr = readback ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
                                                  (void **)&texture);
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
    printf("C53A B8G8R8X8 texture + RTV + list hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        const FLOAT color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
        tex_loc.pResource = texture;
        tex_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        buf_loc.pResource = readback;
        buf_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        buf_loc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
        buf_loc.PlacedFootprint.Footprint.Width = 64;
        buf_loc.PlacedFootprint.Footprint.Height = 64;
        buf_loc.PlacedFootprint.Footprint.Depth = 1;
        buf_loc.PlacedFootprint.Footprint.RowPitch = 256;
        const D3D12_RECT whole = {0, 0, 64, 64};
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, color, rect ? 1 : 0, rect ? &whole : NULL);
        transition(list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &buf_loc, 0, 0, 0, &tex_loc, NULL);
        hr = run_list(queue, fence, list, base + 1, rect ? "C58A clear" : "C53A clear");
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        hr = ID3D12Resource_Map(readback, 0, &all, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            bad = 0;
            for (unsigned i = 0; i < 4096; ++i)
                bad += bytes[4 * i] != 191 || bytes[4 * i + 1] != 128 || bytes[4 * i + 2] != 64;
            printf("%s clear readback bad=%u first=%u,%u,%u,%u\n", rect ? "C58A" : "C53A", bad, bytes[0], bytes[1], bytes[2], bytes[3]);
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    printf("AGX_D3D12_%s: %s\n", rect ? "C58A" : "C53A", SUCCEEDED(hr) && bad == 0 ? "PASS" : "FAIL");
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (texture) ID3D12Resource_Release(texture);
    if (readback) ID3D12Resource_Release(readback);
    return FAILED(hr) || bad != 0;
}
/* C52A (interface 0071): HighestShaderModel 6.5 and the core SM 6.4/6.5
 * intrinsics (agx_d3d12_probe_cs_sm65_dxil.hlsl: packed dot products, wave
 * match and multi-prefix sum/count/product); every dword 0x3f. */
static int step_c52a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) {
    D3D12_FEATURE_DATA_SHADER_MODEL model = {D3D_SHADER_MODEL_6_5};
    const HRESULT hr = ID3D12Device_CheckFeatureSupport(d, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model));
    (void)b;
    printf("C52A HighestShaderModel 0x%x hr=0x%08lx\n", (unsigned)model.HighestShaderModel, (unsigned long)hr);
    if (FAILED(hr) || model.HighestShaderModel < D3D_SHADER_MODEL_6_5) {
        printf("AGX_D3D12_C52A: FAIL\n");
        return 1;
    }
    return compute_constants_code(d, q, f, g_agx_d3d12_probe_cs_sm65_dxil, sizeof(g_agx_d3d12_probe_cs_sm65_dxil), "C52A", 0x3fu);
}
static int step_c6d(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 2); }
static int step_c11(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 3); }
static int step_c13(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 4); }
static int step_c14(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 5); }
static int step_c17b(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 7); }
static int step_c16a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { (void)b; return compute_srv(d, q, f, 1); }
/* rc77: 0x10000 is the VA the rc37 drill proved unmapped (root edge absent,
 * Wom1GpuFaultMapping EXP2005/EXP2007); below every dxgkrnl GPU VA range. */
static int step_rc77d(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) {
    (void)b; return compute_srv_va(d, q, f, 0, 0x10000ULL, 0);
}
/* rc82: the soft-fault discriminator (compute_srv_va soft_probe). */
static int step_rc82a(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) {
    (void)b; return compute_srv_va(d, q, f, 0, 0, 1);
}
static int step_c14nc(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 6); }
static int step_rc38z(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 14); }
static int step_rc38n(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 15); }
static int step_rc38i(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 16); }
static int step_rc43i16z(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 17); }
static int step_rc43i16o(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 18); }
static int step_rc43i16n(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 19); }
static int step_rc43i32z(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 20); }
static int step_rc43i32o(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 21); }
static int step_rc43i32n(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return draw_triangle(d, q, f, b, 22); }
/* EXP1826 (C31A): "warp STEP..." runs steps on WARP with the debug layer
 * and prints its messages, separating API misuse in a probe step from
 * driver behaviour before any hardware run. */
static void dump_debug_messages(ID3D12Device *device, const char *name) {
    ID3D12InfoQueue *q = NULL;
    if (FAILED(ID3D12Device_QueryInterface(device, &IID_ID3D12InfoQueue, (void **)&q))) return;
    const UINT64 n = ID3D12InfoQueue_GetNumStoredMessages(q);
    for (UINT64 m = 0; m < n; ++m) {
        SIZE_T len = 0;
        ID3D12InfoQueue_GetMessage(q, m, NULL, &len);
        D3D12_MESSAGE *msg = (D3D12_MESSAGE *)malloc(len);
        if (msg && SUCCEEDED(ID3D12InfoQueue_GetMessage(q, m, msg, &len)))
            printf("%s DEBUG %d %d %s\n", name, (int)msg->Severity, (int)msg->ID, msg->pDescription);
        free(msg);
    }
    ID3D12InfoQueue_Release(q);
}
static DWORD WINAPI q3_watchdog(LPVOID unused);
static int run_isolated(IDXGIAdapter1 *adapter, const char *name, probe_step step) {
    ID3D12Device *device = NULL;
    ID3D12CommandQueue *queue = NULL;
    ID3D12Fence *fence = NULL;
    D3D12_COMMAND_QUEUE_DESC desc = {D3D12_COMMAND_LIST_TYPE_DIRECT};
    if (!strcmp(name, "Q3RC") || !strcmp(name, "Q3RCR") || !strcmp(name, "Q3RR") || !strcmp(name, "Q3CR") || !strcmp(name, "Q3PC") || !strcmp(name, "Q3CP")) {
        HANDLE watchdog = CreateThread(NULL, 0, q3_watchdog, (LPVOID)name, 0, NULL);
        if (!watchdog) {
            printf("AGX_D3D12_%s: FAIL watchdog-create\n", name); fflush(stdout);
            TerminateProcess(GetCurrentProcess(), 1); return 1;
        }
        CloseHandle(watchdog); /* lives until forced process exit below */
    }
    HRESULT hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandQueue(device, &desc, &IID_ID3D12CommandQueue, (void **)&queue);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    printf("%s fresh device/queue/fence hr=0x%08lx\n", name, (unsigned long)hr);
    int failed = FAILED(hr) ? 1 : step(device, queue, fence, 0);
    if (!strcmp(name, "Q3RC") || !strcmp(name, "Q3RCR") || !strcmp(name, "Q3RR") || !strcmp(name, "Q3CR") || !strcmp(name, "Q3PC") || !strcmp(name, "Q3CP")) {
        if (FAILED(hr)) printf("AGX_D3D12_%s: FAIL fresh-device\n", name);
        if (g_warp && device) dump_debug_messages(device, name);
        fflush(stdout);
        /* Q3 modes are standalone process steps. Never enter unbounded COM/DLL
         * teardown after a timeout, device removal, or even successful readback. */
        TerminateProcess(GetCurrentProcess(), failed ? 1u : 0u);
        return failed;
    }
    if (g_warp && device) dump_debug_messages(device, name);
    if (fence) ID3D12Fence_Release(fence);
    if (queue) ID3D12CommandQueue_Release(queue);
    if (device) ID3D12Device_Release(device);
    /* EXP1859: the x64 SM6 run died of heap corruption (0xc0000374) after
     * C38A; name the first step after which a process heap fails validation */
    {
        HANDLE heaps[64];
        const DWORD count = GetProcessHeaps(64, heaps);
        for (DWORD i = 0; i < count && i < 64; ++i)
            if (!HeapValidate(heaps[i], 0, NULL)) {
                printf("PROBE_HEAP_CORRUPT after %s heap %lu\n", name, (unsigned long)i);
                break;
            }
    }
    return failed;
}

/* Private mode: parse only numeric inherited handles and the exact adapter LUID.
 * The child returns its step status, never enters normal probe enumeration. */
static int c76_child_main(int argc, char **argv) {
    unsigned long long values[8];
    if (argc != 10) return 1;
    for (unsigned i = 0; i < 8; ++i) {
        char *end = NULL;
        if (!argv[i + 2][0] || strspn(argv[i + 2], "0123456789") != strlen(argv[i + 2])) return 1;
        errno = 0;
        values[i] = strtoull(argv[i + 2], &end, 10);
        if (errno || !end || *end || (i < 5 && (!values[i] || values[i] > (UINT_PTR)-1)) ||
            (i >= 5 && i < 7 && values[i] > MAXDWORD) || (i == 7 && values[i] > 1)) return 1;
    }
    c76_shared = (HANDLE)(UINT_PTR)values[0]; c76_ready = (HANDLE)(UINT_PTR)values[1];
    c76_go = (HANDLE)(UINT_PTR)values[2]; c76_done = (HANDLE)(UINT_PTR)values[3]; c76_parent = (HANDLE)(UINT_PTR)values[4];
    LUID luid = {(DWORD)values[5], (LONG)(DWORD)values[6]};
    IDXGIFactory4 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    /* Ensure successful open receipts are not filtered by the parent's setting. */
    if (!SetEnvironmentVariableW(L"APPLE_AGX_UMD_REFUSALS_ONLY", L"0")) return 1;
    g_warp = (int)values[7];
    HRESULT hr = S_OK;
    if (g_warp) {
        ID3D12Debug *debug = NULL;
        hr = D3D12GetDebugInterface(&IID_ID3D12Debug, (void **)&debug);
        if (SUCCEEDED(hr)) {
            ID3D12Debug_EnableDebugLayer(debug);
            ID3D12Debug_Release(debug);
        }
    }
    if (SUCCEEDED(hr)) hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory);
    if (SUCCEEDED(hr)) hr = IDXGIFactory4_EnumAdapterByLuid(factory, luid, &IID_IDXGIAdapter1, (void **)&adapter);
    printf("C76A child adapter=%08lx:%08lx hr=0x%08lx\n", (DWORD)luid.HighPart, luid.LowPart, (unsigned long)hr);
    int failed = FAILED(hr) ? 1 : run_isolated(adapter, "C76A_CHILD", c76_child_read);
    if (adapter) IDXGIAdapter1_Release(adapter);
    if (factory) IDXGIFactory4_Release(factory);
    if (c76_shared) CloseHandle(c76_shared);
    CloseHandle(c76_ready); CloseHandle(c76_go); CloseHandle(c76_done); CloseHandle(c76_parent);
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

/* EXP1858: an access violation inside D3D12Core.dll (x64 C38A under R1)
 * killed the process without a dump; with AGX_PROBE_CRASH_DUMP set an
 * unhandled exception writes a minidump there (dbghelp loaded on demand). */
typedef BOOL (WINAPI *MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, int, void *, void *, void *);
static LONG WINAPI crash_dump(EXCEPTION_POINTERS *ep) {
    char path[MAX_PATH];
    struct { DWORD thread; EXCEPTION_POINTERS *pointers; BOOL client; } info = {GetCurrentThreadId(), ep, FALSE};
    const DWORD n = GetEnvironmentVariableA("AGX_PROBE_CRASH_DUMP", path, sizeof(path));
    HMODULE dbghelp = n && n < sizeof(path) ? LoadLibraryA("dbghelp.dll") : NULL;
    MiniDumpWriteDumpFn write = dbghelp ? (MiniDumpWriteDumpFn)(void *)GetProcAddress(dbghelp, "MiniDumpWriteDump") : NULL;
    printf("PROBE_CRASH 0x%08lx at %p\n", (unsigned long)ep->ExceptionRecord->ExceptionCode,
           ep->ExceptionRecord->ExceptionAddress);
    {
        /* EXP1891: the dump came out empty; the faulting module and offset
         * (and an access violation's data address) still locate the fault */
        HMODULE module = NULL;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)ep->ExceptionRecord->ExceptionAddress, &module))
            GetModuleFileNameA(module, name, sizeof(name));
        printf("PROBE_CRASH_MODULE %s+0x%llx data 0x%llx\n", name,
               (unsigned long long)((const char *)ep->ExceptionRecord->ExceptionAddress - (const char *)module),
               ep->ExceptionRecord->NumberParameters > 1
                   ? (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1] : 0ull);
    }
    if (write) {
        HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            /* MiniDumpWithDataSegs | WithIndirectlyReferencedMemory | WithThreadInfo */
            const BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), f, 0x1 | 0x40 | 0x1000, &info, NULL, NULL);
            CloseHandle(f);
            printf("PROBE_CRASH_DUMP %s %d error %lu\n", path, ok, ok ? 0ul : (unsigned long)GetLastError());
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
/* the first access violation, also one a runtime frame would catch: its
 * dump shows the faulting stack before any later corruption */
static LONG WINAPI first_access_violation(EXCEPTION_POINTERS *ep) {
    static volatile LONG seen;
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && !InterlockedExchange(&seen, 1))
        crash_dump(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

#include "agx_d3d12_probe_material.h"

/* the steps WARP ("warp <steps>") and the AGX adapter ("only <steps>",
 * EXP1862: one step under the guest debugger) run by name */
/* RC63G (EXP1982b GravityMark HD50k stream hang): the GravityMark shape that
 * RC43/RC57/RC59G leave out. One R32 indexed counted ExecuteIndirect, stride
 * 20, MaxCount 60000 with all 60000 records live, records and count written
 * by GPU copies into a DEFAULT buffer behind an INDIRECT_ARGUMENT barrier,
 * inside an open pipeline-statistics query, with a D32 depth test, a PS
 * texture SRV table plus static sampler, and per-record BaseVertexLocation
 * and StartInstanceLocation that select per-vertex positions and per-instance
 * texture coordinates (the stream's per-record PARAMS uniform; every earlier
 * stream probe used zero for both). Record r draws quad r % 4096 over pixel
 * r % 4096 with instance r; depth LESS at z = 0 keeps the first record, so
 * each pixel must show the texel that instance r % 4096 addresses. A dropped
 * base instance, a dropped base vertex, a lost record or a reordered draw
 * changes pixels; the counters prove all 60000 records ran. */
#define RC63_MAX 60000u
#define RC63_PIXELS 4096u
#define RC63_TEXELS 0u
#define RC63_POSITIONS 16384u
#define RC63_INDICES (RC63_POSITIONS + RC63_PIXELS * 4u * 8u)
#define RC63_INSTANCES (RC63_INDICES + 256u)
#define RC63_RECORDS (RC63_INSTANCES + RC63_MAX * 4u)
#define RC63_COUNT (RC63_RECORDS + RC63_MAX * 20u)
#define RC63_UPLOAD_BYTES (RC63_COUNT + 512u)
#define RC63_GPU_COUNT (RC63_MAX * 20u)
static void rc63_record(UINT r, UINT out[5]) {
    out[0] = 6u;                               /* IndexCountPerInstance */
    out[1] = 1u;                               /* InstanceCount */
    out[2] = 0u;                               /* StartIndexLocation */
    out[3] = 4u * (r % RC63_PIXELS);           /* BaseVertexLocation */
    out[4] = r;                                /* StartInstanceLocation */
}
/* Texel i of the 64x64 source: unique R/G, fixed B/A. */
static void rc63_texel(UINT i, unsigned char out[4]) {
    out[0] = (unsigned char)((i % 64u) * 4u + 1u);
    out[1] = (unsigned char)((i / 64u) * 4u + 1u);
    out[2] = 0x40u;
    out[3] = 255u;
}
/* Instance r's COLOR (UNORM8 texture coordinate) addresses one texel centre
 * under point sampling: floor(v / 255 * 64) == texel, |error| <= 0.125. A
 * record past the first 4096 addresses the next texel, so it can never
 * pass in place of the first record of its pixel. */
static void rc63_uv(UINT r, unsigned char out[4]) {
    const UINT q = r < RC63_PIXELS ? r : (r % RC63_PIXELS + 1u) % RC63_PIXELS;
    out[0] = (unsigned char)(((q % 64u) * 2u + 1u) * 255u / 128u + (((q % 64u) * 2u + 1u) * 255u % 128u >= 64u));
    out[1] = (unsigned char)(((q / 64u) * 2u + 1u) * 255u / 128u + (((q / 64u) * 2u + 1u) * 255u % 128u >= 64u));
    out[2] = 0u;
    out[3] = 255u;
}
/* Quad q covers only the centre of pixel (q % 64, q / 64) of the 64x64
 * target: x in [x + 0.25, x + 0.75] pixels, in the probe's strip order. */
static void rc63_quad(UINT q, float out[8]) {
    const float x0 = ((float)(q % 64u) + 0.25f) / 32.0f - 1.0f, x1 = ((float)(q % 64u) + 0.75f) / 32.0f - 1.0f;
    const float y0 = 1.0f - ((float)(q / 64u) + 0.75f) / 32.0f, y1 = 1.0f - ((float)(q / 64u) + 0.25f) / 32.0f;
    out[0] = x0; out[1] = y0; out[2] = x0; out[3] = y1;
    out[4] = x1; out[5] = y0; out[6] = x1; out[7] = y1;
}
/* rc89: record r < n covers pixel r % 4096 first, so pixel p shows texel p
 * when p < n and keeps the red clear colour otherwise. */
static unsigned rc63_bad_pixels_n(const unsigned char *rows, UINT *first, UINT n) {
    static const unsigned char red[4] = {255u, 0u, 0u, 255u};
    unsigned bad = 0;
    *first = RC63_PIXELS;
    for (UINT p = 0; p < RC63_PIXELS; ++p) {
        unsigned char want[4];
        const unsigned char *px = rows + (p / 64u) * 256u + (p % 64u) * 4u;
        if (p < n) rc63_texel(p, want); else memcpy(want, red, sizeof(want));
        if (memcmp(px, want, 4)) { if (!bad) *first = p; ++bad; }
    }
    return bad;
}
static unsigned rc63_bad_pixels(const unsigned char *rows, UINT *first) {
    return rc63_bad_pixels_n(rows, first, RC63_MAX);
}
static int rc63_statistics_ok_n(const D3D12_QUERY_DATA_PIPELINE_STATISTICS *q, UINT n) {
    const UINT64 draws = n;
    return q->IAVertices == 6u * draws && q->IAPrimitives == 2u * draws && q->VSInvocations == 6u * draws &&
           q->CInvocations == 2u * draws && q->CPrimitives == 2u * draws && (q->PSInvocations != 0) == (n != 0) &&
           !q->GSInvocations && !q->GSPrimitives && !q->HSInvocations && !q->DSInvocations && !q->CSInvocations;
}
static int rc63_statistics_ok(const D3D12_QUERY_DATA_PIPELINE_STATISTICS *q) {
    return rc63_statistics_ok_n(q, RC63_MAX);
}
/* One GPU-counted execution of the RC63G shape and what it showed. */
struct rc63_run {
    UINT count;          /* the GPU count the run copies, <= RC63_MAX */
    HRESULT hr;
    unsigned bad;        /* pixels that differ from a complete draw of count */
    UINT first;
    UINT64 draws;        /* IAVertices / 6: records the GPU drew */
    int pixels_ok, statistics_ok, statistics_read;
};
/* Builds the RC63G scene once and executes it once per run, each run with
 * its own GPU count (RC63G: one run of RC63_MAX; RC89S: several on one
 * device, so the driver's stream capacity state carries across runs). */
static HRESULT rc63_stream_runs(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base,
                                const char *step, struct rc63_run *runs, UINT run_count) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0}, depth_desc = {0}, tex_desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_DESCRIPTOR_HEAP_DESC dsv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_INPUT_ELEMENT_DESC elements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1}};
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER parameter = {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE};
    D3D12_STATIC_SAMPLER_DESC sampler = {D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                         D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 1,
                                         D3D12_COMPARISON_FUNC_NEVER, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, 0.0f,
                                         D3D12_FLOAT32_MAX, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    D3D12_INDIRECT_ARGUMENT_DESC arg = {D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED};
    D3D12_COMMAND_SIGNATURE_DESC sd = {20u, 1, &arg, 0};
    D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, 1, 0};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12CommandSignature *signature = NULL;
    ID3D12QueryHeap *statistics = NULL;
    ID3D12Resource *target = NULL, *depth = NULL, *texture = NULL, *gpu_args = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL, *dsv_heap = NULL, *srv_heap = NULL;
    ID3D12Resource *upload = c21_buffer(device, D3D12_HEAP_TYPE_UPLOAD, RC63_UPLOAD_BYTES, D3D12_RESOURCE_FLAG_NONE,
                                        D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *results = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0}, dsv = {0}, srv_cpu = {0};
    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu = {0};
    unsigned char *bytes = NULL;
    HRESULT hr = upload && readback && results && runs && run_count ? S_OK : E_FAIL;
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
    tex_desc = desc;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr) && !bytes) hr = E_FAIL;
    if (SUCCEEDED(hr)) {
        const UINT indices[6] = {0, 1, 2, 2, 1, 3}, count = runs[0].count;
        for (UINT i = 0; i < RC63_PIXELS; ++i) {
            float quad[8];
            rc63_texel(i, bytes + RC63_TEXELS + (i / 64u) * 256u + (i % 64u) * 4u);
            rc63_quad(i, quad);
            memcpy(bytes + RC63_POSITIONS + i * sizeof(quad), quad, sizeof(quad));
        }
        memcpy(bytes + RC63_INDICES, indices, sizeof(indices));
        for (UINT r = 0; r < RC63_MAX; ++r) {
            UINT record[5];
            rc63_uv(r, bytes + RC63_INSTANCES + r * 4u);
            rc63_record(r, record);
            memcpy(bytes + RC63_RECORDS + r * 20u, record, sizeof(record));
        }
        memcpy(bytes + RC63_COUNT, &count, sizeof(count));
        ID3D12Resource_Unmap(upload, 0, NULL);
        printf("%s MaxCount=%u count=%u runs=%u stride=20 index-bytes=4 depth=D32 ps-srv=1 statistics=1 "
               "per-record base-vertex/start-instance=1 upload=%u\n", step, RC63_MAX, count, run_count,
               RC63_UPLOAD_BYTES);
        gpu_args = c21_buffer(device, D3D12_HEAP_TYPE_DEFAULT, RC63_GPU_COUNT + 256u, D3D12_RESOURCE_FLAG_NONE,
                              D3D12_RESOURCE_STATE_COPY_DEST);
        if (!gpu_args) hr = E_FAIL;
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
                                                  (void **)&target);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &depth_desc,
                                                  D3D12_RESOURCE_STATE_DEPTH_WRITE, NULL, &IID_ID3D12Resource,
                                                  (void **)&depth);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &tex_desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
                                                  (void **)&texture);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &dsv_desc, &IID_ID3D12DescriptorHeap, (void **)&dsv_heap);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &srv_desc, &IID_ID3D12DescriptorHeap, (void **)&srv_heap);
    if (SUCCEEDED(hr)) {
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsv_heap, &dsv);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap, &srv_cpu);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap, &srv_gpu);
        ID3D12Device_CreateRenderTargetView(device, target, NULL, rtv);
        ID3D12Device_CreateDepthStencilView(device, depth, NULL, dsv);
        ID3D12Device_CreateShaderResourceView(device, texture, NULL, srv_cpu);
        hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                              (void **)&root);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandSignature(device, &sd, NULL, &IID_ID3D12CommandSignature, (void **)&signature);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateQueryHeap(device, &qd, &IID_ID3D12QueryHeap, (void **)&statistics);
    printf("%s resources/views/root/signature/query hr=0x%08lx\n", step, (unsigned long)hr);
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
        printf("%s CreateGraphicsPipelineState hr=0x%08lx\n", step, (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 &IID_ID3D12CommandAllocator, (void **)&allocator);
    for (UINT run = 0; run < run_count; ++run) {
    struct rc63_run *r = &runs[run];
    r->hr = hr;
    r->bad = RC63_PIXELS;
    r->first = RC63_PIXELS;
    r->draws = 0;
    r->pixels_ok = r->statistics_ok = r->statistics_read = 0;
    if (SUCCEEDED(hr) && run) {
        /* rc89: the next GPU count, through the same upload copy. */
        const UINT count = r->count;
        bytes = NULL;
        hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&bytes);
        if (SUCCEEDED(hr) && !bytes) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            memcpy(bytes + RC63_COUNT, &count, sizeof(count));
            ID3D12Resource_Unmap(upload, 0, NULL);
        }
        if (SUCCEEDED(hr)) hr = ID3D12CommandAllocator_Reset(allocator);
        if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Reset(list, allocator, pso);
    }
    if (SUCCEEDED(hr) && !run)
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        const FLOAT red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        const D3D12_GPU_VIRTUAL_ADDRESS va = ID3D12Resource_GetGPUVirtualAddress(upload);
        D3D12_VIEWPORT viewport = {0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
        D3D12_RECT scissor = {0, 0, 64, 64};
        D3D12_VERTEX_BUFFER_VIEW vbv[2] = {{va + RC63_POSITIONS, RC63_PIXELS * 4u * 8u, 8u},
                                           {va + RC63_INSTANCES, RC63_MAX * 4u, 4u}};
        D3D12_INDEX_BUFFER_VIEW ibv = {va + RC63_INDICES, 24u, DXGI_FORMAT_R32_UINT};
        D3D12_TEXTURE_COPY_LOCATION up = {0}, tex_loc = {0}, target_loc = {0}, back = {0};
        up.pResource = upload;
        up.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        up.PlacedFootprint.Offset = RC63_TEXELS;
        up.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        up.PlacedFootprint.Footprint.Width = 64;
        up.PlacedFootprint.Footprint.Height = 64;
        up.PlacedFootprint.Footprint.Depth = 1;
        up.PlacedFootprint.Footprint.RowPitch = 256;
        tex_loc.pResource = texture;
        tex_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        target_loc.pResource = target;
        target_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        back = up;
        back.pResource = readback;
        back.PlacedFootprint.Offset = 0;
        if (!run) {
            ID3D12GraphicsCommandList_CopyTextureRegion(list, &tex_loc, 0, 0, 0, &up, NULL);
            transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        } else {
            transition(list, gpu_args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
            transition(list, target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        ID3D12GraphicsCommandList_CopyBufferRegion(list, gpu_args, 0, upload, RC63_RECORDS, RC63_MAX * 20u);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, gpu_args, RC63_GPU_COUNT, upload, RC63_COUNT, 4);
        transition(list, gpu_args, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, red, 0, NULL);
        ID3D12GraphicsCommandList_ClearDepthStencilView(list, dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &srv_heap);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list, 0, srv_gpu);
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, &dsv);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 2, vbv);
        ID3D12GraphicsCommandList_IASetIndexBuffer(list, &ibv);
        ID3D12GraphicsCommandList_BeginQuery(list, statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
        ID3D12GraphicsCommandList_ExecuteIndirect(list, signature, RC63_MAX, gpu_args, 0, gpu_args, RC63_GPU_COUNT);
        ID3D12GraphicsCommandList_EndQuery(list, statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
        ID3D12GraphicsCommandList_ResolveQueryData(list, statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1, results, 0);
        transition(list, target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(list, &back, 0, 0, 0, &target_loc, NULL);
        hr = run_list(queue, fence, list, base + 1 + run, step);
    }
    if (SUCCEEDED(hr)) {
        D3D12_RANGE all = {0, 16384};
        bytes = NULL;
        if (SUCCEEDED(ID3D12Resource_Map(readback, 0, &all, (void **)&bytes)) && bytes) {
            const unsigned char *px;
            r->bad = rc63_bad_pixels_n(bytes, &r->first, r->count);
            px = bytes + (r->first % RC63_PIXELS / 64u) * 256u + (r->first % RC63_PIXELS % 64u) * 4u;
            printf("%s run=%u count=%u readback bad=%u first=%u got=%u,%u,%u,%u\n", step, run, r->count, r->bad,
                   r->first, px[0], px[1], px[2], px[3]);
            r->pixels_ok = r->bad == 0;
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    if (SUCCEEDED(hr)) {
        const D3D12_QUERY_DATA_PIPELINE_STATISTICS *q = NULL;
        D3D12_RANGE all = {0, sizeof(*q)};
        if (SUCCEEDED(ID3D12Resource_Map(results, 0, &all, (void **)&q)) && q) {
            r->statistics_ok = rc63_statistics_ok_n(q, r->count);
            r->statistics_read = 1;
            r->draws = q->IAVertices / 6u;
            printf("%s run=%u statistics IA=%llu/%llu VS=%llu clip=%llu/%llu PS=%llu CS=%llu expected-draws=%u\n",
                   step, run, q->IAVertices, q->IAPrimitives, q->VSInvocations, q->CInvocations, q->CPrimitives,
                   q->PSInvocations, q->CSInvocations, r->count);
            ID3D12Resource_Unmap(results, 0, NULL);
        }
    }
    r->hr = hr;
    }
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (statistics) ID3D12QueryHeap_Release(statistics);
    if (signature) ID3D12CommandSignature_Release(signature);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (srv_heap) ID3D12DescriptorHeap_Release(srv_heap);
    if (dsv_heap) ID3D12DescriptorHeap_Release(dsv_heap);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (gpu_args) ID3D12Resource_Release(gpu_args);
    if (texture) ID3D12Resource_Release(texture);
    if (depth) ID3D12Resource_Release(depth);
    if (target) ID3D12Resource_Release(target);
    if (results) ID3D12Resource_Release(results);
    if (readback) ID3D12Resource_Release(readback);
    if (upload) ID3D12Resource_Release(upload);
    return hr;
}
static int rc63_gravitymark_stream(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    struct rc63_run run = {RC63_MAX};
    HRESULT hr = rc63_stream_runs(device, queue, fence, base, "RC63G", &run, 1);
    printf("AGX_D3D12_RC63G: %s\n", SUCCEEDED(hr) && run.pixels_ok ? "PASS" : "FAIL");
    printf("AGX_D3D12_RC63G_STATS: %s\n", SUCCEEDED(hr) && run.statistics_ok ? "PASS" : "FAIL");
    return !(SUCCEEDED(hr) && run.pixels_ok && run.statistics_ok);
}
/* RC89S (rc89 stream capacity): the RC63G shape (MaxCount 60000, GPU-copied
 * count) executed six times on one device. The driver sizes stream outputs
 * by MaxCount until a retired batch reported a GPU count, then by
 * max(16384, the power of two >= 4 x the largest count); a larger count is
 * clamped to the capacity (only `capacity` records drawn, overflow counted)
 * and sizes the later draws. Runs: 150, 150, 150 (warm-up, then outputs of
 * 16384 records), 60000 (overflow: 16384 records with the default policy,
 * all 60000 with APPLE_AGX_D3D12_STREAM_CAPACITY=0 or a run that still saw
 * MaxCount), 60000, 60000 (regrown to MaxCount: exact). Records 0..4095
 * already cover every pixel, so a clamped run keeps its pixels and only the
 * statistics show the 16384 drawn records. */
#define RC89_RUNS 6u
#define RC89_SMALL 150u
#define RC89_CAPACITY_FLOOR 16384u
static int rc89_run_exact(const struct rc63_run *r) {
    return SUCCEEDED(r->hr) && r->pixels_ok && r->statistics_ok;
}
/* 0 FAIL, 1 SIZED (all records drawn), 2 CLAMPED (exactly the floor). */
static int rc89_overflow_kind(const struct rc63_run *r) {
    if (!SUCCEEDED(r->hr) || !r->pixels_ok || !r->statistics_read) return 0;
    if (r->statistics_ok) return 1;
    return r->draws == RC89_CAPACITY_FLOOR ? 2 : 0;
}
static int rc89_stream_capacity(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    struct rc63_run runs[RC89_RUNS] = {{RC89_SMALL}, {RC89_SMALL}, {RC89_SMALL}, {RC63_MAX}, {RC63_MAX}, {RC63_MAX}};
    HRESULT hr = rc63_stream_runs(device, queue, fence, base, "RC89S", runs, RC89_RUNS);
    int small_ok = 1, overflow, regrown_ok;
    for (UINT i = 0; i < RC89_RUNS; ++i)
        printf("AGX_D3D12_RC89S_RUN%u: count=%u draws=%llu bad=%u hr=0x%08lx %s\n", i, runs[i].count,
               (unsigned long long)runs[i].draws, runs[i].bad, (unsigned long)runs[i].hr,
               rc89_run_exact(&runs[i]) ? "EXACT" : "INEXACT");
    for (UINT i = 0; i < 3u; ++i) small_ok &= rc89_run_exact(&runs[i]);
    overflow = rc89_overflow_kind(&runs[3]);
    /* Run 4 may still see run 3's capacity if that batch had not retired;
     * run 5 must be exact. */
    regrown_ok = rc89_overflow_kind(&runs[4]) != 0 && rc89_run_exact(&runs[5]);
    printf("AGX_D3D12_RC89S_SMALL: %s\n", small_ok ? "PASS" : "FAIL");
    printf("AGX_D3D12_RC89S_OVERFLOW: %s\n", overflow == 2 ? "CLAMPED" : overflow == 1 ? "SIZED" : "FAIL");
    printf("AGX_D3D12_RC89S_REGROWN: %s\n", regrown_ok ? "PASS" : "FAIL");
    printf("AGX_D3D12_RC89S: %s\n", SUCCEEDED(hr) && small_ok && overflow && regrown_ok ? "PASS" : "FAIL");
    return !(SUCCEEDED(hr) && small_ok && overflow && regrown_ok);
}
static int step_rc57c(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 base) { return draw_triangle(d, q, f, base, 23); }
static int step_rc57u(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 base) { return draw_triangle(d, q, f, base, 24); }
static int step_rc57m(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 base) { return draw_triangle(d, q, f, base, 25); }
static int step_rc59g(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 base) { return draw_triangle(d, q, f, base, 26); }
/* Q3RC: API submission/output checkpoint. Firmware overlap is established only
 * by Wom1RcProgress, never by a PASS here or by application fence values. */
static HRESULT q3_submit_pair(ID3D12CommandQueue *queue, ID3D12Fence *fence,
    ID3D12GraphicsCommandList *render, ID3D12GraphicsCommandList *compute,
    UINT64 base, int *submitted) {
    HRESULT hr = ID3D12GraphicsCommandList_Close(render);
    UINT64 value;
    ULONGLONG start = GetTickCount64();
    *submitted = 0;
    if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Close(compute);
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *r[] = {(ID3D12CommandList *)render};
        ID3D12CommandList *c[] = {(ID3D12CommandList *)compute};
        *submitted = 1;
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, r);
        hr = ID3D12CommandQueue_Signal(queue, fence, base + 1);
        if (FAILED(hr)) return hr;
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, c);
        hr = ID3D12CommandQueue_Signal(queue, fence, base + 2);
        if (FAILED(hr)) return hr;
        /* No CPU wait, Map, copy or readback between the two Execute calls. */
        do {
            value = ID3D12Fence_GetCompletedValue(fence);
            if (value == ~(UINT64)0) return E_FAIL; /* device removed */
            if (value == base + 2) return S_OK;
            if (GetTickCount64() - start >= 3000) return E_FAIL;
            Sleep(1);
        } while (1);
    }
    return hr;
}
/* Two or three independently recorded lists; close all before exposing any. */
static HRESULT q3_submit_sequence(ID3D12CommandQueue *queue, ID3D12Fence *fence,
    ID3D12GraphicsCommandList **lists, UINT count, UINT64 base, int *submitted) {
    ULONGLONG start = GetTickCount64();
    *submitted = 0;
    if (count < 2 || count > 3) return E_FAIL;
    for (UINT i = 0; i < count; ++i) {
        HRESULT hr = ID3D12GraphicsCommandList_Close(lists[i]);
        if (FAILED(hr)) return hr;
    }
    for (UINT i = 0; i < count; ++i) {
        ID3D12CommandList *list = (ID3D12CommandList *)lists[i];
        HRESULT hr;
        *submitted = 1;
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, &list);
        hr = ID3D12CommandQueue_Signal(queue, fence, base + i + 1);
        if (FAILED(hr)) return hr;
    }
    /* No CPU wait, Map or readback until every measured list is submitted. */
    for (;;) {
        UINT64 value = ID3D12Fence_GetCompletedValue(fence);
        if (value == ~(UINT64)0) return E_FAIL;
        if (value == base + count) return S_OK;
        if (GetTickCount64() - start >= 3000) return E_FAIL;
        Sleep(1);
    }
}
static void q3_fill_vertices(unsigned char *bytes, int blue) {
    const float data[4][6] = {{-1,-1,0,1,0,1},{-1,1,0,1,0,1},
                             {1,-1,0,1,0,1},{1,1,0,1,0,1}};
    memset(bytes, 0, 65536);
    memcpy(bytes, data, sizeof(data));
    if (blue) {
        float *v = (float *)bytes;
        for (UINT i = 0; i < 4; ++i) { v[6*i+3] = 0; v[6*i+4] = 1; }
    }
}
static unsigned q3_bad_outputs(const unsigned char *pixels, const UINT *words) {
    unsigned bad = 0;
    for (unsigned i = 0; i < 64 * 64; ++i) {
        bad += pixels[4*i] != 0;
        bad += pixels[4*i+1] != 255;
        bad += pixels[4*i+2] != 0;
        bad += pixels[4*i+3] != 255;
    }
    for (unsigned i = 0; i < 64; ++i) bad += words[i] != i * 3u + 7u;
    return bad;
}
/* NULL words in R/R: validate both renders without inventing compute work. */
static unsigned q3_bad_sequence_outputs(const unsigned char *green,
    const unsigned char *blue, const UINT *words, int compute, UINT iterations) {
    unsigned bad = 0;
    for (UINT i = 0; i < 64 * 64; ++i) {
        if (green) {
            bad += green[4*i] != 0;
            bad += green[4*i+1] != 255;
            bad += green[4*i+2] != 0;
            bad += green[4*i+3] != 255;
        }
        bad += blue[4*i] != 0;
        bad += blue[4*i+1] != 0;
        bad += blue[4*i+2] != 255;
        bad += blue[4*i+3] != 255;
    }
    if (compute) bad += q3_bad_compute(words, iterations);
    return bad;
}
static DWORD WINAPI q3_watchdog(LPVOID unused) {
    const char *name = (const char *)unused;
    Sleep(15000);
    /* stdout was made unbuffered by main. A blocked D3D call cannot turn this
     * deadline into an unbounded COM or DLL cleanup wait. */
    printf("AGX_D3D12_%s: FAIL process-deadline\n", name);
    TerminateProcess(GetCurrentProcess(), 1);
    return 0;
}
/* Keep one draw and one render batch. More identical fullscreen instances
 * increase raster/fragment work without changing its exactly green output. */
static void q3_bind_render(ID3D12GraphicsCommandList *list,
    ID3D12RootSignature *root, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    ID3D12Resource *vertices) {
    D3D12_VIEWPORT vp = {0,0,64,64,0,1};
    D3D12_RECT scissor = {0,0,64,64};
    D3D12_VERTEX_BUFFER_VIEW vb = {0};
    vb.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vertices);
    vb.SizeInBytes = 4u * 6u * (UINT)sizeof(float);
    vb.StrideInBytes = 6u * (UINT)sizeof(float);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, root);
    ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
    ID3D12GraphicsCommandList_RSSetViewports(list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vb);
}
static void q3_record_render(ID3D12GraphicsCommandList *list,
    ID3D12RootSignature *root, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    ID3D12Resource *vertices, UINT instances) {
    q3_bind_render(list, root, rtv, vertices);
    ID3D12GraphicsCommandList_DrawInstanced(list, 4, instances, 0, 0);
}
/* Two legal DRAW records, count=1. Ignoring the count changes exact statistics.
 * The caller only updates this upload after all previous users have retired. */
static HRESULT q3_set_prelude_arguments(ID3D12Resource *vertices, UINT instances) {
    unsigned char *bytes = NULL;
    const UINT args[] = {4, instances, 0, 0, 4, 1, 0, 0, 1};
    HRESULT hr = ID3D12Resource_Map(vertices, 0, NULL, (void **)&bytes);
    if (FAILED(hr)) return hr;
    if (!bytes) return E_FAIL;
    memcpy(bytes + 2048, args, sizeof(args));
    ID3D12Resource_Unmap(vertices, 0, NULL);
    return S_OK;
}
/* Counted ExecuteIndirect selects GravityMark's GPU-generated VDM path, whose
 * CDM producer is part of this render. Statistics are an output oracle; they
 * are not proof that the indirect stream path or depth-two admission ran. */
static void q3_record_prelude(ID3D12GraphicsCommandList *list,
    ID3D12RootSignature *root, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    ID3D12Resource *vertices, ID3D12CommandSignature *signature, ID3D12QueryHeap *queries, UINT index) {
    ID3D12GraphicsCommandList_BeginQuery(list, queries, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, index);
    q3_bind_render(list, root, rtv, vertices);
    ID3D12GraphicsCommandList_ExecuteIndirect(list, signature, 2, vertices, 2048, vertices, 2080);
    ID3D12GraphicsCommandList_EndQuery(list, queries, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, index);
}
static unsigned q3_bad_statistics(const D3D12_QUERY_DATA_PIPELINE_STATISTICS *s, UINT instances) {
    const UINT64 vertices = (UINT64)instances * 4u, primitives = (UINT64)instances * 2u;
    /* PS invocations are rasterizer-dependent and are not a prelude oracle. */
    return (s->IAVertices != vertices) + (s->IAPrimitives != primitives) +
        (s->VSInvocations != vertices) + (s->CInvocations != primitives) +
        (s->CPrimitives != primitives) + (s->GSInvocations != 0) +
        (s->GSPrimitives != 0) + (s->HSInvocations != 0) +
        (s->DSInvocations != 0) + (s->CSInvocations != 0);
}
static void q3_record_compute(ID3D12GraphicsCommandList *list,
    ID3D12RootSignature *root, ID3D12Resource *uav, UINT iterations) {
    ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 0, ID3D12Resource_GetGPUVirtualAddress(uav));
    if (iterations) ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(list, 1, iterations, 0);
    ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
}
static void q3_record_sequence(ID3D12GraphicsCommandList **lists,
    ID3D12RootSignature **roots, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv2, ID3D12Resource *vertices,
    ID3D12Resource *vertices2, ID3D12Resource *uav, UINT instances, int mode, UINT iterations) {
    if (mode != 3) q3_record_render(lists[0], roots[0], rtv, vertices, instances);
    if (mode != 2) {
        UINT c = mode == 3 ? 0u : 1u;
        transition(lists[c], uav, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        q3_record_compute(lists[c], roots[1], uav, iterations);
    }
    if (mode) q3_record_render(lists[mode == 1 ? 2 : 1], roots[0], rtv2, vertices2, instances);
}
static UINT q3_next_instances(UINT instances, double ms) {
    double scale, next;
    if (!(ms > 0.0)) return instances;
    scale = 7.5 / ms;
    if (scale > 4.0) scale = 4.0;
    if (scale < 0.25) scale = 0.25;
    next = (double)instances * scale;
    if (next < 1.0) return 1;
    if (next > 262144.0) return 262144;
    return (UINT)next;
}
/* Existing read-only, caller-PID-filtered completion escape. No hardware
 * access flag, kernel device/context, new ABI or admission path is needed. */
static HRESULT q3_completions(ID3D12Device *device, UINT64 cursor,
                              Q3_GPU_COMPLETION_QUERY *query) {
    typedef NTSTATUS (APIENTRY *Q3_OPEN)(D3DKMT_OPENADAPTERFROMLUID *);
    typedef NTSTATUS (APIENTRY *Q3_ESCAPE)(const D3DKMT_ESCAPE *);
    typedef NTSTATUS (APIENTRY *Q3_CLOSE)(const D3DKMT_CLOSEADAPTER *);
    HMODULE gdi = LoadLibraryA("gdi32.dll");
    Q3_OPEN open_adapter;
    Q3_ESCAPE escape;
    Q3_CLOSE close_adapter;
    D3DKMT_OPENADAPTERFROMLUID open = {0};
    D3DKMT_ESCAPE request = {0};
    D3DKMT_CLOSEADAPTER close = {0};
    NTSTATUS status;
    if (!gdi) return E_FAIL;
    open_adapter = (Q3_OPEN)(void *)GetProcAddress(gdi, "D3DKMTOpenAdapterFromLuid");
    escape = (Q3_ESCAPE)(void *)GetProcAddress(gdi, "D3DKMTEscape");
    close_adapter = (Q3_CLOSE)(void *)GetProcAddress(gdi, "D3DKMTCloseAdapter");
    if (!open_adapter || !escape || !close_adapter) { FreeLibrary(gdi); return E_FAIL; }
    ID3D12Device_GetAdapterLuid(device, &open.AdapterLuid);
    status = open_adapter(&open);
    if (status < 0) { FreeLibrary(gdi); return E_FAIL; }
    memset(query, 0, sizeof(*query));
    query->Magic = Q3_GPU_TIMESTAMP_QUERY_MAGIC;
    query->Version = Q3_GPU_COMPLETION_VERSION;
    query->Bytes = (UINT)sizeof(*query);
    query->Cursor = cursor;
    request.hAdapter = open.hAdapter;
    request.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    request.pPrivateDriverData = query;
    request.PrivateDriverDataSize = (UINT)sizeof(*query);
    status = escape(&request);
    close.hAdapter = open.hAdapter;
    (void)close_adapter(&close);
    FreeLibrary(gdi);
    return status >= 0 && query->Count <= Q3_GPU_COMPLETION_PAGE ? S_OK : E_FAIL;
}
/* QPCs are CPU KMD enqueue/complete boundaries, not firmware GPU durations.
 * Require exactly the measured R and C, a real prelude, and N+1 submitted
 * before N completes. Typed reason-5/admission receipts remain the final gate. */
static int q3_prelude_pending(const Q3_GPU_COMPLETION_QUERY *q,
    UINT64 cursor, UINT64 missing, UINT pid, UINT64 begin, UINT64 end, int compute_first) {
    const Q3_GPU_COMPLETION *render = NULL, *compute = NULL, *head, *next;
    if (!q->Frequency || q->Lost || q->Reset || q->Count > Q3_GPU_COMPLETION_PAGE ||
        q->Cursor < q->Newest || q->MissingTiming != missing) return 0;
    for (UINT i = 0; i < q->Count; ++i) {
        const Q3_GPU_COMPLETION *r = &q->Record[i];
        if (r->Sequence <= cursor || r->ProcessId != pid ||
            r->SubmitQpc < begin || r->SubmitQpc > end) continue;
        if (r->Status || !r->Fence || r->CompleteQpc <= r->SubmitQpc ||
            r->CompleteQpc > end) return 0;
        if (r->Kind == Q3_GPU_TIMESTAMP_RENDER) {
            if (render) return 0;
            render = r;
        } else if (r->Kind == Q3_GPU_TIMESTAMP_COMPUTE) {
            if (compute) return 0;
            compute = r;
        } else return 0;
    }
    if (!render || !compute || !render->PreludeStart ||
        render->PreludeEnd <= render->PreludeStart || render->Fence == compute->Fence)
        return 0;
    head = compute_first ? compute : render;
    next = compute_first ? render : compute;
    printf("Q3 topology head_fence=%u next_fence=%u head_submit=%llu head_complete=%llu next_submit=%llu next_complete=%llu prelude_start=%llu prelude_end=%llu\n",
        head->Fence, next->Fence, (unsigned long long)head->SubmitQpc,
        (unsigned long long)head->CompleteQpc, (unsigned long long)next->SubmitQpc,
        (unsigned long long)next->CompleteQpc, (unsigned long long)render->PreludeStart,
        (unsigned long long)render->PreludeEnd);
    return next->SubmitQpc > head->SubmitQpc && next->SubmitQpc < head->CompleteQpc;
}
/* API timestamp deltas on this driver include CPU replay/submit latency.
 * Use the completed render's firmware 3D start/end, never its QPC span. */
static int q3_render_duration(const Q3_GPU_COMPLETION_QUERY *q,
    UINT64 cursor, UINT64 missing, UINT pid, UINT64 begin,
    UINT64 previous_end, double *ms) {
    /* EXP2086: other processes' jobs (DWM) and a render's compute prelude
     * complete in the same window, so pick this process's newest retired
     * render after the cursor instead of requiring exactly one record; the
     * 3D span (Start..End) excludes the prelude. Each refusal is printed. */
    const Q3_GPU_COMPLETION *r = NULL;
    unsigned i;
    (void)missing;
    if (!q->Frequency || q->Lost || q->Reset) {
        printf("Q3RC calibration-reject query freq=%llu lost=%llu reset=%u count=%u\n",
            (unsigned long long)q->Frequency, (unsigned long long)q->Lost, q->Reset, q->Count);
        return 0;
    }
    for (i = 0; i < q->Count; ++i) {
        const Q3_GPU_COMPLETION *c = &q->Record[i];
        if (c->Sequence <= cursor || c->ProcessId != pid || !c->Fence ||
            c->Kind != Q3_GPU_TIMESTAMP_RENDER || c->Status || c->SubmitQpc < begin)
            continue;
        if (!r || c->Sequence > r->Sequence) r = c;
    }
    if (!r) {
        printf("Q3RC calibration-reject no-match count=%u pid=%u", q->Count, pid);
        for (i = 0; i < q->Count; ++i)
            printf(" [seq=%llu pid=%u kind=%u status=%u fence=%u]",
                (unsigned long long)q->Record[i].Sequence, q->Record[i].ProcessId,
                q->Record[i].Kind, q->Record[i].Status, q->Record[i].Fence);
        puts("");
        return 0;
    }
    {
        /* EXP2087: the render Start word can belong to another job (objects
         * 28/29 are parity-slotted and may already point at the next build's
         * copy); at depth one this render cannot start before the previous
         * retired job's End, so that bounds Start (duration overestimated by
         * at most one launch gap). */
        UINT64 start = r->Start;
        if (previous_end && (start == 0 || start < previous_end)) {
            printf("Q3RC calibration-bounded start=%llu previous_end=%llu\n",
                (unsigned long long)r->Start, (unsigned long long)previous_end);
            start = previous_end;
        }
        if (r->CompleteQpc < r->SubmitQpc || !start || r->End <= start) {
            printf("Q3RC calibration-reject span start=%llu end=%llu previous_end=%llu\n",
                (unsigned long long)r->Start, (unsigned long long)r->End, (unsigned long long)previous_end);
            return 0;
        }
        *ms = 1000.0 * (double)(r->End-start) / (double)q->Frequency;
    }
    return (int)(r - q->Record) + 1; /* 1-based index of the record used */
}
/* Separate retired R-only warm-up; measured R/C contains no timing query.
 * Only an actually measured instance count is carried into the pair. */
static HRESULT q3_calibrate_render(ID3D12Device *device, ID3D12CommandQueue *queue,
    ID3D12Fence *fence, ID3D12GraphicsCommandList *list,
    ID3D12CommandAllocator *allocator, ID3D12PipelineState *pso,
    ID3D12RootSignature *root, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    ID3D12Resource *vertices, UINT64 base, UINT *instances) {
    Q3_GPU_COMPLETION_QUERY records = {0};
    UINT64 cursor, missing, previous_end = 0;
    int used = 0;
    HRESULT hr = q3_completions(device, 0, &records);
    double ms = 0.0;
    *instances = 1024;
    if (FAILED(hr)) {
        puts("Q3RC r_only_gpu_ms=unavailable source=firmware-completion");
        return hr;
    }
    /* Skip every earlier job, including warm-up. The API below only reads
     * already completed work; no wait or producer is hidden in this query. */
    cursor = records.Newest; missing = records.MissingTiming;
    for (unsigned i = 0; i < records.Count; ++i)
        if (records.Record[i].End > previous_end) previous_end = records.Record[i].End;
    for (unsigned attempt = 0; attempt < 8 && SUCCEEDED(hr); ++attempt) {
        LARGE_INTEGER begin = {0};
        QueryPerformanceCounter(&begin);
        ID3D12GraphicsCommandList_SetPipelineState(list, pso);
        q3_record_render(list, root, rtv, vertices, *instances);
        hr = run_list(queue, fence, list, base + attempt, "Q3RC R-only calibration");
        if (FAILED(hr)) {
            puts("AGX_D3D12_Q3RC: FAIL calibration-submit");
            TerminateProcess(GetCurrentProcess(), 1);
            return hr;
        }
        hr = q3_completions(device, cursor, &records);
        if (SUCCEEDED(hr) && !(used = q3_render_duration(&records, cursor, missing,
            (UINT)GetCurrentProcessId(), (UINT64)begin.QuadPart, previous_end, &ms))) hr = E_FAIL;
        if (FAILED(hr)) {
            puts("Q3RC r_only_gpu_ms=unavailable source=firmware-completion invalid-or-missing-record");
            break;
        }
        cursor = records.Cursor; previous_end = records.Record[used - 1].End;
        hr = ID3D12CommandAllocator_Reset(allocator);
        if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Reset(list, allocator, NULL);
        printf("Q3RC calibration=%u instances=%u r_only_gpu_ms=%.3f source=firmware-3d fence=%u sequence=%llu start=%llu end=%llu tick_hz=%llu\n",
            attempt, *instances, ms, records.Record[used - 1].Fence,
            (unsigned long long)records.Record[used - 1].Sequence,
            (unsigned long long)records.Record[used - 1].Start, (unsigned long long)records.Record[used - 1].End,
            (unsigned long long)records.Frequency);
        if (FAILED(hr) || (ms >= 5.0 && ms <= 10.0) || attempt == 7) break;
        UINT next = q3_next_instances(*instances, ms);
        if (next == *instances) break;
        *instances = next;
    }
    printf("Q3RC render_instances=%u r_only_gpu_ms=%.3f duration=%s target_ms=5-10\n",
        *instances, ms, FAILED(hr) ? "invalid" : ms >= 5.0 && ms <= 10.0 ? "in-target" : "outside-target");
    return hr;
}
/* Compute Start/End are owned by this completion. No render-style previous
 * End substitution: a missing or invalid compute span supplies no calibration. */
static int q3_compute_duration(const Q3_GPU_COMPLETION_QUERY *q,
    UINT64 cursor, UINT pid, UINT64 begin, double *ms) {
    const Q3_GPU_COMPLETION *r = NULL;
    if (!q->Frequency || q->Lost || q->Reset || q->Count > Q3_GPU_COMPLETION_PAGE) return 0;
    for (UINT i=0;i<q->Count;++i) {
        const Q3_GPU_COMPLETION *c=&q->Record[i];
        if (c->Sequence <= cursor || c->ProcessId != pid || !c->Fence ||
            c->Kind != Q3_GPU_TIMESTAMP_COMPUTE || c->Status || c->SubmitQpc < begin) continue;
        if (!r || c->Sequence > r->Sequence) r=c;
    }
    if (!r || !r->Start || r->End <= r->Start || r->CompleteQpc < r->SubmitQpc) return 0;
    *ms=1000.0*(double)(r->End-r->Start)/(double)q->Frequency;
    return (int)(r-q->Record)+1;
}
static HRESULT q3_calibrate_compute(ID3D12Device *device, ID3D12CommandQueue *queue,
    ID3D12Fence *fence, ID3D12GraphicsCommandList *list,
    ID3D12CommandAllocator *allocator, ID3D12PipelineState *pso,
    ID3D12RootSignature *root, ID3D12Resource *uav, UINT64 base,
    const char *name, UINT *iterations) {
    Q3_GPU_COMPLETION_QUERY records={0};
    HRESULT hr;
    UINT64 cursor;
    double ms=0.0;
    *iterations=1024;
    /* Warm this exact shader/PSO and resource before measuring it. */
    ID3D12GraphicsCommandList_SetPipelineState(list,pso);
    q3_record_compute(list,root,uav,*iterations);
    hr=run_list(queue,fence,list,base,name);
    if (FAILED(hr)) { TerminateProcess(GetCurrentProcess(),1); return hr; }
    hr=ID3D12CommandAllocator_Reset(allocator);
    if (SUCCEEDED(hr)) hr=ID3D12GraphicsCommandList_Reset(list,allocator,NULL);
    if (SUCCEEDED(hr)) hr=q3_completions(device,0,&records);
    cursor=records.Newest;
    for (UINT attempt=0;attempt<8 && SUCCEEDED(hr);++attempt) {
        LARGE_INTEGER begin={0};
        int used;
        QueryPerformanceCounter(&begin);
        ID3D12GraphicsCommandList_SetPipelineState(list,pso);
        q3_record_compute(list,root,uav,*iterations);
        hr=run_list(queue,fence,list,base+attempt+1,name);
        if (FAILED(hr)) { TerminateProcess(GetCurrentProcess(),1); return hr; }
        hr=q3_completions(device,cursor,&records);
        used=SUCCEEDED(hr) ? q3_compute_duration(&records,cursor,
            (UINT)GetCurrentProcessId(),(UINT64)begin.QuadPart,&ms):0;
        if (!used) {
            printf("%s c_only_gpu_ms=unavailable source=firmware-compute invalid-or-missing-record\n",name);
            return E_FAIL;
        }
        cursor=records.Cursor;
        printf("%s calibration=%u iterations=%u c_only_gpu_ms=%.3f source=firmware-compute fence=%u sequence=%llu start=%llu end=%llu tick_hz=%llu\n",
            name,attempt,*iterations,ms,records.Record[used-1].Fence,
            (unsigned long long)records.Record[used-1].Sequence,
            (unsigned long long)records.Record[used-1].Start,
            (unsigned long long)records.Record[used-1].End,(unsigned long long)records.Frequency);
        hr=ID3D12CommandAllocator_Reset(allocator);
        if (SUCCEEDED(hr)) hr=ID3D12GraphicsCommandList_Reset(list,allocator,NULL);
        if (FAILED(hr) || (ms>=5.0 && ms<=10.0) || attempt==7) break;
        UINT next=q3_next_instances(*iterations,ms);
        if (next==*iterations) break;
        *iterations=next;
    }
    printf("%s compute_iterations=%u c_only_gpu_ms=%.3f duration=%s target_ms=5-10\n",
        name,*iterations,ms,FAILED(hr)?"invalid":ms>=5.0 && ms<=10.0?"in-target":"outside-target");
    return hr;
}
/* Compiler-owned blob vtables remain live through PSO creation and Release. */
static HRESULT q3_compute_shader(ID3D12Device *device,
    D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, ID3D12PipelineState **pso) {
    HMODULE compiler=LoadLibraryExW(L"d3dcompiler_47.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
    pD3DCompile compile=compiler ? (pD3DCompile)(void *)GetProcAddress(compiler,"D3DCompile"):NULL;
    ID3DBlob *shader=NULL, *errors=NULL;
    HRESULT hr=compile ? compile(q3_compute_hlsl,sizeof(q3_compute_hlsl)-1,"q3-compute",
        NULL,NULL,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&errors):E_FAIL;
    if (SUCCEEDED(hr)) {
        desc->CS.pShaderBytecode=ID3D10Blob_GetBufferPointer(shader);
        desc->CS.BytecodeLength=ID3D10Blob_GetBufferSize(shader);
        hr=ID3D12Device_CreateComputePipelineState(device,desc,&IID_ID3D12PipelineState,(void **)pso);
    }
    if (shader) ID3D10Blob_Release(shader);
    desc->CS.pShaderBytecode=NULL;
    desc->CS.BytecodeLength=0;
    if (errors) { printf("Q3 compute compiler: %s\n",(const char *)ID3D10Blob_GetBufferPointer(errors)); ID3D10Blob_Release(errors); }
    if (compiler) FreeLibrary(compiler);
    return hr;
}
/* Exercise the exact blue successor, then its reuse after retirement. The
 * caller must reset its output to the red sentinel after these depth-one
 * passes. Completion proves safe reuse, not absence of future paging. */
static HRESULT q3_warm_successor(ID3D12CommandQueue *queue, ID3D12Fence *fence,
    ID3D12GraphicsCommandList *list, ID3D12CommandAllocator *allocator,
    ID3D12PipelineState *pso, ID3D12RootSignature *root,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv, ID3D12Resource *vertices, UINT64 base) {
    HRESULT hr = S_OK;
    for (UINT pass = 0; pass < 2 && SUCCEEDED(hr); ++pass) {
        ID3D12GraphicsCommandList_SetPipelineState(list, pso);
        q3_record_render(list, root, rtv, vertices, 1);
        hr = run_list(queue, fence, list, base + pass, "Q3 successor warm-up");
        if (FAILED(hr)) {
            puts("Q3 successor warm-up failed; setup may still be outstanding");
            TerminateProcess(GetCurrentProcess(), 1);
            return hr;
        }
        hr = ID3D12CommandAllocator_Reset(allocator);
        if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Reset(list, allocator, NULL);
    }
    return hr;
}
/* Warm both exact endpoints and query slot serially twice. One extra warmup
 * instance makes a stale query result distinguishable. Caller restores sentinels. */
static HRESULT q3_warm_prelude(ID3D12CommandQueue *queue, ID3D12Fence *fence,
    ID3D12GraphicsCommandList *list, ID3D12CommandAllocator *allocator,
    ID3D12PipelineState **psos, ID3D12RootSignature **roots,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv, ID3D12Resource *vertices,
    ID3D12Resource *uav, ID3D12QueryHeap *queries, ID3D12CommandSignature *signature, UINT instances,
    UINT iterations, int compute_warmed, UINT64 base) {
    HRESULT hr = q3_set_prelude_arguments(vertices, instances + 1u);
    for (UINT pass = 0; pass < 2 && SUCCEEDED(hr); ++pass) {
        ID3D12GraphicsCommandList_SetPipelineState(list, psos[0]);
        q3_record_prelude(list, roots[0], rtv, vertices, signature, queries, 0);
        hr = run_list(queue, fence, list, base + 2*pass, "Q3 prelude warm-up");
        if (FAILED(hr)) { TerminateProcess(GetCurrentProcess(), 1); return hr; }
        hr = ID3D12CommandAllocator_Reset(allocator);
        if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Reset(list, allocator, psos[1]);
        if (FAILED(hr)) return hr;
        if (pass || compute_warmed)
            transition(list, uav, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        q3_record_compute(list, roots[1], uav, iterations);
        hr = run_list(queue, fence, list, base + 2*pass + 1, "Q3 compute warm-up");
        if (FAILED(hr)) { TerminateProcess(GetCurrentProcess(), 1); return hr; }
        hr = ID3D12CommandAllocator_Reset(allocator);
        if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Reset(list, allocator, NULL);
    }
    if (SUCCEEDED(hr)) hr = q3_set_prelude_arguments(vertices, instances);
    return hr;
}
static int q3_sequence(ID3D12Device *device, ID3D12CommandQueue *queue,
                      ID3D12Fence *fence, UINT64 base, int mode) {
    ID3D12Resource *vertices = NULL, *texture = NULL, *uav = NULL, *readback = NULL;
    /* Modes 4/5 use a counted-indirect prelude render in R/C or C/R. */
    const int prelude = mode == 4 || mode == 5;
    if (prelude) mode = mode == 4 ? 0 : 3;
    ID3D12QueryHeap *queries = NULL;
    ID3D12CommandSignature *signature = NULL;
    Q3_GPU_COMPLETION_QUERY topology = {0};
    int pending = 0;
    D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, 1, 0};
    /* mode 0: R/C; mode 1: R/C/R; mode 2: R/R; mode 3: C/R. */
    const char *name = prelude ? (mode == 3 ? "Q3CP" : "Q3PC") : mode == 1 ? "Q3RCR" : mode == 2 ? "Q3RR" : mode == 3 ? "Q3CR" : "Q3RC";
    const UINT count = mode == 1 ? 3u : 2u;
    ID3D12Resource *vertices2 = NULL, *texture2 = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv2 = {0};
    ID3D12RootSignature *roots[2] = {NULL, NULL};
    ID3D12PipelineState *psos[2] = {NULL, NULL};
    ID3D12CommandAllocator *allocators[4] = {NULL, NULL, NULL, NULL};
    ID3D12GraphicsCommandList *lists[4] = {NULL, NULL, NULL, NULL};
    ID3D12DescriptorHeap *rtv_heap = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0};
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gp = {0};
    D3D12_COMPUTE_PIPELINE_STATE_DESC cp = {0};
    D3D12_INPUT_ELEMENT_DESC elements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    unsigned char *bytes = NULL;
    unsigned bad = ~0u;
    int submitted = 0;
    UINT instances = 1024, iterations = 0;
    HRESULT hr = S_OK;
    /* The diagnostic is opt-in, and the watchdog covers setup and teardown too. */
    printf("%s pid=%lu app_fences=%llu,%llu", name, GetCurrentProcessId(),
        (unsigned long long)(base+101), (unsigned long long)(base+102));
    if (count == 3) printf(",%llu", (unsigned long long)(base+103));
    puts(" overlap=UNVERIFIED watchdog_ms=15000");
    fflush(stdout);
    if (prelude) printf("%s required_pair=%s required_barrier=%u prelude=counted-indirect max_count=2 count=1 statistics=oracle warm_passes=2\n",
        name, mode == 3 ? "C/RP" : "RP/C", mode == 3 ? 4u : 3u);
    if (prelude) {
        D3D12_INDIRECT_ARGUMENT_DESC arg = {D3D12_INDIRECT_ARGUMENT_TYPE_DRAW};
        D3D12_COMMAND_SIGNATURE_DESC sd = {16, 1, &arg, 0};
        hr = ID3D12Device_CreateCommandSignature(device, &sd, NULL,
            &IID_ID3D12CommandSignature, (void **)&signature);
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateQueryHeap(device, &qd, &IID_ID3D12QueryHeap, (void **)&queries);
    }
    /* Warm native render and compute queues/manager separately at depth one.
     * These use different resources, so stale warmup outputs cannot pass. */
    if (SUCCEEDED(hr) && draw_triangle(device, queue, fence, base, 0)) hr = E_FAIL;
    if (SUCCEEDED(hr) && compute_dispatch(device, queue, fence)) hr = E_FAIL;
    base += 100;
    if (SUCCEEDED(hr)) {
        vertices = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        uav = uav_buffer(device);
        if (!vertices || !readback || !uav) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(vertices, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr) && bytes) {
        q3_fill_vertices(bytes, 0);
        ID3D12Resource_Unmap(vertices, 0, NULL);
    } else if (SUCCEEDED(hr)) hr = E_FAIL;
    if (SUCCEEDED(hr) && mode) {
        vertices2 = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!vertices2) hr = E_FAIL;
        bytes = NULL;
        if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(vertices2, 0, NULL, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            q3_fill_vertices(bytes, 1);
            ID3D12Resource_Unmap(vertices2, 0, NULL);
        } else if (SUCCEEDED(hr)) hr = E_FAIL;
    }
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = desc.Height = 64;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&texture);
    if (SUCCEEDED(hr) && mode) hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&texture2);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &hd, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    if (SUCCEEDED(hr)) {
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
        ID3D12Device_CreateRenderTargetView(device, texture, NULL, rtv);
        if (mode) {
            rtv2 = rtv;
            rtv2.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            ID3D12Device_CreateRenderTargetView(device, texture2, NULL, rtv2);
        }
    }
    for (unsigned i = 0; i < 2 && SUCCEEDED(hr); ++i) {
        ID3DBlob *blob = NULL, *errors = NULL;
        D3D12_ROOT_PARAMETER parameters[2] = {{D3D12_ROOT_PARAMETER_TYPE_UAV}, {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS}};
        D3D12_ROOT_SIGNATURE_DESC rd = {0};
        parameters[1].Constants.Num32BitValues = 1;
        if (i) { rd.NumParameters = mode == 1 || mode == 3 ? 2u : 1u; rd.pParameters = parameters; }
        else rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
            ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&roots[i]);
        if (blob) ID3D10Blob_Release(blob);
        if (errors) ID3D10Blob_Release(errors);
    }
    gp.pRootSignature = roots[0];
    gp.VS.pShaderBytecode = g_agx_d3d12_probe_vs; gp.VS.BytecodeLength = sizeof(g_agx_d3d12_probe_vs);
    gp.PS.pShaderBytecode = g_agx_d3d12_probe_ps; gp.PS.BytecodeLength = sizeof(g_agx_d3d12_probe_ps);
    gp.BlendState.RenderTarget[0].SrcBlend = gp.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    gp.BlendState.RenderTarget[0].DestBlend = gp.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    gp.BlendState.RenderTarget[0].BlendOp = gp.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    gp.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    gp.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    gp.SampleMask = ~0u; gp.SampleDesc.Count = 1;
    gp.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    gp.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; gp.RasterizerState.DepthClipEnable = TRUE;
    gp.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    gp.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    gp.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    gp.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    gp.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    gp.DepthStencilState.BackFace = gp.DepthStencilState.FrontFace;
    gp.InputLayout.pInputElementDescs = elements; gp.InputLayout.NumElements = 2;
    gp.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gp.NumRenderTargets = 1; gp.RTVFormats[0] = desc.Format;
    cp.pRootSignature = roots[1];
    cp.CS.pShaderBytecode = g_agx_d3d12_probe_cs; cp.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateGraphicsPipelineState(device, &gp, &IID_ID3D12PipelineState, (void **)&psos[0]);
    if (SUCCEEDED(hr)) {
        if (mode == 1 || mode == 3) hr = q3_compute_shader(device, &cp, &psos[1]);
        else hr = ID3D12Device_CreateComputePipelineState(device, &cp, &IID_ID3D12PipelineState, (void **)&psos[1]);
    }
    for (unsigned i = 0; i < 4 && SUCCEEDED(hr); ++i) {
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&allocators[i]);
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocators[i], i == 3 ? NULL : (i == (mode == 3 ? 0u : 1u) && mode != 2 ? psos[1] : psos[0]), &IID_ID3D12GraphicsCommandList, (void **)&lists[i]);
    }
    if (SUCCEEDED(hr) && prelude && mode != 3) {
        /* EXP2104 substituted a previous job's End for a stale render Start:
         * CPU gaps looked like GPU time and shrank R to one instance. Use a
         * bounded workload here; qualify pending via KMD QPCs after the pair. */
        instances = 262144;
        printf("%s render_instances=%u duration=unverified source=fixed-bounded pending=required\n", name, instances);
    }
    else if (SUCCEEDED(hr) && mode == 3) {
        instances = 1; /* C is the calibrated predecessor; R need only finish. */
        printf("%s render_instances=1 r_only_gpu_ms=unavailable source=successor-only\n", name);
    }
    else if (SUCCEEDED(hr) && g_warp) {
        instances = 32768; /* API/output control only; no Apple firmware. */
        printf("%s r_only_gpu_ms=unavailable source=warp-control duration=unverified\n", name);
    } else if (SUCCEEDED(hr)) hr = q3_calibrate_render(device, queue, fence, lists[3],
        allocators[3], psos[0], roots[0], rtv, vertices, base - 32, &instances);
    if (SUCCEEDED(hr) && (mode == 1 || mode == 3)) {
        if (g_warp) {
            iterations = 1024;
            printf("%s c_only_gpu_ms=unavailable source=warp-control duration=unverified\n", name);
        } else hr = q3_calibrate_compute(device, queue, fence, lists[3], allocators[3],
            psos[1], roots[1], uav, base - 16, name, &iterations);
    }
    if (SUCCEEDED(hr) && !prelude && (mode == 1 || mode == 3))
        hr = q3_warm_successor(queue, fence, lists[3], allocators[3],
            psos[0], roots[0], rtv2, vertices2, base - 2);
    if (SUCCEEDED(hr) && prelude)
        hr = q3_warm_prelude(queue, fence, lists[3], allocators[3], psos, roots,
            mode == 3 ? rtv2 : rtv, mode == 3 ? vertices2 : vertices,
            uav, queries, signature, instances, iterations, mode == 3 && !g_warp, base - 6);
    if (SUCCEEDED(hr)) {
        /* Initialize measured resources to wrong values, then retire setup. */
        const FLOAT red[4] = {1,0,0,1};
        ID3D12GraphicsCommandList_ClearRenderTargetView(lists[3], rtv, red, 0, NULL);
        if (mode) ID3D12GraphicsCommandList_ClearRenderTargetView(lists[3], rtv2, red, 0, NULL);
        transition(lists[3], uav, (prelude || ((mode == 1 || mode == 3) && !g_warp)) ?
            D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyBufferRegion(lists[3], uav, 0, vertices, 256, 256);
        transition(lists[3], uav, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (prelude) ID3D12GraphicsCommandList_CopyBufferRegion(lists[3], readback, 49152,
            vertices, 512, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
        hr = run_list(queue, fence, lists[3], base, "Q3RC init");
        if (FAILED(hr)) { printf("AGX_D3D12_%s: FAIL init\n", name); fflush(stdout); TerminateProcess(GetCurrentProcess(), 1); }
        hr = ID3D12CommandAllocator_Reset(allocators[3]);
        if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Reset(lists[3], allocators[3], NULL);
    }
    if (SUCCEEDED(hr) && prelude && !g_warp) hr = q3_completions(device, 0, &topology);
    if (SUCCEEDED(hr)) {
        if (prelude || mode == 1 || mode == 3) {
            /* Internal replay/prepare and kernel admission are not exposed
             * by the D3D12 API. Join the existing UMD/KMD trace, not CPU
             * Execute wall time. Keep this output outside the pair window. */
            printf("%s successor_execute=%u successor_app_fence=%llu warm_passes=2 successor_umd_replay_us=unavailable source=umd-trace\n",
                name, count, (unsigned long long)(base + count));
            puts("Q3 successor timing: within pair_qpc_begin/end and pid, select successor_execute (1-based); "
                "measure-d3d12-execute={lists,commands,replay_us,submit_us,serial}; "
                "measure-d3d12-replay-window={start,replayed,submitted,hz} (lo/hi pairs); "
                "measure-d3d12-wait: draw PagingFence=0x402 PoolMapEscape=0x41c. "
                "KMD admission before predecessor completion and typed Wom1RcProgress are required for overlap.");
        }
        if (prelude) {
            UINT r = mode == 3 ? 1u : 0u, c = 1u - r;
            q3_record_prelude(lists[r], roots[0], mode == 3 ? rtv2 : rtv,
                mode == 3 ? vertices2 : vertices, signature, queries, 0);
            transition(lists[c], uav, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            q3_record_compute(lists[c], roots[1], uav, iterations);
        } else q3_record_sequence(lists, roots, rtv, rtv2, vertices, vertices2, uav, instances, mode, iterations);
        LARGE_INTEGER pair_begin = {0}, pair_end = {0}, qpc_frequency = {0};
        QueryPerformanceFrequency(&qpc_frequency);
        QueryPerformanceCounter(&pair_begin);
        printf("%s pair_qpc_begin=%llu qpc_frequency=%llu\n",
            name, (unsigned long long)pair_begin.QuadPart, (unsigned long long)qpc_frequency.QuadPart);
        if (mode) hr = q3_submit_sequence(queue, fence, lists, count, base, &submitted);
        else hr = q3_submit_pair(queue, fence, lists[0], lists[1], base, &submitted);
        QueryPerformanceCounter(&pair_end);
        printf("%s pair_qpc_end=%llu\n", name, (unsigned long long)pair_end.QuadPart);
        if (FAILED(hr) && submitted) {
            printf("AGX_D3D12_%s: FAIL pair-timeout-or-submit hr=0x%08lx\n", name, (unsigned long)hr);
            fflush(stdout); TerminateProcess(GetCurrentProcess(), 1); /* no Release of outstanding work */
        }
        if (SUCCEEDED(hr) && prelude && !g_warp) {
            UINT64 cursor = topology.Newest, missing = topology.MissingTiming;
            HRESULT th = q3_completions(device, cursor, &topology);
            pending = SUCCEEDED(th) && q3_prelude_pending(&topology, cursor, missing,
                (UINT)GetCurrentProcessId(), (UINT64)pair_begin.QuadPart,
                (UINT64)pair_end.QuadPart, mode == 3);
            printf("%s pending_successor=%s topology=%s\n", name,
                pending ? "yes" : "unproven", pending ? "candidate-receipts-required" : "INCONCLUSIVE");
        }
    }
    if (SUCCEEDED(hr)) {
        D3D12_TEXTURE_COPY_LOCATION src = {0}, dst = {0};
        src.pResource = texture; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = readback; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = desc.Format;
        dst.PlacedFootprint.Footprint.Width = dst.PlacedFootprint.Footprint.Height = 64;
        dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = 256;
        transition(lists[3], texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(lists[3], uav, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(lists[3], &dst, 0, 0, 0, &src, NULL);
        if (mode) {
            src.pResource = texture2;
            dst.PlacedFootprint.Offset = 16384;
            transition(lists[3], texture2, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ID3D12GraphicsCommandList_CopyTextureRegion(lists[3], &dst, 0, 0, 0, &src, NULL);
        }
        if (mode != 2) ID3D12GraphicsCommandList_CopyBufferRegion(lists[3], readback, mode ? 32768 : 16384, uav, 0, 256);
        if (prelude) ID3D12GraphicsCommandList_ResolveQueryData(lists[3], queries,
            D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1, readback, 49152);
        hr = run_list(queue, fence, lists[3], base + count + 1, "Q3 sequence readback");
        if (FAILED(hr)) { printf("AGX_D3D12_%s: FAIL readback\n", name); fflush(stdout); TerminateProcess(GetCurrentProcess(), 1); }
        D3D12_RANGE range = {0, prelude ? 49152 + sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS) : mode ? 33024 : 16640}; bytes = NULL;
        hr = ID3D12Resource_Map(readback, 0, &range, (void **)&bytes);
        if (SUCCEEDED(hr) && bytes) {
            bad = mode ? q3_bad_sequence_outputs(mode == 3 ? NULL : bytes, bytes + 16384,
                (const UINT *)(bytes + 32768), mode != 2, iterations) :
                q3_bad_outputs(bytes, (const UINT *)(bytes + 16384));
            if (prelude) {
                const D3D12_QUERY_DATA_PIPELINE_STATISTICS *stats =
                    (const D3D12_QUERY_DATA_PIPELINE_STATISTICS *)(bytes + 49152);
                bad += q3_bad_statistics(stats, instances);
                printf("%s ia_vertices=%llu expected=%llu ia_primitives=%llu expected=%llu\n",
                    name, (unsigned long long)stats->IAVertices, (unsigned long long)instances*4,
                    (unsigned long long)stats->IAPrimitives, (unsigned long long)instances*2);
            }
            ID3D12Resource_Unmap(readback, 0, NULL);
        }
    }
    if (prelude && SUCCEEDED(hr) && !bad && !pending) {
        printf("AGX_D3D12_%s: INCONCLUSIVE exact_outputs=PASS pending-prelude-pair=unproven\n", name);
        hr = S_FALSE;
    }
    else printf("AGX_D3D12_%s: %s bad=%u overlap=UNVERIFIED receipts=Wom1RcProgress,Wom1RcDecisions,Wom1JobWindow,Wom1JobTiming971\n",
        name, SUCCEEDED(hr) && !bad ? "PASS" : "FAIL", bad);
    fflush(stdout);
    for (unsigned i = 0; i < 4; ++i) {
        if (lists[i]) ID3D12GraphicsCommandList_Release(lists[i]);
        if (allocators[i]) ID3D12CommandAllocator_Release(allocators[i]);
        if (i < 2 && psos[i]) ID3D12PipelineState_Release(psos[i]);
        if (i < 2 && roots[i]) ID3D12RootSignature_Release(roots[i]);
    }
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (texture) ID3D12Resource_Release(texture);
    if (readback) ID3D12Resource_Release(readback);
    if (uav) ID3D12Resource_Release(uav);
    if (vertices) ID3D12Resource_Release(vertices);
    if (queries) ID3D12QueryHeap_Release(queries);
    if (signature) ID3D12CommandSignature_Release(signature);
    if (vertices2) ID3D12Resource_Release(vertices2);
    if (texture2) ID3D12Resource_Release(texture2);
    return !(hr == S_OK && !bad);
}
static int q3_rc_pair(ID3D12Device *device, ID3D12CommandQueue *queue,
    ID3D12Fence *fence, UINT64 base) {
    return q3_sequence(device, queue, fence, base, 0);
}
static int q3_rcr(ID3D12Device *device, ID3D12CommandQueue *queue,
    ID3D12Fence *fence, UINT64 base) {
    return q3_sequence(device, queue, fence, base, 1);
}
static int q3_cr(ID3D12Device *device, ID3D12CommandQueue *queue,
    ID3D12Fence *fence, UINT64 base) {
    return q3_sequence(device, queue, fence, base, 3);
}
static int q3_rr(ID3D12Device *device, ID3D12CommandQueue *queue,
    ID3D12Fence *fence, UINT64 base) {
    return q3_sequence(device, queue, fence, base, 2);
}
static int q3_pc(ID3D12Device *device, ID3D12CommandQueue *queue,
    ID3D12Fence *fence, UINT64 base) {
    return q3_sequence(device, queue, fence, base, 4);
}
static int q3_cp(ID3D12Device *device, ID3D12CommandQueue *queue,
    ID3D12Fence *fence, UINT64 base) {
    return q3_sequence(device, queue, fence, base, 5);
}
/* MEMBW (diagnostic, named step only): GPU memory throughput and dependent-
 * load latency in one compute PSO over a 128 MiB DEFAULT buffer. Seven
 * regions of 16 KiB .. 64 MiB each hold their own random cycle of 64-byte
 * lines (word 0 = next line), so the chase latency per footprint separates
 * caches, TLB reach and DRAM (EXP2143: 64 MiB ~4.9 us). Each measurement is
 * the time of a list with 11 dispatches minus one with 1 dispatch (minimum of
 * three, event wait, no Sleep polling), so submission cost cancels out.
 * Reference: M1 LPDDR4X-4266 peak 68.25 GB/s. Prints MEMBW_STREAM_GBPS and
 * one MEMBW_CHASE_NS line per footprint; PASS = every list completed. */
static HRESULT membw_run(ID3D12CommandQueue *queue, ID3D12Fence *fence, HANDLE event,
                         ID3D12GraphicsCommandList *list, UINT64 signal, double *ms) {
    LARGE_INTEGER t0, t1, f;
    HRESULT hr = ID3D12GraphicsCommandList_Close(list);
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        hr = ID3D12CommandQueue_Signal(queue, fence, signal);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Fence_SetEventOnCompletion(fence, signal, event);
    if (SUCCEEDED(hr) && WaitForSingleObject(event, 10000) != WAIT_OBJECT_0) hr = E_FAIL;
    QueryPerformanceCounter(&t1);
    *ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
    return SUCCEEDED(hr) && ID3D12Fence_GetCompletedValue(fence) >= signal ? S_OK : E_FAIL;
}

/* EXP2238: uav = 1 reads the chased buffer through a UAV (RWByteAddressBuffer u1, device loads) instead
 * of the SRV t0, whose DXBC raw loads are lowered to buffer-texture fetches (dxbc_to_nir mem_load). */
/* EXP2263: uav = 2 binds the same RWByteAddressBuffer u1 through a descriptor table instead of a root UAV, so the
 * compiler emits a uniform base + 32-bit index device_load (Apple's form) instead of a per-lane 64-bit address. */
static int mem_bw_impl(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base, BOOL uav) {
    const BOOL table = uav == 2;
    /* EXP2264: uav = 3 = root UAV + a GPU-busy warm-up (stream passes) at the head of every timed list; the
     * (11 - 1 dispatch) difference cancels the warm-up, so the chase is timed after DVFS had ~90 ms of load. */
    const BOOL hot = uav == 3;
    /* EXP2266: uav = 4 = root UAV with 128x longer chase dispatches (65536 dependent steps, ~10-30 ms each) so
     * the GPU stays busy long enough for DVFS to reach P6 (EXP2265: the 512-step probe ran ~65 % at P1). */
    /* EXP2267: uav = 5 = same long chase, every step a global atomic (InterlockedAdd +0). */
    const BOOL atom = uav == 5;
    const BOOL longchase = uav == 4 || atom;
    const UINT steps_used = atom ? 8192u : longchase ? 65536u : 512u;
    D3D12_DESCRIPTOR_RANGE table_range = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, 0};
    ID3D12DescriptorHeap *table_heap = NULL;
    enum { BYTES = 128u << 20, LINES = BYTES / 64u, STREAM_THREADS = 65536u, CHASE_THREADS = 1024u, CHASE_STEPS = 512u,
           REGIONS = 7, SPREADS = 8, CHASES = 1 + 2 * REGIONS, CASES = CHASES + SPREADS };
    /* cases 1..7: 1024 chasing threads; 8..14: 32 (one SIMD group), so
     * queueing of concurrent misses can be told from the miss cost itself */
    static const UINT region_kib[REGIONS] = {16u, 64u, 256u, 1024u, 4096u, 16384u, 65536u};
    /* EXP2229 page-spread cases (32 threads): a cycle of N 64-byte lines, one per 16 KiB page, so the data
     * (N x 64 B) always fits the caches while the chase needs N TLB entries; latency rising with N = TLB. */
    static const UINT spread_pages[SPREADS] = {1u, 2u, 4u, 8u, 16u, 64u, 256u, 1024u};
    UINT region_first[REGIONS], spread_first[SPREADS];
    D3D12_ROOT_PARAMETER params[3] = {{D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS}, {D3D12_ROOT_PARAMETER_TYPE_SRV},
                                      {D3D12_ROOT_PARAMETER_TYPE_UAV}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {3, params, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3D12Resource *upload = c21_buffer(device, D3D12_HEAP_TYPE_UPLOAD, BYTES, D3D12_RESOURCE_FLAG_NONE,
                                        D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *src = c21_buffer(device, D3D12_HEAP_TYPE_DEFAULT, BYTES,
                                     uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE,
                                     D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *outv = uav_buffer(device);
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    HANDLE event = CreateEventA(NULL, FALSE, FALSE, NULL);
    UINT64 signal = base;
    double best[CASES][2];
    UINT *words = NULL;
    HRESULT hr = upload && src && outv && event ? S_OK : E_FAIL;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = 6;
    for (UINT m = 0; m < CASES; ++m) best[m][0] = best[m][1] = 1e30;
    params[1].Descriptor.ShaderRegister = uav ? 1 : 0;
    if (uav) params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    if (table) {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &table_range;
        if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &hd, &IID_ID3D12DescriptorHeap, (void **)&table_heap);
        if (SUCCEEDED(hr)) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {0};
            D3D12_CPU_DESCRIPTOR_HANDLE cpu;
            ud.Format = DXGI_FORMAT_R32_TYPELESS;
            ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = BYTES / 4u;
            ud.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(table_heap, &cpu);
            ID3D12Device_CreateUnorderedAccessView(device, src, NULL, &ud, cpu);
        }
    }
    params[2].Descriptor.ShaderRegister = 0;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(upload, 0, NULL, (void **)&words);
    if (SUCCEEDED(hr) && words) {
        /* Sattolo: one cycle per region, absolute line indices */
        UINT *next = (UINT *)malloc(LINES * sizeof(UINT));
        UINT seed = 2463534242u, first = 0u;
        if (!next) hr = E_OUTOFMEMORY;
        for (UINT r = 0; next && r < REGIONS; ++r) {
            const UINT count = region_kib[r] * 1024u / 64u;
            region_first[r] = first;
            for (UINT i = 0; i < count; ++i) next[first + i] = first + i;
            for (UINT i = count - 1u; i > 0u; --i) {
                UINT j;
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                j = seed % i;
                { UINT v = next[first + i]; next[first + i] = next[first + j]; next[first + j] = v; }
            }
            first += count;
        }
        for (UINT i = first; next && i < LINES; ++i) next[i] = i;
        {   /* page-spread cycles in the tail: line = base + page * 256 + s (disjoint per case) */
            const UINT base = (first + 255u) & ~255u;
            for (UINT sp = 0; next && sp < SPREADS; ++sp) {
                const UINT n = spread_pages[sp];
                UINT *order = (UINT *)malloc(n * sizeof(UINT));
                if (!order) { hr = E_OUTOFMEMORY; break; }
                for (UINT i = 0; i < n; ++i) order[i] = i;
                for (UINT i = n - 1u; i > 0u; --i) {
                    UINT j, v;
                    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                    j = seed % i; v = order[i]; order[i] = order[j]; order[j] = v;
                }
                /* Sattolo permutation -> one cycle: page order[i] points to page order[i+1] */
                /* EXP2229 fix: vary the in-page line ((page * 17 + case) mod 256) so the lines fall into
                 * different cache sets; a fixed offset aliased every line to one set */
#define SPREAD_LINE(pg) (base + (pg) * 256u + (((pg) * 17u + sp) & 255u))
                for (UINT i = 0; i < n; ++i) next[SPREAD_LINE(order[i])] = SPREAD_LINE(order[(i + 1u) % n]);
                spread_first[sp] = SPREAD_LINE(order[0]);
#undef SPREAD_LINE
                free(order);
            }
        }
        for (UINT i = 0; next && i < LINES; ++i) {
            words[i * 16u] = next[i];
            for (UINT k = 1; k < 16u; ++k) words[i * 16u + k] = i ^ (k * 0x9e3779b9u);
        }
        free(next);
        ID3D12Resource_Unmap(upload, 0, NULL);
    }
    if (SUCCEEDED(hr)) hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                              &IID_ID3D12RootSignature, (void **)&root);
    if (SUCCEEDED(hr)) {
        pso_desc.pRootSignature = root;
        pso_desc.CS.pShaderBytecode = atom ? (const void *)g_agx_d3d12_probe_cs_membw_atom
                                    : uav ? (const void *)g_agx_d3d12_probe_cs_membw_uav : (const void *)g_agx_d3d12_probe_cs_membw;
        pso_desc.CS.BytecodeLength = atom ? sizeof(g_agx_d3d12_probe_cs_membw_atom)
                                   : uav ? sizeof(g_agx_d3d12_probe_cs_membw_uav) : sizeof(g_agx_d3d12_probe_cs_membw);
        hr = ID3D12Device_CreateComputePipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                                                 (void **)&allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) {
        double ms = 0.0;
        ID3D12GraphicsCommandList_CopyBufferRegion(list, src, 0, upload, 0, BYTES);
        transition(list, src, D3D12_RESOURCE_STATE_COPY_DEST,
                   uav ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        hr = membw_run(queue, fence, event, list, ++signal, &ms);
        printf("MEMBW upload copy %.3f ms hr=0x%08lx\n", ms, (unsigned long)hr);
    }
    for (UINT rep = 0; rep < 3u && SUCCEEDED(hr); ++rep) {
        for (UINT m = 0; m < CASES && SUCCEEDED(hr); ++m) {
            const UINT mode = m ? 1u : 0u;
            const UINT region = m && m < CHASES ? (m - 1u) % REGIONS : 0u;
            const BOOL spread = m >= CHASES;
            for (UINT k = 0; k < 2u && SUCCEEDED(hr); ++k) {
                const UINT dispatches = k ? 11u : 1u;
                const UINT threads = !mode ? STREAM_THREADS : m > REGIONS ? 32u : CHASE_THREADS;
                /* spread: every thread starts on the cycle's first line (lines = 1 keeps the start a member) */
                const UINT constants[6] = {BYTES / 16u, mode, mode ? steps_used : CHASE_STEPS, threads,
                                           spread ? spread_first[m - CHASES] : m ? region_first[region] : 0u,
                                           spread ? 1u : m ? region_kib[region] * 1024u / 64u : 1u};
                double ms = 0.0;
                hr = ID3D12CommandAllocator_Reset(allocator);
                if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Reset(list, allocator, pso);
                if (FAILED(hr)) break;
                ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
                ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list, 0, 6, constants, 0);
                if (table) {
                    D3D12_GPU_DESCRIPTOR_HANDLE gh;
                    ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &table_heap);
                    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(table_heap, &gh);
                    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 1, gh);
                } else if (uav) ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 1, ID3D12Resource_GetGPUVirtualAddress(src));
                else ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(list, 1, ID3D12Resource_GetGPUVirtualAddress(src));
                ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 2, ID3D12Resource_GetGPUVirtualAddress(outv));
                if (hot) {
                    const UINT warm[6] = {BYTES / 16u, 0u, CHASE_STEPS, STREAM_THREADS, 0u, 1u};
                    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list, 0, 6, warm, 0);
                    for (UINT w = 0; w < 40u; ++w) ID3D12GraphicsCommandList_Dispatch(list, (STREAM_THREADS + 255u) / 256u, 1, 1);
                    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list, 0, 6, constants, 0);
                }
                for (UINT d = 0; d < dispatches; ++d) ID3D12GraphicsCommandList_Dispatch(list, (threads + 255u) / 256u, 1, 1);
                hr = membw_run(queue, fence, event, list, ++signal, &ms);
                if (ms < best[m][k]) best[m][k] = ms;
                printf("MEMBW rep %u case %u dispatches %u %.3f ms hr=0x%08lx\n", rep, m, dispatches, ms, (unsigned long)hr);
            }
        }
    }
    if (SUCCEEDED(hr)) {
        const double stream_ms = (best[0][1] - best[0][0]) / 10.0;
        printf("MEMBW_STREAM_GBPS %.2f (%.3f ms per 128 MiB pass)\n",
               stream_ms > 0.0 ? (double)BYTES / (stream_ms * 1e6) : 0.0, stream_ms);
        for (UINT m = 1; m < CHASES; ++m) {
            const double chase_ms = (best[m][1] - best[m][0]) / 10.0;
            printf("MEMBW_CHASE_NS %u KiB %.1f (%u threads x %u dependent loads, %.3f ms per dispatch)\n",
                   region_kib[(m - 1u) % REGIONS], chase_ms > 0.0 ? chase_ms * 1e6 / steps_used : 0.0,
                   m > REGIONS ? 32u : (UINT)CHASE_THREADS, steps_used, chase_ms);
        }
        for (UINT m = CHASES; m < CASES; ++m) {
            const double chase_ms = (best[m][1] - best[m][0]) / 10.0;
            printf("MEMBW_PAGES_NS %u pages %.1f (32 threads x %u dependent loads, one 64 B line per 16 KiB page)\n",
                   spread_pages[m - CHASES], chase_ms > 0.0 ? chase_ms * 1e6 / steps_used : 0.0, steps_used);
        }
    }
    printf("AGX_D3D12_MEMBW%s: %s\n", atom ? "ATOM" : longchase ? "LONG" : hot ? "HOT" : table ? "TBL" : uav ? "UAV" : "", SUCCEEDED(hr) ? "PASS" : "FAIL");
    if (table_heap) ID3D12DescriptorHeap_Release(table_heap);
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (allocator) ID3D12CommandAllocator_Release(allocator);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (errors) ID3D10Blob_Release(errors);
    if (blob) ID3D10Blob_Release(blob);
    if (outv) ID3D12Resource_Release(outv);
    if (src) ID3D12Resource_Release(src);
    if (upload) ID3D12Resource_Release(upload);
    if (event) CloseHandle(event);
    return !SUCCEEDED(hr);
}
static int mem_bw(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return mem_bw_impl(d, q, f, b, FALSE); }
static int mem_bw_uav(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return mem_bw_impl(d, q, f, b, TRUE); }
static int mem_bw_tbl(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return mem_bw_impl(d, q, f, b, 2); }
static int mem_bw_hot(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return mem_bw_impl(d, q, f, b, 3); }
static int mem_bw_long(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return mem_bw_impl(d, q, f, b, 4); }
static int mem_bw_atom(ID3D12Device *d, ID3D12CommandQueue *q, ID3D12Fence *f, UINT64 b) { return mem_bw_impl(d, q, f, b, 5); }

/* OVPAIR (named step, bounded reproducer of the EXP2214 hang): a long compute
 * job (stream passes over a private 128 MiB buffer, ~15-20 ms) immediately
 * followed, in its own ExecuteCommandLists, by a render that touches none of
 * the compute's resources (clear + one triangle into a 64x64 target, copied to
 * a readback). With Wom1Ov2NoHazardLead=1 + Wom1Ov5TaWaiver=1 the UMD flags the
 * render TA-independent and the KMD waives its TA wait on the compute stamp,
 * so the TA runs while the compute runs (Apple's c15||R16). Without the
 * switches the same pair runs serialized (control). PASS = every pair
 * completes within 8 s with the expected pixels; a timeout names the
 * iteration (the KMD's TDR would follow). */
static int ov_pair(ID3D12Device *device, ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 base) {
    enum { BYTES = 128u << 20, STREAM_THREADS = 65536u, CHASE_STEPS = 512u, PASSES = 8u, ITERATIONS = 40u };
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    D3D12_INPUT_ELEMENT_DESC elements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_ROOT_SIGNATURE_DESC graphics_root_desc = {0, NULL, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    D3D12_ROOT_PARAMETER params[3] = {{D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS}, {D3D12_ROOT_PARAMETER_TYPE_UAV},
                                      {D3D12_ROOT_PARAMETER_TYPE_UAV}};
    D3D12_ROOT_SIGNATURE_DESC compute_root_desc = {3, params, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
    D3D12_COMPUTE_PIPELINE_STATE_DESC cs_desc = {0};
    ID3DBlob *blob = NULL, *errors = NULL;
    ID3D12RootSignature *root = NULL, *compute_root = NULL;
    ID3D12PipelineState *pso = NULL, *cs = NULL;
    ID3D12Resource *target = NULL;
    ID3D12DescriptorHeap *rtv_heap = NULL;
    ID3D12Resource *vertices = committed_buffer(device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = committed_buffer(device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *src = c21_buffer(device, D3D12_HEAP_TYPE_DEFAULT, BYTES, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12Resource *outv = uav_buffer(device);
    ID3D12CommandAllocator *compute_allocator = NULL, *render_allocator = NULL;
    ID3D12GraphicsCommandList *compute_list = NULL, *render_list = NULL;
    HANDLE event = CreateEventA(NULL, FALSE, FALSE, NULL);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {0};
    unsigned char *bytes = NULL;
    UINT64 signal = base;
    UINT done = 0, bad_pixels = 0;
    double total_ms = 0.0, max_ms = 0.0;
    LARGE_INTEGER freq;
    HRESULT hr = vertices && readback && src && outv && event ? S_OK : E_FAIL;
    QueryPerformanceFrequency(&freq);
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = 6;
    params[1].Descriptor.ShaderRegister = 1; /* RWByteAddressBuffer u1: the streamed buffer */
    params[2].Descriptor.ShaderRegister = 0; /* u0: per-thread results */
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(vertices, 0, NULL, (void **)&bytes);
    if (SUCCEEDED(hr)) {
        const float data[3][7] = {{-0.5f, -0.5f, 0.25f, 0, 1, 0, 1}, {0.0f, 0.5f, 0.25f, 0, 1, 0, 1},
                                  {0.5f, -0.5f, 0.25f, 0, 1, 0, 1}};
        memcpy(bytes, data, sizeof(data));
        ID3D12Resource_Unmap(vertices, 0, NULL);
        hr = ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource,
                                                  (void **)&target);
    }
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_desc, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    if (SUCCEEDED(hr)) {
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap, &rtv);
        ID3D12Device_CreateRenderTargetView(device, target, NULL, rtv);
        hr = D3D12SerializeRootSignature(&graphics_root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                              &IID_ID3D12RootSignature, (void **)&root);
    if (blob) { ID3D10Blob_Release(blob); blob = NULL; }
    if (SUCCEEDED(hr)) hr = D3D12SerializeRootSignature(&compute_root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                                              &IID_ID3D12RootSignature, (void **)&compute_root);
    printf("OVPAIR resources/roots hr=0x%08lx\n", (unsigned long)hr);
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
        pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso_desc.SampleMask = 0xffffffffu;
        pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso_desc.RasterizerState.DepthClipEnable = TRUE;
        pso_desc.InputLayout.pInputElementDescs = elements;
        pso_desc.InputLayout.NumElements = 2;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso_desc.SampleDesc.Count = 1;
        hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        printf("OVPAIR CreateGraphicsPipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr)) {
        cs_desc.pRootSignature = compute_root;
        cs_desc.CS.pShaderBytecode = g_agx_d3d12_probe_cs_membw_uav;
        cs_desc.CS.BytecodeLength = sizeof(g_agx_d3d12_probe_cs_membw_uav);
        hr = ID3D12Device_CreateComputePipelineState(device, &cs_desc, &IID_ID3D12PipelineState, (void **)&cs);
        printf("OVPAIR CreateComputePipelineState hr=0x%08lx\n", (unsigned long)hr);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                                                 (void **)&compute_allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                                                 (void **)&render_allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, compute_allocator, cs,
                                            &IID_ID3D12GraphicsCommandList, (void **)&compute_list);
    if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Close(compute_list);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, render_allocator, pso,
                                            &IID_ID3D12GraphicsCommandList, (void **)&render_list);
    if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Close(render_list);
    for (UINT i = 0; SUCCEEDED(hr) && i < ITERATIONS; ++i) {
        const UINT constants[6] = {BYTES / 16u, 0u, CHASE_STEPS, STREAM_THREADS, 0u, 1u};
        const FLOAT red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        D3D12_VIEWPORT viewport = {0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
        D3D12_RECT scissor = {0, 0, 64, 64};
        D3D12_VERTEX_BUFFER_VIEW vbv = {0};
        D3D12_TEXTURE_COPY_LOCATION target_loc = {0}, back = {0};
        ID3D12CommandList *lists[2];
        LARGE_INTEGER t0, t1;
        double ms;
        UINT64 value = 0;
        vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vertices);
        vbv.SizeInBytes = sizeof(float) * 21;
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
        /* the compute job: PASSES stream passes over the private buffer */
        hr = ID3D12CommandAllocator_Reset(compute_allocator);
        if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Reset(compute_list, compute_allocator, cs);
        if (FAILED(hr)) break;
        ID3D12GraphicsCommandList_SetComputeRootSignature(compute_list, compute_root);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(compute_list, 0, 6, constants, 0);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(compute_list, 1, ID3D12Resource_GetGPUVirtualAddress(src));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(compute_list, 2, ID3D12Resource_GetGPUVirtualAddress(outv));
        for (UINT pass = 0; pass < PASSES; ++pass)
            ID3D12GraphicsCommandList_Dispatch(compute_list, (STREAM_THREADS + 255u) / 256u, 1, 1);
        hr = ID3D12GraphicsCommandList_Close(compute_list);
        if (FAILED(hr)) break;
        /* the render job: clear + triangle + copy out, none of the compute's resources */
        hr = ID3D12CommandAllocator_Reset(render_allocator);
        if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Reset(render_list, render_allocator, pso);
        if (FAILED(hr)) break;
        ID3D12GraphicsCommandList_ClearRenderTargetView(render_list, rtv, red, 0, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(render_list, root);
        ID3D12GraphicsCommandList_OMSetRenderTargets(render_list, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(render_list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(render_list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetVertexBuffers(render_list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(render_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_DrawInstanced(render_list, 3, 1, 0, 0);
        transition(render_list, target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(render_list, &back, 0, 0, 0, &target_loc, NULL);
        transition(render_list, target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        hr = ID3D12GraphicsCommandList_Close(render_list);
        if (FAILED(hr)) break;
        QueryPerformanceCounter(&t0);
        /* EXP2276: two ExecuteCommandLists calls are two UMD executions and the
         * OV5 verdict never pairs across executions (renders 0, Waived 0); one
         * call with both lists is GravityMark's shape (one list set per frame)
         * and lets the render follow the compute batch inside one execution. */
        lists[0] = (ID3D12CommandList *)compute_list;
        lists[1] = (ID3D12CommandList *)render_list;
        ID3D12CommandQueue_ExecuteCommandLists(queue, 2, lists);
        hr = ID3D12CommandQueue_Signal(queue, fence, ++signal);
        if (SUCCEEDED(hr)) hr = ID3D12Fence_SetEventOnCompletion(fence, signal, event);
        if (SUCCEEDED(hr) && WaitForSingleObject(event, 8000) != WAIT_OBJECT_0) {
            printf("OVPAIR iteration %u TIMEOUT (8 s) removed=0x%08lx\n", i,
                   (unsigned long)ID3D12Device_GetDeviceRemovedReason(device));
            hr = E_FAIL;
            break;
        }
        value = ID3D12Fence_GetCompletedValue(fence);
        QueryPerformanceCounter(&t1);
        ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
        total_ms += ms;
        if (ms > max_ms) max_ms = ms;
        if (SUCCEEDED(hr) && value >= signal) {
            D3D12_RANGE all = {0, 16384};
            unsigned char *pixels = NULL;
            if (SUCCEEDED(ID3D12Resource_Map(readback, 0, &all, (void **)&pixels)) && pixels) {
                const unsigned char *centre = pixels + 32 * 256 + 32 * 4, *corner = pixels + 4 * 256 + 4 * 4;
                const int ok = centre[0] == 0 && centre[1] == 255 && centre[2] == 0 && corner[0] == 255 && corner[1] == 0;
                if (!ok) ++bad_pixels;
                if (i < 2 || !ok)
                    printf("OVPAIR iteration %u %.3f ms centre %u,%u,%u corner %u,%u,%u\n", i, ms, centre[0], centre[1],
                           centre[2], corner[0], corner[1], corner[2]);
                ID3D12Resource_Unmap(readback, 0, NULL);
            } else ++bad_pixels;
            ++done;
        } else {
            printf("OVPAIR iteration %u fence %llu < %llu hr=0x%08lx\n", i, (unsigned long long)value,
                   (unsigned long long)signal, (unsigned long)hr);
            hr = E_FAIL;
        }
    }
    printf("OVPAIR_RESULT iterations %u/%u bad_pixels %u mean %.3f ms max %.3f ms hr=0x%08lx\n", done, (unsigned)ITERATIONS,
           bad_pixels, done ? total_ms / done : 0.0, max_ms, (unsigned long)hr);
    printf("AGX_D3D12_OVPAIR: %s\n", SUCCEEDED(hr) && done == ITERATIONS && !bad_pixels ? "PASS" : "FAIL");
    if (render_list) ID3D12GraphicsCommandList_Release(render_list);
    if (compute_list) ID3D12GraphicsCommandList_Release(compute_list);
    if (render_allocator) ID3D12CommandAllocator_Release(render_allocator);
    if (compute_allocator) ID3D12CommandAllocator_Release(compute_allocator);
    if (cs) ID3D12PipelineState_Release(cs);
    if (pso) ID3D12PipelineState_Release(pso);
    if (compute_root) ID3D12RootSignature_Release(compute_root);
    if (root) ID3D12RootSignature_Release(root);
    if (blob) ID3D10Blob_Release(blob);
    if (errors) ID3D10Blob_Release(errors);
    if (rtv_heap) ID3D12DescriptorHeap_Release(rtv_heap);
    if (target) ID3D12Resource_Release(target);
    if (outv) ID3D12Resource_Release(outv);
    if (src) ID3D12Resource_Release(src);
    if (readback) ID3D12Resource_Release(readback);
    if (vertices) ID3D12Resource_Release(vertices);
    if (event) CloseHandle(event);
    return !(SUCCEEDED(hr) && done == ITERATIONS && !bad_pixels);
}

static const struct { const char *name; probe_step step; } named_steps[] = {
    {"OVPAIR", ov_pair},
    {"MEMBW", mem_bw},
    {"MEMBWUAV", mem_bw_uav},
    {"MEMBWTBL", mem_bw_tbl},
    {"MEMBWHOT", mem_bw_hot},
    {"MEMBWLONG", mem_bw_long},
    {"MEMBWATOM", mem_bw_atom},
    {"Q3PC", q3_pc}, {"Q3CP", q3_cp},
    {"Q3CR", q3_cr}, {"Q3RC", q3_rc_pair}, {"Q3RCR", q3_rcr}, {"Q3RR", q3_rr},
    {"RC57C", step_rc57c}, {"RC57U", step_rc57u}, {"RC57M", step_rc57m}, {"RC59G", step_rc59g}, {"RC63G", rc63_gravitymark_stream}, {"RC89S", rc89_stream_capacity},
    {"RC44D", step_rc44d}, {"RC44I", step_rc44i},
    {"RC38Z", step_rc38z}, {"RC38N", step_rc38n}, {"RC38I", step_rc38i},
    {"RC43I16Z", step_rc43i16z},
    {"RC43I16O", step_rc43i16o},
    {"RC43I16N", step_rc43i16n},
    {"RC43I32Z", step_rc43i32z},
    {"RC43I32O", step_rc43i32o},
    {"RC43I32N", step_rc43i32n},
    {"C76A", c76_shared_open}, {"RC77D", step_rc77d}, {"RC82A", step_rc82a},
    {"C29A", c29_bc1}, {"C30A", step_c30a}, {"C31A", step_c31a}, {"C32A", step_c32a},
    {"C33A", c33_append}, {"C34A", c34_ranges}, {"C35A", step_c35a}, {"C36A", c36_placed_textures},
    {"C37A", step_c37a}, {"C37B", step_c37b}, {"C37C", step_c37c}, {"C37D", step_c37d}, {"C37E", step_c37e},
    {"C38A", c38_cached_pso}, {"C39A", c39_stream_output},
    {"C41A", step_c41a}, {"C41B", step_c41b},
    {"C42A", step_c42a}, {"C42B", step_c42b}, {"C42C", step_c42c}, {"C42D", step_c42d}, {"C42E", step_c42e}, {"C42F", step_c42f},
    {"C42G", step_c42g}, {"C42H", step_c42h}, {"C42J", step_c42j}, {"C42K", step_c42k}, {"C42L", step_c42l},
    {"C42M", step_c42m}, {"C42N", step_c42n}, {"C47B", c47_binding_tier2},
    {"C44A", step_c44a}, {"C44B", step_c44b}, {"C48A", step_c48a}, {"C49A", c49_typed_uav_loads}, {"C50A", step_c50a}, {"C51A", step_c51a}, {"C52A", step_c52a}, {"C53A", step_c53a}, {"C57A", step_c57a}, {"C58A", step_c58a}, {"C59A", step_c59a}, {"C59B", step_c59b}, {"C60A", step_c60a}, {"C61A", step_c61a}, {"C61B", step_c61b}, {"C61C", step_c61c}, {"C61D", step_c61d}, {"C62A", step_c62a}};
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0); /* EXP1368: a crash lost every buffered line */
    SetUnhandledExceptionFilter(crash_dump);
    AddVectoredExceptionHandler(1, first_access_violation);
    if (argc > 1 && !strcmp(argv[1], "--c76-child")) return c76_child_main(argc, argv);
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    ID3D12Device *device = NULL;
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
    if (FAILED(hr)) { printf("CreateDXGIFactory1 hr=0x%08lx\n", (unsigned long)hr); return 1; }
    for (int a = 1; a < argc; ++a) {
        if (strcmp(argv[a], "warp")) continue;
        ID3D12Debug *debug = NULL;
        IDXGIFactory4 *factory4 = NULL;
        if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug, (void **)&debug))) ID3D12Debug_EnableDebugLayer(debug);
        if (FAILED(IDXGIFactory1_QueryInterface(factory, &IID_IDXGIFactory4, (void **)&factory4)) ||
            FAILED(IDXGIFactory4_EnumWarpAdapter(factory4, &IID_IDXGIAdapter1, (void **)&adapter))) { puts("WARP unavailable"); return 1; }
        g_warp = 1;
        for (int b = a + 1; b < argc; ++b)
            for (unsigned i = 0; i < sizeof(named_steps) / sizeof(named_steps[0]); ++i)
                if (!strcmp(argv[b], named_steps[i].name)) (void)run_isolated(adapter, named_steps[i].name, named_steps[i].step);
        return 0;
    }
    for (UINT i = 0; IDXGIFactory1_EnumAdapters1(factory, i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        hr = IDXGIAdapter1_GetDesc1(adapter, &desc);
        if (SUCCEEDED(hr) && desc.VendorId == 0x4c505041) {
            int failed;
            /* toolrun.ps1 passes its output file first: find "teardown <mode>" */
            for (int a = 1; a + 1 < argc; ++a)
                if (!strcmp(argv[a], "teardown")) return teardown(adapter, argv[a + 1]);
            for (int a = 1; a < argc; ++a) {
                if (strcmp(argv[a], "only")) continue;
                for (int b = a + 1; b < argc; ++b)
                    for (unsigned i = 0; i < sizeof(named_steps) / sizeof(named_steps[0]); ++i)
                        if (!strcmp(argv[b], named_steps[i].name))
                            (void)run_isolated(adapter, named_steps[i].name, named_steps[i].step);
                return 0;
            }
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
                (void)run_isolated(adapter, "C28A", step_c28a);
                (void)run_isolated(adapter, "C28B", step_c28b);
                (void)run_isolated(adapter, "C28C", step_c28c);
                (void)run_isolated(adapter, "C29A", c29_bc1);
                (void)run_isolated(adapter, "C30A", step_c30a);
                (void)run_isolated(adapter, "C31A", step_c31a);
                (void)run_isolated(adapter, "C32A", step_c32a);
                (void)run_isolated(adapter, "C33A", c33_append);
                (void)run_isolated(adapter, "C34A", c34_ranges);
                (void)run_isolated(adapter, "C35A", step_c35a);
                (void)run_isolated(adapter, "C36A", c36_placed_textures);
                (void)run_isolated(adapter, "C27A", step_c27a);
                (void)run_isolated(adapter, "C27B", step_c27b);
                (void)run_isolated(adapter, "C37A", step_c37a);
                (void)run_isolated(adapter, "C37B", step_c37b);
                (void)run_isolated(adapter, "C37C", step_c37c);
                (void)run_isolated(adapter, "C37D", step_c37d);
                (void)run_isolated(adapter, "C37E", step_c37e);
                (void)run_isolated(adapter, "C38A", c38_cached_pso);
                (void)run_isolated(adapter, "C39A", c39_stream_output);
                (void)run_isolated(adapter, "C41A", step_c41a);
                (void)run_isolated(adapter, "C41B", step_c41b);
                (void)run_isolated(adapter, "C42A", step_c42a);
                (void)run_isolated(adapter, "C42B", step_c42b);
                (void)run_isolated(adapter, "C42C", step_c42c);
                (void)run_isolated(adapter, "C42D", step_c42d);
                (void)run_isolated(adapter, "C42E", step_c42e);
                (void)run_isolated(adapter, "C42F", step_c42f);
                (void)run_isolated(adapter, "C42G", step_c42g);
                (void)run_isolated(adapter, "C42H", step_c42h);
                (void)run_isolated(adapter, "C42J", step_c42j);
                (void)run_isolated(adapter, "C42K", step_c42k);
                (void)run_isolated(adapter, "C42L", step_c42l);
                (void)run_isolated(adapter, "C42M", step_c42m);
                (void)run_isolated(adapter, "C42N", step_c42n);
                (void)run_isolated(adapter, "C47B", c47_binding_tier2);
                (void)run_isolated(adapter, "C44A", step_c44a);
                (void)run_isolated(adapter, "C44B", step_c44b);
                (void)run_isolated(adapter, "C48A", step_c48a);
                (void)run_isolated(adapter, "C49A", c49_typed_uav_loads);
                (void)run_isolated(adapter, "C50A", step_c50a);
                (void)run_isolated(adapter, "C51A", step_c51a);
                (void)run_isolated(adapter, "C52A", step_c52a);
                (void)run_isolated(adapter, "C53A", step_c53a);
                (void)run_isolated(adapter, "C57A", step_c57a);
                (void)run_isolated(adapter, "C58A", step_c58a);
                (void)run_isolated(adapter, "C59A", step_c59a);
                (void)run_isolated(adapter, "C59B", step_c59b);
                (void)run_isolated(adapter, "C60A", step_c60a);
                (void)run_isolated(adapter, "C61A", step_c61a);
                (void)run_isolated(adapter, "C61B", step_c61b);
                (void)run_isolated(adapter, "C61C", step_c61c);
                (void)run_isolated(adapter, "C61D", step_c61d);
                (void)run_isolated(adapter, "C62A", step_c62a);
                (void)run_isolated(adapter, "C76A", c76_shared_open);
                (void)run_isolated(adapter, "C7A", swapchain_present);
                (void)run_isolated(adapter, "C40A", c40_swapchain_10bit);
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
