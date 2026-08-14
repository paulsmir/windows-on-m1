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

### EXP-20260814-017 — operator responsiveness test of unchanged EXP-016

Status: in progress; operator verdict pending
Created (UTC): 2026-08-14

Purpose: give the operator an unmodified, already-recorded EXP-016 assisted boot
for a subjective responsiveness and stability test before any Apple keyboard or
trackpad work begins.  There is no changed firmware variable relative to
EXP-016.  Physical display is enabled; USB framebuffer and external telemetry
observer are disabled.  Debug mode is `monitor` only so no competing proxy owner
is attached after guest handoff.

Artifacts:
- `investigation/artifacts/EXP-20260813-016/m1n1.macho`
- SHA-256 `dd019e4c34db3241bd4e9d1ed6d6bd3db772160b082685da8d1a6410f485be64`
- `investigation/artifacts/EXP-20260813-016/J313_EFI.fd`
- SHA-256 `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`
- Runtime: root `5d827ba` materialized at
  `/private/tmp/wom1-root-5d827ba.WCgcVM`

Launch result so far:
- Launch-contract preflight passed with physical 2560x1600 framebuffer and no
  USB framebuffer or telemetry.
- Mu opened the installed `\\EFI\\BOOT\\BOOTAA64.EFI` and transferred control to
  Windows Boot Manager.
- CPUs 0 through 7 emitted `CPU_ENTRY`; NVMe and xHCI initialized.
- No bugcheck, watchdog, reset, panic, or exception was observed during the
  captured startup window.
- Responsiveness, sustained stability, display, USB input, SSH and RDP results
  remain pending the operator's test and must not be inferred from boot logs.

Process correction: this entry was appended immediately after launch instead of
before it.  That violates the intended pre-run ordering but does not change the
artifact or result.  Future hardware launches must create the entry first.

Operator result:
- Windows reached the desktop but booted much more slowly than the accepted
  morning baseline.
- Continuous pointer micro-stutter returned during ordinary mouse movement.
- The operator classifies this as a definite CPU-path performance regression,
  not an acceptable stability improvement.

Verdict: rejected for performance.  EXP-018 tests the only continuous-rate CPU
policy difference identified against the responsive baseline while retaining
EXP-016's successful secondary-mailbox wake correction.

### EXP-20260814-018 — restore sparse secondary recovery tick

Status: rejected; responsiveness improved but minute-scale timer-progress stall reproduced
Created (UTC): 2026-08-14

Hypothesis: EXP-017's continuous micro-stutter is caused by raising the non-ECV
secondary housekeeping tick from 100 Hz to 1000 Hz.  On J313 this creates about
7000 additional EL2 entries per second across seven secondary CPUs, and the
current secondary FIQ path also performs vGIC resynchronisation.  Restoring the
accepted 100 Hz fallback should restore interactive responsiveness without
reintroducing the intermittent secondary-mailbox startup race.

Single changed variable relative to EXP-017:
- `HV_FALLBACK_SECONDARY_TICK_RATE`: 1000 Hz -> 100 Hz.

Unchanged:
- dual IPI+SEV mailbox notification and unconditional IPI acknowledgement;
- all eight CPUs, secondary FIQ/vGIC correctness path, boot CPU 1000 Hz tick;
- Mu, ACPI, NVMe, xHCI, display, Apple-input passthrough and Windows install;
- physical display, monitor debug profile, no USB framebuffer, no external
  telemetry observer.

Source and artifacts:
- m1n1 commit `90f8545c818ba7bd70063e5ecf3e7711e51c6aa2`;
- `investigation/artifacts/EXP-20260814-018/m1n1.macho`;
- SHA-256 `67773a1c86ca4d41424e217b83ea74fcd39c10c331403b025853741909d60dc4`;
- `investigation/artifacts/EXP-20260814-018/J313_EFI.fd`;
- SHA-256 `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- recovery artifact: EXP-016 m1n1 SHA-256
  `dd019e4c34db3241bd4e9d1ed6d6bd3db772160b082685da8d1a6410f485be64`.

Software verification:
- RED: the tick-policy test failed while the implementation still returned
  1000 Hz.
- GREEN: the complete nested C host suite passed after the change.
- The focused secondary-launch Python suite passed 10/10.
- Clean `make -j8` completed with the canonical Rust path and produced the
  recorded Mach-O hash.

Expected checkpoint: CPUs 0 through 7 enter Windows exactly once, Windows boot
and pointer movement are materially smoother than EXP-017, and no watchdog,
bugcheck, reset, or long global freeze appears during the operator test.

Failure criterion: any missing secondary CPU, boot-time reset, watchdog, or no
observable improvement in the slow boot and continuous pointer micro-stutter.
Recovery: assisted launch only; the ESP is unchanged and EXP-016 remains the
recorded recovery image.

Hardware result:
- Launch-contract preflight passed and CPUs 0 through 7 entered Windows exactly
  once.  NVMe and xHCI both initialized.
- The operator initially observed an improvement over EXP-017, then Windows
  stopped responding completely for at least one minute.  It later recovered
  without a reboot.
- The UART log was quiescent after xHCI route enable.  Two non-destructive
  snapshots were requested through the existing launcher; no second proxy owner
  was attached.
- Across both snapshots, the physical counters advanced but Windows remained in
  a narrow kernel PC range.  The SGI diagnostic queue/IAR/EOI counts did not
  advance between samples (apart from the one diagnostic IPI used to request the
  second snapshot).
- Several CPUs retained a live virtual-timer INTID 18 LR.  The raw LR state must
  be decoded from bits 63:62: `0x502...` is Pending-only, `0x902...` is
  Active-only, and Active+Pending would be `0xd02...`.  EXP-018 contained both
  Pending-only and Active-only timer LRs, but no sampled Active+Pending LR.  Other
  CPUs had no live timer LR, yet none showed normal interrupt progress in either
  sampled interval.
  Queue depth and `no_lr` remained zero.  This excludes NVMe queue exhaustion and
  LR scarcity as the immediate cause.
- The diagnostic IPI briefly advanced the last timer IAR/EOI timestamps and moved
  the live timer LR state between CPUs, but sustained execution was not visible
  during the sampled interval.  The operator later confirmed that Windows did
  recover after a pause of one minute or more.  Because that recovery occurred
  after the diagnostic IPIs, this run cannot distinguish spontaneous timeout
  recovery from recovery assisted by the external wake.

Verdict: rejected for stability.  The A/B establishes that the 1000 Hz secondary
heartbeat in EXP-017 was masking a lost timer/vGIC progress condition while also
creating continuous micro-stutter.  Reducing it to 100 Hz improves responsiveness
but exposes a minute-scale full-system stall.  The next correction must repair event-driven
timer progress; selecting another polling frequency is not an acceptable fix.

Evidence:
- preserved log: `investigation/artifacts/EXP-20260814-018/hv-freeze.log`,
  SHA-256 `7fde0975539dee80628591470687fd3c1b19256ba6e4edfa3efa57ba5ba12289`;
- first and second lock-free snapshots: `HV WATCHDOG CPU` records following the
  two `HV: User interrupt; pre-rendezvous watchdog snapshot` markers.

Decoder correction: earlier experiment prose called raw `0x902...` timer LRs
Active+Pending.  That label is incorrect under `ICH_LR_STATE_SHIFT=62` and
`ICH_LR_STATE_MASK=3`: the sampled state value is 2 (Active-only).  Raw values in
the old records remain valid evidence, but future analysis must use the corrected
decoder above.

### EXP-20260814-019 — level-sensitive guest timer synchronization

Status: rejected after hardware test
Created (UTC): 2026-08-13T23:43:58Z

Hypothesis: EXP-018's minute-scale global stall occurs because the production
timer path treats a live Active INTID 17/18 LR as sufficient ownership and skips
the required level reassertion transition to Active+Pending.  Synchronizing each
sampled timer line directly with LR state, withdrawing stale Pending state on
deassertion, and using one unique deferred owner per timer will preserve every
expiry without a high-frequency recovery heartbeat.

Single changed variable relative to EXP-018:
- event-driven timer/vGIC delivery ownership and LR state synchronization for
  architectural timer INTIDs 17 and 18.

Unchanged:
- all eight CPUs and the race-safe secondary mailbox IPI+SEV protocol;
- boot CPU 1000 Hz and non-ECV secondary 100 Hz heartbeat policy;
- Mu, ACPI, Windows installation, NVMe, xHCI, display, Apple input and memory
  layout;
- physical display, monitor diagnostics, assisted launch, and one USB owner.

Source contract before artifact build:
- root repository `/Users/pavel/public_windows`, branch
  `codex/canonical-public-release`, commit
  `efb93c6be3f362d64eedf2d22ad787cad005d256`;
- root dirty diff SHA-256
  `e110dd7850110333712048a8afbad00de977e7116c4f6bf395f033d553357166`;
- m1n1 commit `2ead84d25644316cb59c552b420396dcbd9d07c2`, clean diff
  SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`;
