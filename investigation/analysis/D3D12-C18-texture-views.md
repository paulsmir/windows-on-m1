# D3D12 C18: preserve SRV component mapping

Status: source-first design and offline implementation, 2026-10-18. Parent
pinned EXP1380 source snapshot `2bd3b5d03f03e18ad78d99dbf566b60051387bbc`
(677 files), then authorized C18 DDI/owner/helper/tests implementation. No C18
builder/package/hardware run or commit is performed by this lane. All graphics
contracts here are FULL GRAPHICS.

## Sources inspected

Line references describe the current C17 integration tree and may move later.

- `drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:1082`: the bounded, <=64-byte
  `AgxD3d12Descriptor` layout; `:1235-1284`: SRV creation discards
  `Shader4ComponentMapping` in both texture and buffer branches;
  `:2920-2937`: `OwnerBindings` propagates SRV format/range but no mapping.
- `drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h:91-99`:
  `AGX_D3D12_SRV` carries format and texture ranges, with no component map.
- `drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp:1091-1128`:
  `bind_stage` initializes XYZW for every sampler view, then constructs a
  texture or buffer view; `:321-351`: only 2D/2D-array texture allocation;
  `:1042-1069`: typed UAV image view bounds.
- Pinned `/Users/pavel/public_windows/.local/reference/wdk26100/d3d12umddi.h`:
  `:835-853` defines six component selectors and the exact encoded mapping;
  `:921-937` includes `Shader4ComponentMapping` in
  `D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002`; `:897-904` specifies cube
  face/mip/LOD fields; `:1165` starts the 3D UAV view structure;
  `:682-690` distinguishes typed UAV write/read format support.
- Microsoft [D3D12_SHADER_COMPONENT_MAPPING](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_shader_component_mapping):
  memory components 0..3, forced zero and format-appropriate one.
- Asahi source root `/Users/pavel/public_windows/.local/reference/mesa`:
  `src/gallium/drivers/asahi/agx_state.c:640-685` (`agx_pack_texture`)
  composes format swizzles with view swizzles and emits the four channels;
  `:722-739` packs 3D depth or cube face-count/6;
  `:760-799` preserves the sampler-view template;
  `src/gallium/drivers/asahi/agx_pipe.c:2048` advertises independent view
  targets, `:2215-2270` checks target/usage-specific format support;
  `src/gallium/include/pipe/p_state.h:512-524` defines sampler-view swizzles,
  mip intervals and `min_lod_clamp`.
- Local D3D11 precedent:
  `drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp:270-319` converts cube
  views with independent Gallium targets and checked face/mip ranges;
  `:480-532` derives 3D UAV slice bounds from the selected mip's depth.
  `drivers/apple-agx/mesa/scripts/build-native-asahi-state.py:3589-3628`
  supplies DXGI cast-family compatibility and lowered texture formats.
- Existing hardware-facing oracle:
  `drivers/apple-agx/windows/icd/agx_d3d12_probe.c:1061-1254` uploads a 2x2
  RGBA8 texture, samples through a point sampler to 64x64 RGBA8 and reads
  back quadrants. C9 at `:1496-1660` proves the compute texture/image path.
  `drivers/apple-agx/mesa/dxbc/dxbc_to_nir.c:1714-1727` already represents
  3D/cube/cube-array shader texture dimensions.

External implementations are inspected for behavior only. No external code is
proposed for copying; the mapping is an original implementation of the WDK ABI.

## WINDOWS CONTRACT:

An SRV's component mapping routes the fetched memory components to the shader's
four returned components. Each output independently selects X/Y/Z/W, zero, or
one. Forced one is integer 1 for an integer source format and floating-point
1.0 for a floating/normalized source format. Identity is the WDK-encoded
(0,1,2,3), including the required always-set bit; a zero-initialized field is
not the encoded default.

The field is view state, not shader state or root-signature state. Copying a
CPU descriptor and replaying its descriptor table must preserve it, including
when C17 maps a logical namespace to a compact physical slot. The shader does
not need recompilation for a different view mapping. Root SRVs have no SRV
descriptor component map and retain the existing identity interpretation.

Confirmed current defect: SRV creation accepts a valid nonidentity mapping but
stores none of it; owner view construction always selects XYZW. The same valid
shader therefore receives different components than the application's SRV
requests. This is deterministic and can be reproduced offline.

