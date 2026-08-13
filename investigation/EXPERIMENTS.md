# Hardware Experiment Ledger

This append-only ledger is the source of truth for J313 hardware experiments.
Chat transcripts are not a substitute. Every run is entered before launch and
completed after observation.

## Entry template

```text
### EXP-YYYYMMDD-NNN — short title

Status: planned | running | confirmed | rejected | inconclusive | superseded
Created (UTC): YYYY-MM-DDTHH:MM:SSZ
Completed (UTC): YYYY-MM-DDTHH:MM:SSZ

Hypothesis:
Single changed variable:

Source contract:
- Repository / branch:
- Root commit / diff SHA-256 / dirty:
- m1n1 commit / diff SHA-256 / dirty:
- Mu commit / dirty:

Artifact:
- Build command:
- Path:
- Profile:
- SHA-256:
- Recovery artifact:

Run contract:
- Install/launch command:
- Expected checkpoint:
- Failure criterion:
- Evidence paths:

Observed result:
- Boot timing/phases:
- CPU/timer/interrupt evidence:
- Windows stop code:
- Display/input/SSH/RDP/storage:

Verdict:
Next experiment:
```

## Experiments

### EXP-20260813-001 — first passive-monitor hot-path gating run

Status: rejected
Created (UTC): 2026-08-13
Completed (UTC): 2026-08-13

Hypothesis: keeping lock-free monitor snapshots while suppressing most synchronous
diagnostic output would retain observability without disturbing Windows timer
progress.

Single changed variable: the monitor build gated runtime trace output, but this
first artifact still emitted bounded SGI queue messages and was therefore not yet
fully passive.

Source contract:
- Repository / branch: `/Users/pavel/public_windows`, `codex/canonical-public-release`
- Exact source hashes were not preserved in the earlier run metadata; this omission
  is the reason the mandatory ledger rule now exists.

Observed result:
- All eight guest CPUs came online.
- SSH did not become available within 120 seconds.
- The artifact emitted 32 `HV SGI QUEUE` messages.
- Windows stopped with `CLOCK_WATCHDOG_TIMEOUT (0x101)`.
- Bugcheck parameters identified CPU4, the first performance core, as the stalled
  processor: `P1=0x18`, `P2=0`, `P4=0x4`.
- CPU4-CPU6 snapshots contained virtual timer LR state active+pending, including
  `LR1=0x9020020000000012` for INTID 18. CPU4's timer was overdue and its last
  IAR/EOI progress was older than the continuing efficient-core activity.
- Evidence was captured in the assisted-run `hv.log`; the log is append-only and
  must be sliced by byte offset for future comparisons.

Verdict: rejected. The run proves a real P-cluster virtual-timer delivery stall,
but it cannot isolate timer policy from the remaining synchronous SGI logging.

Next experiment: run EXP-20260813-002 with every known synchronous SGI,
timer-rate, spurious-IAR, and PCI configuration trace gated off while preserving
the monitor snapshots. Do not change heartbeat frequency in the same run.

### EXP-20260813-002 — fully passive monitor build

Status: rejected
Created (UTC): 2026-08-13
Completed (UTC): 2026-08-13

Hypothesis: the remaining synchronous hot-path trace output causes or materially
amplifies the P-core timer stall; a fully passive monitor should boot Windows while
retaining post-failure snapshots.

Single changed variable: `HV_RUNTIME_TRACE` and the remaining SGI queue,
timer-rate, and spurious-IAR prints require verbose diagnostics. Timer heartbeat
policy is intentionally unchanged.

Source contract:
- Repository / branch: `/Users/pavel/public_windows`, `codex/canonical-public-release`
- Root commit: `824410251e03b8a112d6b187d8b7dbe3ef1b3388`
- Root diff SHA-256: `d47b954fd051f5114659b63e4413d874fe2b4314fa0dadeb73281278dcc2b81a`
- m1n1 commit: `6dc04339570d272e305b5c94d1ba57ee84d35497`
- m1n1 diff SHA-256: `fa186a43523dcd94d6103668a462eba70a6ac8fa2987de401643862823df52d2`
- Mu commit: `63942398cccbd98127cfecbd7f936af99c837d6f` (clean)

Artifact:
- Build command: `STANDALONE_SKIP_MU=1 ./scripts/build-standalone.sh --debug-build --display physical --debug monitor --apple-input on`
- Path: `/Users/pavel/public_windows/dist/j313/debug-monitor/boot.bin`
- Assisted m1n1: `/Users/pavel/public_windows/dist/j313/debug-monitor/m1n1.macho`
- Guest firmware: `/Users/pavel/public_windows/dist/j313/debug-monitor/J313_EFI.fd`
- Profile: J313, physical display, monitor diagnostics, 8 CPUs, Apple input exposed
- `boot.bin` SHA-256: `7a23a385cb781c1e59036e7b96a81b1ea022311c0d7e82b916e962098a3a855f`
- `m1n1.macho` SHA-256: `aa0334f895b1733b1bc798a736b959aefe8a5277324445a7ebcfb0f7860c38e2`
- `J313_EFI.fd` SHA-256: `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`
- Recovery artifact: the previously hardware-validated production image must remain
  installed or locally available before any standalone installation. This run is
  assisted first and does not replace the ESP image.

