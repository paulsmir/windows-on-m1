> Source update: the former resolved-layout blocker below is superseded by the
> implemented LIMITED native two-pattern invariant in
> `D3D12-C21-resolved-layout-contract.md` (52 affected tests PASS). The broader
> prototype history is retained below; no hardware or complete FL11 claim follows.

# C21 offline volume prototype and Windows layout gate

Status2026-10-18: **3D resource admission remains fail-closed. No candidate
hardware launch or complete3D/FL11_0 support is claimed.** Existing2D, buffer,
presentation and capability answers retain their established paths. This note
supersedes any provisional assumption that UNDEFINED is a valid resolved native
layout for creation. Parent owns build, package, ledgers, commits and hardware.

## Implemented/tested prototype

The owner has a pure `AgxD3d12QueryTexture3D` API: DXGI format/dimensions/mips/bind
in,64KiB-rounded size and optional16-entry native mip-offset array out. It builds
a native3D template and calls the fixed screen-free native layout API. It creates
no owner, Screen, context or BO. Actual prototype creation uses the same template
and returned explicit uncompressed modifier, validates exact requested size and
passes that modifier to resource_create_with_modifiers. The linked native
constructor owns physical layout and BO allocation; no logical voxel sum or
16x16 estimate sizes a volume.

The fixed pure native API now rejects invalid formats before its original
initializer, using the real current Asahi `agx_is_format_supported` target/usage
predicate at single sampling without a device. NONE/range, non1x1 color blocks
and depth/stencil are refused first. There is no duplicate guessed Asahi format
table in the owner. Existing screen-dependent layout query and native constructor
policy remain unchanged. The owner imports no AIL/DRM header or format table.

DDI prototype paths preserve volume subresource=mip, real runtime Pitch/SlicePitch,
Z origins, minified depth and checked buffer/resource bounds. Full selected-mip
UAVs require FirstW0 and exactly the minified depth after remaining-slice sentinel
normalization. SRVs have explicit DXBC_SRV_3D shader metadata and native3D sampler
views. RTVs permit one fitting Z plane only; general/layered spans remain refused.
Actual clear framebuffer construction preserves selected mip/Z. Whole-volume
copy minifies depth at each level. Default volume heaps explicitly refuse CPU
Map; immutable queried native mip offsets supply CheckSubresourceInfo.Offset
while CPU row/depth strides remain0. No new descriptor stride is required.

The decisive public gate is `TextureBytes(Texture3D)==0`: allocation-info returns
unsupported size and CreateHeapAndResource returns before EnsureOwner. A synthetic
prototype3D handle also cannot report a valid existing allocation. Lower-level
arithmetic/view/copy tests therefore do not silently enable public3D resources.

## Primary Windows blocker and rejected shortcut

