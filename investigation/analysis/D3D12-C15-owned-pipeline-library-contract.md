# C15 original owned pipeline recipe/library: typed R1 candidate

Design only, 2026-10-18. No implementation, compiler/build, guest, hardware,
negotiation/cap change, ledger update or commit. Adopt the existing bounded
pipeline-library-gate decision without repeating its absence search. This note
specifies a positive CPU implementation alternative. It does not declare the
current0003 driver or the proposed0011 rollout complete.

## Source inventory and current ownership gap

Inspected source HEAD `312455af80e005131ba9e9c8108fb8a973c58670`, specifically
`drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp` root/state/shader/PSO definitions,
calc/create/destroy paths, compile-on-use and native owner header; existing
`D3D12-C15-pipeline-library-gate.md` and `D3D12-C15-ddi-model-contract.md`.
Pinned `.local/reference/wdk26100/d3d12umddi.h` SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`:
R1 definitions3180–3469, IO signatures2069 onward, input-element2882 onward.
No external implementation or sample code was copied.

WINDOWS CONTRACT: target `D3D12DDI_SUPPORTED_0011`, R1 interface0x000c000a/build1,
with typed CORE_0010. Six library callbacks accompany twenty changed state,
shader and PSO callback fields. Command-list/queue ABI is unchanged at this
boundary. LibraryReference carries a library handle plus UINT PipelineIndex;
public names are owned by runtime, not a string parameter supplied to this DDI.

Current shader private objects own raw tokens but discard IO signature inputs.
State objects memcpy old descriptors. Current PSO owns CS/VS/PS token copies,
blend/raster/depth values and at most16 input elements, but `desc` still contains
runtime/private handles. Root data lives in its separate runtime allocation.
Native compute/graphics members contain pipe/compiler state pointers. Therefore
neither current PSO memory nor its descriptor is a persistent recipe.

AGX/ASAHI CONTRACT: recipe/library storage needs no screen, context, BO, GPUVA,
shader execution or native AGX compilation. A loaded logical PSO can lazily
compile again through the same validated frontend/owner path as an ordinary PSO.
The native state belongs to that resulting PSO/device; it is not shared by saving
context-bound pointers in a library. Owner synchronization is a separate gate.

Microsoft public contracts, paraphrased:
[CreatePipelineLibrary](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device1-createpipelinelibrary)
requires input integrity/version/hardware compatibility and avoids duplicate
identical subcomponents. Its library is generally synchronized internally;
concurrent loads of the same PSO require caller synchronization. The driver may
own an additional copy of its opaque payload; public borrowed-input lifetime
does not require borrowing private driver storage.
[StorePipeline](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12pipelinelibrary-storepipeline)
rejects overwrite of an existing public name and may fail for storage exhaustion.
[LoadGraphicsPipeline](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12pipelinelibrary-loadgraphicspipeline)
compares the requested description with stored data; missing/mismatched pipelines
are errors. [Serialize](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12pipelinelibrary-serialize)
allows later-process use, requires sufficient output memory, and does not promise
portability across driver/runtime updates or devices. These API contracts do not
by themselves settle their exact HRESULT translation at the opaque DDI boundary.

## Original in-memory model: no borrowed state handles

Use fixed-size runtime-private headers for shader/state/root/PSO/library handles.
Each header has a type tag, current device cookie, construction state and a
retained immutable CPU component/recipe reference. The runtime still owns the
header allocation; the driver owns referenced allocations. Calc functions return
checked fixed header sizes, not variable token counts inferred from possibly
elided library input. The same decoder serves typed ordinary and reference paths.
Failed creates leave a destructible EMPTY header and release all temporary refs.

An immutable **component** owns canonical bytes, kind/schema, length, strong
reference count and content digest. Equal components are interned only after
kind/length/digest AND exact-byte equality. A digest collision cannot alias
unequal input. Reference ownership never depends on a live shader/root/state/PSO
private pointer. No component owns a library, preventing a library/PSO cycle.

An immutable **recipe** retains the following logical components:

| Component | Canonical contents |
| --- | --- |
|Root|Flags; ordered parameters with type/visibility/register/space/count; ordered ranges with type/count/base register/space/explicit resolved offset; ordered static sampler semantic fields. Preserve root parameter order and DWORD cost; no root handle or source array pointer.|
|Shader per stage|Stage, exact encoding/model/entry/flags, owned verified DXBC token or DXIL program-body span, canonical IO signatures, root compatibility key. Preserve declared program bytes; container/body discriminators cannot be interchanged.|
|IO metadata|Presence discriminator and field-wise signature arrays for the exact negotiated revision. Serialize SystemValue/Register/Mask/component type/min precision, never C padding.0012's Stream byte is not a0011 semantic field. Tessellation/SO schema covers their distinct arrays if those stages become implemented.|
|Element layout|Ordered InputSlot/AlignedByteOffset/Format/InputSlotClass/InstanceDataStepRate/InputRegister entries. Preserve the supported16-element limit without truncation.|
|Blend|AlphaToCoverageEnable, IndependentBlendEnable and all eight render-target semantic fields, normalized BOOLs; no LibraryReference.|
|Depth/stencil|All0010 semantic fields including FrontEnable/BackEnable, masks, comparison/write and both face operations. A current owner unable to express a combination must refuse it explicitly, not discard a field.|
|Rasterizer|All0010 semantic fields including ScissorEnable, ForcedSampleCount and ConservativeRasterizationMode, integer/float bit encodings; no padding/reference.|
|PSO|Compute-versus-graphics/stage-presence mask; component refs; SampleMask, strip-cut/topology, RT count/active formats, DSV, sample count/quality and NodeMask. No destination shader/PSO handle or native compiled result.|

Canonical floats retain exact validated IEEE32 bits, including signed zero;
reject disallowed nonfinite values by the ordinary state validator rather than
silently equating distinct inputs. Canonical order follows semantic array order;
do not sort root parameters or input elements. Unused/absent fields have explicit
presence tags and zero canonical encoding. This avoids hashing uninitialized
padding while preserving every consumed field. An encoder/decoder schema change
changes the compatibility key.

Initial functional profile is existing compute or VS/optionalPS graphics; other
stage families still need truthful typed refusal behavior until their separate
implementation. This library design does not expand stage/capability support.
A NULL PS remains distinct from an explicit shader. A NULL root signature is not
silently replaced with an unrelated empty signature.

Every ordinary PSO creation first owns one logical recipe. Add builds a library-
interned recipe graph from retained immutable components: a new component may be
retained from the PSO, while equal existing library components are reused instead
of retaining another copy. An equal recipe reuses the existing recipe too. This
does not retain the PSO handle or mutate its original recipe. Shader/state/root
destruction can then precede library storage or
load. Loaded shader/state/PSO objects retain components independently of library
lifetime. Native compilation remains lazy; original and loaded PSOs own separate
native states unless a later documented native sharing design is proved.

## Six callbacks and the typed reference family

| Exact callback suffix0010 | Minimal action |
| --- | --- |
|CalcPrivatePipelineLibrarySize|Return fixed library header size; no owner/BO/compiler, no imported pointer retained, no global last-size cache.|
|CreatePipelineLibrary|Initialize EMPTY header. Empty input creates an empty library; otherwise bounded parse/compatibility validation builds a temporary owned component/index graph. Publish READY only after full success.|
|DestroyPipelineLibrary|Close registered header, detach index references, release CPU graph exactly once. Returned objects continue through their own refs. No native PSO destruction through library entries.|
|AddPipelineStateToLibrary|Validate live device-local library/PSO handles, retain immutable recipe, capacity-check and atomically publish index->recipe. Identical duplicate index may be idempotent under the proposed internal policy; conflicting replacement is rejected pending exact index contract. Never borrow PSO memory or compile it.|
|CalcSerializedLibrarySize|Return constant schema arena length L for every live library, including empty. SIZE_T has no error channel: invalid-handle handling requires a separate error policy, not invented HRESULT return bits.|
|SerializeLibrary|Under library CPU lock, write one canonical snapshot into exactly L runtime-provided bytes. Validate handle/output presence; never consult a global previous Calc result or write a larger current size.|

All twenty changed CORE_0010 fields must use actual function-pointer signatures:
layout/blend/depth/raster calc/create; ordinary shader calc and VS/PS/GS/CS creates;
SO calc/create; tessellation calc and HS/DS creates; PSO calc/create. In particular
0010 PSO has its destination as a separate parameter; do not memcpy/cast old0001
arguments. Destroy functions must release the new owned refs even though their
function-pointer types did not change.

For nonzero LibraryReference, resolve the registered library and PipelineIndex
under the CPU lock and retain a recipe before releasing it. Select the requested
component/stage by the callback, never a latest-created shader. Explicit ordinary
inputs are canonicalized and compared when present; mismatches fail before native
creation. A referenced shader may obtain its verified owned program without
reading NULL pShaderCode. A referenced layout may obtain its owned element array
without dereferencing a NULL pVertexElements. Fixed headers remove calc/create
private-size dependence on those nullable payloads.

The exact scalar/pointer elision rules of nonzero-library callbacks remain an
unknown below: zero-valued scalar fields are real values, not a universal
"elided" marker. Do not infer that a NULL PS/root handle means either "inherit"
or "absent" without that callback's normative convention. The proposed storage
and CPU resolver can represent both cases; registration/admission waits for the
convention to be established and tested.

## Bounded stable wire schema v1

Original policy choice: **L=16MiB** per library's serialized driver payload.
This is a CPU storage limit, not a Windows/AGX register or capability. It is
unchanged by Add; overflow returns a real storage-allocation failure. Ordinary
PSOs need not fit this library policy to remain valid ordinary PSOs. The first
schema limits entries to1024 and components to8192 in addition to total L.
The limits are checked during import and Add. No ever-growing capacity exposed
after an earlier unsized Serialize allocation is allowed.

Define bytes explicitly; do not expose packed C structs as the format. All
integers are unsigned little-endian, all offsets absolute in this payload and
8-byte aligned. Header256 bytes:

| Offset | Field |
| ---: | --- |
|0|8-byte magic `AGXPLB1` followed by zero|
|8/12|u32 wire version1 / header size256|
|16/24|u64 total bytes L / used length|
|32/36|u32 pipeline count / component count|
|40/48/56|u64 pipeline-table / component-table / payload-data offsets|
|64|32-byte compiler+recipe compatibility key|
|96|32-byte stable adapter/target compatibility key|
|128|32-byte whole-arena integrity digest; treated as zeros when hashing|
|160|u64 negotiated DDI version encoding|
|168/172|u32 shader-model profile / semantic feature-profile version|
|176..255|zero reserved bytes|

Pipeline table records64 bytes: u32 PipelineIndex; u32 recipe component index;
32-byte recipe content digest; remaining24 bytes zero. Sort by numeric pipeline
index and prohibit duplicate indices. Component table records64 bytes: u32 kind,
u32 component schema, u64 offset, u64 length,32-byte content digest,8 zero bytes.
Sort components by(kind,schema,digest,length,exact bytes), with one entry for each
unique canonical component. Leaf kinds1..8 contain no component references; recipe
kind0x100 may reference leaves only. Assign sorted leaf table indices first, encode
recipe leaf indices, then hash/sort recipe records last. Thus wire hashing has no
circular dependence on table order. Internal logical recipe equality uses leaf
contents; wire recipe digests cover its actual encoded bytes, not pointers.
Component references are validated table indices; no self/cyclic graph is allowed.
Payload is compact aligned component data in
that order; all gaps and all bytes after used length are zero.

Serialize initializes the entire L-byte arena. It cannot leak heap padding,
addresses or uninitialized reserved bytes. Equivalent indexed contents produce
identical bytes independent of allocation addresses or Add insertion order.
Runtime-assigned PipelineIndex values remain preserved, so this is determinism
for the same indexed library rather than inventing equivalence across different
runtime name/index assignments.

Import requires exact L for schema1; validates header/count/offset/length
arithmetic by subtraction, table extents, pairwise disjoint physical component
intervals, ordering, reference kinds, complete recipe graph, zero reserved/gap
bytes, digests and compatibility before allocating/publishing the final graph.
No offset multiplication/addition may wrap on x86. No component points into the
borrowed blob after import. Reject duplicate canonical components rather than
retaining a noncanonical second copy. Byte hashes are integrity checks, not
permission to trust invalid lengths or accept adversarial executable input.
Re-run bounded program/root/state validation on imported canonical data; do not
invoke the shader compiler simply to parse the library.

## Compatibility, handles and synchronization

Compiler/recipe key is a digest of an explicitly serialized manifest: library
schema, UMD build identity, exact DDI version, shader-model/feature profile,
DXBC/DXIL input and signature schemas, owned binding-map convention, reviewed
frontend dependency lock, pinned Mesa/Asahi compiler identity and effective
compiler option profile. Do not hash a machine-local path or current pointer.
Changing any interpretation-bearing key rejects an old payload conservatively.

Adapter/target key uses stable validated vendor/device/revision and AGX target
architecture/feature identity from device info plus the chosen UMD build target.
Do not use a per-process device pointer/cookie, boot-ephemeral LUID or GPUVA in
the wire compatibility key. The initial policy makes no cross-CPU-architecture
reuse promise; wire decoding is portable, compatibility may reject another UMD
build target. Runtime's own framing/version checks remain distinct from ours.

In-memory provenance is stricter: every handle must be a currently registered
object of the expected kind for this device cookie. Resolve membership by pointer
identity before dereferencing untrusted private storage; a magic tag alone is
not a safe cross-device/stale-handle check. Runtime-valid object lifetime still
applies: a lock does not make an application use-after-release legal.

One per-library CPU lock protects index publication, interning and serialization;
lookup retains immutable refs before unlocking. Add prepares data outside lock,
rechecks capacity/index under lock, then commits atomically or rolls back.
Serialization and import perform no native/runtime callbacks under the lock.
Device registry lookup is short and not nested with the library lock: acquire a
retained library lease, release registry lock, then use library. Destroy marks
closing/removes registry visibility, drains valid leases, detaches refs, frees
outside locks. No library lock may be held while acquiring the owner transaction
gate, compiling or calling SetError. Existing owner callback-reentry uncertainty
is thereby excluded from CPU library storage, not claimed resolved globally.

## Failure transactions and deterministic tests

Use a small internal status enum that distinguishes malformed input, missing index,
wrong type/device, incompatible recipe/driver/adapter, unsupported semantic profile,
capacity/OOM and lifecycle error. The HRESULT-returning DDIs can map real validation
and allocation failures deliberately; VOID creates report through the correct
per-device error callback after locks are released and leave EMPTY objects.
Do not return HRESULT bit patterns from SIZE_T callbacks. Exact version/adapter
mismatch HRESULT propagation needs the normative DDI mapping listed below.

No failure may publish partial state, consume a preexisting index, drop an existing
component reference or overwrite a caller destination buffer with a partial valid
blob. Destroy-after-failed-create is safe. Inject failure at each allocation and
retain/publish boundary, then immediately repeat a valid operation to expose stale
construction or reference state.

WHAT REAL BUG OR INVARIANT WILL THESE TESTS CATCH?

1. Actual six callbacks plus typed wrappers using extracted pinned WDK structs/
   function-pointer types on x86 and ARM64: ABI shift/size/canary failures; zero
   owner/BO/compiler invocation for storage/query/import/serialize.
2. Actual canonicalizers: vary C padding while holding semantic fields fixed and
   require equal bytes; vary one meaningful0010 field and require a different
   recipe. Preserve root register spaces/order and shader IO presence/stage.
3. Create shader/root/states/PSO, Add, destroy all originals, overwrite their old
   private storage, resolve components and loaded PSO, destroy library, then use
   the returned object's CPU recipe. ASan and exact ref/destructor counts catch
   borrowed-handle lifetime errors. No native execution is needed.
4. Add same components through distinct PSOs: one stored component allocation,
   different pipeline indices retained; fake digest collision uses byte comparison
   and never aliases unequal content. Conflicting index/OOM leaves original intact.
5. Serialize/import/serialize byte equality, input-blob overwrite after Create,
   changed driver/model/compiler/adapter keys, malformed/overlapping/truncated
   tables, overflow nearUINT32_MAX andSIZE_MAX; all failed imports stay EMPTY.
6. Concurrent Add/CalcSize/Serialize with L-byte buffer canaries: never write beyond
   L, no mixed snapshot, deterministic importable output and no shared last-size
   state. Repeated serialize of empty/full libraries initializes all padding.
7. Reference and ordinary paths resolve equivalent logical recipes without NULL
   dereference; missing/wrong-type/cross-device index fails before any shader read.
   Tests for exact scalar elision wait for its documented convention.

## Minimum coherent implementation order and unknowns

1. Implement original field-wise CPU schemas, checked decoder, immutable refs and
   actual-function tests first. Keep0011 negotiation/caps unchanged.
2. Adapt ordinary object storage/teardown to owned headers/recipes; preserve current
   DXBC ordinary behavior and native lazy-compile inputs, proving old shader/state
   destruction order independently of libraries.
3. Implement registered CPU library/index/dedup/constant-L serialize/import family;
   prove all lifecycle, parser and concurrent-canary gates without native owner.
4. Add all twenty typed0010 wrappers and reference resolution, with signature-
   correct table assignment tests. Complete the elision/error mapping sources
   before claiming nonzero-reference path complete. Keep unimplemented stages
   explicitly outside the current functional profile.
5. Only after the full R1 model and separate compiler/owner gates pass, preregister
   the smallest remaining runtime ordering checkpoint. No hardware bit probing.

WHAT IS STILL UNKNOWN: exact nonzero LibraryReference data elision per callback;
conflicting/repeated PipelineIndex contract; driver-versus-runtime framing of
InitialDataSize/pInitialData/pBlob; and exact DDI HRESULT propagation of driver/
adapter mismatch. Constant L removes size-growth overflow, not these semantic
questions. Public API docs alone cannot resolve them. A positive CPU library
implementation also does not resolve the separate native owner callback reentry
contract, DXIL runtime integration, unsupported shader stages or full R1 admission.

Source/license boundary: original design and wire schema; Microsoft API statements
are linked paraphrases, WDK declarations inspected locally. No external driver,
cache implementation, shader compiler implementation or sample was copied.

## One bounded follow-up on four exact DDI unknowns

Read-only follow-up2026-10-18, source HEAD
`bb637bef2e826871964e7eea287b312f3d5ade7f`. This does not reopen the absence-gate
search or change the proposed16MiB/1024/8192 private policies into interface facts.
No implementation, test, compiler, build, guest, ledger or commit in this pass.

### Pinned declarations and official source endpoints

Rechecked the same WDK26100 SHA above at3192–3351: LibraryReference has exactly
hLibrary/PipelineIndex with no comments specifying elision or replacement.
CREATE_SHADER_0010 includes the ordinary code/root/signature fields alongside
LibraryReference; CREATE_PIPELINE_STATE_0010 similarly includes all handles and
scalar values alongside it. Their `_In_` annotations apply to the outer argument
structure, not a conditional rule for its interior fields. They do not prove
that any particular pointer is elided or any zero scalar is ignored on a load.

Official Microsoft pages inspected directly (retrieved2026-10-18; each available
DDI page below identifies its content update as2024-02-22):

- [LibraryReference0010](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddi_library_reference_0010):
  identifies the handle and index, with no load-elision/repeated-index rules.
- [CreateShader0010](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_create_shader_0010)
  and [CreatePipelineState0010](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_create_pipeline_state_0010):
  describe arguments and return kinds; neither supplies conditional field semantics.
- [AddPipelineStateToLibrary0010](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_add_pipeline_state_to_library_0010):
  defines the operation/parameters but gives no uniqueness, monotonicity, repeated
  index, replacement or idempotency rule. Its return description is generic HRESULT.
- [CreatePipelineLibrary0010](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_create_pipeline_library_0010)
  and [SerializeLibrary0010](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_serialize_library_0010):
  give opaque arguments/return kind without a runtime-header layout, transform,
  payload extraction contract, capacity mapping or mismatch HRESULT table.

The direct CREATE_SHADER_0010 and CREATE_PIPELINE_LIBRARY_0010 structure pages and
CalcSerializedLibrarySize0010 callback page were unavailable through the endpoint;
corresponding exact declarations were read locally. Missing pages are not evidence
of semantics. No older experiment or runtime reverse-engineering was substituted.
The pinned Microsoft [ShaderCache.md](https://github.com/microsoft/DirectX-Specs/blob/71e431fa0f713818c6a3a66f49ab0e6732712d15/d3d/ShaderCache.md)
was checked for these pipeline-library terms; it describes different cache services
and supplies none of the four missing early-library rules. A bounded official-site
search produced later references to the same structure but no normative early
elision/framing/replacement specification. Stop here rather than a second pass.

### What is actually resolved and what is not

| Question | Proven boundary | Remaining unproved rule |
| --- | --- | --- |
|Nonzero LibraryReference|The typed struct carries a library handle and UINT pipeline index; shader/state/PSO callbacks retain their other explicit input fields.|Which ordinary fields remain authoritative, may be absent, must match or are ignored; zero/NULL cannot be assigned universal inheritance semantics.|
|Repeated PipelineIndex|Add receives an explicit UINT; public StorePipeline forbids duplicate names, but the UMD receives no name.|Whether one name/index correspondence is stable, whether an equal repeated index is valid, and whether an index can ever be recycled/replaced. Proposed idempotence is still only a private candidate policy.|
|Opaque serialization framing|WDK provides InitialDataSize/pInitialData on Create and unsized pBlob on Serialize. Public Serialize has its own size argument and public blob lifetime rules.|Whether the driver payload equals the whole API blob or a subregion; any exact runtime prefix/suffix or rebasing; guaranteed pairing/order between driver size and serialization. Do not parse a guessed public header in the driver.|
|Error mapping|Public API documents invalid/corrupt and version/adapter errors. SDK names their concrete HRESULTs.|Which callback returns each failure to runtime and whether runtime translates, intercepts or produces that API result itself. The generic DDI HRESULT wording does not prove identity mapping.|

The error constants themselves are now pinned in
`.local/reference/winsdk26100/shared/winerror.h` SHA256
`0284ea46280c14063bb714d08c9ec48de18de6a7f0384323b263fec234428833`,
lines59832–59847:
D3D12_ERROR_ADAPTER_NOT_FOUND=0x887e0001 and
D3D12_ERROR_DRIVER_VERSION_MISMATCH=0x887e0002. Their comments concern mismatched
adapter and driver blob version respectively. This resolves their definitions and
intended API meaning; it is NOT a newly discovered DDI propagation instruction.
The [public CreatePipelineLibrary page](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device1-createpipelinelibrary)
is the source for their public return behavior. No corresponding early callback
page enumerated them in the inspected material.

### Consequence for the proposed CPU work

The private canonical encoder/decoder, owned references, deduplication, bounded
allocation, compatibility classifier and destruction/OOM tests can be specified
without those missing runtime semantics. A pure CPU internal library module can
therefore be implemented and tested independently as a future step; its private
status enum and serialization policy must remain explicit. These gates cannot
claim to complete nonzero-reference R1 DDIs or justify changing GetVersions.

A typed runtime-facing resolver must remain an unimplemented integration boundary
until the per-callback elision and framing rules are established. Do not silently
choose the note's possible inheritance/idempotence behavior as a Windows fact.
Likewise the16MiB arena,1024entries and8192components are original CPU resource
limits that may be revised before implementation; none is asserted by WDK or
Microsoft documentation. No hardware experiment is proposed to discover any of
these documented-interface questions by trial.

## 2026-10-20 CPU envelope checkpoint (EXP1770)

The original `agx_d3d12_library_wire_envelope.h` adds only the first structural
check for the proposed 16 MiB private arena. Its scope is deliberately smaller
than import: it checks magic/version/header/arena size, bounded table counts,
8-byte offset alignment, subtraction-based table extents, table ordering and
reserved header bytes. It returns offsets and counts only after all these checks
pass and clears output on failure. It does not inspect entries, zero gaps,
verify digests or compatibility keys, follow component references, own memory,
or publish a library. No DDI callback, model/capability, package or GPU path
uses it yet.

The deterministic invariant is that malformed outer lengths never reach an
entry reader or wrap an extent calculation. The new actual-header fixture tests
empty and nonempty envelopes, NULL/truncated buffers, count limits, overlarge
and misaligned offsets, table overlap and reserved bytes. The test was RED with
a missing header, then GREEN under ASan/UBSan; the adjacent D3D12 ABI/native
owner and ledger suite is also GREEN. Next implement the remaining checked
component-table/payload/digest/compatibility validation in one bounded CPU
import transaction before connecting this predicate to the zero-cache library.
The exact nonzero LibraryReference elision/error rules and full R1/SM6 gates
above remain unknown.

## 2026-10-20 CPU table checkpoint (EXP1771)

The next original helper `agx_d3d12_library_wire_tables.h` consumes the accepted
envelope and a caller-provided bounded interval scratch array. It validates
pipeline index order and recipe targets, component kind/schema, component
ordering by the declared canonical tuple, reserved record bytes, payload
bounds/overlap and all zero gaps through the end of the fixed arena. The helper
allocates nothing, calls no runtime/driver/GPU callback, publishes no object and
clears output on every refusal. Empty libraries need no scratch pointer. Its
scratch-capacity refusal is a future import OOM gate, not an HRESULT mapping.

The actual-header fixture was RED while the helper was absent, then GREEN under
ASan/UBSan for valid empty/nonempty/gapped buffers and malformed references,
range overlaps, out-of-range offsets, nonzero gaps/trailing bytes, wrong schema
and wrong component order. The targeted D3D12 ABI/native-owner/ledger suite has
34 PASS. What remains: compute and verify per-component/whole-arena digests,
compatibility keys, actual component contents and a complete acyclic recipe
graph; construct owned refs transactionally and reject failures without
publishing. Neither this predicate nor EXP1770 justifies a library callback or
R1/SM6 advertisement.

## 2026-10-20 CPU digest/identity checkpoint (EXP1772)

`agx_d3d12_library_wire_digest.h` composes the structural predicates with a
caller-supplied SHA-256 callback. It checks every component payload against its
record digest, each pipeline recipe digest against the referenced component,
and the whole 16 MiB arena with only the digest field treated as zero. It also
requires exact caller-provided compiler, adapter, DDI, shader-model and semantic
profile identities. It cannot accept a NULL digest backend or key. No input
buffer is modified and no object is published. The host fixture uses system
CommonCrypto solely to exercise the callback interface; a Windows CNG backend
is not yet wired into the UMD.

The actual-header test was RED while the helper was missing, then GREEN under
ASan/UBSan for valid bytes, component/pipeline/whole-arena corruption, wrong
identity/profile, backend failure and NULL inputs. The targeted D3D12 ABI,
owner and ledger suite totals 35 PASS. This still does not validate component
semantics, an acyclic recipe graph, runtime framing or transactional ownership.
It is an offline CPU gate and makes no R1/SM6 capability claim.
