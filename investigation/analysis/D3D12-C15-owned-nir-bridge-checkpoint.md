# C15: owned PROGRAM to owned NIR checkpoint

Status: original private prototype reviewed; not adopted into the production
owner and not linked into a driver package. Native Windows build is the next
gate. The current EXP1471 package excludes this work and all SM6 publication.

WINDOWS CONTRACT: this CPU prototype requires a valid live provider-created
AgxShaderOwned object, explicitly readable stable capacity, live client/context,
and disjoint initially empty output. These are caller preconditions, not proof
of DDI pointer readability or runtime private-storage capacity. Production
Calc/Create correlation, device provenance, legal error callbacks, teardown and
complete stage/resource admission remain open.

AGX/ASAHI CONTRACT: primary pinned shader_info.h stores workgroup_size as three
uint16_t fields. Existing checked converter, spirv_to_nir, DXBC translator and
project binding-map pass produce owned NIR with one GLSL-type lifetime reference.
No device, BO, upload, interrupt, DMA, power or recovery contract changes here.

TRANSLATION: copy canonical root bytes; dispatch the actual owned program kind;
convert with the actual frontend and immutable logical binding map; explicitly
widen each workgroup component; publish only after all stages succeed; destroy
NIR and owned snapshots together. Legacy CS/VS/PS stay on the existing translator.

WHAT IS STILL UNKNOWN: actual Windows ARM64 compilation/link/runtime behavior;
production owner admission/lifetime; actual backend and GPU execution. Separate
EXP1474 backend fixture compiled but stopped at missing owner symbols at link;
that STOP does not disprove this frontend-only bridge or prove GPU semantics.

Root independently read all seven original code/helper files and the complete
runner, verified READY source/receipt hashes and all105 run8 artifacts. Actual
run7 RED and unchanged run8 oracle outputs were inspected. No broad portable
suite repeated. Bundle READY SHA17781974e55a8cdaf20a49d063e2c549702fb4eb43322367f0101f68a2286c23;
run8 verdict7890219134f1ef3abc5d88ab885c081c6040d4e86c5a09d2b30c6218410076cc.
The source and preserved raw evidence live outside tracked source under
.local/c15-owned-nir-bridge-prep. Original code MIT; existing reviewed external
libraries/headers are reused without copying their implementation bodies here.

Smallest next checkpoint: bounded native build-only using actual WDK/Windows
headers and fresh1466/1469 ABI-consistent libraries plus the pinned checked
converter closure. No ARM64 PE execution on builder. Future CPU guest execution
requires its own BEFORE and can begin only after EXP1471 exact visible rollback
reaches RECOVERY_DONE. CPU-only tests need no GPU package or reboot.

The detailed frozen prototype report follows, with its observed limits retained.

# Original owned PROGRAM → owned NIR/map CPU bridge

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

Misdispatching an owned DXIL PROGRAM as legacy token bytecode; retaining released PROGRAM/root/storage pointers in the returned NIR/map/root; changing an already live immutable map during a later failed conversion; class/space/register collisions; publishing partial output after extent/entry/capacity refusal; and miscopying compiler metadata widths. The first complete run7 reproduced the last bug: nir shader_info stores workgroup_size as uint16_t[3], while the project output uses uint32_t[3]. memcpy12B produced wrong dimensions. Preserved RED source matches run7 BEFORE hash in revisions/workgroup-width-RED. Elementwise widening fixed it without changing the64x1x1 oracle; fresh run8 passed.

## Implementation and ownership

owned_nir_bridge.{h,cpp} is original unlinked CPU code accepting a valid live AgxShaderOwned object plus explicit accessible capacity and stable/disjoint request/output. It copies canonical root bytes and publishes one opaque result only after frontend success. Result getters expose owned NIR, a const-only map, copied root span and workgroup dimensions. Destroy releases NIR/GLSL-type reference and project-owned storage, then clears the caller handle. It does not validate arbitrary DDI handles, infer accessible memory from a self length, or store new device/generation provenance in the88-byte existing owned header.

bridge_native.{h,c} invokes CURRENT actual legacy framer/DXBC translator (and fresh-FS position normalization), or CURRENT checked owned DXIL converter, actual SPIR-V frontend and actual project agx_dxil_nir_lower_map. Frontend passes match tests/data/dxil_nir_map_fixture.c. Legacy token stages are mapped explicitly to Mesa stages (PS0→fragment, VS1→vertex, CS5→compute), avoiding an enum-value cast. No EnsureOwner, pipe create_compute_state/create_fs_state, BO, upload, backend compiler, Windows callback, registration or caps call exists. Accepted static host libraries are reused; Asahi libraries occur in the inherited accepted frontend link closure but no backend entry is invoked or rebuilt.

