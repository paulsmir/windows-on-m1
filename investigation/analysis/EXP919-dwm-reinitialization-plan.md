# EXP919: distinguish adapter transition state from steady composition failure

WHY THIS HYPOTHESIS:
1. EXP918 has an active Apple output and a hardware-validated public composition
   chain/backbuffer creation path after b5291548. DWM tasks complete without
   recorded TDR, but physicalPresent0/currentDCPzero persists.
2. Current original ETW shows the same DWM1220 first creating Basic Render,
   destroying that device, then creating AppleFL10.0. The compositor retains
   its process across this transition; steady-state fresh initialization has
   not been measured. This makes transition state distinguishable from a
   permanent rendering/presentation contract failure.
3. Reinitializing only the console compositor after AppleCode0 is a smaller
   causal discriminator than a new advertised DDI/capability set or firmware
   rewrite. This is a diagnostic, not the proposed permanent fix.

WINDOWS CONTRACT: FULL GRAPHICS; unchanged package30.0.918.0. Read actual console
DWM PID/session, exact original boot and driver version. Use a held process handle;
query IsProcessCritical and refuse termination when critical/unknown. One explicit
TerminateProcess followed by WaitForSingleObject; observe whether Windows creates
a new session1 DWM while Explorer identities, boot and Code0 remain unchanged.
These process API semantics are documented by Microsoft. Automatic DWM replacement
and resulting composition are observed behavior, not a promised API contract.
Do not use DwmEnableComposition to toggle Windows11 composition.

AGX/ASAHI CONTRACT: no reinitialization of hardware/driver/firmware/caps. Existing
context destruction refuses outstanding fences, retires private state and detaches
GPUVA; process destruction waits for devices/contexts/uncertain mappings. Observed
prior DWM work is completing. Process teardown is a real Windows driver lifecycle
operation, not a forced completion or GPU reset. Any resulting failure requires
its own exact evidence and verdict, never repeated process kills.

TRANSLATION: freshly reinstall exact hash-verified918 after EXP918 was fully
cleaned. One original boot. After originalCode0/CPU8 and at least120s, run the
hash-pinned one-shot once against the PID in the fresh console observation.
Capture before/after receipts; monitor newPID, DWM ETW, physicalPresent and current
DCP snapshots. Continue original observation to at least750s. Do not also run the
composition creation client in this diagnostic; its result is already proven by918.
If visible updating desktop appears, collect direct evidence and investigate the
owning driver transition defect; process restart alone is not a production fix.

WHAT IS STILL UNKNOWN: whether fresh compositor initialization on the already
active Apple adapter starts physical presentation, or reproduces the same absence.
A freshPID withnooutput rejects the transition-only hypothesis. A critical-process/
access refusal or missing replacement is inconclusive for rendering; do not retry.
If session/boot/driver state changes, preserve the explicit cause and recover.

One variable versus the unchanged918 driver: one console DWM reinitialization.
Its early timing allows existing+120/+300/+600 current-surface observations to
capture effects; no m1n1 diagnostic change. All diagnostic observations remain
read-only. No artificialRED for this process stimulus. Verify PowerShell parser
and compile the P/Invoke declaration without running the intervention on builder.

Sources inspected: current EXP918 ETW/module/SDK-probe/frame evidence;
render-admission/src/display.c QueryChildRelations/QueryChildStatus;
lifecycle.c final StartDevice publication;
callbacks.c DestroyContext; gpuva_g3_windows.c DestroyProcess;
Microsoft IsProcessCritical, TerminateProcess, WaitForSingleObject and DWM
composition overview. Asahi/m1n1/Mu runtime remains byte-identical918; no hardware
protocol changes. Owner of the remaining uncertain boundary is Windows/DWM
adapter-to-output initialization, not untested AGX power or IRQ guesses.

Recovery: original host size/SHA gates, ordered restart to immutable377/392
ordinaryCode43, separate recovery evidence gate, exact918 devnode/package/modules/
signer/diagnostics removal, finalCode28 twice/free4GiB/shadows0. Hidden385 emergency
only. Never terminate more than the one pinned DWM instance, never Explorer or
Winlogon, never rearm/retry a failed stimulus. Exact commands/hashes/UTC in ledger.
