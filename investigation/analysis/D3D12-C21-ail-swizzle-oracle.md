# C21: AIL byte-address oracle for a future Windows layout declaration

Status: offline foundation only. No layout capability, DDI admission, resource policy,
or hardware result changes in this note. The C21 probe lane was frozen separately.
This is a RENDER_ONLY resource-layout contract. The Windows decoder is a separate
lane; its pattern-count and DDI-version constraints must be resolved before admission.

## Sources and scope

Pinned Mesa root: `/Users/pavel/public_windows/.local/reference/mesa`.

- `src/asahi/layout/layout.c:32-214`: maximum tiles, GPU-tiled mip allocation,
  padding and physical layer stride; `:327-387`: full-chain expansion and final size.
- `src/asahi/layout/layout.h:18-19`: 128-byte cacheline and 16-KiB AIL page;
  `:203-219`: additive level/layer offsets; `:253-265`: Morton bit spacing;
  `:278-302`: tile-origin accessor; `:560-568`: DRM modifier to tiling conversion.
- `src/asahi/layout/tiling.cc:15-110`: actual CPU tile/detile address calculation;
  subsequent dispatch supports 1, 2, 4, 8 and 16 bytes per element.
- `src/gallium/drivers/asahi/agx_state.h:958-962`: CPU map adds layer and mip offset.
  `agx_pipe.c:1000-1005,1050-1055`: transfer calls tile/detile on that adjusted pointer.
- Pinned WDK `/Users/pavel/public_windows/.local/reference/wdk26100/d3d12umddi.h`:
  `D3D12DDI_SWIZZLE_BIT_ENTRY`, `D3D12DDI_SWIZZLE_PATTERN_DESC_0003`/`_0004`,
  `D3D12DDI_SUBRESOURCE_INFO_0003` and texture-layout enums. These establish field
  existence, not the complete interpretation of the Windows postamble. The separate
  Windows source/spec decoder must supply that interpretation.

The actual AIL files carry SPDX MIT; this work adds an original mathematical oracle
and calls the existing host library. It does not vendor AIL implementation code.
Source SHA-256 pins in `tests/test_ail_swizzle_oracle.py`:

| File | SHA-256 |
| --- | --- |
| layout.c | `2620c7369759a671e606e4720b70e5b4fefb94f103ac3c085fe4b61afcfd0457` |
| layout.h | `e4c55c1541406d384e5c67df6ff81e0b45b38d424e287cf5c4256a8de822f2a3` |
| tiling.cc | `160a85b334883f79cecb6f10d5a80e9070d72044ab23089f91503d15351db17f` |

Scope is single-sample, uncompressed formats with 1/2/4/8/16-byte texels and
`AIL_TILING_GPU`. The fixed uncompressed native query selects
`DRM_FORMAT_MOD_APPLE_GPU_TILED`, which maps to this tiling mode. It does **not**
select `AIL_TILING_TWIDDLED`; that is a separate constructor with different NPOT
allocation behavior. Compression metadata, MSAA sample interleave, compressed
pixel formats, sparse mapping and 32/64-byte elements are outside this oracle.

## AGX/ASAHI CONTRACT: exact address

For mip `l`, pixel/element coordinate `(x,y,z)` and byte `c` within the texel,
let `B` be bytes per texel, `(Tx,Ty)` be `layout.tilesize_el[l]`,
`R = ceil(layout.stride_el[l] / Tx)`, `O = level_offsets_B[l]`, and
`S = layer_stride_B`. Define `spread(v)` by placing bit `i` of `v` at bit `2*i`.
The allocation-relative byte address is:

```
q = (y / Ty) * R + (x / Tx)
m = spread(x % Tx) + 2 * spread(y % Ty)
A(l,x,y,z,c) = O + z*S + B*(q*Tx*Ty + m) + c
```

All divisions in this expression are integer division. Tile dimensions are powers
of two. The large rectangular tile has `Tx == Ty` or `Tx == 2*Ty`, so the Morton
bit positions do not leave holes within that tile. There is no XOR in this AIL
uncompressed address calculation. Tile-row arithmetic includes ordinary integer
multiplication and addition; it is not a global Morton permutation of all XY bits.

