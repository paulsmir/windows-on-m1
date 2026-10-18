# EXP939: remove GPU-idle synchronization from the DWM receipt

WHY THIS HYPOTHESIS:
1. EXP938 repaired pixel corruption: CPU-upload/full-GPU-copy, shared GPU-clear/readback, and cross-process shared pixels all pass with0 errors among4096000. Windowed Present is still OCCLUDED; fullscreen creation did not return within12s.
2. The current paired stack (SDK1420 and DWM1224, captured03:54:03Z) places the SDK in DXGI focus-proxy creation waiting on DWM ALPC. DWM's LPC thread is in AdmissionUmdGpuvaFrameArm -> EscapeCB -> NtGdiDdDDIEscape, called before submit during a D2D constant-buffer update. This is one observed wait boundary, not proof that it is permanently stuck.
3. The actual FrameArm handler only validates owner/context and updates CPU diagnostic fields under a mutex/nonblocking claim. Its HardwareAccess=1 requests Windows level-two synchronization (GPU idle, no DMA in scheduler) on the normal DWM submission path. That synchronization is unnecessary for these operations and violates diagnostic behavioral equivalence.

WINDOWS CONTRACT: FULL GRAPHICS driver, software-only private diagnostic escape. Microsoft D3DDDI_ESCAPEFLAGS says HardwareAccess selects level-two synchronization; the level-two specification guarantees idle graphics hardware and no DMA work passing through the scheduler. Flags0 preserves normal adapter synchronization; NoAdapterSynchronization is not selected. No DDIs, caps, NO_REDIRECTION policy, or WDDM version changes.
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_d3dddi_escapeflags
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/threading-and-synchronization-second-level
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_escape

AGX/ASAHI CONTRACT: inspected UMD AdmissionUmdGpuvaFrameArm and its submit/copy call sites, KMD AdmissionGpuvaG3FrameArmEscape, and actual AdmissionDwmFrameArmWindows. No hardware, broker, UAT, MMIO, firmware queue, power or DMA operation occurs in this call graph. Existing Asahi-derived render/queue/fence behavior, m1n1 broker and Mu/ACPI contracts remain unchanged. Actual938 trace proves hardware pixels; no new hardware values are invented.

TRANSLATION: UMD sends HardwareAccess=0 for this receipt only. KMD acceptsFlags0 and the legacyFlags1, rejects other flags, and retains every PID, process, device, context, size, alignment and IRQL check. The existing state mutex and nonblocking diagnostic claim remain. A dropped receipt stays best-effort and never becomes fabricated rendering success.

ATOMIC CONTRACT: One private request contract between its UMD producer and KMD validator. The new software request would be rejected by the old Flags==1 check; update both ends together. Other real hardware escapes, private RELEASE, submission, fences and memory ownership do not change.

WHAT IS STILL UNKNOWN: Whether removing this unnecessary synchronization lets the DWM LPC request and fullscreen creation complete on the current Windows scheduler. One short identicalfullscreenSDK test distinguishes that. Record the first callback/return or paired stacks after12s, then stop. Windowed Present and physical output remain separate checkpoints; no stability wait before correct output.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Replay the actual UMD request builder, KMD validator and CPU receipt writer. Old UMD requests GPU-idle entry and old KMD rejectsFlags0 (both RED). Fixed path records while a simulated GPU is busy without requesting that admission, and preserves owner/flag/IRQL/alignment guards, legacyFlags1 compatibility and duplicate/non-DWM suppression underASan/UBSan. The shim models the documented admission requirement; it does not claim to reproduce the internal Windows wait duration.

Recovery: preserve938 original evidence and pixel-positive package before ordinary377/392 rollback. Remove exact938 package, durably verify one inert Code28 devnode/no package/service/signer/files, then stage only hash-verified939 after native/WDK/sign/PDB gates. Firmware remainsbdcf/MuR143. Keep the AuxFBInfo issue deferred and do not combine a presentation-status change.