Run contract:
- Launch command: `./scripts/run-assisted.sh --proxy
  /dev/cu.usbmodemC02HDNCCQ6L41 --vuart /dev/cu.usbmodemC02HDNCCQ6L43
  --chainload --m1n1 dist/j313/debug-monitor/m1n1.macho --firmware
  dist/j313/debug-monitor/J313_EFI.fd --display physical --debug monitor
  --foreground`.
- Expected checkpoint: all 8 CPUs online, no synchronous PCI/SGI/TIMERRATE trace,
  Windows login and SSH reachable without `0x101`.
- Failure criterion: frozen Windows spinner, missing SSH after 120 seconds, any
  watchdog bugcheck, or recurring active+pending overdue P-core timer state.
- Evidence: `/Users/pavel/public_windows/hv.log`, UART output, Windows minidump if
  produced, and elapsed time to SSH. The public runner truncates `hv.log` when it
  starts, so this experiment owns the complete new file. The initially recorded
  byte offset `118920` applied to the pre-launch file and must not be used.

Observed result:
- The public runner validated the J313 artifacts and launch contract, then
  truncated `hv.log`; the complete run is therefore in that file rather than an
  append-only byte slice.
- Mu reached `bootmgfw.efi`; all eight guest CPUs entered Windows.
- Synchronous trace counts were all zero: `PCI cfg=0`, `HV SGI QUEUE=0`, and
  `TIMERRATE=0`.
- SSH was still unavailable after 109 seconds of probing. No reset or bugcheck had
  occurred at that point.
- A non-destructive Ctrl-C monitor snapshot showed CPU4 and CPU6 with virtual timer
  INTID 18 active+pending (`LR0=0x9020020000000012`), masked timer routing, and
  deeply overdue virtual deadlines. CPU2, CPU3, and CPU5 had current timer progress;
  CPU4 and CPU6 had stale IAR/EOI progress. The guest continued after the snapshot.
- Evidence copies: `/tmp/exp-20260813-002-hv.log` and
  `/tmp/exp-20260813-002-hv-after-snapshot.log`; canonical live log:
  `/Users/pavel/public_windows/hv.log`.

Verdict: rejected. Removing synchronous trace output did not eliminate the boot
stall. It strengthens the conclusion that the P-cluster virtual-timer recovery path
is the root defect; trace overhead was secondary.

Next experiment: increase only the secondary recovery heartbeat from 100 Hz to
1000 Hz, add/update its regression test first, and repeat the same assisted monitor
launch. Do not change LR state transitions in the same experiment.

### NOTE-20260813-001 — host-test invocation correction

The Python vGIC trace contract completed successfully (`8/8`). A subsequent direct
`make` invocation in the nested test directory accidentally used the macOS hosted
clang environment and failed on freestanding `uintptr_t`, `ptrdiff_t`, and `ssize_t`
typedef collisions. This was an invalid test invocation, not a runtime-diag source
failure. Future full nested tests must use
`m1n1_windows/tests/run_host_tests.sh`; do not invoke an assumed Makefile target.

Correction: the script itself resolves source paths relative to the nested m1n1
repository root. The exact valid invocation is `cd m1n1_windows &&
./tests/run_host_tests.sh`. Running it from the public repository root also fails
with missing relative source paths.

### EXP-20260813-003 — 1 ms secondary recovery heartbeat

Status: confirmed
Created (UTC): 2026-08-13T17:12:20Z
Completed (UTC): 2026-08-13

Hypothesis: on T8103 without ECV, a 100 Hz secondary EL2 housekeeping heartbeat
does not recover a masked physical timer route and active+pending guest timer LR
quickly enough. A 1000 Hz heartbeat will bound recovery to 1 ms and prevent the
observed CPU4/CPU6 stalls without restoring the old 5000 Hz all-core overhead.

Single changed variable: `HV_FALLBACK_SECONDARY_TICK_RATE`, from 100 Hz to 1000 Hz.
No LR transition, vGIC, SGI, display, NVMe, Mu, or Windows setting changes are part
of this experiment.

Source contract:
- Repository / branch: `/Users/pavel/public_windows`, `codex/canonical-public-release`
- Root commit: `824410251e03b8a112d6b187d8b7dbe3ef1b3388`
- Root diff SHA-256 at artifact capture:
  `e3328a4124411d285bab793aded132ed914125869f49280a67fca0211456052b`
