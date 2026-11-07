# RC11 offline error-contract correction

Scope: codex/rc11 at 6725550c, no hardware, guest, builder or other worktree.

Inspected primary contracts: cached Microsoft handling-errors.html:739-779 and
cpu-efficiency.html:2695-2755 under .local/d3d12-error-routing-1422-primary/sources
(shared host cache); WDK d3d10umddi.h:6757 and DDI map/copy table entries;
Mesa src/gallium/drivers/asahi/agx_pipe.c:912 onward; local GPUVA request,
canonical replacement, native query, D3D10 FlushStatus and D3D12 submit_owner.
RC9's runtime report and EXP1947/1948 UMD traces supply observed evidence.

Microsoft explicitly makes allowed errors per-DDI. AllowOutOfMemory permits
E_OUTOFMEMORY; AllowMapErrors permits WASSTILLDRAWING only with DONOTWAIT and
DEVICEREMOVED. Thus blanket conversion of every DWM Map/Copy failure to OOM
cannot be claimed to prevent removal. The RC9 report did not decode the
runtime's per-DDI category table. No guessed category override is justified.

The UMD owns preservation of callback error information, trim/retry and CPU
staging. VidMm owns placement/paging. The KMD authenticates native backing;
Asahi consumes the bool transport and latches failed batches. Firmware, DMA,
interrupts, initialization, power and recovery ownership are unchanged. Live
state, m1n1/Mu comparisons and package work are excluded by the offline task.
No external code is copied.

Implementation plan: preserve failed residency/copy/canonical callback status
in separate GPUVA operation storage, classify only absent/unpublished native
leaves as unavailable backing, retain identity/translation errors, and consult
that status at frontend failure boundaries instead of inventing E_FAIL/HUNG.
Never clear a faulted batch, pretend dropped commands succeeded, or hide an
actual DEVICEREMOVED. Existing bounded trim and scheduling-touch retries stay.
Tests execute production functions with failure injection before and after.

The smallest hardware checkpoint is the integration owner building base HEAD
plus these changes with Wom1GpuFaultInfoRead and Wom1PoolHalfSize, 2k then 10k.
Collect first runtime error/removal and KMD receipts before exact-package
GPU-visible recovery. Expected product outcome remains DWM alive and GM
rendering or clean OOM; the offline correction alone does not prove it.


## Confirmed clean-refusal defect

`AdmissionUmdGpuvaRestoreCpuMapResidency` used to mark DrawTerminal after every
restore failure, even E_OUTOFMEMORY with no accepted paging work. Its caller
ScreenMapBuffer then invented DEVICE_HUNG. The restore helper already handles
accepted paging uncertainty. It now owns the full distinction: clean callback
refusal retains its HRESULT and allows a later retry; malformed success or a
fence on refusal stays terminal. Existing failed/missing paging-fence tests
remain intact. The regression refuses the map, checks no pointer/residency/hold
was published, releases pressure, and maps/unmaps successfully.

## Runtime removal remains unresolved

EXP1948 gm1948a-umd.log:15504 reports MakeResident 0x88760870 before replacement
failure (:15505) and before owner Execute reports HUNG (:15510). EXP1947 has
the same ordering at :10137-10143. Neither log has a preceding SetError receipt
identifying the original runtime decision. EXP1950/evidence is absent locally.
Do not relabel an already removed device as OOM. The correction preserves that
status and stops canonical replacement after a terminal query callback error.

The generated D3D11 Map/Copy and Asahi failed-batch state are not replayable
transactions. Clearing their fault flags would silently lose commands. The
published per-DDI error policy also precludes claiming that returning OOM
alone keeps these devices alive. Allocation/backing placement or a complete
retained-command retry design is still necessary if the next run reaches that
boundary. No unsupported blanket SetError rewrite is included.

## Evidence arrived during handoff: EXP1950

Correction to the earlier availability statement: EXP1950/evidence appeared
before the final report. Arm A gm1950a-umd.log:10047-10054 shows CantTrimFurther=0,
MakeResident E_OUTOFMEMORY after bounded trimming, then batch refusal and our
Execute DEVICE_HUNG. This directly supports the error-preservation change.
Arm B :15348-15356 shows CantTrimFurther=1 accepted pending work followed by
MakeResident 0x88760870 and then our HUNG, like EXP1947/1948. Its earlier runtime
removal cause remains unresolved. Both application stderr files report removal.
The global umd.log:33742-33746 also directly pairs native leaf-absent reason 3
with D3D11 ResourceCopyRegion E_FAIL. These are old-package observations, not
validation of RC11. No hardware was accessed to obtain them.