## AGX/ASAHI CONTRACT:

Gallium's `pipe_sampler_view` provides four swizzle fields. Asahi first gets
the resource format's own swizzle, composes the user/view swizzle, and packs the
result into the texture descriptor. This handles format channel layout and
view remapping together. An R/B swap in frontend metadata must not be applied
twice or substituted for the format's existing swizzle.

No new AGX command, firmware operation, capability bit, KMD interface or
synchronization rule follows from this fix. Existing `create_sampler_view` and
`set_sampler_views` are already assigned by Asahi. UAV image views do not gain
an SRV component mapping.

## TRANSLATION:

Carry the encoded WDK mapping through the bounded CPU descriptor, copied
command/descriptor representation, and `AGX_D3D12_SRV` to `bind_stage`. Decode
four 3-bit selectors explicitly to the Gallium X/Y/Z/W/ZERO/ONE enum values;
do not assume enum-number equality without checking the pinned headers.

Keep identity metadata explicit for descriptor-less root SRVs and for internal
raw/structured views. Do not let the zero-initialized owner aggregate silently
mean a valid encoded WDK map. Preserve typed buffer descriptor maps through the
same metadata path; verify the typed `Buffer<T>` lowering before applying a
view swizzle to the internal R32_UINT representation used for raw/structured
loads. If a nonidentity raw/structured combination cannot be derived from its
Windows contract, refuse that combination explicitly rather than change byte
address interpretation by accident. The primary C18 pixel checkpoint uses a
2D typed texture and has no such ambiguity.

Validate the encoded field (required bit and selectors 0..5) before consuming
it; malformed selectors must not reach Gallium/AGX. Keep the existing <=64-byte
CBV/SRV/UAV descriptor size and descriptor increments. Append owner metadata in
a way that preserves existing aggregate field meanings. Recheck replay stack
frames after the owner SRV array grows: there are 128 physical SRV slots.

Ownership: the DDI validates/stores view identity and encoded selectors; command
recording snapshots roots and retains descriptor references under the existing
runtime lifetime contract; replay copies view metadata into owner bindings;
the owner builds per-stage Gallium views; Asahi composes/packs format and view
swizzles and owns their GPU representation. KMD retains residency, scheduling,
interrupt and recovery ownership. Mu/m1n1 initialization and runtime state are
unaffected, so no new machine-state measurement is required for this offline
view-state decision.

## Deterministic tests

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A nonidentity SRV map is
currently discarded, producing the original fetched tuple; forcing one must
also preserve integer versus float semantics.

1. Compile/extract the actual production SRV descriptor creation and replay
   propagation, then inspect the actual owner-created sampler-view template.
   Assert a nonidentity map survives all three boundaries. The existing code
   must fail because the resulting view remains XYZW. Avoid a test-only
   duplicate decoder as the implementation under test.
2. Exhaustively test each valid selector 0..5 in each of four output positions,
   identity and repeated channels, plus malformed selectors 6/7 and missing
   required bit. Check the actual mapped Gallium selectors and format-aware
   constant semantics. Exercise RGBA8_UNORM, a signed/unsigned integer format,
   and a format with nonidentity memory channel order (e.g. BGRA8) so format
   composition is not mistaken for a second frontend channel swap.
3. Verify SRV descriptor copy and stage replay preserve mapping while root
   SRVs/default owner views remain identity; retain C17 namespace resolution
   tests. If typed buffer descriptors participate, test their actual NIR/view
   contract separately from raw/structured internal texel-buffer reads.
4. Run the affected D3D12 owner/ABI/root suites and the existing DXBC corpus.
   Builder syntax3x, ARM64 probe, and both <=16384 stack gates are package
   prerequisites. Parent runs the full host suite once before commit/snapshot.

## Exact later pixel checkpoint

Use a fresh device, the existing full-screen point-sampled textured quad,
2x2 `R8G8B8A8_UNORM` upload, no blending, and 64x64 RGBA8 readback. Keep shader,
root table and sampler unchanged; the changed factor is SRV component mapping.
Use deliberately asymmetric source bytes so each fetched component is visible:

