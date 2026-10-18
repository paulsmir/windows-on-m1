# R149 offline CPU0 / UMD error investigation

The CPU0 process, module and function remain **unidentified**. The available
evidence does not prove a UMD spin, a wait after rejected submission, or a cause
for loss of SSH. A separate, deterministic GPUVA rejection-to-frontend error
propagation defect was reproduced and fixed offline. It must not be described
as a hardware stall fix.

## Evidence and attribution limits

Evidence root: `.local/experiments/EXP862-r148-pools` in the main repository.
Derived evidence: `.local/experiments/R149-offline-stall`. Builder work used
only `C:\agx\r149-stall` on the authorized Windows builder; no Air endpoint,
package build/install, guest interaction or reboot occurred.

`cpu-diagnostics.json` contains two CPU0 snapshots with
PC `0x7ff8eb98f104`, SPSR `0x20001040`, and host tick count
630974 -> 3288160. The `iar=121307` field is **SGI acknowledgement count**,
not an all-interrupt count: `m1n1_windows/src/hv_exc.c:456` takes PC from
`hv_get_elr()` and `sgi_iar` from `sgi.iars`. Its independent
`last_iar_intid/last_iar_tick` fields are updated by `hv_irq_diag_vgic_iar`
at line 271. Both snapshots name INTID 18, with advancing timestamps
`0xfbc34eb3 -> 0x3f85a0358`. Therefore neither frozen guest interrupt
delivery nor a continuously executing user instruction follows from the
unchanged SGI counter/saved ELR. Distinguishing user execution from a repeated
EL2 boundary needs more state than these two snapshots. CPU0 LR0 remains
`0x9080020000000040`; this report makes no register-policy change from it.

The exact collected ETL has SHA-256
`54f134cd07bf3c7871638430c1add2347a3de25fd3e05bece2cdf1c0cf645999`.
Native OpenTrace/ProcessTrace successfully decoded 145467 events, with
145465 extended stack records containing 2573790 user and 2128780 kernel
frames. There were zero frames in
`0x7ff8eb980000 <= PC < 0x7ff8eb990000`. Providers are only DxgKrnl,
Kernel-EventTracing metadata, and trace headers; process/image-load and CPU
sampling/context-switch providers are absent. Get-WinEvent emits 145466
rows (one fewer header), from `09:25:31.2690057Z` through
`09:26:56.5446902Z`. The ETL header end is `09:39:15.9195558Z`;
that end time does not establish events across the whole hardware window.
Header lost-event/buffer counters are zero, which likewise does not establish
complete capture of a stalled/unflushed tail.

There is a further provenance constraint: receipt pointers
`0xffff9388623cf3a0` and `0xffff938861bfb380` do not occur in the decoded
ETL. Numeric handle `0x40000fc0` occurs for other objects, including an
allocation created by PID 5108 at `09:25:49.9542819Z` and destroyed
`09:25:50.0271274Z`, and an unrelated context. The corresponding kernel
pointer domains are `ffff8287` / `ffff9a01`. The QUERY investigation owns
the allocation-lifetime joins; these handle occurrences must not identify
the receipt owner or CPU0 process. Exact armed-boot provenance needs to be
resolved before making same-boot claims from this ETL.

The exact packaged UMD DLL and PDB are present. DLL SHA-256 is
`817348ae8cce47e8685a301f30c48a3f7dc9da81c92d43108468893eed369849`;
PDB SHA-256 is
`4b7883515840bd62c9336fdb11b336a3d8734e233d79bac5ecb27df67f8b433d`.
DLL machine is ARM64, preferred image base `0x180000000`, SizeOfImage
`0xbc9000`, RSDS GUID bytes `f88281fc1913544caca262440e4d9b97`, age 1.
No actual process load base accompanies the saved PC. Preferred base and
PDB alone cannot convert an ASLR virtual address into an RVA. An older dump,
or a later recovery process/module list, cannot supply that missing same-boot
identity. No symbol name is assigned by guessing a 64-KiB-aligned base.

## Actual rejected-submit and wait paths

`umd_gpuva_windows.c:350` calls `pfnSubmitCommandCb`, logs its result, and
returns 0 immediately on FAILED(result), before `signal_render` or
`wait_object`. `AgxWin32GpuvaSubmit` then evicts the held residency and
returns failure; `AgxWin32AsahiBatchFinish` sets Rejected. Its Poll function
returns immediately for Rejected, without WaitRender. Native
`agx_batch_submit` marks `ctx->any_faults`; subsequent native operations
fail closed. No local retry/spin loop was found on this path. The source
functions Submit/Retire, FlushStatus/flush_retire, BatchPoll/BatchRelease
match EXP862 source.zip exactly; `umd_gpuva_windows.c` matches byte-for-byte.

