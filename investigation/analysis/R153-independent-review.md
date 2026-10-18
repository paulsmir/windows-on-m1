# R153 independent offline review

Review date: 2026-09-28. Base: `1b9db94d`. Reviewed the uncommitted production
diff, the split R153 tests and fixtures, and the manager contract report. No Air
access, package build, or source changes were performed during the review.

## Verdict and scope correction

Original verdict: accept the reviewed offline implementation; no confirmed
Important or Critical issue found within the reviewed paths. This was a review
of manager materialization, provider rebind accounting, and Windows ownership
wiring, not proof that the complete staging-to-publication path succeeded.

After this review, the root agent's final integration audit found that
`Exp208DeriveDynamic` still rejected `IncludeInitBm` when sequence was not 1.
Consequently, the actual StageJob path could not stage a manager rebind after
the first job despite the reviewed materializer/provider tests passing. The
root agent reports removing that obsolete guard with StageJob and dynamic
RED-to-GREEN regressions. The root agent also found and fixed the standalone
manager header's missing `apple_agx_state.h` type dependency. These later fixes
were not independently reviewed or rerun by this reviewer. The original
acceptance must not be read as approval of the pre-fix end-to-end path or an
independent verification of the subsequent fixes.

## Findings

No confirmed Important or Critical issue was identified in the reviewed
snapshot, idle-provider, or process-lifetime changes.

The primary references inspected were Asahi
`drivers/gpu/drm/asahi/{buffer.rs,queue/mod.rs,queue/render.rs,workqueue.rs,fw/buffer.rs}`
in the local Asahi reference checkout and m1n1
`proxyclient/m1n1/agx/{context.py,render.py,__init__.py}`. Asahi's Queue owns a
VM-backed Buffer with persistent Info/control/counter/stats, while scenes and
hardware-slot occupancy have separate lifetimes. InitBuffer follows slot
rebind or growth independently of workqueue creation.

The reviewed implementation restores the four manager objects, refreshes
Scene13 each submission, reapplies Info/Scene relocations, and preserves queue
and event objects. Owner, generation, backing and root distinguish binding
transitions; root changes require rebind without discarding the saved manager
state. BeginJob associates the snapshot after exact private-scene and graph
admission. The queued/started scene and graph prevent reclamation until joined
completion and reporting; image reconstruction clears the temporary pointer.

Save is ordered after both TA and 3D completion and CPU synchronization, before
GraphEndJob releases the lease. Failed synchronization leaves pending state
intact. Existing started-scene cancellation and reset remain fail-closed. The
provider invalidates only BufferManagerInitialized, accounts for the additional
TA entry, and preserves ring positions and first-run flags. Inspection of the
Normal/WC CPU mapping, barrier synchronization and m1n1 Shared object mappings
found no evidence requiring an additional cache-maintenance operation here.

Minor, nonblocking coverage improvement: `tests/fixtures/r153_manager.inc:24`
mutates Info and BlockControl for A-to-B-to-A, but does not mutate Stats or
explicitly compare both queues' stamp/pointer objects. Distinct Stats and
queue/stamp sentinels would strengthen the regression. The reviewed production
copy list includes Stats and excludes queue/stamp objects; no defect was shown
by this observation.

## Independently executed checks

- Both `test_r153_manager*.py` tests passed with
  `CC=/tmp/agx-clang-wrapper`, AddressSanitizer and UndefinedBehaviorSanitizer.
- `test_apple_agx_g13_queue_provider.py` passed with the same compiler wrapper.
- `git diff --check` was clean.

These checks preceded the subsequent integration fixes described above.

## Declined to judge

- Whether this correction resolves EXP866's firmware timeout, produces pixels,
  or meets the 600-second DWM checkpoint.
- ARM64 compiler results and full-suite baseline equivalence beyond results
  supplied by the root agent; those gates were not independently rerun.
- Firmware-internal asynchronous behavior beyond the observable completion
  and slot-retirement contract.
- General mixed legacy/private-G4 manager switching, sustained counter wrap,
  or successful recovery from a real GPU hang.
- End-to-end StageJob rebind admission, which was not exercised by this
  review and was subsequently found defective by the root integration audit.
- The later staging-guard and standalone-header corrections themselves;
  this persisted report records the completed review, not a fresh review.
