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
    if (options.TiledResourcesTier == D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED) {
        puts("AGX_D3D12_C53B: SKIP tier 0 (set APPLE_AGX_D3D12_TILED=1)");
        return 0;
    }
    const int textures = texture_steps(&c, (UINT)options.TiledResourcesTier);
    const int buffers = buffer_steps(&c);
    printf("AGX_D3D12_C53B: %s\n", textures && buffers ? "PASS" : "FAIL");
    return 0;
}
