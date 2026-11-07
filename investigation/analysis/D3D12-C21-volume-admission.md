# C21 volume admission: companion contracts and plane RTVs

Read-only source decision2026-10-18. This complements the physical-layout design
in `D3D12-C21-volume-textures.md`; it does not implement admission or claim a
package/hardware verdict. FULL GRAPHICS. No capability, source, builder, hardware,
ledger or commit change is made by this note.

## Decision

Committed Texture3D SRVs and full-mip UAVs are a useful **partial implementation
checkpoint**. They cannot be called complete support for the currently advertised
3D graphics contract while every3D RTV remains fail-closed. There is no inspected
per-dimension switch that disables3D RTV while preserving the relevant mandatory
format features. Conversely, this is not a reason to invent new format caps or
to claim all unrelated existing FL11_0 gaps must be solved in the same change.
State exactly which volume operations are implemented and tested.

The smallest useful C21 volume graphics checkpoint should include ordinary
volume allocation/copy/SRV, typed full-mip image access, **single-Z-plane RTV
creation/clear/draw**, and exact readback of selected and untouched slices.
General legal UAV/RTV slice spans remain part of the public contract: a
single-plane/full-mip checkpoint alone does not establish their conformance.
If those spans are left refused, say so rather than describing3D as complete.

## WINDOWS CONTRACT:

Current `agx_d3d12_ddi.cpp` advertises
`D3D12DDI_3DPIPELINELEVEL_11_0` through both pipeline-support queries. Its
`CheckFormatSupport` returns0. The pinned WDK
`/Users/pavel/public_windows/.local/reference/wdk26100/d3d12umddi.h:672-692`
explicitly labels these format bits as optional-on-some-formats. There is a
RENDERTARGET bit, but no3D-specific RTV/resource-disable bit in that interface.
A zero optional answer does not remove a mandatory feature of a baseline format.
Binding/heap tier1, tiled-resources-disabled and optional UAV-load answers do
not provide such a dimension-specific exclusion either.

Microsoft's [FL11_0 format tables](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/format-support-for-direct3d-11-0-feature-level-hardware)
require Texture3D and render-target support for baseline RGBA8_UNORM and
R32_UINT. R32_UINT also requires typed UAV load/store; RGBA8 typed UAV loads are
optional at that level. Thus RGBA8 RTV and R32_UINT UAV are source-backed test
choices without enabling additional typed-UAV-load feature sets. This is not a
complete format-support matrix or a proposal to change format query bits.

Microsoft [resource flags](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_flags)
define ALLOW_RENDER_TARGET as allowing RTV creation and render-target state,
subject to the format/layout/heap constraints. There is no inspected Texture3D
exception that makes an RTV-capable color format SRV/UAV-only.

Current pinned WDK Tex3D RTV and UAV structures each contain MipSlice, FirstW,
WSize. The API's [Tex3D RTV](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_tex3d_rtv)
and [Tex3D UAV](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_tex3d_uav)
define depth-slice origin/count, with UINT_MAX meaning the remaining slices.
Bounds are against max(1,depth0>>MipSlice), using subtraction before addition.
These are W/Z slices, not format planes or independent mip subresources.
No optional cap was found that makes all nontrivial FirstW/WSize ranges invalid.

