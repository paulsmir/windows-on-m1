# Q23: opt-in compute event units after EXP2118

The source correction is behind effective `Wom1Depth2Prelude`, still default off.
It makes all compute users allocate queue-local firmware stamps in 0x100 units,
independent of Windows submission fences. It is a hardware candidate: neither
the dump nor host tests prove that this caused or fixes the firmware assertion.

## Dump evidence and causal decision

Paths below are relative to the repository, except debugger artifacts under
`.local/handoff/codex/q23-prelude-crash-stamps/` in the shared repository root.

- `cdb-output-1.txt:432`, `:497`: head RP fence 0x1829 expects TA/3D/compute
  0x7a0a5d00/0x3d0a5d00/0x1829; successor C fence 0x182a expects compute
  0x182a. Slots are 0/1/2 and 2 respectively.
- `:1191` reads actual compute ring slots 0x25/26/27: prelude work
  ffffffa0004a0000, successor barrier ffffffa0004a2820, successor work
  ffffffa0004a2000. The failed tail expression at :1151 does not invalidate
  these successful independent reads. No second debugger round was needed.
- `:1027`, `:1049`, `:1076`: observed firmware stamps remain at the prior
  TA/3D/compute values 0x7a0a5c00/0x3d0a5c00/0x1820. Driver copies agree.
  Compute done=0x25 and read=write=0x28 (`:1173`).
- `:1562`, `:1579`: TA waits on compute(0x1829), slot 2; successor compute
  waits on 3D(0x3d0a5d00), slot 1, with self stamp 0x182a. The graph is
  compute(P) -> TA(P) -> 3D(P) -> compute(S). The dump shows distinct A/B
  command storage and matching work/finalize endpoints, not an overwrite,
  raw-value collision, or dependency cycle.
- `:923` and `:939`: both notifier flights are published and visible. Current
  count 0x14d8 and the four-byte threshold 0x14dc (`:1115`, `:1128`) match
  three RP notifications plus one C notification. The following word 0x8c
  lies beyond the threshold object; it is not evidence of a 64-bit count.
- RTKit Crashed=1 (`:459`, `:534`). No Windows crashlog mapping exists
  (`:1601`), so the assertion text remains unknown.

Asahi primary sources under `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi`:
`event.rs:58-85` advances event values by 0x100 and shifts signed deltas by 8;
`workqueue.rs:309-311,373-386` advances and commits queue events;
`queue/mod.rs:652-659,787-813,839-872` constructs barriers from source endpoints
and records the next endpoints; `queue/compute.rs:215,339` emits them in finalize
and work metadata. Shared event/stamp ownership is intentional. The forward
reference delta from raw 0x1829 to 0x182a is zero. Applying that arithmetic to
proprietary firmware is a hypothesis, not a measured firmware implementation.

Before this change, platform `backend_platform_windows.c:3299,3543,3584` supplies
raw fences. The original shared builder copied them into firmware work and
microsequences. A blanket input shift would also change notifier deduplication
and sidecar ownership, which must remain tied to the original fence.

## Implementation and ownership

- `shared/src/apple_agx_render_shared_memory.c:807`: choose one queue event
  for each successfully built compute command; reuse it for same-lifetime
  duplicates. Skip zero at wrap because existing command/barrier validators
  reserve zero. Do not derive it by shifting or truncating the Windows fence.
- `:515`, `:546`: notifier-flight snapshots restore the event on cancellation.
  Visible commands retain their allocation. `:754` resets the sequence only
  on compute-queue initialization; a zero sequence invalidates old dedup state.
- Return `Output.StampValue`; use it for both work and FinalizeCompute.
  The three Windows build consumers record that output in expected stamps;
  both leading compute barriers use it as self stamp, and TA waits on it
  (`backend_platform_windows.c:3338,3345,3388,3553,3614,3623`).
- `backend_platform_windows.c:7373` selects the mode from effective
  `Depth2Prelude` at startup. Raw input identities remain in counters,
  EventSequence, JobWordStamp, NotifierComputeStamp, flights and completions.

KMD owns allocation/publication, DMA/job lifetime, interrupt completion and
recovery; firmware owns execution and physical retirement. Mesa selects prelude
work. m1n1/Mu initialization, power, memory, ACPI and launch ownership are
unchanged. Their submodules are absent in this offline worktree; EXP2118 and
Q14/Q15 remain the supplied launch contracts, with no new live-state claim.
No external implementation was copied. Microsoft requires the original completed
submission's fence in `DmaCompleted.SubmissionFenceId`; this change preserves it.
See the [official interrupt-data contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkargcb_notify_interrupt_data)
and pinned WDK `d3dkmddi.h:1010`.

## Verification and limits

`tests/test_prelude_compute_stamps.py` executes the real shared builder, extracted
Windows barrier functions and real queue runtime. RED reproduced raw firmware
stamps; GREEN covers the EXP2118 RP/C ring positions, work/finalize/barrier
agreement, separate storage, threshold arithmetic, event-plus-stamp-plus-done
retirement, original completion fences, cancellation/retry, duplicates, wrap,
queue restart with reused fence, and a fence above the 24-bit boundary. It also
compiles all Windows endpoint assignments and startup mode selection.

The shared task folder retains RED/GREEN logs, default-off byte equivalence
against base 191d7d92, review disposition, affected-test output and the full
baseline output. No hardware, guest, package operation or native WDK build was
performed. The changed C uses existing kernel-safe types, named fields and
32-bit arithmetic; source audit does not substitute for native /W4 /WX.

Final verification: `Q22_M1N1_SOURCE=/Volumes/pwdev/public_windows/.worktrees/m1n1-q22
python3 run_against_baseline.py` from `tests/` reports:

```
ran 859 tests in 343 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The source override is the existing Q22 fixture prerequisite (clean m1n1 source
commit 7cce5a0e8427d30b5ad9337a2098f27f1ab86ea4), not a baseline exception.
The affected suite separately passes 37/38 tests; the reset-order source test
`test_render_job_depth2_kick_rc102.KickWiring.test_reset_waits_for_and_aborts_the_kicked_job`
fails identically against the base. Its proof is in `inherited-failure.txt`.
The new regression passes both tests; its final raw-fence mutation fails as
expected. Independent review's restart edge was reproduced and fixed. The
implementation commit is e4739a48; no native or hardware validation is claimed.

## Smallest hardware checkpoint — integration owner only

WHY THIS HYPOTHESIS:
1. EXP2100 and EXP2118 retain RP/C with adjacent raw compute event values;
   primary Asahi sources instead use queue event units of 0x100.
2. EXP2118 now confirms the actual ring order and distinct command storage,
   reducing the wrong-order/overwrite alternatives.
3. The observed notifier delta is exactly the expected four notifications.

After merge review and the native WDK gate, use the existing bounded Q3PC
checkpoint for actual RP/C overlap, pixels, compute output and IA statistics.
Then run GravityMark HD 1280x720, 100k asteroids, temporal 0, windowed, with
Wom1Depth2Prelude=1. Require completion without RTKit crash, 0x116, device removal
or incorrect output; compare score to the same-package default-off control
(reference about 5980). Require aligned, increasing compute endpoints in receipts.
If the crash persists, reject this causal hypothesis and inspect the new failure;
do not declare the source correction hardware validated. Preserve the pinned
GPU-visible recovery artifact and collect evidence before exact package cleanup.
