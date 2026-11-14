# D3D12 C19: cube and cube-array SRVs over existing 2D arrays

Status: offline implementation, 2026-10-18. Parent pinned C18 package source
snapshot `56fe3977d3909012636dc87aaff9f0d177526574` (678 files), then authorized
C19 production/tests implementation. Parent owns probe/builder/hardware,
commits and ledgers. Classification: FULL GRAPHICS view/texture translation,
with existing KMD/firmware contracts unchanged.

## Sources inspected

References describe the current C18 working tree; line numbers may move.

- Pinned `$HOME/public_windows/.local/reference/wdk26100/d3d12umddi.h`:
  `:897-904` exact `D3D12DDIARG_TEXCUBE_SHADER_RESOURCE_VIEW`;
  `:911-918` `D3D12DDI_RD_TEXTURECUBE=5`;
  `:921-937` common SRV header, mapping field and TexCube union arm.
- Microsoft [TextureCube SRV](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_texcube_srv)
  and [TextureCubeArray SRV](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_texcube_array_srv):
  mip intervals, face origin, cube count and separate MinLOD semantics.
- Microsoft [D3D11.3 functional specification, TextureCube 5.3.8](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#5.3.8%20TextureCube):
  six square faces per cube, view face span and mip subset. Use the pinned WDK
  for the current D3D12 ABI; historical online ResourceBinding text omits a
  TexCube field and must not override the current header.
- `drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:589-606` TextureBytes;
  `:905-953` committed 2D-array allocation;
  `:1242-1266` texture SRV path currently accepts only RD_TEXTURE2D;
  `:2059-2170` CopySide/CopyTextureRegion decomposes array subresources.
- `drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp:321-351` creates
  PIPE_TEXTURE_2D_ARRAY for multiple layers; `:787-842` buffer/texture copy;
  `:733-758` GPU texture-region copy; `:1108-1134` sampler view construction
  currently uses `templ.target = texture->target`.
- `drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp:270-319` local cube-view
  conversion, including format-family/mip/face checks. Its `count==6` target
  heuristic is a precedent, not sufficient proof for a one-element cube array.
- `drivers/apple-agx/mesa/dxbc/dxbc_to_nir.c:1714-1727` cube shape/coordinates;
  `:1758-1774` NIR sampler_dim/is_array/texture_index;
  `:1807-1865` sampling and explicit LOD sources.
- Asahi root `$HOME/public_windows/.local/reference/mesa`:
  `src/gallium/drivers/asahi/agx_pipe.c:2048` independent sampler-view targets;
  `src/gallium/drivers/asahi/agx_state.c:614-624` distinct descriptor dimensions
  for 3D/CUBE/CUBE_ARRAY; `:640-739` view swizzle/mip/layer/address packing;
  `:760-799` preserves the view template.
  `src/asahi/compiler/agx_nir_lower_texture.c:253-299` cube-array fourth
  coordinate, array layer conversion/clamp;
  `src/asahi/compiler/agx_compile.c:875-876` distinct shader CUBE/CUBE_ARRAY
  instruction dimensions. Gallium `p_state.h:512-524` bounds view fields.

## WINDOWS CONTRACT:

A cube SRV views six square 2D faces; a cube-array SRV views one or more groups
of six faces. Resource allocation remains an ordinary D3D12 TEXTURE2D array.
An SRV selects face origin, number of cubes, mip origin/count and component
mapping. Pixel/sample cube selection is relative to the view, not the entire
backing allocation. The shader's declaration distinguishes TextureCube from
TextureCubeArray and supplies three or four sample coordinates respectively.

The exact current DDI TexCube layout is five DWORD-sized fields, in order:

| Offset | Pinned WDK field | Meaning |
| --- | --- | --- |
|0|UINT MostDetailedMip|First resource mip visible to the view|
|4|UINT MipLevels|Visible mip count; UINT_MAX means remaining levels|
|8|UINT First2DArrayFace|First backing array face|
|12|UINT NumCubes|Number of six-face cube groups|
|16|FLOAT ResourceMinLODClamp|Independent minimum LOD property|

The API has distinct TextureCube and TextureCubeArray view dimensions, but the
pinned DDI combines them into RD_TEXTURECUBE/TexCube; it provides no explicit
cube-array boolean. In particular NumCubes=1 is not proof of a nonarray shader
contract. Do not infer the shader dimension solely from that count.

Required checked normalization, performed before narrowing to Gallium fields:

1. Backing resource must be a single-sample 2D array with square faces and a
   supported typed view format. Preserve C18 component mapping unchanged.
2. Require firstFace < resourceLayers and cubeCount > 0. Before multiplying,
   require cubeCount <= (resourceLayers-firstFace)/6. Only then compute
   faceCount=cubeCount*6 and lastFace=firstFace+faceCount-1. UINT_MAX cubeCount
   is not documented as an all-remaining sentinel; refuse an overflowing or
   nonfitting finite count. Do not clamp a finite invalid count silently.
3. Require firstMip < resourceLevels. UINT_MAX mip count means
   resourceLevels-firstMip; otherwise require count > 0 and
   count <= resourceLevels-firstMip before computing the inclusive last mip.
4. Verify face and mip values fit Gallium's 16-bit layer and 8-bit mip fields
   before casts. Existing 2D allocation limits make legal mip values small,
   but the view checker should not depend on a narrowing cast to validate them.
5. Nonzero/NaN MinLOD is not implemented by this work. Reject it on the new
   cube path; do not copy it into an unused field and claim support. Preserve
   the separate existing 2D MinLOD issue for its own correction.

No inspected primary source requires firstFace to be a multiple of six. Do not
invent that restriction: the D3D11.3 specification expresses a fitting span of
six faces per cube after the selected origin. A fitting non-six-aligned origin
is a deterministic bounds test unless a further primary rule excludes it.

## AGX/ASAHI CONTRACT:

Allocation layout and sampling interpretation are separate. The existing
backing resource may remain PIPE_TEXTURE_2D_ARRAY. Asahi's sampler-view target
capability allows a distinct view target; agx_pack_texture uses that target to
emit the hardware dimension, derives the address from first_layer and divides
the viewed face span by 6 for cube depth. It retains first_level/last_level and
C18's composed channel mapping.

CUBE and CUBE_ARRAY are distinct AGX descriptor and shader-instruction
dimensions. The shader translator already emits sampler_dim=CUBE and is_array
according to the declaration. Array sampling consumes coordinate.w, converts
it to a layer and clamps it against texture-size depth. Thus a one-element
TextureCubeArray must not become a nonarray binding simply because its view
contains six faces. Hardware probing is not a substitute for this source-level
dimension agreement.

## TRANSLATION:

Add cube classification to the CPU SRV metadata and preserve first face,
face count, first mip, mip count and existing encoded component mapping through
copy/replay. Keep backing allocation target unchanged. At owner view creation,
select the independent Gallium view target coherently with the shader's declared
texture dimension for that physical binding/stage.

Preferred deterministic route if no primary source proves a count-only target
rule: capture per-binding cube/nonarray versus cube/array metadata from the
finalized NIR before create_*_state consumes it. NIR texture instructions expose
sampler_dim/is_array and direct texture_index; gather per-stage immutable
metadata in the compiled compute/VS/PS objects. Require consistent dimensions
for each used slot; validate indices and refuse conflicts. Pass that stage's
metadata to bind_stage. Select CUBE for used nonarray cube operations and
CUBE_ARRAY for used array cube operations, including NumCubes=1. Do not extend
the C17 semantic namespace map with a root-dependent pointer or infer arrayness
from backing allocation size. Unused shader resources do not require invented
sampling state. Parent can choose an equally proven smaller route after source
review, but not a hardware guess about mismatched dimensions.

A nonarray shader must see one cube. An array shader may see one or more cubes.
A cube-class descriptor paired with an incompatible shader view dimension must
refuse before sampler-view creation. Keep old 2D/buffer behavior unchanged.
This field propagation and matching is one coherent cube-view contract; do not
bundle 3D, MinLOD implementation or format-capability changes into it.

Ownership: DDI validates view bounds/format and stores metadata; shader
compilation stores immutable dimension requirements; replay matches physical
stage slots and copies view metadata; owner constructs Gallium views; Asahi
packs descriptors and generates sampling instructions. Existing KMD owns
residency/scheduling/interrupt/recovery; Mu/m1n1 state is unchanged.

## Existing upload/copy contract reused

For resource mip count L, subresource index is mip + face*L. Current DDI copy
recording decodes level=subresource%L and layer=subresource/L. Buffer-to-texture
upload uses a placed footprint with the mip's minified width/height; each face
can be uploaded separately with depth 1. Owner CopyTexture bounds level against
last_level and X/Y against max(1,base>>level), then layer/depth against array_size;
it honors mapped texture transfer stride/layer_stride and buffer Pitch/SlicePitch.
It checks the full buffer span using subtraction before copying.

Use separate face/mip uploads for the first probe, with 256-byte row pitch and
512-byte-aligned placed offsets, keeping the texture data, barriers and readback
on the existing validated 2D path. No new cube-specific allocation/copy mechanism
is required. Parent must verify actual copy packets offline; source-level reuse
does not by itself establish a hardware verdict for cube sampling.

## Deterministic regression plan

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A valid cube descriptor is
currently refused as a non-2D view, or would be sampled as a 2D array if only
admission were changed. Range overflow can select unrelated faces; NumCubes=1
can erase the shader's array dimension.

- Extend the C18 real-function harness: compile actual CreateShaderResourceView,
  actual descriptor/copy/replay metadata and complete bind_stage; capture the
  actual create_sampler_view template. RED must reproduce current refusal or
  wrong target. Observe exact format/swizzle/target/first-last layer/level.
- Positive boundaries: first 0/count 1, first 6/count 1, count 2 over 12 faces,
  last fitting 6-face span, finite mip subset and all-remaining mip sentinel.
  Include TextureCubeArray with one element and both PS/CS stage metadata.
- Negative boundaries: zero cube/mip count, firstFace==layers,
  cubeCount>remaining/6, UINT_MAX and multiplication-overflow candidates,
  firstMip==levels, mipCount>remaining, non-square backing faces, multisampling,
  unsupported dimension/metadata conflict and nonzero/NaN MinLOD.
- Verify a fitting firstFace 1 view over at least 7 faces without an invented
  modulo-six check, subject to any later pinned primary specification finding.
- Compile self-authored FXC cube and cube-array shaders with SampleLevel and
  GetDimensions. Validate the real DXBC->NIR path, expected cube is_array and
  coord count, plus agx_preprocess_nir. Assert one-element array remains array.
- Extract the production copy recording and verify mip+face*L maps to exact
  owner level/layer/footprint offsets. Reuse existing copy tests without adding
  a second implementation of cube upload.
- Retain C17 namespace and C18 swizzle suites, descriptor<= 64 and both stack
  frames<= 16384. Parent runs full host suite once before commit/snapshot.

## Smallest fresh-device hardware oracles

Use one 12-face square RGBA8 resource, 2 mips (e.g. 4x4 and 2x2), distinct constant
color per face/cube/mip, uploaded through existing 2D subresources. Use explicit
SampleLevel 0, point sampler and zero MinLOD; return sampled RGBA unchanged.
For face f in [0,5], cube c in [0,1] and resource mip m in [0,1], define exact bytes:

    R = 17 + 23*f
    G = 31 + 67*c
    B = 43 + 89*m
    A = 151 + 7*f + 11*c + 13*m

All values fit 8 bits and uniquely expose face, cube and mip. Each 64x64 output uses the parent's implemented 3x2 face grid:
face=floor(3*(x+0.5)/64)+(y>=32 ? 3 : 0), matching SV_Position at pixel centers,
and samples exact center axis directions, in order +X,-X,+Y,-Y,+Z,-Z. Constant axes avoid seams, interpolation and orientation
ambiguity. Expected bytes come from the formula for every pixel; require
4096 pixels * 4 channels exact and print first mismatch.

| Checkpoint | View / shader | Expected cube/mip |
| --- | --- | --- |
|C19A|firstFace 0, NumCubes 1, firstMip 0; TextureCube|c0,m0|
|C19B|firstFace 0, NumCubes 1, firstMip 0, MipLevels 1; TextureCubeArray index 0|c0,m0|
|C19C|firstFace 6, NumCubes 1, firstMip 1, MipLevels 1; TextureCubeArray index 0|c1,m1|
|C19D|firstFace 0, NumCubes 2, firstMip 1, MipLevels 1; TextureCubeArray index 1|c1,m1|

Each checkpoint uses a fresh isolated device and its own
AGX_D3D12_C19A/B/C/D: PASS|FAIL receipt. C19C is the discriminating one-element
cube-array plus nonzero face/mip origin case. Keep C19A nonarray, C19B one-element array, and C19D
multiple-array controls so a target mistake cannot pass by sampling cube 0.
Use existing identity component mapping and a legacy/simple root signature;
C17/C18 remain controls. Do not change format/capability flags or firmware.

## WHAT IS STILL UNKNOWN:

Before implementation: exact storage/transport of shader-derived dimension
metadata must be chosen and verified offline, including GetDimensions and
one-element arrays. Cube-array versus nonarray descriptor/instruction
compatibility cannot be assumed solely from NumCubes; source-derived matching
above resolves it without hardware guessing. After offline gates, hardware only
verifies the designed face/address/mip sampling on J313. No MinLOD behavior or
3D resource behavior is admitted by these checkpoints.

## Experiment and recovery boundary

C18 snapshot is pinned as above. Later C19 build/package/launch needs
its own BEFORE hypothesis, hashes and recovery artifact and ACTUAL verdict.
Use the established full-owner profile, evidence-first exact package cleanup
and inert GPU-visible recovery. This note does not modify the compact GPU
state or authorize a launch.


## Implemented source and verification checkpoint

`dxbc_resources.h` defines compact UNUSED/OTHER/CUBE/CUBE_ARRAY classifications
for 128 direct physical SRV slots. `dxbc_nir_srv_dimensions` inspects actual owned
finalized NIR before Gallium shader-state creation consumes it; it validates
direct texture sources, slot bounds and conflicting dimensions and clears output
on refusal. Compute/VS/PS compiled objects own the 128-byte arrays by value.
Owner compile paths populate them and bind_stage consumes existing pointers;
no additional per-binding scratch arrays or borrowed NIR pointers are used.

CPU SRV `cube` classification survives descriptor copy/replay; owner views use
the shader's classification to choose CUBE/CUBE_ARRAY even for one-element
arrays over the same 2D-array backing. Unused cube bindings are skipped; used
cube/noncube or nonarray/multiple-cube mismatches refuse. Cube creation enforces
finite face/mip ranges with subtraction/division before multiplication, accepts
the all-remaining mip sentinel and rejects zero/invalid counts or nonzero/NaN
MinLOD. Existing valid 2D/buffer behavior and C17 maps remain unchanged. New
owner SRV checks use subtraction before addition/narrowing. Descriptor stride
remains 64 bytes on host (production sizeof assertion <=64).

GetDimensions requires no shim. `emit_resinfo` emits NIR txs 2 components for
nonarray cube and 3 for cube-array (z=number of cubes), plus query_levels 1.
Asahi `agx_state.c:722-739` packs descriptor.depth=faceCount/6;
`src/asahi/libagx/texture.cl:12-62` adds descriptor.first_level to queried LOD,
minifies width/height, preserves array depth and sets cube height=width;
`:79-88` returns query level count=(last_level-first_level)+1.
Thus a view at firstFace 6/firstMip 1 reports minified width 2/height 2, cubeCount 1
and levelCount 1 over the 4x4/2x2 two-mip backing. A two-cube view at mip 1 reports
cubeCount 2. The parent's actual FXC shaders guard the exact cube count (1/2), positive
square width/height and positive level count before sampling; pixel colors
independently distinguish the selected resource mip.

RED `/tmp/agx-c19-views-red.log` reproduces current cube SRV refusal in the
production extraction harness; `/tmp/agx-c19-dim-red.log` proves missing NIR
classification fails the actual-NIR cube/one-element-array assertion. GREEN
production harness covers cube/nonarray and one-element-array target selection,
firstFace 6/mip 1, count 2, all-mips sentinel, fitting firstFace 1, finite overflow/
zero/empty/square/MinLOD failures, unused bindings and dimension mismatches.
Actual FXC headers ps_cube, ps_cube_array and ps_cube_array_second are decoded
directly by the dedicated test; all translate with correct slot 0 metadata,
3/4 sample coordinates, 2/3 textureSize components, 1 query-level component,
and validate after AGX preprocessing. No Asahi implementation code is copied.

Affected D3D12 suite 41 tests PASS 0.986s; DXBC 13 tests PASS 7.523s, corpus 439
translated/13 refused/0 crashes. Logs `/tmp/agx-c19-targeted-final.log` and
`/tmp/agx-c19-dxbc-final.log`. Existing native-owner source assertions are
updated only for the added explicit metadata pointer in bind_stage calls.
Builder syntax/stack/probe gates, full-suite comparison, commits and experiment
preregistration remain parent-owned. No C19 hardware verdict is established.

## Parent package gate

Full1513 tests167.539s:68fail51errors2skips; identical119failure/error signatures toC18baseline. Three pinnedMSVC syntaxvariants rc0; ARM64probe67a9fd5c9424669de9173c4f5d36e3253d65135c7bbbb84ad26cb60a4cac2514. All3exactFXCshaders NIR+AGXvalidated; D3D1241PASS, DXBC13PASS/corpus439/13/0. C20typedextenttests added after fullsuite discovery intentionallyremainRED outside thiscandidate; theirsourcefix is excluded fromC19snapshot. REVIEW historicalR113/G3 DEFER; R85/R47 ACCEPT exactsource/profile/hash; R48 oneUSBowner; R97 noarchaeology; R14 targeted/full once.
