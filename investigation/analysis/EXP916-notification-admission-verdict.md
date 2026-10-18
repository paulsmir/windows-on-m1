# EXP916: notification admission survives the window; desktop remains black

The desktop acceptance criterion is **FAIL**. Package 30.0.916.0 contains the
notification admission/wakeup correction `1e8ac1c3f306427d366b595d57a5c7d0828c41ba`,
built from `9136ff1b29c997fb19950fd21e6bdcc29735dbac`. No package is accepted.

## What the hardware proves

Original boot receipt: `2026-10-01T17:56:44.3708060Z`. The preregistered monitor
reached 773.762302 seconds with Code 0 and eight CPUs. The later original
collector at 18:20:05 also reported Code 0 / StartStage 12 in the same boot.
No recurrence of EXP915's `0x119/2/80000011` was observed. This is one window of
nonrecurrence, not direct evidence that a new command arrived during
NotificationClaimed in this run. The deterministic production-code RED/GREEN
replay remains the direct verification of that interleaving.

The read-only SDK console probe ran in session 1 after the baseline. It proved
one active primary Apple path: LUID `00000000:00041b92`, source 0, target 0,
target available, 2560x1600, `\\.\DISPLAY2`, GDI attached/primary flags 5.
The task was removed. Its 873-byte output has SHA-256
`de52317978596b02a42173a4ccee873e1d63e638d1f73d80fddd45024d1828df`.
Missing active display topology is therefore excluded for this original boot.

Tracked DWM physical/virtual Present counters remain zero. Current matched DCP
surface IOVA `0x102a0000`, PA `0x8e0110000`, sequence 2, has 4096000 zero pixels
at +120/+300/+600 seconds. **Correction:** the actual late source records have
broker/latched swap ID **10**, not the preliminary progress summary's 9.
`cache_clean=0` remains a limitation of the CPU-view pixel measurement.

## ETW interpretation correction

The first frozen ETL contains 30646 Dwm-Core events. After its Apple HWDEVICE
creation event (feature level 10.0), 62 retained ENDFRAME_DRAWLIST_BATCH_STATS
events have zero HW and WARP draw calls. This is an observed early window,
not a claim about every composition cycle throughout the boot. ETL wall-clock
values precede the later CIM boot receipt because of guest clock adjustment;
use the same-boot snapshot receipt and event order, not raw absolute timestamps,
for attribution.

Exact guest metadata was obtained with `wevtutil gp Microsoft-Windows-Dwm-Core
/ge:true /gm:true /f:xml`; dwmcore version `10.0.26100.9278`, OS build 26200.
The metadata file is 85562 bytes, SHA-256
`c08ffb718485ae7c8a37afdca82adc2de1bcd5add274175c7c9f33f2debce325`.
It identifies event 468 as **Dx_Flip_Consumed**. Prior EXP915/916 wording that
called this a display binding event is superseded: absence of event 468 cannot
prove absence of output-target creation or binding. Event 201 is
SCHEDULE_DERIVEDISPLAYSET; its isolated fSucceeded/attempt fields still do not
supply the missing failure HRESULT or a proven causal branch.

## Source and specification findings

