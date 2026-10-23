/* C53B (investigation/analysis/D3D12-C53-tiled-textures.md): reserved
 * resources through the real D3D12 API -- CreateReservedResource,
 * GetResourceTiling, UpdateTileMappings, CopyTextureRegion, CopyTiles,
 * CopyTileMappings -- on the AGX adapter (needs APPLE_AGX_D3D12_TILED=1 for
 * tier 1) or on WARP ("warp", the reference for the probe's own logic).
 * Each step prints AGX_D3D12_C53B_<step>: PASS|FAIL|SKIP and its HRESULTs. */
#define COBJMACROS
#include <windows.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <stdio.h>
#include <string.h>

#include "agx_d3d12_tiled_probe_cs.h"
#include "agx_d3d12_tiled_probe_minmax_cs.h"
#include "agx_d3d12_tiled_probe_minmax_slot_cs.h"
#include <math.h>

#define TILE 65536u
#define SIZE 256u

typedef struct {
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;
    ID3D12Fence *fence;
    UINT64 value;
    HANDLE event;
} Ctx;

static HRESULT ctx_init(Ctx *c, IDXGIAdapter1 *adapter) {
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT};
    HRESULT hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&c->device);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandQueue(c->device, &qd, &IID_ID3D12CommandQueue, (void **)&c->queue);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandAllocator(c->device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                                                 (void **)&c->allocator);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(c->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, c->allocator, NULL,
                                            &IID_ID3D12GraphicsCommandList, (void **)&c->list);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateFence(c->device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&c->fence);
    c->event = CreateEventA(NULL, FALSE, FALSE, NULL);
    return SUCCEEDED(hr) && !c->event ? E_FAIL : hr;
}

/* close, execute, wait, reset */
static HRESULT ctx_flush(Ctx *c) {
    HRESULT hr = ID3D12GraphicsCommandList_Close(c->list);
    if (FAILED(hr)) return hr;
    ID3D12CommandList *lists[] = {(ID3D12CommandList *)c->list};
    ID3D12CommandQueue_ExecuteCommandLists(c->queue, 1, lists);
    hr = ID3D12CommandQueue_Signal(c->queue, c->fence, ++c->value);
    if (SUCCEEDED(hr) && ID3D12Fence_GetCompletedValue(c->fence) < c->value) {
        hr = ID3D12Fence_SetEventOnCompletion(c->fence, c->value, c->event);
        if (SUCCEEDED(hr) && WaitForSingleObject(c->event, 10000) != WAIT_OBJECT_0) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) hr = ID3D12CommandAllocator_Reset(c->allocator);
    if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Reset(c->list, c->allocator, NULL);
    if (SUCCEEDED(hr)) hr = ID3D12Device_GetDeviceRemovedReason(c->device);
    return hr;
}

static ID3D12Resource *buffer(Ctx *c, D3D12_HEAP_TYPE type, UINT64 bytes, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap = {type};
    D3D12_RESOURCE_DESC d = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, bytes, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                             D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
    ID3D12Resource *r = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(c->device, &heap, D3D12_HEAP_FLAG_NONE, &d, state, NULL,
                                                      &IID_ID3D12Resource, (void **)&r);
    return SUCCEEDED(hr) ? r : NULL;
}

static void barrier(Ctx *c, ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    ID3D12GraphicsCommandList_ResourceBarrier(c->list, 1, &b);
}

static UINT32 texel(UINT x, UINT y, UINT level) { return 0xff000000u | (level << 16) | ((y & 0xffu) << 8) | (x & 0xffu); }

static void result(const char *step, int pass, HRESULT hr, UINT bad) {
    printf("AGX_D3D12_C53B_%s: %s hr=0x%08lx bad=%u\n", step, pass ? "PASS" : "FAIL", (unsigned long)hr, bad);
}