| Quadrant | Source RGBA | Map (B,R,0,1) expected RGBA | Map (G,B,A,R) expected RGBA |
| --- | --- | --- | --- |
| TL |17,61,109,151|109,17,0,255|61,109,151,17|
| TR |29,83,137,173|137,29,0,255|83,137,173,29|
| BL |43,97,163,199|163,43,0,255|97,163,199,43|
| BR |59,113,181,227|181,59,0,255|113,181,227,59|

The parent's implemented C18A/C18B probe uses fresh isolated devices for the two
maps, legacy SM4 pixel shader, and draw_textured_impl variants 2/3. Each copies
the first SRV into a second descriptor with CopyDescriptorsSimple and samples
the copy. Compare every pixel against its quadrant tuple (4096 pixels, all 4
channels including alpha, bad=0), with separate
`AGX_D3D12_C18A: PASS|FAIL` / `AGX_D3D12_C18B: PASS|FAIL` results. Identity
mapping retains the existing texture control. Parent built probe SHA-256
prefix 1b2809d4; the later experiment must record its complete hash. This tests routing and both constants without shader changes or a
format-capability change. Integer forced-one coverage is mandatory offline;
a later separate integer image/readback checkpoint can verify the already
proven type-specific mapping without conflating it with this RGBA8 pixel test.

## WHAT IS STILL UNKNOWN:

No Windows-to-Gallium mapping uncertainty remains for typed texture SRV
swizzles: implement and test that contract offline. The later hardware unknown
is whether actual descriptor propagation and Asahi's composed view swizzles
produce the exact expected pixels on the J313. C18 hardware is verification of
a designed mapping, not a capability-bit experiment. Raw/structured nonidentity mapping remains explicitly unsupported as detailed
in the implementation decision below; no scalar texture swizzle is substituted
for the post-fetch vector mapping.

## Separate future findings (outside C18)

### Cube and cube-array SRVs