- Mu commit `63942398cccbd98127cfecbd7f936af99c837d6f`, tracked-source
  diff SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`;
  nested Mu checkout markers remain and are not source changes.

Software evidence before artifact build:
- RED `hv_vgic_diag_test`: missing level-result type and sync API;
- GREEN plus mutation check: the test fails when Active+asserted incorrectly
  remains Active-only and passes when it becomes Active+Pending;
- RED `hv_timer_delivery_test`: missing deferred-owner source;
- GREEN unique ownership, withdrawal, FIFO and invalid-INTID tests;
- RED root integration suite: missing synchronizer and EOI timer-drain boundary;
- GREEN root integration suite 11/11, complete nested C host suite, focused SMP
  Python suite 10/10, and complete root Python suite 247/247;
- clean freestanding `make -j8` completed from the canonical public tree.
- the final hot-path refactor removed a redundant second LR-bank scan and VI
  recomputation after `hv_vgic3_inject_irq()`; the complete nested C host suite
  remained green before the recorded post-commit artifact build.

Recorded artifact:
- `investigation/artifacts/EXP-20260814-019/m1n1.macho`;
- SHA-256 `7c9c5a400b3a14cc842119fa7663ef36ab2390bf0b41d3292e75c52963e056c5`;
- provenance manifest
  `investigation/artifacts/EXP-20260814-019/MANIFEST.json`, SHA-256
  `4dc45bc768bee43e688d87f8f5cdba28e3fc19bf0706b628272881c7d1580502`;
- manifest root commit `5c585245162a39d0e0f632a6e8a062c019692838`, root
  diff SHA-256
  `dc75ba5d62525f016d3fbd9dd6b5e7e6e1c7160c455dc94de961f19cfda8ffab`,
  m1n1 commit `2ead84d25644316cb59c552b420396dcbd9d07c2`, and Mu
  commit `63942398cccbd98127cfecbd7f936af99c837d6f`;
- unchanged `investigation/artifacts/EXP-20260814-019/J313_EFI.fd`, copied from
  EXP-018 Mu SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- recovery artifact EXP-016 m1n1 SHA-256
  `dd019e4c34db3241bd4e9d1ed6d6bd3db772160b082685da8d1a6410f485be64`.

Exact build command:

```sh
cd /Users/pavel/public_windows/m1n1_windows
PATH="/opt/homebrew/opt/rustup/bin:$PATH" make clean
PATH="/opt/homebrew/opt/rustup/bin:$PATH" make -j8
```

Post-commit build result: successful.  The warnings are pre-existing in the
tree; the linker produced the recorded Mach-O from clean m1n1 commit
`2ead84d25644316cb59c552b420396dcbd9d07c2`.

Planned launch: canonical `scripts/run-windows.sh` assisted execution with the
recorded EXP-019 m1n1 and unchanged Mu artifacts, physical display, monitor
diagnostics, explicit proxy/vUART devices, and no second observer.

Expected checkpoint: launch preflight passes; CPUs 0 through 7 enter exactly
once; NVMe, xHCI and external USB input remain alive; Windows reaches login or
desktop; timer IAR/EOI progress continues without diagnostic IPIs; no asserted
Active-only timer survives synchronization; and no global pause, watchdog,
reset, or EXP-017 continuous micro-stutter occurs through a bounded ordinary and
CPU/storage stress window longer than the EXP-018 pause interval.

Failure criterion: missing CPU_ENTRY, Windows failing to reach login, any global
pause, `CLOCK_WATCHDOG_TIMEOUT`, reset, timer-progress loss, stale asserted
Active-only LR, or continuous micro-stutter.  Recovery is assisted-only; ESP is
unchanged and EXP-016 remains available.

Evidence paths after launch:
- `investigation/artifacts/EXP-20260814-019/hv.log`;
- existing-launcher SIGINT lock-free snapshots only if progress stalls;
- no standalone `hang_telemetry.py` or competing proxy owner.

Hardware attempt 1: infrastructure-aborted before Windows.  The exact artifact
and manifest preflight passed, all secondaries started in m1n1, guest handoff
occurred, NVMe ECAM/BAR initialized, and Mu reached
`AppleUsbTypeCBringupDxeBringupCallback`.  The host command session then reaped
the detached `run_uefi.py` and UART-reader children; `hv.log` ended mid-line and
both recorded PIDs were gone, with no guest exception or reset record.  Because
the runner disappeared before secondary Windows `CPU_ENTRY`, this attempt says
nothing about the timer correction.  Repeat with the same artifacts and the
canonical launcher held in foreground; do not change the guest variable.

Hardware attempt 2: infrastructure-aborted before guest initialization.  The
proxy was still owned by the live Windows hypervisor left by attempt 1 rather
than by a clean Stage 1.  Chainloading through that live guest raised an EL1
exception and rebooted the target; after reconnect, the launcher continued
against the installed old Stage 1 (`b791225`), which correctly rejected the new
launch-publish opcode as `Bad Command`.  The resulting fresh `Running proxy`
state was verified with `probe.py`.  Repeat unchanged from that confirmed Stage
1; do not classify this as timer behavior.

Hardware attempt 3: clean run in progress.  The exact frozen artifacts passed
manifest verification, m1n1 `2ead84d` chainloaded once from a confirmed fresh
Stage 1, and the foreground runner remained the sole proxy owner.  Mu started
the installed Windows fallback loader; CPUs 0 through 7 entered exactly once;
NVMe reached ready state and xHCI enabled its Windows route.  No bugcheck,
exception, reset, LR shortage, or deferred timer backlog was recorded.

After a passive interval longer than EXP-018's original pause, two lock-free
snapshots showed all eight physical counters advancing and substantial forward
progress in `q`, `iar`, and `eoi` on every CPU.  Live INTID 18 LRs included the
new `0xd02...` Active+Pending state, and `ap_eoi` was nonzero on the CPUs that
had consumed reassertions.  Between snapshots CPU0 `iar/eoi` advanced from
17973/17973 to 41437/41437, CPU1 from 20383/20383 to 55491/55491, and the other
CPUs advanced similarly.  `no_lr=0`, timer queue depth remained zero, and guest
PCs changed.  This directly validates the corrected level transition and shows
that the minute-scale timer-progress loss did not recur in the observed window.

Partial evidence: `investigation/artifacts/EXP-20260814-019/hv-progress.log`,
SHA-256
`7de57f58349fad9c4d313e1669a2488b971769cc72fa9abd2d81be707b9a85de`.
Final verdict remains pending operator confirmation of the physical login or
desktop, pointer/input responsiveness, and the bounded ordinary/stress window.

### EXP-20260814-020 — remove the boot-CPU 1 kHz recovery tax

Status: rejected; early Windows boot requires the 1 kHz boot heartbeat
Created (UTC): 2026-08-14

Hypothesis: after EXP-019 repaired event-driven architectural-timer delivery,
CPU0's remaining 1 kHz EL2 recovery heartbeat is unnecessary and causes the
observable interactive latency.  CPU0 is the only vCPU using 1 kHz; CPU1-7 use
100 Hz.  A Windows QPC affinity test measured CPU0 at 5344.4 ms for a 5000 ms
window with p95 overshoot 56.465 ms and maximum 169.936 ms, while CPU4 completed
in 5000.4 ms with zero measured overshoot.  A follow-up all-core test again
isolated the only non-zero delay to CPU0.

Single changed variable relative to EXP-019:
- boot CPU EL2 recovery tick: 1000 Hz -> 100 Hz.

Unchanged:
- EXP-019 level-sensitive INTID 17/18 synchronization and deferred ownership;
- all eight CPUs, mailbox IPI+SEV protocol, Mu, Windows, NVMe, xHCI, display,
  memory layout, assisted launch and monitor diagnostics;
- secondary non-ECV recovery tick remains 100 Hz.

Pre-change evidence from the live EXP-019 guest:
- Windows Stopwatch/QPC frequency: 24 MHz;
- 100 x 100 ms scheduler waits: total 14264 ms, median 111 ms, p95 168.4 ms,
  maximum 1544.5 ms;
- idle CPU about 1%, DPC 0%, processor queue 0, disk latency 0 ms;
- CPU0 affinity test showed timing loss while CPU4-P was exact;
- the post-test lock-free snapshot showed empty software queues, no LR shortage,
  and continuing IAR/EOI progress on all CPUs.

Expected checkpoint: CPUs 0-7 enter Windows; the same CPU0 and CPU4 affinity
test both complete 5000 ms with negligible overshoot; the 100 x 100 ms scheduler
test no longer contains a large outlier; no timer stall, watchdog, reset or
bugcheck occurs.

Failure criterion: CPU0 remains uniquely delayed, any vCPU loses timer progress,
or Windows fails to reach the desktop.  Recovery uses the immutable EXP-019
artifact; the ESP is unchanged.

Hardware result:
- artifact `m1n1.macho` SHA-256
  `2b9a073435f0f481621e38a7b36f45ab9dc38864c6af7dfb918f8b844194ef1b`
  from m1n1 commit `cf9770180e0122d92b2274081eb7ecebda5a7d2d` passed manifest
  preflight and chainloaded from a clean Stage 1;
- CPUs 0-7 entered once and NVMe reached ready;
- Windows then raised bugcheck `0x7e` with parameter 1 `0xffffffffc0000094`
  (integer divide by zero) at `0xfffff803a06935d0` before reaching login;
- the snapshot placed CPU0 in the stall-check path and showed INTID 18 pending
  on every vCPU; Windows requested PSCI reset immediately afterward.

Verdict: rejected.  The unique 1 kHz CPU0 heartbeat correlates with the measured
CPU0 latency, but reducing it from reset is unsafe.  A valid correction must
retain the startup cadence and switch to sparse/event-driven service only after
an explicit guest-ready milestone; do not tune the static constant again.

### EXP-20260814-021 — two-phase boot CPU heartbeat

Status: rejected as a complete fix; CPU0 tax improved but global pauses remain
Created (UTC): 2026-08-14

Hypothesis: Windows requires CPU0's 1 kHz EL2 heartbeat during early SMP/timer
initialization, but retaining it after device initialization causes the measured
CPU0-only scheduling gaps.  Keep 1 kHz through early boot, then atomically switch
CPU0 to 100 Hz when Windows enables the J313 xHCI hardware IRQ route.  In every
successful EXP-019 boot this milestone occurs after CPUs 0-7 enter and NVMe is
ready; EXP-020 failed before reaching it.

Single changed variable relative to EXP-019:
- CPU0 tick policy becomes two-phase: startup 1000 Hz, post-xHCI-route 100 Hz.

Unchanged:
- secondary ticks and EXP-019 timer delivery state machine;
- Mu, Windows, NVMe/xHCI implementation, display, memory layout and launch mode.

Expected checkpoint: early Windows boot reaches the xHCI route without the
EXP-020 `0x7e`; the log records one cadence transition; SSH returns; CPU0's
5-second affinity timing converges with CPU4; no large scheduler-wait outlier,
bugcheck, watchdog or reset occurs.

Failure criterion: transition occurs before all CPUs/NVMe are ready, Windows
fails to reach SSH, CPU0 remains uniquely delayed, or timer progress is lost.
Recovery is immutable EXP-019; the ESP remains unchanged.

Frozen assisted artifact before launch:
- m1n1 commit `ec7dd42b` (`hv: lower boot tick after guest runtime handoff`);
- `investigation/artifacts/EXP-20260814-021/m1n1.macho`, SHA-256
  `0928ec0a8c342aecca1bf0526059aea5d39a3c647fbc533ef943cb6c6efe9b8f`;
- unchanged Mu `J313_EFI.fd`, SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- manifest SHA-256
  `e5a66d86266c18d10ab6d4608e24ea58f0e5038dea7cc2703196b39f088e39a6`.

Preflight result: manifest roles/profile/layout validated; focused RED tests
failed before the runtime-ready API and xHCI hook existed; after implementation
the complete nested C host suite and the focused root vGIC contract passed.  No
hardware result has been inferred from those host tests.

Hardware result:
- all eight CPUs entered, NVMe reached ready, and the one-time
  `boot tick 1000Hz -> 100Hz` transition occurred only after the Windows xHCI
  route; the EXP-020 early `0x7e` did not recur and SSH reached the desktop;
- CPU0's 5-second affinity window improved from 5344.4 ms to 5002.789 ms;
  CPU0 p95/max overshoot became 30.184/102.074 ms while CPU4 completed in
  5000.009 ms;
- the system scheduler test did not converge: 100 x 100 ms waits still took
  14222.120 ms, median 110.576 ms, p95 364.678 ms and maximum 799.383 ms;
- SSH twice stopped accepting a connection for about 20 seconds and then
  recovered without a reset or bugcheck;
- the snapshot taken during the second timeout showed all eight Windows PCs in
  the idle/WFI path, empty software delivery queues, no LR shortage, and live
  Pending or Active+Pending INTID 18 LRs.  Several CPUs had HCR.VI asserted but
  ISR_EL1 clear.  Timer and SGI counters continued between snapshots.

Verdict: the 1 kHz boot-CPU heartbeat was a measurable CPU0 tax, but changing
its steady-state cadence is not the root fix.  The remaining pause signature is
an idle-wakeup/delivery failure: a virtual timer can be represented in the vGIC
while the physical Apple core remains asleep.  Do not ship EXP-021 as the
stability fix.

### EXP-20260814-022 — trap guest WFI/WFE to isolate idle-wakeup loss

Status: rejected after hardware test
Created (UTC): 2026-08-14

Hypothesis: the long recoverable pauses occur after Windows enters physical WFI
with a deliverable timer LR; changing HCR.VI/LR state does not reliably wake the
Apple core.  The tree already contains the bounded `HV_DIAG_TRAP_WFX` path.
Trapping WFI/WFE and advancing the guest PC prevents physical sleep while
leaving timer, vGIC, NVMe, Mu, Windows, topology and display unchanged.

Single changed variable relative to EXP-021:
- build m1n1 with `DIAG_TRAP_WFX=1`.

Expected checkpoint: the exact CPU0/CPU4 and 100 x 100 ms probes complete
without the 20-second SSH/UI pause, and repeated SSH probes remain responsive.
This build is diagnostic only because idle CPUs busy-poll and power consumption
will increase.

Failure criterion: any global pause remains, Windows bugchecks/resets, or the
WFI trap policy is lost after guest handoff.  Recovery remains immutable
EXP-019; the ESP is unchanged.

Frozen assisted artifact before launch:
- source commit `ec7dd42b`, built with `DIAG_TRAP_WFX=1` and no source change;
- `investigation/artifacts/EXP-20260814-022/m1n1.macho`, SHA-256
  `7c787f54035586d2aa8679e889e0385be41d6ac09358842bb5410d54213703c6`;
- unchanged Mu SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- manifest SHA-256
  `ced7652f769c9b5b6a769f42c084642ed6525d9cab9d8aba0e99b922ea25cfc8`.

Preflight result: clean build completed and manifest roles/profile/layout
validated.  This entry intentionally precedes the hardware launch.

Hardware result:
- all eight CPUs entered, NVMe reached ready, the runtime tick transition and
  xHCI route completed, and the log confirmed that TWI/TWE were active;
- two snapshots roughly 30 seconds apart showed Windows CPUs in idle paths
  with live Pending or Active+Pending timer LRs, but every CPU's timer
  queue/IAR/EOI counters remained unchanged; only the diagnostic snapshot IPI
  counter advanced;
- Windows then bugchecked with `INTERNAL_POWER_ERROR` (`0xa0`):
  `P1=0x618`, `P2=0xffffa20d52c2cb40`,
  `P3=0xffffa603c169a080`, `P4=0x0`.

Verdict: rejected.  Advancing guest WFI/WFE as a busy-poll changes Windows
power-idle semantics, does not restore timer/vGIC progress, and produces a
power-manager bugcheck.  Never enable `DIAG_TRAP_WFX` in a production or
stability candidate.  Continue the investigation at virtual interrupt
delivery/deactivation without replacing architectural WFI/WFE behavior.

### EXP-20260814-023 — identify the global EL2 lock owner during a freeze

Status: completed; hypothesis not confirmed by sampled lock state
Created (UTC): 2026-08-14

Hypothesis: the apparently idle vCPU/timer signature is secondary evidence of
global EL2 lock contention.  In two EXP-021 snapshots CPU0's breadcrumb ended
in `X`, which is emitted immediately before `spin_lock(&bhl)`, while its INTID
18 and INTID 64 LRs and last IAR/EOI state remained byte-for-byte unchanged.
If `bhl` is leaked or held by another CPU, all serialized vCPUs can stop while
physical counters and the lock-free snapshot mechanism remain alive.

Single changed variable relative to immutable EXP-021:
- the lock-free watchdog dump prints the live `bhl.lock` owner and recursive
  `bhl.count`; no timer, vGIC, WFI/WFE, tick, NVMe or guest behavior changes.

Expected checkpoint: during the next operator-visible pause, a non-destructive
snapshot identifies either a concrete owner/count or proves that CPU0's `X`
breadcrumb was not a live bhl wait.  Repeated snapshots must distinguish a
long holder from a permanently leaked lock.

Failure criterion: diagnostic output perturbs boot, the bhl fields cannot be
read locklessly, or no pause occurs during the bounded observation.  Recovery
is immutable EXP-021; the ESP remains unchanged.

Preflight result so far: the focused source-contract test fails because the
watchdog dump does not yet publish bhl owner/count.  This entry intentionally
precedes implementation, artifact creation and hardware launch.

Implementation and frozen artifact:
- m1n1 commit `922bb87` (`diag: expose global hypervisor lock owner`);
- the RED source-contract test failed before implementation and passed after;
- complete nested C host suite passed and the post-commit freestanding build
  completed; `HV_DIAG_TRAP_WFX` is absent from the build configuration;
- `investigation/artifacts/EXP-20260814-023/m1n1.macho`, SHA-256
  `fe0ac8041e5dcb280cb70d40c1212527ed48eb3c6ec1f40e46499a4dc9b7ceda`;
- unchanged Mu SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- manifest SHA-256
  `09d864ad14ce3390b4c04f2bac8986ce1e63fc326a5bd82a21e690d8a5452c34`.

No hardware result is inferred from software tests.  This update is recorded
before the assisted launch.

Hardware result:
- EXP-023 reached the same all-CPU, NVMe and xHCI checkpoints as EXP-021;
- Windows again failed to reach SSH during the bounded boot interval;
- a non-destructive snapshot reported `HV WATCHDOG BHL: owner=0 count=1`.
  The dump itself runs from CPU0 while it legitimately owns bhl, proving that
  CPU0 acquired the lock after the earlier `X` breadcrumb; it does not show a
  permanent leak or a different CPU holding the lock.

Verdict: the snapshot falsifies a permanently leaked bhl at the sampled time.
The `X` breadcrumb is a transient wait marker, not sufficient evidence of the
root cause.  Do not change spinlock semantics from this result.

### EXP-20260814-024 — demand-driven guest IRQ recovery heartbeat

Status: planned; artifact frozen before hardware launch
Created (UTC): 2026-08-14

Hypothesis: on T8103, a synthetic virtual IRQ represented by an LR plus HCR.VI
does not always provide a physical wake after Windows has entered idle.  The
static 1 kHz secondary heartbeat bounded this loss but imposed continuous EL2
overhead; the 100 Hz version restored responsiveness but exposed long stalls.
A 1 ms EL2 recovery tick only while INTID 17/18 is owned by an LR/deferred
delivery should preserve wake progress without polling idle vCPUs at 1 kHz.

Single changed variable relative to EXP-023/EXP-021:
- at the end of a physical FIQ, re-arm the local EL2 tick for 1 ms when that
  CPU still owns an undelivered guest physical/virtual timer interrupt;
- once Windows EOIs and rearms the guest timer, the existing 10 ms secondary
  and 10 ms runtime boot-CPU cadence resumes automatically.

Unchanged: WFI/WFE semantics, vGIC LR state machine, priorities, Mu, Windows,
NVMe, xHCI, display and all eight CPUs.

Expected checkpoint: boot reaches SSH without the long spinner pause; repeated
100 x 100 ms waits have no multi-second outlier; no 0x101, 0xa0, reset or
minute-scale UI freeze occurs.  Normal idle does not pay a continuous 1 kHz
heartbeat because the recovery cadence is conditional on live timer ownership.

Failure criterion: early bugcheck/reset, unchanged long pause, or evidence that
the recovery tick remains at 1 kHz after timer ownership clears.  Recovery is
immutable EXP-021; ESP remains unchanged.

Preflight result so far: the pure tick-policy test and root integration test
fail because the recovery rate/API and conditional FIQ re-arm do not exist.
This entry intentionally precedes implementation, artifact creation and launch.

Implementation and frozen artifact:
- m1n1 commit `0cad7b2` (`hv: bound pending guest timer wake latency`);
- the pure tick-policy test and root FIQ integration test failed before the
  recovery API/calls existed and passed afterward;
- complete nested C host suite, diff check and post-commit freestanding build
  passed;
- `investigation/artifacts/EXP-20260814-024/m1n1.macho`, SHA-256
  `95e0be58ef28430eb7ab9e06ba2a8d760d72ef9db87dd45d35ce4afd4f42eb8d`;
- unchanged Mu SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- manifest SHA-256
  `606c9f70db4465dd899351446a3e0c092b73eac75e9af77b1d7e27fb26e6aded`.

No hardware result is inferred from software verification.  This update is
recorded before the assisted launch.

Hardware result:
- all eight CPUs entered Windows; NVMe reached ready and xHCI routing was
  enabled, with no hypervisor reset or captured bugcheck;
- Windows remained materially slow and did not answer on its previously known
  SSH address during the bounded observation window;
- the non-destructive snapshot showed converged SGI accounting and no queued
  LR exhaustion, but CPU2 held INTID 18 `Active+Pending` while CPU3 held INTID
  18 `Pending`;
- the EXP-024 predicate armed the same 1 ms recovery tick for both states.
  `Pending` may require a physical wake before guest acknowledgement, whereas
  `Active` and `Active+Pending` mean Windows has already acknowledged the IRQ;
  polling EL2 at 1 kHz cannot finish that handler and adds avoidable latency.

Verdict: rejected as the final performance fix.  Demand-driven recovery is
retained, but ownership is too broad; EXP-025 narrows it to an unmasked,
Pending-only timer LR.

### EXP-20260814-025 — pending-only guest timer recovery wake

Status: planned; artifact frozen before hardware launch
Created (UTC): 2026-08-14

Hypothesis: EXP-024 removed continuous 1 kHz polling from completely idle
vCPUs, but continued polling at 1 kHz throughout `Active` and
`Active+Pending` timer handling.  Recovery is useful only while an unmasked
INTID 17/18 LR is exactly `Pending` and HCR.VI is asserted.  Once the guest has
acknowledged the IRQ, the normal 100 Hz secondary cadence must resume until a
new Pending-only delivery actually needs a wake.

Single changed variable relative to EXP-024:
- `hv_guest_timer_recovery_needed()` now scans the live LR bank and selects
  only a priority-deliverable, Pending-only INTID 17/18 while HCR.VI is set;
- Active and Active+Pending timer LRs no longer select the 1 ms recovery tick.

Unchanged: the 1 ms recovery interval itself, 100 Hz normal secondary cadence,
WFI/WFE semantics, LR level state machine, priorities, Mu, Windows, NVMe,
xHCI, display and all eight CPUs.

Expected checkpoint: all eight CPUs, NVMe and xHCI reach their established
markers; Windows reaches the login/desktop materially faster than EXP-024;
there is no long spinner pause, 0x101, 0xa0, reset or minute-scale freeze.

Failure criterion: early bugcheck/reset, unchanged slow boot, or repeated long
stalls with a Pending-only timer LR.  Recovery is immutable EXP-024; ESP
remains unchanged.

Preflight and frozen artifact:
- RED pure-policy and source-contract tests failed before the pending-only
  predicate existed and passed after implementation;
- m1n1 commit `ab3fda9` (`hv: limit recovery wake to pending timers`);
- complete nested C host suite, focused root vGIC contract suite, diff check
  and post-commit freestanding build passed;
- `investigation/artifacts/EXP-20260814-025/m1n1.macho`, SHA-256
  `4c21fe02bf45078949e9821f3b953ef2fc214851f62603d4691b2c6631800b5e`;
- unchanged Mu SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- manifest SHA-256
  `e1e83760881ae8616228da0d38ed49eec8aade1a29682493576eb41f27b1d058`.

No hardware result is inferred from software verification.  This entry is
recorded before the assisted launch.

Hardware result:
- all eight CPUs, NVMe and xHCI reached the expected markers with no captured
  reset or bugcheck;
- Windows remained slow and did not reach its previously known SSH address in
  the bounded interval;
- two non-destructive snapshots 20 seconds apart showed essentially frozen
  guest SGI/IAR/EOI progress.  A timer IAR/EOI advanced only adjacent to the
  diagnostic IPI, while Pending and Active+Pending INTID 18 LRs persisted.

Verdict: rejected as a root fix.  The narrower predicate removes unnecessary
recovery polling after acknowledgement, but the recovery source itself is not
proven to fire while the vCPU is stalled.  EXP-026 measures the host EL2 CNTP
state and arm/fire counts directly.

### EXP-20260814-026 — measure host recovery timer arm and expiry

Status: planned; artifact frozen before hardware launch
Created (UTC): 2026-08-14

Hypothesis: the normal/recovery EL2 CNTP is programmed but either does not
expire, is masked, or expires without producing guest timer progress.  Earlier
snapshots exposed only `CNTP_*_EL02`, which is the guest physical timer view;
they could not distinguish those cases.

Single changed variable relative to EXP-025:
- add per-CPU counters for normal tick arms, recovery tick arms and observed
  host CNTP expiries;
- publish `CNTP_CTL_EL0` and `CNTP_CVAL_EL0` alongside the existing guest
  `CNTP_*_EL02` state in the lock-free watchdog snapshot.

No scheduling, timer interval, vGIC, WFI/WFE, Mu, Windows, NVMe, xHCI, display
or CPU behavior is intentionally changed.

Expected checkpoint: two snapshots identify exactly one of three cases:
recovery is never armed, it is armed but host CNTP does not expire, or it
expires while the pending timer LR/IAR/EOI remains unchanged.

Failure criterion: diagnostic fields perturb boot or cannot distinguish arm
from expiry.  Recovery is immutable EXP-025; ESP remains unchanged.

Preflight and frozen artifact:
- the focused source-contract test failed before the fields/counters existed
  and passed after implementation;
- m1n1 commit `89d41fa` (`diag: measure host timer recovery progress`);
- complete nested C host suite, root vGIC contract suite, diff check and
  successful post-amend freestanding build passed;
- an earlier failed compile could not resolve `MAX_CPUS` through `hv.h`; its
  stale copied binary was detected by the unchanged EXP-025 hash and was
  overwritten.  The artifact below is from the successful post-amend build;
- `investigation/artifacts/EXP-20260814-026/m1n1.macho`, SHA-256
  `ee29ddccd0a96f6aff2994db5e1f33046280324bbd5ba04c3f591b0a8919f6be`;
- unchanged Mu SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- manifest SHA-256
  `0d005d1789327a134ce330288e9362b62fc492fa8fc32292f4b9bc1c8a39f2e5`.

No hardware result is inferred from software verification.  This entry is
recorded before the assisted launch.

Hardware result:
- all eight CPUs, NVMe and xHCI reached their expected checkpoints; no reset or
  bugcheck was captured during the bounded observation window;
- two non-destructive snapshots proved that the host `CNTP_EL0` recovery source
  is armed and expires.  On several secondary CPUs its arm/fire counters grew
  at nearly the 1-ms recovery cadence;
- deliverable Pending timer LRs persisted while the recovery source repeatedly
  fired.  Every such virtual-pending secondary FIQ was forced into the legacy
  slow path and global `bhl`, creating thousands of serialized EL2 entries per
  second across the guest;
- Windows remained slow.  The operator later reported an
  `INTERNAL_POWER_ERROR`; its exact parameters were not captured by this run,
  so it is not attributed as a measured EXP-026 bugcheck.  A prior captured
  `0xA0 / P1=0x618` has the Microsoft-defined meaning "runtime power worker
  blocked too long" and is consistent with, but does not by itself prove, this
  contention mechanism.

Verdict: diagnostic succeeded; rejected as a performance fix.  The missing
event is not the host recovery timer.  EXP-027 removes already-completed local
virtual-interrupt delivery from the global lock path while preserving the live
LR, freshly recomputed HCR.VI and recovery safety net.

### EXP-20260814-027 — return completed local virtual IRQs without global bhl

Status: planned; artifact frozen before hardware launch
Created (UTC): 2026-08-14

Hypothesis: Windows stalls and runtime-power watchdogs because every deliverable
secondary timer IRQ enters the global `bhl` slow path.  EXP-026 measured nearly
continuous 1-ms recovery FIQs on several vCPUs; serializing each one can starve
guest scheduling and power workers even though timer state itself is valid.

Single changed variable relative to EXP-026:
- after local guest-timer/IPI handling and live-LR `HCR.VI` recomputation, allow
  an otherwise eligible secondary to return directly even when a virtual IRQ
  is pending.  Physical FIQs, rendezvous, proxy CPU switches and the
  interruptible CPU retain the serialized path.  Pending timers retain the
  short recovery wake until Windows acknowledges/rearms them.

No timer level-state machine, recovery interval, Mu, NVMe, xHCI, display, CPU
topology, Windows installation, WFI mode or guest memory layout is changed.

Expected checkpoint: normal-duration boot and responsive desktop without the
20-second/minute stalls, `CLOCK_WATCHDOG_TIMEOUT`, or `INTERNAL_POWER_ERROR`.
The host recovery counters may still advance, but completed secondary local
FIQs must no longer contend for `bhl` merely because HCR.VI is asserted.

Failure criterion: early watchdog/reset, lost timer delivery, unchanged slow
boot, or a long stall.  Recovery is immutable EXP-026; ESP remains unchanged.

Preflight and frozen artifact:
- the focused fast-path test failed before Pending virtual IRQs were removed
  from the completion predicate and passed after implementation;
- m1n1 commit `7e84a84` (`hv: keep local virtual IRQs off the global lock`);
- complete nested C host suite, focused root vGIC contract suite, diff check and
  successful post-commit freestanding build passed;
- `investigation/artifacts/EXP-20260814-027/m1n1.macho`, SHA-256
  `d83b0cfa7d44cd5f83b4cf927918b20d7696dd5167692587f179879c02406515`;
- unchanged Mu SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- manifest SHA-256
  `aa9bead3a0928c4e1588428d8020da0d6e55eb17df453f444165838cfedb9d0a`.

No hardware result is inferred from software verification.  This entry is
recorded before the assisted launch.

Preliminary hardware observation (final operator verdict pending):
- the exact frozen artifact reported m1n1 `7e84a84`, reached NVMe ready, xHCI
  route enable and guest runtime cadence; a lock-free snapshot contained live
  state for all eight vCPUs;
- Windows reached its known network address and port 22 responded, although
  the post-reinstallation host key no longer authorized the existing SSH key;
- no bugcheck, reset or hypervisor exception was captured during the initial
  observation plus a subsequent 45-second passive window;
- desktop responsiveness and sustained stability remain unclaimed until the
  operator reports the physical-screen result and a longer workload passes.

Final operator result:
- Windows booted, but was extremely slow and froze immediately after reaching
  the desktop.  The target later returned to proxy and the assisted runner lost
  its USB device; no successful sustained session was observed.
- A lock-free snapshot measured roughly 29,000 host tick expiries and 35,000
  separate recovery-timer arms on several secondaries.  CPU4 and CPU6 did not
  emit their Windows `CPU_ENTRY` diagnostics until after the manual snapshot.
- Five preserved Windows dumps from the same regression family decode as
  `CLOCK_WATCHDOG_TIMEOUT (0x101)` with hung CPU indices 3, 1, 5, 2 and 5.  The
  failure is therefore not tied to one physical core; arbitrary secondary CPUs
  stop processing clock interrupts.

Verdict: rejected.  Letting Pending virtual IRQs bypass the global lock did not
repair delivery and retained the near-continuous 1-ms recovery source.  EXP-028
returns to the accepted virtual-IRQ exit contract and disables that source while
retaining only the independently validated secondary-mailbox wake correction.

### EXP-20260814-028 — accepted timer exit contract plus race-safe SMP wake

Status: planned; software verification complete, hardware not launched
Created (UTC): 2026-08-14

Hypothesis: the responsiveness regression is caused by the post-baseline 1-ms
guest-IRQ recovery source and modified secondary FIQ early-return policy, not by
the independently validated dual IPI+SEV secondary-mailbox fix.  Disabling the
extra recovery source and restoring the accepted rule that both physical FIQ and
HCR.VI must be clear before the abbreviated return will remove continuous EL2
work without restoring the intermittent secondary-start race.

Single changed runtime variables relative to rejected EXP-027:
- guest IRQ recovery tick rate: 1000 Hz -> disabled;
- secondary fast completion again requires no physical FIQ and no HCR.VI;
- the extra fast-boundary `hv_vgic3_update_vi()` scan is removed;
- Apple-input passthrough is disabled for this A/B build only.

Unchanged:
- race-safe secondary mailbox publication using targeted IPI plus SEV and
  unconditional physical IPI acknowledgement;
- eight CPUs, 100 Hz ordinary secondary heartbeat, Mu, ACPI, NVMe, xHCI,
  physical display, Windows installation and guest memory layout;
- assisted launch, one proxy owner and monitor diagnostics.

Software evidence:
- RED: focused fast-path tests failed to compile with the accepted three-input
  completion contract and the tick-policy test failed at recovery rate 1000;
- GREEN: both focused tests passed after the minimal change;
- complete nested C host suite passed;
- focused secondary-start Python suite passed 10/10;
- focused root vGIC contract suite passed 15/15 and both diffs passed checks;
- clean freestanding build with `APPLE_INPUT=0` completed successfully and
  `build/build_cfg.h` contains `HV_DISABLE_APPLE_INPUT`.

Recorded artifacts:
- m1n1 commit `2ae94afc82bbf7667c9959f47bf515b381288343`;
- `investigation/artifacts/EXP-20260814-028/m1n1.macho`, SHA-256
  `d57df4625b3f5e0e760b2c54dc79565d96e53d051d420a43e437974a663816ae`;
- unchanged Mu firmware SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`.

