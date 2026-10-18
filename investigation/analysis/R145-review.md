# R145 independent final review

Reviewed current uncommitted production/test changes against base `79e5b983` and the R142/R145 specification and plan. Review was read-only except this requested report. Preserved m1n1/Mu dirt was excluded. No Air, packaging, source edits, or agents.

## Strengths

- KMD copy requests use acquired device-specific runtime handles, attached process/device/context identity, current `ResidentPtes` allocation and offset provenance, generation checks and full-chunk prevalidation. CPU pointers and PFNs never enter the buffered request. Both logical PTE formats are tested through real paging publication.
- UMD now separates canonical local GPU storage from class0 CPU staging for every native class. Imported original allocations remain borrowed, partial CPU writes preserve the mirrored contents, and GPU-written BOs are downloaded after an internal completion wait and before the published completion fence.
- Failure paths retain copy holds and allocation ownership after uncertain completion/readback/unlock. Shader BO allocation/map failure now propagates through empty-shader creation and the newly fixed compute callers.
- Tests exercise real production decoder/copy/Windows callback bodies and two sequential GPU-write/partial-CPU-write batches, rather than validating source strings alone.

## Issues

### Important — imported primary rotation still compares the canonical handle to the original handle

Files: `drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp:733`; introduced by the new split at `drivers/apple-agx/render-admission/umd/src/umd_win32_screen.c:609` and imported adoption at `:666`.

`AgxD3d10WindowsPresentationRotate` requires `slot->KernelAllocation == r->Resource.KernelAllocation`. R145 gives the slot a newly allocated canonical handle while the presentation resource correctly keeps its original borrowed CPU/display handle in `Resource.KernelAllocation`; that original is now `slot->StagingAllocation`. These handles necessarily differ. Consequently every otherwise valid imported two-primary rotation returns `HRESULT_FROM_WIN32(ERROR_BUSY)` before rotating any identity. This regresses the existing presentation rotation contract.

Repro: create/adopt two presentation resources with original handles101/102 and distinct canonical handles201/202, flush/retire, then call the real `AgxD3d10WindowsPresentationRotate(...,2)`. Its existing identity guard deterministically refuses the first resource. `tests/test_g4_primary_rotation_replay.py` currently seeds one equal handle in both roles and therefore masks the regression.

Fix: compare the borrowed staging handle in the GPUVA profile, preserve the old profile, and extend the real-function rotation replay to use distinct canonical/staging identities while asserting that the canonical/staging/token/resource groups continue rotating together and original handles remain the display handles.

No Critical issue found. No other confirmed Important issue in the inspected diff.

## Unknowns and gates (not defects)

- Native shader/UMD full-translation-unit compile and final host-suite results were still running at review dispatch. The parent must record them before commit. The accepted gate is **no new failure/error names versus base15 failures/38 errors/2 skips**, per the user's correction; this review does not call the full suite green.
- VidMm acceptance of the complete CpuVisible0/local-only/AccessedPhysically shape, local-budget and eviction behavior, hardware coherency/performance of local-view copies, imported CPU publication, AGX completion, and display output need later separately authorized hardware evidence.
- The saved DXGI factory-destruction crash has no proven causal link to the Render backing rejection and is appropriately left unchanged.

## Declined to judge

- Firmware, m1n1/Mu reservation, caps, signer and package behavior: no R145 changes and explicitly outside this offline task.
- Unrelated pre-existing Mesa allocation sites (disk-cache BO rehydration, linked-shader BO allocation, sampler heap) and general OOM completeness: outside the saved executable BO failure propagation repair; no new nullable caller regression identified beyond the already fixed compute case.
- Scheduling/performance/frame correctness on hardware: offline source and replay cannot establish those outcomes.

## Assessment

**Ready to merge: With fixes.** Fix the imported rotation identity regression with a real-function RED/GREEN, then require the pending compile and no-new-failure full-suite evidence. The central local-copy provenance and staging/readback design is consistent with the stated ownership contract, subject to the separately recorded hardware unknowns.

## Narrow recheck — rotation finding resolved

Read the production rotation correction, both profile fixtures, and the retained `R145-offline/rotation-red.log` / `rotation-green.log`. The GPUVA guard now compares `slot->StagingAllocation` with the original presentation handle; the legacy guard keeps `slot->KernelAllocation`. The real-function GPUVA replay assigns canonical201/202 separately from originals101/102, failed at the original rotation guard before the repair, and now passes alongside the legacy case (2 tests). Assertions preserve the canonical/staging pairs and rotate the presentation original, retirement handle, render token, render resource, and bound views consistently.

**Disposition: Important finding ACCEPTED and FIXED.** No remaining finding from this review blocks the offline implementation. This narrow recheck does not broaden or replace the earlier review; final acceptance still requires the controller's stable-tree suite and final translation-unit compile receipts. The reported earlier full suite preserves all53 baseline failure/error names, and the reported native shader/UMD compilation passed; the stable-tree suite and added rotation TU compile were still running when this disposition was requested. Hardware unknowns remain unchanged.