Native `Adapter.cpp` installs `CreateDevice` directly as `pfnCreateDevice`.
The adaptation in `build-native-asahi-state.py` retains the direct
`DXGI_STATUS_NO_REDIRECTION` return in native `Device.cpp`. The S_OK return in
the non-native `AdmissionUmdCreateDevice` is not on this package's path. The
hypothesis that a wrapper normalizes this positive status is rejected offline.
[Microsoft's DXGI DDI contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/supporting-the-dxgi-ddi)
requires this status when hardware is not shared with a D3D9 DDI implementation.
Do not replace it with S_OK without implementing the corresponding sharing contract.

A separate, confirmed implementation gap is the native UMD interface level:

- `build-native-asahi-state.py` restricts SupportedDDIInterfaceVersions to
  D3D10_0 and D3D10_0_x. Actual DWM device receipts show Interface `0x000a0006`,
  Version `0x177a`, status `0x087a0004`.
- Pinned Mesa `State.h` disables D3D10.1 and D3D11. Its `Device.cpp` initializes
  `D3D10DDI_DEVICEFUNCS`; the native adaptation does not implement a modern table.
- Pinned WDK26100 `d3d10umddi.h` defines the distinct `D3D11_1DDI_DEVICEFUNCS`
  and runtime `p11_1DeviceFuncs` union slot. Several existing-slot signatures
  change (constant-buffer range binding and update/copy operations), and new
  functions include Discard, ClearView, and CheckDirectFlipSupport.
- Pinned `dxgiddi.h` defines DXGI1_2 Blt1 and offer/reclaim slots. Later DXGI1_3
  Present1 is a separate versioned contract and must not be conflated with 1.2.

[Microsoft's software requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/software-requirements)
require D3D11.1 UMD DDI even for D3D10-class hardware under WDDM1.2; its table
also requires D3D9 DDI. [The feature matrix](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/wddm-v1-2-features)
distinguishes FULL GRAPHICS from DISPLAY_ONLY and RENDER_ONLY. This is a
confirmed completeness gap, **not yet proof that it is the immediate branch
causing this black screen**. Successful DWM device creation at FL10.0 does not
prove modern presentation support. Do not run a candidate which merely adds
an interface version or enables capabilities over unimplemented DDIs.

The next implementation work must derive a coherent native UMD ABI/resource/
presentation mapping offline. See `EXP916-native-umd-contract-gap.md`. No new
hardware experiment is justified by changing NO_REDIRECTION or guessing caps.

## Evidence and recovery

All paths below are relative to root `.local/experiments/EXP916-notification-admission/`.

- Manifest `cf918cd51d873e4bca014ecc0839b13a6ae2956a71ca572ddb741fb261c21e12`;
  payload `a7824e1c8331c9600dd0f56a155d4fdfc1f43524a9cbcc32996be2550540b426`.
- Ten complete snapshots / 162 frozen files: `original-frozen-host-gate.json`,
  SHA `bde47007434a2d6822493d2fed8bb6210d93639158c93a4d2da3546d905ab8bc`.
- Original collector: 16 files including manifest; `hardware-evidence-original/host-gate.json`,
  SHA `85decd1de4201d1a72d6141e60c8ab4c6d85f771159744bce0b4823614be9128`.
- Ordinary Code43 recovery: 15 files including manifest;
  `hardware-evidence-recovery/host-gate.json`, SHA
  `04916fd2daa7fd4cf01b62a31551a55b2b3f6c89b01bd20555f298a5c191ce75`.
- `console-topology-host-gate.json`, `dwm-early-boundary.json`,
  `dwm-provider-metadata-guest.xml`, and `full.log` preserve the new findings.

Original ordered restart was accepted at 18:21:43 and completed with delay.
The verified owner had already exited when an optional forced-recovery identity
check ran, so **no SIGTERM/SIGINT was sent**. Immutable EXP377/392 ordinary
recovery reached Code43, exact oem5 package916, arm null, CPU8/disks2/USB5,
SSH/RDP and autologon1. All original and recovery evidence passed independent
host size/SHA checks before diagnostics cleanup. Exactly two hash-verified
owned ETL duplicates were removed on the guest to restore 4750073856 free bytes;
host copies remain. Final exact package cleanup / durable Code28 receipts are
recorded below when complete.

Final recovery completed: ordinary EXP377/392 boot
`2026-10-01T18:30:53.4670870Z`, checked at 18:31:17 and 18:34:50.
One APPL0002 Code28, no package/SYS/UMD/service/signer/arm/diagnostics,
CPU8/disks2/USB5, SSH, RDP service running, autologon1. Free C: 4739559424
bytes, ShadowCount0. Three cleanup receipts independently size/SHA verified:
`cleanup-host-receipts/host-gate.json`, SHA
`326b284d73c404086b3580150a2393ff5cf1cc2a385aa8b8c7eeed977cd719f5`.
No emergency hidden boot or forced signal was used. Report/ledger schema tests
passed (2 tests); driver source was unchanged during this evidence closure.
