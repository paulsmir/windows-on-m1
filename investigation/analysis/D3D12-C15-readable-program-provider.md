# C15 next CPU bridge: explicit readable PROGRAM lease + owned root compatibility provider

SOURCE_ONLY, one bounded pass. No implementation, compile, remote, guest, capability/table/link change, fault test or hardware proposal. Classification RENDER_ONLY. Existing e28a1096 tagged continuity (host114467+424029/native3 compile-only PASS) remains unlinked CPU evidence, not runtime readiness. This task does not investigate any graphics compiler failure.

## Decision

Prepare one original **unlinked CPU provider/adapter checkpoint**, preserving the present core and both framers. The adapter acquires a short-lived device/root/input lease, supplies actual explicit spans to AgxShaderOwnedCalculate/Create, copies data into owned private storage, and publishes only on success. Typed CPU status and failure sink are testable now. Do not register new VOID DDIs, link the DXIL converter, negotiate0011, advertise SM6 or feed GPU state until the separate Windows-facing readability/error/lifetime conditions below are closed.

The strongest remaining production boundary is precise: neither0003 nor0010 shader callback contains a separate accessible allocation byte count; public D3D12 Create pages specify no per-error HRESULT policy for the VOID callback. An internal explicit-span API can be completed and tested without asserting an invented Windows buffer-probing or error-code contract.

## Primary sources inspected

Project current source: investigation/analysis/D3D12-C15-dxil-plan.md; D3D12-C15-shader-owned-cpu-core.md; D3D12-C15-legacy-program-frame.md; bounded-DXIL-input, ddi-model-contract, sm60-minimum-ddi-evidence and owned-pipeline-library-contract notes. Production UMD agx_d3d12_ddi.cpp device/callback setup, root creation/storage, current shader Calc/Store/Create/Destroy and device error helper. CPU shader_owned_core C++/H, zero_cache_library C++/H, legacy_program_frame C/H and DXIL program C/H. Exact files, line anchors and SHA256 are in source-manifest.json.

Pinned WDK26100 d3d12umddi.h:

* :2209–2218: PFND3D12DDI_CALC_PRIVATE_SHADER_SIZE returns SIZE_T;0003 CreateShader/CreateComputeShader return VOID. Code pointer has `_In_reads_(pShaderCode[1])` annotation. Root is a handle; signatures are caller records, not driver-owned bytes.
* :3269–3280: actual0010 args contain hRootSignature, pShaderCode, IOSignatures union, Flags, LibraryReference. There is **no explicit byte length**.
* :3290–3295:0010 Calc receives device,args;0010 Create receives device,args,destination shader handle and returns VOID. Do not cast0003 function pointers/argument layouts to0010.
* :2089–2125: signature counts and deprecated record pointers; the WDK explains signatures may be supersets of actual shader use. Core correctly canonicalizes selected fields, not padding. The selected0010/0011 domain uses deprecated signature entries, not later0012 union interpretation.
* :2602: PFND3D12DDI_SETERROR_CB is `(D3D10DDI_HRTDEVICE,HRESULT)->VOID`; :2626 installs it in core callbacks. This proves ABI, not the legal callback-specific HRESULT set.

Official Microsoft pages inspected this pass:

* [CalcPrivateShaderSize](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_calc_private_shader_size) describes driver-private shader memory sizing and the code/root/signature arguments. It adds no independent accessible-span parameter.
* [CreateShader0003](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_create_shader_0003) and [CreateShader0010](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_create_shader_0010) confirm exact signatures and no return value. These pages do not enumerate allowed reported HRESULTs.
* [Core-layer callbacks0003](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddi_corelayer_devicecallbacks_0003) identifies the runtime error callback field; it does not resolve the callback-specific error taxonomy.

The direct official Calc0010/SetError pages were unavailable in this browser pass. No D3D10/D3D11 allowed-error list is imported into D3D12 merely because the handle typedef is reused. No additional equivalent search pass is proposed.

## What exists and what the next bridge must supply

The CPU core takes `AgxShaderTrustedInput {program,input_signature,output_signature,root}`. It checks program.data equals args.pShaderCode, spans cover signature counts, framed PROGRAM extent, stage/model/flags/library reference, checked layout arithmetic, destination capacity/alignment/nonaliasing. It copies program/signatures/canonical-root bytes and publishes magic/output last. It retains no input pointer. This already catches framing/storage defects; it does not make the spans trusted.

The legacy framer reads exactly the8-byte header, bounds words against supplied bytes before multiplying, excludes accessible tail and interprets no instructions. DXIL PROGRAM parsing similarly requires caller_bytes. Neither infers a safe DDI allocation length. A valid header is framing, not semantic compiler approval.

The zero-cache module can reject nonlive or foreign libraries by pointer membership before dereference, but its ResolveDevice/ResolvePsoOwner hooks currently promise only identity stability, not retained read leases. ValidateLibraryReference must execute during a real device lease; it does not confer root ownership. A no-library record is an actual readable record with NULL hLibrary; a NULL record pointer is invalid.

