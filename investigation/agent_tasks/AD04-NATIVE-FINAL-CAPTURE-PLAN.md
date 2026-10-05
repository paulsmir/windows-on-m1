# AD04 native PPP/CF capture contract — 2026-09-13

Status: source-derived implementation plan; no hardware authorization implied by
this document and no new hardware result. Adapter normalization is edd25d7;
fresh x64 native pool/pipeline/adapter execution and ARM64 build/link pass were
reported by the integration owner. Physical/patch-list architecture is retained.
GPUVA remains CLOSED/NO. Accelerated desktop acceptance precedes OpenGL/CS1.6.

## Decision and next implementation unit

Implement one offline **v3 native PPP subgraph contract**: an exact read-only
PPP source role backed by the existing General allocation class, a typed
PPP-to-USC pipeline field, and a typed PPP-to-CF-bindings field. Prove the graph
`Encoder -> PPP -> {fragment USC, CF descriptor}` with native pack/unpack,
Windows owner facts and two materializer placements. This is useful independently
of full agx_encode_state runtime linking. Do not enable native dispatch yet.

This does not create an allocation class or relax Encoder allocation ownership.
Native PPP is a suballocation of the mixed native batch pool, whose Windows BO
is correctly General. The new role identifies which exact immutable source
bytes must be copied and patched. Calling those bytes Encoder would violate the
existing class restriction and calling them Descriptor would lose PPP-specific
outgoing-field policy.

## Inspected primary sources and observed contract

Mesa paths below are relative to the pinned source root
`/Users/pavel/public_windows/.local/reference/mesa`. Inspected agx_state.c SHA256:
`5015b75863202a170f8d6015eb82a76a70ecd4b92204894fa3d1886b1baa94ba`.

- `src/gallium/drivers/asahi/agx_state.c:3423` agx_encode_state;
  `:3470-3490` VDM vertex USC; `:3557` native PPP allocation from batch->pool;
  `:3678-3685` fragment USC and CF bindings fields; `:3706` PPP finalization.
- Same file `:937-1025` viewport/scissor PPP; `:3273-3337` initial batch PPP;
  `:3450-3466` CF linkage allocated from pipeline_pool using native header plus
  binding count; `:3053` agx_build_bg_eot; `:5203-5249` state then actual draw.
- `src/asahi/lib/agx_ppp.h:125-145`: native PPP_STATE packs pointer high8/low32,
  preserves the word count and block bits, and advances VDM by eight bytes.
- `src/asahi/genxml/cmdbuf.xml:623-630`: fragment USC relative32 field has six
  low non-address bits; CF binding relative32 field has two low non-address
  bits. `:749-754` PPP_STATE layout; `:799` VS USC field.
- `src/gallium/drivers/asahi/agx_pipe.c:1548-1603`: real render finalization
  appends VDM termination, builds BG/partial/EOT pipelines and uploads final
  scissor/depth-bias arrays. `:1607` is the flush entry point to replace on
  Windows. `agx_batch.c:632,643,829` documents attachment ownership and Linux
  submit/synchronization, which Windows must not import.
- `src/gallium/drivers/asahi/agx_uniforms.c:26-79,109-150` proves that root and
  stage uniforms contain nested VBO/table/UBO/SSBO pointers; they are not closed
  by merely capturing a USC uniform reference.

Repository sources inspected:
`mesa/winsys/agx_win32_asahi_pipeline.c`, `agx_win32_asahi_capture.c`,
`agx_win32_reloc_capture.[ch]`, `agx_win32_reloc_capture_test.c`,
`shared/include/apple_agx_win32_abi.h`, `shared/src/apple_agx_win32_abi.c`,
`render-admission/src/render_win32_transport.c`, `apple_agx_dynamic_job.c`,
`render_dynamic_dma.c`, and UMD composer/adapter source and Windows tests.
All repository paths in this paragraph are under `drivers/apple-agx/`.

