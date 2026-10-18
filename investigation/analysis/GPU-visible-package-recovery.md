# GPU-visible exact-package recovery

The current user AGENTS requires ordinary driver rollback through a GPU-visible
inert guest. The historical recNNNNh helper instead booted the emergency
GPU-hidden image before every cleanup. Preserve that immutable emergency path;
ordinary rollback now disables only the exact experiment's driver service before
an ordered restart, then removes its exact package with the driver unloaded.

WINDOWS CONTRACT: Microsoft sc.exe config documents start=disabled as unable to
start; CM_PROB_DISABLED_SERVICE is Code32. Sources:
https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/sc-config
https://learn.microsoft.com/en-us/windows-hardware/drivers/install/cm-prob-disabled-service
The pinned INF names AppleAgxAdmission (kernel, demand). The existing exact cleanup
already accepts an ExpectedProblemCode parameter and verifies INF/SYS/UMD hashes,
bound/staged published name, zero arms, no running service and durable reboot.
Only its expected inactive Code45 becomes Code32; package identity gates remain.

AGX/ASAHI CONTRACT: no live GPU cache, queue, DMA or power operation is changed.
The currently running KMD completes ordinary Windows shutdown. The first recovery
boot uses the same validated normal GPU-visible firmware and disabled power
broker, while Windows cannot start the experiment KMD. m1n1/Mu images, bindings,
interrupt routing and memory map are unchanged.

TRANSLATION: verify J313/admin, exactly one APPL0002, exact staged/bound package,
service and hashes, proven absent or explicit DWORD-zero G3Armed/B1Armed. Persist service Start4
through SCM and RegFlushKey plus a new Flush(true) receipt. No hot stop or unload.
Then ordered restart, wait only for this experiment's launcher, inspect both USB
endpoints and proxy under the serial guard, launch the hash-verified normal
profile. Require exactly one Code32 devnode and stopped service before cleanup.
Reuse the original removal script with Code32. Its durable ordered restart is
followed by normal-profile boot, Preflight PASS/PackageAbsent and exactly one
Code28 devnode, absent service and eight CPUs. No broad launcher kill exists.

WHAT IS STILL UNKNOWN: actual first GPU-visible disabled-service cleanup on this
J313. EXP1380 recovery preregisters that checkpoint after all render evidence was
collected. Failure stops before the next phase; emergency hidden recovery is
reserved for a GPU-visible guest that cannot be recovered, with both control
planes checked before operator action. Recovery ACTUAL must record Code32 and
final Code28/PackageAbsent evidence; source/syntax checks alone are insufficient.

Arm-source correction: lifecycle.c AdmissionConsumeGpuvaArm requires DWORD1, then
ZwDeleteValueKey/ZwFlushKey consumes it. Live keys are absent after consumption;
absence proven by successful key opening/enumeration is disarmed, while read
errors and nonzero/wrong-type values fail. Receipt preserves Absent versus
ExplicitZero. Native AST-extracted gate regression covers11 cases.

Verification before use: bash -n on the host; native PowerShell AST parse on the
builder; static identity-before-mutation/durable-PASS review. No artificial RED
phase for this workflow repair. EXP1380 experiment results precede this rollback
variable and remain immutable.

REVIEW R113: ACCEPT disarmed and service-disabled before ordinary visible boot.
R85/R47: ACCEPT existing verified normal launch manifest and exact package hashes.
R48: ACCEPT one serial guard and exact owned launcher; no global pkill.
Historical hardware/admission items: DEFER, no unrelated platform modification.
