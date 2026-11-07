# C15: adopt prepared NIR before native owner handoff

Root reviewed the complete bounded source plan below and the actual projected
shader constructors, current Windows owner, WDK Dispatch signature and Gallium
ownership comment. This is the accepted next implementation order; it changes no
DDI registration, ShaderModels response or production build linkage.

The plan's pending backend checkpoint has since completed as **EXP1482 PASS**:
actual J313 ARM64 process8544 exited0 with empty stderr; actual Mesa normalization
removed one base-global intrinsic; actual Asahi emitted740 bytes/70instructions,
workgroup64,1,1, push16=36/reserved16=16/two actual push ranges. The original384word
oracle,346-mismatch negative controls and seven structural refusals remained;
420checks executed. No BO upload or GPU execution occurred. Root review:
.local/c15-backend-cpu1482-result/root-review1482.json SHA
`e19ac5be9cd5b4accc81ee743dce2623ad7e07711a6b863ba6b2e024d7221717`.
Original EXP1477 CPU failure remains recorded and preserved.

The module and native projection changes are being prepared separately as
original private proposals. Root alone will adopt tracked changes, run current
source tests, commit implementations and gate native/package/hardware work.
A no-BO backend result proves compilation; actual Gallium state creation may
upload executable BOs and needs its own controlled checkpoint. The initial
allocation NULL ownership discrepancy must be resolved in the native owner
before any caller assumes uniform consume-on-call cleanup.

Source-plan SHA `51d2ce9206ce9c56e4d361c634daa6edfd05b33302e669f7fcf05be111d24a3c`;
source-pins SHA `8f75fe70b31cd764f4f470167a4602dd3c8c5adb43e687886e3948f8a168320a`.
Root verified every pinned current/projected/primary file before accepting this
plan. No external implementation body is copied here.

# Owned PROGRAM → NIR adoption and actual Gallium handoff: bounded source decision

Scope: SOURCE-ONLY plan; no implementation, compiler, remote, guest, Git or shared writes. Actual1480 CPU result (79newbridge checks/384words,3negative controls346each) is accepted by root review5bfbb002248068126a63d604b6c3b822000aec863058581e7ad710a7528a5544. It proves the explicit valid-live-CPU-object/capacity composition, not Windows DDIs or runtime lifetime. Original1480source/receipts remain frozen. Source-pins.json distinguishes CURRENT project modules from accepted1469 native projection and corresponding Mesa/WDK primary files; current DDI is source evidence, not a hardware-qualified snapshot.

## Decision / smallest reviewable implementation

Adopt the original bridge as a CURRENT project module + opt-in current-source CPU fixture, still unlinked/default-OFF for production. In that module add the source-proven non-base compute normalization at its owning translation boundary and test the resulting actual NIR. Keep actual Gallium owner consumption as a separate next patch/gate: it needs an explicit clone/consume contract and the early-NULL ownership gap below closed. Do NOT wire CORE0003 CreateShader or publish SM6 as part of either patch. Existing1480CPU behavior is historical proof; later normalized NIR is a new independently reviewed version, not a reinterpretation of1480.

Suggested original files: mesa/dxil/agx_owned_program_nir.{h,cpp} for typed owned-object/result lifetime and a C translation unit for NIR/frontend/map calls. Retain an opaque owner-facing result, const map/root getters, valid-owned-object+explicit capacity precondition and project status enum; no host WDK shim in production. Move source exactly first, then add the one normalization as an explicit delta. A CURRENT-only renderer/test must compile these actual modules, not private-prep fallback. Portable/native nominal dependency closure stays the accepted checked19facade/composite9 + freshMesa core/runtime; default tests skip before external tool/dependency access. No production build hook until an explicit separate choice: adding the CPU test/module is not native DDI wiring or a license to link translator libraries into the active UMD.

## WINDOWS CONTRACT / current Windows-facing boundary

Pinned WDK26100 d3d12umddi.h3270-3296: actual0010 shader args contain hRootSignature, pointer-only pShaderCode, IO signature union, Flags and LibraryReference; Calc returns SIZE_T, Create is VOID and receives a runtime private handle. There is no byte-capacity field. PFNDISPATCH1776-1777 takes only three UINT dimensions, with no dispatch base. CORE0003 and0010 are different typed callback contracts; CPU use of0010 records does not change negotiated registration. All pointer-readability, private-capacity, failure signaling and runtime retirement guarantees remain OPEN as previously recorded; no pointer probing or global lastCalc cache is proposed.

