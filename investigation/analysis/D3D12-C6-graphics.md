# D3D12 C6: textures, render targets and draws

Status 2026-10-17. C5 (compute) is built on the C4b owner model: every
D3D12 device owns a native Asahi screen + context; heaps are owner
resources; lists record commands that ExecuteCommandLists replays as
Gallium calls, then flush + retire before the runtime's queue signal.
C6 adds graphics in three checkpoints:

- C6a: committed 2D textures, RTVs, ClearRenderTargetView, CopyTextureRegion
  (buffer footprint <-> texture subresource).
- C6b: graphics pipeline (VS/PS through dxbc_to_nir, blend/depth/rasterizer
  and input layout to Gallium CSOs), vertex/index buffers, viewports and
  scissors, OMSetRenderTargets, DrawInstanced/DrawIndexedInstanced.
- C6c: depth-stencil textures, DSVs, ClearDepthStencilView, depth testing.

Sources inspected: WDK 26100 d3d12umddi.h (D3D12DDIARG_CREATERESOURCE_0003,
D3D12DDIARG_CREATE_RENDER_TARGET_VIEW_0002, PFND3D12DDI_CLEAR_RENDER_TARGET_VIEW_0003,
PFND3D12DDI_COPYTEXTUREREGION_0003, D3D12DDIARG_PLACED_RESOURCE with
D3D12DDI_RL_SELECT_SUBRESOURCE / D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED);
Mesa (pinned 9aa1215f) gallium/frontends/d3d10umd OutputMerger.cpp
(RTV = pipe_surface by value, clear_render_target) and Format.cpp
FormatTranslate; p_state.h pipe_surface/pipe_framebuffer_state (by-value
surfaces in this Mesa); asahi agx_state.c (texture_map syncs with the GPU).

## C6a

### WINDOWS CONTRACT

- A committed texture arrives as CreateHeapAndResource with a TEXTURE2D
  D3D12DDIARG_CREATERESOURCE_0003 (Width, Height, DepthOrArraySize,
  MipLevels, Format, SampleDesc, Layout UNKNOWN, Flags such as
  ALLOW_RENDER_TARGET); CheckResourceAllocationInfo is asked first and must
  return a size (UINT64_MAX refuses creation). Textures have no GPU VA.
- RTVs are CPU descriptors (RTV heaps are never shader visible); the
  runtime consumes them when ClearRenderTargetView/OMSetRenderTargets are
  recorded, so their contents are read at record time.
- CopyTextureRegion: the texture side is D3D12DDI_RL_SELECT_SUBRESOURCE
  (placement Offset = subresource index = mip + slice * mips), the buffer
  side D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED (Format, block
  extent, Pitch, SlicePitch; placement Offset = byte offset); an optional
  source box.

### AGX/ASAHI CONTRACT

- Owner textures: screen->resource_create(PIPE_TEXTURE_2D / _ARRAY, pipe
  format from FormatTranslate, bind RENDER_TARGET | SAMPLER_VIEW, usage
  DEFAULT) -- Asahi picks the tiled/compressed layout.
- Clears: clear_render_target(pipe_surface {format, level, layers,
  texture}, color, rect).
- Copies: texture_map/buffer_map synchronise with pending GPU work on the
  context (Asahi flushes writers and waits); a CPU row copy between the
  mapped buffer footprint and the mapped texture box, in replay order.

### TRANSLATION

1. Allocation info for 2D textures: an upper bound of the Asahi layout
   (blocks x bytes x layers, x 4/3 for mip chains, 64 KiB granule).
2. CreateHeapAndResource(TEXTURE2D, sample count 1) creates an owner
   texture; the heap's native resource is the texture (not registered as a
   VA range).
3. RTV descriptor = {texture resource, pipe-format DXGI view format, mip,
   first slice, slices}; ClearRenderTargetView records it with the color
   and rects; replay clears each rect (or the whole level).
4. CopyTextureRegion records {texture side: resource, mip, slice, x/y/z;
   buffer side: heap, offset, pitch, slice pitch; box}; replay maps both
   and copies rows (the owner's texture_map/buffer_map provide the GPU
   ordering).

### WHAT IS STILL UNKNOWN

- Whether the 26100 runtime asks further DDIs for a render-target texture
  (format support, clear values, subresource info) -- receipts name any
  refusal.
- Hardware: clear and copy correctness of Asahi's texture layout through
  the D3D12 owner context (the D3D11 path proves the same calls).

### Checkpoint

Probe C6A: 64x64 R8G8B8A8_UNORM DEFAULT texture (ALLOW_RENDER_TARGET),
RTV heap + RTV, ClearRenderTargetView (0.25, 0.5, 0.75, 1.0), transition,
CopyTextureRegion to a READBACK footprint (pitch 256) -> every pixel
(64, 128, 191, 255); then CopyTextureRegion UPLOAD footprint -> texture ->
READBACK, pattern compare. Signals 8 and 9.
