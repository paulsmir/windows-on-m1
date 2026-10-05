# EXP934: submit GPUVA work without a management ping prerequisite

WHY THIS HYPOTHESIS:
1. Exact933 kernel dump, matchedPDB: adapterffffd307f7a02000, SchedulerFaulted265165=0x40bcd, exactbackend_platform_windows.c:3021. Activepacket9189 was activated in software, but worker returned at failed pre-submit heartbeat before BeginJob/manager/backend submit. Backend remained Ready. Schedulercompleted9188, active9189; TDR0x116 follows.
2. Heartbeat receipt evenSequence134, Calls67, Fence9189, Result3=Timeout, Start153791ms, End158890ms, Deadline154291ms. Eight runtime wakes arrived (Rx553->561, endpoint20/payload0042000000000000), CPUReady/Running1, StopIdle, CrashlogCrashed0; outboxcontrol00301401 was nonempty. A missed management deadline is not evidence that submitted GPU work failed: this packet was never sent. Do not claim the cause of the 5099ms interval is known.
3. Inspected localAsahi77cb8f24c2381a8abb7272d7bbdec548d6426a8a gpu.rs start_op/kick_firmware and workqueue.rs JobSubmission::run: ready work is published/kicked and completed by events/stamps. m1n1 agx/render.py run queues and wait use the same ordering without a per-job management pong. Its management ping is a separate facility.

WINDOWS CONTRACT: FULL GRAPHICS. DxgkDdiSubmitCommandVirtual accepts well-formed virtual DMA and driver restores the correct address space before actual submission. Completion/fault must correspond to the submitted packet, not an extra diagnostic exchange that was inserted between software activation and hardware submission. Microsoft https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_submitcommandvirtual and TDR documentation inspected. No capability, model, fence-completion or reset implementation is changed.

AGX/ASAHI CONTRACT: RENDER_ONLY. Asahi's start_op uses a firmware kick; queue run publishes write pointer and sends RunWorkQueue, then completion comes from firmware events/stamps. m1n1 renderer directly runs TA/3D queues and waits for stamps/events. Existing Windows queue provider already publishes the run message and doorbell. Management heartbeat is not a required prerequisite for every work item. No external code copied. Startup management handshakes, memory/UAT validation, ownership, queue guards and completion/fault checks remain. Source inspected: shared ASC/session transport, backend worker and queue provider, local Asahi sources, m1n1 render/mgmt, current MuR143/pinned reserve ownership unchanged.

TRANSLATION: In FULL GPUVA_G3 builds only, omit the pre-submit management ping gate. Continue through existing BeginJob, manager preparation, backend submit and real completion polling. Legacy diagnostic profiles retain their heartbeat behavior. Do not extend the timeout, forge pong, fake completion, clear SchedulerFaulted after a real fault, or ignore firmware crash evidence. Zero heartbeat calls means not invoked, not a successful ping.

WHAT IS STILL UNKNOWN: Whether removing this false prerequisite allows the boot composition workload to reach physical Present, or reveals another submission/completion defect. Why the original worker interval was5099ms is not proven and is not disguised as a firmware crash. One newhardware discriminator with nootherfunctionalchange, usingfixedHVCfirmware933.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Compile and execute the actual worker span from software packet preparation to backend submit. Inject the observed management timeout with an otherwise valid activated G3 job: old code faults without sending; corrected G3 sends exactly once, does not fake completion, and still blocks if BeginJob or manager preparation fails. Also verify legacy profile still rejects heartbeat timeout. This is a runtime control-flow regression, not a string-presence assertion.

Recovery: preserve933dump/trace/hashgate8854c883 and allsource/PDB artifacts. CurrentnormalGPUvisible guest is933Code43unarmed; removeexact933 package withhashes andorderednormalrecovery before fresh934stage. SameMuR143, newHVCm1n1bdcf8715, sameprofile/caps/layout. Immediatefirstready/Present check; no stabilitywaitwithoutcorrectphysicalimage.


Pre-hardware source audit correction: `AppleAgxPlatformProviderDrainEvents` consumes
shared-memory event records, but heartbeat was the only runtime consumer of ASC
mailbox messages. Omitting it alone would allow that mailbox to fill. First934build
is superseded before installation. The complete single change REPLACES synchronous
per-job ping/pong with bounded nonblocking notification draining before submission
and during completion polling. Empty is success; at most64 available messages per
pass. Recognized runtime wake and management pong are consumed; boot-provisioned
crash notification marks crash/fails; unknown payloads and MMIO errors fail closed.
No waiting, new ping, timeout extension, or fabricated completion. Startup/shutdown
management remain unchanged. New native ASC/session tests cover empty/queued/bounded
messages, no transmit or clock/pause use, unknown message and crash rejection. This
is necessary ownership continuity, not a separate optional hardware variable.
