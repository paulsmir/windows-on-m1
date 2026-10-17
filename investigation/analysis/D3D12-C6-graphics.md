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

## C6b

### WINDOWS CONTRACT

- D3D12DDIARG_CREATE_PIPELINE_STATE_0001: hVertexShader/hPixelShader
  (tokenized programs from pfnCreateVertexShader/PixelShader), hBlendState,
  hRasterizerState, hDepthStencilState, hElementLayout (elements carry the
  runtime-resolved InputRegister), SampleMask, PrimitiveTopologyType,
  RTVFormats; the runtime may destroy the state objects after the pipeline.
- List state: pfnOMSetRenderTargets (CPU RTV handles, consumed at record;
  single-range form), pfnRsSetViewports/ScissorRects, pfnIaSetTopology,
  pfnIASetVertexBuffers (VA, size, stride), pfnOmSetBlendFactor/StencilRef,
  pfnSetGraphicsRootSignature; pfnDrawInstanced. D3D12 always scissors.

### AGX/ASAHI CONTRACT

- The D3D10 frontend's translations (OutputMerger.cpp blend/depth-stencil,
  Rasterizer.cpp rasterizer + viewport scale/translate with y flipped,
  InputAssembly.cpp elements indexed by input register, Draw.cpp element
  strides applied per draw); create_vs/fs_state(NIR from dxbc_to_nir, I/O
  linked by register number); pipe_framebuffer_state with by-value
  surfaces; draw_vbo.

### TRANSLATION

- Pipeline: copies of VS/PS tokens and state descriptions; compiled on the
  owner at its first draw (shaders + blend/DSA/rasterizer CSOs + element
  records); the vertex-elements CSO is created with the draw's strides and
  cached per pipeline until they change.
- Draw: snapshot of RTVs, viewport 0, scissor 0, topology, vertex buffers
  (VA -> owner heap + offset), blend factor, stencil ref; replay sets the
  framebuffer and state and calls draw_vbo; the submit unbinds framebuffer
  and vertex buffers before flushing.
- Refused with measure-d3d12-draw {reason}: graphics root parameters
  (C6c), GS/HS/DS, no RTV; DSVs (measure-d3d12-render-targets-refused).

### WHAT IS STILL UNKNOWN

- Hardware: whether the owner's render pass on a D3D12 device context runs
  like the D3D11 path's (same Asahi context type; first D3D12 render).

### Checkpoint

Probe C6B: red clear, green triangle (-0.5,-0.5)/(0,0.5)/(0.5,-0.5) from an
UPLOAD vertex buffer (float2 + float4, stride 24), empty root signature
(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT); PASS = centre green, corner red, no
other colours, 448..576 green pixels.