- m1n1 commit: `6dc04339570d272e305b5c94d1ba57ee84d35497`
- m1n1 diff SHA-256 at artifact capture:
  `5f4fc2fd34af6ac65fdd33d3c8c1b7b26e57b424142f8fb3d98b81fee54741a3`
- Mu commit: `63942398cccbd98127cfecbd7f936af99c837d6f`; Mu source is unchanged by
  this experiment.

Regression evidence:
- RED: the full nested harness passed all preceding tests and stopped at
  `hv_tick_policy_test` asserting that the no-ECV secondary rate must be 1000 Hz.
- GREEN: after changing the policy, the complete valid invocation
  `cd m1n1_windows && ./tests/run_host_tests.sh` passed.

Artifact:
- Build command: `cd m1n1_windows && PATH="$(brew --prefix rustup)/bin:$PATH"
  make -j8`.
- m1n1 path: `/Users/pavel/public_windows/investigation/artifacts/EXP-20260813-003/m1n1.macho`
- m1n1 SHA-256: `7e30162ea81247fe79940f0d8074679da3867db154824b62d317614b32986da2`
- Mu path: `/Users/pavel/public_windows/investigation/artifacts/EXP-20260813-003/J313_EFI.fd`
- Mu SHA-256: `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`
- Profile: assisted J313, physical display, passive monitor diagnostics, 8 CPUs,
  Apple input exposed but its Windows driver not installed.
- Recovery: assisted launch only; the ESP standalone image is not replaced.

Run contract:
- Launch: public `scripts/run-assisted.sh` with `--chainload`, the two recorded
  artifacts above, `--display physical --debug monitor --foreground`.
- Expected checkpoint: log reports `boot=1000Hz secondary=1000Hz`, all eight CPUs
  enter Windows, login and SSH become available, and repeated snapshots contain no
  deeply overdue P-core timer LR.
- Failure criterion: frozen spinner/login, SSH unavailable after 120 seconds,
  `CLOCK_WATCHDOG_TIMEOUT`, or stale active+pending timer state on CPU4-CPU7.
- Evidence: newly truncated `/Users/pavel/public_windows/hv.log`, UART, elapsed SSH
  time, and Windows dump if one is produced.

Observed result:
- Artifact preflight and manifest verification passed. Runtime reported
  `boot=1000Hz secondary=1000Hz`; all eight CPUs entered Windows.
- SSH was not reachable at the initial 143-second boundary but appeared during the
  immediate follow-up, approximately 2.5-3 minutes after handoff. This is too slow
  for a performance success criterion but differs from the unrecoverable EXP-002
  stall.
- The post-boot snapshot showed no active+pending timer LR on any CPU: every LR on
  CPU1-CPU7 was zero, timer mode was unmasked/idle, and queue/drain/IAR/EOI counters
  converged. CPU4 and CPU6 no longer reproduced the stale deadline seen in EXP-002.
- Windows reported 8 processors. Five fresh SSH command latencies were 3597.9,
  523.8, 611.4, 593.4, and 3121.8 ms.
- No bugcheck or reset occurred and synchronous trace counts remained zero.
- Preserved log: `investigation/artifacts/EXP-20260813-003/hv.log`, SHA-256
  `b6ee38455b2b13d586d6f5af97be8158c44a0b023d8204767381a588cb343a24`.

Verdict: confirmed for the narrow timer-recovery hypothesis. A 1 ms secondary
heartbeat removes the reproduced active+pending P-core timer stall. It does not yet
prove acceptable boot time, latency, stress stability, or overall smoothness.

Next experiment: a separately recorded short all-core CPU load followed by another
lock-free timer/vGIC snapshot. Do not change code between the idle and load samples.

### EXP-20260813-004 — short all-core load on the 1 ms heartbeat build

Status: planned
Created (UTC): 2026-08-13
Completed (UTC): pending

Hypothesis: EXP-003 will keep all eight CPUs and their virtual timers progressing
during a bounded 10-second all-core load without `CLOCK_WATCHDOG_TIMEOUT` or an
active+pending overdue timer LR.

Single changed variable: runtime CPU load only; source, binaries, Mu, display,
diagnostics, and heartbeat are identical to EXP-003.

Artifact and source contract: exactly EXP-20260813-003. No rebuild. Use the recorded
`m1n1.macho` SHA-256 `7e30162e...86da2` and its preserved manifest.

Run contract:
- Start eight bounded PowerShell worker jobs over SSH for 10 seconds.
- Verify command completion and SSH availability.
- Immediately capture a non-destructive Ctrl-C monitor snapshot.
- Failure criterion: command timeout, lost SSH, reset/bugcheck, non-converged queues,
  or overdue active+pending virtual timer on any CPU.
