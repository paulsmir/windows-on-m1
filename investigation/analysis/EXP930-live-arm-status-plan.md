# EXP930: identify the same-boot arm-gate failure

WHY THIS HYPOTHESIS:
- EXP929 live PnP update returned0 and installed929/oem6 in the same Windows boot, but StartDevice stopped at stage1/c00000bb before GPU initialization.
- A separately preregistered rearm/device-only restart also consumed the verified DWORD1 and returned the same status. This excludes installer-finally cleanup alone. No new ARM_CONSUMED serial receipt appeared, while the launcher remains active.
- The current boolean helper collapses registry deletion/flush and hypervisor acknowledgment failures into STATUS_NOT_SUPPORTED. AddDevice enforces a nonzero generation;928/929 HVC machine bytes are identical. Repeating the restart without identifying this branch cannot distinguish the remaining causes.

WINDOWS CONTRACT:
FULL GRAPHICS StartDevice and supported PnP device restart; no OS reboot. Registry API failures must be observed without bypassing required one-shot deletion/flush. Existing return values and restart admission stay unchanged.

AGX/ASAHI CONTRACT:
This boundary is before GPU access. No RTKit/UAT/queue/render change is relevant. Current m1n1 hv_guest_ipa_pa.c and hv_exc.c acknowledge a valid arm-consumed HVC and log its generation; the handler has no CPU0-only restriction. Mu local reserve validation remains before the arm gate and has already passed.

TRANSLATION:
Record failure phase1=open,2=query/type/value,3=delete,4=flush,5=missing generation,6=HVC acknowledgment. Record native NTSTATUS, raw HVC result, generation and compiled package build. The diagnostic writes happen after closing the original registry handle; they do not replace the original flush, consume a second arm, retry, or change success/failure. This package inherits929 preparation correction unchanged.

WHAT IS STILL UNKNOWN:
Which exact registry/HVC operation refuses same-boot startup. Install diagnostic930 unarmed into the existing Code43 device, then one explicit rearm and device-only restart with no reboot flag. Read exact phase/status/build and serial receipt. Do not bypass the gate or perform a third equivalent unchanged retry.

Sources: lifecycle.c AdmissionConsumeGpuvaArm/AdmissionReportGpuvaArmConsumed/AddDevice; receipts.c existing key helpers; m1n1 hv_guest_ipa_pa.c/hv_exc.c; Microsoft PnPUtil and DXGK StartDevice contracts previously read. Device/registry lifecycle belongs to Windows/KMD, HVC acknowledgment to m1n1. Immutable377/392 and exact928/929 artifacts remain preserved; user prohibits launching recovery that reboots.

Verification: existing actual resource/admission replay GREEN; zero-warning pinned WDK and source/sign/hash gates before package install. Receipt-only instrumentation does not require artificial RED. Hardware discriminator is exact nonzero failure phase, not a successful screen claim.