Expected checkpoint: normal-duration boot, CPUs 0 through 7 enter exactly once,
pointer and desktop remain responsive, recovery-arm counters stay zero, and no
long pause, `0x101`, `INTERNAL_POWER_ERROR`, reset or lost SSH occurs during the
bounded interactive test.  ESP remains unchanged; this is assisted-only.

Hardware result:
- all CPUs 0 through 7 entered Windows independently before any diagnostic
  snapshot, NVMe and xHCI reached their ready checkpoints, and every recorded
  guest-IRQ recovery-arm counter remained zero;
- Windows then stopped with `DPC_WATCHDOG_VIOLATION (0x133)`, parameter 1 equal
  to 1.  This parameter means cumulative time at DISPATCH_LEVEL or above
  exceeded the watchdog period, rather than one isolated long-running DPC;
- the reset that followed was a Windows PSCI reset.  No EL2 exception or m1n1
  panic preceded it.

Verdict: rejected as a usable build, but the experiment isolated the next
regression layer.  Continuous 1-ms recovery work was removed successfully; the
post-baseline timer level-state synchronizer still differs from the accepted
morning path and is the only timer-delivery variable removed by EXP-029.

### EXP-20260814-029 — accepted timer delivery plus race-safe SMP wake

Status: rejected after hardware test
Created (UTC): 2026-08-14

Hypothesis: the post-baseline timer level-state synchronizer keeps Windows at
elevated IRQL long enough to trigger cumulative DPC watchdog violations.  The
accepted morning timer latch/re-pend path did not contain that synchronizer.
Restoring it while retaining only the independently proven secondary-mailbox
wake correction should recover the responsive baseline without restoring the
secondary-start race.

Single changed runtime variable relative to rejected EXP-028:
- timer delivery is restored to the accepted `timer_irq_outstanding` latch,
  `timer_repend_live_irq` and maintenance-drain implementation;
- the later `hv_sync_timer_level` state machine and timer-specific EOI drain are
  removed from production.

Unchanged:
- targeted IPI plus SEV mailbox publication and unconditional physical IPI
  acknowledgement;
- disabled 1-ms guest-IRQ recovery source and accepted secondary fast-exit
  predicate;
- eight CPUs, ordinary 100-Hz secondary heartbeat, Mu, ACPI, NVMe, xHCI,
  physical display, Windows installation, guest memory layout and Apple Input
  disabled for this A/B build.

Software evidence:
- RED: focused source-contract tests failed at the level-state synchronizer and
  timer-specific EOI drain;
- GREEN: focused root vGIC tests passed 15/15, the complete nested C host suite
  passed, focused SMP Python tests passed 10/10, and both diffs passed checks;
- m1n1 commit `7f8061c` (`hv: restore accepted timer delivery path`);
- clean freestanding `APPLE_INPUT=0` build SHA-256
  `df8c9127b191317d8ea4ddd795f0e95834eff436b7b4a910c8fa790d90d77efd`.

Expected checkpoint: Windows reaches the login/desktop in normal time, physical
pointer motion is smooth, all CPUs remain live, recovery-arm counters remain
zero, and neither `0x101`, `0x133`, `INTERNAL_POWER_ERROR`, spontaneous reset nor
a long global pause occurs.  ESP remains unchanged; assisted launch only.

Hardware result:
- all CPUs 0 through 7 entered Windows, NVMe and xHCI reached their runtime
  checkpoints, no recovery tick was armed, and no EL2 exception or immediate
  bugcheck occurred;
- Windows reached the lock screen, then repeatedly paused for tens of seconds.
  The operator could enter a password only between pauses; SSH never became
  reachable;
