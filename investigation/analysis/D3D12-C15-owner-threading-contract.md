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
