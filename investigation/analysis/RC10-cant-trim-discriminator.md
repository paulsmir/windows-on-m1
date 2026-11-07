# RC10: CantTrimFurther discriminator and return-status evidence

Scope: offline, base 54422fe5, branch codex/rc10. No hardware, guest, builder,
package, other-worktree access, firmware change or capability change. Handoff
README assigns EXPERIMENTS/CHANGES updates to the integration owner.

## Evidence and ownership

- EXP1946 ACTUAL (`investigation/EXPERIMENTS.md:63840`) distinguishes the 2k
  arm's E_OUTOFMEMORY with no GPU fault/TDR from the 10k arm's DWM GPU fault.
  Do not generalize “DWM survived EXP1946” to its 10k arm.
- EXP1947/1948 ACTUAL (`:63857`, `:63880`) report E_PENDING after trimming,
  followed by DEVICEREMOVED and DWM crashes, without reset/TDR. Several rc7
  changes shipped together; the association does not prove CantTrimFurther
  is the removal mechanism. EXP1949 (`:63898`) captured no ETL and is
  inconclusive about the internal Windows decision.
- `.local/reference/wdk26100/d3dumddi.h:4371–4372` declares
  PFND3DDDI_MAKERESIDENTCB(HANDLE hDevice, D3DDDI_MAKERESIDENT *).
  `d3dukmdt.h:1466–1469` says CantTrimFurther permits exceeding the **current**
  budget, but “MakeResidentCb will still fail if the request puts the
  application over the maximum budget.” MustSucceed instead says “put the
  device in error if the resource cannot be made resident.” These are separate
  bits. The header does not prescribe adapter-wide removal for CantTrimFurther,
  nor reveal every later asynchronous failure in VidMm. Current source zeroes
  the request and sets only CantTrimFurther (`umd_gpuva_windows.c:410–414`).
- `gpuva_g3_paging_windows.c:1389–1414` normalizes disallowed UpdatePageTable/
  FlushTlb statuses to success after attempting process poison. Therefore an
  outward-success census cannot exclude this failure route. This existing
  policy is preserved; no new evidence establishes a correct replacement.
- `paging_windows.c:183–219` stores operation inputs without return status.
  `driver.c:21–109` registers callbacks. The new wrappers observe actual
  outward returns, including every early return, without changing the original
  callback bodies. UpdatePageTable is a BuildPagingBuffer operation, not a
  separately registered DDI (`d3dkmddi.h:4593–4629`, `:4687–4710`).

VidMm owns budgets, placement and the internal removal decision. UMD owns
MakeResident retry/trim and paging-fence propagation. KMD owns mapping/paging,
submission and the statuses returned to dxgkrnl. The saved Asahi/native-leaf
mismatch in RC8/RC9 remains a separate limitation, not the demonstrated first
removal trigger. Initialization, power, DMA, interrupts and recovery retain
existing Asahi/m1n1/Mu/Windows contracts; live measurements and source in other
worktrees are excluded by this task. No external implementation is copied.

## Changes and invariants

1. `umd_gpuva_windows.c:397–455`: only exact
   `APPLE_AGX_UMD_CANT_TRIM_FURTHER=0` disables the final overbudget request and
   the last-round exhaust-all trim policy. Unset, =1 and other values retain
   current behavior. The bounded ordinary trim loop, held allocations, hard
   refusal, and accepted paging fence remain intact. The diagnostic
   `measure-make-resident-cant-trim` records count, actual CantTrimFurther bit,
   rounds, HRESULT and the existing trace QPC. It appears on the flagged retry
   or on OOM in the disabled arm.
2. `render_ddi_status_wrappers.h:4–14`, `:212–215`, `:332–335` and
   `driver.c:113`: all 67 registered NTSTATUS callbacks are wrapped in the G3
   profile. Originals execute exactly once and the exact status is returned.
   VOID/BOOLEAN callbacks are deliberately not interpreted as NTSTATUS.
   Allocation is captured before the call where supplied: every supported
   paging operation with hAllocation, and DescribeAllocation. Other calls use
   zero (absent/multiple/unspecified); SubmitCommandVirtual has no allocation
   argument. Object is the opaque first callback argument, never dereferenced
   after the callback and not necessarily a process/device identity.