`ail_tile` and `ail_detile` use only the tile/Morton part relative to their supplied
pointer. Callers must add `O + z*S` first. `ail_get_twiddled_block_B` already adds
those offsets but accepts **tile-aligned origins only**; using it as a general
pixel address accessor would violate its assertions.

The byte shift must be applied after element interleave. A Windows decoder whose X
input is in bytes needs identity positions for the lowest `log2(B)` X-byte bits
and must interleave the remaining X-element bits with Y. Z is a plane-stack term
with stride `S`, not an interleaved third coordinate in this layout.

## Tiles, 4 KiB regions and mip thresholds

The maximum AIL tile occupies exactly 16 KiB:

| Bytes/texel | Maximum tile XY | Morton region occupying the first 4 KiB |
| --- | --- | --- |
| 1 | 128 x 128 | 64 x 64 |
| 2 | 128 x 64 | 64 x 32 |
| 4 | 64 x 64 | 32 x 32 |
| 8 | 64 x 32 | 32 x 16 |
| 16 | 32 x 32 | 16 x 16 |

The last column is a mathematical contiguous Morton subregion, not a separate
4-KiB AIL page or proof of a Windows tile declaration. In particular, a 16-KiB
AGX tile does not establish `64KB_TILE_UNDEFINED_SWIZZLE` compatibility.

For logical base width/height `W,H`, the first small-miptree level `p` is the first
level at which `max(1,W>>p) < Tx_max` **or** `max(1,H>>p) < Ty_max`.
For `l < p`, the tile retains the maximum dimensions. At and after `p`, it is
square with side `nextpow2(max(1,min(W,H)>>l))`. It can therefore shrink at successive
mips, and a strongly elongated texture can keep several tile rows or columns even
when the shorter dimension has become one. A single fixed full-image Morton map
must not be assumed to describe those transitions.

## NPOT allocation, mip offsets and depth

The address formula and the allocation formula are distinct. Let
`a=ceil(W/Tx_max)`, `b=ceil(H/Ty_max)` and `mask=(1<<l)-1`. For a large level the
**actual allocated tile count** is:

```
N_l = floor(a*b / 2^(2*l))
    + ((a & mask) != 0 ? floor(b / 2^l) : 0)
    + ((b & mask) != 0 ? floor(a / 2^l) : 0)
    + ((a & mask) != 0 && (b & mask) != 0 ? 1 : 0)
```

Do not replace this expression with a product of separately rounded mip tile
counts. For `129x129`, B=4, `a=b=3`, mip1 allocates **five** 16-KiB tiles; its logical
floor-minified width is 64, so its actual pixel coordinates occupy one
64x64 tile. The extra allocation is constructor padding and must remain in the
query extent. This example also shows why deriving extent only from the highest
logical pixel address is incorrect.

For the small chain, initial allocated POT dimensions are
`P=nextpow2(max(1,W>>p))`, `Q=nextpow2(max(1,H>>p))` for these uncompressed formats.
At each level allocate `B*P*Q` bytes, then minify P and Q separately with clamp1.
Both large and small level ends are aligned to 128 bytes; the next level starts
at that aligned end. `stride_el[l]` remains the **logical** minified width, not
necessarily the POT allocation width. The actual address row count `R` therefore
comes from that field and that mip's tile width.

When requested mip count exceeds one, AIL allocates the full chain through
`floor(log2(max(W,H,D)))+1` levels for `mipmapped_z=true`, even if the API exposes
only two mips. Logical depth is `max(1,D>>l)`, but every Z plane uses the **same**
physical `S` and every mip resides at `O_l` within that plane's miptree. It does not
pack each smaller-depth mip into an independently sized contiguous volume.

Let `E` be the final 128-aligned end of one miptree. For these non-depth formats,
layers become 16-KiB aligned when `(levels!=1 && E>16384 && D>1)`, or whenever
`writeable_image`, or when `renderable && D>1`. Then `S=align(E,16384)`;
otherwise `S=E`. Uncompressed `size_B=S*D`. Compression and sparse metadata are
separate constructor contracts; neither is inferred here. Native BO rounding
(64 KiB in the GPUVA backend) is also separate from this AIL byte extent.