The [Microsoft functional specification, Texture3D render output5.3.7.2](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#5.3.7.2%20Pipeline%20Binding:%20RenderTarget%20Output)
describes a selected mip as2D slices for output, with renderTargetArrayIndex
selecting a relative slice; absent that output, rendering uses relative slice0.
Therefore a nonzero FirstW single-plane RTV can be tested with the existing VS/PS
path, without guessing GS emulation. Layered rendering through GS is a distinct
existing gap; current RecordDraw explicitly refuses geometry/hull/domain shaders
and the owner fixes framebuffer.layers=1. A plane test does not validate that gap.

## AGX/ASAHI CONTRACT:

Pinned Mesa `9aa1215f878b504f66159dd2ead4c7973142126e` provides an actual source
route for3D plane RTVs; no new AGX register protocol must be guessed:

- `gallium/frontends/d3d10umd/OutputMerger.cpp::CreateRenderTargetView` maps
  Tex3D.MipSlice/FirstW/WSize to pipe_surface level/first_layer/last_layer.
  Local D3D11 uses this base callback. Its UAV helper separately validates
  volume bounds against u_minify(depth0,level), not array_size.
- `gallium/drivers/asahi/agx_pipe.c::agx_resource_setup` marks
  PIPE_TEXTURE_3D as mipmapped_z; allocation uses AIL's actual physical layout.
  `agx_is_format_supported` accepts3D as a target and checks format/usage,
  excluding nonrenderable formats and RGB32 texture emulation where relevant.
- `agx_state.c::agx_set_framebuffer_state` accepts/copies pipe surfaces.
  `image_view_for_surface` preserves level and first/last Z and sets
  single_layer_view when first==last. `agx_batch_upload_pbe` then forces that
  view to PIPE_TEXTURE_2D, preserves its mip and adds its Z-plane backing base.
- The render reload path's `sampler_view_for_surface` likewise creates2D for a
  single layer; layered surface reload/spill uses2D arrays. `target_is_array`
  deliberately includes PIPE_TEXTURE_3D, and PBE can flatten internal layered
  access. These are render-target implementation paths, not proof that every
  arbitrary user3D UAV subrange already has correct size/address semantics.
- `agx_pack_texture` has a true3D texture descriptor and uses base resource depth;
  `agx_batch_upload_pbe` has per-view level/layer/count. Arbitrary3D image-view
  subset load/GetDimensions behavior needs an explicit source-derived mapping;
  a successful full-mip store alone would not establish it.

Existing owner `clear_view_layers` already iterates each selected pipe-surface
layer as a single-layer framebuffer and calls Asahi's implemented pipe->clear.
Do not call clear_render_target/clear_depth_stencil slots: prior source/history
shows those callbacks are absent on Asahi. This helper is a reusable mechanism,
not permission to leave the caller's current2D array_size bounds unchanged.

## TRANSLATION: smallest coherent volume implementation scope

1. Use the shared pre-BO layout query/actual AIL physical size. For volume
   resources retain target=3D, array_size1, base depth and requested mip count.
   Do not reuse16x16 estimates or logical voxel sums as physical allocation.
2. Preserve one subresource per mip. Support ordered full/box copies in both
   directions and volume-to-volume copies with minified width/height/depth,
   runtime row/slice pitches, Z origin, exact resource bounds and buffer spans.
   Do not decode Z with array-subresource modulo. Keep depth/stencil/multiplane
   formats outside the initial single-plane color scope.
3. Carry explicit3D SRV/image dimension through compile/bind and preserve mip
   origin/count, component mapping and all visible Z slices. Refuse mismatched
   shader/view dimensions. Derive UAV FirstW/WSize semantics before claiming
   partial-image-view support; keep known full-mip tests as limited evidence.
4. Add Tex3D RTV CPU metadata at the existing descriptor boundary; normalize
   WSize remaining sentinel and require a fitting positive slice span at the
   selected mip. Pass first/last plane through replay. Owner RTV clear/draw
   validation must use minified volume depth rather than array_size1; use the
   actual pipe_surface route above. A valid single-plane view at nonzero Z and
   nonzero mip must not be admitted only for a test-specific offset/size.
5. Test clear of finite spans and whole-remaining spans using the per-plane
   framebuffer helper; test ordinary VS/PS draw into a nonzero plane. Capture
   actual Gallium surface/FB state offline and prove untouched neighboring
   planes. Keep GS/layered drawing limits explicit, with no new capability
   advertisement implied by this milestone.

## Deterministic gates and smallest hardware evidence

Before any volume allocation hardware, prove physical sizing and logical-vs-
physical metadata invariants offline. Extract actual DDI/replay/owner code to
verify mip/Z/row/slice pitches, bounds, descriptor spans and pipe-surface target,
level and plane. Include malformed volume subresource indices and finite range
overshoots, rather than silently clamping. Keep format admission independently
consistent with Windows and Asahi usage checks.

Separate fresh-device evidence can use a small RGBA8 volume with unique values
per mip/Z/XY, copy upload/readback, SRV sampling, then clear and draw to one
nonzero-mip/nonzero-Z plane and compare every selected and untouched texel.
R32_UINT full-mip UAV write/load/GetDimensions can verify the baseline integer
image path independently. No experiment is justified to discover a documented
3D RTV field, format bit or size rule. Hardware only checks the already-derived
addressing/rendering translation; the parent owns preregistration/recovery.

## WHAT IS STILL UNKNOWN:

Actual native layout query integration and volume allocation/copy metadata are
still work in progress. Arbitrary user3D UAV slice-span load/dimension semantics
and GS/layered graphics remain unvalidated; do not infer them from internal PBE
flattening. No source evidence was found for omitting all3D RTVs while calling
the present FL11_0-based volume feature complete. Single-plane RTV implementation
has a supported source path and can close that concrete blanket-refusal gap.
