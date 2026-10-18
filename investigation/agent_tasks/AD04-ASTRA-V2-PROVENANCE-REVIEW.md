# ASTRA ARCHITECT_DECISION — v2/provenance review

Reviewed baseline: 417f375b60bc13c547c79bb387ea7ccec4c5367f.
Scope: commits29f41e7, d2c2ad8,044b946,3c32f1a and their immediate consumers.
Result: CHANGES_REQUIRED before real agx_build_pipeline wiring. Hardware absent.

## 1. Version contract

APPROVE the version-gated reuse of the existing128-byte payload: in command
v2, UscPipelineReference denotes VS and Reserved[0] denotes FS. Reference
Offset/Bytes already identify each allocation-relative subrange, so no guessed
concatenation is required. Other reserved words remain zero; Clear remains v1.
Document that the existing name is wire storage, not an unversioned extension.

REJECT inferring command version from a nonzero Reserved[0]. Reference index0
is a valid ordinal when allocations are ordered differently; it cannot mean
absence. The old BuildDraw API also silently changes behavior for malformed
v1 requests. Keep old BuildDraw and RelocSeal as explicit v1 wrappers. Add
version-taking variants for native callers; accept only versions1/2. Do not
change the existing request or wire layout just to select the version.

Test v1 reserved rejection through the builder, explicit v2 with FS reference0,
unknown versions, malformed/missing/wrong-role/same-reference stages and
unchanged caller output on failure. Naming a reference is not proof that the
encoder reaches it: require source-backed stage relocations in composition tests.

## 2. Unsupported consumers must reject

AdmissionDynamicOverlayPlan currently ignores Header.Version, adds only the
single legacy pipeline and applies its64-byte split. BindingsFromView drops
Reserved[0] and stores no version. DMA v3 cannot represent two stage bindings.
Do not feed v2 into this path. Immediately reject unsupported command versions
at both legacy overlay entry points before side effects. Set explicit v1 in
PlanFromJob's reconstructed header and existing fixture headers.

This is a temporary rejection at the unsupported consumer, not native-path
implementation. Preserve v1 byte layout and behavior. Before a v2 hardware
candidate, implement the complete versioned stage carry through plan, DMA,
reconstruction, resolve, apply, receipts and release; validate two independent
pipelines through that real composition. The new path must use owned placements.

## 3. Provenance and lifetime

The bridge currently compares caller-supplied CpuBase/SliceCpu and GpuBase/
SliceGpu differences. Query returns token/serial/size but no authoritative CPU
mapping or native BO association. Thus it proves arithmetic consistency only.
The single mismatch test does not prove rejection of a forged pair of bases.
The query-then-RelocReference query also does not carry the originally expected
serial into the final retain; serialized capture alone does not serialize BO
destruction or CPU unmapping.

Keep the arithmetic helper, but do not call it complete provenance. Before
native wiring, derive identity and current mapping from a device-owned BO
association keyed by the actual native BO returned by the pool allocator.
Acquire an exact identity/source-read hold under that association's lock;
derive the slice offset from its authoritative CPU map. Keep mapping/source
lifetime valid through the copy that consumes the bytes, or copy into owned
request storage. Native GPU values used during emission are relocatable
construction addresses; equality does not establish a Windows GPU mapping.

Implement an expected-identity capture entry point sharing existing reference
logic: compare owner/token/serial/generation/size/access/index from the expected
identity with the authoritative query BEFORE Retain, and pass that same serial
to an atomic owner-side retain. Failure leaves reference counts/holds/output
unchanged. Existing generic Reference retains its current documented semantics.
The final real owner callback must serialize mapping invalidation/destruction
with this acquire; neither a local boolean nor a repeated query supplies it.

Tests required: wrong owner/generation/serial; changed identity between bridge
query and capture acquire; wrong/forged mapping bases; unmap/destroy while a
source-read hold exists; full-range overflow; independent pool subranges;
rollback and exact once release. Distinguish callback-contract tests from real
native BO integration. Do not claim all are covered by the current test.

## 4. Next integration order

First fix explicit version selection and legacy consumer rejection with tests.
Then complete expected-identity acquisition and authoritative native BO mapping
association using existing Windows allocation owner. Do not add another generic
device callback framework before a concrete native caller consumes it.

Pinned agx_build_pipeline CURRENTLY calls agx_pool_alloc_aligned; its inline
wrapper delegates to with_bo with NULL. The out_bo facility is available but
the stage callsite does not yet obtain provenance. Correct this handoff wording.

Next native slice: a pinned, license-reviewed source overlay changes that real
allocation call to obtain out_bo and invokes the device-owned capture seam,
propagating failure through the real caller. Compile the affected Mesa source
and execute a production-emitter composition test, not another synthetic screen.
Direct DRM syncobj/device/queue calls are verified later factory integration
boundaries; replacing them requires real Windows fence semantics, not opaque
handle stubs. Native VA construction and all address-bearing emitters require
typed relocation closure before native submit. GPUVA migration remains NO.

Existing Windows x64/ARM64 builds and scoped host tests remain valid for exactly
their tested inputs. They do not establish v2 KMD composition, native pool use,
residency, real agx_screen/context or Full Graphics acceptance.

Routing: Terra Low implements the above corrective slice. Return to Astra High
for the concrete native ownership/placement design or production factory contract;
no user approval is needed for these reversible decisions.