This unit changes the private versioned command ABI, not a Microsoft DDI,
allocation callback signature, capability, scheduling model, interrupt, DMA
ownership or firmware initialization. The existing composer owns runtime-thread
pfnRenderCb, dense allocation indices, Windows submission holds and ordered
completion; native capture owns source BO references. KMD owns validation,
request placement, patching and hardware submission. m1n1/Mu/power/IRQ behavior
is unchanged and is not implicated by this deterministic offline boundary.

License: the inspected Asahi/Mesa implementation carries MIT notices (including
agx_uniforms.c). The plan derives observable packing/ownership semantics. It
copies no upstream implementation. Any generated inclusion of native helpers
must retain existing notices and use the already reviewed pinned-source process;
new source copying requires an explicit compatibility review first.

## Exact private v3 contract

Proposed symbolic names/numbers are new and must be checked for conflicts before
implementation; existing numbers and structures do not change.

| Element | Exact admission and encoding |
| --- | --- |
| `AppleAgxWin32RolePppState = 13` | v3 only, Access exactly Read, existing General allocation class only, nonzero exact source span, offset and length multiples of four. No Write or Execute. No universal byte-span exception. |
| Existing `PppStateAddress40` | Preserve existing Encoder->Encoder behavior. Add only v3 Encoder->PppState. Width eight, source/target offsets aligned four, address below 2^40. Preserve size/block bits. Validate packed native PPP size against the captured target range in producer tests. |
| `PppPipelineOffset32 = 10` | v3 only, PppState->UscPipeline only, width four, source offset aligned four. Address must be >= ShaderBase; difference <= UINT32_MAX and aligned 64. Preserve low six bits. Separate kind avoids silently broadening the VDM kind. |
| `PppCfBindingsOffset32 = 11` | v3 only, PppState->Descriptor only, width four, source offset and target offset aligned four. Address must be >= ShaderBase; difference <= UINT32_MAX and aligned four. Preserve low two bits. Descriptor remains read-only; capture uses the real linkage byte count and pipeline_pool Windows identity. |

CF's native allocation is aligned 16 but its documented packed address contract
requires four; do not accidentally impose the USC 64-byte rule. The materializer
must use the supplied ShaderBase and resolved request placement, never the native
construction address or a presumed fixed 0x1100000000 base. A zero CF field with
zero bindings has no relocation/target. A nonzero binding count must have an exact
tracked linkage source and edge; omission fails the producer's completeness gate.

Role13 references and kinds10/11 are rejected by v1/v2, including when a caller
constructs the wire packet directly. The new role is transitively reachable from
the existing EncoderReference; add no payload field or root. Preserve old role,
class, offset, access, overlap and legacy version behavior. Keep the 16-reference,
64-relocation and 4096-byte command capacities; exceeding them fails closed.

## Implementation scope and ownership

1. Extend the role/version/access validators and capture role bounds. Make
   relocation policy version-aware for the one existing PPP edge alternative.
   Extend width/alignment validation for the two new four-byte kinds.
2. Add an explicit PppState class branch in render_win32_transport.c requiring
   v3 + exact Read + General. Encoder/UscPipeline still require Encoder class.
   Do not admit arbitrary General-backed encoder bytes or rollover.
3. Add PppState to dynamic_copy_role. Copy exactly Reference.Bytes from the
   source before patching, then apply both masked relative encodings. Never
   modify original source BO bytes. CF already uses copied Descriptor semantics.
   Resolve every target through the existing per-request callback.
4. Add a narrowly typed PPP emission helper which records the native exact PPP
   interval and its field locations, resolves owner identities, and rejects
   mismatched packed addresses. For field references to already-captured USC,
   reuse the existing exact reference instead of overlapping a guessed span.
   Validate all byte ranges with overflow-safe arithmetic.
5. Exercise the native header/pack/finalization helpers through the existing
   pinned Windows test mechanism without linking or replacing the complete
   agx_encode_state body. Retain source/header digests and real helper inclusion.
   Hooking the full producer is a subsequent tested unit once runtime closure
   and remaining graph edges are complete.

