# RC43 — EXP1969 submit failure and bounded stream validation

Offline task base `362f05d14a5c6ccce645480984b329ddc363763b`,
branch `codex/rc43`. The handoff owns the scope: source, regression tests and
commits only. Integration owns package/hardware work and the experiment/change
ledgers. Neither indirect-stream default selection nor rc42 profiling changes.

## Evidence: the stream was not proved to be rejected

Saved evidence root:
`/Volumes/pwdev/public_windows/.local/experiments/EXP1969-no-wait/evidence`.
Derived evidence and verification logs are in shared `handoff/codex/rc43`.

- `crash-b/102626-13906-01.dmp` has an ARM64 `PAGEDU64` header with bugcheck
  **0x119**, arguments **2, ffffffff80000011, fffff60455b04a50,
  ffff8c0d2f093170**. SHA-256:
  `80f4863351b221d5f6e7a396dfc003391d300c38113f7613fe1a217585807d6c`.
  `dump-header.json` records header-only extraction; no debugger was run.
  P1=2 is failed command submission, consistent with the returned
  `STATUS_DEVICE_BUSY` in P2. Microsoft's public bugcheck page defines P1;
  it does not identify the objects in P3/P4. Those pointers are not assigned
  guessed types or used as a caller attribution.
  [Microsoft 0x119 documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0x119---video-scheduler-internal-error).
- `rec1969h/kmd-receipts-pre-disable.txt:189`: one firmware **Timeout**, fence
  **0x6879**, PID **3396**, **30 ms** after submission, halt count 1 and one
  resume. FAULT_INFO **0x469faae3197cc694** was read but VALID is clear; zero
  valid faults. There is no valid address/unit/access to attribute to an output
  BO, USC window or command fetch. `decoded-1969.json` uses the repository decoder.
  No `Wom1G4SubmitFailure` or bind-refusal record is present in this snapshot.
- `crash-b/gm1969b-umd.log:9778,9786,9791,9796,9801`: five accepted command
  **recordings**, kind 2/indexed, MaxCount 2400, with count buffer. The other
  42 indirect recordings are kind 3/dispatch, MaxCount 1, without a count.
  **No uncounted multi-record draw was recorded.** There is no successful
  `measure-d3d12-counted-path` row, but that does not prove no counted draw ran.
  `d3d12/agx_d3d12_ddi.cpp:5936` emits at record time;
  `mesa/winsys/agx_d3d12_windows.cpp:3974,4001,4010` calls draw_vbo, returns on
  failure, and only then emits that success receipt.
- The final log rows are texture copies, three BO creations and successful
  `measure-touch` at line **14411**. `render-admission/umd/src/umd_gpuva_windows.c:975-982`
  emits touch after submission, signal and wait. This proves that touch finished;
  it does **not** identify the next failing submission as another touch.
- `crash-b/gm1969b-out.txt` reaches “Starting ... Benchmark” at 15.579 s and
  “Resizing ... frame” at 15.583 s. The failure is at initial rendering, not
  proved to be before the benchmark began. `EXP1969/full.log` ends in the guest
  NVMe/PSCI reset; the later host serial exception follows reset.

`source-comparison.json` proves byte identity between the saved `source.zip`
and task base for the virtual-submit path, CPU queue, backend, G4 parser,
scheduler and stream emitter/projection. The hardware manifest identifies source
`04e4637a967e56443759a98e2b565a201500af48`, SYS
`4992bf4284964b90b868fffafbe4f240ce8e2298788b77b9e2480c59fe9abafc`,
m1n1 macho `452ab29fd81b10dc026605ea6f64830dd1963a1ec2de299dbd1eb3fe97a171fd`
and Mu `1c7d87d6fd4321feee605b5f9440dab6a350bc7a3964fb2c62b34602f565f3e5`.
The EXP1969 BEFORE ledger has a stale short m1n1 label; the manifest hashes and
explicit 075e3980 launch selection are the comparison, not that short label.

## Confirmed KMD contract defect and fix

