# C21 EXP1419: sparse estimate mistaken for an active sparse allocation

Source-first CPU/query boundary analysis. Hardware source snapshot:
`542c688816dbfec539742d342cff13a80bac7415`; source archive SHA-256
`91b530914f32da6c964aa714a37f497ba403be9a2bbcfe519ae4b5e4efb21e6e`.
This note does not claim a repaired driver or completed RED/GREEN run.

## Observation

EXP1419 records five paired volume-query input/output receipts. All inputs meet
our unchanged limited domain: type4, samples1/quality0, undefined layout,
alignment65536; R32_UINT4x4x8/mips2/flags16 for A/B/C, RGBA8_UNORM32x32x4/mips2/
flags17 for D, RGBA8_UNORM4x4x8/mips2/flags16 for E. Each returns reason3 and
owner pure-query HRESULT0x80004001. Public owner result fields are zero because
AgxD3d12QueryTexture3DLayout clears its result before returning E_NOTIMPL. They
are NOT a measurement that the internal native layout has zero fields.

C22A/B and the prior47 steps PASS. This investigation concerns no GPU volume
execution, rendering, scheduler, firmware, power or register behavior.

## Primary source and exact causal mismatch

Package owner `drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp` SHA-256
`16fcfab9194487f37b6a17a3508484e92d46153066162640291513452453c2c9`
contains texture3d_layout rejecting any nonzero SparseTableSizeBytes. Package
native projection script SHA-256
`70210d44500d8adbe384f6a18051de5e6b6ac64808397e4d0e5ccbeb9e72129b`
copies actual AIL sparse_table_size_B into that field without reinterpretation.
The package native archive commit is542c6888; archive SHA-256
`4ded159a9a68210cc1b87c815899b8dbbf38f601cb716ec574b8927fbb87dfcd`;
NativeResourceSourceSha256f9817235100a25d4b8454b7c81b6bd3354fa34177613cc7b0af38b0b62a958b9;
NativePreparedResultSha2567131f6150ba4f1e8b237ffe74c282a41a10083acc54e95eec9b14349b5200516.

Pinned Asahi primary layout.h146-156 explicitly defines sparse_table_size_B as a
potential page-table size calculated for every image, including nonsparse ones;
it does not contribute to image size_B. layout.c384 unconditionally calls
ail_initialize_sparse_table after ordinary layout/compression setup. Its formula
uses ceil(layer_stride_B / image_bytes_per_folio) times baseDepth times2048.
For our layerStride16384 and small finite shapes the potential field is16384
at depth8 and8192 at depth4. It is positive even though no sparse map exists.

The consumer distinction is explicit in Asahi Vulkan hk_image.c917-948:
sparse-bound/resident are separate creation flags; sparse_map BO creation from
this field occurs only under sparse_resident. Gallium agx_pipe.c's ordinary
resource constructor contains no sparse-table allocation or sparse_table_size_B
consumer. Our owner creates a zeroed ordinary resource template with flags0 and
only sampler/RT binds. The field is therefore not a predicate for active sparse
storage, a second BO, compression, or a larger ordinary SizeBytes allocation.

Relevant primary hashes:
layout.c2620c7369759a671e606e4720b70e5b4fefb94f103ac3c085fe4b61afcfd0457;
layout.he4c55c1541406d384e5c67df6ff81e0b45b38d424e287cf5c4256a8de822f2a3.

## Why existing host tests missed it

The owner fixture in d3d12_volume_query_fixture.cpp clears the native result and
populates size/stride/mips/offsets, leaving SparseTableSizeBytes zero. The actual
AIL-based stand-in in test_d3d12_resolved_volume.py likewise omits the field.
Separate native_resource_layout tests DO compare the complete native sparse field
with actual AIL, but previously did not pass that full result through the owner
predicate. These two individually green boundaries did not test their composition.

## Minimal repair proposal and deterministic regression

