# EXP924: snapshot only after both fullscreen backbuffer imports

WHY THIS HYPOTHESIS: EXP923's 83,612-byte exact SDK dump captured MapGpuVirtualAddress
inside D3D11 device creation, before the proven current boundary. The matching UMD
trace later returned successfully from that map and both BGRA imports. Thus the
fixed-delay capture does not identify the fullscreen wait. EXP922 and EXP923 both
stop before any mode/Present callback after accepted backbuffers.

WINDOWS CONTRACT: FULL GRAPHICS workload and exact920 package remain unchanged.
The documented external MiniDumpWriteDump normal/threadinfo snapshot is unchanged.
An owned-process observer now waits for exactly two successful native resource exits
and two exact BGRA87/dimension3/binda8/misc20002/native2560x1600 frontend records for
that same PID, then snapshots once two seconds later. No arbitrary capability bits.
AGX/ASAHI CONTRACT: unchanged linear presentation resources, UAT, queues, firmware
and KMD/UMD. EXP923 MapGpuVirtualAddress callback E_PENDING later completed; its
transient presence on a stack is not a proven mapping defect.
TRANSLATION: resource lifecycle checkpoint -> exact owned SDK stack -> native/runtime
blocking frame. Precompile observer before launch. Hold PID plus start time and
image identity. Stop/freeze owned ETL immediately after dump. Hash only closed
observer/dump files; the previous wrapper attempted to hash a redirected stdout
while SDK held it open and failed before its combined receipt. End only the held
owned SDK in finally after the diagnostic, including failure paths; stable stdout
is hashed afterward. No orphan or prolonged no-Present stability trial.
WHAT IS STILL UNKNOWN: exact blocking call after both imports; writer/layout of
physical corrupted pixels. If checkpoint is absent by35s, record failure and do not
claim a post-import snapshot. Existing task remains bounded at60s; no automatic retry.

Verification: builder PowerShell AST zero errors and DbgHelp PInvoke compile; this
is diagnostic-only instrumentation and lifecycle repair, no artificial RED test.
Reference sources unchanged from EXP922/923, official MiniDumpWriteDump/MINIDUMP_TYPE
links in EXP923 plan. One independently observed instrumentation variable. No new
hardware until original EXP923 evidence gates, ordinary Code43 exact package cleanup,
then normal Code28 twice and fresh disk/dual-control checks. Ledger/manifests required.
