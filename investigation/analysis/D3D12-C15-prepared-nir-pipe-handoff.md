# C15 prepared NIR callback handoff — accepted next source gate

Root read the full design and proposed C header, independently verified all nine CURRENT source pins and both proposal hashes, and accepted this bounded next implementation after actual EXP1487. Source READY SHA `48f4f81d04b45377bc6d7f5b55818d676b96d913c9097ad9237d4274dcd59428`; design SHA `7eb24f5e0aa38fd08416568566630e9010688130dcbf94e2ae17eafa72861718`. Original source proposals are private; no remote/native execution is authorized by this note alone. Root owns adoption, testing, native BEFORE and approval.

Implementation refinement: add C++ extern-C guards to the original C API; refuse an output containing a live state without overwriting it, and keep failures on empty output all-zero. Do not inspect padding or use forged object bytes. Actual callback fixture coverage must remain labelled as caller-ownership evidence. No production winsys linkage or wrapper is included in this phase.

# Unlinked prepared NIR → consuming Gallium callback proposal

SOURCE ONLY. No compiler, source selector, remote command, guest operation or test was run. The proposed C module is separate from winsys and not linked into production. No shader-model, DDI, caps, firmware or backend opcode changes.

## Exact next boundary

Add original `agx_owned_nir_pipe.{h,c}` beside the owned native bridge. It includes actual Gallium/NIR headers in C. The proposed header in this directory shows the minimal result and two functions. Do not make C++ winsys include internal NIR headers or reach through AgxOwnedNir's private layout. A later small C++ wrapper inside the existing opaque implementation can call the C helper while holding its existing native/root object; that wrapper is a separate adoption decision. Existing raw-token compute/graphics entrypoints remain untouched.

The source argument is the actual live native result (NIR/map/workgroup), not an arbitrary shader/DDI pointer. The helper determines stage from actual `nir->info.stage`; accepts only current VS/FS/CS. For CS check known nonzero fixed block dimensions and consistency with copied workgroup metadata before cloning. Use actual `dxbc_nir_compute_info` for block/shared and current actual SRV/UAV dimension gatherers. Source maps are copied by value. No generic admission validator or root validator is added here.

## Proposed function body, in order

1. Require empty output and stable, readable, disjoint source/context/output under caller serialization. Invalid nonempty output is refused without overwriting a live state. On valid empty output, use only a zero-initialized local result until success. Check relevant create and delete callback pair before cloning; reject unsupported stages and missing pairs.
2. Gather dimension, block and shared-memory metadata from the borrowed original NIR before handoff. Verify CS workgroup metadata agrees; neither mutate original NIR nor rerun compute-system-value or fragment-position passes. Metadata failure publishes no State/map/dimensions.
3. `nir_shader_clone(NULL, source->nir)` creates the sole transferred clone. A NULL clone produces CLONE_FAILED; this is not a generic recoverable upstream-OOM claim. The source's GLSL singleton reference and live screen/context remain held during clone and callback. If any pre-call rejection were added after cloning, free that still-local clone exactly once.
4. Build actual zero-initialized `pipe_compute_state` (NIR/prog/static_shared_mem), or `pipe_shader_state` (NIR/ir.nir), and call only the matching compute/VS/FS callback. Ownership transfers at invocation. From then on never dereference or free the clone, including a NULL return. This depends on the qualified native consume-on-call policy, not return-value inference.
5. NULL state produces CREATE_FAILED with still-empty output. Non-NULL state is assigned to the local snapshot and the entire local snapshot is published last. The result has no NIR, root, options, map or workgroup pointer. Delete uses the recorded stage and matching pipe delete callback then zeroes the local state. It requires the same live caller-serialized context; it is not a stale-context validator.

Keep canonical root bytes with the caller's existing CPU-owned prepared/PSO object. This helper does not add a root pointer to the owner state or claim current legacy PSOs already preserve a C15 root key. Before a future production caller releases AgxOwnedNir it must have copied the root compatibility key into its own retained PSO metadata (or retain that prepared object). Copy/allocation must complete before native callback and final PSO publication. Do not introduce a borrowed root pointer or hold registry locks over Gallium compilation. Device/context/root ownership and Windows threading/error policy remain separate.

