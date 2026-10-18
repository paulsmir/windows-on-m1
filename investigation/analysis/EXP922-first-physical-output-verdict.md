# EXP922: first physical pixels, fullscreen creation still blocked

The operator supplied two photographs and explicitly confirmed the first physical
image. It consists of repeating/deformed fragments and horizontal artifacts.
Original raw photos are preserved under EXP922/operator-photos, SHAad974cbb/70d0d77d.
Matched DCP Swap9/seq2/IOVA102a0000/PA8e0110000 changes from zero to46174 nonzero
pixels out of4096000 at+120s, avgBGRA1,1,1,2, hash6005537bae205dd3. This is physical
output evidence. It does not establish a correct, updating desktop or green SDK frame.

Exact package30.0.920.0/source d16f42c0 and firmware were unchanged. The new SDK
client e1d5875c/sourcef8b7f525 ran asPID10204/Session1 on AppleLUID3f734. DeviceFL10
and two2560x1600 BGRA87/a8/misc20002 allocations/imports all succeeded. The client
then blocked in CreateSwapChainForHwnd; it never logged returned fullscreen state,
RTV creation, clear or Present. Its stopped task reports267014 withPT1M limit.
Partialstdout476B SHAdd1dfc23 was independently hostverified and the exact stopped
task removed afterward. Native SDK log has40lines and no failed resource callback.
No repeat or capability/NO_REDIRECTION change was made.

DWM physical/virtualPresent remain0, allPresentDDIcounters0 and SourceAddresscount1
is unchanged before/after. Therefore the physical-buffer writer is still unknown.
Photos alone do not select a pitch/tile/format fix. Current presentation resource
source explicitly uses AIL_TILING_LINEAR/DRM_FORMAT_MOD_LINEAR. Do not blindly detile.

SSH temporarily timedout/refused while NVMe reinitialized repeatedly. One SIGINT
to verified liveowner10068 capturedCPU/IRQ state and documentedcontinue, not reboot.
SSH returned in the same originalboot00:07:17.442271Z/Code0/CPU8/DWM1228; Explorer
5316 was replaced by5896. No newMemoryDMP/LiveKernelReport or capturedTDR. The newest
System1001 isoldEXP91921:04:10, not this experiment. A kernel crash was initially a
hypothesis from NVMe activity, then remained unproved; do not attribute old119 here.
The signal may have affected progress, so this does not establish natural stability.

Original15file hostgatec67ab3d6; frozencontrol18 gate8f0e663f includesphotos, raw
before/after frame, monitor, CPU snapshot and partialSDK receipts. Lateoriginal
circularETL returned noSDK PID records and cannot identify the blocking wait.
Initial decoder failure and independentempty query are retained. Recovery14file
gate5a2552d7 precedes Code43 exact920/oem5cleanup. Finalnormal377/392 Code28 verified
01:31:42 and01:33:18 onboot01:28:45.904514Z: oneAPPL0002/no package/module/service/
signer/arm/diagnostics, CPU8/disks2USB5, SSH/RDPservice/autologon1, free4868280320,
shadow0. No hiddenboot or forcedreboot; manual operator action was unnecessary.

Next is an early external MiniDumpWriteDump snapshot of only the hash-verified SDK
process during the wait, before tasktermination; see EXP923-sdk-creation-stack-plan.
The guarded helper a9c3013e passed builder AST/PInvoke checks, but923 isnotstaged/run.
Use exact public symbols and observed wait to choose the owning source correction.
No driver change or desktop success is claimed in922.
