# Owner slice review — a9c56bf is incomplete

## Follow-up at59dd662 — finish existing design before native integration

The same requirements remain open, not a new design question:

- HasLiveSources only observes and releases the lock. It cannot reserve close.
  Implement BeginClose under the shared lifecycle lock: check holds AND pending
  transitions, then mark Closing before releasing. Acquire/Query/Create/Map must
  reject Closing. Internal teardown unmap/deallocate remains permitted. CancelClose
  restores admission if PipeDeviceClose reports busy without teardown. Runtime
  finalization uses this reservation too, with callback reentry unable to recursively
  finalize the same owner. Caller-serialized CloseDevice owns that reservation;
  do not treat an arbitrary repeated BeginClose as a second teardown authorization.
- TestLock's QuerySource currently fails because Mapped is false even if transition
  code is removed. This is a map-publication check only. Add TestUnlock reentry
  with previously valid source identity: it must reject Acquire because Transition
  is set even though the buffer remains mapped until callback return. Add callback
  attempts at Finalize during allocation/map/unmap and require busy without clearing
  context/storage. Force callback failure and verify original identity/epoch intact.
- Unique IDs must reject exhaustion before increment, never skip zero after wrap.
  OwnerCookie must be populated and checked, including held-record lookup; currently
  it exists only as an unused Device field. Query/Acquire failure preserves output;
  explicitly handle Expected==Held before clearing or writing output. CopySource and
  create slot reservation are still absent. These are prior approved requirements.
- Previous upload commands placed newer umd_contract_windows.c in remote src/ while
  the vcxproj compiles tests/umd_contract_windows.c. Those earlier binaries cannot
  establish the claimed new two-hold/busy tests without a matching input manifest.
  The latest test upload used tests/ correctly. Capture the full current source set
  into a NEW immutable input directory and fresh output. Compile the optional
  EnableMesaPipeFactoryTest configuration to cover agx_d3d10_windows.cpp; default
  UmdContractTest does not include that file. No old binary claims cover new code.

Next step is Terra implementation, not further Astra redesign. Complete these
requirements and the original checklist with one exact end-to-end owner test
result before asking for native integration review. Do not self-send a new task
message or finish after each helper/build; use direct tools for the whole slice.

Native association shape is already approved: internal backend-owned create
associates a live native BO key with its existing ScreenBuffer; borrowed lookup
never dereferences unknown keys. Add associate/query/detach operations there with
owner/serial checks, holds and Closing guards. This is an internal API, not a
security boundary within one process. No claim of actual native BO creation until
that caller is compiled. Do not invent GPU construction addresses or change v2
placement during this owner slice.

Baseline3f94397f451974802ab77fae799d728a3585ff81.
The existing AD04-NATIVE-BO-OWNER-DECISION remains the architecture. This review
is a correction of implementation/proof claims, not a new architecture gate.

## Concrete violations

1. AcquireSource uses ScreenBufferLock but Create/Map/Unmap/Destroy do not.
   Unmap can read SourceHolds==0, then AcquireSource succeeds, then pfnUnlockCb
   invalidates that mapping. A lock used only by readers does not protect it.
2. ReleaseSource recognizes token/serial/map epoch, not an acquisition id.
   Acquire A and B of the same buffer; release A twice: both return success and
   SourceHolds becomes0 while B is live. Single-hold duplicate-release testing
   does not catch this.
3. ScreenFinalize may leave a held buffer undeallocated; RuntimeDeviceFinalize
   nevertheless destroys context and ZeroMemory(device). CloseDevice can free
   the owner. A source hold therefore does not preserve device storage.
4. Source identity lacks a device-owner cookie. Device-local counters and
   generation comparisons alone do not establish ownership across devices.
   SourceHolds overflow is unchecked; serial/map epoch wrap skips zero and can
   reuse old identities. Acquire clears output before handling output/input alias.

The prior x64 executable remains a PASS of its sequential test. It does not
prove concurrency, independent acquisitions, owner lifetime, NativeBo association
or complete mapping lifetime. Preserve that evidence and append this correction.

## Complete the already-approved implementation

- Use one lock for all slot state transitions, as the original decision says.
  Reserve mapping/unmapping/destroying/creating under it, call runtime outside
  it, then commit/rollback under it. Acquire rejects non-live phases. Do not
  hold SRWLOCK across callback or byte copy. Queries alone are not usable holds.
- Add unique monotonically allocated HoldId plus OwnerCookie. Track live hold
  records inside the existing device (bounded table is acceptable; this is not
  a second resource registry). A hold records exact slot serial/map epoch/range.
  Validate the table entry on release/copy and consume that entry once. Two
  acquires of the same range remain distinct. Fail before counter/id overflow.
- Add CopySource using the live hold record and authoritative LockedBase; copy
  outside lock while its hold remains registered. Caller serializes copy versus
  release of its own handle. Do not expose a claim that a queried raw pointer
  survives unlock. Input/output alias must reject before modification or use a
  saved input; failure must preserve caller output as specified in prior review.
- Begin-close blocks acquisition and rejects busy source/capture/transition state
  without destroying context, clearing device, invalidating Screen or freeing
  Pipe owner. Wire this through actual AgxD3d10WindowsCloseDevice before its
  destructive actions. Preserve the legacy terminal error contract for already
  terminal deallocation errors, distinct from busy. Void DDI destruction needs
  quiescence before dispatch; never pretend a void return can ask Windows to
  retain runtime-owned private storage. Keep owner-source APIs internal until
  that integration is proven. This was already required in the original design.
- Add the NativeBo association to existing slots per the original decision.
  Only internal BO creation attaches it. Do not dereference unknown NativeBo.
  Native factory/caller is still pending; no GPUVA or residency assertion.

## Tests required before another milestone claim

Extend the existing Windows contract executable with controlled callback reentry
at pfnUnlockCb/pfnLockCb/pfnAllocateCb/pfnDeallocateCb. This deterministically
tests the real transition reservation without scheduling luck. A callback should
try Acquire/Query/Finalize and observe busy/closed with no deadlock or hold.
Test two simultaneous same-buffer holds and replay A while B remains live;
wrong device, stale map epoch, id exhaustion, input/output alias, callback failure
rollback, readable copied bytes and close-busy followed by release and retry.
Exercise the production owner API directly as well as the outer screen wrapper:
the wrapper's existing Mapped guard is not proof of the new source-hold guard.
Restore strong callback assertions for expected flags/classes instead of
weakening all existing fixture validation to accommodate one new workload.

Run current-source Windows x64 executable and ARM64 build with pinned toolchain;
preserve raw output, exact source manifest and binary hashes. Do not reuse an
immutable staging root by overwriting source or run /Clean over old evidence.
Current state should say partial until every applicable item above is tested.

Terra Low owns this full corrective slice, including tests, before returning
to Astra. Routine state-machine implementation is covered by the prior design;
do not bounce to another design review after just a counter/struct/helper change.