3. `render_ddi_status.h:9–31`, `receipts.c:12–45`: 128 fixed 48-byte slots,
   failure counters and immutable first-failure QPC/status/operation/allocation/
   object. Atomic claim and release publication prevent mixed first records;
   only NT_SUCCESS=false counts. No allocation, wait, registry I/O or spin
   loop in capture. Slots 0–66 are per DDI, 80–111 outward paging by operation,
   112–127 internal paging before normalization (UpdatePageTable=123,
   FlushTlb=124). First means first successful concurrent claim, not a total
   ordering of simultaneous CPUs. Counts include expected retry statuses.
4. `receipts.c:2132`, `:2187`, `:2248`, `:3005`, `:3099`: first failures with
   a live adapter queue the existing passive worker; profile publication
   queues counter refreshes through that same serial worker. Startup also
   queues it: an older concurrent publisher cannot overwrite newer evidence. A 6176-byte heap snapshot keeps the KMD stack bounded.
   `Wom1DdiStatus` is overwritten by the startup work item and later snapshots.
   Data lives for the loaded driver image, not per process or per arm. There
   is no concurrent reset API. Counter samples are independent, and registry
   persistence is best effort, not a crash-durable transaction. A callback
   lacking a live adapter or running above DISPATCH relies on later publication.
5. `tools/decode_ddi_status.py` validates v1 size and publication state, names
   DDIs and paging operations, and labels expected retry statuses. Raw binary:
   `python3 tools/decode_ddi_status.py Wom1DdiStatus.bin`.

No confirmed source defect establishes the device-removal cause. The delivered
behavior change is the requested controlled opt-out; the receipt gap is fixed.
No protected C75c/headroom policy or speculative paging-success policy changes.

## Smallest integration checkpoint

WHY THIS HYPOTHESIS: (1) the new removal symptom starts with rc7's overbudget
retry among other changes; (2) accepted pending residency precedes the terminal
MakeResident failure; (3) previous receipts cannot exclude returned or normalized
KMD failures and EXP1949 ETW failed to capture. Vary only the retry switch.

One package: task base HEAD (54422fe5) plus RC10 commits. WDK/native compilation
and package hashes/manifests belong to integration. Wom1GpuFaultInfoRead=1 and
Wom1PoolHalfSize=1, existing firmware/m1n1/signer/caps/recovery. Run GravityMark
D3D12 2000 asteroids, windowed, timeout 240, ValidateRefs=1, first arm a with
APPLE_AGX_UMD_CANT_TRIM_FURTHER=0, then arm b with =1. Keep trim enabled,
descriptor guard=1 and validation record-only. Keep DWM/machine environment
identical between arms; the switch is set in the GravityMark task process.
Use a fresh same-profile boot of the same package for each arm so failures,
VidMm pressure, and immutable first slots from arm a do not contaminate arm b.
Capture pre/post Wom1DdiStatus, UMD logs, existing paging/pool/G4 receipts,
DWM events and elevated DxgKrnl ETW verified active before the workload.

Expected discriminator: arm a reaches ordinary OOM without the flagged retry;
arm b records flag=1/E_PENDING and either renders or reproduces removal.
Correlate first failing DDI/internal status QPC with the first removal event,
not merely the later MakeResident DEVICEREMOVED observation. A normalized raw
UpdatePageTable failure with no outward failure identifies a different path
from a failing SubmitCommandVirtual or outward BuildPagingBuffer. A successful
KMD return does not rule out UMD SetError or internal VidMm failures. An expected
INSUFFICIENT_DMA_BUFFER/ALLOCATION_BUSY receipt alone is not causation. If an
expected first failure masks a later status, use ETW and count deltas; this is
not an unbounded event log. Success still requires actual frames and DWM alive.
Collect evidence before exact-package GPU-visible cleanup; retain existing
emergency recovery only for its established conditions. No run performed here.

## Unknowns

First internal Windows removal reason; whether it affects one device or the
adapter as a whole; whether either arm exercises normalized paging failures;
performance and WDK/native compilation; actual render/DWM results. The header
contract alone cannot answer those questions. Host tests validate the switch,
record publication/counting, decoder and wrapper wiring, not Windows execution.
