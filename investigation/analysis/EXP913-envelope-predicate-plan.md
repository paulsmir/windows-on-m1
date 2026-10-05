# EXP913: distinguish the first DWM envelope rejection

EXP912 passed 750.129 seconds without the EXP911 paging bugcheck, but current DCP swap10/seq2 remained zero at +120/+300/+600. This is not desktop validation. Its two armed DWM1244 contexts fail stage1 at flags0x80, fences3336 and8007, immediately following private/preempt fences3335 and8006; both record contextState0x18 (active and Win32 transport, not poisoned). First-stage diagnostics do not identify the failed predicate. Three retained Apple-era D3DKMTPresent events return success for source80002240 and flags8000 (RedirectedFlip); these do not demonstrate display submission. ETL windows contain gaps and a clock bias.

WHY THIS HYPOTHESIS:
1. Both DWM failures are valid nonpaging resubmissions immediately after a discarded queued fence, making a transient backend admission state more likely than malformed original packet data. Parsing has not run.
2. AdmissionG4SubmitVirtualEnvelope rejects when AdmissionPlatformRuntimeReady is false. That function requires both Backend.Phase Ready and WorkScheduled zero. AdmissionDispatchQueuedWork explicitly handles a preceding worker still returning and retries when AdmissionPlatformWorkerFinished clears WorkScheduled.
3. Other process/root/local-view guards share the same stage1 receipt, so the exact condition is still unproven. No behavior fix is justified solely by stage1.

WINDOWS CONTRACT:
FULL GRAPHICS WDDM3.0, pinned WDK26100. SubmitCommandVirtual STATUS_INVALID_PARAMETER means malformed DMA/private data and puts the device in an error state; other errors cause bugcheck. GPU preemption resubmits nonpaging work with a new fence. Sources: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_submitcommandvirtual and https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-preemption and DXGKDDI_PREEMPTCOMMAND. No DISPLAY_ONLY/KMDOD assumption.

AGX/ASAHI CONTRACT:
Unchanged previously audited m1n1 9320da31 broker/DCP path and Mu0dac6871 APPL0002/R143 contract, reference Asahi77cb8f24 DCP/AGX firmware ownership. EXP912 hardware-manifest pins firmware, ADT/memory/IRQ inherited from the same full-owner launch. This diagnostic runs before any AGX command, UAT mutation, or output mapping. No new MMIO, IRQ, power, or firmware sequencing is proposed.

TRANSLATION:
KMD owns Windows admission and preemption, CPU dispatch and backend worker lifetime. Backend owns actual RTKit/AGX completion; m1n1 owns assisted hardware transport/DCP and Mu exposes ACPI. Preserve all behavior and evaluation order. Capture the exact first failed outer predicate at evaluation, including the first runtime-ready predicate, in the existing per-context first-envelope record. Never infer a failure by rereading racy state afterward. Diagnostic frame ABI3 adds two integers; producer and probe are built together. Existing static/replay tests verify packet behavior remains equivalent. Diagnostics alone do not require artificial RED.

WHAT IS STILL UNKNOWN:
Which outer predicate rejected those actual DWM resubmissions: runtime (and which runtime condition), process/root/context, or local memory view. One diagnostic run distinguishes these; no cap probing, rendering rewrite, speculative readiness relaxation, or synthetic completion.

Inspected owning files: render-admission/src/gpuva_g3_windows.c (outer admission/private resubmission); backend_platform_windows.c (RuntimeReady, Submit, WorkerFinished); work_queue_windows.c (dispatch and retry); scheduler_windows.c (preemption discard/notify); render_qualification.h and one-shot/apple_agx_blt_probe.c (receipt ABI). Prior source/firmware audit is retained in EXP911/912 references rather than repeated.

Smallest hardware checkpoint: first armed DWM branch7 captures nonzero Predicate (1..14), and when Predicate13, RuntimePredicate (1..11), matching flags/fence/context. Recover with immutable377/392 GPU-visible and exact package cleanup after host evidence; hidden385 only if ordinary recovery fails. Candidate package is not retained without visible updating desktop. Complete EXP912 recovery before any next build/stage.

Offline validation: the actual ReadyEx function replay covers all1024 combinations plus null adapter/runtime, checks first failure and balanced lock operations. Actual G4 envelope replay distinguishes predicate13/runtime8 from context poison6 and still admits the existing high local output. These are diagnostic equivalence checks, not evidence that runtime8 caused the hardware failure.
