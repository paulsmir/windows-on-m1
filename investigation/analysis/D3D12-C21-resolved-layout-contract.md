# C21 bounded resolved device-dependent layout: offline design

Status: implemented LIMITED source contract after deterministic RED/GREEN,
not hardware validated. Native-owner builds now admit only the bounded domain
below together with the complete legacy registry and reflection companions.
No hardware, builder, guest command or general format-cap change was performed
for this pass. This does not claim complete FL11 support.

## Primary evidence and causal decision

Inspected pinned `.local/reference/wdk26100/d3d12umddi.h` lines168–248,
573–611,2611–2617,2697–2708; current DDI GetCaps/allocation/create/existing/
subresource callbacks; native owner texture3d_template/layout; Asahi
`src/gallium/drivers/asahi/agx_pipe.c` resource layout initialization and
`src/asahi/layout/layout.c` tail allocation; the existing original Windows/AIl
address/coalescing fixtures. Windows source is Microsoft DirectX-Specs
[ResourceHeaps.md, pin71e431fa0f713818c6a3a66f49ab0e6732712d15](https://github.com/microsoft/DirectX-Specs/blob/71e431fa0f713818c6a3a66f49ab0e6732712d15/d3d/ResourceHeaps.md),
particularly Parameterized Swizzle and allocation/subresource sections.

The primary spec explicitly directs drivers needing no more than two patterns per
texture to reject TEXTURE_LAYOUT1. Succeeding that query switches runtime and
UMD to six pattern slots, five transitions, DESC_0004. This is a protocol mode
switch, not something inferred from the output-buffer capacity. The sentence
about reporting "more than 6" conflicts with its surrounding two-to-six
explanation; the exact WDK declarations remove that numerical ambiguity.

The checkpoint resources each request TWO mip levels. They do not require six
patterns or coalescing: their actual native blocks require only two patterns.
Use the legacy two-slot TEXTURE_LAYOUT / DESC_0003 interface. The proposed
patterns need neither XOR3 nor flags, so DESC_0003 loses no semantics. Do not
advertise TEXTURE_LAYOUT1 merely to use a newer descriptor name.

The existing coalescing result proves active-coordinate equality for a larger
common Morton block, but not exclusive padded-block storage. In particular a
32x32/4-byte pattern describes a4096-byte block while the4x4 volume's mip1 starts
at128. Declaring that larger footprint could overlap storage assigned to another
mip. This design avoids relying on undocumented permission to overlap such
footprints. Native blocks end before the next native mip and are individually
aligned; no coalesced block is advertised.

## Bounded resource domain

One natural finite domain, rather than exact checkpoint dimensions:

- committed DEFAULT Texture3D; square power-of-two width=height in1,2,4,8,16,32;
- depth1..2048; one or two REQUESTED levels, subject to the ordinary legal maximum
  mip count from max(width,height,depth); no MSAA or arrays;
- exactly DXGI_FORMAT_R32_UINT and DXGI_FORMAT_R8G8B8A8_UNORM, with their existing
  explicit native format translations; no typeless/format-family inference;
- explicit uncompressed native GPU-tiled modifier; no metadata, compression,
  sparse table, protected/shared/cross-adapter/placed/reserved resource path;
- existing implemented SRV, full selected-mip UAV and one-Z-plane RTV operations;
  unsupported layered/general/partial-range view paths continue to refuse.

Reject every descriptor outside this domain before EnsureOwner/BO allocation.
A two-level descriptor with width=height=depth=1 is invalid. Higher native
allocated levels are padding/private allocation state, not additional D3D
subresources. The pure native query still owns their physical extent.

No inspected0003 WDK declaration or parameterized-swizzle specification states
that every enumerated device-dependent layout must represent every dimension,
format or mip count. A layout describes the mapping selected for an admitted
resource; it does not independently advertise arbitrary Texture3D support.
This is an absence of a newly identified layout-specific blocker, NOT a claim
that partial dimension/format implementation satisfies the existing FL minimums.
CheckFormatSupport's optional bits remain unchanged; no broad format/3D cap is
introduced. The limited feature must be labelled as such in its checkpoint.

## Exact immutable registry and GetCaps contract

Use global unique pattern IDs and layout IDs, not per-resource-generated IDs.
The implemented IDs below are covered by actual GetCaps/packed-WDK tests.

| Index i | Native square side | Pattern ID | Layout ID | Two pattern slots |
| --- | ---: | --- | --- | --- |
|0|32|0x100|0x100|0x100,0x101|
|1|16|0x101|0x101|0x101,0x102|
|2|8|0x102|0x102|0x102,0x103|
|3|4|0x103|0x103|0x103,0x104|
|4|2|0x104|0x104|0x104,0x105|
|5|1|0x105|0x105|0x105,0x105|

The repeated last slot is an unused slot for a one-pattern layout, not a second
identical global registry entry. This is consistent with transition semantics
allowing slots to remain unreferenced. Layout and pattern enum spaces are
separate even though this table assigns equal numeric values.

Each descriptor is SPECIFIC TO4-byte elements. For side S=2^k its valid primary
bit sequence, low-to-high, is X0,X1, then X2,Y0,X3,Y1,...,X(k+1),Y(k-1).
Remaining primary entries and both XOR arrays are zero. StackDepthSlices=FALSE.
Block length is2+2k bits, X channel consumes2+k bits, Y consumesk, Z consumes0;
therefore the block is4*S*S bytes and one depth slice tall. Do not reuse the
registry for8/16-byte formats without another independently proved registry.

| Query | Input and exact output contract |
| --- | --- |
|TEXTURE_LAYOUT, pInfo=NULL|For recognized12-byte base caps return counts6/6 and Supports64KStandardSwizzle=FALSE. For the16-byte0001 caps additionally return SupportsRowMajorTexture=FALSE. Reject unsupported sizes, NULL pData or malformed query; do not use the counts answer for indexed requests.|
|TEXTURE_LAYOUT, pInfo!=NULL|pInfo points to UINT zero-based layout index0..5; DataSize=2*sizeof(D3D12DDI_SWIZZLE_PATTERN)=8. Return the corresponding two IDs above. Refuse out-of-range index or wrong output size. There is no pInfo-size field in this ABI; runtime supplies the documented UINT.|
|TEXTURE_LAYOUT1|Return E_NOTIMPL. No mode state, no six-slot writes.|
|SWIZZLE_PATTERN|pInfo points to UINT zero-based pattern index0..5, NOT enum0x100+i. DataSize=sizeof(DESC_0003)=100. Copy the immutable100-byte descriptor. Refuse DESC_0004's104-byte size or NULL/out-of-range inputs; no accidental mode switch.|
|TEXTURE_LAYOUT_SETS|Preserve the existing row-major functional-unit answers. This query describes row-major subcaps, not the device-dependent registry.|

All queries are pure immutable reads. Device creation need not mutate the
registry. Before implementation, add tests against actual WDK-derived packed
structs to establish100/104 sizes and every source bit; do not serialize the
logical test struct WinSwizzle0004 as a Windows ABI descriptor.

## Allocation, creation and reflection are one atomic invariant

WINDOWS CONTRACT: allocation query is deterministic, concurrent, and resolves
UNDEFINED before creation. The pinned0003 callback takes UINT64
AlignmentRestriction. Native opaque layout is not ROW_MAJOR,64KB standard or
64KB undefined tiled-resource layout. The latter carries separate tile-tier
requirements and must not be used as a shortcut.

AGX/ASAHI CONTRACT: native uncompressed GPU-tiled resource setup sets renderable
true. For these small square POT levels it chooses a square native tile exactly
the minified XY size. Each mip occupies4*S_l*S_l bytes, followed by128-byte
cacheline padding. Mipmapped volumes may allocate the full physical mip chain;
Z slices have the constant queried LayerStrideBytes. The native resource and
pure allocation query use the same template/modifier, so the full extent is
SizeBytes BEFORE the backend64KiB BO rounding.

TRANSLATION:

1. A plain immutable owner query result must expose native SizeBytes,
   LayerStrideBytes and LevelOffsets (the current public owner wrapper exposes
   only rounded bytes and offsets). This comes from the existing screen-free
   native query; no Screen/device/context/BO/EnsureOwner and no copied AIL table
   in the DDI. Native metadata and compression flags must be absent.
2. Allocation query validates the domain and UNDEFINED or its one matching
   resolved layout ID, then returns Layout=0x100+i and native SizeBytes rounded
   upward to64KiB, ResourceDataAlignment=64KiB. Additional sizes and alignments
   remain zero because this modifier has no separately mapped metadata. Check
   AlignmentRestriction against the actual supported alignment; never silently
   fulfill a tighter placement contract the owner cannot create.
3. With one requested level, MipLevelSwizzleTransition=UINT8_MAX. With two
   levels and side>1, transition=1. Side1 usesUINT8_MAX because both levels use
   the same1x1 pattern. No additional transitions are used in legacy mode;
   leave the AdditionalDataHeaderAlignment union zero, do NOT write four255s
   there and accidentally report a huge alignment under old semantics.
4. Create accepts only the resolved matching layout, never UNDEFINED, and uses
   exactly the same validated native template/modifier/size. DEFAULT/no CPU
   access and existing committed-only gates still apply. Store resolved layout,
   transition, native mip offsets, row strides and queried constant depth stride
   as immutable resource metadata. Both R32_UINT and RGBA8 checkpoints follow
   the same registry; their formats are validated independently.
5. CheckExistingResourceAllocationInfo returns exactly the stored resolved
   layout/transition/rounded size/alignment and the real heap allocation handle.
   It must agree byte-for-byte on semantic fields with precreation reflection.
6. CheckSubresourceInfo for valid mip m returns Offset=native LevelOffsets[m],
   RowStride=4*S_m*S_m (one full native tile row),
   DepthStride=native LayerStrideBytes, and all three pre-swizzle offsets0.
   These are parameterized-swizzle strides, NOT linear row pitch. S_m=max(S>>m,1).
   A row stride of0 cannot describe this resolved layout even though Map is
   refused. Reject/zero invalid mip indices consistently. CPU Map remains
   fail-closed; no upload/readback texture or standard swizzle support is added.

Checkpoint examples, two REQUESTED levels:

| Resource | Layout | Rounded bytes | Transition | mip0 Offset/Row/Depth | mip1 Offset/Row/Depth |
| --- | --- | ---: | ---: | --- | --- |
|R32_UINT4x4x8|0x103|131072|1|0/64/16384|128/16/16384|
|RGBA8_UNORM32x32x4|0x100|65536|1|0/4096/16384|4096/1024/16384|

## Full-block and padding proof, and what is still unknown

A declared block now equals the actual allocated native XY mip tile. Every
square level fills the ENTIRE declared tile with texels; there is no partial
4096-byte tile whose unoccupied part extends into another mip. For each native
mip offset O, block B, constant Z stride L, allocation extent E, the finite-domain
property gate checks O%B=0, O+B<=next native mip offset when present,
O+B<=L, and O+(minifiedDepth-1)*L+B<=E. Consequently all requested mip/Z block
intervals are disjoint; their complement is allocation padding.128-byte native
cacheline padding and unused native full-chain levels remain outside those
intervals. No assumed runtime permission to overwrite padding is needed.

New original test-only files:
`tests/windows_ail_two_pattern_footprint_fixture.c` and
`tests/test_windows_ail_two_pattern_footprints.py`.
The fixture calls actual linked AIL for all6 sides, every depth1..2048, requests1/2,
and both explicit formats. ASan/UBSan PASS:

```
AGX_LOCAL_TREE=/Users/pavel/public_windows/.local python3 -m unittest discover -s tests -p test_windows_ail_two_pattern_footprints.py -v
NATIVE_TWO_PATTERN_FOOTPRINT_PASS resources=49150 mips=73724 disjoint_full_blocks=62939144
```

This is an interval/extent proof, supplementing existing actual-tiling byte and
no-alias oracles; it does not itself execute runtime CPU swizzle routines or GPU
commands. Existing coalescing and72-layout comparison results are not rerun here.

WHAT IS STILL UNKNOWN: Windows runtime and GPU behavior for this newly
implemented limited path awaits parent-controlled build/sign/hash gates and the
existing LIMITED C21A–E exact-voxel checkpoint. No hardware bit probing is
justified. The entire companion invariant is implemented, while unsupported
resource/view domains remain explicit refusals. This does not establish complete
FL11 support or change general format-support flags.

Implementation verification: `test_d3d12_resolved_volume` first reproduced a
real RED: the actual TextureBytes function returned0 for4x4x8R32_UINT/mips2.
It now executes actual GetCaps (all six packed descriptors and output canaries),
allocation query, CreateVolumeResource (the real callback's delegated branch),
owner query/create, existing reflection and subresource reflection. The creation
roundtrip links actual AIL for native offsets/extents, covers all six XY sizes,
both requested mip counts and both formats, and proves query paths call no
EnsureOwner or BO creation. Invalid domains/layouts/heaps/restrictions fail
before owner creation. WDK declarations are extracted directly: DESC_0003=100,
DESC_0004=104, allocation info=56, subresource info=32 bytes. Packed descriptor
addresses cover each whole native block exactly once.

```
AGX_LOCAL_TREE=/Users/pavel/public_windows/.local PYTHONPATH=tests python3 -m unittest test_d3d12_resolved_volume test_d3d12_volume test_d3d12_abi_gate test_d3d12_native_owner test_d3d12_view_mapping test_d3d12_typed_srv_extent test_native_resource_layout test_windows_ail_two_pattern_footprints
Ran52tests:OK
```

The existing volume metadata test was updated from prototype zero strides to the
now-implemented native block/depth strides; CPU Map refusal remains tested. The
ABI gate's accepted cases now include the actually implemented SWIZZLE_PATTERN
query and explicitly rejected TEXTURE_LAYOUT1 case. C18 device zeroing, existing
buffer/2D paths, native fixed validation guard9d49473d and recovery scripts were
not edited by this change.

Source/license boundary: Microsoft documentation is CC-BY4.0 and its code MIT at
the pinned repository LICENSE/LICENSE-CODE; descriptions above are paraphrased.
Fixtures and table design are original; no external implementation was copied.
The linked native AIL dependency retains its existing MIT notices.
