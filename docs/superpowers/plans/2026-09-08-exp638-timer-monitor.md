# Existing monitor profile for the post-fence clock failure

EXP636 kernel dump locates CPU4 at a byte load in the output verifier, with
IRQL0 and saved SPSR.I clear. The GPU completed frame14 and all pixels were
correct; byte hash and presentation had not finished. EXP637 performs16 copies
of the same9.4-second verification in user mode on CPU4 without AGX and passes.
Neither result identifies the exact timer delivery defect.

Use the existing non-verbose m1n1 monitor, not a timer policy patch. Frozen
sourcec6d10e04afdad5314e8ac1e67bc3919b094ab000, full-owner flag unchanged.
The isolated RELEASE1 control reproduced frozenEXP584 byte-for-byte:
12f18f6fa3883387c2f80fa2a92c0eeb2a1c941c672c64db634b717399b3ffd3.
Selected eleven original depfile inputs and frozen Rust library are hashed
under EXP638-timer-monitor. Original source/build and ANS worktree untouched.

RELEASE0/RUNTIME_DIAG_VERBOSE0 gives existing lock-free samples and counters,
with synchronous verbose hot-path formatting disabled. Monitor SHA256:
31e964cdc0f6ee18d1c438f4e2a39f59a5a81565a79adc92fa6bc84aa4d43fd7.
Reviewed RELEASE branches: diagnostic gates and Linux kboot-only DT transfer;
EFI raw launch does not call that Linux setup. No change to timer comparators,
FIQ routing, LR policy, root ownership, IRQ model or AGX code.

Existing profile-gate/snapshot executable tests pass;26 host telemetry/profile
tests pass; dry-run from isolated experiment cwd passes. The host uses
--debug-mode monitor with the existing EVT_TELEMETRY decoder. Absolute paths
and experiment cwd keep output separate from older telemetry.

Use the exact signed635 package and unchanged16-frame workload. After natural
bind/preflight, send the established SIGINT snapshot request once; verify
per-CPU records and the unattended handler's continue response before producer.
SIGTERM requests reboot; SIGUSR1 is not the supported snapshot operation.
This preflight can perturb timing and is recorded as part of observation.

If watchdog occurs, save kernel dump and EL2 samples. Compare CPU4 comparator,
route enables, VMCR priority, live LR state, queue/latch state and timestamps.
Do not equate a missing sample with a lost interrupt, or a monitor PASS with a
fix. No new hardware run without interpreting the captured evidence first.
Recover through the unchanged ordinary/emergency artifacts, remove the exact
test package after preservation. Kernel dump7 remains temporary with backup3.
