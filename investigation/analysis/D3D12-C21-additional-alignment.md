# Canonical alignment for absent D3D12 additional data

Scope: RENDER_ONLY allocation query/reflection within the current FULL GRAPHICS
D3D12 profile. This note records the source-derived repair and observed CPU gates.

## Observed boundary

EXP1424 query-parse.json records five paired successful native owner/DDI queries:
A/B/C/E resource131072, alignment65536, layout0x103, transition1;
D resource65536, alignment65536, layout0x100, transition1.
Public allocation queries nevertheless return18446744073709486080 (-65536).
The sparse-table-estimate guard fix therefore reaches its intended boundary.

## Exact already-saved runtime evidence

Matched saved D3D12Core SHA052241d7d9fdcf32dc2a86491c56e524f80e78990e6a3ddd787f75767bd41a82;
existing dis.txt at scratchpad/d3d12core. No new disassembler/tool was run.

- 27cec0 calls legacy0003 callback, 27cee4 converts legacy record through255f30.
- 255f30..255f7c preserve the three sizes and convert three alignments at
  legacy offsets24/32/40 to new offsets24/28/32; layout48->36, transition52->40.
- GetResourceAllocationInfo_Worker 1e8a00 calls27cb28; its actual post-query
  composition at1e8a3c..1e8ad0 reads header alignment at5bc, forms
  UINT64(-1)+UINT32(headerAlignment), adds resource size, checks unsigned overflow.
  HeaderAlignment0 produces size-1<size at1e8a68 and branches1e8ad0, assigning-1.
  AdditionalDataAlignment0 has the equivalent predicate at1e8a84..1e8a94.
- The separate composition helper1e2a78..1e2b04 has the same contract, including
  header-alignment-zero failure at1e2aa0..1e2aac.
- 27cf0c..27cfac first normalize individual regions; a zero alignment is NOT
  replaced by1. The zero-sized extra regions remain zero with zero alignment.
- Rounding the resulting invalid value to65536 yields -65536, exactly the five
  observed public results. No swizzle/layout change is necessary for this cause.

This arithmetic identifies a deterministic postquery cause; it is not evidence
that every remaining Create/GPU path will succeed after fixing it.

## Microsoft/WDK contract and smallest representation

Pinned d3d12umddi.h SHAd338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28,
allocation record573..592, PFN1833..1835. All three sizes and alignments are
independent named fields; old64-bit header alignment overlaps later transitions.

Official primary pages inspected in one bounded pass:
https://microsoft.github.io/DirectX-Specs/d3d/ResourceHeaps.html
CheckResourceAllocationInfo and CheckExistingResourceAllocationInfo sections:
query determines the three sizes/alignments, then create uses resolved properties.
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddi_resource_allocation_info_0022
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_checkresourceallocationinfo_0003

The inspected official pages do NOT prescribe a particular absent-region value,
and do NOT give zero alignment a documented special meaning for size0. Therefore
1 is an ORIGINAL representation choice: the minimum power-of-two byte alignment,
truthfully imposing no extra placement constraint. Do not call1 a quotation or
normative special-case value from Microsoft. Exact runtime arithmetic independently
requires nonzero alignment. Both absent sizes stay0; resource extent/alignment,
layout/transitions/allocation handle stay unchanged. High32 header-union bytes
remain0 (legacy two-pattern mode); no fake transitions/metadata BO is introduced.

## Candidate and original actual-function regression

baseline1424.patch changes only two defaults in each of actual CheckResource-
AllocationInfo and CheckExistingResourceAllocationInfo. Source is saved exact
1424 DDI75a934b93814953da194da4540cacad91a83e14497dbef0e2e53cff760472172.
currentC23.patch applies the SAME four assignments to current C23 source solely
for software adoption; it is NOT authority to bundle dependent C23 into hardware.

The new renderer extracts BOTH actual callbacks and actual resource/heap structs,
plus ten actual pinned WDK declarations including full allocation record and
creation record. Native size helper stubs provide fixed receipt-known extents;
they do not claim AIL coverage. Existing actual native roundtrip remains separate.
The ORIGINAL checked-concatenation oracle requires valid alignment even when a
region has size0, never duplicates swizzle/allocation implementation, and checks
resource extent retained, extra sizes0, canonical extra alignments1, poisoned
storage0/a5/ff, both callbacks, buffer/2D/3D, currentC23-only1D, allocation handles,
opaque query/existing full tuple, null output safety, zero-alignment controls.

