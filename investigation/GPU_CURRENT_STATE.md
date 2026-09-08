# GPU current state

Updated 2026-09-08T17:15Z. Main process only; no agents.

## Current machine / next boundary

EXP643 is currently installed and naturally bound on the full-owner release
platform: APPL0002 Code0, exact oem5/INF/SYS hashes, service Running,8CPU.
The corrected boot is stable and SSH works. Interactive task ran in session1:
AGX Render/physical completion fence269 PASS, but D3DKMTPresent returned
STATUS_GRAPHICS_PRESENT_DENIED before KMD Present. All producer objects
teardown0 and no fresh system events.

Current first unknown remains standard Windows presentation. EXP641 proved
only that an SSH session0 producer cannot acquire exclusive VidPN source0:
D3DKMTSetVidPnSourceOwner returned STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE before
SetDisplayMode, Render or Present. KMD Present/SetVidPnSourceAddress remain
untested. EXP642 attempted the DWM-coexisting interactive windowed BLT route,
but its scheduled producer never ran: exact dump localizes a pre-producer CDD
Present crash to the new diagnostic reading DXGK_ALLOCATIONLIST through the
wrong pAllocationInfo stride. Commit9beed62 corrects only that view. Next is
fresh EXP643 with the unchanged interactive HWND/BLT discriminator.
EXP644 late launch changed Present denial to STATUS_GRAPHICS_PRESENT_OCCLUDED.
DWM is in a reproducible dwmcore MILERR_DEVICE_CREATION_FAILURE restart loop;
WER shows DXGI/D3D11/WARP but not the Apple UMD. Next is one scoped DWM
LocalDump capture and exact device-creation analysis, not another BLT retry.

## Hardware proof retained

- EXP475/477/478: retained-root/context0/RTKit/firmware/native initdata,
  BackendRuntimeStart, arena/context/queues.
- EXP581/585/586/588: Windows-originated physical TA/3D, exact output,
  completion and Windows fences. Completion ingress remains polling.
- EXP591: physical panel scanout photo.
- EXP631/632: two full2560x1600 outputs/latches,15s HOLD, owned-primary
  retirement and all teardown statuses0.
- EXP634: four exact repeated full frames and latches.
- EXP636/638: kernel dumps prove recurring0x101 CPU4 during uninterrupted
  output byte hash over mapped DXGK noncached physical memory at IRQL0 with
  guest IRQ mask clear. Aggregate monitor telemetry did not establish a
  timer/vGIC fault.
- EXP637: same verifier over user pagefile-backed noncached memory, fixed CPU4,
  sixteen9.4s scans PASS.
- EXP639: dedicated system thread satisfies Microsoft's work-item contract but
  does not eliminate0x101; shared worker pool hypothesis rejected.
- EXP640: sixteen Windows Render/Patch/Submit physical TA+3D operations,
  exact completions/fences256–271, sixteen full4096000-pixel results with
  alternating hashes, sequences3–18 and16 A408/D589 latches. Two exact owners
  reuse offsetsfa0000/1f40000 and PAs9bcf90000/9bdf30000. No stale/duplicate
  completion, active-surface write or corruption.
  - HOLD15s: all16 records byte-exact, device ACTIVE, Code0/service Running,
    8CPU/NVMe2/USB5/keyboard1, no fresh41/1001/129.
  - Retirement: retained Windows primary sequence19 ownerffffd3817b01f670
    offset0/PA9bbff0000, exact D589; allocations/contexts/paging queue/device/
    adapter all destroy/close0; producer result0; final uptime205s.
  - Physical repeated color observation remains pending/uninstrumented.
  - This is direct full-frame AGX output plus private qualification presentation,
    not standard Windows Present/DWM/OpenGL.
- EXP641: exact standard-flip candidate bound Code0, but exclusive VidPN source
  acquisition returned0xC01E0342 before Render/Present. All WDDM objects
  teardown0 and clean ordinary recovery PASS. Exclusive session0 route is
  rejected; no standard-present hardware readiness bit changes.
- EXP642: 0x7E c0000005 at AdmissionDdiPresent line128 before scheduled task;
  wrong diagnostic union view read handle0x2. Windowed BLT producer remains
  untested. Exact cleanup and ordinary recovery PASS.

## Validated architecture/change

Commitb17e53a5fe7ae2b52481117080be7c559e79a5b4 gives long output work a
per-runtime driver-owned PASSIVE thread with explicit wake/drain/stop/exit and
handle close. Commit6a7e3aa4c976c7cc66077aa2099a77f27b18208f bounds mapped
noncached output reads to256KiB and performs1ms nonalertable progress waits.
The verifier keeps full pixel/poison/guard/FNV results and publishes atomically.
AGX, firmware, queues, fences, DCP and allocation/display ownership unchanged.

EXP640 exact30.0.640.0 hashes:
ZIP2de1a31d762773134824b40e48554d093b935d2ece6dc4f9095f28444f96113f;
SYS2f2cbcf88ecefb5eed4af365a50d84eb50b9ab9a04f0311505d8100ccb33ac67;
INFba16ad7e204f1269cb0a4503065ca1606e80feb6d10a37441bb7985eb701a13b;
CATcc0a6a3dbf6a432122de76c25bae82f68e643248fa8a51e2788692ad3a4b2f82;
UMDcc7485f12480773541eee6f83a21c0af0cacb5b7519d2f5c9b25fed7aade6638;
producerac0b236e9d8571fe0a836c76a696eed04b383403842f58ccc2721227d8929af5.
Final stdout SHA256c06ed83ad7574d421f9f01ffd7f4a1477975d4b79eee6ff58ba296d8440c2bf7.
Evidence: .local/experiments/EXP640-bounded-output.

## Constraints / final goal

Do not reopen retained-root, AGX/PBE/UAT/RTKit/completion/DCP/qualification
lifetime without contradictory evidence. Preserve unrelated dirty tree and
native-ANS. Event129 remains telemetry without causal proof. One causal
variable per experiment; source-first WDK translation. Final mission remains
normal Windows rendering/present, accelerated OpenGL and CS1.6 through real
AGX. Leave only the final accepted package installed; intermediate packages
are removed after evidence.
