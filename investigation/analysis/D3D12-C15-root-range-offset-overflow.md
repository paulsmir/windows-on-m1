# C15 / current 0003 root descriptor range offset arithmetic

Classification: RENDER_ONLY UMD CPU root-signature translation. Source-first
inputs: pinned WDK26100 `d3d12umddi.h` lines 1285–1308 and current
`agx_d3d12_ddi.cpp` root Calc/Create at 428–496 and descriptor binding at
3323–3342/3407–3414; Microsoft [D3D12_DESCRIPTOR_RANGE](https://learn.microsoft.com/en-us/windows/desktop/api/d3d12/ns-d3d12-d3d12_descriptor_range)
(NumDescriptors UINT_MAX is unbounded; APPEND follows the preceding range;
a range following unbounded must have an explicit offset), and
[HLSL root-signature syntax](https://learn.microsoft.com/en-us/windows/win32/direct3d12/specifying-root-signatures-in-hlsl)
(explicit offsets may overlap). The existing Mesa/Asahi owner consumes the
already-resolved `range.offset`; its GPU protocol, m1n1 and Mu state are
unchanged. EXP1663 J313 source9b374daf is the accepted ordinary baseline.

WINDOWS CONTRACT: the WDK supplies `UINT` offset and descriptor count.
`D3D12DDI_DESCRIPTOR_RANGE_OFFSET_APPEND` is UINT_MAX; an unbounded range
has count UINT_MAX and cannot be followed by APPEND. Explicit offsets can
legally overlap. For finite count the resolved end must fit UINT, or the
current UMD `next` calculation would wrap and misresolve a following APPEND.

AGX/ASAHI CONTRACT: the current private root stores a 32-bit resolved offset,
which replay converts to 64-bit only after Create. A wrap in Create loses
information before that promotion; neither Asahi nor KMD can repair it.

TRANSLATION: Calc and Create share `AgxD3d12MeasureR1RootPrivate` from
EXP1663. Validate each table's range offset sequence there: derive APPEND
from `next`, reject APPEND when `next` is UINT_MAX after an unbounded range,
and reject any finite `offset + count` above UINT_MAX. Preserve explicitly
offset ranges after unbounded and all legal overlaps. This is an indivisible
size-then-create rejection gate with no change to valid layout or caps.

WHAT IS STILL UNKNOWN: no GPU behavior is unknown for rejected malformed
range inputs; the next check is deterministic host RED/GREEN plus pinned WDK
syntax. A separate production package/J313 discriminator would only be needed
if the change is later included in a new accepted driver source with other
behavior requiring hardware validation. Do not run J313 merely to test UINT
arithmetic.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Before the fix, actual-source
Calc returns a nonzero private size and Create writes for an explicit
`UINT_MAX-2 + 4` range; it also accepts APPEND after an unbounded range.
Actual-source ASan/UBSan RED: 19 checks / 5 failures. GREEN requires both
callbacks to refuse these inputs before destination writes and to retain
an explicit offset after unbounded as valid.

EXP1664 offline result: the actual-source host fixture is GREEN under
Appleclang21 ASan/UBSan with 21 checks and zero failures. It covers the
explicit overflow, APPEND after unbounded, and legal explicit offset after
unbounded in both Calc and Create, including no destination write on errors.
The pinned Windows builder staged header SHA256
`68c2a6ba7a3f0621a251d07bd615fa5e1a377daf717811dad6e437ebe12d86fe`
with unchanged DDI SHA256
`ecb3ca32e5afcd8ec294e41a24203b5d46d81ee4a1005eecaaab1e9e55047a6c`;
WDK26100 native-owner/NONE/winsys syntax returned three `rc=0`, no new
source diagnostics. Builder log
`.local/c15-root-range-syntax1664/builder.log` SHA256
`ddac7d6b56ae5ad6b0982f2191bba1037db051b79879fc66b6a51330c42730e1`.
Focused ABI/native owner/change ledger 32 tests PASS. This is a CPU/build-only
contract; no source-bound production package, GPU run or J313 claim.
