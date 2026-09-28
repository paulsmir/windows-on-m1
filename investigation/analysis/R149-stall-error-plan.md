# R149 rejected GPUVA batch error propagation

The production frontend Flush -> BatchFinish -> GpuvaSubmit rollback ->
FlushStatus replay is RED: `rejected=1 terminal=0 frontend_errors=0 status=0`.
The rejected batch retained no submitted fence and successfully evicted its
residency; the renderer nevertheless reported S_OK. This proves an error
propagation defect, not the cause of EXP862 SSH loss.

Inspected sources: `agx_win32_gpuva_batch.c`, `agx_win32_gpuva.c`,
`umd_gpuva_windows.c`, `agx_d3d10_windows.cpp`, `agx_win32_asahi_bo.c`,
`agx_win32_asahi_scene.c`, the native lifecycle/frontend generator
`build-native-asahi-state.py`, and EXP862 pinned `agx_batch-pinned.c` and
`agx_pipe-pinned.c`. Microsoft SubmitCommandCb documents HRESULT failure;
WaitForSynchronizationObjectFromCpuCb and its argument structure document the
separate synchronous fence wait. Hardware, m1n1, Mu, DMA, power and interrupt
ownership are unchanged and not involved in this error-publication fix.

Observed contract: native lifecycle sets `ctx->any_faults` on BatchFinish
rejection. Windows FlushStatus instead checks Backend.Failed and
Runtime.DrawTerminal (plus the legacy batch transaction). GPUVA rejection with
successful rollback sets neither of those. Thus Mesa and Windows disagree
about the same renderer's usability.

Smallest implementation: set Backend.Failed on the existing BatchFinish fail
label, preserving Rejected and the original rollback. Allocation/preparation
failures reaching that label also become terminal for rendering, consistently
with the existing native any_faults rule. Do not set Gpuva.Terminal: that flag
means uncertain ownership and would veto safe release. BatchRelease,
agx_bo_unreference/dispose, Collect and Detach do not veto release merely for
Backend.Failed. Readers that do reject Failed perform new allocation/import,
address capture or rendering, not terminal cleanup.

Verification: execute the real frontend Flush body, BatchFinish, GpuvaSubmit,
parser and FlushStatus in a sanitizer host replay. Inject a rejected submit,
require one frontend error callback and immediate rejected-batch poll; keep
all original residency rollback/private-lease/reference release assertions.
Also exercise preparation failures and successful submissions. Parent owns
full suite, ARM64 /W4 /WX /analyze and implementation commit/CHANGES row.

No hardware checkpoint is needed to establish this software contract. A later
authorized experiment may test whether this changes guest survival; it must
use the existing ordinary Code28 recovery and an exact preregistered package.
No such experiment, package or causality claim is part of this change.