CURRENT ddi CreateShader/StoreShader430-456 still copies legacy two-word token lengths into AgxD3d12Shader. PSO552-560 independently copies legacy token bytes, then CompileCompute2900-2910 holds the PSO compile_lock and calls EnsureOwner before the native owner path. EnsureOwner830-846 creates the actual Win32/Asahi owner lazily. It is NOT a pure CPU operation and remains outside shader Calc/Create and new bridge preparation. Current per-PSO serialization does not prove global EnsureOwner or shared-pipe-context concurrency. Pipeline lifetime keeps the native state until DestroyPipelineState2912 onward, which invokes native destroy helpers for compiled success.

A future registered path would need explicit typed owned-program views copied/retained into PSO state; the current UINT token copies cannot be repointed at the tagged88-byte owned header. Windows may release input shader/root storage independently of the PSO, so new PSO-owned PROGRAM/map/root metadata cannot borrow runtime private pointers. That is production integration debt, not fixed by the CPU module. No such wrapper/PSO change is included here.

## AGX/ASAHI CONTRACT / actual NIR ownership

Gallium p_state.h297-305 explicitly says the driver takes nir_shader ownership and frontends retaining IR must clone. p_context.h386-393 exposes ordinary create_fs_state/create_vs_state callbacks. Actual accepted1469 projected agx_state.c1982 onward uses cso->ir.nir for NIR graphics;2092 initializes/serializes state then2097 frees NIR before precompiling VS/FS variants. Actual create_compute_state2161-2189 similarly uses cso->prog, initializes/serializes, compiles the variant, then frees NIR both on its late compile failure2182 and success2189. Compilation is not an inert CPU-only callback: Mesa agx_state.c1484-1488 uploads a compiled binary into an executable BO. Calling create_compute_state in a fixture is therefore a different boundary from the accepted noBO backend compiler fixture.

Delete callbacks agx_state.c2730-2734 call agx_delete_uncompiled_shader2690-2725: variants and linked shader BOs are released, both serialized NIR blobs finished, then the state freed. It does not re-free the original NIR pointer saved incidentally in so->base. The owner wrappers currently clear successful state records after delete. GLSL types have independent screen lifetime: actual agx_pipe.c2679 increments the global singleton reference and screen destruction2549 decrements it. The private bridge also owns its own reference; keep that reference alive throughout cloning and native state creation. A successful live screen/state owns the subsequent native lifetime, but that is not permission to outlive the owner's device/context.

**Concrete ownership gap requiring an owning-native decision:** both actual graphics1987-1990 and compute2167-2170 return NULL if initial so allocation fails BEFORE taking/freeing the provided NIR. Later NULL paths consume/free it. Caller cannot safely decide whether to free the input clone from the return value alone. Existing owner legacy paths already assume Gallium ownership and don't free on NULL. Do not conceal this with free-on-NULL (double-free later failures) or claim failure cleanup is universally proved. Minimal owning-layer repair is for the Windows native projection to honor consume-on-call for NIR inputs on those initial NULL paths (graphics conditional on PIPE_SHADER_IR_NIR, compute only its admitted NIR contract), or an explicitly maintained take result inside the actual native constructor. It is not an owner-side guessed allocation test or a replacement allocator. This is a finite deterministic ownership obligation, not a general recoverable internal-OOM guarantee.

## TRANSLATION / correct normalization location

Actual current bridge ends the DXIL frontend path with nir_lower_system_values then vars-to-SSA/optimization/gather_info. Mesa state_tracker/st_glsl_to_nir.cpp557-560 explicitly follows system-value lowering with nir_lower_compute_system_values(&zero_options); st_program.c383-391 explicitly sets has_base_global_invocation_id=false and has_base_workgroup_id=false. nir_lower_system_values.c728-735 replaces the two base loads with zero under those options. Asahi agx_compile.h387 explicitly has has_cs_global_id=true, so this target retains its supported global invocation load; do not infer a generic workgroup-ID rewrite from other drivers' options.

CURRENT owner AgxD3d12OwnerDispatch1496-1505 zero-initializes pipe_grid_info and sets block/grid dimensions; it does not set grid_base. Direct and existing indirect Dispatch have the same non-base domain. Therefore place the zero-base normalization in the C15 owner-facing translation module AFTER nir_lower_system_values, before final optimization/gather_info and before agx_preprocess/native compiler. This is target/frontend normalization, not a new Asahi backend opcode handler and not unconditional generic DXBC-parser lowering. The exact same normalization is shared by its CPU/backend fixtures and future handoff. Existing legacy dxbc_nir_finalize2888-2889 already lowers compute system values with NULL; leave that validated public translator/D3D11 route unchanged initially. The new C15 DXIL compute result acquires its non-base contract once. Reject any future nonzero-base dispatch profile until separately designed; don't silently zero a promised base.

