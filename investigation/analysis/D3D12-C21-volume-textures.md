# D3D12 C21: coherent volume resource, copy and view contract

Status: read-only source/design checkpoint, 2026-10-18. C19 source `53e02537`
is pinned/building; parent and the C20 agent own subsequent production work.
Only this note is written by the C21 lane. No C21 implementation, builder,
package, hardware run, commit or ledger verdict is established.
Classification: FULL GRAPHICS frontend resource/copy/view semantics.

## Sources inspected

Line numbers describe the C19/C20 working tree and may move.

- `drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:575-606`: resource metadata and
  TextureBytes; `:615-635`: allocation-info response; `:905-953`: committed
  texture creation; `:1134-1160`: 2D-only UAV descriptor creation;
  `:1239-1305`: SRV descriptor paths; `:1860-1895`: whole-resource copies;
  `:2100-2210`: subresource/footprint/box copy recording.
- `drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp:321-354`: native texture
  creation; `:733-758`: GPU region copies; `:787-843`: buffer/texture copies;
  `:1053-1083`: typed UAV image construction; `:1128-1160`: SRV view creation.
  `agx_d3d12_windows.h` contains owner texture-copy/UAV/SRV records.
- Pinned `/Users/pavel/public_windows/.local/reference/wdk26100/d3d12umddi.h`:
  `:396` RT_TEXTURE3D=4; `:450-462` resource dimensions and layout;
  `:890-895` Tex3D SRV; `:916` RD_TEXTURE3D=4;
  `:1165-1171` Tex3D UAV; `:1546-1554` pitched physical footprint;
  `:18934` D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION=2048.