This is the smallest missing dimension feature. Committed six/twelve-layer 2D
arrays, mip-aware copies and cube shader dimensions already exist. The missing
contract is `RD_TEXTURECUBE` admission plus a view target independent of the
backing `PIPE_TEXTURE_2D_ARRAY`. WDK `TexCube` gives `MostDetailedMip`,
`MipLevels`, `First2DArrayFace`, `NumCubes`, and `ResourceMinLODClamp`;
[Microsoft's cube-array description](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_texcube_array_srv)
separates face range from cube count. Validate square faces, nonzero count,
checked multiplication by 6, full face span, and mip subset; choose CUBE versus
CUBE_ARRAY explicitly. Asahi `agx_state.c:722-739` uses the view target and
converts the view face count to cube count. D3D11's `:270-319` helper demonstrates
the local conversion. A later checkpoint samples 6 axis directions from distinct
face colors, then selects cube 1 and mip 1, each with an exact oracle.

### Minimum LOD clamp

Current SRV creation drops `ResourceMinLODClamp`; this silently changes a valid
nonzero-clamp view. Setting Gallium's `min_lod_clamp` field alone is insufficient:
there is no consumer of that field in the current Asahi driver/compiler source.
Do not implement this as a metadata-only change. Derive a per-view lowering or
explicitly refuse nonzero clamp until supported; changing a shared sampler's
MinLOD is not generally equivalent when one sampler serves differently clamped
views. Existing zero-clamp mip subsets already map to first/last level.

### 3D textures

Do not admit 3D by extending one enum. `TextureBytes` at DDI`:589-606` and owner
allocation`:321-351` assume constant 2D-array layer count and `depth0=1`;
`CopySide`/`CopyTextureRegion` at DDI`:2059-2135` identify only 2D textures and
decompose subresources into mip/array layer. A coherent 3D slice requires
mip-shrinking depth, `PIPE_TEXTURE_3D`, 3D copy Z/depth and W-slice bounds derived
from `max(1, depth0 >> mip)`. D3D11 `uav_image:514-528` already uses that bound.

### Typed format gates

`AgxD3d12FormatBlockBytes` is not a target/usage format-support predicate. The
owner's current allocation/view path does not call Asahi's
`agx_is_format_supported`; the D3D10 compatibility/lowered-format helpers are a
useful local reference, not an automatic ABI substitute. Derive view-family
compatibility, target support, and any needed format lowering before broadening
coverage. The [typed-UAV load specification](https://learn.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads)
requires R32_FLOAT/UINT/SINT and defines an additional all-or-nothing set plus
individually optional formats. Do not infer advertised typed-load capability
from one image-store result or cherry-pick additional capability bits.

## Recovery and experiment boundary

Parent must preregister the later C18 experiment, exact source/package/probe
hashes, expected checkpoint and known-good recovery before installation. Reuse
the established hash-pinned full-owner launch and experiment-local package
cleanup. Collect evidence before rollback. This design note neither changes
GPU_CURRENT_STATE nor establishes a hardware verdict; C17/EXP1380 remains the
parent's active boundary.


## C18 implementation decision and offline evidence

Primary-source follow-up inspected Microsoft's
[ResourceBinding Shader Resource View section](https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html#shader-resource-view)
and [ld_structured operation](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/ld-structured--sm5---asm-).
The former defines post-fetch four-component mapping and format-appropriate
ONE, without a raw/structured exemption. The latter defines a vector read from
a structure's byte offset. Current `dxbc_to_nir.c:2123-2150` implements those
raw/structured reads as separate scalar R32_UINT texel fetches; applying a
Gallium view swizzle to each scalar fetch cannot implement an arbitrary mapping
of the completed vector. Therefore C18 accepts encoded identity for raw and
structured descriptors and refuses nonidentity with SRV receipt reason 8. It
makes no claim that the Windows mapping field is generally ignored for them.
Root SRVs explicitly carry encoded identity (0x1688).

`agx_d3d12_view_mapping.h` validates the required bit, rejects reserved bits,
and rejects selector 6/7. DDI descriptors retain the encoded field through
copy/replay; owner `srv_component_mapping` translates selectors through an
explicit Gallium enum table. Typed textures and typed Buffer<T> views use it;
owner defensive validation also refuses malformed/nonidentity internal untyped
views. The bounded descriptor remains 56 bytes on the host (<=64).

ForcedONE semantics are checked against the actual Mesa
`util_format_apply_color_swizzle` implementation at `src/util/format/u_format.c:963`,
which uses integer 1 or float 1.0 based on the format type. Asahi
`src/asahi/lib/agx_helpers.h:63-76` maps PIPE_SWIZZLE_1 to AGX_CHANNEL_1;
`agx_pack_texture` packs that alongside the format's type. No CPU conversion
of the constant into a baked float bit-pattern is introduced.

Tests `test_d3d12_view_mapping.py` compile the actual production descriptor
layout and CreateShaderResourceView, actual OwnerBindings SRV loop, actual
owner SRV layout/helper, and the entire bind_stage function against host
Gallium headers. Fake pipe callbacks only capture the actual sampler-view
creation arguments. Both creation/copy/replay propagation and root default
are observed; the decoder is not reimplemented in a test mock. The test also
executes actual Mesa format/view-swizzle composition and integer/float
constant application functions for RGBA/BGRA component order.

RED log `/tmp/agx-c18-red.log`: 3 expected behavioral failures before production
changes (nonidentity mapping discarded, invalid mapping accepted, root mapping
metadata left 0). GREEN: 3 ASan/UBSan tests PASS, exhausting all 6 selectors in all 4
positions for texture and typed-buffer paths, float/UINT/SINT formats,
RGBA/BGRA composition, descriptor copy, malformed encodings, raw/structured
refusal, defensive owner refusal and explicit root identity. Affected D3D12
suite 40 tests PASS in 0.776s (`/tmp/agx-c18-targeted.log`). Parent still owns
probe implementation, builder/stack gates, full-suite comparison, commits,
ledger and any later hardware preregistration.

## Parent integration gates

Pinned MSVC syntax3xrc0; ordinary ARM64 C18probe1b2809d42b11b519f9077a9cee38938f6cc1f7d6e8b9df6bcf8796550c9f1023. Full1512 tests:68fail51errors2skips; all119failure/error signatures identical to1509 C17 baseline. Targeted40PASS plus DXBC13PASS/corpus439/13/0. Production extraction ASan/UBSan RED3->GREEN3. Package stack/sign/hash and hardware verdict remain pending.

REVIEW R113/historicalG3 items DEFER (existing pinned launch); R85/R47 ACCEPT exactsource/profile/hash; R48 ACCEPT oneUSBowner; R97 ACCEPT noarchaeology; R14 ACCEPT targeted then full once.
