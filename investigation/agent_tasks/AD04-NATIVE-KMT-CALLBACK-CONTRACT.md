# Native KMT callback bridge — bounded qualification contract

2026-09-14 (local date). Implementation starts after clean checkpoint 36ae38d.
No hardware execution or supported Direct3D feature-level claim. This is the
immediate preregistered native hardware qualification path while pipeline mask
stays zero; production runtime factory/frontend and desktop work continue.

Sources inspected: pinned WDK26100 `d3dumddi.h` callback structures,
`d3dkmthk.h` matching KMT operations, `d3dukmdt.h` Lock/Signal flags; existing
`apple_agx_d3dkmt_render.c`, `apple_agx_dynamic_triangle.c`, UMD runtime device,
screen, owner, batch adapter and composer; KMD `allocation_windows.c`,
`memory_windows.c`, `lifecycle.c`; physical topology; native Asahi constructor
and `agx_device.h`; Asahi Linux `hw/t8103.rs` for the selected G13G model.

## Ownership and public interface

`windows/one-shot/agx_kmt_native_bridge.[ch]` belongs to the existing one-shot
project. The bridge borrows real adapter/device/paging-queue handles and the
mapped paging-fence pointer. `Create` invokes the existing UMD runtime
initialization, which creates one real KMT context. A failed Create can still
return a nonnull caller-owned bridge for receipt inspection and retryable Close.
The caller must not discard it solely because the HRESULT failed.

`GetBinding` returns the existing Windows screen, native backend, native owner,
owner/batch operations, compiled KMD device-info contract and actual context
handle. ScreenBuffers remains the only allocation identity table. The bridge
stores no second allocation-handle registry. Callbacks and native work execute
serially on the creating thread. Native scene/context/screen destruction must
finish before bridge Close; borrowed KMT handles are closed by the client only
after Close succeeds. Uncertain ownership preserves the bridge for recovery.

## Callback mapping

- QueryAdapterInfo -> D3DKMTQueryAdapterInfo/UMDRIVERPRIVATE, exact output size.
- Create/DestroyContext -> real KMT operations, preserving private generation,
  node/affinity and returned context/buffer/list identities.
- Allocate -> D3DKMTCreateAllocation with the existing native owner description.
  Only one allocation and no runtime resource handle are admitted.
- Lock -> ensure residency, then D3DKMTLock with unchanged whole-allocation
  ReadOnly/WriteOnly flags. No Discard, renaming, aperture or IgnoreSync policy
  is invented. Unexpected changed handles stop and preserve uncertain ownership.
- Unlock -> D3DKMTUnlock for the exact owner allocation list.
- Deallocate -> balance this bridge's explicit residency contribution, then
  D3DKMTDestroyAllocation2 with AssumeNotInUse=0 and SynchronousDestroy=1.
- Render -> validate/correlate the actual v4 wire command, ensure residency for
  the composer's dense list, call D3DKMTRender once, return all replacement
  buffers/list capacities and queue fields. The existing composer owns holds,
  replay prohibition and ordered retirement.
- Signal -> D3DKMTSignalSynchronizationObject2 with the real context and the
  composer's EnqueueCpuEvent handle. Hardware code never calls SetEvent.

The bridge does not publish D3D/DXGI callbacks or a feature level. Its unused
DXGI table contains no successful Present stub. The qualification caller reuses
the shared real native scene; controlled KMD images, manual events and teardown
injection remain confined to the existing executable test.

## Residency correction and tests

MakeResident is reference-counted, not idempotent. The official
[D3DDDI_MAKERESIDENT contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-d3dddi_makeresident)
states that it increments residency references and reports the paging fence for
pending completion. The [residency overview](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/residency-overview)
requires matching Evict calls.

Two qualification-only fields in each existing ScreenBuffers slot record the
bridge's one held residency reference and pending paging fence. They are zeroed
with the slot. MakeResident is issued for one allocation at a time, avoiding
partial-batch attribution. STATUS_SUCCESS and STATUS_PENDING with one admitted
allocation acquire the contribution; it is stored before waiting. Timeout keeps
that contribution and exact fence. Later Lock/Render requests only wait/reuse.
Unexpected positive statuses/counts stop with uncertain ownership. A failed
native call is retained in the operation log, never reported as resident.

Evict is allowed only after the allocation is unmapped and both source and
submission holds are zero, and after any pending paging fence completed. A
failed Evict retains the contribution; successful Evict clears it before
Destroy. Failed Destroy therefore cannot cause a duplicate Evict on retry.
Subsequent use of a surviving allocation reacquires one reference.