- two in-place snapshots proved that the host heartbeat and guest IAR/EOI
  counters advanced on every CPU, queues remained empty, and the global lock
  was not contended.  The guest therefore was not a dead hypervisor or a single
  permanently stopped secondary;
- the log marks the exact runtime transition `boot tick 1000Hz -> 100Hz`.  This
  policy is not present in accepted baseline `55531e9`, which keeps CPU0 at
  5000 Hz while only the secondaries use the sparse 100-Hz cadence.

Verdict: rejected.  Restoring the accepted timer delivery removed the early
`0x133`, but did not restore responsiveness because the CPU0 service cadence
was still a post-baseline runtime change.  EXP-030 restores that final runtime
difference while retaining the proven SMP wake correction.

### EXP-20260814-030 — exact accepted cadence plus race-safe SMP wake

Status: rejected after hardware test
Created (UTC): 2026-08-14

Hypothesis: reducing CPU0's EL2 service cadence from the accepted fixed 5000 Hz
to 1000 Hz during boot and 100 Hz at guest runtime allows global guest progress
to occur only in bursts.  Restoring the fixed accepted CPU0 cadence while
leaving secondaries at 100 Hz should remove the lock-screen pauses without the
all-core polling overhead that earlier 1000-Hz secondary experiments caused.

Single changed runtime variable relative to rejected EXP-029:
- CPU0 boot and runtime service cadence: 1000/100 Hz -> fixed 5000 Hz.

Unchanged:
- secondary cadence 100 Hz on T8103, guest IRQ recovery disabled, accepted
  timer latch/re-pend delivery, and race-safe IPI+SEV mailbox publication;
- eight CPUs, Mu, ACPI, NVMe, xHCI, physical display, Windows installation,
  guest memory layout and Apple Input disabled for this A/B build.

Expected checkpoint: Windows crosses the lock screen without a long pause,
reaches a responsive desktop, and remains free of `0x101`, `0x133`, power
bugchecks, spontaneous reset and minute-scale freezes.  ESP remains unchanged.

Software evidence:
- RED: focused tick-policy test failed because boot/runtime CPU0 rates were
  still 1000/100 Hz rather than the accepted fixed 5000 Hz;
- GREEN: focused test and complete nested C host suite passed; root vGIC suite
  passed 15/15, focused SMP Python suite passed 10/10, and both diffs passed;
- m1n1 commit `f4dfb0e` (`hv: restore accepted CPU0 service cadence`);
- clean freestanding `APPLE_INPUT=0` build SHA-256
  `847412d2c8e1ebe66052fc0594d394bfd8c3aa5d8999e59d91692219c02423d6`.

Hardware result:
- the artifact reported the required fixed `boot tick 5000Hz -> 5000Hz`, all
  eight CPUs entered, NVMe/xHCI completed, and Windows reached both the network
  and an authenticated SSH session;
- boot progressed faster than EXP-029, but the guest again paused.  SSH stopped
  before the first read-only command and later recovered after a physical
  rendezvous snapshot;
- the freeze snapshot showed CPU1 executing a user virtual address while the
  other CPUs were in Windows idle paths.  Every CPU's host heartbeat and guest
  IAR/EOI counters remained live, queues were empty, recovery remained zero,
  and no EL2 exception or bugcheck occurred.

Verdict: fixed 5000-Hz CPU0 cadence improves boot progress but is not sufficient
to eliminate the pause.  The monitor image samples a large watchdog record and
all virtual LRs every 64 ticks; at 5000 Hz this is about 78 heavy samples per
second on CPU0.  EXP-031 tests the identical runtime in RELEASE, where that
monitor-only hot path must be truly zero-cost.

### EXP-20260814-031 — production runtime without monitor hot-path sampling

Status: software-verified; frozen artifact pending clean provenance manifest
Created (UTC): 2026-08-14

Hypothesis: the clean installer/release profile was visibly smoother because it
did not require monitor snapshots, but the implementation still executes the
snapshot sampler regardless of `RELEASE`.  At the restored 5000-Hz CPU0 cadence
this repeatedly reads architectural timer registers, all LRs and publishes a
large record.  Returning before even the sample counter in RELEASE should make
production diagnostics zero-cost while preserving the monitor build unchanged.

Single source variable relative to rejected EXP-030:
- `hv_watchdog_snapshot_tick()` returns immediately when runtime diagnostics
  are disabled.

Build/profile variable required by the hypothesis:
- `RELEASE=1`, `APPLE_INPUT=0`, assisted physical display, debug off.

Unchanged:
- fixed CPU0 5000 Hz, secondary 100 Hz, accepted timer delivery, disabled
  recovery source, race-safe SMP wake, Mu, NVMe, xHCI and Windows installation.

Expected checkpoint: fast lock-screen/desktop entry and continuous pointer/SSH
responsiveness without monitor sampling overhead, BSOD or reset.  This build
cannot provide an in-place watchdog snapshot by design; failure is judged from
the foreground launcher lifecycle, physical screen and network continuity.

Software evidence:
- RED: the root source-contract test failed while the release path still
  incremented the snapshot sampler before checking runtime diagnostics;
- implementation commit `f397d93abbebb0444df731b55148318652db3228` returns
  before the sampler when runtime diagnostics are disabled;
- GREEN: root vGIC contract suite 16/16, focused release/watchdog/fast-path/tick
  tests, and the complete nested host suite passed;
- clean `RELEASE=1 APPLE_INPUT=0` build completed and produced m1n1 SHA-256
  `0ed330e5da17e0c64b35c2ce46efd41985c405a163bc416c19cf455528e63c77`;
- `build_cfg.h` contains both `RELEASE` and `HV_DISABLE_APPLE_INPUT`.

Hardware result: observation-incomplete.  The clean artifact reached all eight
Windows CPUs, NVMe controller reinitialisation and xHCI discovery without a
recorded reset or EL2 exception.  The known Windows address did not answer on
the network during the bounded interval, but the physical-only profile provided
no independent framebuffer evidence to distinguish a slow boot from a frozen
logo, spinner, login screen or desktop.  This run is not used to accept or
reject the runtime hypothesis.  All subsequent problem investigation must use
the guarded `--observed` launcher mode and `display=both`.

### EXP-20260814-032 — zero-cost release runtime with mandatory framebuffer observation

Status: rejected as a performance verdict; observer transport failed and guest froze
Created (UTC): 2026-08-14

Purpose: repeat EXP-031 without changing m1n1, Mu, Windows, CPU cadence, timer
delivery, storage or USB.  The sole profile change is `physical` -> `both`, so
the same guest framebuffer is consumed by the internal DCP scanout and the
asynchronous USB viewer.  This closes the Windows-logo-to-network blind spot.

Process guard:
- canonical launch uses `--observed`, which defaults to `display=both`, starts
  the viewer at `http://127.0.0.1:8766/`, and rejects an explicit non-`both`
  display;
- code commit `4abdf3996b319b7dd498b734a0a8591d922522f1`;
- RED test rejected the previously unknown `--observed` option; GREEN focused
  test and complete public launcher suite 34/34 passed.

Unchanged artifact payloads:
- m1n1 commit `f397d93abbebb0444df731b55148318652db3228`, SHA-256
  `0ed330e5da17e0c64b35c2ce46efd41985c405a163bc416c19cf455528e63c77`;
- Mu SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- `RELEASE=1`, `APPLE_INPUT=0`, debug off, CPU0 5000 Hz and secondaries 100 Hz.

Expected checkpoint: the viewer visibly progresses through firmware, Windows
logo/spinner, login and desktop; frame generation remains live during animation;
physical display shows the same surface; network becomes available; pointer and
SSH remain continuously responsive without a BSOD, reset or long pause.

Failure classification: a stale viewer generation while the physical screen is
also unchanged is a guest/transport freeze; a live viewer with a stale physical
panel is a DCP issue; a stale viewer with a changing physical panel is a USB
observation failure and does not classify Windows runtime behavior.

Hardware result:
- the unchanged release payload reached all eight CPUs, NVMe and xHCI, and the
  internal DCP surface progressed through firmware and Windows to the lock
  screen;
- the operator observed Windows freeze at the lock screen and the known guest
  address did not answer a bounded SSH probe, so this was not merely a stale
  physical panel;
- the USB viewer stopped earlier at Windows boot logo generation 31/frame 30;
  the sole proxy owner then reported repeated corrupted framebuffer events with
  `type=3 length=16348 wire=0x00000000` followed by long NUL runs;
- 16348 is exactly the data length of a maximum framebuffer chunk: the 28-byte
  framebuffer header plus the current 16320-byte payload.  The complete proxy
  event is 16360 bytes, only 24 bytes below the 16-KiB DWC3 transfer boundary.

Verdict: the Windows freeze is real, but EXP-032 cannot classify its cause or
measure release responsiveness because the mandatory observer itself lost USB
framing before the freeze.  Fix and validate the observer transport before the
next CPU/timer experiment.  Do not use this run to accept or reject the
zero-cost release hypothesis.

### EXP-20260814-033 — bounded framebuffer events with unchanged guest runtime

Status: observer transport confirmed; guest runtime rejected after reproduced freeze
Created (UTC): 2026-08-14
Completed (UTC): 2026-08-14T15:30:00Z

Hypothesis: the framebuffer observer loses CDC framing because its maximum
16360-byte proxy event leaves only 24 bytes below the 16-KiB DWC3 transfer
boundary.  Keeping complete framebuffer events below 4 KiB should preserve the
same frames and cadence while allowing the sole proxy owner and web viewer to
remain synchronized through the Windows freeze.

Single changed variable relative to EXP-032:
- framebuffer payload per proxy event: 16320 -> 4032 bytes; complete maximum
  event: 16360 -> 4072 bytes.

Unchanged:
- m1n1 release runtime, CPU0 5000-Hz cadence, secondary 100-Hz cadence, timer
  delivery, Mu, Windows, NVMe, xHCI, eight CPUs, Apple Input disabled, debug
  off and display `both`;
- ESP remains unchanged and the run is assisted through the sole proxy owner.

Software evidence:
- RED: `hv_fb_stream_usb_limit_test` aborted because the old 16360-byte event
  exceeded the literal 4096-byte safe budget;
- implementation commit `72b2aab8a6089b2099242f3bdb4a8cfd08e1113b` reduces
  only `HV_FB_STREAM_PAYLOAD_SIZE` to 4032 bytes;
- GREEN: focused framebuffer, USB-limit and proxy-event tests passed; the
  complete nested host suite passed; root display/receiver/launcher suite
  passed 54/54;
- clean Docker release build completed with `RELEASE=1` and
  `HV_DISABLE_APPLE_INPUT` in `build_cfg.h`.

Provenance and commands:
- root commit `c471d6cba22ca012a8cf7df9af6b94bc8e82ad78`, m1n1
  commit `72b2aab8a6089b2099242f3bdb4a8cfd08e1113b`, Mu commit
  `63942398cccbd98127cfecbd7f936af99c837d6f`;
- build: `docker run --rm -v /Users/pavel/public_windows:/work -w
  /work/m1n1_windows windows-on-m1-build:local sh -lc 'make clean && make
  -j8 RELEASE=1 APPLE_INPUT=0'`;
- artifact directory `investigation/artifacts/EXP-20260814-033`, m1n1 SHA-256
  `aff6ffc54594ac41b2841fc4bada47c0a39e908bfcba15f74bf788ec5ca0b932`,
  unchanged Mu SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`,
  manifest SHA-256
  `c714885a1a5aa52ef598575e68c8274213ecb667dd694908b08d468dc7af3d5d`;
- launch: `./scripts/run-windows.sh --execution assisted --observed --debug
  off --proxy /dev/cu.usbmodemC02HDNCCQ6L41 --vuart
  /dev/cu.usbmodemC02HDNCCQ6L43 --firmware
  investigation/artifacts/EXP-20260814-033/J313_EFI.fd --m1n1
  investigation/artifacts/EXP-20260814-033/m1n1.macho --chainload
  --foreground`;
- recovery artifact: the existing ESP remains unchanged; terminate the sole
  assisted runner and reboot to return to Stage 1.

Expected checkpoint: physical and web displays both advance through firmware,
Windows logo and lock screen without proxy checksum errors or NUL runs.  If
Windows freezes, the web viewer must stop on the same visible state and the
sole launcher must retain valid framing so the next CPU/timer diagnosis can
use trustworthy evidence.

Failure criterion: any checksum error, parser desynchronisation, stale web
frame while the physical display continues, EL2 exception, reset, or inability
to reach the same Windows state as EXP-032.

Hardware result:
- the recorded hashes and manifest passed preflight from a freshly probed Stage 1;
  m1n1 `72b2aab` chainloaded once and remained the sole proxy/event owner;
- the J313 hardware reached the guest handoff, all seven secondary PSCI entries,
  NVMe ready and the xHCI route-enable checkpoint with Apple Input disabled;
- the bounded observer remained correctly framed throughout the run.  Metadata
  advanced from generation 16/frame 15 to generation 75/frame 74 without a
  checksum error, a zero-wire checksum, a NUL run or parser desynchronisation;
- the published framebuffer itself remained byte-identical at CRC32
  `0x99875e96` for more than two minutes and showed the Windows boot logo and
  spinner.  The known guest address `192.168.1.35` answered neither ICMP nor a
  bounded TCP/22 probe;
- no Windows bugcheck, EL2 exception, spontaneous reset or PSCI reset occurred
  before recovery.  Physical display, internal input, RDP and Windows CPU
  enumeration were not independently observed in this clean run and are not
  inferred;
- SIGTERM requested the documented final snapshot/reboot recovery.  The snapshot
  reported `HV WATCHDOG BHL: owner=0 count=1`, the same expected CPU0-owned state
  classified by EXP-023; it is not evidence of a leaked global lock.  The target
  returned to Stage 1 and a post-recovery probe succeeded.

Evidence:
- `investigation/artifacts/EXP-20260814-033/evidence/freeze-fb-info.json`,
  SHA-256 `390909f418fa05ef030c6bea97f83a5a46a870f7266bc6bd585b9a9db44e14ff`;
- `investigation/artifacts/EXP-20260814-033/evidence/freeze-fb.raw`, SHA-256
  `cdbfca1d7d5370ff64fc999c807efa85048ad951c56dd573eb7ffdbf263ae08a`;
- `investigation/artifacts/EXP-20260814-033/evidence/freeze-frame.png`, SHA-256
  `ab3689cd9b2a4a97059eb4e548e9462f492254573169e30509bdb7b4e02f6340`.

Verdict: confirmed for the observer-transport hypothesis and rejected as a
usable Windows runtime.  Keeping complete proxy events below 4 KiB eliminates
the EXP-032 CDC framing failure, while the unchanged release guest still freezes
with a correct, continuously published but byte-identical Windows framebuffer.
This independently excludes Apple Input (compiled out), a stale physical-only
DCP surface, and observer corruption as the root cause.  The next experiment
must retain the validated 4-KiB observer while collecting a bounded, zero-hot-path
CPU/vGIC/timer state transition at the first unchanged-frame interval.

### CORRECTION-20260814-003 — EXP-033 did not fully exclude Apple Input

EXP-033 compiled `HV_DISABLE_APPLE_INPUT` into m1n1, which skipped the
Apple-input ADT preflight, stage-2 identity mappings and physical-to-virtual IRQ
route.  It did **not** remove `AINP` from the unchanged Mu DSDT: firmware still
reported `_HID=APPL0001` and `_STA=0x0F`.  Therefore an already installed
Windows driver could still bind and perform its prepare-hardware MMIO reads.
The sentence claiming that EXP-033 independently excluded Apple Input is
superseded by this correction.  Observer validation remains valid.

### EXP-20260814-034 — true AINP-enumeration exclusion control

Status: confirmed build-pipeline defect; no hardware launch
Created (UTC): 2026-08-14T15:38:05Z
Completed (UTC): 2026-08-14T15:45:00Z

Hypothesis: the regression that began with the J313 keyboard/trackpad work is
caused by Mu advertising `AINP` as present before the Apple SPI3 power, clock,
pinctrl and reset contract is owned by any layer.  An installed Windows
`ACPI\\APPL0001` driver can consequently bind and read SPI/GPIO MMIO even when
m1n1 was built with `APPLE_INPUT=0`.  Removing only the ACPI enumeration should
allow the otherwise identical EXP-033 guest to progress past the frozen Windows
logo.

Single changed variable relative to EXP-033:
- Mu DSDT no longer includes `J313AppleInput.asl.inc`; m1n1, timer/vGIC policy,
  CPU count, Windows disk, NVMe, xHCI, display and observer are unchanged.

Source-first evidence inspected before build:
- live J313 ADT/register read is the first planned diagnostic command below;
- Asahi Linux `arch/arm64/boot/dts/apple/t8103.dtsi` describes SPI3 at
  `0x23510c000`, IRQ 617, a 120-MHz clock, SPI3 pinctrl and `ps_spi3` power
  domain; `t8103-j313.dts` adds the SPI HID transport, AP GPIO 195 and active-low
  nub GPIO interrupt 13;
- Asahi `drivers/spi/spi-apple.c` enables the controller clock before register
  initialization and initializes/reset FIFOs before registering the bus;
- current m1n1 `src/hv_apple_input.c` observes ADT, maps the three MMIO ranges
  and registers a level route, but does not own power, clock, pinctrl or reset;
- current Mu `DSDT.asl` unconditionally includes `AINP` with `_STA=0x0F`;
- current Windows AppleInput driver binds `ACPI\\APPL0001` and reads SPI/GPIO
  registers in `EvtDevicePrepareHardware`; it has no KMDF interrupt object or
  runtime DPC loop;
- official Arm generic-timer guidance, Mu GTDT/AIC sources and Microsoft
  `0x101`/`0x133` documentation were also checked because earlier failures were
  timer-shaped; Mu's only change since the pre-input commit is this DSDT include.

Observed ownership contract before the experiment:
- Mu owns ACPI enumeration and must not report a device usable until its
  dependencies are prepared;
- m1n1 currently owns stage-2 visibility and IRQ virtualization only;
- Windows would own SPI transactions, HID protocol and runtime recovery after
  a supported prepare/start contract exists;
- no current layer owns SPI3 power/clock/pinctrl/reset preparation for Windows.

Source contract before artifact build:
- root `/Users/pavel/public_windows`, branch
  `codex/canonical-public-release`, commit
  `c324955b87b0d4a04c26ba1ac4eb26961c011995`; tracked source outside the
  append-only ledgers is unchanged;
- m1n1 commit `72b2aab8a6089b2099242f3bdb4a8cfd08e1113b`, clean tracked
  diff SHA-256
  `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`;
- Mu commit `63942398cccbd98127cfecbd7f936af99c837d6f`, one experimental
  tracked DSDT diff SHA-256
  `60a5e432ff53016969382c268fa2ca62eeaba9dcb6b3529db61e2f3a5583a41f`;
  existing nested checkout markers are not source changes.

Live evidence before build (read-only, Stage 1):
- the first diagnostic fetched the complete live ADT but stopped after using a
  nonexistent `ProxyUtils.read32` method; it made no controller-MMIO access;
- the corrected read observed `/arm-io/spi3` compatible `spi-1,spimc`, range
  `0x23510c000+0x4000`, child compatible `hid-transport,spi`, AP GPIO phandle
  106/pin 195, nub GPIO parent phandle 108 and interrupt pin 13;
- PMGR device `SPI3` resolves to pstate register `0x23b700258`, value
  `0x000000ff`: desired `0xf`, actual `0xf` (active) in the inherited Stage-1
  state;
- direct SPI controller reads were deliberately skipped: the PMGR observation
  is sufficient to disprove a simple already-powered-off explanation without
  risking an unowned peripheral access.  The narrower remaining hypothesis is
  Windows binding/prepare activity or a later ownership transition, not merely
  an SPI3 domain that was off at this sample.

Exact build command:

```sh
STANDALONE_BUILD_MU_ONLY=1 ./scripts/build-standalone.sh \
  --debug-build --display both --debug off