Fresh FS position normalization is already performed by the bridge's legacy path and current create_shader1124. A new prepared-NIR owner helper must NOT call that legacy translation/normalization again. Existing raw-token legacy entrypoints remain unchanged; a separate prepared-NIR entrypoint consumes the prepared contract. This avoids double-lowering GL/TGSI and keeps existing legacy receipts meaningful.

## Owner handoff API and publication

Propose an owner helper taking an already-live AGX_D3D12_OWNER/context plus const prepared result, not arbitrary DDI shader handles and not a lazy EnsureOwner call. Validate profile/stage and derive SRV/UAV dimension metadata and compute block/shared fields from actual prepared NIR BEFORE transfer. Keep the immutable binding map and canonical root metadata associated with that same shader. Clone original NIR with nir_shader_clone while bridge/screen references are alive; on pre-call metadata/clone failure clear all new owner output and publish no state. Call the proper create_compute_state or create_vs/fs_state with the clone under the reviewed consume-on-call contract. The bridge retains its original NIR and const map; native modifications cannot mutate the cache/semantic oracle's source. On success copy map/dimensions/block and publish owner state together. On native failure do not publish partial Bindings/dimensions/State; delete only actual returned states that require rollback. Never pass the borrowed AgxOwnedNirShader pointer directly into a consuming Gallium callback and then destroy its bridge (success double-free).

Typed C opaque-NIR helpers belong beside the translation/native C modules; production C++ owner should not include internal nir.h or invent ABI prototypes (same reason as the accepted fragment-position helper). Root/context provenance, actual provider scope and thread-safe owner admission must be supplied by the future registered caller; an opaque prepared pointer does not provide them magically.

## Real regression gates / no new hardware proposal

1. CURRENT-source bridge adoption: unchanged legacy CS/VS/PS route, explicit extent/entry/UAV-limit refusals, output atomicity, const map, source-release/root-copy independence and legitimate provider/context lifetime. Preserve old1480result; run the normalized version as a new gate. No host-only depth counter in native tests, no NDEBUG-erased project calls.
2. Actual normalization invariant: real accepted mixed PROGRAM→frontend/map; before-state records base-global load from the actual source case; after-state no load_base_global_invocation_id/load_base_workgroup_id, unchanged semantic key map and64x1x1. Use the existing384word actual-NIR oracle and all346-negative controls without changing expectations. Current target has_cs_global_id=true means no reason to invent a new IR interpreter; if an actual new opcode appears, stop at that specific supported-semantics question rather than loosening the oracle. Optional second-pass semantic stability is a finite idempotence check, not a requirement to assert identical SSA textual hashes.
3. Handoff regression against the actual new owner helper: actual NIR clone and recorded Gallium callback ownership demonstrate the original NIR/map/root survives a consuming callback, callback failure leaves state metadata empty and partial VS/FS success is deleted exactly once. Such a callback fixture proves the UMD caller contract only, not native Asahi BO behavior. The owning native early-NULL cleanup must separately be tested against the actual constructor/projection branch with tracked allocation/free ownership; don't substitute a fake shader compiler/driver-success body or call it full OOM proof. No such test/execution is performed by this source task.
4. Build gates: actual portable debug source fixture + real WDK native compile/link/import qualification, complete dependency pins and canonical551fc across all actual NIR consumers; current unlinked modules only. Independent1481 backend gate must prove normalized actual Asahi compilation before a hardware-capable owner path is considered. Future actual Gallium state creation additionally crosses code-upload/BO boundary and needs its own preregistered context/recovery checkpoint after all source contracts close. Do not call the noBO compiler fixture a create_compute_state proof.

## WHAT IS STILL UNKNOWN / stop

Early-NULL NIR consumption must be made coherent before asserting complete owner handoff cleanup. Windows pointer/capacity, legal callback failure policy, owner/device/root provenance and shared context synchronization remain OPEN. Full required-stage/resource semantics and typed R1 registration/caps remain distinct later gates. Accepted CPU1480 and a future backend PASS do not authorize CORE/ShaderModels/default changes. One bounded source pass ends here: smallest independent code delta is current-module adoption + non-base normalization; actual owner handoff follows only with the explicit consumption/lifetime contract, not a DDI shortcut. No architecture/hardware flag probe is proposed.