`render-admission/src/work_queue_windows.c:34-48` returns DEVICE_BUSY for a
faulted/stopping scheduler, full queue, or refused fence order/preemption.
`gpuva_g3_windows.c:4857` returned it directly through the residency-touch path.
Virtual paging at `:3512` also preserved the CPU queue status. The final wrapper
previously returned every inner result unchanged. These are deterministic leaks.
The firmware-timeout-to-faulted-scheduler-to-busy chain is consistent with the
saved evidence, but the exact crashing subpath and busy predicate remain unknown.

The virtual-submit DDI permits only SUCCESS or INVALID_PARAMETER; the latter
puts the calling device in error and requires its rejected fence to become the
completed boundary after preceding work finishes. Physical SubmitCommand has
no analogous error return: validation belongs in Render before admission.
[Microsoft virtual-submit contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_submitcommandvirtual).
This change is scoped to the GPUVA virtual callback used by the workload.

The fix at `gpuva_g3_windows.c:4970` enforces exact status equality rather than
NT_SUCCESS, retains raw unexpected status in the existing first-failure record
(branch 16/downstream status), and records rejection under SchedulerLock. It
neither reports successful execution nor adds a fake DMA interrupt. A valid
packet refused by terminal internal state now errors the device instead of
returning an OS-fatal status; no unsupported retry protocol is introduced.

`shared/src/apple_agx_scheduler.c:175` attaches each rejected fence to the newest
accepted predecessor. Consecutive refusals coalesce in existing fixed queue
storage, including when all 64 accepted slots are full. Idle refusal advances
immediately; active/queued/preempted predecessors retain ownership. Old rejected
paging resubmissions carry their dependent tail to the prior accepted packet.

- Queue activation transfers the rejected tail at `:279`.
- Genuine success and known-fence fault complete through it at `:288,316`,
  while the backend still notifies/releases only the actual packet.
- Paging resubmission and repeated preemption retain both arrays at `:256,367`.
- The wrap-aware retired boundary at `:20` no longer prefers an older abort
  over a newer completed rejection. Preemption observation at `:446` captures
  the advanced boundary. A claimed notification snapshot remains immutable.
- Reset at `:532` retains queued paging dependencies and reports the actual
  aborted packet. Only a rejected software tail can advance there. This uses
  the existing **physical-quiescence precondition** at
  `render-admission/src/scheduler_windows.c:573,597` and
  `backend_platform_windows.c:3844,3862`; no-tail reset semantics are unchanged.

The original firmware fault completion gate is unchanged:
`backend_platform_windows.c:3431-3436` requires real completion and graph-job
release before `:3474` advances the faulted fence. Notification at `:3291`
remains DMA_PAGE_FAULTED. Local WDK26100 `d3dkmddi.h:1063-1078` defines the known
fence and permits VA zero for another GPU error. Merely refusing a submission
is not a basis for inventing a GPU page fault. Existing timeout/recovery failures
whose firmware queues never retire remain separate defects.

## UMD bounds and source-supported discriminator

The G4 parser validates the envelope, mapped addresses, RC40 timestamp target,
and stream endpoints; it does not execute or inspect the future GPU-generated
VDM. Its refusals map to INVALID_PARAMETER, not DEVICE_BUSY
(`shared/src/apple_agx_g4_submit.c:18,83,203,309`;
`render-admission/src/gpuva_g3_windows.c:4756`). There is no source proof here
of a parser/continuation/USC-window rejection causing EXP1969.

The stream emitter did lack a complete output-range proof. New
`mesa/winsys/agx_win32_indirect_stream_emit.h:73,142,182` checks BO backing and
VA reservation extents, aligned output addresses, the full VDM padding and USC
clone allocation, the 39-bit Windows process range, and continuation space before
any generator launch/link. The existing 32-bit USC execution-window guard stays.
Failures mark the UMD context; the owner propagates DEVICE_HUNG at
`agx_d3d12_windows.cpp:3979`, before its successful-draw receipt.

Primary sources inspected:

- `.local/reference/mesa/src/gallium/drivers/asahi/agx_state.c:3787-3823`:
  link plus 0x800 mapped fetch padding, 256-byte chunk allocation.