The fixture constructs every source object through actual CURRENT provider BeginOperation/CopyPinned/EndOperation/ReleaseInput and actual owned core/zero-cache resolver. It keeps the CPU client/device/context alive. After legitimate borrow release it overwrites input PROGRAM, args and caller root bytes; after bridge success it retires the root and overwrites the copied source-object buffer. Returned actual NIR/map/root remain usable and independent. This is CPU composition, not a production provider/Windows lifetime implementation.

## Finite actual results

run8: fourteen source compiles + link + actual fixture, all16 rc0;7110 direct/header/tool/archive/generated/input pins matched postflight; every emitted .d dependency was in that canonical pin set. Runtime stderr empty, no ASan/UBSan diagnostic. External static libraries are not all instrumented, so no whole-dependency sanitizer claim. Link stderr is exactly51 bytes, SHA ea4090f75f622fb03e64dffcdf8ee0ea79fdd5f11d3163ab1ca96a582531a60b: ld warns about duplicate-lc++ because the accepted C frontend link recipe explicitly lists it and this composition links with clang++. No flags or expected oracle were changed to suppress it.

Actual fixture:79 checks,5 accepted bridge results,6 atomic refusals. Inputs are real mixed DXIL PROGRAM plus legacy CS5.0/VS4.0/PS4.0 PROGRAMs from existing original renderers. Four short-owned-capacity refusals, wrong DXIL entry and insufficient UAV slots leave output NULL; a valid conversion succeeds again on the same thread and does not alter a prior live map. Mixed map is exactly CBV b0space3→0, SRVs t0space2/7→0/1, UAVs u0space1/9→0/1, and workgroup64x1x1.

The unchanged CURRENT restricted actual-NIR interpreter reads all seven independently pinned1397 vectors:384 exact output words,192 lanes,173 active/19 tails,346 stores; SRV swap, CBV seed loss and wrong-slot controls each produce346 mismatches; unsupported-opcode and bounds controls refuse. It implements actual returned NIR, not HLSL expected arithmetic. Actual output arrays are persisted by the existing oracle in run8.

## Preserved failures and limits

run1/run2 stopped before compilation during renderer-directory/gold-vector path preparation; their directories are preserved, but those earliest Python traceback streams were tool output, not saved as raw files. run3 preserves private typo gl_shader_stage versus primary mesa_shader_stage; run4 preserves strict missing CURRENT project-header pins; run5 preserves missing accepted external C API include argument; run6 preserves /Users↔/Volumes alias mismatch in dependency keys. The final runner canonicalizes existing pin names, checks conflicts and pins the finite CURRENT project-header closure; it does not add a broad external allowlist. run7 preserves all14 compile/link success and the functional workgroup RED. run8 is the complete GREEN.

No native Windows bridge build or runtime admission is claimed. Existing provider/owned core sources and old fixtures are unchanged; this run does not repeat or replace their historical79/121/114467/424029 results. The new79 marker is this bridge's own independent count, not the historical provider79. Host shim declarations are original aliases around actual WDK records, not Windows ABI proof. Current checked facade/composite dependency and license policy stay pinned; no foreign implementation body is imported into tracked source and no external archive is rebuilt.

Readable/stable pointer-only DDI inputs, Calc/Create private capacity, production device/root/shader provenance and teardown/quiescence, legal runtime error callbacks, required graphics stages/resources and complete SM6 admission remain OPEN. The bridge's explicit valid-live-CPU-object precondition does not establish foreign/retired-device rejection. Output bytes/NIR independence does not keep an eventual runtime-owner token valid after context death. DXIL remains the current compute6.0 singleton CBV/rawSRV/rawUAV subset; no operation/model/caps generalization. No general recoverable internal-OOM guarantee.

sm51's separate actual NIR→Asahi backend fixture may take the returned NIR and const map, clone it, then run its owning compiler boundary. That gate and future owner/GPU binding are separate; no duplicated backend code is here. Root owns native/preregistered execution, adoption and ledgers. This bundle is frozen after run8; no more tests run for this task.