- Microsoft [volume subresource indexing](https://learn.microsoft.com/en-us/windows/win32/direct3d12/d3d12calcsubresource),
  [Tex3D SRV](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_tex3d_srv),
  [Tex3D UAV](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_tex3d_uav),
  [subresource footprint](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_subresource_footprint),
  and [placed footprint](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_placed_subresource_footprint).
- Local `drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp:480-532`:
  3D UAV FirstW/WSize validation against u_minify(depth0,mip).
- Asahi/Mesa source root `/Users/pavel/public_windows/.local/reference/mesa`:
  - `src/gallium/frontends/d3d10umd/Resource.cpp:131-179`: 3D target has
    array_size 1; volume boxes use minified depth;
    `Shader.cpp:1236-1241`: 3D SRV uses mip interval and default-template layers.
  - `src/gallium/drivers/asahi/agx_pipe.c:136-160`: layout setup sets
    mipmapped_z for PIPE_TEXTURE_3D and depth_px=depth0*array_size;
    `:386-428`: modifier/compression selection;
    `:527-586`: AIL layout and native BO allocation from layout.size_B;
    `:925-1025`: texture-map stride/layer_stride and per-Z detiling;
    `:2120`: max_texture_3d_levels12 (2048 edge);
    `:2215-2270`: target/usage format predicate.
  - `src/asahi/layout/layout.c:198-214`: layer alignment and size_B;
    `:329-386`: miptree initialization/full-chain allocation for requested
    levels > 1 (major axis includes depth for volumes);
    `layout.h:203-219`: layer_stride and mip-offset addressing;
    `:560-568`: selected APPLE_GPU_TILED modifier maps to AIL_TILING_GPU.
  - `src/gallium/drivers/asahi/agx_state.c:614-616`: 3D descriptor dimension;
    `:640-739`: sampler descriptor first/last mip, 3D base depth and address;
    `:1119-1250`: image PBE level, first layer and viewed layer count;
    `:2741-2754`: image-load sampler descriptor conversion.
  - `src/gallium/auxiliary/util/u_helpers.c:533-579`: image-to-sampler view;
    `util/u_sampler.c` default template; `pipe/p_state.h` descriptor field widths.
  - `src/asahi/libagx/texture.cl:12-62`: textureSize minifies all three
    nonarray 3D dimensions; `:79-88`: visible mip count.
    `src/asahi/compiler/agx_nir_lower_texture.c:511-539`: robustness handling;
    `agx_compile.c:1120-1185` and `:1210-1260`: typed image load/store dimensions.
- `drivers/apple-agx/mesa/dxbc/dxbc_to_nir.c`: tex_shape already represents
  nonarray GLSL_SAMPLER_DIM_3D,3 coordinates; typed UAV lowering already carries
  3D image dimension. C19 immutable SRV dimension metadata currently groups
  noncube dimensions into OTHER; a volume admission needs an explicit 3D class.

No external source code is proposed for copying. Reuse linked AIL/Gallium
interfaces and write the translation at its owning frontend/winsys layer.

## WINDOWS CONTRACT:

A Texture3D has width, height and depth, with array size 1. At mip m its extent is
(max(1,W>>m),max(1,H>>m),max(1,D>>m)). A mip's entire volume is one subresource;
Z slices are not separate array subresources. For the initial single-plane
formats, subresource=mip and subresource>=MipLevels must refuse. Do not decode
an excessive index with modulo into a valid mip and a fictitious array layer.

The pinned DDI provides:

| Interface | Exact fields / interpretation |
| --- | --- |
|CreateResource|Width UINT64, Height UINT, DepthOrArraySize UINT16, MipLevels UINT16; for RT_TEXTURE3D the depth field is D, array count is 1|
|Tex3D SRV|MostDetailedMip UINT, MipLevels UINT, ResourceMinLODClamp FLOAT; all Z slices of the selected mip interval|
|Tex3D UAV|MipSlice UINT, FirstW UINT, WSize UINT; WSize UINT_MAX means all slices from FirstW to that mip's end|
|Physical pitched footprint|Format, PhysicalWidth/Height/Depth (block dimensions), Pitch and SlicePitch; both pitches supplied by the runtime|

SRV finite mip spans must fit by subtraction, with UINT_MAX meaning remaining
mips. UAV slice bounds use Dm=max(1,D>>MipSlice), not base depth or array count.
FirstW<Dm and finite WSize>0 must fit Dm-FirstW; the sentinel expands to that
remaining value. Preserve C18 SRV component mapping and C17 logical namespaces.
Nonzero/NaN SRV MinLOD remains unsupported and must fail closed on the new path.

For buffer/texture copies, use runtime Pitch/SlicePitch and physical footprint
dimensions. Texture and buffer coordinates refer to their respective source/
destination boxes, not the same offsets. For a checked uncompressed block1
region, the last addressed buffer byte span is:

    (depth-1)*SlicePitch + (height-1)*Pitch + width*bytesPerTexel

Its base additionally includes selected buffer footprint offset and the source
or destination buffer point: z*SlicePitch+y*Pitch+x*bytesPerTexel. Multiplication
and sums require checked UINT64 arithmetic before comparing against buffer
Bytes-offset. SlicePitch is not recomputed from copied box height: a sub-box
can retain the larger footprint's slice pitch. Initial formats have block
width/height/depth 1; compressed/planar formats require separate block-address
translation and stay outside the first implementation.

## AGX/ASAHI CONTRACT:

PIPE_TEXTURE_3D uses width0=W,height0=H,depth0=D,array_size=1,last_level=L-1.
Allocation layout has mipmapped_z=true. Hardware sampling sees a 3D descriptor
with resource base depth; visible first/last levels select the SRV interval.
Texture-size queries add first_level then minify X/Y/Z, whereas the array count
of a 2D array is not minified. A 3D SRV therefore cannot be modeled as a 2D-array
view or checked against array_size as its available Z extent.

CPU texture maps return transfer.stride and transfer.layer_stride. Asahi
performs staging/detiling for every requested Z slice and reports strides for
the mapped box. The owner must keep its existing row/Z loop, using those
returned texture strides and the runtime buffer pitches independently. Never
invent the GPU tiling, physical slice stride or mip offsets in the frontend.

For typed images, Asahi builds both a load sampler descriptor and a PBE store
descriptor. PBE uses selected mip, first layer and viewed layer count. The load
sampler retains resource depth for 3D; this difference matters for partial UAV
slice views and is a remaining blocker below. Local D3D11 supplies useful
bounds but does not prove arbitrary partial-W load/size semantics on this
Asahi path.

## Physical allocation versus advertised runtime span

These quantities must be distinguished:

1. Native allocation: Asahi resource_create calls ail_make_miptree and allocates
   the BO from layout.size_B. The current owner caller's bytes argument does
   not reserve or limit that BO. KMD allocation/handle follows the actual native
   allocation. No physical underallocation is established by this note.
2. Advertised/runtime metadata: TextureBytes feeds allocation-info and is stored
   in texture.Bytes/heap.bytes/resource.bytes. This estimate can differ from
   the native layout size; its truthfulness is a separate contract.
3. Copy/view extents: legal texel/mip/Z ranges and buffer footprint byte ranges.
   These must not be derived from BO padding or from advertised byte count alone.

AIL may allocate a full mip chain when requested levels > 1, page-align each
physical layer and multiply by base depth. A logical sum of minified volume
texels is not its physical layout. C20 only fixes typed buffer SRV byte extent
(R8/R16); that lane adds no texture allocation-size query.

One concrete offline metadata counterexample was evaluated against the current
host libasahi_layout.a with AIL_TILING_GPU,RGBA8_UNORM,W=H=4,depth_px 64,
requested levels 2,renderable=true,uncompressed. For a 2D array, AIL returns
actual levels 3,layer_stride 16384,size 1048576, while the current TextureBytes
formula advertises 131072. With mipmapped_z=true it returns actual levels 7
and the same physical size. Temporary reproducer `/tmp/agx-c21-layout.c`
links the existing host layout library; no hardware was used. Source modifier
selection permits this GPU-tiled uncompressed small image, and resource_setup
sets renderable=true. This confirms a formula/layout metadata mismatch for
that source-defined template; it does not prove an observed runtime failure
or tie anything to the C19 checkpoint. No existing 2D fix is made here.

Before new 3D admission, derive a truthful layout query/upper-bound contract in
the owner/native Asahi layer. A pure query must use the same format, target,
modifier/compression, sample, alignment and full-miptree decisions as creation;
prefer reusing linked AIL and the actual selector rather than copying formulas.
CheckResourceAllocationInfo runs before creation, so a post-create BO size alone
cannot repair its response. After creation, confirm native extent and advertised
span under the chosen contract. Asahi resource_get_param currently has stride,
offset and modifier, not a size query; do not assume one exists. Resource handle
export reports layout.size_B but is post-create and Windows export support must
be checked. Exact helper placement is an implementation blocker to resolve
before code, not a reason to guess or launch hardware.

## Smallest coherent translation and owner fields

Start with committed DEFAULT-heap, single-plane, uncompressed, single-sample
volume textures with the supported opaque/native layout;
use source-supported format/target/usage predicates and the documented 2048 edge
bound shared by WDK and Asahi. Do not advertise a new capability bit from a
successful small volume. Native Asahi rejects individual layouts >= 4 GiB; preserve
that real allocation failure rather than fabricate a narrower dimension cap.

- Resource metadata: retain explicit dimension, W/H/baseDepth/mipCount, with
  array layers 1 for 3D. Add a separate depth field; do not overload layers with
  both meanings and then treat Z as an array slice in one consumer.
- Owner creation: add a volume-capable template/create path with depth0=D and
  array_size 1; no presentation/share special path. Resolve truthful advertised
  size and native layout extent as above. Do not admit user CPU texture Map,
  cross-adapter row-major layout, or placed volumes through this addition.
  Existing CheckSubresourceInfo only describes buffer subresource0; extending
  that CPU mapping interface is separate from internal Asahi transfer maps.
- Copy metadata: retain mip, true Z origin, width/height/depth and runtime
  footprint pitches. Existing layer fields may be renamed/generalized to
  Z-or-array-origin only if every consumer branches on resource dimension.
- CopySide admits only real texture dimensions, never treats a 3D texture as a
  buffer because it is not 2D. Validate volume subresource<mipCount before use.
  Full-volume depth is Dm when no box is supplied. Whole-resource copy iterates
  mips with each mip's own minified depth. A 3D-to3D region copy forwards XYZ
  coordinates and depth to Gallium resource_copy_region with volume bounds.
- SRV metadata: preserve a volume view class, mip span and mapping; no layer
  subset. Select PIPE_TEXTURE_3D, with first_layer 0 and a default full-resource
  layer span consistent with Gallium's default sampler template. Validate Z
  against depth semantics, not array_size 1. Extend immutable shader dimension
  metadata to distinguish 3D and reject mismatched 2D/cube bindings.
- Descriptor budget: C19's 64-byte CPU descriptor is already full. Generalize
  the existing cube classification field to an explicit texture-view kind
  (inherit 2D/cube/volume) rather than append a field and exceed descriptor stride.
- UAV initial slice: support the complete selected mip only, FirstW 0 and
  WSize=Dm or the all-remaining sentinel. Build image level=mip,first_layer 0,
  last_layer=Dm-1. Refuse partial-W views explicitly until the load/size/store
  contract is resolved. R32_UINT is a mandatory typed load/store format and a
  suitable first exact word oracle; no additional typed-load format group is
  advertised. C18 swizzles apply only to SRVs.

Ownership: DDI validates dimensions, subresources, footprints and views; owner
constructs native templates and translates boxes/strides; Asahi owns layout,
BO allocation, maps, sampling/images and GPU operations; KMD owns actual memory,
residency, scheduling, interrupts and recovery. Mu/m1n1 state is unchanged.

## Precise remaining blockers

- Truthful pre-create allocation-info versus native AIL extent, including the
  confirmed existing 2D metadata mismatch. Native BO allocation is independent
  and must not be called undersized without separate evidence.
- Partial 3D UAV load/GetDimensions bounds: image sampler descriptor uses full
  resource depth while PBE honors viewed layer count. Do not assume FirstW/
  WSize become view-local size merely because the D3D11 helper fills them.
  Derive correction in the owning layer or retain explicit refusal.
- Resource/copy dimension plumbing: every 2D-only branch, whole-resource copy,
  full-copy depth default and transfer box currently needs an explicit volume
  interpretation. No enum-only admission is coherent.
- Initial typed format and nonzero MinLOD remain bounded to proven contracts;
  RTV 3D/depth formats/placed 3D/compression/planar layouts are separate work.

## Meaningful offline invariants

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Modulo subresource decoding can
alias an invalid index to a valid mip; array_size 1 can refuse a valid Z slice;
base-depth bounds can admit an invalid higher-mip slice; copied-box height can
corrupt buffer slice addressing; SRV/UAV size can expose the wrong mip depth.

1. Compile actual resource-size/template construction against linked AIL and
   compare the advertised contract to native layout results over sparse,
   rectangular and depth-dominant mip chains. Include the 4x4x64 source case,
   partial requested chain, one-level resource, checked arithmetic limits,
   rejected formats and failure cleanup. Do not duplicate a guessed layout in
   the test oracle.
2. Execute real CopyTextureRegion/CopyResource recording and owner copy with
   fake pipe map/copy callbacks. For W/H/D=8/4/4, require mip 0 extent 8/4/4 and
   mip 1 extent 4/2/2; subresource 2 is invalid when only 2 mips exist. Verify nonzero
   source/destination XYZ boxes, all-face versus volume distinction, empty-box
   behavior and UINT64 overflow without command submission.
3. Buffer pitch tests must use padded runtime Pitch/SlicePitch independently
   of texture transfer stride/layer_stride and of copied sub-box height.
   Fill guard bytes around rows/slices and prove exact addressed bytes for
   upload and readback. Check last-fitting span and first overflowing byte.
4. Extract actual SRV/UAV creation, copy/replay, sampler/image view construction.
   Verify minified mip depth, all-remaining sentinels, finite invalid counts,
   zero/NaN MinLOD refusal, component mapping, 3D target and explicit partial-W
   refusal. Existing2D/cube descriptors and 64-byte stride remain valid.
5. Compile self-authored FXC Texture3D and RWTexture3D shaders. Verify real
   NIR 3D dimensions, 3-component coordinates, txs 3-dimensional result and typed
   image intrinsics, then validate through Asahi preprocessing. Exercise
   shader-owned metadata lifetime and dimension mismatch refusal.
6. Retain current D3D12/C17/C18/C19/C20 tests. Parent owns full-suite comparison,
   syntax/stack/probe gates, source snapshots and commits.

## Smallest later hardware checkpoints

Use fresh isolated devices and separate receipts. Initial volume RGBA8 is 4x4x4
with mip 1=2x2x2; populate both mips with a unique exact texel pattern:

    RGBA(x,y,z,m) = (17+19*x, 31+23*y, 43+29*z, 151+37*m)

All channels fit 8 bits. Obtain actual footprints via GetCopyableFootprints and
record runtime Pitch/SlicePitch/offset values; do not synthesize an unverified
slice pitch. Comparison walks the returned footprint for each mip/Z/row.

- C21A: committed volume upload->readback for both mips, all texels exact with
  untouched padding guards. This isolates creation/subresource/pitch/Z copy
  before shader sampling.
- C21B: Texture3D point SampleLevel 0 through a selected mip SRV into the existing
  64x64 RGBA8 output. Use four fixed Z bands for mip 0 and two for mip 1; select
  exact texel centers (x+0.5)/Wm,(y+0.5)/Hm,(z+0.5)/Dm and compare every RGBA
  pixel against the pattern. GetDimensions must report the selected view's
  4/4/4 or 2/2/2, not array depth 1. Keep SRV mapping identity for this checkpoint.
- C21C: R32_UINT RWTexture3D full-selected-mip UAV; write each mip texel
  value=x+16*y+256*z+65536*m, then ordered readback. Verify both mips and every
  Z slice; untouched resource mips remain sentinels. GetDimensions must expose
  minified 3D extent; no partial W promise is made.
- C21D only if the initial implementation includes GPU volume-region copies:
  copy an interior XYZ box between initialized volumes with different source/
  destination Z origins, then verify destination patch and all untouched texels.

The source-defined mapping is implemented/tested offline first. Hardware only
verifies native layout/maps and actual 3D sampling/image behavior on J313.

## Experiment boundary and recovery

C19/C20 remain parent-owned active work. Before any C21 package, record the
source-derived mapping, remaining hardware unknown, exact artifact hashes and
known-good full-owner recovery in a new experiment BEFORE. Collect evidence
before exact package cleanup and GPU-visible inert recovery. This note is not
permission to build/install/launch and establishes no hardware verdict.

## Bounded follow-up: callable layout-query entry and minimal extension

One additional targeted source pass inspected current pipe_screen callbacks and
Asahi assignment sites; no repeated layout comparison was performed.

| Candidate | Current source result | Allocates a BO / usable before creation? |
| --- | --- | --- |
|screen.resource_create_unbacked|Declared in pipe/p_screen.h:656-663 with an explicit no-backing-memory contract; never assigned by current Asahi agx_pipe.c and not added by the Windows winsys/overlay|No callable implementation here; do not call the NULL pointer|
|screen.get_resource_info / resource_get_info|No such assigned query in current Asahi or relevant pipe_screen API|Unavailable|
|screen.resource_get_param|Assigned at agx_pipe.c:2480; implementation:299-328 supports stride, mip offset, modifier and plane count, then returns false for other parameters|Post-create only; no allocation-size result|
|screen.resource_get_handle|Assigned:2479; implementation reports handle.size=layout.size_B at:289-293|Requires an existing resource/BO and may export a handle; not a pre-create no-allocation query|
|screen.resource_create_with_modifiers|Assigned:2481; existing implementation:483-586 calls ail_make_miptree and agx_bo_create|Allocates native BO; not an unbacked query|
|public ail_make_miptree|Declared in asahi/layout/layout.h:379; implementation layout.c:329-386 updates caller-owned ail_layout only|Callable, no BO allocation; exact only with the same validated inputs and modifier decisions as native construction|

The smallest lower-level callable sizing primitive is therefore
`ail_make_miptree`, not an existing Gallium unbacked-resource callback. A direct
owner call with guessed modifier/compression/bind initialization would still
duplicate native constructor policy and is not an acceptable resolution.

Smallest coherent extension: expose a narrow owner/native layout query backed
by one shared pre-BO initialization routine in Asahi's existing resource
construction translation unit. Factor the existing pre-allocation prefix into
that routine so resource_create_with_modifiers and the query execute the same
code, rather than implement a second constructor. The prefix must preserve:

- copied pipe_resource template and base.screen;
- modifier selection through actual agx_select_modifier_from_list when an
  explicit list is supplied, otherwise actual agx_select_best_modifier, including
  device debug flags and bind/usage constraints; reject MOD_INVALID;
- existing inferred PIPE_BIND_SHADER_IMAGE when modifier is uncompressed and
  templ.depth0==1, before constructing layout.writeable_image;
- nresource.mipmapped=(requested last_level>0), while layout.levels may expand;
- exact agx_resource_setup fields: tiling from modifier, compressed from modifier,
  mipmapped_z=(target==PIPE_TEXTURE_3D), format, width0,height0,
  depth_px=depth0*array_size, sample_count=max(nr_samples,1),
  levels=requested last_level+1, writeable_image from the effective bind,
  renderable=true;
- actual ail_make_miptree full-chain/layer alignment, compression and sparse
  metadata initialization and size_B final alignment, plus the existing
  layout.size_B>=2^32 refusal.

A portable query result need expose only required allocation bytes/alignment,
chosen modifier and any values needed to prove creation matches the query.
For offline verification, compare actual expanded layout levels, level offsets,
layer_stride_B, metadata_offset_B and compression_layer_stride_B as well.
Compression metadata is included by actual AIL initialization; sparse-table
metadata has separate fields and must not be invented as an additional size
term. The first volume slice excludes sparse resources. No public ABI should
expose a borrowed agx_resource/ail_layout pointer.

The query path stops before agx_bo_create and never reserves KMD memory. The
real creation path continues unchanged to allocate its native BO from the
shared result. Preserve requested pipe_resource.last_level separately from the
expanded AIL layout level count. The DDI may then derive its appropriately
aligned runtime ResourceDataSize from the proven result and verify it against
actual creation; that policy still needs explicit tests. A post-create native
extent accessor can verify this without pretending it retroactively repaired
the earlier allocation-info callback.

This identifies the required extension and its exact shared inputs; no native
helper, Asahi refactor, allocation metadata correction or 3D admission is
implemented by this note. Parent decides whether the small shared-prefix
extension is accepted before implementation. Existing 2D metadata correction
remains a separately reviewable defect, with no observed physical-underallocation
or hardware-failure claim.