- `src/asahi/genxml/cmdbuf.xml:915-925`: ordinary VDM links carry 40-bit targets;
  Windows G4's `valid_va` at shared parser `:18` limits process mappings to 39.
- `src/asahi/lib/pool.c:60-91`, `agx_bo.h:50-83`: allocation and BO/VA extents.
  Windows populates both extents at `agx_win32_asahi_bo.c:480,501`; GPU_ONLY
  creates writable General storage and LOW_VA reserves the USC window at `:406-422`.
- RC38's current-chunk projection, ordinary continuation link, BO references,
  full CDM barrier and render dependency are retained. No register/cache-bit,
  output-pool permission, firmware or residency-placement guess is added.

These are defensive producer-contract fixes, not a proved hardware timeout cause.
The new steps in `windows/icd/agx_d3d12_probe.c:1092,1148,1198,1504,7683` extend
RC38's probe with **R16/R32 × count 0/1/2**, MaxCount 2, followed by one ordinary
indexed draw. The UAV count must be final coverage multiplied by count+1; the
image and counter both affect process exit. Count storage is outside both records.
Steps are named-only; the default suite and default CPU count-bounded fallback
remain unchanged. No new external UMD C/C++ linkage is introduced.

## Ownership, verification and next checkpoint

UMD/Asahi owns encoding, input hazards, output bounds and BO retention. KMD/VidMm
owns residency, scheduler admission and ordered completion; firmware executes.
m1n1/Mu retain initialization, interrupt/DMA/power and launch ownership. Their
submodule directories are empty in this worktree; no other-worktree access or
network fetch substitutes for missing current source. No live state was read.
This task makes no firmware-dependent behavioral change.

RED/GREEN logs: `kmd-red.log`, `umd-red.log`, `probe-red.log`; production-code
replays now pass. `targeted-final.log`: **64 tests, OK**, including 132 backend
completion combinations, real scheduler/submit code, ASan/UBSan stream emission
and kernel, complete projected native translation units and the probe fragments.
The full baseline result and commit/file hashes are recorded in REPORT.md.

An extra legacy work-queue replay cannot compile its stale WDK shim (missing
LONGLONG, QPC and several context fields). `work-queue-base-failure.log` reproduces
that error with the original 362f05d1 scheduler; it is not added to known failures.
The competing-preemption test passes from repository root with PYTHONPATH=.; its
initial import error from tests/ is recorded. No native Windows build was run.

**WHY THIS HYPOTHESIS:** the same process has a real firmware timeout with no
valid memory fault, five recorded counted indexed calls, and no post-draw success
receipt. A bounded zero-count continuation bypasses per-record USC/index fetch;
then one/two records distinguish generated-draw consumption and multiplicity.
Source alone cannot select cache visibility, generation or runtime residency.

Hardware owner only, not executed or preregistered here:

1. Native matching KMD and ARM64/x64/WOW64 compile/link/stack gates; preserve the
   established GPU-visible recovery artifact, record exact package hashes and
   keep EXP1969's expanded RAM, full pool, m1n1/Mu and launch settings.
2. Fresh-process fallback controls with stream=0, then stream=1. Start with
   `only RC38Z`; require image and `_COUNT` PASS, path3/reason0, and ordinary
   follow-up completion. Next `only RC43I16Z` and `only RC43I32Z` separately.
3. If zero passes, run `RC43I16O`, `RC43I16N`, `RC43I32O`, `RC43I32N` separately.
   Require correct pixels and counters, path3/reason0 and no device/DWM error,
   timeout, fault or bugcheck. These cover 0/1/N at N=2; higher-count and
   parameter-sensitive semantics still need the existing RC33/36 checkpoints.
4. Only after those gates, GravityMark 2k with stream=1, then 10k. Collect
   receipts/logs/dumps before the established exact-package cleanup. A remaining
   timeout is a failed checkpoint even if the 0x119/2 is now contained.

WHAT IS STILL UNKNOWN: exact crashing DDI/busy predicate; the original firmware
Timeout cause; actual GPU-written VDM/USC visibility, pixels/statistics and
high-count performance; native Windows compilation; and recovery when queue
ownership cannot return. These changes do not establish stream safety on G13.