Current production root private object is counts/flags/parameter array/ranges/static samplers; it contains no live-device owner/registry/canonical byte representation and DestroyRootSignature is empty. Raw memcpy of that private struct is not a canonical compatibility provider. Current shader private object is stage,dwords,tokens; its Calc reads code[1], Store copies that declared count and failure uses process-global callbacks/runtime_device. Existing per-device callback/runtime fields and DeviceError helper are a usable route for a later per-device adapter; no new process-global callback slot is needed. The new88-byte tagged owned object is not layout-compatible with current AgxD3d12Shader; no reinterpret cast to tokens is allowed.

## Minimal provider/adapter shape (proposal only)

1. **AcquireDevice** by registered live handle address; obtain stable device identity and per-device error target/lease. Unknown handles are compared before dereferencing arbitrary private memory. No native owner creation, GPU state, translator TLS or allocation is required merely to validate device membership.
2. **AcquireRootCompatibility**(device,hRootSignature) checks same-device live root membership, then returns a retained immutable canonical blob and generation/read lease. Root registry lock is released before core/library calls or error reporting. The lease protects bytes through the entire core operation; root destroy removes visibility and retires bytes only after existing legal leases finish. This supplements, not replaces, the runtime's valid object-lifetime/synchronization precondition. It does not promise to legalize arbitrary application use-after-release.
3. **AcquireInputSnapshot/lease** receives explicit PROGRAM/header/signature spans from a trusted caller/provider. The adapter must know the args record and signature descriptor are readable too; the core reads IOSignatures.Standard and its counts before validating the pointed arrays. Snapshot standard counts/pointers/flags/root/reference consistently while the provider guarantees no concurrent source mutation. Retain no borrowed pointer after release. Require nonoverlap with destination and all output records.
4. Call existing core Calculate/Create outside all registry/provider locks. Use the existing same-device library validator while the device lease is held. Release leases after copies and before issuing any external error callback. Publish live shader registry entry/output only after owned contents are complete. Failed creation leaves an EMPTY, not reusable-as-valid object; destroy-after-failed-create is benign and cannot follow unregistered handles.

Canonical root bytes must be built once from the actual validated creation description while it is readable, with an original field-wise encoder: schema tag, root flags, ordered parameters, parameter type and visibility, constants register/space/count, root descriptors register/space, table ranges type/count/base register/space/resolved append offsets, and every static sampler field including register/space/visibility and exact relevant scalar bit patterns. Preserve root parameter/range order; do not hash raw pointers, padding, private offsets, process cookies or GPUVA. A conservative equality of normalized ordered descriptions is acceptable; do not claim maximal semantic equivalence. Owner identity is checked separately from compatibility bytes, so equal descriptions on different devices still cannot supply foreign handles. Exact bounded root-description validation and checked encoding arithmetic are part of this provider, not granted by current unchecked root-count arithmetic.

First checkpoint need not implement the entire PSO/library component framework: use the actual current root normalization fields in an original standalone CPU root registry/provider, with typed readable records and explicit spans as public preconditions. Do not pretend test-owned memory constitutes a production Windows readability provider.

## PROGRAM span boundary: two distinct contracts

**Explicit-span CPU API:** caller provides `(pointer,actual accessible bytes)`. Preserve this as the only malformed-buffer test surface. Exact self length must fit accessible bytes; both2-word legacy and6-word DXIL headers must be readable before fields are used. Truncation must refuse without body reads. No VirtualQuery/IsBadReadPtr, SEH, longjmp or inferred host-allocation bound is proposed.

**Future runtime DDI adapter:** pinned SAL describes pShaderCode[1] DWORDs readable as a runtime-supplied precondition, not an independent physical allocation query. Subject to a reviewed runtime guarantee of at least the header/full declared range, adapter may perform checked DWORD-count multiplication and construct a logical PROGRAM span. That must be explicitly labelled a trusted DDI input contract, not memory probing or validation of hostile pointers.0010's `_In_ args` does not add a byte-count member. Current minimum-runtime evidence establishes PROGRAM versus application DXBC-container framing, not a generic safe-memory-provider API. Until that trust boundary is accepted, only the explicit-span CPU caller is supported.

Calc/Create matching cannot use a global lastCalc or pointer-address cache. Calc has no destination handle; callers may submit unrelated shaders concurrently and reuse addresses. Reacquire/recalculate from stable leased inputs for each call. Future runtime Create storage is guaranteed by the matched Calc/private-allocation contract; its actual byte capacity is not passed to the callback. CPU checkpoint retains an explicit destination capacity and canary tests; do not claim that check validates arbitrary runtime Create memory. A fixed header plus separately owned payload is a possible later alternative, but introduces allocation/Destroy/error obligations and is not needed for this minimal checkpoint.

The generic0010 Calc stage must be derived from the trusted version header; stage-specific Create wrappers know expected VS/PS/CS and cross-check it. Do not invent stage shader flags. Only current core stages0,1,5 are admitted in the internal profile; deferredGS/HS/DS remain Unsupported and do not become advertised support.

