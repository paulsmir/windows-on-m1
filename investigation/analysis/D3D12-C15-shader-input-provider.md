# C15: unlinked CPU input/root provider

Classification: RENDER_ONLY; this module is not registered with the runtime.

Original unlinked implementation, verified through the explicit-span CPU and native object checkpoints below. Actual current owned-core/zero-cache/legacy+DXIL C framer sources are copied byte-identically into current/. Existing baseline oracle sources/real PROGRAM fixtures are unchanged and pinned. No translator libraries, native shader compiler, Windows table, HRESULT mapping or capability change.

## Exact narrow contract

One AgxCpuProvider owns live device/root registries and immutable copies of caller-supplied opaque canonical root bytes. No interpretation, serialization schema, canonical equality or production extraction is added. The caller owns actual DDI identity values and their external lifetime; the provider never dereferences these opaque pDrvPrivate addresses. API membership uses nonreused process-unique uint64 IDs, not caller-supplied object addresses or magic probes. This prevents stale IDs aliasing newly registered objects at reused identity addresses.

All EXTERNAL calls on one provider are caller-serialized and nonreentrant. This is a deliberately falsifiable single-caller CPU contract, not thread safety or a Windows threading implementation. The sole internal allowed reentry is synchronous read-only device identity resolution from the existing zero-cache validator during actual core Calculate/Copy. No registry lock is held (this module has none). Failure sinks are noexcept and must not reenter APIs or mutate buffers. Their caller-owned context remains valid while registered or any lease retains the device, including after retirement. No cross-thread/atomic object publication claim follows from the ID counter's atomic uniqueness.

Root registry holds one reference; root owns device reference. Input lease holds root and device references independently. Root retirement removes future lookup visibility and drops registry reference; existing leases retain immutable bytes and can still copy while device remains live. Device retirement removes device and its roots from live lookup, but lease references preserve memory until release. After device retirement Calculate/Copy refuses before entering core; LeaseRootBytes remains a borrowed read-only view until release. Provider destruction returns Busy if any input lease exists. Zero-cache child libraries must be destroyed before retiring their device, following its existing identity-lifetime precondition; the provider does not claim to discover those children.

AcquireInput reads only caller-guaranteed valid args and optional Standard descriptor; it snapshots those records/counts. PROGRAM and signature arrays stay caller-owned readable+unchanged until ReleaseInput. Provider supplies owned root span. The actual core receives all explicit spans and retains no borrowed pointer after copy. No Windows pointer probing, readable-span inference, SEH, canonical-root decoder or arbitrary dangling pointer test is introduced.

Core statuses are preserved exactly with core_called flag. Provider membership/lifecycle/OOM/exhaustion errors are distinct internal statuses. Failure sink fires once for failed AcquireInput with known live device or failed Calculate/Copy with known lease; other APIs return status only, and unknown handles/null outputs cannot invent a callback target. No status is converted to HRESULT, callback registration or SIZE_T sentinel. These are CPU callbacks, not pfnSetErrorCb.

Acquire's lease output is zeroed before failure; owned-copy output is null before failure and assigned only after actual core Create succeeds. Core/program/destination nonaliasing and writable output preconditions remain exactly those of the existing core. No source/destination invalid memory or fault injection is used to test them.

## Meaningful proposed oracle

The existing unmodified DXIL114467 and legacy/DXIL424029 oracles remain commands3/4 and5/6 with both original framers. New commands7/8 link/execute one original fixture against the actual module sources. It uses actual1397DXIL PROGRAM and actualcs50 legacy PROGRAM; no synthetic compiler output.

New checks cover copied opaque root independence, immutable record snapshots, actual core size/copy and exact StorageInvalid taxonomy, per-device sink identity/publication-null observation, source overwrite only AFTER release, cross-device root rejection, root retirement with existing read lease, stale ID rejection after same identity is re-registered, retired-device refusal before core, Busy destroy and eventual reference cleanup. All input buffers are actual owned valid allocations; only API membership states change. ASan/UBSan flags are inherited from the existing host workflow, not a fault sweep. Malloc/new failure branches are source-handled but not tested/claimed recoverable by this checkpoint; no internal translator OOM work is included.

## Proposed source/build boundary

commands-proposed.json lists exact8argv, timeout60s each and first unexpected result STOP. Commands1/2 compile actual current framers;3/4 unchanged DXIL oracle;5/6 unchanged continuity oracle;7/8 new provider fixture. Actual WDK declarations are emitted by the existing original data renderer with original host aliases, so this is NOT native Windows ABI proof. Private actual modules remain hash-identical to shared current source. Root must review/freeze/preregister BEFORE compiler invocation; no command has run during preparation.

Production readable PROGRAM/signature spans, root extraction/canonicalization, legal VOID-DDI error routing, runtime threading, shader/PSO integration, registered R1 callbacks and SM6 behavior remain open. This original CPU proposal establishes none of those Windows decisions and does not alter old StoreShader or old public APIs. No source from the external translator is copied or linked. License remains original project MIT and existing reviewed WDK-declaration/host-shim test arrangement.

EXP1440 actual host checkpoint: eight commands rc0 with empty stderr; original114467/424029 controls and new79 checks pass. Source110 and runner pins remain unchanged. Receipt bdd960e20d0193eeaed9a40d0befe939c28ae09c7fa1db22bf8770e67c466480. Current-source durable and native WDK object gates follow; no implementation has been linked into the driver project.

WINDOWS CONTRACT: pointer-only runtime shader arguments, readable spans, legal VOID-DDI errors and runtime threading remain open as recorded in D3D12-C15-readable-program-provider.md.
AGX/ASAHI CONTRACT: no shader compiler, translator, native owner, DMA, power or recovery operation occurs here.
TRANSLATION: original explicit-span CPU provider supplies owned immutable opaque root bytes and leased stable input records to the existing owned core, preserving its exact status. Runtime keeps ownership of its eventual arguments and private allocation; production adaptation is deferred.
WHAT IS STILL UNKNOWN: production root canonical encoding, accepted runtime read-range contract, legal runtime error policy and threading/PSO integration. No hardware experiment is justified by this CPU-only boundary.

CURRENT ROOT durable result: one opt-in test PASS2.010s, four derived commands rc0, all stderr empty,79 checks,62 post-input pins unchanged. Default discovery skips before helper/tool access.

Actual WDK26100/MSVC14.44 native gate: four W4WX/NOMINMAX ARM64 objects passed with empty stderr and actual dependency closure;4906 pins unchanged (3096 compiler/SDK,1801 capture runtime,9 exact sources). Objects are AA64; no link or execution. Controller manifest a4dab6d275105d7a4923dc68c098c9d79819951b0af8af8ceb90b3023b40f239.

Full suite1599/216.630s preserves exact119baseline (68fail51errors12skip), no added/removed failures; input pins match. SHA-2563aa2d28e373cd87c7949e9f3d66e1c1753d08353483446253d74e8014a4014ae. This run preceded the independent host recovery admission fix, which has its own32 passing tests. Existing core/framer/zero-cache sources and their114467/424029 controls remain unchanged.