## WINDOWS CONTRACT / TRANSLATION: representability gate

The inspected 0003 subresource structure has `Offset`, `RowStride`, `DepthStride`
and row-byte/column/depth pre-swizzle offsets. Its swizzle descriptor has primary,
XOR and XOR2 source entries and `StackDepthSlices`; 0004 changes the final fields.
Those versions must not be combined into an invented ABI. A Windows bit-source
entry alone cannot express arbitrary NPOT tile-row multiplication with carries.
A documented block/postamble stride can potentially express it.

A candidate to verify in the separate decoder is: interleave only coordinates
within one AIL tile; use block size `B*Tx*Ty`, block-row stride `R*B*Tx*Ty`,
block-depth stride `S`, and subresource base offset `O`. For plane-stack AIL,
Z need not be a primary interleaved bit and StackDepthSlices must not be selected
merely because the resource is 3D. No XOR or pre-swizzle displacement is required
by the AIL expression itself. This is a candidate **until the exact Windows
postamble and version rules are executed and compared**, not a capability claim.

Remaining obligations include pattern index/count limits across all mips, 4-KiB
versus 16-KiB block requirements, byte-X input units, descriptor limits, legal mip
transition choices, and whether the runtime permits every required Row/DepthStride.
A hypothetical smaller fixed Morton region must also be checked against AIL row
ordering: grouping several subregions into a 16-KiB tile changes the ordering of
outer X and Y bits. The test must compare exact bytes, not just total size or
round-trip through the same decoder.

## Deterministic gate and evidence

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Incorrect low bit ordering, a
BPP shift applied to the wrong coordinate, NPOT tile-row arithmetic, wrong mip tile
thresholds, loss of constructor padding, wrong physical Z stride, or aliasing active
voxels. These would make a declared Windows resource layout describe different
bytes from the resource Asahi creates.

`tests/ail_swizzle_oracle_fixture.c` is original code. It independently computes
extent/offset/tile expectations, then compares pixel byte addresses with actual
linked AIL `ail_tile` and `ail_detile`. The reusable function
`c21_ail_reference_offset(layout,l,x,y,z,byte)` returns allocation-relative offsets.
The test fills the full allocation with poison, compares every allocated byte
including padding, checks no active byte aliases another, and checks detiling.
It exercises B=1/2/4/8/16, POT/NPOT/threshold-crossing and 1D-like XY, requested
single/multiple mip chains, depth1/2/3/4/5/7/8/9, and writable/nonwritable layouts.
The actual AIL routines allocate no Screen, context or GPU BO; malloc supplies CPU
backing only. Constructor equivalence remains covered by the separate
`test_native_resource_layout` suite.

Reproduce from the repository worktree:

```
AGX_LOCAL_TREE=/Users/pavel/public_windows/.local \
 python3 -m unittest discover -s tests -p test_ail_swizzle_oracle.py -v
```

2026-10-18 offline result: **PASS**, ASan+UBSan, 1 test in 1.320 seconds,
260 layouts / 960 levels / 1,980 Z slices / 50,285,968 logical bytes;
full padding comparison passed. Log `/tmp/agx-c21-ail-oracle-persisted.log`.
Source pins are checked before compilation; host library is the existing matching
Mesa build, not a newly built external algorithm. The external library itself is
not sanitizer-instrumented; fixture buffers and original address math are.

## WHAT IS STILL UNKNOWN / checkpoint

Exact Windows descriptor representability is not established by the AIL test.
The next offline checkpoint is the original Windows decoder versus this actual-AIL
oracle, including narrow NPOT textures, all mip transitions and pattern-budget
limits. A mismatch must identify the first `(format,mip,x,y,z,byte)` and descriptor.
Only after equality and ABI/capability review can a driver layout declaration be
considered. No new hardware checkpoint or recovery action is justified by this
math-only pass; the existing frozen C21 fresh-device copy/load/store/plane-render
probe will verify actual admitted behavior after a truthful layout contract exists.
