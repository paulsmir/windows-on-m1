# C21 EXP1407: allocation boundary and next discriminator

EXP1407 source `3ae2c234783bb8a653fbda9f1f1d04d5136ff618`; probe SHA-256
`73ac69ac2a4e71de21fa27bb5d7876685442fa07d0ff3a4041dce90aa453313a`.
This is an original source/receipt analysis, not an accepted volume implementation
or a claim that the failing predicate has been identified.

## Observation and bounded source pass

`tool-d3d12-on-std.txt` SHA-256
`e2093dfb5edf9ca172f20da9a8376fc7346e7a74ae7fb9312e9c39be4128be8d`:
C21A/B/C/E 4x4x8/mips2 return SizeInBytes 18446744073709486080
(-65536), alignment65536 instead of131072. C21D 32x32x4/mips2 expects65536
and fails identically. All five fail their allocation-info comparison before
CreateCommittedResource. Prior47 independent steps pass. No volume GPU execution
is established.

`tool-d3d12-on-umd.log` SHA-256
`9e9cbb3824347be81b00d96ad949f0e2f426141bcd05f7b2cf72eb769a05aca1`:
there is no existing generic allocation receipt for these volumes. That receipt
is after the 3D early return in CheckResourceAllocationInfo, so its absence
cannot establish whether the runtime called the DDI. The custom layout registry
is queried without indexed-layout or swizzle-pattern requests in this capture;
this does not identify the failed predicate either.

Inspected production functions: DDI VolumeLayoutCaps, VolumeLayoutInfo,
TextureBytes, CheckResourceAllocationInfo; owner texture3d_template,
texture3d_layout, AgxD3d12QueryTexture3DLayout; native projection
agx_resource_layout_query/agx_uncompressed_query_format_valid in
build-native-asahi-state.py; primary Asahi agx_is_format_supported and
ail_make_miptree. Existing host native-extent/ABI/roundtrip tests do not execute
the exact Windows native query archive in this allocation callback.

Inspected pinned SDK26100 d3d12umddi.h SHA-256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`:
0003 custom layout/pattern bases are0x100; alignmentRestriction is UINT64;
legacy allocation-info Layout and MipLevelSwizzleTransition occupy offsets48/52.
No confirmed custom-ID or legacy-structure mismatch was found.

Inspected existing matched-runtime disassembly only (no new tool execution):
D3D12Core SHA-256 `052241d7d9fdcf32dc2a86491c56e524f80e78990e6a3ddd787f75767bd41a82`.
VersionedCheckResourceAllocationInfo RVA27cb28, early0003 call path27ce20..27cec0,
CSafeDDIResource245250, conversion24d888, allocation conversion255f30,
ValidateStandardResourceAllocationInfo27c1f8, outer validation278808.
The early0003 path passes normalized descriptor alignment (normally65536 for
these single-sample requests), preserves returned size/layout fields through
legacy conversion, and clears newer UAV-only flag bits for the early ordinal.
The NONE/UAV requests are predicted to carry legacy SRV0x10; RT carries0x11,
both admitted by the source domain. Standard allocation validation at27c1f8
checks the cross-adapter case, unlike these requests. These are source facts,
not a measurement of live callback arguments or proof of successful native query.
The public invalid-size result is compatible with rejection but is not unique
to one cause. One offline pass did not prove a failing predicate; stop here.

## WINDOWS CONTRACT / AGX-ASAHI CONTRACT / TRANSLATION

The allocation callback must return a valid extent/alignment and a truthful
resolved layout before creation. The pure native query must remain screen/BO
free and derive the uncompressed AIL extent. The existing owner and DDI admission,
registry, size, layout, allocation and GPU behavior remain unchanged. No cap or
layout alternative is inferred from this failure.

## Diagnostic-only change and exact receipt schema

Only DDI VolumeLayoutInfo and the 3D CheckResourceAllocationInfo branch change.
The native query executes once, with the same arguments/order and HRESULT test.
Optional local diagnostic outputs do not change other callers. Both new measure
records use the existing opt-in trace mechanism, which permits measure records
outside the startup128-record budget; each has13 words, within the16-word limit.
Existing pid/tid/session/qpc identify the fresh probe process and ordered pair.

`measure-d3d12-volume-query-input`, hr=S_OK, words:
0 ResourceType;1 Width low32;2 Width high32;3 Height;4 DepthOrArraySize;
5 MipLevels;6 Format;7 sampleCount;8 sampleQuality;9 inputLayout;10 Flags;
11 alignmentRestriction low32;12 alignmentRestriction high32.

`measure-d3d12-volume-query-output`, hr=actual owner pure-query HRESULT, or
numeric1 when no query was called; words:
0 reason (0 success,1 domain rejection,2 incoming-layout rejection,
3 native query refusal,4 alignment rejection);
1/2 ResourceDataSize low/high;3/4 ResourceDataAlignment low/high;
5 returnedLayout;6 MipLevelSwizzleTransition;
7/8 query result NativeBytes low/high;9/10 DepthStride low/high;
11 MipOffsets[0] low32;12 MipOffsets[1] low32.
The last offsets fit32 bits under the unchanged native extent bound. On native
query failure its public result is zeroed, so these words do not reveal the
internal native predicate. A reason3 would justify the next targeted pure-query
checkpoint, not a guessed native repair.

## WHAT IS STILL UNKNOWN / falsifiable checkpoint and recovery

Next fresh C21 process under EXP1419 (parent owns package/BEFORE/recovery):
(a) no input receipt, while other fresh-process trace records prove capture,
places failure before this 3D callback; (b) reason1/2/4 names input rejection;
(c) reason3 and HRESULT names native query refusal; (d) reason0 with expected
size/layout but public invalid-size result places rejection after this callback.
Absence of both records without proof of fresh-process trace capture is
inconclusive. Preserve C21 exact expected sizes and all existing probe oracles.
Independent C22 can share the package with separate PASS/FAIL receipts.
Collect evidence before the standard GPU-visible exact-package removal and
ordered recovery. No guest action is performed by this source lane.

This instrumentation is receipt-only: no manufactured RED is required. Run the
existing volume/resolved-volume/ABI tests; do not claim a repaired allocation or
hardware PASS until the next discriminator identifies and verifies the cause.

Parent gates: existing volume/resolved-volume/ABI29testsPASS2.346s. ActualWindows existing three syntax commands allrc0 (nativeDDI/nonownerDDI/owner); local/remote5pins unchanged before-after, no warnings/errors. Worktree .local/syncheck1419/artifacts.json SHA74f40184b1523beae8389cd10c31737500121660d1bcd431718ef2e2e983fd34 rootverified. Full1582tests205.868s/68fail51errors8skips: exact119baseline failure/error signatures unchanged, no added/removed. Full log SHAaa3a724309a325d9ce6c5dd7bfa4835337e3e0604830c2129697283588900df1; .local/c21-allocation-receipts-1419-fullsuite/comparison.json. No hardware diagnostic result yet.
