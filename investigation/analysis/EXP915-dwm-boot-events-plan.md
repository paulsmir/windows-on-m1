# EXP915: observe DWM's choice of display surface

WHY THIS HYPOTHESIS:
1. EXP914 eliminates the measured render preemption handoff defect. Actual Apple DWM queue5 is preempted at2494, resubmitted2495/preempted, then2496 completes. Both tracked DWM contexts have no envelope rejection through772.839s; standard physical Present counters remain0 and matched DCP surfaces remain zero.
2. Session1 enumeration proves Apple4064d has one source and render/display/post flags. Native UMD forwards the received opaque DXGI context and returns three successful callbacks; no current DWM UMD rejection is logged. A stack-only snapshot shows the compositor in CScheduler::WaitForWork, not blocked in UMD or a GPU wait.
3. Current DXGKrnl-only ETL cannot show why DWM accepts/rejects its display-surface candidate or stops scheduling composition. Installed Microsoft DWM provider metadata exposes DISPLAYSURFACE_SWAPCHAINCANDIDATE289, REJECTCANDIDATE290, CANDIDATEATTRIBUTES291, HWDEVICE409/410, and scheduler/device-state events. This is a concrete observer for the remaining Windows decision.

WINDOWS CONTRACT:
FULL GRAPHICS WDDM3.0 with the same package914 and pinnedWDK26100. Microsoft DXGIDDICB_PRESENT requires copying the opaque DXGI context; current code does so. Existing DXGI_STATUS_NO_REDIRECTION remains because the documented D3D9 shared-hardware presentation contract is not implemented; do not toggle it experimentally. Microsoft AutoLogger supports multiple providers beneath one bounded session; Enabled1, EnableLevel5, MatchAnyKeyword0 (all keywords), EnableProperty2 (session ID) are documented registry settings. Source: https://learn.microsoft.com/en-us/windows/win32/etw/configuring-and-starting-an-autologger-session and the existing pinned DXGI/WDK contracts.

AGX/ASAHI CONTRACT:
No package, firmware, memory, power, IRQ, UAT, queue, scanout, or render behavior changes. Reinstall the exact EXP914 signed package after its complete rollback, using immutable full-owner m1n1/Mu artifacts. Retained corrected behavior is source038cd941/package source49b4c56c; this is not a new driver build.

TRANSLATION:
Add only Microsoft-Windows-Dwm-Core provider9e9bba3c-2e38-40cb-99f4-9e8281425164 to the existing EXP801DxgBoot circular256MiB AutoLogger. Resolve and verify the provider on Windows; read back settings. Existing first-SSH periodic copies and original collector preserve it alongside DXGKrnl in the same ETL. Existing whole-session stop/removal handles cleanup. No second logger, additional tracing stacks, synthetic Present, or new capability. The extra ETW observer can affect timing; record that diagnostic difference explicitly.

WHAT IS STILL UNKNOWN:
The internal DWM display-surface/device decision and first HRESULT/reason before physical Present. One early DWM trace distinguishes candidate rejection, software/fallback device selection, and a compositor that simply schedules no physical present. A hardware run is justified only to observe that current internal Windows ordering; flags are not being guessed.

Source/specs inspected: native Mesa overlay _Present and Device.cpp adaptation, agx_d3d10_windows.cpp PresentationSubmit, umd.c AdmissionUmdSubmitPresent, WDK dxgiddi.h and d3dkmthk.h, display.c source visibility and scanout_windows.c, Mu J313 DSDT includes (no newly inferred lid/register state), Microsoft DXGI and AutoLogger docs, installed Dwm-Core provider metadata. Recovery power metadata ACLine1/no system battery/AC VIDEOIDLE0 excludes the configured AC inactivity timeout; no power policy was changed.

Smallest checkpoint: first frozen original ETL contains DWM PID/session and candidate/device/scheduler events before and after Apple startup, with decoded failure/reason or selected path. Raw HRESULTs and event schema remain evidence; do not infer an unlogged error. Preserve original750s and DCP+120300600 requirements. If event loss prevents attribution, mark inconclusive rather than guessing a patch. Restore immutable377/392 Code43, verify original/recovery hashes, remove exact package and whole logger, and verify finalordinaryCode28 twice. Package914 is not carried installed across experiments.

Verification: PowerShell parser and primary provider metadata; staged registry readback and manifest SHA gates. This is receipt-only instrumentation; no artificial RED test or WDK rebuild is required. Driver tests and native artifacts remain the exact EXP914 verified ones.