```

Build checkpoint: the first sandboxed attempt failed before build because Docker
API access was denied.  The approved retry compiled a new `DSDT.aml` without
`APPL0001`, but EDK2's incremental dependency graph left the final FD at its old
August 8 timestamp and old SHA-256 `0dba13c...`; that cached FD is rejected and
must not be launched.  The planned retry adds only Stuart's supported `--clean`
flag to the same DEBUG Mu build:

```sh
docker run --rm -e STANDALONE_IN_CONTAINER=1 \
  -v /Users/pavel/public_windows:/work -w /work/mu \
  windows-on-m1-build:local \
  /work/.build/mu-venv/bin/stuart_build --clean \
  -c Platform/MacBookAirMid2020Pkg/PlatformBuild.py \
  TOOL_CHAIN_TAG=CLANGPDB TARGET=DEBUG 'BLD_*_AIC_BUILD=FALSE'
```

Planned artifact:
- directory `investigation/artifacts/EXP-20260814-034`;
- unchanged m1n1 copied from EXP-033, SHA-256
  `aff6ffc54594ac41b2841fc4bada47c0a39e908bfcba15f74bf788ec5ca0b932`;
- experimental `J313_EFI.fd` and manifest hashes will be recorded after build
  and before launch;
- recovery artifact: unchanged ESP/Stage 1 plus EXP-033 assisted artifacts.

Exact launch command:

```sh
./scripts/run-windows.sh --execution assisted --observed --debug off \
  --proxy /dev/cu.usbmodemC02HDNCCQ6L41 \
  --vuart /dev/cu.usbmodemC02HDNCCQ6L43 \
  --firmware investigation/artifacts/EXP-20260814-034/J313_EFI.fd \
  --m1n1 investigation/artifacts/EXP-20260814-034/m1n1.macho \
  --chainload --foreground
```

### EXP-20260814-038 result — LR state preserved and timer stall classified

Completed (UTC): 2026-08-14T16:43:09Z
Status: confirmed diagnostic contract and confirmed root cause

The exact pre-recorded artifact reported m1n1 `3aeef41`; all eight CPUs, NVMe,
guest runtime and xHCI reached their checkpoints.  Windows then remained on the
eight-second disk-check countdown while observer generations advanced from 43
to 64 and the complete framebuffer remained byte-identical at SHA-256
`53b7558e0a93482b4a10570e2efa73a5f1e01eff24278b703965a6106150efa8`.
TCP/22 remained unavailable.  Two explicit diagnostic boundaries advanced the
guest from that timer-dependent screen to black, after which observer generations
continued to 120 with a second byte-identical framebuffer SHA-256
`6992296c77327bc9aaab7ca4758501ce5d2bd2e3c1ec7050f400881ed9ffbdcb`.

The bounded dump change succeeded: every CPU record contains `lrc=8` and
`lr0..lr7`.  Two late snapshots more than ten seconds apart independently show:
- every CPU host tick counter advanced, queues were empty and bhl was not stuck;
- `CNTV_CTL=0x5` and `vinj=1` on the stalled vCPUs: the virtual timer remained
  enabled and expired while m1n1 still considered delivery owned;
- CPUs 1, 2, 4, 6 and 7 held pure Pending INTID 18 LRs
  (`0x5020020000000012`) and HCR.VI was asserted where sampled;
- CPUs 0, 3 and 5 held Active-only INTID 18 LRs
  (`0x9020020000000012`) with HCR.VI clear.  Those Active-only values and stale
  timer IAR/EOI timestamps persisted across both late snapshots;
- therefore the accepted latch's `timer_*_injected == true` branch incorrectly
  treats any live LR as sufficient.  It never reflects the still-asserted timer
  level into the architecturally required Active+Pending state, so EOI cannot
  expose a next Pending interrupt and Windows can wait indefinitely for time.

Preserved evidence:
- `investigation/artifacts/EXP-20260814-038/evidence/hv.log`, SHA-256
  `3429fb6cb7bceb28844b62808d09a1fa9a0f0a4ebd1fa16db1eeecb683882188`;
- `frame-a.raw` and `frame-b.raw`, both SHA-256 `53b7558e...`;
- `frame-c.raw`, `frame-d.raw` and `final-frame.raw`, all SHA-256
  `6992296c...`;
- `final-meta.json`, SHA-256
  `f344216fc22668196f7ee46be62cd423122702fe6d6e8d4489d982ebe0eebec1`.

Verdict: EXP-037's ambiguous timer-delivery hypotheses are resolved.  The
freeze is not caused by Apple Input, observer transport, a global hypervisor
lock, host-tick loss, LR scarcity or a missing timer LR.  It is a violated
level-sensitive PPI state transition in m1n1's vGIC owner: asserted+Active must
become Active+Pending, and deassertion must withdraw that Pending state.  The
diagnostic formatting change is hardware-validated; it is not itself the fix.

Recovery succeeded after the recorded SIGTERM boundary.  Installed Stage 1
`b791225` answered the live probe and reported eight CPUs and 8.0 GiB DRAM.

### EXP-20260814-039 — synchronize live virtual-timer LR level

Status: planned
Created (UTC): 2026-08-14T16:43:09Z

Hypothesis: synchronizing only the live virtual-timer INTID 18 LR with the
sampled CNTV assertion will close the demonstrated stall without restoring the
rejected 1-ms recovery source, changing CPU cadence, or replacing the accepted
timer queue/latch architecture.  Asserted+Active becomes Active+Pending;
deasserted+Active+Pending becomes Active.  Pending, Active+Pending and unrelated
LRs remain unchanged.

Source-first contract inspected:
- live J313 EXP-038 records above are the primary evidence and show the same
  Active-only INTID 18 on CPUs 0, 3 and 5 twice while `CNTV_CTL=0x5`;
- Arm GICv3/v4 Software Overview DAI0492 section 4.2 defines Pending, Active and
  Active+Pending and requires a level-sensitive source to retain its asserted
  state until the peripheral deasserts; the Generic Timer is a per-PE PPI;
- current m1n1 `src/hv_exc.c` masks Apple's virtual-timer FIQ route after expiry,
  then skips LR mutation whenever `timer_v_injected` is already true;
  `src/hv_vgic_diag.c` already has a tested, currently production-unused
  `hv_vgic_diag_sync_level_lr()` transition primitive;
- historical m1n1 `2ead84d` proved Active+Pending and advancing IAR/EOI on all
  CPUs in EXP-019, but it also replaced the queue, both INTID 17 and 18 paths,
  deactivation drains and ownership model.  EXP-039 does not restore that broad
  patch;
- Mu exposes the standard Arm Generic Timer/GIC contract to Windows; Microsoft
  documents that Windows uses built-in Arm Generic Timer support through GTDT.
  Mu, ACPI and Windows do not own m1n1's LR state machine and will not change;
- the Asahi/Linux side uses the architectural per-CPU Generic Timer/GIC PPI
  contract and contains no J313-specific substitute for virtual LR ownership.

Ownership: Windows programs CNTV and acknowledges/EOIs INTID 18; m1n1 owns the
Apple FIQ route, synthetic vGIC LR state, VI output and recovery from LR state;
Mu only describes the standard timer.  The correction therefore belongs in
m1n1's local timer-to-vGIC boundary.

Single changed runtime variable relative to EXP-038:
- reflect CNTV asserted/deasserted level into an already-live INTID 18 LR using
  the existing four-state helper.  Do not change INTID 17, deferred queue
  ownership, tick rates, fast-return policy or diagnostics.

TDD checkpoint: first require the production virtual-timer asserted branch to
synchronize a live LR even while `timer_v_injected` is true and require the
deasserted branch to withdraw a pending bit; observe RED on `3aeef41`.  Add a
mutation-sensitive host test for Active+asserted -> Active+Pending and
Active+Pending+deasserted -> Active, then implement the smallest call-site and
run focused, complete nested and root suites.

TDD and implementation result:
- RED: the new production-path contract failed on m1n1 `3aeef41` because
  `timer_sync_live_irq` did not exist;
- GREEN: the existing mutation-sensitive four-state host test and the new root
  call-site test pass.  Only the already-live virtual INTID 18 LR is synchronized;
  INTID 17, queue ownership, tick cadence and recovery remain unchanged;
- synchronization writes the LR and recomputes VI only when the level transition
  changes the state, avoiding extra LR writes for stable Pending or
  Active+Pending values;
- focused root tests passed 20/20, the complete nested C host suite passed, the
  complete root suite passed 257/257 in the project environment, and both diff
  checks passed;
- m1n1 implementation commit
  `ca6ab37ce0dbbb7c18da40102887aebb58cc9dbb`.

Hardware checkpoint after a clean RELEASE `APPLE_INPUT=0` build: boot through
the disk-check countdown without a diagnostic boundary, reach login and TCP/22,
then maintain continuously changing UI/time and IAR/EOI progress for at least
ten minutes.  Any static frame over two minutes, timer Active-only while
`CNTV_CTL=0x5`, `0x101`, `0x133`, reset or pause over five seconds rejects the
fix.  Recovery remains the unchanged Stage 1 and EXP-038 artifacts.

Pre-launch artifact record (UTC 2026-08-14T16:48:22Z):
- root `fc0a7b922f99beef34c5f42d934ea6af4386e99a`, m1n1
  `ca6ab37ce0dbbb7c18da40102887aebb58cc9dbb`, Mu
  `63942398cccbd98127cfecbd7f936af99c837d6f`; strict manifest records clean
  tracked source;
- exact clean build: Docker `make clean`, then
  `make -j8 RELEASE=1 APPLE_INPUT=0`; build succeeded with pre-existing warnings
  plus the same signedness warning family as the adjacent bounded LR scans;
- `investigation/artifacts/EXP-20260814-039/m1n1.macho`, SHA-256
  `74bbaf481875897e79b673b2813de40e351c019f08a6cd00abf7ece14099c554`;
- unchanged no-AINP `J313_EFI.fd`, SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- `MANIFEST.json`, SHA-256
  `3ac93f65dd7a4a696ed7c70bc618b0f219609d077bdcd4cb1806c6b4768a4f5f`;
  release/display-both/debug-off and both artifact roles verified;
- recovery: installed Stage 1 plus immutable EXP-038 artifacts.

Exact launch command:

```sh
./scripts/run-windows.sh --execution assisted --observed --debug off \
  --proxy /dev/cu.usbmodemC02HDNCCQ6L41 \
  --vuart /dev/cu.usbmodemC02HDNCCQ6L43 \
  --firmware investigation/artifacts/EXP-20260814-039/J313_EFI.fd \
  --m1n1 investigation/artifacts/EXP-20260814-039/m1n1.macho \
  --chainload --foreground