Pinned WDK26100 `d3d12umddi.h` declares0003 allocation callback alignment as UINT64,
not the later UINT32 ABI. [Microsoft Resource Heaps](https://microsoft.github.io/DirectX-Specs/d3d/ResourceHeaps.html)
requires layout selection before creation; UNDEFINED is input freedom, not a
resolved layout. Current caps describe zero device-dependent layouts/swizzles.
A3D native GPU-tiled layout cannot be relabeled ROW_MAJOR or64KB tile-undefined
without proving the respective addressing contract.

The smaller linear alternative is rejected by current Asahi source:
`agx_pipe.c::agx_linear_allowed` excludes PIPE_TEXTURE_3D through its default
switch arm, forbids mipmapped linear resources and rejects linear image usage.
Its comments explain that3D texture types cannot supply the needed explicit
stride. This is a source-owned blocker, not an unknown hardware register.

## Exact0003/0004 companion invariant to derive before changing caps

|Boundary|Required source-derived contract|Current state|
|---|---|---|
|GetCaps TEXTURE_LAYOUT,pInfoNULL|Actual DeviceDependentLayoutCount/SwizzleCount; existing0001 SupportsRowMajorTexture and standard-swizzle flags remain truthful|Counts0; no native layout advertised|
|TEXTURE_LAYOUT,pInfoindex|Two SWIZZLE_PATTERN IDs for a layout in legacy mode, with bounded index/size|Current handler ignores pInfo; returning counts here would be wrong|
|TEXTURE_LAYOUT1,pInfoindex|If six-pattern mode is needed, exactly six pattern IDs, with the documented runtime/driver mode switch|Currently E_NOTIMPL; do not infer mode solely from a buffer size|
|SWIZZLE_PATTERN,pInfoindex|Complete current DESC_0004 source/XOR/XOR2 arrays, constant XOR3 and Flags; legacy mode uses its appropriate DESC_0003 shape|Currently unsupported; no bit probing|
|Allocation-info|Chosen DEVICE_DEPENDENT_SWIZZLE_0+layout index, native physical size/64KiB alignment, actual mip transitions|3D unsupported until proven|
|CreateHeapAndResource|Accept precisely the already-resolved layout and same native modifier/template|3D gate returns before owner creation|
|CheckExistingResourceAllocationInfo|Same resolved identity, physical size and allocation handle|No valid3D result exposed|
|CheckSubresourceInfo|Deterministic native mip offset and the applicable swizzle row/depth/preoffset characteristics; no invented CPU-linear pitch|Cached native offsets/CPU refusal prototype; final swizzle metadata not derived|

Pinned0004 has three32-entry bit-source arrays, a UINT InterleavePatternXOR3
constant and UINT Flags. It does **not** have0022's additional XOR-source arrays
or postamble fields.0003 resource allocation info stores the first transition in
MipLevelSwizzleTransition and four more in AdditionalMipLevelSwizzleTransitionsArray.
This version supports at most six patterns/five transitions per texture layout
when TEXTURE_LAYOUT1 is recognized. Counts, indexed tables, descriptions, resolved
layout IDs and per-resource transitions must be one coherent implemented invariant.
Changing only a count is prohibited by that invariant.

No production table/count/pattern was added. A possible bounded resource-domain
admission must be based on a proved mapping with fitting pattern cardinality,
not exact probe shapes or a claim that every volume is now supported.

## Original CPU0004 subset decoder and source/license review

New test-only sources:

- `tests/windows_swizzle_reference.h`
- `tests/windows_swizzle_reference_fixture.c`
- `tests/test_windows_swizzle_reference.py`
- `tests/windows_ail_swizzle_comparison_fixture.c`
- `tests/test_windows_ail_swizzle_comparison.py`

They are original authored implementations of the functional bit/stride contract;
no Microsoft or Asahi implementation was copied into them. The comparator links
the already pinned MIT AIL and includes the separately authored AIL reference
fixture for independent offset checks. It also compares the Windows-produced
address against actual `ail_tile` byte output and checks address uniqueness.

Source attribution: Microsoft DirectX-Specs,
[ResourceHeaps.md at71e431fa0f713818c6a3a66f49ab0e6732712d15](https://github.com/microsoft/DirectX-Specs/blob/71e431fa0f713818c6a3a66f49ab0e6732712d15/d3d/ResourceHeaps.md),
especially Parameterized Swizzle updates and CheckSubresourceInfo. This note
paraphrases and applies those contracts; wording/structure have been changed.
Repository [documentation license](https://github.com/microsoft/DirectX-Specs/blob/71e431fa0f713818c6a3a66f49ab0e6732712d15/LICENSE)
is CC-BY4.0; [code license](https://github.com/microsoft/DirectX-Specs/blob/71e431fa0f713818c6a3a66f49ab0e6732712d15/LICENSE-CODE)
is Microsoft MIT. Neither grants permission to claim conformance from an unproved
mapping. The new test code remains original MIT project code. Any future external
code copying requires its own attribution/notices review; none occurred here.

The current published SimpleSwizzle example belongs to0022 and even retains a
stale StackDepthSlices member reference. It was read as context, not copied or
passed off as exact0004 source. The reference implements the nonstacked0004
subset needed by AIL: primary interleave, two optional source XORs, byte-preserving
low bits, low constant XOR, preoffsets, external tile-row/depth strides and base.
It rejects stacked-depth flags and ambiguous high constant-XOR cases rather than
inventing their interpretation. Thus it is explicitly a subset oracle, not a
claim to implement every allowed0004 pattern or to serialize its packed ABI.

Logical X is supplied in **bytes**, Y/Z in elements/slices. The original candidate
preserves low byte-within-element bits, interleaves X/Y inside an AIL power-of-two
tile, then adds block-X, row-block stride and independent Z-plane stride. In the
flags0 candidate, native separate Z planes require no stacked-depth flag.

## Observed deterministic evidence

Initial real-function RED cases: volume sizing returned0 instead of native rounded
size; actual owner volume copy rejected valid Z1/depth2 because array_size=1; actual
volume SRV creation returned reason4/null. These arithmetic/view prototypes became
GREEN. Once the unresolved Windows layout was identified, public sizing/admission
was deliberately kept unsupported; pure owner sizing tests now exercise the native
query independently and assert that the public gate performs no query/owner work.

Five volume fixtures execute actual sizing/creation, DDI SRV/UAV/RTV, sampler/image
binding, plane-clear framebuffer construction, copy recording/CPU memcpy, native
mip metadata and Map refusal. They check minified bounds, remaining sentinels,
malformed subresource index, negative signed BOX coordinates, exact pitches/Z,
untouched neighboring planes and invalid backing spans. The copy recorder fixture
uses actual LONG BOX fields and treats sign-compare warnings as errors, reproducing
and fixing the Windows builder's signed/unsigned comparison defect.

Pure native invalid-format RED accepted PIPE_FORMAT_NONE. GREEN now rejects
NONE/RGB96/A8/depth/out-of-range/unsupported RT, clears output and performs0 BO
and0 device lookups. Original native16-case and extended22-case/19 screen-free
modifier matrices retain their results. Combined affected native+volume+view+
typed-extent+owner+ABI suite passed48 tests. Parent's full-suite/builder results
remain separate; no claim of green full suite or successful package build is made.

Standalone hand-derived vectors verify byte preservation, Morton bit order,
non-power-of-two tile-row carry, Z stride, source XORs/low constant, preoffsets,
overflow and explicit unsupported cases. The linked comparison passed:

```text
WINDOWS_AIL_0004_SUBSET_PASS layouts=72 mip_levels=285 exact_logical_bytes=28268296 no_alias=YES max_native_tile_patterns=7
```

The matrix covers4/8/16-byte elements, small volumes, NPOT, both sides of tile
thresholds, depth/mips,1xN/Nx1 and64x2048. Native row multiplication is representable
through the external tile-row stride; a pure permutation alone is not the model.
The unoptimized diagnostic exceeded its30s bound; the same unchanged matrix with
-O2 and ASan/UBSan completed in14.2s. No hardware was involved.

A specific candidate budget warning remains:

```text
resource64x2048x2,4-byte elements:7 distinct native-tile Morton candidates;0004 per-layout slots6
```

That is a property of this candidate basis, **not a proof that every possible
0004 representation fails**. The strongest next alternate is to share patterns
when a mip's coordinates fit inside one tile so unused high bits are zero,
then derive a bounded, shape-independent accepted domain with a valid pattern
assignment and transitions. A separate property oracle is being prepared for
that question. Do not repeat an equivalent address pass or use hardware to pick
capability bits. General3D admission stays unsupported until that mapping and
all companions are proved.