The retained UMD log has no submit/signal/completion diagnostics. Its NUL
tail and uncertain trace completeness prevent asserting those paths never
ran, but they provide no accepted-submit/fence proof. G4 KMD rejection
receipts alone do not prove a UMD callback return or a later wait.

There **is** a separate timeout-contract defect on accepted work:
GPUVA BatchPoll discards its timeout argument, then Retire -> WaitRender ->
`wait_object` invokes WaitForSynchronizationObjectFromCpuCb with a zeroed
request and `hAsyncEvent=NULL`. The same helper is used for paging and the
accepted submission's internal readback fence. Microsoft documents that a
NULL event blocks until the condition is satisfied. Thus a zero-time poll
can block on an accepted unsignaled fence. This is a blocking callback,
not a demonstrated CPU busy-spin. It was not changed here because EXP862
has no accepted-work causal evidence and a safe correction needs explicit
pending-event lifetime, timeout/error state and retained-resource tests.
Proposed replay: leave a successfully submitted fence unsignaled, call
TryFlushRetire/Poll(0), require immediate WASSTILLDRAWING and unchanged
ownership, then signal and require exactly one retire. A future fix should
thread a bounded/poll wait through the GPUVA operations and preserve all
holds after timeout; it must not synthesize completion.

Official contracts inspected:

- [SubmitCommandCb](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_submitcommandcb)
- [WaitForSynchronizationObjectFromCpuCb](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_waitforsynchronizationobjectfromcpucb)
- [D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-d3dddicb_waitforsynchronizationobjectfromcpu)

## Confirmed error propagation correction

See `R149-stall-error-plan.md`. Before correction, clean rollback left
Backend.Failed unset; FlushStatus ignores native any_faults and returned
S_OK. The extracted real frontend Flush -> production BatchFinish ->
GpuvaSubmit/parser -> rollback -> real FlushStatus replay reported:
`rejected=1 terminal=0 frontend_errors=0 status=0` (RED).

The fail label now sets Backend.Failed independently of Gpuva.Terminal.
The former stops new work and exposes failure to frontend Flush; the latter
retains its ownership-uncertainty meaning. Preparation/allocation failures
at this label also become terminal for rendering, consistent with native
any_faults. No residency, fence, private-lease, parser or hardware acceptance
rule changed. Cleanup readers were checked: BatchRelease,
agx_bo_unreference/dispose, Collect and Detach can still release safely
rolled-back resources with Backend.Failed set.

`tests/test_r149_submit_error.py` is GREEN with
`rejected=1 terminal=0 frontend_errors=1 status=80004005`. It preserves
the original production-pool replay's successful admission, rollback,
private-lease release and reference assertions, covers preparation failures,
and requires immediate Poll(0) for a rejected batch. Both this test and
`test_g4_mesa_pool_residency_replay.py` pass with AddressSanitizer and UBSan.
The Submit operation is the modeled runtime/KMD boundary; no claim is made
that this host test executes a real Windows kernel callback. Parent owns
the full suite, ARM64 verification and commit/ledger entry. No hardware
validation or SSH-cause verdict follows from this software RED -> GREEN.

## Next causal evidence

Stop this offline stall-attribution pass: no same-boot module identity or
execution stack remains to resolve CPU0. The smallest additional capture
needs CPU0's actual process identity (TTBR/process/thread) and same-boot
module base/RVA or a kernel dump, together with timer/IRQ state that
distinguishes a guest user loop from repeated EL2 entry. If a future
authorized run reaches pinned SSH, collect module/image-load and scheduling
evidence before loss. If it cannot, preregister a bounded capture at the
failure boundary and retain the existing exact-package dump-first recovery.
Do not select a new hardware stall experiment solely from the stationary PC.

If the next authorized experiment prioritizes fail-closed guest survival,
recommend **error-publication-only on the exact EXP862 base** before either
USC acceptance progress or the pending Flush-retirement change. The single
variable has a demonstrated software defect and does not enable a new GPU
acceptance path. Independent checkpoint: the same ordinal14 rejection is
followed for that device by `flush-state` Backend.Failed=1 and a frontend
`reject-seterror` failure, with no further native submissions from that
terminal device. SSH survival is a secondary observation, not the primary
acceptance criterion. Missing callback/Flush return evidence makes the result
inconclusive. USC remains the strongest mapping-boundary target; this order
isolates failure publication before moving into the separately unresolved
accepted-submit wait path. No hardware run is authorized by this report.