Existing native roundtrip fixture previously ASSERTED both bad alignments0:
regression-adoption.patch corrects that assertion and expands the legacy tiny
metadata fixture's allocation record to named real fields. New authoritative
regression uses exact WDK records; no ABI proof is claimed for DXGI scalar aliases.

The original commands.json remains an unexecuted proposal. Approved replacement
commands-approved.json4a48f3e2 used pinned Xcode clang++1590ac95 with explicit SDK.
Under BEFORE b297b425 all eight argv executed once. All compiles returned0; exact
EXP1424 baseline240 checks72 failures became0; current C23324 checks96 failures
became0. All20 approved inputs remained unchanged. Verdict1c99fdcbc88b934a8131372e5375aabd98bbe3c1ec6f17956acac08eec1af583;
artifact manifest18a5fefcd27591dd32583f293ba8d74f5ebcdfa7ed3b89fb6f766ea14ef8e1a4.
This is functional RED/GREEN, not a compiler-failure classification.

## Hardware ordering

Preserve old a875 C23 snapshot and preparation1426 UNLAUNCHED/superseded.
New EXP1426 must be exact5078 source plus ONLY baseline1424.patch, original54probe
ec529c1282caa21ba967c33757a97a4a5535f03b7777a10bfe3282d6e07e571a.
C23 allocation depends on this correction and moves to EXP1427 after1426 verdict.
No cap, swizzle pattern, layout ID, KMD, firmware or m1n1 change is proposed.

## Atomic contract and final software verification

ATOMIC CONTRACT: absent additional-data regions retain size0 and byte alignment1
in both CheckResourceAllocationInfo and CheckExistingResourceAllocationInfo.
Both alignment fields must be nonzero: the exact public worker concatenates both
regions, so fixing only one leaves the second failure. Both callbacks describe
the same resolved allocation and must preserve their complete tuple. This is one
representation invariant, not independent capability probing. Microsoft defines
these separate regions; the pinned runtime arithmetic and original actual-callback
regression establish the concrete zero-size boundary and roundtrip requirement.

Durable actual-function regression passed with resolved volume/Texture1D controls:
14 tests in10.028s. Full final branch:1589 tests213.465s, exact119 old failure/error
signatures, no additions or removals. Full log SHA256
2161111224ce824e66819c62d0aa2f29b0e98377f9bb857a166870b955a080e2.
The full suite is not GREEN. All three real Windows syntax profiles for the exact
hardware snapshot returned0;29-file manifest0bd8969f95a27c79bebbb874ae236982848ae737f573db9ae33b21efff641641.

Hardware source939cf61a78833349e7be51e028fd9ddf82a2e85a is exactly507812fb plus
six inserted source lines in one DDI file, SHAfb162cd3fbf1eb1fdc4c4cc145e1fe5dc885c3b6d8e8d8c96f22c8ccb5b927ea.
It excludes C23 and C15. PACKAGE1426_BUILD_PASS/PDBs and ARM64 stack13824/WOW6544
were observed. Original four native artifacts are preserved before repackage in
native-build-evidence, preservation manifest405209c3bb156b1f6a453ecd400166f69b51c2e7d15ce887deea786b819aa9d6.
The detailed build BEFORE append failed before the orchestration incorrectly
continued to build; EXPERIMENTS.md records this late-entry process error explicitly.
The immutable source and minimal contract were recorded before build. Installation
and hardware remain subject to a complete preregistration and all final hash gates.

The source-level allocation-composition cause is closed offline. Actual public
allocation, Create and volume GPU operations remain the next hardware checkpoint.
Original54-step probe and all regressions are retained; recover through the exact
FactoryV2 GPU-visible controller to Code28/PackageAbsent/noService8CPU afterwards.
No capability, swizzle, resource-size, metadata BO, firmware, KMD or m1n1 change.
