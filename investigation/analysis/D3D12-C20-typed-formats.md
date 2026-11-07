# C20 source finding: typed-buffer SRV byte extents

Status2026-10-18: source review only, while C19 remains the active owner lane.
No production change, format-cap expansion, build, hardware run or verdict.
FULL GRAPHICS. The confirmed finding concerns **typed-buffer SRVs**; the requested
broader typed-texture UAV/SRV coverage remains open and is not declared fixed.

## WINDOWS CONTRACT:

Microsoft's [D3D12_BUFFER_SRV](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_buffer_srv)
defines FirstElement and NumElements in elements. With StructureByteStride0,
a typed format supplies element size. The pinned WDK
`/Users/pavel/public_windows/.local/reference/wdk26100/d3d12umddi.h` preserves
those fields in its buffer SRV ABI. A view of3 R8_UINT elements therefore names
3 bytes;3 R16_UINT elements name6 bytes. A dword alignment requirement for raw
views does not redefine the element count of typed views.

The pinned header's comment above D3D12DDI_FORMAT_SUPPORT explicitly describes
optional format features. Current `CheckFormatSupport` returns0, so this review
does not infer an absent mandatory capability from that return and does not
propose setting any bit. Microsoft [typed UAV load documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads)
separates baseline R32_FLOAT/UINT/SINT loads, the additional all-or-nothing set,
and individually optional formats. Backend enum conversion alone is insufficient
to advertise any new format/UAV feature. That query contract must remain separate
from fixing an already-admitted SRV's byte extent.

## AGX/ASAHI CONTRACT:

Pinned Mesa `9aa1215f878b504f66159dd2ead4c7973142126e`:

- `src/gallium/frontends/d3d10umd/Format.cpp` maps DXGI R8_UINT/R16_UINT to
  PIPE_FORMAT_R8_UINT/R16_UINT.
- `src/gallium/frontends/d3d10umd/Shader.cpp` buffer SRV creation uses exact
  NumElements*format block bytes for `desc.u.buf.size`, without dword rounding.
  The repository D3D11 frontend delegates ordinary typed SRVs to this owner.
- `src/gallium/drivers/asahi/agx_pipe.c::agx_is_format_supported` distinguishes
  usage, texture target, format validity, renderability, samples and depth.
  RGB32/luminance emulation is buffer-only; a nonzero FormatTranslate result or
  a1x1 format block is not universal texture support.
- `src/gallium/drivers/asahi/agx_state.h::agx_texture_buffer_size_el` computes
  MIN2(AGX_TEXTURE_BUFFER_MAX_SIZE, view_size_bytes / format_block_bytes).
- `agx_state.c::agx_pack_texture` passes the actual `state->u.buf.size` to that
  helper and packs the returned element count in `cfg.buffer_size_sw`; it adds
  `state->u.buf.offset` to the buffer address. This is the actual consumer of
  the owner-provided byte extent, not a second local test algorithm.

## TRANSLATION:

Current D3D12 `CreateShaderResourceView` uses
`AgxD3d12FormatBlockBytes` for typed-buffer stride and records exactly
NumElements*stride. `OwnerBindings` preserves descriptor offset/bytes/format.
Then `mesa/winsys/agx_d3d12_windows.cpp::bind_stage` unconditionally executes:

```cpp
templ.u.buf.size = static_cast<unsigned>(srvs[i].Bytes && srvs[i].Bytes < room ? srvs[i].Bytes : room) & ~3u;
```

That transforms legitimate typed R8_UINT3 from3 to0 bytes, and R16_UINT3 from6
to4 bytes. Asahi consequently receives0 and2 elements instead of3; the trailing
element is outside the packed view's bounds. This is a deterministic owner
translation defect, independent of texture allocation, scheduler, GPUVA or
firmware. Do not change KMD/m1n1/Mu to conceal it. Retain byte clipping against
the backing resource; preserve raw/structured dword rules separately.

The smallest future correction is to retain the validated typed view's exact
byte extent at this owner boundary. No new format support bits or format-family
casting rules are needed to demonstrate that correction. The parent must freeze
C19 first because the compiler lane currently owns this source file.

## Texture-format pass and limits

`TextureBytes` bounds dimension/sample/mips and asks for an uncompressed1x1
format's bytes. Owner texture creation uses FormatTranslate and builds a2D or
2D-array Gallium resource; typed SRV/UAV construction carries its declared format.
These paths do not call `screen->is_format_supported` for every usage/view tuple.
This is a candidate admission-validation gap, **not evidence that a particular
legal runtime texture format presently renders incorrectly**. Selecting a
format family and compatibility policy requires a complete source-backed
Windows-versus-Asahi usage matrix; this pass does not guess it or use register
experiments to discover it. Existing local D3D10 Windows format-family helpers
are references for legal reinterpretation, not permission to broaden D3D12 caps.

