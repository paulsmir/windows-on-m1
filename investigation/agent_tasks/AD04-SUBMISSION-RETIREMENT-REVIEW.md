# Submission lifetime review — 2026-09-13

Reviewed 452ce35 on clean ledger HEAD 2144155, restricted to submission holds,
callback disposition, timeout and retirement. Existing architecture retained.

## Findings and decisions

- SubmissionHolds retain UMD allocation identity; they are not residency, a CPU
  immutable snapshot, a native Mesa BO reference or display retirement.
- Runtime callbacks run outside the owner lock. Current context transaction
  excludes reentrant dispatch/retirement and resource invalidation. Accepted
  Render cannot replay after returned-buffer/event failures.
- Timeout correctly restores the accepted/post-error phase and keeps holds.
- Confirmed defect: missing fence/event returned false without updating
  LastScreenError. Retirement could return stale S_OK with live holds. Regression
  failed on x64 in native-evidence-009, then passed after explicit E_INVALIDARG.
- Tightened callback disposition: after entering pfnRenderCb, a failing HRESULT
  alone is not used as evidence that source consumers never ran. Keep holds,
  make further draw admission terminal, and synchronize via the same context
  marker. Errors before dispatch and explicit pre-dispatch abort release at once.
- A recovered marker permits ownership retirement, not an AGX execution PASS.
  Rendering verdict still requires device status and physical completion receipts.
- If both marker recovery and runtime context operation fail, holds remain.
  Production enablement requires the separately verified terminal teardown path;
  it must prove GPU quiescence before disposing native/runtime source storage.

Signal contract inspected: Microsoft D3DDDICB_SIGNALFLAGS, EnqueueCpuEvent
requires ObjectCount=0 and a valid event, with SignalAtSubmission left zero:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_d3dddicb_signalflags

## Verification

Current x64 WDK26100/MSVC14.44 composer test execution PASS. The same real KMD
validator/materializer callback is exercised before an injected E_FAIL, and
retirement now requires the ordered marker for that case too. Missing-event
failure explicitly preserves the accepted phase and all eight allocation holds.
No hardware run. Internal producer integration may continue; production/hardware
enablement is still subject to native BO lifetime, terminal teardown and ordinary
package/preflight gates. This is not a fundamental blocker or a mission stop.