- Evidence: the EXP-003 live `hv.log`, copied after the load to the EXP-004 artifact
  directory, plus the SSH duration and result.

Observed result: `kd_liveness.py` sent break-in but received no `STATE_CHANGE64`
within its timeout. Its `finally` path sent KD Continue. No guest reboot or source
change was performed.

Verdict: inconclusive. The virtual UART/KD transport was unavailable in this state;
this does not prove that the Windows kernel stopped, especially because lock-free
vCPU counters had advanced between the preceding snapshots.

Next experiment: resolve the repeated sampled Windows PCs using preserved local
PDB/symbol artifacts. If unavailable, add a lock-free, bounded kernel-base capture
to the monitor rather than increasing synchronous logging.

Offline follow-up:
- Local-only Windows 26100.8037 artifacts were found in the legacy investigation:
  `ntoskrnl.exe` SHA-256
  `5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c` and
  `ntkrnlmp.pdb` SHA-256
  `3a2346d8beb860b74978805c7950ecd0c03f8ce2da32f8df8395835ba09aa090`.
  They are proprietary and must not be copied or committed to the public repository.
- The observed boot PCs and those symbols identify the current kernel base as
  approximately `0xfffff80145800000`.
- Repeated sampled locations resolve to `KiDirectSwitchThread+0x67c`,
  `RtlpImageDirectoryEntryToDataEx+0x28/+0xcc`, and
  `RtlpAmd64xVirtualUnwind+0x398`. Earlier entry samples resolve around
  `KiThawSingleThread`. These samples show real scheduler/unwind progress rather than
  every CPU sitting in one fixed watchdog loop. They do not explain why EOIR progress
  later stops; the next diagnostic must correlate per-CPU LR transitions with trapped
  IAR/EOIR without synchronous logging.

### CORRECTION-20260813-002 — EXP-003 is not a complete timer fix

The first EXP-003 snapshot showed clean LRs and was sufficient to confirm that the
1 ms heartbeat can recover the specific initial stall. It was not sufficient to
claim that the timer defect was eliminated. After the successful SSH probes, SSH
disappeared again. A second non-destructive snapshot showed that every CPU had made
substantial queue/IAR/EOI progress since the first snapshot, proving the guest had
continued to execute, but active+pending INTID 18 returned on CPU0, CPU1, and
CPU4-CPU6. Therefore EXP-003 remains confirmed only for temporary recovery of the
first reproduced stall; it is rejected as a complete fix for sustained operation.

The next code experiment must address the timer LR/EOI state machine using the ARM
GIC architectural contract. Raising heartbeat frequency again is not justified by
this evidence.

### RESULT-20260813-004 — load did not start

Status: inconclusive
Completed (UTC): 2026-08-13

The bounded PowerShell load was never executed: the SSH connection timed out before
the remote command began. This result says nothing about stress stability. The
second hardware snapshot taken after the timeout proves vCPU counters advanced, so
the loss of SSH was not an all-core halt at the instant of capture. Preserve this as
a failed test setup, not as a failed CPU stress test.

### EXP-20260813-005 — KD classification of the sustained stall

Status: planned
Created (UTC): 2026-08-13
Completed (UTC): pending

Hypothesis: Windows is still executing but one or more kernel threads remain in an
interrupt/timer path that never reaches the emulated EOIR, explaining recurrent
active+pending INTID 18. Resolving the sampled PCs and Windows thread state will
distinguish a vGIC lifecycle defect from an unrelated USB/network loss.

Single changed variable: none. This is a read-only classification of the running
EXP-003 guest. Stop only the host UART log reader so KD can own the same virtual UART;
do not rebuild, reboot, or alter Windows configuration.

Run contract:
- Use `tools/kd/kd_liveness.py`, then obtain the kernel base/module and relevant
  execution state with the existing KD tools.
- Every KD operation must send Continue in `finally` so diagnosis cannot leave the
  guest paused.
- Restore passive UART capture afterward if additional boot logging is required.
- Evidence: KD output plus the two EXP-003 lock-free snapshots already in `hv.log`.

Failure criterion: no KD state-change response within the tool timeout. Such a result
is inconclusive and must not be interpreted as a dead Windows kernel because the UART
transport may itself be unavailable.

Observed result: pending.
Verdict: pending.

### RESULT-20260813-005 — KD transport did not classify the stall

Status: inconclusive
Completed (UTC): 2026-08-13

`kd_liveness.py` opened the virtual-UART transport and issued a break-in, but no
`STATE_CHANGE64` packet arrived before its bounded timeout.  Its cleanup path sent
Continue and the passive UART reader was restored.  This did not change source or
guest state and cannot distinguish a stopped Windows debugger transport from a
guest scheduler/timer stall.

