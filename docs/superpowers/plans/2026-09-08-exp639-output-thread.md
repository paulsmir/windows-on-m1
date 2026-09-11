# EXP639: driver-owned output thread

## Evidence and contract

EXP636 and EXP638 kernel dumps independently resolve CPU4 at the full-frame
byte scan/hash inside `AdmissionOutputWorker`; the saved IRQL is zero and the
guest IRQ mask is clear. EXP638 reached eight complete frames and entered the
ninth output operation before the same watchdog. EXP637 ran the exact verifier
sixteen times on CPU4 from an ordinary user thread without a watchdog.

Microsoft's System Worker Threads contract says work items use a limited pool
and may contain only short operations; long processing should use a driver
created thread. `PsCreateSystemThread` supplies a kernel system thread at
PASSIVE_LEVEL and requires the driver to terminate it with
`PsTerminateSystemThread` and close its handle with `ZwClose`.

- https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/system-worker-threads
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-pscreatesystemthread

This is the first proven contract violation adjacent to the failure. No timer,
vGIC, firmware, AGX, DCP, memory mapping or rendering ABI change is justified.

## Implementation and ownership

Keep the existing single-generation output queue and completed/display leases.
Replace only the `IoQueueWorkItem` execution owner with one system thread per
platform runtime. Completion schedules the same generation and signals a wake
event. The thread begins the exact generation, runs the unchanged verifier and
presentation, finishes it, and signals idle.

Stop sets the platform stopping gate before requesting thread stop. A queued or
active generation drains before exit. New schedules fail after stop. The thread
marks exited, signals the exit event, calls `PsTerminateSystemThread`; destroy
waits for this state, confirms the queue is idle, then calls `ZwClose` exactly
once. A failed thread creation resets the local queue and follows existing
reverse cleanup.

The GPU worker remains the existing short `IoQueueWorkItem`; only the known
nine-second verification/presentation path moves. Allocation, residency,
completed-output and display ownership remain unchanged.

## Offline and hardware proof

The executable state test was RED when an unstarted/stopping queue accepted
work and when active work could exit. It is GREEN with start, drain, stop,
exit and post-stop rejection. The full render-focused suite is 122 GREEN.
Pinned WDK/SDK 26100 build, code analysis, Universal validation, Inf2Cat,
TestSign, version and hashes are required before staging.

EXP639 uses the ordinary release EXP584 m1n1 and current Mu/full-owner profile,
not the monitor. The sole candidate difference from the failing EXP636 package
is the output execution owner. Run the unchanged sixteen-frame producer. PASS
requires sixteen exact full outputs/latches, monotonic fences/sequences, HOLD,
explicit owned-primary retirement and clean destruction. A run that only avoids
the watchdog is not enough. On failure preserve a kernel dump and exact phase
before package cleanup and ordinary recovery.
