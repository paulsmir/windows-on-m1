# EXP-20260827-128 verdict

Rejected without retry.

The one permitted G2 boot reached responsive eight-core Windows with
AppleInput, stornvme and USBXHCI running, zero critical events and zero
stornvme Event 129.  The exact `oem17.inf` package bound to
`ACPI\APPL0002\0`, but PnP stopped before the MMIO boundary with Problem 31
(`CM_PROB_FAILED_ADD`) and ProblemStatus `0xC0000182`
(`STATUS_DEVICE_CONFIGURATION_ERROR`).  No fresh MMIO map, ASC subview or
unmap receipt exists.  The old stage-7 lifecycle values in the persistent
device key predate this package and are not accepted as current evidence.

No GPU register access, firmware, RTKit, interrupt connection, active UAT,
queue, command, rendering, presentation or display ownership occurred.  The
candidate shut down normally.  Exact recovery removed only `oem17.inf` and
signer `A40D8EC7010BB5D4E14792C360737F79F79D0151` without `/force`.  Its first
boot recorded six Event 129 resets and therefore failed the recovery gate; the
required cleanup boot ended with eight CPUs, no package, signer, service or
present APPL0002, running input/NVMe/xHCI, zero critical events and zero Event
129 through the final quiet window.

The next authorized work is offline only: make translated-resource and
lifecycle breadcrumbs available to every qualification profile and determine
why the MMIO-only package returns `STATUS_DEVICE_CONFIGURATION_ERROR` before
mapping.  EXP-128 must not be retried.