```

Hardware result (UTC 2026-08-14T16:55:05Z):
- the exact recorded RELEASE artifact launched and reported m1n1 commit
  `ca6ab37`; all eight CPUs entered, NVMe reached runtime, and xHCI discovery
  completed;
- Windows froze on the same `To skip disk checking, press any key within 8
  seconds` screen.  Observer generations continued from 22 through 49 and 69,
  but the captured framebuffer remained byte-identical for more than two
  minutes at SHA-256
  `bcbcef15c93229fde86d0a5d0f8815ed5e1dae073a1c5056d5acf25feccff742`;
- the 30-, 60- and 120-second raw frames and final frame are under
  `investigation/artifacts/EXP-20260814-039/evidence/`; the 30-second PNG is
  SHA-256
  `31b8857f1a67637dabe72e69620bfdd0ff391217b5877b1d328c54060ea04484`;
- metadata SHA-256 values are
  `f3e5eb076bd38d1a5d7288163cff7c054ae3cacd38bb275d60a52b1fade658a2`
  at 30 seconds,
  `4b1004ed4680017752a0021d6ff9dd859b1a1c9ea47e42b88f7f683544d2b108`
  at 60 seconds, and
  `b3e04ccc36f66a179f01c99b0c73813d75e40d7e7fd6bf9f2b119ed6d62be7ed`
  at 120 seconds/final;
- TCP/22 never appeared.  Occasional observer transport checksum corruption
  appeared in the release console, but generation continued and independently
  saved frames remained byte-identical, so it is not used as the freeze
  criterion;
- SIGTERM was sent only to the verified launcher PID after the failure criterion.
  The final control path printed `BHL owner=0 count=1`; the later guest exception
  occurred during forced reboot and is not attributed as the initiating failure;
- recovery completed successfully: Stage 1 `b791225` responded after reboot and
  reported J313, all eight CPUs and 8.0 GiB DRAM.

Verdict: rejected.  Correcting Active to Active+Pending for an asserted live
INTID 18 is architecturally necessary but is not sufficient to wake/progress the
Windows vCPU.  The next experiment must preserve this correction and observe the
EOI/deactivation and physical wake boundary; it must not restore the rejected
1-ms polling source, trap WFI, or signal HCR.VI for an Active+Pending LR.

Finalized (UTC): 2026-08-14T16:55:05Z.

### EXP-20260814-040 — observe corrected timer level at the release freeze

Status: planned
Created (UTC): 2026-08-14T17:00:05Z

Hypothesis: EXP-039 rejected the sufficiency of the narrow INTID 18 level
correction, but its RELEASE profile could not prove which LR state survived at
the frozen disk-check countdown.  A monitor build of the exact same runtime
source will show either (a) `Active+Pending` now persists and the remaining
failure is after level synchronization, most likely at guest EOI/physical idle
wake, or (b) `Active-only` persists and the new production call site was not
reached for the sampled path.

Source-first contract inspected before the run:
- live EXP-038 J313 records show expired `CNTV_CTL=0x5` with both Pending-only
  `0x502...12` and Active-only `0x902...12` LRs, empty queues, advancing host
  ticks and idle-loop Windows PCs; diagnostic IPIs temporarily advanced the
  guest;
- EXP-039 used m1n1 `ca6ab37` and froze for over two minutes at the same
  countdown while observer generations advanced, but RELEASE intentionally
  omitted LR/IAR/EOI telemetry;
- current `src/hv_exc.c` maps asserted+Active INTID 18 to Active+Pending and
  current `src/hv_vgic.c` maps EOI Active+Pending to Pending before recomputing
  HCR.VI; it deliberately does not signal VI while the LR is still Active;
- upstream Asahi m1n1 keeps Apple's physical virtual-timer FIQ route enabled
  until the comparator asserts, then masks the route while reflecting timer
  state to the guest.  The Windows fork additionally owns synthetic vGIC LR and
  HCR.VI delivery;
- Mu's J313 DSDT contains only commented-out `_LPI` methods, while its GTDT/MADT
  expose the architectural Arm timer/GIC.  Windows therefore uses its inbox Arm
  timer/HAL path; Apple Input remains absent and cannot own CPU wake.

Ownership: Windows owns CNTV programming and IAR/EOI; m1n1 owns Apple timer FIQ
routing, LR state, synthetic VI and the physical wake boundary; Mu owns only
enumeration.  The smallest falsifiable checkpoint is two complete per-CPU
snapshots at least ten seconds apart during the reproduced static frame.

Single changed experimental variable relative to EXP-039:
- RELEASE/debug-off -> DEBUG/monitor, enabling sampled counters and complete LR
  snapshots.  Source commit `ca6ab37`, timer/vGIC policy, cadence, disabled
  recovery timer, disabled Apple Input, Mu/no-AINP firmware, topology, storage,
  xHCI and display layout remain unchanged.

Pre-run provenance: root `298e2c3b6396099af0c215e21acc743e0b64586d`,
m1n1 `ca6ab37ce0dbbb7c18da40102887aebb58cc9dbb`, Mu
`63942398cccbd98127cfecbd7f936af99c837d6f`; all three tracked diffs are empty
at SHA-256
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.

Exact planned build:

```sh
docker run --rm -v /Users/pavel/public_windows:/work \
  -w /work/m1n1_windows windows-on-m1-build:local make clean
docker run --rm -v /Users/pavel/public_windows:/work \
  -w /work/m1n1_windows windows-on-m1-build:local make -j8 APPLE_INPUT=0
```

After the clean build, copy `m1n1.macho` and the unchanged EXP-039 firmware to
`investigation/artifacts/EXP-20260814-040`, create and strictly verify a
DEBUG/monitor/both manifest, and append all hashes before launch.  The assisted
launch will use `--observed --debug monitor` and the same proxy/vUART devices.

Expected checkpoint: reproduce the byte-identical countdown or later freeze,
capture two complete LR banks and counter deltas, then request the final
snapshot/recovery.  Failure to reproduce is inconclusive because monitor
diagnostics can perturb timing.  A `0x101`, `0x133`, reset or exception rejects
the diagnostic as a clean reproduction.  Recovery remains installed Stage 1
and the immutable EXP-039 artifacts.

Recorded artifact before launch (UTC 2026-08-14T17:01:58Z):
- clean Docker monitor build completed from m1n1
  `ca6ab37ce0dbbb7c18da40102887aebb58cc9dbb` with `APPLE_INPUT=0`; warnings
  are the pre-existing signedness/unused-variable families recorded by prior
  monitor builds;
- root manifest commit `9ff74abb2503defb3cc129f35c06424cec9fcac4`, Mu
  `63942398cccbd98127cfecbd7f936af99c837d6f`, all tracked sources clean;
- `investigation/artifacts/EXP-20260814-040/m1n1.macho`, SHA-256
  `2556c430863a1e8b40715e91d1f8b37ef4c6a7ee1e956665df52bd903b102517`;
- unchanged no-AINP `J313_EFI.fd`, SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- `MANIFEST.json`, SHA-256
  `9986ab43b07a4f53bcde08dece1a037b67fb3d9ed052f41128b4cb54a4a979d3`;
  strict DEBUG/monitor/both and artifact-role verification passed.

Exact launch command:

```sh
./scripts/run-windows.sh --execution assisted --observed --debug monitor \
  --proxy /dev/cu.usbmodemC02HDNCCQ6L41 \
  --vuart /dev/cu.usbmodemC02HDNCCQ6L43 \
  --firmware investigation/artifacts/EXP-20260814-040/J313_EFI.fd \
  --m1n1 investigation/artifacts/EXP-20260814-040/m1n1.macho \
  --chainload --foreground
```

Hardware result (UTC 2026-08-14T17:08:16Z):
- the exact monitor artifact reported m1n1 `ca6ab37`; CPUs 0 through 7 entered,
  NVMe and xHCI reached runtime, and no bugcheck or initiating EL2 exception was
  captured;
- observer generation advanced from 30 to 71 while two full 16,384,000-byte
  frames remained byte-identical at SHA-256
  `49d3595690a1571dc937594effd1f7a81846c1b3b8a3aa199e55915495f2e165`;
  TCP/22 was absent, confirming the visible countdown was a guest freeze rather
  than a stopped observer;
- after a diagnostic physical IPI boundary, the guest advanced from that
  countdown to a black frame, SHA-256
  `6992296c77327bc9aaab7ca4758501ce5d2bd2e3c1ec7050f400881ed9ffbdcb`.
  This repeats EXP-035/038's wake-assisted progress;
- the first useful complete snapshot proved the EXP-039 correction executes:
  CPU0 and CPU2 contained INTID 18 `Active+Pending` LRs
  (`0xd020020000000012`), while CPUs 1, 3, 4, 5 and 7 contained Pending-only
  `0x5020020000000012` with HCR.VI asserted; queues were empty and no LR
  shortage existed;
- over the next 38.84 seconds, CPU1's host tick count advanced 22378 -> 26260
  at the expected approximately 100-Hz cadence, while its Pending INTID 18 and
  guest SGI lifecycle counts remained unchanged.  Other secondaries showed the
  same live host/idle-Pending pattern;
- each requested physical diagnostic IPI moved timer state: the immediately
  following records show fresh INTID 18 IAR/EOI timestamps, and CPU2 changed
  from Active+Pending to Pending before the next stall.  The guest then returned
  to its idle path with another correctly represented Pending timer;
- complete evidence is
  `investigation/artifacts/EXP-20260814-040/evidence/hv.log`, SHA-256
  `729dda825353a4da58be72d80e5a16a42311b3cf79288a93079ecd612cdb78aa`;
  `frame-a.raw` and `frame-b.raw` have the countdown hash above, while
  `frame-c.raw` has the black-frame hash;
- SIGTERM targeted only the verified `run_uefi.py` PID after the failure
  criterion.  The final guest exception followed the requested recovery reboot
  and is not the initiating failure.  Stage 1 `b791225` then responded with all
  eight CPUs and 8.0 GiB DRAM.

Verdict: confirmed for the missing physical wake classification.  EXP-039's
level correction is effective and must remain, but LR+HCR.VI alone does not
reliably cause an Apple core in the Windows idle/WFI path to enter the virtual
IRQ.  A physical IPI boundary consumes the pending timer and advances the guest.
The next correction will issue one local physical IPI only on the false-to-true
edge of a priority-deliverable timer VI.  It will not poll, trap/skip WFI, signal
Active+Pending, alter timer cadence, or change Windows/Mu/input behavior.

Finalized (UTC): 2026-08-14T17:08:16Z.

### EXP-20260814-038 pre-launch continuation and ledger correction

Recorded (UTC): 2026-08-14T16:33:41Z

Correction: the historical EXP-036 hardware-result block that follows the first
EXP-038 planning text above was inserted out of chronological order; it describes
only EXP-036 (`0cde15e`, release artifact `8f545c13...`) and is not an EXP-038
result.  This appended correction preserves the old text while preventing it from
being interpreted as evidence for the not-yet-launched EXP-038 artifact.

Exact pre-launch source and artifact contract:
- root `d35bbfda6b6182c0c81ecd2a9f90a05ad340e376`, m1n1
  `3aeef41261ee51d7eaa922721773c0d199a44780`, Mu
  `63942398cccbd98127cfecbd7f936af99c837d6f`; manifest records clean tracked
  source (Mu's existing untracked nested markers are not included);
- clean Docker monitor build: `make clean`, then
  `make -j8 APPLE_INPUT=0`; build succeeded with only pre-existing warnings;
- `investigation/artifacts/EXP-20260814-038/m1n1.macho`, SHA-256
  `7a900c9120506b1a47a3740c2b7edd3dcee0b752c285ba82bd75f08d3bae796b`;
- unchanged no-AINP `J313_EFI.fd`, SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- `MANIFEST.json`, SHA-256
  `c28a7a279be1b5de09b376bb346ed59d8f25dd86e4d007b2f633b1ae14a73a69`;
  strict debug/display-both/monitor and artifact-role verification passed;
- recovery artifact: unchanged ESP/Stage 1 and the recorded EXP-037 artifacts.

Exact launch command:

```sh
./scripts/run-windows.sh --execution assisted --observed --debug monitor \
  --proxy /dev/cu.usbmodemC02HDNCCQ6L41 \
  --vuart /dev/cu.usbmodemC02HDNCCQ6L43 \
  --firmware investigation/artifacts/EXP-20260814-038/J313_EFI.fd \
  --m1n1 investigation/artifacts/EXP-20260814-038/m1n1.macho \
  --chainload --foreground