### EXP-20260813-006 — exact-source A/B against the accepted morning baseline

Status: in progress
Created (UTC): 2026-08-13

Hypothesis: the recurrent long freezes are a regression introduced after the
accepted 2026-08-13 eight-core checkpoint, rather than a failure of its original
timer/IPI and vGIC correction.  Returning only the assisted m1n1 source to commit
`55531e9d9443e2543e172ed4c7f6ef8a7173a54e` while retaining the byte-identical Mu
firmware will restore the accepted behavior.

Single changed variable: m1n1 binary source.  The preceding EXP-003 guest used the
current dirty diagnostic/input tree.  EXP-006 uses a clean temporary clone at
`55531e9`; Mu remains SHA-256
`0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`.

Baseline provenance:
- Root release record: `5d827ba6b7f50daf538df0a167ed123c9a1f5731`.
- Root gitlink: m1n1 `55531e9d9443e2543e172ed4c7f6ef8a7173a54e`, Mu
  `9dccb0133f244f2e4de7e3862dcb9f0ef7ba4776`.
- The original accepted m1n1 binary SHA `7b735bf1...956d40` is documented but is no
  longer present in `dist/` or `.local/`.  It must not be silently substituted.
- A clean rebuild from the exact m1n1 commit produced SHA-256
  `3bfd9cc8080fc28a381a4c8fce39b5feeb093290b55caa928c8b6d745f069c6c` in
  `/private/tmp/wom1-baseline-55531e9.WLOUx1/m1n1_windows/build/m1n1.macho`.

Preceding failure evidence:
- The final EXP-003 snapshot showed CPUs 0, 1, 4, 5, and 6 retaining virtual timer
  INTID 18 as Active+Pending while their queue/IAR/EOI counters stopped advancing.
- A later SIGTERM snapshot showed the same retained LRs and almost unchanged
  counters; the managed handler then rebooted the Air and returned both USB ports.
- The observed Windows PCs were in scheduler/image-unwind code.  The evidence is
  consistent with the user-visible global pause but does not yet identify which
  post-baseline source change caused it.

Run contract:
- Assisted public launcher; physical display; passive monitor diagnostics; eight
  CPUs; no virtual framebuffer; no AppleInput Windows driver.
- Expected checkpoint: all eight CPUs enter Windows, desktop/SSH becomes available,
  and a bounded idle interval plus snapshots do not reproduce stopped EOI counters
  with overdue Active+Pending timer LRs.
- Failure criterion: frozen spinner/desktop, SSH loss after initial availability,
  watchdog bugcheck, or the same non-advancing LR/counter state.
- Recovery: SIGTERM to the foreground public runner, which captures a final snapshot
  and reboots to proxy.  The ESP is not modified.

Intermediate result:
- The clean `55531e9` m1n1 chainloaded and identified itself on hardware.  It
  initialized the physical panel and returned to proxy successfully.
- All seven physical secondary CPUs reported `Started` during host bootstrap.
- Before guest handoff, the current post-baseline Python launcher raised
  `RuntimeError: secondary CPU startup failed`.  No Windows code ran and this is
  not a baseline stability result.

Verdict so far: test setup rejected.  Reverting only the target binary is not a
valid A/B because the host proxyclient/preflight contract changed after the
  baseline.  Repeat with matching root `5d827ba` launcher and m1n1 `55531e9`; retain
  the identical Mu and hardware/profile variables.

Matching-runtime result:
- A clean matching root `5d827ba` launcher plus clean m1n1 `55531e9` passed host
  preflight and entered Windows Boot Manager.
- CPU0 entered the guest.  CPU1 then stopped after
  `Secondary 1 published entry=...`; `hv_init_secondary` never completed and no
  further secondary CPU entered Windows for more than two minutes.
- SIGTERM also stalled because its rendezvous required the same non-responsive CPU.
  Host processes were terminated and a physical reset was required.

Verdict: rejected as a complete stability baseline.  The morning accepted session
was real, but the same committed source still contains an intermittent secondary
mailbox/wakeup race.  Its own documentation correctly described it as a development
checkpoint with the repeated-cold-boot production gate still open.

### EXP-20260813-007 — atomic secondary mailbox and WFI-to-WFE transition

Status: rejected on hardware
Created (UTC): 2026-08-13

Hypothesis: CPU1 can sleep in the old deep-WFI loop while the boot CPU publishes
plain C `wfe_mode=true` and subsequently uses only SEV for mailbox notification.
SEV does not release that WFI waiter, leaving `smp_call4()` blocked after the
secondary entry is published.  Plain target/flag accesses also form a C data race.

