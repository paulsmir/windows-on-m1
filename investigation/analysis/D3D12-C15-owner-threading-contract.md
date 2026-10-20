# C15 preparation: native-owner publication and device threading

Status: primary-source contract and bounded test proposal only. No implementation,
regression fixture, build, guest command or hardware run belongs to this note.
This is a future C15 ownership/reentrancy gate; it does not change feature order.
Inspected current source at `8327378beab562cd240507f99a667afe35301a60`.

## Primary Windows contract

Microsoft DirectX-Specs [CPUEfficiency.md, pinned
71e431fa0f713818c6a3a66f49ab0e6732712d15](https://github.com/microsoft/DirectX-Specs/blob/71e431fa0f713818c6a3a66f49ab0e6732712d15/d3d/CPUEfficiency.md),
sections **Threading Model** and **DDI Overview / Threading**, specifies free-threaded
device methods and no configurable D3D12 threading mode. The UMD must implement
that model. The runtime serialization exception concerns command-queue operations
and associated DXGI Present/Blt; its enumerated DDIs are ExecuteCommandLists,
UpdateTileMappings and CopyTileMappings. This is not a device-wide resource-creation
lock. Consequently independent resource creations on the same device may reach
the UMD concurrently; CreateHeapAndResource cannot assume runtime serialization.

Pinned WDK `.local/reference/wdk26100/d3d12umddi.h` corroborates the concrete ABI:
`D3D12DDI_CREATE_DEVICE_FLAGS` contains DISABLE_IMPLICIT_MGPU and DEBUGGABLE,
not a single-threaded opt-out. `D3D12DDI_DEVICE_FUNCS_CORE_0003` contains
`pfnCreateHeapAndResource` with device/resource-private handles; neither its
signature nor the negotiated caps provide a resource-creation serialization bit.
The header alone is not the positive concurrency guarantee; the Microsoft DDI
threading section above supplies it. This is a FULL GRAPHICS UMD contract.

## Current source observations and owner boundary

In `drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp`:

- `EnsureOwner` (lines772–787 at inspection) tests plain owner/owner_failed,
  calls AgxD3d12OwnerCreate and then publishes two plain fields. There is no
  once-state or synchronization surrounding this sequence.
- `CreateHeap`, committed-2D creation and CreateVolumeResource can reach that
  same lazy owner. Native heap creation subsequently uses the returned owner.
- RegisterHeap/UnregisterHeap (lines790–799) mutate heap_count/heaps without
  synchronization; HeapAt (lines800–809) reads them. Correct initialization of
  those fields does not make subsequent concurrent operations atomic.
- DestroyDevice destroys only the currently stored owner pointer. With multiple
  successful constructions, an earlier owner can be replaced while resources
  created through it remain live. Exact downstream effects require a regression,
  not speculation about an existing hardware trace.

These are source-level concurrency gaps under the allowed Windows call model.
No runtime trace in this task demonstrates that they have occurred on J313.
The already-proved EXP1381 uninitialized private-device defect is separate:
its dump proves owner=-1 consumption and does not identify the writer. Nothing
in this note attributes that value to a race or changes its accepted diagnosis.

The UMD owns publication, per-device resource bookkeeping and native-owner
lifetime. KMD/firmware/m1n1/Mu do not serialize these user-mode fields. No AGX
register, initialization sequence or hardware capability should change for
this contract. A later complete owner-usage design must also inspect actual
Gallium/Asahi context concurrency; a publication-only fix must not silently
claim that all operations on the shared context are safe.

## Smallest bounded causal test proposal

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Two valid resource-creation
threads on one correctly initialized DDI device must not construct and publish
two independently live native owners, leak one, or return owners inconsistent
with the device lifetime owner.

Extract actual AgxD3d12Device, CreateDevice, EnsureOwner and required callback
wiring, following the existing private-device initialization fixture. Replace
only external AgxD3d12OwnerCreate with a counting stub returning distinct stable
owner objects. Start from actual CreateDevice on nonzero storage, preserving
the prior initialization regression independently.

For an original-source counterexample, force this schedule with coordinator
condition variables and bounded waits:

1. Thread A enters actual EnsureOwner, reaches the owner-create stub and blocks.
2. Thread B enters actual EnsureOwner while A's stub has not returned. Observe
   whether a second stub invocation occurs; do not insert a test implementation
   of the production predicate or publisher.
3. For the two-construction schedule, let A return owner A and join A before
   allowing B's stub to return owner B. Thus the conflicting publication writes
   occur in a coordinator-established order. Both initial reads happen before
   those writes. This demonstrates duplicate construction/overwritten ownership
   without depending on simultaneous writes or a C++ data-race outcome.
4. Assert a single live constructor result shared by both callers, one published
   owner and one destruction; two created owners or mismatched returned owners
   falsify the invariant. Assert all newly created objects are accounted for.

The fixed implementation may intentionally block B before its constructor stub;
therefore a future GREEN harness must not require two stub arrivals unconditionally.
It should release A after the bounded observation interval, join both workers,
and judge constructor/owner/destructor counts. A timeout is a test failure only
when workers fail to retire after A is released, not when correct initialization
serialization prevents a second constructor call. Include a success/failure
interleaving only after the intended once/failure policy has been specified.
Do not make a leak claim from constructor count alone if the selected design
explicitly and safely retires a redundant constructed owner.

This test is strictly offline. Existing hardware probes create isolated devices
sequentially and cannot establish the multi-threaded publication invariant.
A TSAN run can complement the schedule but must not replace its deterministic
ownership assertions. RegisterHeap/UnregisterHeap/HeapAt correctness is a related
separate invariant; design its bounded test after choosing the registry's locking
and object-lifetime rules, rather than widening this first test speculatively.

## Remaining decision

Evidence supports concurrent-entry preparation; it does not select a lock,
INIT_ONCE, eager creation, per-thread owner or per-operation context design.
Before implementation, specify callback/reentrancy behavior, failure caching,
resource lifetime and lock ordering. Preserve allocation callbacks on the
entering DDI thread and avoid carrying a publication lock into unknown callback
reentrancy. No hardware experiment is needed to decide these deterministic
Windows/object-lifetime rules.

Source-license boundary: this note paraphrases Microsoft CC-BY4.0 documentation
at the pinned link. It copies no external implementation or sample code.

## Follow-up: complete current call graph and synchronization boundary

One bounded source pass after the initial note; no implementation or tests added.
Primary native evidence is `.local/reference/mesa/docs/gallium/screen.rst`,
**Thread safety**, lines1090–1097: screen methods must support concurrent use,
whereas a rendering context need not; different contexts sharing a screen may
be used from different threads. Therefore a single shared pipe_context needs
UMD-controlled exclusive use. A screen's own locking cannot substitute for that.

Additional inspected implementations:
`mesa/winsys/agx_d3d12_windows.cpp`, `agx_win32_asahi_scene.c`,
`render-admission/umd/src/umd_runtime_device.c`, `umd_win32_screen.c`,
`umd_gpuva_windows.c`, `umd_asahi_owner.c` (all under `drivers/apple-agx`).
The Asahi context retirement wrapper directly traverses native batch slots,
active/submitted bitsets and synchronizes batches. It is not a separate
thread-safe completion service to call while another thread changes that context.

### Actual outward callbacks

| Boundary | Observed source path and callbacks |
| --- | --- |
|Owner construction|OwnerCreate -> RuntimeAdapterInitialize -> adapter QueryAdapterInfoCb; then kernel CreateContextVirtualCb; InitializeForQueue -> RuntimeDeviceAttach -> CreatePagingQueueCb/CreateSynchronizationObject2Cb -> ScreenInitialize -> native screen/context construction.|
|Native BO allocation/mapping|Screen resource creation -> Win32 transport -> AllocateCb; GPUVA helpers -> ReserveGpuVirtualAddressCb, MapGpuVirtualAddressCb, MakeResidentCb and paging waits; mapped staging paths also use LockCb/UnlockCb. Native initialization can allocate internal resources through these same paths.|
|Submission/retirement|Context flush/native batch transport -> SubmitCommandCb, synchronization callbacks, GPUVA transfer/identity EscapeCb; retire polls native batches and transport completion.|
|Destruction/failure cleanup|OwnerDestroy flushes/retires pending presentations, destroys context/screen, finalizes runtime state, destroys paging/synchronization objects and the owned kernel context; underlying BO release can DeallocateCb/FreeGpuVirtualAddressCb/EvictCb.|
|Core error report|AdmissionUmdSetError -> owner_error -> DDI OwnerError -> device->callbacks->pfnSetErrorCb. This is an outward runtime callback from within an owner operation today.|

These are callback-presence observations, not evidence that any callback actually
reenters a D3D12 DDI. WDK CORELAYERCALLBACKS_0003 explicitly separates SetError /
SetCommandListError / SetCommandListDDITable from its kernel-callback entries;
owner construction currently uses the D3DDDI_DEVICECALLBACKS table for its kernel
context and transport. Both families must be considered before holding a lock.

Existing inner ScreenBufferLock sections reserve/publish buffer slots and release
the lock before AllocateCb (`umd_win32_screen.c` around614), then reacquire it to
finish bookkeeping. Asahi owner Enter/Leave/Associate/Detach/Identity use this
inner lock. They do not acquire any existing D3D12 device or context lock. This
establishes a prospective context-gate -> inner transport-lock order, but not
permission to hold either lock across arbitrary runtime reentry.

### All shared-context operation families

- Compute and graphics PSO creation/deletion use pipe create/delete state calls;
  DXBC/NIR translation also reads the published screen's immutable NIR options.
- Buffer creation maps a newly created resource through the native wrapper;
  texture/presentation creation and release touch the owner's Screen/Runtime.
  They must participate in owner lifetime even where no pipe_context call occurs.
- Render-target/depth/UAV clears, buffer/texture copies, query creation-on-use,
  begin/end/resolve and image/sampler binding use the shared context.
- Dispatch and Draw bind state, execute, unbind temporary views and update
  PendingDispatches/PendingDraws/PendingCopies. SubmitAndWait consumes those
  fields, flushes and retires the same native context.
- Presentation preparation flushes for present; presentation destruction flushes
  and updates PendingPresentations; OwnerDestroy consumes that linked list and
  finally destroys Context/Screen/Runtime.

Public-to-public nested calls in this file are OwnerCreate failure -> OwnerDestroy
and OwnerCreateGraphics failure -> OwnerDestroyGraphics. Other shared helpers
(clear_view_layers, bind_stage, heap_query, collect_presentations) are internal.
An exclusive nonrecursive lock naively added to every public entry would deadlock
those cleanup calls. Recursive locking would avoid that symptom while allowing
unsafe logical reentry into partially modified context state; it is not a proof
of correctness.

### Concrete minimal state and lock contract

The following is the candidate contract to enforce, rather than a claim that a
particular Windows primitive has already been proved safe across callbacks:

1. **Publication/lifetime state:** one per-device short metadata lock guards
   EMPTY/CREATING/READY/FAILED/CLOSING, creator thread identity, published owner,
   cached creation result and active owner leases. It is never held during native
   construction, destruction, kernel/core callbacks, or a blocking wait. The
   selected creator claims CREATING, drops the lock, builds an unpublished owner,
   then publishes one terminal result and wakes waiters. No lock-free plain-field
   fast path may race that state. CLOSING prevents new leases and drains active
   operations before one destruction. No redundant successfully created owner is
   silently lost. A same-thread CREATING reentry policy remains unresolved below.
2. **Registry:** a separate short shared/exclusive metadata lock guards heap_count
   and every heap array read/update. Register/unregister/lookup do not call owner,
   runtime or native code while holding it. Unregister removes visibility before
   resource release; release occurs after unlocking. Lookup returns a borrowed
   object only under the runtime/application's valid lifetime guarantee; the lock
   alone does not extend that lifetime. Independent resource changes must not
   expose half-written records. Do not introduce whole-DDI serialization merely
   to protect this small array.
3. **Native owner transaction gate:** exactly one operation may mutate/use the
   shared context and Pending* state at a time, across ALL queues on that device.
   The transaction for ExecuteCommandLists starts before replay of its first
   command and ends after SubmitAndWait and retirement. Per-command locks are
   insufficient: another queue could otherwise interleave query begin/end or
   split a replay from the flush that consumes its state. Present preparation,
   creation/deletion of context state, copies/maps and teardown use the same gate.
   Screen-only work at least holds a lifetime lease; pure layout queries need
   neither owner nor gate. Pure compiler work may eventually occur outside the
   gate if it owns its immutable inputs and mutable compiler/cache state.
4. **Composition/order:** acquire a lifetime lease under publication metadata,
   release metadata, then acquire the context transaction gate. A short registry
   lookup during that transaction is permitted only if no registry path waits
   for the gate while holding the registry lock. Release the transaction gate
   before releasing the final lifetime lease or doing outward core error reports.
   Native inner locks are below the context gate; no newly introduced metadata
   lock may be held across those callbacks. Internal cleanup helpers require an
   already-held transaction and must not reacquire it.
5. **Error delivery:** owner_error can record an operation-local error for the
   entering DDI transaction, with the actual SetErrorCb delivered by that DDI
   after the context gate is released. Delivery must stay on the original caller
   thread and retain the correct device/runtime handle. This is a proposed
   mechanism requiring deterministic propagation tests, not an implemented change.
   It does not remove kernel callbacks from the native transaction.

### Decisive remaining normative uncertainty: callback reentry

The bounded review of pinned headers, the D3D12 DDI threading specification and
[AllocateCb documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_allocatecb)
did not establish whether the specific allocation/context/residency/submission
callbacks may synchronously enter another UMD DDI on the same device, or wait
for work requiring that same device's native transaction. Callback signatures
and their kernel-facing purpose are not sufficient to prove nonreentrancy.
The SetError callback path is visibly outward, but its downstream behavior is
also not defined by the inspected local declaration. No runtime trace resolves
this point in the present work.

Accordingly the candidate above is NOT yet permission to put a nonrecursive
mutex around the complete native callback-containing operation. A stateful gate
without an OS lock held across callbacks still has a logical wait dependency;
it can deadlock on the same reentry. Nor is returning E_FAIL on legal reentry a
supported substitute absent a contract. Stop this design pass at this exact
uncertainty. The next source question is the normative same-device callback
reentry guarantee, followed by a forced reentry callback-stub test against the
chosen owner-state/gate behavior. No hardware run is justified to guess it.

The general older Microsoft UMD threading page describing Direct3D9/DirectDraw
single-device serialization is not applicable evidence for this negotiated
D3D12 free-threaded contract. Keep that historical model separate.

## EXP1745 actual-source double-publication counterexample

The current `EnsureOwner` still reads `owner`/`owner_failed` before calling
`AgxD3d12OwnerCreate` and publishes the result afterward without an owner
state gate. A bounded test extracted the current `AgxD3d12Device`,
`CreateDevice`, `OwnerError` and `EnsureOwner` verbatim. Its test-only
constructor blocked thread A before publication, let B enter the same actual
function, then released A and joined it before releasing B. Both original
threads returned distinct owner pointers; the device retained only B's
pointer. Constructor calls were 2, and the old success/failure/invalid
initialization controls remained green. The ASan/UBSan result is
`.local/c15-owner-race1745/result1745.json` SHA-256
`57024cdff471b7bd892f46a38b0bf9391831435a26bef4bc1b534b0eb45ba5dd`.
The schedule does not depend on simultaneous publication writes. This is a
confirmed UMD publication defect, though no J313 trace attributes a real
failure to it.

EXP1751 restored the older poisoned-private-device test harness after the
production `CreateDevice` gained shader-provider registration. The source
extractor is unchanged; its host fixture now supplies only the provider types
and registration stubs needed to compile the current function, and asserts a
fresh provider device token. Its success/failure/invalid controls pass under
ASan/UBSan again. This repair does not change `EnsureOwner` or make the
EXP1745 race test green.

The earlier heap-registry observation is superseded: current source uses
`heap_lock` around register, unregister and lookup. This counterexample
isolates owner publication. Current `AgxD3d12OwnerCreate` synchronously calls
the adapter and kernel callbacks during setup; the inspected Microsoft
threading and callback pages do not establish whether any can reenter a
same-device DDI or wait for work that would. A mutex or wait gate around the
whole constructor can therefore introduce a deadlock. A duplicate-constructor
cleanup policy would also need proof that two partial native contexts can be
created and retired safely. Keep the production fix pending an explicit
callback/reentry and lifetime contract; do not hide this in KMD, firmware or
the shader provider.
