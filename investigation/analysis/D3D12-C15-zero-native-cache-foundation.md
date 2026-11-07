# C15 ZERO_NATIVE_CACHE library foundation: EXP1418

Original MIT source, unregistered and unlinked. This checkpoint changes no main
DDI, CORE table, model/profile/capability, PSO layout, owner or build script.
Execution was preregistered under EXP1418 BEFORE and the receipts below.
No synthetic RED is proposed for a new deterministic callback contract.

## Source-derived contract

The pinned 26100 `d3d12umddi.h`, SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`,
lines 3328–3351 supplies the create record and all six callback typedefs. These
are distinct SIZE_T/HRESULT/VOID functions, not interchangeable error stubs.
`D3D12-C15-runtime-r1-contract.md` and immutable local manifests record the exact
runtime's ordinary shader/PSO forwarding. The further zero-cache source proof is
`.local/c15-runtime-library-cache-contract/zero-cache-manifest.json`, SHA256
`b3811c27698e49327f2a15f1485614775db01ed324be32987c210b3185273358`.

For the inspected runtime's early-R1 ordinals 2/3, public library support is
present; it cannot be suppressed by an absent driver cache. Runtime metadata
owns names/hash/index information. Valid load misses forward full caller
pipeline descriptions to ordinary shader/state/PSO creation. Driver opaque bytes
may be zero, but the runtime still calls SerializeLibrary. Import passes only
the driver span after validated outer framing; size zero can accompany NULL or
a nonnull end pointer. The public LoadCompute/Graphics APIs require full pDesc;
that does not replace the separately recorded implementation evidence.

## New module and invariants

`agx_d3d12_zero_cache_library.{h,cpp}` implements the actual six typed callbacks:

| Callback | Foundation behavior |
| --- | --- |
| CalcPrivatePipelineLibrarySize | Fixed private-header size for a live device and zero-size initial data; otherwise zero. |
| CreatePipelineLibrary | Initialize one runtime-provided header; accept zero input size without reading pInitialData; reject nonzero opaque input and duplicate live storage. |
| DestroyPipelineLibrary | Unlink and clear only the matching live, owned header. Invalid/foreign/destroyed handles cannot corrupt another header. |
| AddPipelineStateToLibrary | Check live device/library/PSO provenance, including an owned uncompiled PSO; index is unused even at UINT_MAX. No PSO reference or per-index cache is retained. |
| CalcSerializedLibrarySize | Stable zero opaque bytes. Invalid handles also return zero because this signature has no error channel. |
| SerializeLibrary | Validate the library, return S_OK and write exactly zero bytes. No output pointer is dereferenced. |

Create's destination has the normal DDI precondition: writable, appropriately
aligned, nonoverlapping CalcPrivate-sized runtime storage. This module does not
promise safe writes to an arbitrary invalid Create destination. It never reads
uninitialized destination contents. Duplicate/cross-device live destinations are
recognized before any write. Other live-handle operations compare an unknown
address against the registry before dereferencing that address.

A process-wide intrusive CPU registry is protected by an SRW lock. It needs no
allocator: linkage lives in runtime private headers. It serializes only library
registration/live lookup/removal. This is not a GPU/context/compiler mutex and
is not a proposed repair for unknown native concurrency. No allocation, GPU
operation, external ownership resolver or error callback occurs inside its
critical section.

All source is original. No upstream implementation or SDK implementation was
copied. Actual WDK record/typedef declarations are extracted only into ignored
host fixture data as explicitly requested.

## Explicit ownership resolver boundary

Two external C interfaces are declarations only in this module:

- `AgxD3d12ZeroCacheResolveDevice(hDevice)` returns a stable, live device identity
  or NULL. The identity is opaque; the foundation never dereferences it.
- `AgxD3d12ZeroCacheResolvePsoOwner(hPipelineState)` returns its live owner's
  identity or NULL, regardless of whether native PSO compilation has occurred.

There is no runtime-configurable resolver setter and no guessed PSO field.
Hooks run before acquiring the library registry lock. They must return stable
identity snapshots under the eventual adapter's ownership/lifetime policy and
must not throw or invoke GPU work. The host fixture supplies original stand-ins;
no production definition is added.

The future main-DDI integration must enroll device and PSO ownership at their
actual successful creation boundaries and retire it at their actual destruction
boundaries. A sidecar registry can associate the creating DDI hDevice with a
live PSO allocation without changing or guessing its current private layout.
Device identities cannot be reused while child library callbacks/storage remain
live. Runtime-supplied new storage must outlive registration and be removed
before its allocation is released. Existing COM/DDI child lifetime and a future
sidecar error/OOM policy must be proved before integration. Stale-address ABA
across invalid same-address object reuse is not claimed to be detected.

## Deterministic host checkpoint

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Reading a zero-length initial
span, writing beyond zero serialized extent, dereferencing an unknown library
handle, corrupting a foreign live header, accepting a foreign/dead PSO, retaining
an unnecessary compiled-object requirement, or treating an opaque index as an
array/allocation size.

The host renderer enforces the exact WDK hash, extracts the actual create record
and six callback typedefs, and supplies an original small Windows handle/SRW
shim. The actual new module translation unit is compiled unchanged against that
overlay; it is not reimplemented or copied into the behavioral fixture. Six
static type checks are in the module. This proves host declaration compatibility
only, not Windows calling convention or Windows ABI.

The fixture covers all six callbacks, inaccessible PROT_NONE initial/output
pointers, NULL/non-null end pointers, output/private-storage canaries, unknown
and cross-device handles, duplicate creation, destroyed libraries and resolver-
reported destroyed PSOs, an owned uncompiled PSO, and UINT_MAX without per-index
state. It verifies unrelated libraries survive invalid operations and removal.
All objects are destroyed before fixture exit. There is no translator/native
compiler, GPU or driver cache in this test.

Preparation freezes module/header, fixture/shim/renderer/runner, actual WDK,
generated declaration inputs, compiler/SDK/runtime pins and exact command argv.
The opt-in runner refuses any mismatch before invoking the compiler and refuses
an existing receipt root. Neither source preparation nor default test discovery
invokes a compiler. Execution requires a parent-approved manifest hash and BEFORE.

The separate Windows gate compiles the actual module to an ARM64 COFF object
with real Windows SDK/WDK headers and its six callback static assertions. A
second original `/Zs` translation unit checks actual SDK sizes and offsets. It
does not link the unresolved resolver hooks or prove production lifetimes.

## Remaining admission gates

- Main-DDI integration and eventual Windows public library roundtrip; the
  isolated host behavior, Windows compilation and independent source review
  below do not close these integration gates.
- Main-DDI ownership resolver/sidecar lifetime and failure policy, typed ordinary
  R1 wrappers, initialization/teardown ordering and no invalid partial objects.
- Owned ordinary shader/root/state/PSO bytes and exact framing acquisition; no
  assumption that a library reference supplies omitted data.
- Translator internal OOM policy, owner/TLS ownership, shared context concurrency
  and callback reentry. No general recoverable-OOM claim is made here.
- Real Windows Store/Serialize/import/Load roundtrip and pixel/word oracles after
  source/build/model/companion gates. A synthetic runtime clone is not that proof.
- Truthful feature/profile coverage before any R1/SM6 advertisement or production
  registration. Current CORE0003 remains unchanged.

Native compiled-binary caching and per-index recipe storage are deliberately
absent from this bounded policy. The six lifecycle/serialization operations are
implemented successfully for its supported zero-opaque format; no capability
absence is inferred from the serialized size being zero.

## EXP1418 isolated gate results

Independent source review found a real C++17 raw-storage lifetime issue before
execution: Create now starts the trivial header lifetime using placement new,
after duplicate-storage rejection. The frozen source and behavior expectations
were then unchanged across host and Windows gates.

Host attempt1 stopped before compiler/output creation because the receipt path
used the /Users symlink while the guard compared its /Volumes canonical path.
Retry1 changed only the canonical receipt-root/token in approval v4; the source,
compiler and two command arrays were unchanged. Actual module plus original
fixture passed ASan/UBSan: `checks=59 callbacks=6 actual_WDK_declarations=YES
host_Windows_ABI=NO no_registration=YES`, compiler/run exit0. Host output manifest
`.local/c15-zero-cache-1418/host/output-manifest.json`, SHA256
`24eba427e7bdf3672676ed1536dcba2937746b24bf360870bab941b0f3d7f1fb`,
eight entries independently checked by the coordinator.

Windows BEFORE c6d5dfa6 froze 23 compiler/header/source pins and the exact two
native commands. Actual MSVC ARM64 19.44.35228.0 compiled the module `/c /W4 /WX
/sdl /std:c++17`, exit0. The original actual-SDK ABI TU `/Zs` also returned0:
six callback pointers are8B, the create record is16B with size/pointer offsets
0/8. The native object is6437B, raw COFF machine0xaa64, SHA256
`117553279866ac147cae3b13d9d45b0b6f7c6e7739bdcf011e7780f30fa6522d`.
The recorded argv exactly matched approval and all23 current remote pins matched
an independent post-run rehash. The coordinator checked all21 saved artifacts
and raw object target. Manifest `.local/c15-zero-cache-1418/windows/artifact-manifest.json`
SHA256 `58d66143a6d2d739005ad5ee426b3eef13e0018c0915683f377461f2686b4033`.
An initial read-only post-capture query misread PowerShell's Inputs wrapper; its
error is preserved and a second metadata query used Inputs.value. The compiler
and frozen script ran once. No linking, resolver definitions, module execution,
GPU work, CORE registration or capability change was performed.

Parent full suite:1582 tests205.648s,68 failures51 errors8 skips. All119 failure/error signatures exactly match the frozen1581-test C22 baseline; no added/removed signature. The extra default skip is this explicitly approved opt-in fixture, whose separate actual execution passed59 checks. Full log SHA256 `f1010e1a32bd6d60c61a12c88f4fcedd55fa4717464973ea2ffc8ed192fe2196`; comparison `.local/c15-zero-cache-1418-fullsuite/comparison.json`. This is unchanged baseline, not a passing full suite.
