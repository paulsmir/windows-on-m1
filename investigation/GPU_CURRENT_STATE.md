# GPU current state

Updated 2026-09-08T18:51Z. Main process only; no agents.

## Current machine / next boundary

EXP647 ran after a confirmed physical login in session1. Exact source0
D3DKMTSetVidPnSourceOwner(EXCLUSIVE) returned0xC0000022 STATUS_ACCESS_DENIED
before SetDisplayMode, Render or Present; standard trace has zero KMD events.
All producer-owned WDDM objects were destroyed with status0 and the device
remained healthy. Exact oem5 package was removed. Ordinary377/392 is restored
and verified: SSH, one inert APPL0002 Code28/null INF, packages/service/module/
SYS/UMD absent,8CPU,NVMe2,USB5,keyboard1 and no fresh41/1001/129.

Current first unknown remains standard Windows presentation. EXP641 proved
only that an SSH session0 producer cannot acquire exclusive VidPN source0.
EXP643 proved one interactive Windows render and physical AGX completion
fence269, but windowed BLT Present was denied before the KMD DDI. EXP644
advanced the denial to OCCLUDED while DWM reported
MILERR_DEVICE_CREATION_FAILURE. EXP646 proves D3D11CreateDevice(Apple) returns
DXGI_ERROR_UNSUPPORTED while Basic/WARP pass because the exact installed Apple
UMD has no truthful 3D pipeline/device contract. EXP647 now rejects the
independent fullscreen direct-KMT seam even from a real console token. Current
first unknown is therefore a coherent minimal truthful UMD device/pipeline and
runtime-managed presentation contract; no capability bit may be enabled alone.
Caps remain unchanged. EXP646's post-failure `GetModuleHandleW==NULL` is not
evidence that the UMD was never loaded; cold-path callback ordering remains
unobserved. Commitfac65c79f4b179219b6afaf3bad64d0064b0049e now fixes
format-query error semantics and exact allocation association/primary release/
deferred Flush lifetime with pipeline caps still zero. Mock-runtime tests and
ARM64 code-analysis build are GREEN. Next is one unchanged-cap loader/callback
trace to name the exact runtime rejection before larger Mesa frontend work.

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