Single changed variable: starting from clean m1n1 `55531e9`, make `wfe_mode`, the
mailbox target, and completion flag use release/acquire atomics; on mode transition,
send a physical IPI to every alive secondary before relying on SEV/WFE.  No timer,
vGIC, PSCI, Mu, display, input, NVMe, or Windows change is included.

Regression evidence:
- RED: two targeted tests failed on the absent release/acquire publication and
  non-atomic WFE transition.
- GREEN: the targeted eight-test secondary suite passed, followed by the complete
  `m1n1_windows/tests/run_host_tests.sh` suite.

Artifact:
- m1n1: `investigation/artifacts/EXP-20260813-007/m1n1.macho`
- m1n1 SHA-256:
  `8ff5562d67cb4b9c462d20c5ddcc67f87909cfceb893ba8abbd8dab1aad918ac`
- Mu: `investigation/artifacts/EXP-20260813-007/J313_EFI.fd`
- Mu SHA-256:
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`
- Build: clean plain `make -j8` after `make clean` in the temporary exact-commit
  checkout, with only the recorded `src/smp.c` diff.
- Recovery: assisted only; physical display and passive monitor; ESP unchanged.

Expected checkpoint: all seven secondary calls reach `Entering guest secondary`
and all eight CPU_ENTRY records appear without any mailbox stall.  This experiment
does not claim to fix the later timer Active+Pending freeze.

Hardware result:
- The matching root `5d827ba` runtime reached Windows Boot Manager and published
  CPU1's guest entry.
- Immediately after `HV: Secondary 1 published ...`, both USB endpoints vanished
  and the machine reset to the proxy.  No `Entering guest secondary 1` marker was
  emitted.
- Disassembly showed that the atomic completion increment introduced an LSE
  `ldaddl` instruction in the first secondary mailbox path.  The reset coincided
  with the first opportunity to execute that changed path.  This is correlation,
  not proof that LSE itself caused the reset, so the entire atomic rewrite is
  rejected rather than retained as a partial fix.
- The original `smp_set_wfe_mode()` already sent an IPI to every live secondary
  before SEV.  Therefore the initial claim that transition used “only SEV” was
  incomplete and cannot explain the baseline race by itself.

Recovery: the Air reset to `Running proxy`; a bounded proxy NOP succeeded.  The
ESP and Windows installation were not modified.

### EXP-20260813-008 — retain IPI/WFI mailbox mode through PSCI bring-up

Status: rejected on hardware
Created (UTC): 2026-08-13

Hypothesis: `hv_init()` switches the m1n1 callback dispatcher to WFE before
Windows starts secondary processors.  `hv_start_secondary()` subsequently uses
three mailbox callbacks (`mmu_secondary_setup`, `hv_init_secondary`, and guest
entry), each awakened by one SEV and acknowledged only indirectly via `flag`.
Keeping the dispatcher in its original deep-WFI/IPI mode removes the unacknowledged
WFE event-register dependency while preserving the known instruction sequence on
the secondary.

Single changed variable relative to clean m1n1 `55531e9`:
- `hv_init()` calls `smp_set_wfe_mode(false)` instead of `true`.
- Bounded console markers surround the first `mmu_init_secondary()` mailbox so a
  failure can be classified as publish, callback execution, or completion.
- EXP-007's atomic mailbox rewrite is completely removed; ordinary loads/stores
  and the original explicit `dmb/dsb` protocol remain.

No timer, vGIC, PSCI topology, Mu, display, input, NVMe, or Windows change is
included.  Expected checkpoint: CPU1 prints both MMU mailbox markers and reaches
`Entering guest secondary 1`, followed by the other six secondaries.

Hardware result:
- CPU1 reached `HV: Secondary 1 MMU mailbox publish` but never reached the matching
  completion marker.  Both USB endpoints then disconnected and the Air reset to
  proxy.
- Keeping WFI/IPI mode therefore does not resolve the failure.  The fault is in,
  or is triggered while waiting for, `mmu_secondary_setup()` itself.

### EXP-20260813-009 — pre-initialize secondary stage-1 before guest entry

Status: rejected on hardware
Created (UTC): 2026-08-13

Hypothesis: the physical secondaries are powered before guest entry but their
stage-1 MMU is initialized only later, inside a Windows PSCI CPU_ON call.  That
callback performs broadcast TLB maintenance while CPU0 is already executing the
guest.  Initialize every live secondary's stage-1 MMU in `hv_init()` instead,
while m1n1 still owns all CPUs; PSCI then performs only per-CPU EL2/vGIC setup and
guest entry.

Single changed variable relative to clean `55531e9`: move
`mmu_init_secondary(cpu)` from `hv_start_secondary()` into a pre-guest loop in
`hv_init()`.  Keep the original barrier-based mailbox and original WFE mode; no
timer, vGIC, Mu, display, input, NVMe, or Windows change.

Hardware result:
- The Air reset immediately after `HV: Pre-initializing MMU on secondary 1`, before
  Mu or Windows was started.
- This proves that the failure is the secondary EL1 MMU setup itself, not guest
  concurrency, PSCI timing, WFE wakeup, or Windows.

### EXP-20260813-010 — leave PSCI secondary EL1 MMU disabled

Status: rejected on hardware
Created (UTC): 2026-08-13

Hypothesis: `mmu_init_secondary()` violates the PSCI handoff contract by enabling
m1n1's EL1 stage-1 regime on a CPU that must enter the Windows secondary startup
trampoline with `SCTLR_EL1.M` clear.  m1n1's dispatcher and `hv_init_secondary()`
execute at EL2 and do not need EL1 stage-1.  Remove the call entirely and let
Windows install its own EL1 translation regime.

Single changed variable relative to clean `55531e9`: remove
`mmu_init_secondary(cpu)` from the PSCI secondary path.  Keep the original SMP
barriers, WFE mode, timers, vGIC, Mu, display, input, NVMe, and Windows unchanged.

Hardware result: CPU1 still reset immediately after its entry was published.
Therefore the next callback, `hv_init_secondary()`, contains an independent reset
trigger.  Removing only EL1 MMU setup is insufficient.

### EXP-20260813-011 — minimal EL2-only secondary initialization

Status: rejected on hardware
Created (UTC): 2026-08-13

Hypothesis: `hv_init_secondary()` copies boot-CPU EL1/EL12 and GXF/SPRR state onto
a fresh PSCI CPU even though the guest startup trampoline expects reset-like EL1
state.  One of those implementation-defined writes resets the CPU/system before
the required EL2 state has completed.

Diagnostic A/B: retain required hypervisor state only (HCR, HACR, VTCR, VTTBR,
MDCR, ACTLR_EL2, CNTHCTL, CNTVOFF, timer routing, vGIC list registers, and EL2
heartbeat).  Temporarily omit GXF setup and copied EL1/EL12 AMX, PAuth, APSTS,
ACTLR, SPRR, GXF, and MDSCR state.  No Mu/Windows/display/input/NVMe change.

Hardware result: reset remained immediately after CPU1 entry publication.  The
trigger is therefore either one of the retained EL2 operations or the mailbox
callback/rendezvous itself.

### EXP-20260813-012 — empty secondary callback control

Status: inconclusive on hardware
Created (UTC): 2026-08-13

Control experiment: make `hv_init_secondary()` return without any register write.
If the Air still resets, the callback dispatch or the following guest-entry
mailbox is faulty.  If it does not, reintroduce required EL2 groups by binary
search.  This build is diagnostic only and is not expected to run Windows safely.

Hardware result: USB still disconnected after CPU1 publication, but the existing
log had no marker between callback completion and the immediately following guest
entry.  The result therefore cannot distinguish those adjacent operations.

### EXP-20260813-013 — hold CPU1 after empty callback

Status: rejected on hardware
Created (UTC): 2026-08-13

Control: retain the empty callback, print and flush a completion marker after
`smp_wait()`, then return from PSCI handling without entering CPU1 into Windows.
This deliberately leaves Windows waiting and is not a boot candidate.  It only
separates mailbox completion from `hv_enter_guest()`.

Hardware result: the completion marker was never emitted and USB disconnected.
An empty callback therefore was never executed; the failure is lost mailbox wake,
not secondary register programming or guest entry.

### EXP-20260813-014 — dual IPI+SEV mailbox notification

Status: successful mailbox fix on hardware; EXP-015 tests stricter IPI cleanup
Created (UTC): 2026-08-13

Hypothesis: a target can observe either side of the global WFI-to-WFE transition,
while `smp_call4()` chooses only one notification from the sender's current view.
The published callback then remains pending forever.  Make notification robust to
that race by always sending both a targeted IPI and SEV, and clear the IPI after
either sleep path returns.

Single changed variable relative to clean `55531e9`: SMP mailbox wake protocol.
No `hv.c`, timer, vGIC, PSCI, Mu, display, input, NVMe, or Windows change.

Hardware result:
- All seven secondary mailboxes completed and all seven cores reached guest entry.
  This removes the repeatable reset immediately after `Secondary 1 published` and
  confirms the WFI/WFE notification race.
- The first bounded log read initially contained `CPU_ENTRY` only for CPUs 2, 4,
  5, 6 and 7.  This was not a reset: the m1n1 proxy endpoint intentionally remains
  available while the guest runs, and `run_uefi.py` returning is normal.
- A later deliberate chainload interrupted the still-live hypervisor.  Its
  pre-rendezvous snapshot proved CPUs 1 and 3 were also online.  All eight CPUs
  had processed tens of thousands of SGIs with matching IAR and EOI counts; no
  no-LR backlog was present.  Never infer guest death from proxy presence or the
  launcher process exiting again.
- Network reachability was not evidence either: the tested Windows installation
  did not acquire/respond at the previously cached address during that window.
- The IPI acknowledgement still lived inside the sleep loop.  EXP-015 keeps the
  proven dual notification and tests stricter cleanup for a target that observes
  its mailbox before sleeping.

Artifacts:
- `investigation/artifacts/EXP-20260813-014/m1n1.macho`
- SHA-256 `9a17a7139a770c63daa5c422e203f1c64f3af913d2a6cca409bbc5e0afda9c53`

### EXP-20260813-015 — dual wake with unconditional IPI acknowledgement

Status: invalid mixed-runtime attempt; superseded by EXP-016
Created (UTC): 2026-08-13

Hypothesis: EXP-014 fixed wake delivery but left a stale physical IPI whenever a
secondary observed the mailbox without entering its sleep loop.  That IPI then
raced Windows' first virtual SGIs after guest entry.  Acknowledge the IPI once,
unconditionally, after the mailbox becomes visible and before executing the
callback.

Single changed variable relative to EXP-014: location of the physical IPI
acknowledgement.  It now runs for both the slept and did-not-sleep paths.  The
baseline remains exact m1n1 `55531e9`; Mu, Windows, timers, vGIC, NVMe, display
and input are unchanged.

Artifacts:
- `investigation/artifacts/EXP-20260813-015/m1n1.macho`
- SHA-256 `f34bbc2cb8d5ef7b4b45b5daa9b233f04ed4acd6c1ac33a905e9c5e9cd552fb7`

Hardware result:
- The image reached proxy, but the current public launcher received `Bad Command`
  for `P_HV_LAUNCH_PUBLISH` before guest launch.  No CPU or Windows conclusion is
  valid from this attempt.
- EXP-016 rebuilds the same SMP change directly from the canonical public tree
  and restores its committed production `hv_init_secondary()` and
  `mmu_init_secondary()` paths, avoiding another mixed-tree launch.

### EXP-20260813-016 — canonical public runtime with EXP-015 SMP wake

Status: CPU/SMP path successful; clean endurance rerun required
Created (UTC): 2026-08-13

Canonical public m1n1 with the full launch-descriptor ABI, production secondary
MMU/EL2/vGIC/timer initialization, and the dual IPI+SEV mailbox wake with
unconditional physical IPI acknowledgement.  Diagnostic-only empty-secondary
experiments and their tests were removed.  The focused Python SMP contract suite
passes 10/10 and the complete C host suite passed before this build.

Artifacts:
- `investigation/artifacts/EXP-20260813-016/m1n1.macho`
- SHA-256 `dd019e4c34db3241bd4e9d1ed6d6bd3db772160b082685da8d1a6410f485be64`
- Mu SHA-256 `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`

Hardware result:
- Launch preflight passed.  CPUs 0 through 7 all emitted `CPU_ENTRY`; every
  secondary mailbox was published, consumed and entered exactly once.
- After more than one minute, a watchdog snapshot showed all eight CPUs still
  executing Windows.  Per-CPU SGI queue, IPI receive, IAR and EOI counters were
  balanced (tens of thousands each), with zero pending SGI masks, zero queue
  depth and zero no-LR events.  This is direct evidence that the earlier
  mailbox/SGI startup failure is removed.
- The subsequent reset was caused by the observer, not the guest:
  `hang_telemetry.py --once` attached as a second proxy client, reported
  `Proxy callback without handler: 3, 3`, then drove m1n1 into an EL2 data abort
  at relative PC `0x51264`.  The target rebooted immediately afterward.
- Do not use standalone `hang_telemetry.py` against a live assisted guest until
  callback/event ownership is fixed.  Presence of both USB interfaces is not a
  failure signal, and an external observer must not compete with the launcher.
- A clean rerun without any second proxy owner again passed preflight and emitted
  `CPU_ENTRY` for CPUs 0 through 7.  It remained alive for the bounded passive
  observation window (more than three minutes), with the same USB generation and
  no bugcheck, watchdog, exception, panic or reset marker.  The old cached Windows
  IP did not respond, so login/desktop responsiveness still requires visual or
  current-network confirmation by the operator; it is not inferred from silence.
- Verification after cleanup: root Python suite 242/242 passed, nested SMP Python
  suite 10/10 passed, and the complete nested C host suite passed.
- Local clean hardware transcript SHA-256:
  `668690622096b14c6258e40f52b7280550576cca4a00404b71851cfb3c0c5308`
  (`investigation/artifacts/EXP-20260813-016/hv-clean.log`, ignored by Git).