```

Expected checkpoint: reproduce the late static framebuffer with advancing
observer generations, capture at least two watchdog snapshots ten seconds apart,
and verify that every CPU record now includes `lrc/lr0..lr7`.  Decode INTID 18's
LR state (Pending, Active, Active+Pending, or absent) on the stalled vCPUs.  A
missing LR bank, failure to reproduce, reset, or inconsistent snapshot is
inconclusive.  The final control action is an explicit SIGTERM snapshot/reboot,
followed by a Stage-1 eight-CPU/8-GiB recovery probe.

Hardware result:
- exact recorded monitor artifact launched and reported m1n1 `0cde15e`; all
  eight CPUs, NVMe, guest-runtime cadence and xHCI route checkpoints completed;
- Windows entered automatic disk checking after the preceding forced recovery,
  then the observed framebuffer became black and remained byte-identical for
  more than three minutes at SHA-256
  `cdbfca1d7d5370ff64fc999c807efa85048ad951c56dd573eb7ffdbf263ae08a`;
  the known TCP/22 endpoint remained unavailable;
- three diagnostic boundaries produced current records for all eight CPUs.
  Between samples every CPU's host tick arm/fire counters advanced and guest PCs
  moved; bhl reported only CPU0's expected print-time ownership, excluding a
  global EL2 deadlock;
- CPUs 2-5 continued updating timer INTID 18 IAR/EOI timestamps.  CPUs 0, 1, 6
  and 7 repeatedly showed `CNTV_CTL=0x5` (enabled and expired), `vinj=1`, the
  virtual-timer physical FIQ route disabled, HCR.VI clear, empty queues and stale
  timer IAR/EOI timestamps.  This localizes the late stall to an owned virtual
  timer delivery whose wake/acknowledgement state no longer progresses on those
  vCPUs;
- the monitor dump's single formatted CPU record exceeded the console formatting
  budget and truncated exactly after `marker`, before `lrc/lr0..lr7`.  Therefore
  the preserved run cannot distinguish a Pending-only LR from Active or
  Active+Pending, the distinction required before changing delivery semantics;
- evidence: `investigation/artifacts/EXP-20260814-037/evidence/hv.log`, SHA-256
  `f9a3c438013ce5c89a2ccae381d21e37d05bdcc949caa07512fc8d82012c9bba`;
  `stall.raw`, SHA-256
  `cdbfca1d7d5370ff64fc999c807efa85048ad951c56dd573eb7ffdbf263ae08a`;
  `final-meta.json`, SHA-256
  `ce215f1157e552373a5a6fad561526aae71defc9b32ceb0a36e82aa1a3111a76`;
- explicit recovery succeeded and the Stage-1 probe again verified eight CPUs
  and 8 GiB DRAM.

Verdict: hypothesis (c), global-lock deadlock, is rejected; host timer loss is
also rejected.  The run confirms a per-vCPU virtual-timer delivery stall but is
inconclusive between hypotheses (a) and (b) because the LR payload was truncated.
Do not alter timer state handling until that final state is captured.

Finalized (UTC): 2026-08-14T16:28:00Z.

### EXP-20260814-038 — preserve LR bank in bounded monitor snapshot records

Status: planned
Created (UTC): 2026-08-14T16:28:00Z

Hypothesis: splitting the existing watchdog CPU record into two bounded `printf`
formatting calls while keeping one newline-delimited logical record will preserve
`lrc/lr0..lr7` without changing sampled data or guest runtime behavior.  Repeating
the monitor run will then determine whether EXP-037's stalled `vinj=1`, VI-clear
vCPUs own a Pending, Active, Active+Pending, or no timer LR.

Single changed variable relative to EXP-037:
- diagnostic snapshot formatting only: CPU/timer/IRQ fields and LR fields are
  formatted in two bounded calls and terminated by the same single newline.

Unchanged: all runtime code and policies, snapshot cadence/content, m1n1 timer and
vGIC delivery, host tick rates, Apple Input disabled, Mu, Windows, CPUs, NVMe,
xHCI and displays.  The formatting executes only after an explicit diagnostic
request and cannot be a production fix.

Falsifiable software checkpoint: update the focused source contract first so the
summary must end after `marker` without a newline and a second call must emit
`lrc/lr0..lr7` plus the newline; observe RED on `0cde15e`, implement only the
split, then run the focused/root and complete nested suites.  The existing
line-oriented stability parser must continue accepting a synthetic split-call
logical record.

After the implementation commit, build the same clean non-RELEASE
`APPLE_INPUT=0` monitor artifact, record all hashes and launch command before
hardware use.  Capture two complete LR-bearing snapshots at the reproduced stall
and recover to Stage 1.

TDD and implementation checkpoint:
- RED: the focused source-contract test failed because the CPU record still
  formatted `marker`, `lrc` and all eight LRs in one oversized call;
- GREEN: the summary call now ends after `marker` with no newline, and a second
  bounded call appends `lrc/lr0..lr7` and the sole newline, preserving the exact
  logical record consumed by the line-oriented parser;
- focused vGIC/platform-stability suites passed 35/35, the complete nested C host
  suite passed, and both diffs passed checks;
- m1n1 implementation commit
  `3aeef41261ee51d7eaa922721773c0d199a44780`; no runtime path changed.

Hardware result:
- exact recorded artifacts launched and m1n1 reported `0cde15e`; all eight CPUs,
  NVMe and xHCI discovery initialized normally;
- unlike EXP-035, the guest advanced without any host diagnostic request from the
  Windows-logo frame SHA-256 `1b02574b...` to `guest runtime ready`, xHCI route
  enable and the Windows lock screen.  This confirms that the restored local VI
  recomputation closes a real early lost-wake window;
- the guest then stopped at the lock-screen frame showing 5:22.  From
  2026-08-14T16:11:48Z through 16:13:47Z, observer generations advanced from 127
  through 182 while the complete framebuffer remained byte-identical at SHA-256
  `6bccade3bf50e472a3b62ba4cac9a15d71a694a38570911d18fa36bdfe373260`;
- TCP/22 at the known guest address remained unavailable throughout.  A manual
  diagnostic boundary did not change the framebuffer hash or restore the network;
- preserved evidence:
  `investigation/artifacts/EXP-20260814-036/evidence/lock-screen.raw` and
  `post-wake.raw`, both with the framebuffer hash above; `final-meta.json`,
  SHA-256
  `a7ff2862fd2c85a16102badbce15480195452f1ee6b9e15da239dfdf26b68455`;
- the release profile intentionally contained no per-CPU watchdog records; the
  control boundary reported only the known CPU0 print-time lock ownership.  The
  exception dump again occurred after the explicit reboot request and is not
  treated as the initiating failure;
- recovery succeeded: both USB serial functions returned, Stage-1 m1n1 `b791225`
  answered, and the live probe verified eight CPUs and 8 GiB DRAM.

Verdict: rejected as the complete freeze fix, but confirmed as a partial
correction.  It moves the independently observed failure from early Windows boot
to the lock screen, proving one stale-VI boundary existed.  A second wake/progress
failure remains and requires a diagnostic-profile run that records live per-CPU
timer, LR, VI, ISR and lock state at the later checkpoint without changing the
accepted timer policy.

Finalized (UTC): 2026-08-14T16:15:48Z.

### EXP-20260814-037 — classify the later lock-screen stall with monitor snapshots

Status: planned
Created (UTC): 2026-08-14T16:15:48Z

Hypothesis: the remaining EXP-036 lock-screen stall is one of three distinguishable
states: (a) a deliverable Pending timer LR with HCR.VI asserted but no physical idle
wake, (b) an expired guest timer with no live/queued delivery, or (c) vCPUs blocked
behind the global hypervisor lock.  A normal monitor build of the exact EXP-036
runtime code will publish enough lock-free per-CPU state to select among these
without changing timer/vGIC policy.

Single changed experimental variable relative to EXP-036:
- RELEASE diagnostics off -> monitor diagnostics on, enabling sampled per-CPU
  snapshots and diagnostic counters but not verbose synchronous hot-path tracing.

Unchanged: m1n1 source commit `0cde15e`, restored fast-boundary VI recomputation,
5000/100-Hz cadence, accepted timer latch/repend path, disabled recovery timer,
Apple Input disabled, Mu/no-AINP firmware, eight CPUs, storage, xHCI and both
display consumers.  This is a diagnostic classification run, not a performance
acceptance build; any observer effect will be recorded.

Source contracts inspected:
- `src/hv_runtime_diag.h`: non-RELEASE enables counters and sampled snapshots;
  `HV_RUNTIME_DIAG_VERBOSE` remains absent, so timer/vGIC console formatting stays
  disabled;
- `src/hv_exc.c`: snapshot records host CNTP, guest CNTP/CNTV, VM timer routing,
  HCR/ICH state, ISR, timer latches/queues, SGI lifecycle, last IAR/EOI, all live
  LRs and breadcrumb; dump reads records without acquiring bhl;
- EXP-026 already validated that these counters distinguish host timer firing from
  guest delivery, while its 1-ms recovery policy is not present here.

Exact planned clean build:

```sh
docker run --rm -v /Users/pavel/public_windows:/work \
  -w /work/m1n1_windows windows-on-m1-build:local make clean
docker run --rm -v /Users/pavel/public_windows:/work \
  -w /work/m1n1_windows windows-on-m1-build:local \
  make -j8 APPLE_INPUT=0
```

Planned artifact directory:
`investigation/artifacts/EXP-20260814-037`; record a DEBUG/monitor/both manifest,
artifact SHA-256 and exact revisions before launch.  The assisted command will use
`--observed --debug monitor` and the same ports, firmware and foreground lifecycle
as EXP-036.

Expected checkpoint: reproduce the later static lock-screen frame, capture two
snapshots at least 10 seconds apart, then classify by deltas.  Failure to reproduce
is inconclusive because diagnostics can perturb timing; a dump with no current
per-CPU records also rejects the instrumentation.  Recovery remains Stage 1 and
the immutable EXP-036 artifacts.

Recorded artifact before launch:
- root `6b1ad9f4ccb9f0d265bd08d8d2aa5e7019c3fb6a`, m1n1
  `0cde15ea76e84e64b8effb37bec4308c2f211c59`, Mu `63942398`;
- `investigation/artifacts/EXP-20260814-037/m1n1.macho`, SHA-256
  `ea8b66aee0aad0629b38debd7b16ff86b22c7ae82d093645ccc50ec027a0ef3e`;
- unchanged no-AINP firmware SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- DEBUG/monitor/both manifest SHA-256
  `152437aeb44c9adb10b3561da80d20352fa963f30fb1f0b515291af590126181`;
  strict role verification passed and all tracked source revisions are clean.

Exact launch command:

```sh
./scripts/run-windows.sh --execution assisted --observed --debug monitor \
  --proxy /dev/cu.usbmodemC02HDNCCQ6L41 \
  --vuart /dev/cu.usbmodemC02HDNCCQ6L43 \
  --firmware investigation/artifacts/EXP-20260814-037/J313_EFI.fd \
  --m1n1 investigation/artifacts/EXP-20260814-037/m1n1.macho \
  --chainload --foreground
```

Expected checkpoint: with valid observer framing, Windows must advance beyond
the EXP-033 frozen framebuffer, reach the lock/login screen, acquire the known
network address and remain continuously responsive for at least ten minutes.

Failure criterion: the same byte-identical Windows-logo interval for two minutes,
loss of observer framing, EL2 exception/reset, Windows bugcheck, or any new
failure before the EXP-033 checkpoint.

Planned evidence: `investigation/artifacts/EXP-20260814-034/evidence/`, viewer
metadata/frame, launcher/UART log, bounded ICMP/TCP checks and any Windows dump.
On failure, terminate the sole assisted runner to request the documented final
snapshot and reboot, then verify the Stage-1 probe.

Build and provenance result:
- the clean no-AINP `DSDT.aml` has SHA-256
  `78f50ccf35327f8c98358514bf65befc04fbd14e0ead12a335e11cafe5eca102`
  and contains no `APPL0001` string;
- the clean final FD nevertheless has SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`,
  exactly equal byte-for-byte to EXP-033 and all other assisted freeze artifacts;
- the EXP-033 FD timestamp is August 8, while Mu commit `63942398` adding AINP
  is dated August 13.  A clean build from the same Mu tree with the AINP include
  removed deterministically reproduces that old FD.

Verdict: superseded without a hardware launch.  EXP-033 already ran the
no-AINP firmware control; launching this identical binary again would change no
variable.  The AppleInput Windows driver cannot explain the reproduced EXP-033
freeze because the guest firmware artifact did not enumerate its ACPI hardware
ID.  The root problem remains in the common runtime path.

Recovery: no image was launched or installed; the target remained in Stage 1.

Next falsifiable checkpoint: restore the committed AINP include and perform a
clean Mu build without launching it.  The embedded ACPI payload and final FD
must change.  This packaging check determines whether the input work was merely
absent from artifacts or whether Mu also fails to carry rebuilt ACPI into the
firmware volume.

### CORRECTION-20260814-004 — artifact evidence restores EXP-033 input exclusion

CORRECTION-20260814-003 inferred the guest ACPI contract from Mu source commit
`63942398`, but the actual EXP-033 FD was a cached August 8 artifact.  The clean
no-AINP reproduction above proves that the launched FD was byte-identical to the
pre-input firmware.  Therefore EXP-033 did exclude AppleInput at the guest
artifact boundary, though not for the reason its manifest claimed.  The
observer result remains valid; the provenance/build-cache defect must be fixed
separately and future conclusions must inspect the built ACPI, not only source
commit metadata.

### CORRECTION-20260814-005 — commented ASL include was still expanded

The first EXP-034 control used `// #include`.  Mu's ASL preprocessing pipeline
preserved/exposed that directive in its intermediate input; the subsequent
clean AINP-on build produced the same `DSDT.aml` SHA-256 `78f50cc...`, and
`DSDT.iii` proved that `J313AppleInput.asl.inc` had been expanded.  Therefore
the claimed clean no-AINP reproduction and CORRECTION-004 conclusion are
invalid and superseded.  No hardware image was launched.

EXP-034 is reopened with a real preprocessor guard:

```asl
#if 0
#include "J313AppleInput.asl.inc"
#endif
```

The new Mu tracked diff SHA-256 is
`8fbfc301f9f64885565f621f1de035294fa45ffa419f82b0ee2b9be3385da90b`.
The next clean build uses the already recorded Docker/Stuart `--clean` command.
It is acceptable only if `DSDT.iii` contains neither `Device (AINP)` nor
`APPL0001`, `DSDT.aml` changes hash, and the final FD changes hash relative to
EXP-033.  Otherwise the artifact is rejected before launch.

### CORRECTION-20260814-006 — final EXP-034 packaging verdict

The true `#if 0` build produced `DSDT.iii` with no AINP.  After restoring the
committed include, a final clean build produced:
- `DSDT.iii` SHA-256 `dfd297910e2dbb4f0eaa4c5b1818d88e7736bc273edc36e591eaf7bc717c4d9e`,
  with `Device (AINP)` and `APPL0001` present;
- actual iasl input `DSDT.iiii` SHA-256
  `1734ce548c9ac8e2f115db5a52e1401432f4a1af4cd93173046dfbb724771c55`,
  with both AINP identifiers absent;
- final `DSDT.aml` SHA-256 `78f50ccf...` and FD SHA-256 `0dba13c...`,
  unchanged from the true no-AINP build and EXP-033.

Final verdict: Mu's `Trim --source-code -l` drops the body of the quoted ASL
include between C preprocessing and iasl.  AINP has never entered the tested
firmware artifact, so AppleInput is conclusively excluded as the cause of the
EXP-033 freeze.  CORRECTION-004's conclusion is restored by stronger pipeline
evidence; its earlier reasoning is not reused.  This packaging defect must get
its own TDD fix after runtime stability is restored.

Finalized (UTC): 2026-08-14T15:50:00Z.  No image was launched; Stage 1 remains
the recovery state.

### EXP-20260814-035 — zero-cost release tick diagnostics

Status: planned
Created (UTC): 2026-08-14T15:50:00Z

Hypothesis: commit `89d41fac` added diagnostic writes to every host tick after
the last responsive baseline.  In the release artifact CPU0 atomically updates
one element of a shared counter array at 5000 Hz, all seven secondaries update
adjacent elements at 100 Hz, and every CPU also updates `host_tick_fires`.
These measurements are never consumed because release snapshots return before
sampling.  Eliminating only those release writes should remove the shared-cache
and EL2 hot-path regression while preserving timer/vGIC behavior and all debug
observability.

Single changed variable relative to EXP-033:
- diagnostic tick counters become compile-time no-ops in RELEASE; no timer
  interval, interrupt delivery, guest state, Mu, ACPI, storage, USB, CPU count,
  display or observer change.

Source contract before TDD:
- root commit `c324955b87b0d4a04c26ba1ac4eb26961c011995`, branch
  `codex/canonical-public-release`; only append-only ledgers are dirty;
- m1n1 commit `72b2aab8a6089b2099242f3bdb4a8cfd08e1113b`, clean;
- Mu commit `63942398cccbd98127cfecbd7f936af99c837d6f`, tracked source clean;
- EXP-033 Mu/Windows artifact remains unchanged and already excludes AINP by
  CORRECTION-006.

Falsifiable software checkpoint: extend the real host runtime-diagnostics tests
so a diagnostic counter increments in debug and remains unchanged in RELEASE;
observe RED before adding the helper, then GREEN.  The RELEASE disassembly of
`hv_arm_tick`/FIQ must contain no counter RMW/store while retaining CNTP writes.

Planned build: clean Docker `make -j8 RELEASE=1 APPLE_INPUT=0`; artifact,
manifest, exact hashes and launch command will be recorded before hardware use.
Recovery remains the unchanged ESP/Stage 1 and EXP-033 artifacts.

TDD checkpoint before artifact build:
- RED: focused host compilation failed exactly because
  `HV_RUNTIME_DIAG_COUNT` did not exist;
- GREEN: debug behavior incremented a real counter from 41 to 42, while the
  RELEASE build left it at 41; both focused binaries passed;
- production now routes the two arm counters and `host_tick_fires` through the
  compile-time diagnostic counter contract; timer register writes are unchanged;
- root vGIC/runtime source-contract suite passed 16/16;
- current m1n1 tracked diff SHA-256
  `a3ac7c1d6d4b891696af03596bc0a29116d190e51e884851a5a9dd465387556b`.

Implementation commit and exact post-commit build:
- m1n1 `46c2240df2df467b9ee3d89b86f740f63a452acb`, tracked source clean;
- `docker run --rm -v /Users/pavel/public_windows:/work -w
  /work/m1n1_windows windows-on-m1-build:local make clean`;
- `docker run --rm -v /Users/pavel/public_windows:/work -w
  /work/m1n1_windows windows-on-m1-build:local make -j8 RELEASE=1
  APPLE_INPUT=0`.

Recorded artifact before launch:
- root `7c299066cc62133e9e44661af8042b2d12759aa9`, m1n1
  `46c2240df2df467b9ee3d89b86f740f63a452acb`, Mu `63942398`;
- `investigation/artifacts/EXP-20260814-035/m1n1.macho`, SHA-256
  `30d4c5fdcf724a4f0e516e1b11a349d71349506ebf57bf198c8a69d9c919f08c`;
- unchanged no-AINP `J313_EFI.fd`, SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- manifest SHA-256
  `f494d13c13d64096e128434cb96ed688c3f5356671286fe1d78583e03ed242f5`;
  strict release/display-both/debug-off role verification passed;
- RELEASE object disassembly retains `CNTP_TVAL_EL0`/`CNTP_CTL_EL0` in
  `hv_arm_tick` and has no tick-counter reference or RMW/store.

Exact launch command:

```sh
./scripts/run-windows.sh --execution assisted --observed --debug off \
  --proxy /dev/cu.usbmodemC02HDNCCQ6L41 \
  --vuart /dev/cu.usbmodemC02HDNCCQ6L43 \
  --firmware investigation/artifacts/EXP-20260814-035/J313_EFI.fd \
  --m1n1 investigation/artifacts/EXP-20260814-035/m1n1.macho \
  --chainload --foreground
```

Expected checkpoint: all eight CPUs, NVMe and xHCI initialize; the valid viewer
advances beyond EXP-033's Windows-logo CRC, Windows reaches login/network, and
UI/SSH remain continuously responsive for ten minutes.  Failure is a two-minute
byte-identical framebuffer, any checksum loss, bugcheck/reset, or a pause longer
than five seconds.  Evidence goes under the artifact `evidence/` directory;
SIGTERM requests the final snapshot/reboot recovery.

Hardware result:
- the exact recorded m1n1 and Mu artifacts launched through the recorded assisted
  observed command; m1n1 reported commit `46c2240`, all eight CPUs entered, NVMe
  initialized, and xHCI discovery completed;
- the observer remained structurally healthy: metadata advanced from generation
  18/frame 17 to generation 60/frame 59, but the framebuffer stayed byte-identical
  at SHA-256
  `494fe4af7deeecdc4347e1ea314e6c742015f56ba59b181e892c75d96b539f7b`;
- the metadata evidence is
  `investigation/artifacts/EXP-20260814-035/evidence/freeze-fb-info.json`,
  SHA-256
  `219a14cfe23014c743fd80283ba6f517bd9530fa88c1be7c627b5ca7459a81bc`;
  the raw frozen frame is `freeze-fb.raw` with the framebuffer hash above;
- Windows did not reach its known TCP/22 endpoint.  The unchanged frame exceeded
  the two-minute failure criterion, so the release diagnostic writes are not the
  root cause of the freeze;
- when the host requested a diagnostic/recovery boundary, the guest immediately
  advanced far enough for m1n1 to print `guest runtime ready` and the xHCI route
  enable.  This repeats EXP-029's observation that an external EL2 wake can
  temporarily restore guest progress;
- the final control request observed the known diagnostic condition
  `BHL owner=0 count=1` while CPU0 was printing.  The exception dump appeared only
  during the forced recovery/reboot and is not attributed as the initiating guest
  failure;
- recovery completed successfully: the target returned to Stage 1, the probe
  responded, and both USB serial functions reappeared.

Verdict: rejected as the freeze root cause.  The change remains a correct
zero-cost RELEASE cleanup, but EXP-035 proves it is insufficient.  The reproduced
failure and wake-assisted progress localize the next check to the secondary FIQ
return boundary and its synthetic HCR.VI wake contract.

Finalized (UTC): 2026-08-14T16:02:28Z.

### EXP-20260814-036 — resynchronize synthetic VI before secondary idle return

Status: planned
Created (UTC): 2026-08-14T16:02:28Z

Hypothesis: EXP-028 removed the last `hv_vgic3_update_vi()` at the secondary FIQ
fast-return boundary.  `HCR_EL2.VI` is a software-cached output on this Apple-vGIC
implementation.  Most fresh injections update it, but the accepted timer latch
path deliberately does nothing while the same INTID remains live.  If another
guest vGIC transition cleared VI while a timer LR remained Pending, the current
fast path reads the stale clear value, returns to a Windows idle vCPU, and has no
physical event that guarantees a wake.  Recomputing VI from live LR/VMCR state
immediately after local timer/IPI handling should prevent that lost wake without
restoring the rejected 1-ms recovery timer or a global exit scan.

Evidence and source contract inspected before implementation:
- EXP-029 and EXP-035 both show that an external diagnostic wake temporarily
  restores progress; EXP-035 reproduces the freeze with Apple Input absent and
  RELEASE diagnostic writes compiled out;
- m1n1 `src/hv_exc.c`: the secondary fast path services `hv_update_fiq()` and
  `hv_handle_local_ipi()`, then decides from the current physical FIQ and cached
  HCR.VI bits whether to return directly;
- m1n1 `src/hv_vgic.c`: `hv_vgic3_update_vi()` derives the synthetic VI output
  from live Pending LR state, VMCR group enable, PMR, and running priority;
- m1n1 commit `091caeed` performed this recomputation only at the fast-return
  boundary and reached the desktop in EXP-017; commit `2ae94afc` removed it while
  restoring the accepted completion predicate, and the later no-recovery builds
  retain that removal;
- fresh inject/repend helpers update VI, but the `timer_*_injected == true` live-LR
  branch can legitimately retain the LR without recomputing VI.

Ownership contract: m1n1 owns Apple timer FIQ capture, virtual LR state and the
synthetic HCR.VI signal used to wake Windows.  Mu/ACPI owns enumeration only;
Windows owns timer programming and EOI/rearm after delivery.  Therefore the fix
belongs at m1n1's local FIQ-to-guest boundary, not in the input driver, Mu or
Windows.

Single changed runtime variable relative to EXP-035:
- after local timer/IPI service and before testing HCR.VI, recompute the synthetic
  VI line once from live local vGIC state.

Unchanged: 5000/100-Hz host cadence, accepted timer latch/repend path, disabled
guest recovery timer, global serialized exit policy, RELEASE diagnostics, Mu,
ACPI/no-AINP artifact, eight CPUs, storage, xHCI, display and observer profile.

Falsifiable software checkpoint: first change the focused source-contract test to
require one `hv_vgic3_update_vi()` after both local-source helpers and before the
HCR.VI completion read; observe RED on m1n1 `46c2240`, add only that production
call, then require GREEN plus the complete nested host and root vGIC suites.

Planned clean build: Docker `make clean`, then `make -j8 RELEASE=1 APPLE_INPUT=0`.
Record the implementation commit, artifact SHA-256 and strict manifest before
launch.  The exact assisted observed launch shape remains EXP-035 with only the
EXP-036 m1n1/artifact paths changed.  Recovery is the unchanged ESP/Stage 1 plus
the recorded EXP-035 artifacts.

Expected hardware checkpoint: advance past the frozen EXP-035 framebuffer, reach
login and the known network endpoint, then sustain continuous UI/SSH progress for
at least ten minutes with no pause over five seconds.  A byte-identical frame for
two minutes, dead network, bugcheck/reset or long pause rejects the hypothesis.

TDD and implementation checkpoint before artifact build:
- RED: the focused root contract failed on m1n1 `46c2240` because the secondary
  fast boundary contained zero `hv_vgic3_update_vi()` calls;
- GREEN: after adding exactly one recomputation after both local-source helpers
  and before the completion predicate, the focused test and root vGIC suite 16/16
  passed;
- the complete nested C host suite passed, the complete root suite passed 255/255
  when allowed to bind its loopback test server, and both repository diff checks
  passed;
- implementation commit:
  `0cde15ea76e84e64b8effb37bec4308c2f211c59`; m1n1 tracked source is clean.

The exact build now being run is the previously recorded clean Docker RELEASE
build.  No hardware launch is authorized until its artifact and manifest hashes
are appended below.

Recorded artifact before launch:
- root source commit `cd4cc923e2acfe09d6f2c3279f8cc52555ce04af`, m1n1
  `0cde15ea76e84e64b8effb37bec4308c2f211c59`, Mu `63942398`;
- `investigation/artifacts/EXP-20260814-036/m1n1.macho`, SHA-256
  `8f545c136ef77ca7226b66dbf6c16e0e035b0052380fd0743e9487785947afcb`;
- unchanged no-AINP `J313_EFI.fd`, SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`;
- manifest SHA-256
  `38b85ceb72eb507064a4a791c1f69553675bd4b13739eb2d9d3e9573e3876dea`;
  strict release/display-both/debug-off role verification passed and records
  clean source revisions;
- build completed successfully; emitted warnings are pre-existing and unrelated
  to the single added call.

Exact launch command:

```sh
./scripts/run-windows.sh --execution assisted --observed --debug off \
  --proxy /dev/cu.usbmodemC02HDNCCQ6L41 \
  --vuart /dev/cu.usbmodemC02HDNCCQ6L43 \
  --firmware investigation/artifacts/EXP-20260814-036/J313_EFI.fd \
  --m1n1 investigation/artifacts/EXP-20260814-036/m1n1.macho \
  --chainload --foreground
```

### EXP-20260814-041 — wake a newly deliverable timer VI with one physical edge

Status: planned; implementation and software verification complete
Created (UTC): 2026-08-14T18:11:50Z

Hypothesis: EXP-040 established that Windows can remain indefinitely in its idle
path with a priority-deliverable Pending INTID 18 LR and HCR.VI asserted, while
one physical IPI immediately causes a fresh timer IAR/EOI cycle and guest
progress.  Publishing HCR.VI followed by exactly one local physical IPI on the
clear-to-set edge of a deliverable architectural timer VI will supply the missing
J313 wake event without changing timer cadence, polling, trapping WFI, or
repeatedly interrupting a core while VI remains asserted.

Primary evidence and implementations inspected:
- live J313 EXP-040 telemetry: CPUs 1, 3, 4, 5 and 7 retained Pending-only INTID
  18 (`0x5020020000000012`) with HCR.VI set, empty queues, no LR shortage and
  advancing host ticks; a physical diagnostic IPI produced new timer IAR/EOI
  timestamps and framebuffer progress;
- current fork `m1n1_windows/src/hv_exc.c`, `src/hv_vgic.c`, `src/smp.c` and
  `src/cpu.c`: the fork owns Apple timer FIQ capture, LR state, HCR.VI synthesis,
  local physical IPI send/acknowledgement and the EL2-to-guest return boundary;
- current upstream Asahi m1n1 `src/hv_exc.c`: Apple timer FIQ routing and the
  physical exception boundary establish that a physical event enters EL2 before
  guest virtual interrupt delivery; no source was copied;
- current Mu J313 MADT/GTDT/DSDT generation: firmware exposes the architectural
  Arm GIC/timer contract to Windows and does not own runtime timer delivery;
- Arm GICv3/v4 architecture guidance: a Pending virtual interrupt in an LR and
  the virtual interrupt signal describe virtual distributor/CPU-interface state,
  while physical core wake is a separate implementation boundary;
- Microsoft ACPI system-description guidance and the generated Mu tables:
  Windows consumes the firmware-described architectural timer/GIC path; AINP and
  APPL0001 are absent from the artifact, so the unfinished Apple Input driver
  cannot bind in this experiment.

Observed ownership contract:
- Mu/ACPI owns enumeration; Windows programs CNTV, enters idle/WFI and performs
  virtual IAR/EOI; m1n1 owns physical timer routing, vGIC LR/HCR state and the
  physical wake needed to re-enter the guest;
- DMA is not involved in architectural timer delivery; storage, xHCI, DART and
  display mappings remain unchanged;
- Windows owns guest power policy, but m1n1 must turn a newly deliverable virtual
  timer into a hardware-visible wake edge on this Apple core;
- recovery remains the installed Stage 1 plus the immutable EXP-040 firmware and
  m1n1 artifacts.

Differences reconciled: Asahi/Linux handles its physical timer and idle wake in a
native kernel interrupt path; upstream m1n1 provides the physical EL2 boundary;
this Windows fork additionally virtualizes the GIC and must bridge HCR.VI to a
sleeping guest core.  Mu can describe the timer but cannot repair that runtime
EL2 wake contract, and a Windows input driver is outside the path.

Single changed runtime variable relative to EXP-040:
- when `hv_vgic3_update_vi()` finds Pending INTID 17 or 18 priority-deliverable
  and the previous HCR.VI value is clear, it writes HCR.VI, executes ISB, then
  sends one local physical self-IPI.  An already asserted VI, a non-timer IRQ,
  a masked timer and Active+Pending do not issue a wake.

Unchanged: 5000/100-Hz host cadence, timer comparator programming, live-LR level
synchronization, LR EOI transitions, recovery timer disabled, secondary fast
return, Mu/no-AINP firmware, Windows image, all eight CPUs, NVMe, xHCI, display
layout and observed monitor profile.

TDD and software checkpoint:
- RED: the new helper test failed to compile before the edge predicate existed,
  and the root integration test failed because `hv_vgic3_update_vi()` contained
  no timer edge wake;
- GREEN: the helper covers the true edge and all four suppressed cases; the
  integration contract requires HCR.VI write -> ISB -> local IPI ordering;
- focused root suites passed 49/49, the complete nested host suite passed, the
  complete root suite passed 258/258 in the project environment, and both diff
  checks passed;
- implementation commit `fa42daad5a4e047ed6c9e854f89410bd6b5d723e`;
  root contract/ledger commit `7466592e8011b521fea0ac9b008f5b7eca7b169d`;
  Mu `63942398cccbd98127cfecbd7f936af99c837d6f`; all tracked diff hashes are
  `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.

Exact planned clean build:

```sh
docker run --rm -v /Users/pavel/public_windows:/work \
  -w /work/m1n1_windows windows-on-m1-build:local make clean
docker run --rm -v /Users/pavel/public_windows:/work \
  -w /work/m1n1_windows windows-on-m1-build:local make -j8 APPLE_INPUT=0
```

After the clean build, copy `m1n1.macho` and the unchanged EXP-040 firmware to
`investigation/artifacts/EXP-20260814-041`, create and strictly verify a
DEBUG/monitor/both manifest, and append all SHA-256 values before launch.  The
planned launch is:

Recorded artifact before launch (UTC 2026-08-14T18:14:03Z):
- the exact clean Docker monitor build completed from m1n1
  `fa42daad5a4e047ed6c9e854f89410bd6b5d723e` with `APPLE_INPUT=0`; emitted
  signedness, unused-variable, lifetime and stack warnings are the pre-existing
  warning families also present in EXP-040;
- source manifest root `7720645e2abb5c884f0263c4aad0887a55ff9d82`, Mu
  `63942398cccbd98127cfecbd7f936af99c837d6f`; tracked root, m1n1 and Mu diffs
  were empty before the build;
- `investigation/artifacts/EXP-20260814-041/m1n1.macho`, SHA-256
  `e8ab3d56817d1b1e8223235c0a2de3afa29b5f9af7c210de12768341575c9c7a`, size
  901120 bytes;
- unchanged no-AINP `J313_EFI.fd`, SHA-256
  `0dba13c6fa652ec86900c8879babf6b48ac6a723f37f187ab99ee5f676e00ba5`,
  size 30965760 bytes;
- `MANIFEST.json`, SHA-256
  `e0700dbe9147678f1bdcb917ca8b0746d133799d17e163caa413252b68c78834`;
  strict DEBUG/monitor/both verification passed for both required artifact
  roles.  No guest image has been launched at this checkpoint.

```sh
./scripts/run-windows.sh --execution assisted --observed --debug monitor \
  --proxy /dev/cu.usbmodemC02HDNCCQ6L41 \
  --vuart /dev/cu.usbmodemC02HDNCCQ6L43 \
  --firmware investigation/artifacts/EXP-20260814-041/J313_EFI.fd \
  --m1n1 investigation/artifacts/EXP-20260814-041/m1n1.macho \
  --chainload --foreground
```

Smallest falsifiable hardware checkpoint: at the first previously freezing
countdown/black/login frame, require autonomous framebuffer change and fresh
timer IAR/EOI activity without a manually requested diagnostic IPI.  Acceptance
then requires all eight CPUs, NVMe and xHCI alive plus continuous UI and TCP/22
progress for at least ten minutes with no pause over five seconds.  A
byte-identical frame for two minutes, dead network, bugcheck/reset, repeated
physical-wake storm or long pause rejects the hypothesis.  On failure, capture
two complete snapshots and frames, terminate only the verified runner PID, and
confirm Stage 1 recovery before any further change.