No stronger confirmed texture-UAV/SRV failure was established in this bounded
pass. Do not describe the typed-buffer correction as full typed-texture coverage,
BC support, SRGB UAV support, typeless-family coverage or3D support.

## Deterministic regression and checkpoint

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
An admitted typed SRV must retain its exact number of1-byte or2-byte elements
through owner binding, including the final element when totalbytes%4 !=0.

New dedicated regression should extract the complete production `bind_stage`
and its component-mapping helper, capture the real sampler-view template at
create_sampler_view, and use the actual pinned Asahi element-count helper.
Cases: R8_UINT3 must produce3 bytes/3 elements; R16_UINT3 must produce6 bytes/3
elements. Include aligned typed and untyped controls. Test the production owner
boundary rather than duplicating its rounding expression. The expected RED is
size0 versus3, or size4 versus6, followed by actual Asahi elementcount0/2.
Do not edit the owner before the C19 snapshot is pinned.

After offline RED/GREEN and normal gates, the smallest independent fresh-device
checkpoint is a compute shader with `Buffer<uint>` SRVs t0/t1 and an existing
R32_UINT output UAV. Initialize R8 data bytes [17,83,197] plus distinctive bytes
outside the view, and R16 words [257,4099,60013] plus outside-view words. Create
3-element typed SRVs, read indices0,1,2 and query GetDimensions into the output.
Require exact output [3,17,83,197,3,257,4099,60013] (dimension counts interleaved
with each resource's values), with the trailing elements197/60013 explicit.
Use shader-load operations, not filtered sampling. Record actual format-query
results and any runtime/driver refusal; absence of required API admission makes
the hardware discriminator unmet, never a reason to silently advertise bits.
Own `AGX_D3D12_C20: PASS|FAIL` and readback/UMD receipts must be separate from C19.

## WHAT IS STILL UNKNOWN:

Hardware has not tested the corrected binding. Runtime format-query outcomes
for the proposed fresh-device probe must be recorded before interpreting it.
Broader typed-texture format/usage coverage remains a separate design task.
The byte-extent defect itself requires no diagnostic hardware to choose its fix.

### Observed dedicated RED, before owner changes

`python3 -m unittest discover -s tests -p test_d3d12_typed_srv_extent.py`
compiled successfully and ran4 tests under ASan/UBSan:2 expected failures,
2 passing controls. R8 captured0bytes/Asahi0elements rather than3bytes/3elements;
R16 captured4bytes/Asahi2elements rather than6bytes/3elements. Both report
trailing_index2_addressable0. R32_UINT3 and untyped3dword controls pass.

New files: `tests/test_d3d12_typed_srv_extent.py` and
`tests/d3d12_typed_srv_extent_fixture.cpp`. They extract the complete current
production owner functions and actual Asahi size helper/constants. The test
links Mesa's real format table for element sizes; it does not recreate Asahi's
count calculation or pretend that an offline callback read GPU memory. The
thin DXGI-to-pipe adapter supplies the two mappings checked in the primary
Format.cpp source. Tests intentionally remain RED until C19 source is frozen
and the parent authorizes the owner correction.

## C20A/B concrete probe preparation (after C19 snapshot)

Shader now prepared, without editing the owner or main probe:
`windows/icd/agx_d3d12_probe_cs_typed_extent.hlsl`. Parent FXC command:

```text
fxc /nologo /T cs_5_0 /E main /Vn g_agx_d3d12_probe_cs_typed_extent /Fh agx_d3d12_probe_cs_typed_extent.h agx_d3d12_probe_cs_typed_extent.hlsl
```

It declares Buffer<uint>t0 and RWByteAddressBufferu0. `[numthreads(64,1,1) with thread_id.x>=3 early return]`
and Dispatch(1,1,1) produce three active threads in the established64-thread group. Each loads its own source index to
out[0..2]; thread0 writes GetDimensions(count) to out[3]. This concrete layout
supersedes the rough combined checkpoint's count-first/interleaved output above.
No filtered sample, extra format advertisement or optional typed-UAV load is
needed; the output uses the established raw-buffer path.

A nonzero input origin can respect the inspected hardware source contract:
`agx_pipe.c::agx_init_screen_caps` sets
`caps->texture_buffer_offset_alignment=64`, and `agx_pack_texture` adds the exact
view byte offset to its base address. Choose a64-byte origin for both formats,
not an unaligned1-byte or2-byte origin. Accordingly FirstElement64 for R8_UINT
and FirstElement32 for R16_UINT both select offset64. A valid buffer descriptor
expresses that element-relative origin; the owner must preserve it. Arbitrary
unaligned typed offsets are outside this checkpoint and must not be implied as
supported by this fix.

|Checkpoint|Typed SRV|Input and poison|Exact first4 output DWORDs|
|---|---|---|---|
|C20A|R8_UINT, FirstElement64, NumElements3, stride0, flagsNONE|Fill buffer0xA5; bytes64..66=[17,83,197]; byte63=0xE1, byte67=0xD9|[17,83,197,3]|
|C20B|R16_UINT, FirstElement32, NumElements3, stride0, flagsNONE|Fill words0xA55A; words32..34=[257,4099,60013]; word31=0x2BAD, word35=0xFEDC|[257,4099,60013,3]|

Use independent `run_isolated` devices/queues/fences and own
`AGX_D3D12_C20A/B: PASS|FAIL`. Initialize upload data in full, copy into the
existing DEFAULT input buffer and transition to NON_PIXEL_SHADER_RESOURCE.
Create the typed t0 table descriptor with identity component mapping. Use the
existing raw u0 path, explicitly initialize all output DWORDs to a nonzero
sentinel, dispatch once, then UAV barrier/transition/copy to readback. Compare
all four words exactly; print actual/expected count and every value. Preserve
format-query results, readback, shader/PSO/dispatch/fence receipts and package
identity. Byte64 origin and outside-view poison distinguish wrong-offset and
wrong-extent errors. Counts3 plus nonzero final values197/60013 exclude a
zero/null/tail-truncated view from passing accidentally.

The parent owns FXC, builder compilation, the eventual minimal owner correction,
main-probe wiring, snapshot, signing and any later experiment. No such action
was taken in this preparation lane.

## Owner handback and offline GREEN

After parent pinned C19 snapshot
`53e025377af0ea6ceadb80dec78d5b4ade67869f`, the owner lane was handed back.
The minimal correction now preserves typed `templ.u.buf.size` unchanged after
the existing backing-span clipping, and applies `&~3u` only to UNKNOWN-format
raw/structured SRVs. No API fields, resource allocation, format-family policy,
capabilities or alignment rules changed.

Dedicated regression now passes4/4. It also uses the intended64-byte view
origin and strengthens the untyped control: requested15 untyped bytes still
round to12, while typed3/6-byte ranges remain exact. Actual pinned Asahi counts
are3 in all cases, making the trailing index2 addressable. This supersedes the
earlier intentionally-RED current-status note; the earlier RED observations
remain the evidence that this regression catches the defect.

Affected command:

```text
cd tests
python3 -m unittest test_d3d12_typed_srv_extent test_d3d12_view_mapping test_d3d12_native_owner test_d3d12_abi_gate
```

Observed38 tests passed. Parent still owns the full suite, compile/package and
hardware verdict. No builder/hardware action or commit was made in this lane.

Main probe now includes the parent-generated typed-extent shader header and
adds C20A/B in fresh run_isolated devices, without changing older checkpoint
bodies. It records format-query results and refuses an unmet buffer/shader-load
query rather than modifying caps. It initializes complete input buffers with
poison and output with0xD15EA5ED, uploads to DEFAULT buffers, uses legal copy/UAV/
SRV transitions plus a UAV barrier, and compares all4 output DWORDs exactly.
Each checkpoint prints its source origin, actual/expected values, count, fence
and its own PASS/FAIL line. These probes have not been compiled/run by this lane.

## Parent final gates

Full1517 tests168.497s:68fail51errors2skips; all119failure/error signatures matchC19baseline. Four extenttestsGREEN,38affectedtestsPASS, pinnedMSVCsyntax3xrc0. Final ARM64probea56a31322d02278bcd9242e91617a1f37a34e30aea410c0b6b40a2d602d79674 uses established64x1x1 group and guards x>=3, preserving one extentvariable. Exactshaderheaderdd35370db2063fcfc2a6feafd7fed8ff23ed373a4ceb32a23b5a746fbbf93b07 passes actualDXBCentry/NIRfinalize/dimensiongather/Asahipreprocess: t0BUF uinttxf1coord, scalarbufinfo txs elementcount, u0rawSSBOstores, legacyinactive map. /tmp/agx-c20-nir-check.log. No hardwareverdict yet.

REVIEW historicalR113/G3 DEFER; R85/R47 ACCEPT exactsource/profile/hash; R48 oneUSBowner; R97 noarchaeology; R14 targeted/full once.
