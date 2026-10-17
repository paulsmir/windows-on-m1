# D3D12 C9: compute texture views, samplers and typed UAVs

Status 2026-10-18. C5-C5d bound buffer views to compute; C6e added texture
SRVs and samplers to draws. C9 gives compute the same view set and adds the
typed UAVs (RWTexture2D, typed RWBuffer) D3D12 applications use for
post-processing, culling and mip generation.

Sources inspected: WDK 26100 d3d12umddi.h (D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002
with D3D12DDIARG_TEX2D_UNORDERED_ACCESS_VIEW_0002 {MipSlice, FirstArraySlice,
ArraySize, PlaneSlice} and the buffer view {FirstElement, NumElements,
StructureByteStride, Flags, counter}, D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002,
D3D12DDI_RESOURCE_FLAG_0022_UNORDERED_ACCESS); the repository's D3D11 path
(mesa/d3d11/agx_d3d11_ddi.cpp uav_image, bind_uavs: a typed UAV u# is
pipe image u#); dxbc_to_nir.h binding conventions (typed UAV u# = image
slot #) and dxbc_to_nir.c uav_image/emit_typed_load (indexed image_*
intrinsics, EXP1304); Mesa asahi agx_state.c agx_set_shader_images
(decompresses a written image, references its resource) and its context
table (set_shader_images, bind_sampler_states assigned).

## Defect found offline

The owner's compute Dispatch built every SRV as an R32_UINT PIPE_BUFFER
view: a texture SRV in a compute table reached it with Texture = 1 and was
bound as a buffer view of a texture (wrong data or a GPU fault). The draw
path's bind_stage handles texture views; compute now binds through it.

## WINDOWS CONTRACT

- A typed UAV is a CPU descriptor: a texture's single mip and slice range,
  or a buffer range of Format elements (neither RAW nor structured); the
  resource carries ALLOW_UNORDERED_ACCESS (DDI flag 0x80).
- Typed buffer SRVs read Format texels. Compute root signatures may carry
  sampler tables and static samplers (visibility ALL).

## AGX/ASAHI CONTRACT

- pipe_image_view {resource, format, READ_WRITE, u.tex level/layers or
  u.buf offset/size}; set_shader_images(MESA_SHADER_COMPUTE); Asahi
  decompresses images that are written (textures with UAV access are
  created with PIPE_BIND_SHADER_IMAGE so the layout is image-writeable).
- Typed buffer SRVs: a PIPE_BUFFER sampler view of the format.
- Samplers: create_sampler_state/bind_sampler_states per stage (the C6e
  translation).

## TRANSLATION

- Descriptors: kDescriptorUavTexture {heap, format, mip, slices},
  kDescriptorUavTypedBuffer {heap, offset, bytes, format} (element size =
  the format's block bytes), typed SRVs keep their format in the buffer SRV
  record (raw/structured: UNKNOWN). Counter UAVs still refuse (reason 7).
- AGX_D3D12_UAV gains {Format, Texture, Level, FirstLayer, Layers}; the
  owner binds typed UAVs as images u# for one launch, untyped as shader
  buffers; CBVs/SRVs go through bind_stage; sampler CSOs are created for
  the launch and deleted after it.
- Owner buffers get PIPE_BIND_SHADER_IMAGE; textures with the UAV flag too.

## WHAT IS STILL UNKNOWN

- Hardware only: Asahi image stores into an RGBA8 texture created by the
  D3D12 owner and texel-buffer image stores (the D3D11 path runs both on
  the same driver).

## Checkpoint

Probe C9 (agx_d3d12_probe_cs_img.hlsl, fresh device): a table {SRV t0
(2x2 RGBA8), UAV u0 (64x64 RGBA8 texture), UAV u1 (R32_UINT buffer, 4096
elements)} + static point sampler; Dispatch(8, 8, 1). C9A PASS = the 64x64
image shows the four texels as quadrants; C9B PASS = typed[y*64+x] ==
x + 1000y.
