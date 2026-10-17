# D3D12 C10: UAV clears, DiscardResource, SetMarker

Status 2026-10-18. Rejected list DDIs that ordinary D3D12 applications call
on every frame: ClearUnorderedAccessView{Uint,Float}, DiscardResource and
SetMarker (PIX events).

Sources inspected: WDK 26100 d3d12umddi.h (PFND3D12DDI_CLEAR_UNORDERED_ACCESS_VIEW_UINT_0003 /
_FLOAT_0003: list, GPU handle in the current heap, CPU handle, resource,
values[4], rects; PFND3D12DDI_DISCARD_RESOURCE_0003; PFND3D12DDI_SET_MARKER);
the repository's D3D11 path (mesa/d3d11/agx_d3d11_ddi.cpp
ClearUnorderedAccessView{Uint,Float}11, pack_uint_bits, clear_typed:
clear_buffer, clear_texture or u_default_clear_texture since Asahi has no
clear_texture, EXP1330); Mesa asahi agx_pipe.c (clear_buffer assigned).

## WINDOWS CONTRACT

- The CPU descriptor (non-shader-visible) is valid at record time; the GPU
  handle names the same view in the bound heap. Uint: untyped views write
  values[0] to every 32-bit word, typed views the low bits of each value
  per channel. Float: typed views, the values converted to the format.
  Rects restrict the clear (textures: x/y; buffers: element ranges).
- DiscardResource makes contents undefined (keeping them is valid);
  SetMarker carries tool metadata.

## AGX/ASAHI CONTRACT

- pipe->clear_buffer(resource, offset, size, pattern, pattern size);
  textures through u_default_clear_texture (render-target clear or CPU),
  the path the D3D11 frontend runs on this driver.

## TRANSLATION

- Record: the UAV descriptor by value + values + rects (8 at most, as RTV
  clears); a float clear of an untyped view or more rects refuse with a
  list error (receipt measure-d3d12-clear-uav).
- Replay: DescriptorUav (shared with dispatch bindings) -> owner
  AgxD3d12OwnerClearUav: untyped = clear_buffer with the 4-byte word;
  typed buffer = clear_buffer with one pixel; texture = per-rect boxes over
  the view's layers. One pixel comes from util_format_description and the
  format's packer (no u_format.h inline helpers in the UMD).
- DiscardResource and SetMarker: no-ops.

## WHAT IS STILL UNKNOWN

- Hardware only: u_default_clear_texture on a D3D12 owner texture with UAV
  bind (D3D11 runs the same function).

## Checkpoint

Probe C10 (fresh device): raw buffer UAV cleared to 0x12345678 then
elements [100, 200) to 7 (C10A: all 16384 words); a 64x64 RGBA8 UAV texture
cleared blue then rect (0,0)-(32,32) red (C10B: (8,8) red, (48,48) blue).