The unit does not broaden render_dynamic_dma or the legacy native overlay to
accept a new graph. Those currently enforce older accepted shapes; modifying
that gate before full root/materialization mapping would allow premature dispatch.
This unit proves capture/ABI/reference validation/materializer semantics only.

## Deterministic RED/GREEN evidence

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
General-backed native PPP is currently rejected; its relative CF pointer cannot
be represented by the existing 64-aligned USC relocation. A wrong mask would
corrupt native flags or leave a construction-time address after materialization.

Add focused cases to the existing capture/materializer and Windows owner suites:

- Native packed PPP and CF records, with distinct source/target suballocations,
  exact source identities and native expected sizes. Include a CF placement
  aligned four but not 64 to catch accidental reuse of USC alignment.
- v3 positive graph at two valid placements; unpack pointers to confirm they
  resolve to the placement's USC/CF/PPP addresses and preserve low flag bits,
  PPP size/block bits and all unrelated source bytes.
- Source bytes unchanged after both materializations; first output remains
  unchanged after producing the second; each PPP source is copied exactly once.
- Reject wrong class (including Encoder class for dedicated native PPP), Write,
  Execute, v1/v2 role/kinds, wrong source/target roles, unreachable PPP/CF,
  truncated/overlapping source intervals and stale owner identities.
- Reject relative target below ShaderBase, >32-bit delta, misalignment, target
  out of range and capacity exhaustion; rejected materialization leaves no
  apparently usable job/output. Exercise zero-bindings with no target separately.
- Prove old Encoder->Encoder PPP behavior and General Encoder rejection remain
  unchanged. Existing composer and adapter hold/failure suites remain GREEN.

Run the relevant host sanitizer capture suite, reference/class suite, then fresh
Windows x64 execution and ARM64 build/link. Expected pre-fix RED is unsupported
role/kind/class admission, not an artificial source-text assertion. A coherent
commit gets a CHANGES.csv row with status=implemented; no hardware claim.

## Supported producer subset and next gates

The next complete native producer should initially admit exactly one direct,
non-indexed triangle-list draw, first vertex/instance zero, one instance, one
uncompressed BGRA8 color attachment, no depth/stencil, no compute/GS/tessellation,
no scratch, no indirect/bindless/queries/streamout, and no encoder rollover.
These are fail-closed implementation limits, not advertised Full Graphics
capabilities or final desktop acceptance. Native shader/VBO/uniform and attachment
source construction must remain real. Do not manufacture missing tables/roots to
satisfy mandatory payload fields. Any ordinary producer path that cannot be
represented must be rejected before adapter Seal/Dispatch.

Even this subset needs capture of initial PPP, viewport PPP, complete VDM draw
and termination, root/stage uniform pointer edges, final scissor/depth-bias data,
BG/partial/EOT pipelines and PBE/texture attachment addresses. Keep capture active
from allocation/initial state through finalization; freeze source bytes after
seal and retain all native/Windows holds to the same completion marker. Reject
any remaining unknown address-bearing field. A census of addresses and supported
modes is a dispatch precondition, not a hardware experiment.

Only after those deterministic gates and Windows-native runtime closure pass may
Windows flush call the adapter exactly once from the supported runtime thread.
Use existing physical Render/Patch/Submit and completion protocol. Native batch
cleanup occurs only after successful retirement; timeout keeps storage and both
hold sets. Linux syncobj/virtio/shared-BO ioctl semantics remain absent.

The smallest eventual hardware checkpoint is one completely captured native
Windows-originated request reaching the existing physical AGX completion and
Windows fence, with the preregistered output evidence. It is not desktop
acceptance. Preregister hashes/manifests/hypothesis and all playbook health gates;
use ordinary GPU-visible exact-package rollback on failure. This PPP contract
has no hardware uncertainty and therefore needs no hardware run by itself.