The conditional `AgxKmtNativeBridgeResidencyContractTest` belongs in the existing
UmdContractTest and injects only MakeResident/Evict dispatch. It checks pending
timeout/reuse, deduplication, mapped/held eviction denial, failed Evict/retry,
no double decrement and reacquisition. The hardware client must not define
AGX_KMT_NATIVE_BRIDGE_TEST; production dispatch binds the real KMT imports.

## Mapping through Render

Microsoft's [allocation usage tracking](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/allocation-usage-tracking)
explicitly permits keeping allocations mapped for their lifetime in applicable
LockCb/UnlockCb paths. Mapping lifetime is separate from synchronized CPU use.
The qualification freezes source writes at Seal and uses its existing ordered
retirement before reuse/destruction. The KMD creates CPU-visible,
AccessedPhysically allocations, supports aperture plus local placement, and
does not set a cached-allocation flag. Local segment CpuVisible is not silently
changed. Residency precedes locking to avoid committing an unsupported
nonresident locked allocation later. There is no source justification for
adding IgnoreSync or replacing the Lock flag contract with Lock2 flags.

This is a source-derived supported approach, not a claim that an unmeasured
placement has succeeded on this machine. Actual KMT Lock/Render results are
preregistered checkpoints. A rejected CPU mapping or source residency is a
pre-GPU boundary and must not be misreported as an AGX execution verdict.

## Correlation and verification

Before real Render the bridge stores/emits the validated command ContentHash,
Windows generation, reference/relocation counts and byte length. A CREATE_NEW
binary dump records the exact submitted command without replacing earlier
evidence. These correlate with KMD NativeGraph receipt CommandHash; the UMD's
local ordered fence number must not be compared to WDDM SubmissionFenceId.
Every KMT call records its exact NTSTATUS and converted HRESULT, stage, real
handle and counts. CPU-event completion, physical KMD receipt and output
readback remain distinct evidence.

UMDRIVERPRIVATE generation/model/page/class values are a **compiled KMD
contract plus current boot generation**, not measured live GPU inventory.
The native parameter translator derives the allowed G13G one-cluster model
from these validated facts and pinned T8103 configuration, with optional
soft-fault use disabled. Parent hardware gates must independently establish
the actual J313/package/retained-root contract before launch.

Compile-only evidence: source snapshot
`ebba371d0e2b9434e2bc4cdf53a92081451c88c35db45b59ac9a7d44c996519d`
in `evidence/AD04-kmt-bridge-compile-20260913T221855Z/` compiles the final bridge
and its conditional test function on x64 and ARM64 with pinned MSVC14.44 and
WDK26100, `/std:c11 /W4 /WX`. This is not a linked/executed client or hardware
result. Parent owns full client linking, executable regression invocation,
package/sign/hash gates, experiment preregistration and controlled hardware run.

## Integrated qualification gates and operator evidence

Final i-x64 shared scene execution and clientlink PASS; j-arm64 test/clientlink
PASS, noARM64execution. Bothpending andimmediateorderedmarkers preserve the stable
capsule until explicitretirement. Residency regression:2acquires/3reuses/2evicts,
errors0. Freshness regression rejects old/equal snapshots with reusedcommand.
The client takes baseline before native resources/submit and requires a newer
samebuild/boot snapshot, correlated by actualwire hash andWin32generation. The
Windows local orderedmarker counter is not compared to KMD submissionfence.

The actual KMD native completion path captures a lease on the real destination,
performs cache synchronization after physicalcompletion, copies the complete raw
allocation (bounded16KiB), and publishes a versionedNativeGraphReceipt. It does
not use the legacy72-pixel/color oracle orVisibleAgxPresent. The client verifies
rawhash, native roots/counts, and the actualscene128-pixel triangle using its same
Mesa-packed color constants. Unknown completion/readback/cleanup preservesprocess.

Final source/artifact hashes and allscope limits are inGPU_CURRENT_STATE.md.
16hosttests and99ARM64 KMD objects withSubmitQualification +codeanalysis pass.
BothMSBuild appgates have0warnings/errors. Subsequent package-script pinning is
Windows-parser verified but actual SYS/package/signing is still pending.

Last hardware source/profile anchor: root.local/experiments/
EXP682-four-native-frames/air-manifest.json (PackageBuild680,VisibleAgxQualification).
Use unchanged validatedEXP584 m1n1 +EXP406 Mu artifacts for GPU-enabled launch,
ordinary377/392 for recovery; hashes will be preregistered beforeanylaunch.
Rootrun_uefi.py has a preexisting ramdisk-chunk upload change only; ordinary
non-ramdisk GPU/recovery commands do not enterthat branch. Do not overwriteit.
No hardware readiness or desktop acceptance follows from these offlinegates.