## VOID-DDI error policy

The next CPU adapter should return `AgxShaderStorageStatus` plus an explicit failure category, rather than hide uncertainty behind a guessed HRESULT. Its test-only/project-owned failure sink records device identity/status/phase and verifies callback timing. Keep `Invalid`, `Unsupported`, `LibraryRejected`, `Overflow`, `StorageInvalid` distinguishable; none is honestly an allocation failure in this allocation-free core. Size callbacks return sizes, never HRESULT bit patterns.

A future typed DDI policy adapter must choose exact legal SetError HRESULTs from a primary D3D12 contract or exact saved-runtime evidence for this selected ABI. In particular: do not blanket-map unsupported/malformed input to E_OUTOFMEMORY, do not treat existing process-global E_INVALIDARG StoreShader as a proved normative policy, and do not manufacture a device removal code. No callback while any registry/provider lock is held. Use the live device's runtime handle+callback, not whichever device was created last; callback reentry must observe EMPTY/no published object. Calc failure should be0 only under an explicitly accepted private-size failure contract; its generic official page does not specify that sentinel/error propagation. Production registration remains blocked until this is settled. A CPU failure sink proves routing/transactionality, not legal runtime HRESULT acceptance.

## Smallest meaningful CPU checkpoint to prepare next

One unlinked actual-module harness: existing zero-cache + both framers + current owned core + original root/device provider/lease adapter, no translator or native Asahi owner. Derive inputs from the existing9 reallegacy CS/VS/PS PROGRAM fixtures and actualDXIL framing control. Keep the114467 and424029 continuity oracles unchanged as regression controls; do not recompile/run them during this source task.

Proposed new invariant checks:

* Two live devices with distinct callback spies; ordinary zero-library and same-device registered library; foreign/dead/null/unknown device/root/library refused before private-memory access or output publication.
* Same root description at different addresses produces identical canonical bytes; mutation of source descriptor after registered root creation cannot change it. A changed space, visibility, APPEND resolved offset, root constant count, sampler field or flags changes canonical content; equal bytes do not bypass same-device membership.
* Input lease with explicit accessible extent accepts exact PROGRAM/trailing readable tail and excludes tail from owned bytes; short/overflowing counts/signatures and unaligned PROGRAM exercise actual framers/core. Test-owned guard pages may verify reads within declared spans, but no probing fallback is added.
* Destroy/root closing removes future visibility; an already acquired valid read lease holds canonical bytes through copy. Device/root/library lock order is explicit; callbacks see no held lock, can perform bounded same-device lookup and see EMPTY on failure. No universal arbitrary stale-pointer race guarantee.
* Calculate and Create interleave across two devices and unrelated shaders without global lastCalc state. Stage mismatch, alias, capacity1short, unsupported flags and library rejection leave output null/private header unregistered; success followed by source mutation still yields identical owned bytes. Destroy failed/unknown/live objects follows registry membership rather than magic-pointer probing.
* CPU failure sink gets exactly the correct device/status/phase once, after lease/lock release, with no HRESULT mapping assertion. Later actual WDK0003/0010 function-pointer static_assert/native compile is a separate gate after policy resolution; no function-table registration here.

These tests catch real dangling canonical-root references, cross-device aliasing, stale callback routing, trusting self-length as independent accessible bound and partial shader publication. They are not a receipt-only protocol test.

## Ownership / limits / next decision

UMD provider owns canonical root/device registry and lease policy; runtime owns callback argument memory and private shader allocation/lifetime; core owns copied offset-based bytes only after successful publication. Translator remains external/unlinked. KMD/m1n1/Mu initialization/DMA/interrupt/power/recovery unchanged; no machine observation is needed for this CPU boundary. There is no proposed hardware checkpoint before these CPU/ABI/error-contract gates and existing binding/NIR/frontend/OOM/complete-DDI requirements are met.

Concrete unresolved production conditions: (a) accepted readable PROGRAM/signature args trust provider for pointer-only DDI; (b) exact permitted VOID shader/Calc error signaling on selected D3D12 ABI; (c) actual per-device root/device/shader live registries and teardown/lifetime implementation; (d) safe PSO access to tagged stored programs instead of oldtokens layout; (e) existing broader R1/translation/license/notices/complete advertised model gates. One pass establishes a useful original CPU bridge checkpoint; it does not close those conditions or justify any SM6/GPU/caps change. Stop here, no equivalent source archaeology loop.

Root checkpoint scope: prepare an original unlinked CPU provider with opaque trusted compatibility bytes copied into provider-owned storage. The production field-wise canonical root encoder remains a separate open condition. Use owned valid test buffers and API lifecycle transitions only; no pointer probing, fault injection, fault sweep, C21E analysis, translator invocation or production HRESULT/DDI/caps changes. Caller serialization and lease/reentry rules must be explicit. Source plan manifest: .local/c15-next-bridge-source-plan/source-manifest.json SHA-256 4696c924bead602a61b2e3e5f6894a121739a500c39171b14127a05512b42bc0.
