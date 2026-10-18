# ARCHITECT_DECISION — native BO association and CPU source lifetime

Baseline158a7403c9c3732dd5f0e65bc5c787ac1ed8ad53; offline design only.
This implements the remaining scope of AD04-ASTRA-V2-PROVENANCE-REVIEW.

## Source facts and proof limits

`render-admission/umd/src/umd_internal.h` defines ADMISSION_UMD_DEVICE and its
64 ScreenBuffers. Each buffer owns Token, KernelAllocation, Bytes and LockedBase.
`umd_win32_screen.c` creates the allocation using pfnAllocateCb, stores pfnLockCb's
pData in LockedBase, calls pfnUnlockCb before clearing it and pfnDeallocateCb
before clearing the slot. There is currently no lock or source-read hold for
these operations. Finalize loops through unmap/destroy; RuntimeDeviceFinalize
and AgxD3d10WindowsCloseDevice must not free a busy owner.

Microsoft D3DDDICB_LOCK documents pData as the returned CPU mapping and
GpuVirtualAddress as reserved/zero. Discard can replace hAllocation; current
map code does not request Discard. Sources:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-_d3dddicb_lock
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_lockcb

Pinned Mesa pool.c returns CPU/GPU offsets from the exact out_bo. agx_state.c
does not yet request out_bo. The current bridge accepts bases from its caller.
b30bfaa adds an expected-identity callback CONTRACT, not a real atomic owner
implementation. Test RetainExact uses memcmp over a struct with possible padding;
replace with explicit member comparisons. Expected/generic reference logic is
duplicated; share internal validation/publication logic while preserving APIs.

## Selected owner and identity

Extend existing ScreenBuffers; do not introduce a second device/resource registry.
Internal Windows-native BO creation associates the actual `struct agx_bo *` with
its already-created slot after allocation succeeds. NativeBo is an opaque key
until membership is proven; never dereference a foreign pointer to find an owner.
Association APIs are internal backend operations, not user-command fields.

Slot identity: device owner cookie, device generation, monotonic Token/Serial,
NativeBo key, KernelAllocation, allocation Bytes, Class/Access. No token/serial
wrap reuse. Mapping identity: MapEpoch, current LockedBase, mapped length/access.
Serial changes on re-creation; MapEpoch changes on unmap/remap. A native caller
holds its Mesa BO reference while requesting association lookup, preventing ABA
on that pointer. Reset closes acquisition; old device/generation never revives.
Allocation-list index belongs to a particular capture/submission, not a stable
slot ordinal or KernelAllocation handle: build that index from the request list.

Use one SRWLOCK in ADMISSION_UMD_DEVICE for slot associations/lifecycle and
counts. Distinguish BO/capture references from CPU source holds. Neither count
means VidMm residency. A source hold is effective only because the real unmap,
destroy, detach, finalize and reset paths consult it before invalidating memory.

## Atomic transitions and callbacks

Slot phases: free/reserved/live/mapping/unmapping/destroying; mapped status is
separate. Under lock reserve a phase, drop lock for Windows callbacks, reacquire
to publish or rollback the same token/generation. Do not call runtime callbacks,
Mesa free functions or user error callbacks while holding this lock. Reentry
observes an in-progress phase and fails/busy instead of deadlocking.

AcquireSource(Device, NativeBo, expected generation/serial/map epoch,
SliceCpu, Bytes) locates the live association under lock, checks CPU-read access
and derives Offset solely from authoritative LockedBase. Check pointer/range
overflow, acquire SourceHolds, return an opaque hold id + exact range identity.
No CpuBase or GpuBase supplied by the request participates as authority.
WriteOnly mappings are not valid read sources; use a supported readable mapping.
Unknown key/stale epoch/wrong owner fails without modifying outputs or counts.

CopySource(hold, destination) consumes only the validated range while the hold
keeps its map alive. Copy outside the SRWLOCK into owned request bytes. Serialize
native emission and this copy on the existing caller-owned batch/capture path;
a lifetime hold alone does not prevent concurrent writes. ReleaseSource checks
exact hold id/generation once. Unmap/destroy/detach while held returns busy with
all state intact; no Sleep/deferred raw pointer. Callback failure restores the
prior live/mapped identity rather than dropping ownership.

BO RetainExact runs under the same owner lock and compares all fields against
the actual slot; Release reverses the exact acquisition. Snapshot/copy source
holds are separate from references retained until Render consumption/fence.
Do not hold a Windows CPU lock as a claimed GPU residency mechanism. Before
native submit, integrate owned source bytes into the existing materializer/read
path and prove residency independently; do not submit stale copied addresses.

Finalize first prevents new work and reports busy for active source/capture holds.
AgxD3d10WindowsCloseDevice must preserve owner storage on this busy result before
freeing the pipe or runtime owner. Do not hide busy behind the void Finalize API.
Reset may invalidate admission immediately but must defer mapped storage disposal
until existing readers drain; late release targets the retained old generation.

## Concrete next implementation slice (Terra Low)

Implement the association/source operations IN the existing UMD screen owner and
gate its map/unmap/destroy/finalize operations. Use the existing Windows-runtime
test harness with controlled WDK callbacks to execute these real functions.
Cover forged bases/key, stale token/map epoch, pending callback reentry, failed
lock/unlock/deallocate rollback, source hold versus unmap/destroy/finalize,
snapshot read after attempted invalidation, release twice and BO pointer reuse
with old generation. Include wrong identity between Query and RetainExact.
These are owner/callback tests, not actual hardware or Mesa emitter acceptance.

Do not add production NativeBo association callers until the real native BO
create/free seam exists. A test may attach an opaque sentinel through the same
internal association function, with this limitation explicit. Next integration
checkpoint must compile the actual Mesa BO/pool source against the Windows
owner, not another standalone device helper.

## Native source overlay shape after owner tests

The Windows-specific native agx_build_pipeline variant calls with_bo(...,&bo),
registers exact emitted stage byte length and known typed pointer fields with its
batch, then seals/copies under the source hold. Emit using actual pinned native
pack functions. Propagate capture/allocation failure through the real pipeline
callers and prevent submit; zero is not a successful substitute pipeline.
Do not patch shared host/OpenCL USC builder layout merely to carry callbacks.
Native construction-address allocation and v2 KMD owned placement remain a
separate Astra integration contract before executable native draw publication.

No caps/wire/hardware change is authorized by this owner slice. Full Graphics
mission remains active; Terra executes these concrete changes and tests without
another design-only bounce for routine details.