static int texture_steps(Ctx *c, UINT tier) {
    D3D12_RESOURCE_DESC d = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, SIZE, SIZE, 1, 3, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
                             D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE, D3D12_RESOURCE_FLAG_NONE};
    ID3D12Resource *tex = NULL, *up = NULL, *rb = NULL;
    ID3D12Heap *heap = NULL;
    HRESULT hr = ID3D12Device_CreateReservedResource(c->device, &d, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                                     &IID_ID3D12Resource, (void **)&tex);
    result("CREATE", SUCCEEDED(hr), hr, 0);
    if (FAILED(hr)) return 0;
    /* tiling: the runtime's standard shapes and the driver's packing */
    UINT total = 0, subs = 3;
    D3D12_PACKED_MIP_INFO packed = {0};
    D3D12_TILE_SHAPE shape = {0};
    D3D12_SUBRESOURCE_TILING tiling[3] = {0};
    ID3D12Device_GetResourceTiling(c->device, tex, &total, &packed, &shape, &subs, 0, tiling);
    printf("C53B tiling total=%u shape=%ux%u standard=%u packed=%u packedTiles=%u start=%u mip0=%ux%u\n", total,
           shape.WidthInTexels, shape.HeightInTexels, packed.NumStandardMips, packed.NumPackedMips,
           packed.NumTilesForPackedMips, packed.StartTileIndexInOverallResource, tiling[0].WidthInTiles,
           tiling[0].HeightInTiles);
    result("TILING", shape.WidthInTexels == 128 && shape.HeightInTexels == 128 && tiling[0].WidthInTiles == 2 &&
                         tiling[0].HeightInTiles == 2 && total <= 8, S_OK, 0);
    D3D12_HEAP_DESC hd = {8 * TILE, {D3D12_HEAP_TYPE_DEFAULT}, 0, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES};
    hr = ID3D12Device_CreateHeap(c->device, &hd, &IID_ID3D12Heap, (void **)&heap);
    /* every tile onto its own heap tile, in resource tile order */
    D3D12_TILED_RESOURCE_COORDINATE start = {0, 0, 0, 0};
    D3D12_TILE_REGION_SIZE all = {total, FALSE, 0, 0, 0};
    UINT heap_start = 0, count = total;
    D3D12_TILE_RANGE_FLAGS flag = D3D12_TILE_RANGE_FLAG_NONE;
    if (SUCCEEDED(hr))
        ID3D12CommandQueue_UpdateTileMappings(c->queue, tex, 1, &start, &all, heap, 1, &flag, &heap_start, &count,
                                              D3D12_TILE_MAPPING_FLAG_NONE);
    up = SUCCEEDED(hr) ? buffer(c, D3D12_HEAP_TYPE_UPLOAD, 3 * SIZE * SIZE * 4u, D3D12_RESOURCE_STATE_GENERIC_READ) : NULL;
    rb = up ? buffer(c, D3D12_HEAP_TYPE_READBACK, 3 * SIZE * SIZE * 4u, D3D12_RESOURCE_STATE_COPY_DEST) : NULL;
    if (!rb && SUCCEEDED(hr)) hr = E_OUTOFMEMORY;
    /* upload every mip; the footprints of mip 0 serve all three (pitch 1024) */
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[3];
    unsigned char *map = NULL;
    D3D12_RANGE none = {0, 0};
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(up, 0, &none, (void **)&map);
    UINT64 offsets[3] = {0, SIZE * SIZE * 4u, 2u * SIZE * SIZE * 4u};
    for (UINT level = 0; SUCCEEDED(hr) && level < 3; ++level) {
        ID3D12Device_GetCopyableFootprints(c->device, &d, level, 1, offsets[level], &fp[level], NULL, NULL, NULL);
        for (UINT y = 0; y < (SIZE >> level); ++y)
            for (UINT x = 0; x < (SIZE >> level); ++x)
                ((UINT32 *)(map + fp[level].Offset + (UINT64)y * fp[level].Footprint.RowPitch))[x] = texel(x, y, level);
    }
    if (map) ID3D12Resource_Unmap(up, 0, NULL);
    for (UINT level = 0; SUCCEEDED(hr) && level < 3; ++level) {
        D3D12_TEXTURE_COPY_LOCATION t = {tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        D3D12_TEXTURE_COPY_LOCATION b = {up, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        t.SubresourceIndex = level;
        b.PlacedFootprint = fp[level];
        ID3D12GraphicsCommandList_CopyTextureRegion(c->list, &t, 0, 0, 0, &b, NULL);
    }
    if (SUCCEEDED(hr)) {
        barrier(c, tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        for (UINT level = 0; level < 3; ++level) {
            D3D12_TEXTURE_COPY_LOCATION t = {tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
            D3D12_TEXTURE_COPY_LOCATION b = {rb, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
            t.SubresourceIndex = level;
            b.PlacedFootprint = fp[level];
            ID3D12GraphicsCommandList_CopyTextureRegion(c->list, &b, 0, 0, 0, &t, NULL);
        }
        hr = ctx_flush(c);
    }
    UINT bad = 0;
    D3D12_RANGE whole = {0, 3 * SIZE * SIZE * 4u};
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(rb, 0, &whole, (void **)&map);
    for (UINT level = 0; SUCCEEDED(hr) && level < 3; ++level)
        for (UINT y = 0; y < (SIZE >> level); ++y)
            for (UINT x = 0; x < (SIZE >> level); ++x)
                bad += ((UINT32 *)(map + fp[level].Offset + (UINT64)y * fp[level].Footprint.RowPitch))[x] != texel(x, y, level);
    if (SUCCEEDED(hr)) ID3D12Resource_Unmap(rb, 0, &none);
    result("UPLOAD_READBACK", SUCCEEDED(hr) && !bad, hr, bad);
    /* alias: mip-0 tile (1,0) onto tile (0,0)'s heap tile -> its texels read (0,0)'s */
    if (SUCCEEDED(hr)) {
        D3D12_TILED_RESOURCE_COORDINATE t10 = {1, 0, 0, 0};
        D3D12_TILE_REGION_SIZE one = {1, FALSE, 0, 0, 0};
        UINT h0 = 0, n1 = 1;
        ID3D12CommandQueue_UpdateTileMappings(c->queue, tex, 1, &t10, &one, heap, 1, &flag, &h0, &n1,
                                              D3D12_TILE_MAPPING_FLAG_NONE);
        D3D12_TEXTURE_COPY_LOCATION t = {tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        D3D12_TEXTURE_COPY_LOCATION b = {rb, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        b.PlacedFootprint = fp[0];
        ID3D12GraphicsCommandList_CopyTextureRegion(c->list, &b, 0, 0, 0, &t, NULL);
        hr = ctx_flush(c);
    }
    bad = 0;
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(rb, 0, &whole, (void **)&map);
    for (UINT y = 0; SUCCEEDED(hr) && y < 128; ++y)
        for (UINT x = 128; x < 256; ++x)
            bad += ((UINT32 *)(map + (UINT64)y * fp[0].Footprint.RowPitch))[x] != texel(x - 128, y, 0);
    if (SUCCEEDED(hr)) ID3D12Resource_Unmap(rb, 0, &none);
    result("ALIAS", SUCCEEDED(hr) && !bad, hr, bad);
    /* CopyTiles: mip-0 tile (0,1) to a buffer, row-major at the tile pitch */
    ID3D12Resource *tiles = SUCCEEDED(hr) ? buffer(c, D3D12_HEAP_TYPE_READBACK, TILE, D3D12_RESOURCE_STATE_COPY_DEST) : NULL;
    if (tiles) {
        D3D12_TILED_RESOURCE_COORDINATE t01 = {0, 1, 0, 0};
        D3D12_TILE_REGION_SIZE one = {1, FALSE, 0, 0, 0};
        ID3D12GraphicsCommandList_CopyTiles(c->list, tex, &t01, &one, tiles, 0,
                                            D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
        hr = ctx_flush(c);
        bad = 0;
        D3D12_RANGE tile_range = {0, TILE};
        if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(tiles, 0, &tile_range, (void **)&map);
        for (UINT y = 0; SUCCEEDED(hr) && y < 128; ++y)
            for (UINT x = 0; x < 128; ++x) bad += ((UINT32 *)map)[y * 128 + x] != texel(x, y + 128, 0);
        if (SUCCEEDED(hr)) ID3D12Resource_Unmap(tiles, 0, &none);
        result("COPYTILES", SUCCEEDED(hr) && !bad, hr, bad);
        ID3D12Resource_Release(tiles);
    }
    (void)tier;
    if (rb) ID3D12Resource_Release(rb);
    if (up) ID3D12Resource_Release(up);
    if (heap) ID3D12Heap_Release(heap);
    ID3D12Resource_Release(tex);
    return SUCCEEDED(hr);
}

/* a reserved buffer: tiles onto a heap, a fill, CopyTileMappings tile 0 ->
 * tile 3, a read of tile 3 */
static int buffer_steps(Ctx *c) {
    D3D12_RESOURCE_DESC d = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, 4 * TILE, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                             D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
    ID3D12Resource *res = NULL, *up = NULL, *rb = NULL;
    ID3D12Heap *heap = NULL;
    HRESULT hr = ID3D12Device_CreateReservedResource(c->device, &d, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                                     &IID_ID3D12Resource, (void **)&res);
    D3D12_HEAP_DESC hd = {4 * TILE, {D3D12_HEAP_TYPE_DEFAULT}, 0, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS};
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateHeap(c->device, &hd, &IID_ID3D12Heap, (void **)&heap);
    D3D12_TILED_RESOURCE_COORDINATE start = {0, 0, 0, 0};
    D3D12_TILE_REGION_SIZE three = {3, FALSE, 0, 0, 0};
    UINT heap_start = 1, count = 3;
    D3D12_TILE_RANGE_FLAGS flag = D3D12_TILE_RANGE_FLAG_NONE;
    if (SUCCEEDED(hr))
        ID3D12CommandQueue_UpdateTileMappings(c->queue, res, 1, &start, &three, heap, 1, &flag, &heap_start, &count,
                                              D3D12_TILE_MAPPING_FLAG_NONE);
    up = SUCCEEDED(hr) ? buffer(c, D3D12_HEAP_TYPE_UPLOAD, 3 * TILE, D3D12_RESOURCE_STATE_GENERIC_READ) : NULL;
    rb = up ? buffer(c, D3D12_HEAP_TYPE_READBACK, TILE, D3D12_RESOURCE_STATE_COPY_DEST) : NULL;
    if (!rb && SUCCEEDED(hr)) hr = E_OUTOFMEMORY;
    UINT32 *words = NULL;
    D3D12_RANGE none = {0, 0};
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(up, 0, &none, (void **)&words);
    for (UINT i = 0; SUCCEEDED(hr) && i < 3 * TILE / 4; ++i) words[i] = 0xc0000000u | i;
    if (words) ID3D12Resource_Unmap(up, 0, NULL);
    if (SUCCEEDED(hr)) {
        ID3D12GraphicsCommandList_CopyBufferRegion(c->list, res, 0, up, 0, 3 * TILE);
        hr = ctx_flush(c);
    }
    /* tile 3 takes tile 0's mapping: it reads tile 0's words */
    if (SUCCEEDED(hr)) {
        D3D12_TILED_RESOURCE_COORDINATE dst = {3, 0, 0, 0}, src = {0, 0, 0, 0};
        D3D12_TILE_REGION_SIZE one = {1, FALSE, 0, 0, 0};
        ID3D12CommandQueue_CopyTileMappings(c->queue, res, &dst, res, &src, &one, D3D12_TILE_MAPPING_FLAG_NONE);
        barrier(c, res, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(c->list, rb, 0, res, 3 * TILE, TILE);
        hr = ctx_flush(c);
    }
    UINT bad = 0;
    D3D12_RANGE whole = {0, TILE};
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(rb, 0, &whole, (void **)&words);
    for (UINT i = 0; SUCCEEDED(hr) && i < TILE / 4; ++i) bad += words[i] != (0xc0000000u | i);
    if (SUCCEEDED(hr)) ID3D12Resource_Unmap(rb, 0, &none);
    result("BUFFER_COPYTILEMAPPINGS", SUCCEEDED(hr) && !bad, hr, bad);
    if (rb) ID3D12Resource_Release(rb);
    if (up) ID3D12Resource_Release(up);
    if (heap) ID3D12Heap_Release(heap);
    if (res) ID3D12Resource_Release(res);
    return SUCCEEDED(hr) && !bad;
}

/* Tier 2 (D3D12_TILED_RESOURCES_TIER): a shader's SRV reads of a NULL tile
 * return 0. A reserved buffer of 4 tiles, tile 2 unmapped and written to
 * first (Tier 2 discards it; on AGX it lands in the tile scratch); a
 * compute shader copies it through a root SRV into a UAV. Tier >= 2 must
 * read 0 there (PASS/FAIL); tier 1 only reports what it reads. */
static int null_read_step(Ctx *c, D3D12_TILED_RESOURCES_TIER tier) {
    D3D12_RESOURCE_DESC d = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, 4 * TILE, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                             D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
    ID3D12Resource *res = NULL, *up = NULL, *out = NULL, *rb = NULL;
    ID3D12Heap *heap = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3DBlob *blob = NULL;
    HRESULT hr = ID3D12Device_CreateReservedResource(c->device, &d, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                                     &IID_ID3D12Resource, (void **)&res);
    D3D12_HEAP_DESC hd = {4 * TILE, {D3D12_HEAP_TYPE_DEFAULT}, 0, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS};
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateHeap(c->device, &hd, &IID_ID3D12Heap, (void **)&heap);
    /* tiles 0, 1 and 3 onto heap tiles 0-2; tile 2 stays NULL */
    D3D12_TILED_RESOURCE_COORDINATE starts[2] = {{0, 0, 0, 0}, {3, 0, 0, 0}};
    D3D12_TILE_REGION_SIZE sizes[2] = {{2, FALSE, 0, 0, 0}, {1, FALSE, 0, 0, 0}};
    UINT heap_starts[2] = {0, 2}, counts[2] = {2, 1};
    if (SUCCEEDED(hr))
        ID3D12CommandQueue_UpdateTileMappings(c->queue, res, 2, starts, sizes, heap, 2, NULL, heap_starts, counts,
                                              D3D12_TILE_MAPPING_FLAG_NONE);
    up = SUCCEEDED(hr) ? buffer(c, D3D12_HEAP_TYPE_UPLOAD, 4 * TILE, D3D12_RESOURCE_STATE_GENERIC_READ) : NULL;
    rb = up ? buffer(c, D3D12_HEAP_TYPE_READBACK, 4 * TILE, D3D12_RESOURCE_STATE_COPY_DEST) : NULL;
    if (rb) {
        D3D12_HEAP_PROPERTIES def = {D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC od = d;
        od.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        hr = ID3D12Device_CreateCommittedResource(c->device, &def, D3D12_HEAP_FLAG_NONE, &od,
                                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource,
                                                  (void **)&out);
    } else if (SUCCEEDED(hr)) {
        hr = E_OUTOFMEMORY;
    }
    UINT32 *words = NULL;
    D3D12_RANGE none = {0, 0};
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(up, 0, &none, (void **)&words);
    for (UINT i = 0; SUCCEEDED(hr) && i < 4 * TILE / 4; ++i) words[i] = 0xd0000000u | i;
    if (words) ID3D12Resource_Unmap(up, 0, NULL);
    /* root signature: SRV t0 and UAV u0 as root descriptors */
    D3D12_ROOT_PARAMETER params[2] = {{D3D12_ROOT_PARAMETER_TYPE_SRV}, {D3D12_ROOT_PARAMETER_TYPE_UAV}};
    D3D12_ROOT_SIGNATURE_DESC rd = {2, params, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    if (SUCCEEDED(hr)) hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, NULL);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(c->device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&root);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {root, {g_tiled_probe_cs, sizeof(g_tiled_probe_cs)}};
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateComputePipelineState(c->device, &pd, &IID_ID3D12PipelineState, (void **)&pso);
    if (SUCCEEDED(hr)) {
        /* fill all 4 tiles: tile 2's write goes to a NULL tile */
        ID3D12GraphicsCommandList_CopyBufferRegion(c->list, res, 0, up, 0, 4 * TILE);
        barrier(c, res, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ID3D12GraphicsCommandList_SetComputeRootSignature(c->list, root);
        ID3D12GraphicsCommandList_SetPipelineState(c->list, pso);
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(c->list, 0, ID3D12Resource_GetGPUVirtualAddress(res));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(c->list, 1, ID3D12Resource_GetGPUVirtualAddress(out));
        ID3D12GraphicsCommandList_Dispatch(c->list, 4 * TILE / 4 / 64, 1, 1);
        barrier(c, out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(c->list, rb, 0, out, 0, 4 * TILE);
        hr = ctx_flush(c);
    }
    UINT bad = 0, zero = 0, nonzero = 0;
    D3D12_RANGE whole = {0, 4 * TILE};
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(rb, 0, &whole, (void **)&words);
    for (UINT i = 0; SUCCEEDED(hr) && i < 4 * TILE / 4; ++i) {
        if (i / (TILE / 4) == 2) { if (words[i]) ++nonzero; else ++zero; }
        else bad += words[i] != (0xd0000000u | i);
    }
    if (SUCCEEDED(hr)) ID3D12Resource_Unmap(rb, 0, &none);
    printf("C53B null read tier=%d mapped_bad=%u null_zero=%u null_nonzero=%u\n", (int)tier, bad, zero, nonzero);
    const int pass = SUCCEEDED(hr) && !bad && (tier < D3D12_TILED_RESOURCES_TIER_2 || !nonzero);
    if (tier < D3D12_TILED_RESOURCES_TIER_2 && SUCCEEDED(hr) && !bad)
        printf("AGX_D3D12_C53B_NULL_READ: MEASURED tier 1 (null_zero=%u null_nonzero=%u)\n", zero, nonzero);
    else
        result("NULL_READ", pass, hr, bad + nonzero);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (blob) ID3D10Blob_Release(blob);
    if (rb) ID3D12Resource_Release(rb);
    if (out) ID3D12Resource_Release(out);
    if (up) ID3D12Resource_Release(up);
    if (heap) ID3D12Heap_Release(heap);
    if (res) ID3D12Resource_Release(res);
    return pass;
}

/* C65A: min/max reduction filtering (FL 12_0 with Tier 2). A 4x4 + 2x2
 * RGBA32F texture sampled with SampleLevel at fixed points through four heap
 * samplers (MIN/MAX, linear or point mips, clamp addressing), against the
 * CPU reference of the D3D11.3 rule: the texels the filter weights, zero
 * weights excluded, component-wise min/max. */
static float texel_value(UINT level, UINT x, UINT y, UINT c) {
    const UINT i = x + y * (4u >> level);
    switch (c) {
    case 0: return (float)(level * 100u + i);
    case 1: return (float)(level * 100u + 15u - i);
    case 2: return (float)((x * 7u + y * 3u + level * 5u) % 11u);
    default: return 1.0f;
    }
}
static const float minmax_points[8][3] = {{0.300f, 0.600f, 0.0f}, {0.375f, 0.375f, 0.0f}, {0.375f, 0.600f, 0.0f},
                                          {0.050f, 0.950f, 0.0f}, {0.300f, 0.600f, 0.5f}, {0.300f, 0.600f, 1.0f},
                                          {0.700f, 0.200f, 0.25f}, {0.625f, 0.875f, 0.75f}};
static void minmax_reference(int maximum, int mip_linear, const float *pt, float out[4]) {
    const float lod = pt[2] < 0.0f ? 0.0f : pt[2] > 1.0f ? 1.0f : pt[2];
    UINT levels[2] = {mip_linear ? (UINT)floorf(lod) : (UINT)floorf(lod + 0.5f), 0};
    if (levels[0] > 1) levels[0] = 1;
    UINT count = 1;
    if (mip_linear && lod - floorf(lod) > 0.0f && levels[0] < 1) { levels[1] = levels[0] + 1; count = 2; }
    for (UINT c = 0; c < 4; ++c) out[c] = maximum ? -1e30f : 1e30f;
    for (UINT l = 0; l < count; ++l) {
        const float size = (float)(4u >> levels[l]);
        const float px = pt[0] * size - 0.5f, py = pt[1] * size - 0.5f;
        const int x0 = (int)floorf(px), y0 = (int)floorf(py);
        const int nx = px - floorf(px) > 0.0f ? 2 : 1, ny = py - floorf(py) > 0.0f ? 2 : 1;
        for (int dy = 0; dy < ny; ++dy)
            for (int dx = 0; dx < nx; ++dx) {
                int x = x0 + dx, y = y0 + dy;
                const int hi = (int)(4u >> levels[l]) - 1;
                x = x < 0 ? 0 : x > hi ? hi : x;
                y = y < 0 ? 0 : y > hi ? hi : y;
                for (UINT c = 0; c < 4; ++c) {
                    const float v = texel_value(levels[l], (UINT)x, (UINT)y, c);
                    out[c] = maximum ? (v > out[c] ? v : out[c]) : (v < out[c] ? v : out[c]);
                }
            }
    }
}
/* mode 0: heap samplers (unbounded table); 1: a one-sampler table (slot);
 * 2: a static sampler (slot, filter 0 only) */
static int minmax_step(Ctx *c, D3D12_TILED_RESOURCES_TIER tier, int mode) {
    static const char *const names[3] = {"C65A_MINMAX", "C65A_MINMAX_SLOT", "C65A_MINMAX_STATIC"};
    const UINT runs = mode == 2 ? 1u : 4u;
    static const D3D12_FILTER filters[4] = {D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR, D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR,
                                            D3D12_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT,
                                            D3D12_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT};
    D3D12_HEAP_PROPERTIES def = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC td = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 4, 4, 1, 2, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0},
                              D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE};
    ID3D12Resource *tex = NULL, *up = NULL, *out = NULL, *rb = NULL;
    ID3D12DescriptorHeap *views = NULL, *samplers = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pso = NULL;
    ID3DBlob *blob = NULL, *error = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(c->device, &def, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST,
                                                      NULL, &IID_ID3D12Resource, (void **)&tex);
    up = SUCCEEDED(hr) ? buffer(c, D3D12_HEAP_TYPE_UPLOAD, 4096, D3D12_RESOURCE_STATE_GENERIC_READ) : NULL;
    rb = up ? buffer(c, D3D12_HEAP_TYPE_READBACK, 4 * 8 * 16, D3D12_RESOURCE_STATE_COPY_DEST) : NULL;
    if (rb) {
        D3D12_RESOURCE_DESC od = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, 4 * 8 * 16, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                                  D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
        hr = ID3D12Device_CreateCommittedResource(c->device, &def, D3D12_HEAP_FLAG_NONE, &od,
                                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource,
                                                  (void **)&out);
    } else if (SUCCEEDED(hr)) {
        hr = E_OUTOFMEMORY;
    }
    /* both mips through one upload buffer */
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2];
    if (SUCCEEDED(hr)) ID3D12Device_GetCopyableFootprints(c->device, &td, 0, 2, 0, fp, NULL, NULL, NULL);
    unsigned char *map = NULL;
    D3D12_RANGE none = {0, 0};
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(up, 0, &none, (void **)&map);
    for (UINT l = 0; SUCCEEDED(hr) && l < 2; ++l)
        for (UINT y = 0; y < (4u >> l); ++y)
            for (UINT x = 0; x < (4u >> l); ++x)
                for (UINT k = 0; k < 4; ++k)
                    ((float *)(map + fp[l].Offset + (UINT64)y * fp[l].Footprint.RowPitch))[x * 4 + k] = texel_value(l, x, y, k);
    if (map) ID3D12Resource_Unmap(up, 0, NULL);
    for (UINT l = 0; SUCCEEDED(hr) && l < 2; ++l) {
        D3D12_TEXTURE_COPY_LOCATION dst = {tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        D3D12_TEXTURE_COPY_LOCATION src = {up, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        dst.SubresourceIndex = l;
        src.PlacedFootprint = fp[l];
        ID3D12GraphicsCommandList_CopyTextureRegion(c->list, &dst, 0, 0, 0, &src, NULL);
    }
    if (SUCCEEDED(hr)) barrier(c, tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    /* the SRV and the four samplers in shader-visible heaps */
    D3D12_DESCRIPTOR_HEAP_DESC vd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_HEAP_DESC sd = {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 4, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(c->device, &vd, &IID_ID3D12DescriptorHeap, (void **)&views);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(c->device, &sd, &IID_ID3D12DescriptorHeap, (void **)&samplers);
    if (SUCCEEDED(hr)) {
        D3D12_CPU_DESCRIPTOR_HANDLE vh, sh;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(views, &vh);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(samplers, &sh);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_SRV_DIMENSION_TEXTURE2D,
                                               D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
        srv.Texture2D.MipLevels = 2;
        ID3D12Device_CreateShaderResourceView(c->device, tex, &srv, vh);
        const UINT step = ID3D12Device_GetDescriptorHandleIncrementSize(c->device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        for (UINT i = 0; i < 4; ++i) {
            D3D12_SAMPLER_DESC s = {filters[i], D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                    D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 1, D3D12_COMPARISON_FUNC_NEVER, {0, 0, 0, 0},
                                    0.0f, D3D12_FLOAT32_MAX};
            D3D12_CPU_DESCRIPTOR_HANDLE at = {sh.ptr + (SIZE_T)i * step};
            ID3D12Device_CreateSampler(c->device, &s, at);
        }
    }
    /* root: SRV table t0; mode 0 an unbounded sampler table s0 and b0 pick,
     * mode 1 a one-sampler table s0, mode 2 a static sampler s0; root UAV u0 */
    D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0},
                                        {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, mode ? 1u : UINT_MAX, 0, 0, 0}};
    D3D12_ROOT_PARAMETER params[4] = {{D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE}, {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE},
                                      {D3D12_ROOT_PARAMETER_TYPE_UAV}, {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS}};
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &ranges[0];
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &ranges[1];
    params[3].Constants.Num32BitValues = 1;
    const UINT uav_param = mode == 2 ? 1u : 2u;
    if (mode == 2) params[1] = params[2];
    D3D12_STATIC_SAMPLER_DESC fixed = {filters[0], D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                       D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 1, D3D12_COMPARISON_FUNC_NEVER,
                                       D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, 0.0f, D3D12_FLOAT32_MAX, 0, 0,
                                       D3D12_SHADER_VISIBILITY_ALL};
    D3D12_ROOT_SIGNATURE_DESC rd = {mode == 0 ? 4u : mode == 1 ? 3u : 2u, params, mode == 2 ? 1u : 0u, mode == 2 ? &fixed : NULL,
                                    D3D12_ROOT_SIGNATURE_FLAG_NONE};
    if (SUCCEEDED(hr)) hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateRootSignature(c->device, 0, ID3D10Blob_GetBufferPointer(blob),
                                              ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&root);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {root, {g_minmax_probe_cs, sizeof(g_minmax_probe_cs)}};
    if (mode) {
        pd.CS.pShaderBytecode = g_minmax_slot_probe_cs;
        pd.CS.BytecodeLength = sizeof(g_minmax_slot_probe_cs);
    }
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateComputePipelineState(c->device, &pd, &IID_ID3D12PipelineState, (void **)&pso);
    if (SUCCEEDED(hr)) {
        ID3D12DescriptorHeap *heaps[2] = {views, samplers};
        D3D12_GPU_DESCRIPTOR_HANDLE vg, sg;
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(views, &vg);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(samplers, &sg);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(c->list, 2, heaps);
        ID3D12GraphicsCommandList_SetComputeRootSignature(c->list, root);
        ID3D12GraphicsCommandList_SetPipelineState(c->list, pso);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(c->list, 0, vg);
        if (mode == 0) ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(c->list, 1, sg);
        const UINT step = ID3D12Device_GetDescriptorHandleIncrementSize(c->device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        for (UINT i = 0; i < runs; ++i) {
            ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(c->list, uav_param,
                                                                        ID3D12Resource_GetGPUVirtualAddress(out) + i * 8 * 16);
            if (mode == 0) ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(c->list, 3, i, 0);
            if (mode == 1) {
                D3D12_GPU_DESCRIPTOR_HANDLE at = {sg.ptr + (UINT64)i * step};
                ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(c->list, 1, at);
            }
            ID3D12GraphicsCommandList_Dispatch(c->list, 1, 1, 1);
        }
        barrier(c, out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyBufferRegion(c->list, rb, 0, out, 0, 4 * 8 * 16);
        hr = ctx_flush(c);
    }
    UINT bad = 0;
    float *got = NULL;
    D3D12_RANGE whole = {0, 4 * 8 * 16};
    if (SUCCEEDED(hr)) hr = ID3D12Resource_Map(rb, 0, &whole, (void **)&got);
    for (UINT i = 0; SUCCEEDED(hr) && i < runs; ++i)
        for (UINT p = 0; p < 8; ++p) {
            float want[4];
            minmax_reference(i & 1, i < 2, minmax_points[p], want);
            const float *v = got + (i * 8 + p) * 4;
            if (fabsf(v[0] - want[0]) > 1e-4f || fabsf(v[1] - want[1]) > 1e-4f || fabsf(v[2] - want[2]) > 1e-4f) {
                if (bad < 6)
                    printf("%s filter %u point %u got %g %g %g want %g %g %g\n", names[mode], i, p, v[0], v[1], v[2], want[0],
                           want[1], want[2]);
                ++bad;
            }
        }
    if (got) ID3D12Resource_Unmap(rb, 0, &none);
    if (error) {
        printf("C65A root signature: %s\n", (const char *)ID3D10Blob_GetBufferPointer(error));
        ID3D10Blob_Release(error);
    }
    printf("%s tier=%d\n", names[mode], (int)tier);
    result(names[mode], SUCCEEDED(hr) && !bad, hr, bad);
    if (pso) ID3D12PipelineState_Release(pso);
    if (root) ID3D12RootSignature_Release(root);
    if (blob) ID3D10Blob_Release(blob);
    if (views) ID3D12DescriptorHeap_Release(views);
    if (samplers) ID3D12DescriptorHeap_Release(samplers);
    if (rb) ID3D12Resource_Release(rb);
    if (out) ID3D12Resource_Release(out);
    if (up) ID3D12Resource_Release(up);
    if (tex) ID3D12Resource_Release(tex);
    return SUCCEEDED(hr) && !bad;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    IDXGIFactory4 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    const int warp = argc > 1 && !strcmp(argv[argc - 1], "warp");
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory);
    if (FAILED(hr)) { printf("CreateDXGIFactory1 hr=0x%08lx\n", (unsigned long)hr); return 1; }
    if (warp) {
        hr = IDXGIFactory4_EnumWarpAdapter(factory, &IID_IDXGIAdapter1, (void **)&adapter);
    } else {
        for (UINT i = 0; IDXGIFactory4_EnumAdapters1(factory, i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter, &desc)) && desc.VendorId == 0x4c505041) break;
            IDXGIAdapter1_Release(adapter);
            adapter = NULL;
        }
        if (!adapter) hr = E_FAIL;
    }
    if (FAILED(hr)) { printf("adapter hr=0x%08lx\n", (unsigned long)hr); return 1; }
    Ctx c = {0};
    hr = ctx_init(&c, adapter);
    printf("C53B device %s hr=0x%08lx\n", warp ? "WARP" : "AGX", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    hr = ID3D12Device_CheckFeatureSupport(c.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    printf("C53B TiledResourcesTier=%d hr=0x%08lx\n", (int)options.TiledResourcesTier, (unsigned long)hr);
    /* C65A first: min/max filtering does not need reserved resources */
    if (argc > 1 && !strcmp(argv[1], "minmax")) {
        /* each mode on its own pipeline; the list is flushed per step */
        int ok = 1;
        for (int mode = 0; mode < 3; ++mode) ok &= minmax_step(&c, options.TiledResourcesTier, mode);
        return ok ? 0 : 1;
    }
    if (options.TiledResourcesTier == D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED) {
        puts("AGX_D3D12_C53B: SKIP tier 0 (set APPLE_AGX_D3D12_TILED=1)");
        return 0;
    }
    const int textures = texture_steps(&c, (UINT)options.TiledResourcesTier);
    const int buffers = buffer_steps(&c);
    const int null_read = null_read_step(&c, options.TiledResourcesTier);
    printf("AGX_D3D12_C53B: %s\n", textures && buffers && null_read ? "PASS" : "FAIL");
    return 0;
}
