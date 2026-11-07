# C15: explicit ordinary LibraryReference provenance (EXP1420)

Original CPU foundation extension; module remains unlinked and unregistered.
No main DDI, CORE/model, capabilities, native owner or GPU behavior changes.

## Contract and ownership

Actual pinned WDK26100 d3d12umddi.h SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`
lines3191–3195 defines LIBRARY_REFERENCE_0010 as library handle plus opaque UINT
PipelineIndex. Existing runtime-r1-contract and zero-native-cache foundation
source evidence permits ordinary fields with a valid optional reference; it does
not imply omitted-field inheritance or require per-index/native-cache retention.

New internal helper AgxD3d12ZeroCacheValidateLibraryReference is not a WDK callback.
Its documented original contract is deliberately explicit:

- NULL reference record returns E_INVALIDARG. A nonnull record must be readable.
- Null library handle is ordinary no-library input, accepted only for a live valid
  device. PipelineIndex is unused, including UINT_MAX.
- Nonnull library must be in the live same-device registry. Unknown library
  addresses are compared against registered addresses before dereference.
- Device resolver executes before registry SRW acquisition. No PSO resolver,
  retention, index storage, allocation, native operation or runtime callback is
  added. All six original callbacks and PFN assertions are unchanged.

This helper does not extend identity lifetime or fix child/device retirement.
Existing requirements remain: runtime object storage remains live during legal
callbacks; device identity remains resolvable until all libraries are removed;
resolvers run outside registry locks. Invalid concurrent Destroy/use and stale
same-address ABA are not made valid. No configurable production hook is added.

## Deterministic regression

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Acceptance of null-library
references on dead devices, accidental null-record shorthand, foreign/destroyed
library admission, unknown-handle dereference, index-based retention/mutation,
PSO resolution during reference validation, or resolver invocation under SRW.

The existing durable fixture directly compiles the actual module translation
unit, preserving59 old checks and adding22 for81 total. The renderer extracts the
actual WDK LibraryReference record in addition to six callback typedefs and the
create record. Fixture code supplies input/oracles only, not validator logic.
Protection-page handles, storage canaries, repeated/UINT_MAX indices, dead-device
resolver simulation, and a host lock-depth observation enforce those invariants.
Resolver identity is restored before child cleanup; the test claims no lifetime
repair. Host overlay is explicitly not Windows ABI.

Default discovery still has one opt-in test that skips before compiler/tool
operations unless the existing receipt/approval environment is supplied. Explicit
execution requires parent BEFORE plus exact source/tool/generated-input approval
hashes and two frozen argv arrays. Changed sources correctly invalidate old1418
approval hashes; archived inputs/receipts are preserved.

## Evidence scope

Parent executed the external frozen candidate once under EXP1420 HOST BEFORE
5aa89f66 and approval SHA256
`2a8d187dfc7516b8288d75b0ebf2b0f632737953424fd8fd268a343c1b5f5f6c`:
compiler and fixture exit0; marker `ZERO_NATIVE_CACHE_REFERENCE_PASS checks=81
callbacks=6 reference_validator=YES actual_WDK_declarations=YES
host_Windows_ABI=NO no_registration=YES`.
Receipts: `.local/c15-zero-cache-reference-1420/host-receipts/`.
The repository H/CPP and fixture/shim are byte-identical to that candidate.
This is external host evidence, not execution of the newly adapted durable runner.

Repository adoption/rendering is prepared only; no durable test/compiler has been
run at this source-freeze boundary. Separate native ARM64 object/actual-SDK ABI
prep exists under `native-prep/`, still awaiting parent native BEFORE. There is no
Windows execution/link/resolver integration/R1/SM6 or hardware admission result.

## Parent verified results

Durable actual repository module and fixture passed81 checks with ASan/UBSan;
compiler0/run0, all15 frozen inputs independently rehashed unchanged before
this result-note append. Manifest durable-host-receipts/output-manifest.json
SHA256 dbb53e1a708f54eb1935e8a28778bd1ccc1e78b2383e858ea064c987709afa93.

The actual Windows ARM64 object and actual-SDK ABI gate ran once after1419
package-build gates under BEFORE f23e53d3: both compiler calls returned0.
SixPFN types, reference16B/offset0,8 and helper signature passed.
Native object7132B/machine0xaa64/SHA256
b80443919f9c73c6ff8cddbbca8d6ef2fc75e7349f297238fefc454f1f3831e3.
All24 current tool/header/source pins, actual argv and eight downloads matched.
The coordinator verified all24 manifest files and raw object target:
native-receipts/artifact-manifest.json SHA256
fe23aab1804c7adc9f25723e3c6a128d2c8d3a19723051fa824e16bc7a8cee2d.
This remains object-only, no link, resolver integration or native execution.

Full1582tests205.202s:67 failures50 errors8 skips,117 signatures and NO added
failure/error against119 baseline. Two unrelated existing serial-guard tests
busy_port_rejects_before_command and lock_rejects_before_command passed in
this run; their source was unchanged, so no fix is claimed. Preserve the
comparison rather than rerunning to force the old count. Full log SHA256
c4a2daba8e82723645164b9d5685889c4e3d185ea90864bf79c3f0b5cdacec62,
.local/c15-zero-cache-reference-1420-fullsuite/comparison.json.
No hardware integration or R1/SM6 admission result is claimed.