## Actual driver contract and qualification

Accepted primary source: Gallium p_state.h297–305 requires driver ownership and frontend cloning. The actual 1469 Asahi shader constructor serializes then frees NIR before variant work; compute frees on its late failure and success. Delete tears down variants/blobs/state, not the already-consumed original NIR. Actual shader compilation can allocate executable BOs (agx_state.c1484–1488); thus calling the real native callbacks is NOT the no-BO CPU backend checkpoint.

The early-NULL consumption defect is now independently qualified: actual1487 RED six NIR leaks/exit1, GREEN eight case markers/exit0, empty stderr, post15 input matches. Root review SHA53e8130d295254c7c9410b6fd94ee1d50d1bab57003eb3affaa3adf7f7919c3c. That finite actual-prefix proof covers the first allocation branch and unchanged non-NIR controls; it is not whole-constructor OOM recovery. Future native build must use the CURRENT projector containing that exact repair, not historical1469 unchanged archive.

## Smallest nominal deterministic helper regression

WHAT REAL BUG OR INVARIANT: passing borrowed source NIR into a consuming callback causes source destruction/double-free; publishing map/state before callback success exposes partial state; repeating FS normalization corrupts positions; missing partial graphics rollback leaks returned stage states.

Compile the actual new C helper with actual NIR/Gallium/dimension functions. Supply a real pipe_context whose relevant callbacks are explicitly fixture callbacks; do not call them Asahi driver/compiler success. They consume the actual cloned NIR with `ralloc_free`, attach a fresh ralloc child destructor to observe that clone consumption, record actual stage/state/shared fields, then return an owned fixture state or ordinary NULL. No NIR destructor replacement. An independent child on original NIR proves the source remains alive. Test success and ordinary callback-reported failure (not allocator failure injection), exact by-value map/dimension/block metadata, clone pointer distinctness, original actual NIR semantic execution before/after, original root/map bytes unchanged, matching delete exactly once and idempotent empty-delete. Always-evaluated fixture checks survive NDEBUG.

Use accepted mixed compute actual NIR/map + existing semantic oracle384words; legacy VS/FS originals exercise current stage contracts. For FS snapshot actual position structure before/after to ensure this helper does not lower again. Require callback count0 for invalid stage/metadata/missing callback rejection. Callback failure must leave all output bytes zero and original source reusable for another ordinary call. Do not manufacture clone-allocation failure or invoke an upstream fault sweep.

A small fixture caller composes two actual helper calls: VS success followed by FS ordinary failure; it deletes returned VS exactly once and publishes no aggregate graphics state. This tests that fixture caller's rollback, not production `AgxD3d12OwnerCreateGraphics`, which is untouched. Production rollback proof is a later test against the actual adopted caller, not a claim made by this gate.

Before source implementation/gate, pin current helper sources plus actual fixture inputs and reference headers/libraries. Local host opt-in pass is possible without native runtime library/BO providers because callbacks are fixture callbacks. Native build qualifies real Windows headers/ABI and canonical551fc NIR closure separately. Actual Asahi state creation remains an independent GPU-capable owner checkpoint after a reviewed native context exists; do not replace it with a fake compiler or claim CPU callback test uploaded code.

## Limits and ownership

Caller serialization is required; no context lock or multithread guarantee. Output initially empty, source/options/context live, proper matching create/delete callbacks and qualified consume policy are preconditions. No persistent borrowed storage. No Windows HRESULT translation, Calc/Create capacity, readable-pointer provenance, owner lazy creation or DDI wiring. No samplers/typed/array/graphics-DXIL admission added by helper success. This gate covers current prepared profiles only. Original project source remains MIT. Existing Mesa MIT and accepted accompanying notices apply to real NIR/Gallium dependencies; no external implementation body is copied into this proposal.