The owning layer is texture3d_layout. Remove ONLY its
`|| layout->SparseTableSizeBytes` rejection. Preserve the pure native field,
compression/metadata refusal, native extent limit, every domain/modifier/layout/
alignment predicate, same template and ordinary creation path. Do not zero the
query field, enlarge capacity, change caps, add sparse support, or change AIL.
Actual sparse templates are not exposed by this owner API: it builds flags0,
validates only its existing RT/sampler bind mask and has no sparse-creation input.
No native sparse rejection or template flag policy is broadened by this proposal.

Prepared original two-TU regression:
`.local/c21-native-owner-1424-prep` in the main host tree. native-query.c reuses
existing source-generation code up to, but not including, compilation: actual
projected native query/modifier/setup/format validation and linked primary AIL.
Its existing BO/device stubs count accesses; the pure path must touch neither.
owner-query.cpp extracts actual texture3d_template, texture3d_layout and public
AgxD3d12QueryTexture3DLayout, linked to that real C query. No replacement query
fills selected matching fields. The full native result crosses the boundary.
The fixture's two known DXGI-to-pipe format mappings retain the existing host
fixture mapping; it is not a new format-family coverage claim.

Five cases cover the two formats, both hardware shapes, sampler/RT binds and
one/two requested levels. Native query must succeed, report exact131072/65536
image bytes and positive potential sparse extent, preserve its input, remain
uncompressed and perform zero BO/device accesses. Public owner query must return
S_OK, equal size/stride and each requested actual native mip offset. Unchanged
invalid domain/bind cases must still refuse. Old guard is expected to produce
functional RED E_NOTIMPL; same oracle after the sole guard removal must GREEN.
No compiler or executable has run at source preparation time. Exact generated
sources, existing archive/tool hashes and proposed argv are frozen in that prep
manifest; parent must preregister BEFORE before compiler invocation.

## WINDOWS CONTRACT / AGX-ASAHI CONTRACT / TRANSLATION / UNKNOWN

FULL GRAPHICS current0003 allocation query must provide truthful ordinary extent
beforeCreate. Asahi's always-computed potential sparse-table estimate is metadata
about a possible different creation mode, not actual ordinary allocation. The
owner must preserve that distinction while continuing to reject unsupported
formats/layouts/actual compression and use the exact ordinary native extent.
No m1n1/Mu/KMD initialization or recovery contract changes.

The deterministic composed regression is the next proof gate; until executed,
other internal predicates have not been individually measured. The false sparse
predicate is independently sufficient to reject valid real results, and exactly
explains why incomplete host stand-ins could pass. Do not infer from source alone
that all subsequent C21 copy/view/render stages work.

After RED/GREEN, focused tests, actual Windows syntax gates and immutable build,
parent's EXP1424 discriminator is all54 checkpoints: prior49 including C22A/B,
and C21A-E with unchanged allocation sizes and exact copy/compute/graphics oracles.
Keep input/output diagnostics. Collect evidence, then standard exact-package
removal and ordered GPU-visible recovery. No extra diagnostic hardware is needed
to guess the documented layout-field semantics. No hardware action in this lane.

Parent actual1424: complete realnativequery->actualowner functionalRED fivecases/0,0,0,1 thenSOLEfalseguard removal GREENfive/0,0,0,0. NativepotentialSparse remains16384/8192, ordinaryextent131072/65536, correctmip/depthstride, noBO/device/actualsparseflag. InitialcompilemissingSDKpreserved, explicitpinnedMacOSX27 usedonretry. Durable9focusedtestsPASS2.689s; actualWindows3syntaxrc0. Full1583tests210.099s/68fail51errors8skip: exact119knownbaseline signatures unchangedvs1419full; twoexistingserialguard failuresreappearedvs117environmentbaseline, notnewsourcechange. LogSHA34d2e83fa61bf120cd5956ae8d33ea0fa5db76cf2ec6109cd1077a36daa0cff9. Package1424BUILD_PASS/PDB/ARM64stack13824/WOW6544; hardwarecheckpointpending.
