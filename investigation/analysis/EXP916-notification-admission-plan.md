# EXP916: accept commands while preemption notification is in flight

WHY THIS HYPOTHESIS:
1. EXP915 original kernel dump504e0652, exactpackage914 PDB, proves0x119/2/80000011 at686.928s onCPU0 in dxgmms2!VidSchiSubmitPagingCommand. Input is a valid new system paging packet: flags1, fenceF972, DMA0x4101150/208bytes,13private120-byte records. It is not a resubmission or malformed G4 packet.
2. Adapterffffe1894cc13000 has completed=lastSubmitted=F971, active/queued/CPUqueue/dispatched/pagingPending/faulted allzero. Preemption117 remains NotificationClaimed phase3 with cutoff/lastCompletedF971. Capacity and stale-fence explanations are excluded by actual state.
3. Both queue admission functions use DispatchBlocked, which rejects all non-idle preemption phases. The notification is already being delivered when Windows submits new work, before the driver commits its local phase. Actual production notification plus shared scheduler replay is RED for this exact F972/phase3 case.

WINDOWS CONTRACT:
FULL GRAPHICS WDDM3.0/pinnedWDK26100. A well-formed SubmitCommandVirtual must be accepted; STATUS_DEVICE_BUSY is not supported backpressure and triggers a bugcheck. Preemption notifications go through NotifyInterrupt/QueueDpc; Windows work may arrive while notification delivery is still returning (directly proven by dump). Paging replay may reuse an owned discarded fence, while new commands retain increasing IDs. Microsoft SubmitCommandVirtual, GPU preemption, QueueDpc, and SynchronizeExecution contracts apply.

AGX/ASAHI CONTRACT:
No active AGX command at the failing boundary. Queue acceptance is software ownership; hardware dispatch must remain blocked until preemption notification commits. No UAT, firmware, memory-map, MMIO, IRQ, rendering, display, or capability change. Keep corrected worker-retirement deferral038cd941.

TRANSLATION:
Separate queue admission from dispatch blocking. Admit valid new fences and owned paging replay when phase is Idle or NotificationClaimed; preserve capacity, monotonic/ownership, and all other phase guards. Activation/dispatch remain blocked throughout NotificationClaimed. After successful notification commit, explicitly kick queued work outside SchedulerLock, covering a DPC that already ran and found dispatch blocked.

ATOMIC CONTRACT:
Admission during delivery plus a post-commit dispatch kick form one reentrant notification invariant. Admission alone can lose the only wakeup; dispatching before commit would violate preemption ordering. No unrelated scheduler capabilities or queue sizes change.

WHAT IS STILL UNKNOWN:
Whether the corrected mapping closes the observed F972 failure over a fresh hardware window, and the independent unresolved DWM display-surface path. EXP915 DWM traces and the console topology probe remain relevant to physicalPresent0; do not conflate this scheduler correction with desktop success.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Use actual notify/commit code and the real shared scheduler. Simulate the OS DPC submitting the exact dump-shaped next fence before QueueDpc returns. It must be queued but not activated until commit, and must be activated afterward even if that DPC has already finished. Exercise same-ID owned paging replay too. Preserve earlier worker-reservation/no-premature-notify, pending-completion, stop/reset, stale/duplicate, capacity, and pre-notification guards.

Inspected: exact dump/PDB and args/context/device/adapter chain; shared apple_agx_scheduler.c QueueFence, QueueResubmittedPagingFence, DispatchBlocked, Claim/CommitBoundaryPreemption; scheduler_windows.c TryNotify/WorkerFinished/Dpc/PreemptCommand; work_queue_windows.c real CPU enqueue and dispatcher. Source/report artifacts root.local/experiments/EXP915-dwm-boot-events/kernel-*-cdb.log and notify-admission-RED.log. No hardware until EXP915 exact cleanup and stable ordinary recovery; next one behavior experiment retains the EXP915 DWM provider and its bounded logger so only admission/wakeup behavior changes. The already-built read-only console topology probe runs after the baseline window. Recovery immutable377/392; hidden385 only emergency; every evidence/hash gate retained.
