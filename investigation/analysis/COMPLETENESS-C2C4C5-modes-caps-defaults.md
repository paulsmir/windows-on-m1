## WP C2 / C4 / C5 / F2-F3: verification against the arch/integrate-k2 tree, which contains 1cadce2b3 and bf56f6728

**Short answer:** the bench m1n1 line (s2v3) does report the scaled-source capability, and the 11-mode table is offered today. But no mode change has been run on the current bench line. Reading the code turns up one real defect at scaled modes (C2 below). Of the requested lists, the KMD switches are fully classified (73 registry switches, every member named); the UMD environment variables are counted per group but only the drill/diagnostic group is named member by member. Nothing in the repository was modified. My scratch files are in `$HOME/public_windows/.local/night-20261110/wp-c2c4c5/`: `env-names.txt`, `env-context.txt` (the file:line and context for each environment variable) and `classify.py`.

### 1. Current state

**C2: does s2v3 report the capability?**
- **Yes.** Any broker started at ABI V2 gets CAP_SCALED_SOURCE (bit 8): m1n1 `hv_agx_scanout_broker.c:366-376`.
- V2 is selected only when DCP starts and the latch source is proven (`hv_agx_power_mmio.c:1041-1053`). Two layers (bit 9) come only from the `AGX_SCANOUT_TWO_LAYERS` build flag (`hv_agx_power_mmio.c:1047`).
- Hardware receipt from EXP2380, decoded from `evidence/EXP2380/kmd-receipts-final.txt` (`Wom1DisplayModesReceipt`):
  - BrokerAbi 2, BrokerCaps 0x3ff, ScaledModes 1.
  - CommitCalls 1 (2560x1600), MonitorModeCalls 1, **LastMonitorModesAdded 1**.
- The KMD gate is `scanout_windows.c:1080-1101`; the mode count is `apple_agx_fixed_panel.c:103-106`.

**C2: what happens today when 1920x1200 is chosen (Settings or an application calling ChangeDisplaySettingsEx)**
1. **Monitor modes:** `RecommendMonitorModes` offers every table size as a monitor mode of its own (`display.c:1066-1106`).
2. **Cofunctional enumeration:** a pinned scaled target restricts the source modes to that one size with identity scaling (`display.c:559-566`, `601-605`). The target set is all 11 sizes (`652-661`).
3. **IsSupportedVidPn:** `display.c:300-319`.
4. **CommitVidPn:** for a scaled target the KMD reports IDENTITY to Windows but the panel gets aspect-ratio-centred-max (`display.c:950-962`). `AdmissionScanoutCommit` retires the MPO slot 1 (`scanout_windows.c:1159-1180`). The destination rectangle is computed in `apple_agx_fixed_panel.c:191-281`.
5. **SetVidPnSourceAddress with the smaller primary:** `display.c:1234-1244` → `AdmissionScanoutQueuePresent` checks that the primary has exactly the committed geometry and passes the 64 KiB-aligned span (the EXP2055 fix), at `scanout_windows.c:1612-1660`.
6. **To the panel:** the KMD writes the destination origin and size registers (`apple_agx_scanout.c:162-167`) → the broker validates them (`hv_agx_scanout_broker.c:57-77`, `297-301`) → DCP swap `destination[0]` with a black background (`dcp_iomfb_present.c:119-150`) → the vUART line at `display.c:863`.
7. **With MPO on:** a one-plane Set3 of the full-screen primary goes the same V2 route (`mpo_windows.c:980-1008`), so the picture is correct.

**C2: where it fails (found by reading source, not seen on hardware)**
- With MPO effective, the plane rectangles are in mode coordinates, because `PanelWidth/Height` is the committed mode (`mpo_windows.c:148-149`).
- The layer-record builder copies those rectangles straight into panel coordinates (`mpo_layers.c:25-48`).
- So the FS-1 path (application plane alone, `mpo_contract.c:371-411`) and the STRETCH-1 one-plane records would show at 1:1 in the top-left corner of the panel, not aspect-fitted. The broker accepts this because the rectangle is inside the panel.
- Two-plane configurations are refused at non-native modes (`mpo_contract.c:285-288`), which is correct.
- Other gaps:
  - The user cannot choose STRETCHED or CENTERED for a live path (`display.c:1036-1039`), and a scaled target always gets aspect-fit.
  - Hiding the source only sets a flag; it does not blank the panel (`apple_agx_fixed_panel.c:306-316`).
  - `GetScanLine` returns NOT_SUPPORTED (`display.c:1127-1139`).
  - There is one refresh rate: 266630000/(2642×1682) ≈ 59.9999 Hz for every size (`display.c:8-21`, `259`).

**C2: what experiments already showed**
- **Proven on hardware (EXP2056, old m1n1 74a4d8f4-scaled, before the ANS line):** GravityMark switched to 1920x1080 through ChangeDisplaySettingsEx, m1n1 printed "panel 2560x1440 at 0,80", 13890 flips with status 0, native mode restored at exit. A SystemSettings.exe WER (Windows.UI.Xaml 0xc000027b) appeared during the switch.
- EXP2301 failed only because that bench m1n1 lacked bit 8 (BrokerCaps 0xff).
- STRETCHFS (EXP2375–2377) is a 2x popup at the native mode, not a mode change.
- **Implemented but unproven on the current line:** all 10 non-native modes; the Settings path; mode change with MPO effective; 1024x768 (2.08x upscale) and 800x600 (2.67x upscale).
- **Missing:**
  - Translation of layer records from mode to panel coordinates.
  - Live scaling choice.
  - Blanking on hide.
  - Scan-line reporting.
  - The post-display hand-back for a scaled mode.
  - A probe that enumerates the mode list.
- **What an application's exclusive full-screen switch needs:**
  - DXGI SetFullscreenState → SetDisplayMode with a primary created through `pPrimaryDesc`.
  - CommitVidPn with `hPrimaryAllocation`.
  - Flips of the mode-size primary.
  - The UMD primary gates use the shared table (`direct_flip_contract.c:21`) and never write `DriverFlags` (`umd.c:1005-1021`). So a blt-model or non-table primary has no fallback, as in the earlier fs-windows-audit F1/F3.

**C4: capability truth table**

| Capability | Where | Verdict |
|---|---|---|
| FlipOnVSyncMmIo, FlipIndependent, MaxQueuedFlipOnVSync 1 | `lifecycle.c:815-832` | truthful |
| FlipImmediateMmIo not set | — | truthful; tearing presents are missing (under-claims) |
| SupportSmoothRotation | `lifecycle.c:834-836` | truthful flag. **Correction to the audit:** the only rotation offered is Identity/Offset0 (`display.c:700-705`, `1040-1043`), so nothing extra is advertised. Portrait rotation is simply absent. |
| SupportDirectFlip | `lifecycle.c:838` | truthful (required for MPO) |
| SupportMultiPlaneOverlay / MaxOverlayPlanes 2 / ImmediateFlip off / CursorScaledWithMultiPlaneOverlayPlane0 off | `lifecycle.c:848-852`, only when MPO is effective | truthful at the native mode |
| MPO caps: StretchRGB 2.0/0.5, no filter bits | `mpo_windows.c:447-477`, `mpo_contract.c:446-466` | truthful at native; **over-claims at scaled modes** (the C2 defect). Under-claims upscaling: Asahi `plane.c:80-95` says DCP downscales up to 2x and upscales up to 4x. |
| Pointer caps 0, SetPointerShape/Position NOT_SUPPORTED | `display.c:139-161` | truthful |
| Display caps extension (HDR / FP16 / ARGB10 / HDR10 metadata) all zero | `lifecycle.c:1165-1177` | truthful: the surface is SDR sRGB (`dcp_iomfb_present.c` transfer function and colour space) |
| QueryVidPnHWCapability all zero | `display.c:1115-1125` | truthful |
| Gamma: default ramp only, SetPalette NOT_SUPPORTED | `display.c:1031-1032`, `36-49` | truthful; C3 is missing |
| Child: VOT_INTERNAL, HpdAwarenessInterruptible | `display.c:74-88` | **probably over-claims:** Interruptible implies change notifications, but no `DxgkCbIndicateChildStatus` call exists and `NotifyAcpiEvent` returns NOT_SUPPORTED (`callbacks.c:16-26`). The HpdAwareness enum is not in the pinned header subset; check against the documentation. |
| Device descriptor (EDID) | `display.c:129-132` returns CHILD_DESCRIPTOR_NOT_SUPPORTED | **under-claims:** Windows sees a "Generic Non-PnP Monitor" with no size, and 1009 DEFAULT_MONITOR devnodes accumulated (EXP2055). Asahi gets the panel size from DCP display attributes (`iomfb_template.c:588`, `parser.c:621`) or from the device tree (`dcp.c:1040`). |
| UpdateMonitorLinkInfo: 0 | `display.c:23-34` | truthful |

**C5: registry switches on the display path**
- **`Wom1MpoTwoPlanes`:** opt-in, default off (`scanout_windows.c:672-675`; the reader requires exactly 1). It is effective only with broker bit 9 and the negotiation read-back. The bench sets it by hand with `AGX_DEVICE_SWITCHES` in `.local/scratchpad-tools-backup/launch-exp.sh:63-79`; the INF sets no Wom1 values.
- **`Wom1MpoClipRead`:** opt-in, receipt only, but it also runs inside the paging page-table update (`gpuva_g3_paging_windows.c:541-543`). It is in the bench normal set, so it must leave that set.
- **`Wom1MpoLeadUs`:** a value, default 5000, clamped 1000..12000 (`scanout_windows.c:715-717`).
- **Turning two-plane MPO on by default needs:**
  1. A KMD opt-out reader (absent means on).
  2. A default m1n1 built with `AGX_SCANOUT_TWO_LAYERS`, and the `m1n1_windows` gitlink updated to s2v3 (the merge message says the gitlink lacks V3).
  3. Test updates: `tests/test_render_mpo_wiring.py:9-11` and `:198-206` (it asserts "defaults off" and the reader's `== 1u`). The effective-gate tests stay (`test_apple_agx_render_admission.py:120`, `test_apple_agx_render_type1_ready.py:52`).

**F2/F3 inventory**

KMD: 73 registry switches are read; about 360 other Wom1 names are receipt values that are written, not read.
- **Production default candidates (3):** MpoTwoPlanes, CrossProcessPairs, SubmitRequeue. All three are in the accepted bench set.
- **Opt-out safety switches (19):** JobThreadAnyCore, ComputeOwnQueue, NotifierFlight, ManagerSlots, IrqCompletion, FwRxDrain, G3RetainedSlots, JobBuildAhead, JobDepth2, JobDepth2Caps, RcDepth2, Depth2General, Depth2Prelude, Depth2TaOnCompute, Depth2TaOn3d, Depth2ComputeOnCompute, SoftFaults, PoolPercent, PoolPressureWaitMs.
- **Measurement or experiment levers that must not ship (32):** JobThreadOff, PreludeNotifyGate, Ov2NoHazardLead, Ov2CrossLead, Ov5TaWaiver, Ov5TaWaiverNoFeature, PreludeTimestampsOff, BuildDirectReads, JobDepth2Kick, JobDepth2KickOn3d, Depth2Present, CrossProcessPrivateLead, CrossProcessRenderPairs, ManagerSaveOnEvict, Depth2StampComplete, Depth2OwnNotify, Depth2Notifiers, GpuFaultRecoverChannel, PerfTgtUtilization, JobPollSpinUs, MpoLeadUs, GpuPrivateCached, SysmemGpuMap, LiveAdditivePaging, SystemSegment, PoolHalfSize, PoolFraction, PoolApertureReserveHalf, PoolPrefill, PoolConstructLargePages, SysmemSpill, SysmemShadow.
- **Drills (8):** TdrDrillAllowed, FwRestartDrillAllowed, GpuFaultDrillAllowed, Depth2Drill, Depth2FallbackDrill, Depth2FallbackDrillBuilt, Ov5FragWaitDrill, PoolAllocProbe.
- **Receipt / diagnostic (11):** GpuFaultInfoRead, GpuFaultControlRead, TaStartTimestamps, ManagerReceipt, JobDiagnostics (in the bench set), SubmitBracket, PresentTimestamps, RcReceipt, GpuPerfReceipt, MpoClipRead, TdrHaltObserve.

UMD: 106 environment-variable names in the UMD/ICD sources, classified in `classify.py`.
- **Downgrade opt-outs (40):** F3 says these must be gated out of production.
- **Opt-in levers (40):** must not ship.
- **Drills / diagnostics (9):** D3D12_NULL_PS, DIAG_CUT, D3D12_DIAG_INDIRECT_ARGS, D3D12_RESERVED_SELFTEST, UMD_VALIDATE_REFS, UMD_VALIDATE_REFS_REFUSE, UMD_WATCH_VA, D3D_ROBUST_DIAG (non-conformant), D3D12_DDI.
- **Receipts / traces that write files (17):** UMD_TRACE_FILE is the file writer.

**How the build could separate production from qualification**
- Today there is no production configuration. The accepted package is built with `-GpuvaG3Qualification -UmdAdmissionTrace -NativeFrontend` (EXP2387 `build-kmd.ps1:22`).
- `APPLE_AGX_GPUVA_G3_QUALIFICATION` (186 uses) is the real GPU-MMU path, so it cannot serve as the separator.
- `ADMISSION_OV2_ON`, `ADMISSION_OV5_ON` and `ADMISSION_PREEMPT_TRACE_ON` are hard-coded to 1 (`render_ov2.h:8`, `render_ov5.h:9`, `render_preempt_trace.h:18`).
- `build-driver.ps1:259-275` passes only the per-feature qualification properties.
- Proposal: add one property, `AppleAgxProfile=Production|Qualification`, plus one switch table header (name, class, default). In production:
  - Drill, measurement and receipt classes compile to their default, and their name strings disappear.
  - Only the opt-out safety switches still read the registry.
  - `AdmissionUmdTraceConfig` returns NULL, and the UMD environment getters go through one macro.
  - A test greps the production `.sys`/`.dll` files for the forbidden names.

### 2. Windows contract
Mode lists come from the monitor source mode set and the cofunctional source/target sets. ChangeDisplaySettingsEx matches exact sizes. Plane rectangles in MPO calls are in source-mode pixels. Set3 must latch exactly what Check3 accepted. All of this is from Microsoft documentation and the existing notes, not re-read here.

### 3. AGX/Asahi contract
DCP scales one surface into a destination rectangle (`destination[0]`), limited to 2x down and 4x up (Asahi `plane.c:80-95`). m1n1 limits layer records to 2x in both directions (`dcp_iomfb_present.c:176-179`). The V2 single-surface path has no scale check in m1n1.

### 4. Translation
- **m1n1:** owns the scaler. It must ship bit 9 by default.
- **KMD:** owns translating mode coordinates to panel coordinates: `panel = committed destination rectangle ∘ mode rectangle`, applied in `AdmissionMpoInput` and the record build. This can be tested offline with PanelWidth 1920 and a destination of 0,80,2560,1440.

### 5. Still unknown (each with its checkpoint)
1. **LastMonitorModesAdded 1 with ScaledModes 1:** were 10 adds rejected as duplicates, or does the Settings list come from cofunctional enumeration? Checkpoint: probe that enumerates the GDI and DXGI mode lists.
2. **DCP above 2x on the V2 path** (800x600, 1024x768). Checkpoint: set each mode and read the vUART geometry line.
3. **Mode change with MPO default on.** Checkpoint: the mode-set probe step with planes on.
4. **The SystemSettings WER** from EXP2056.

Recovery for all four: the probe restores with `ChangeDisplaySettingsEx(NULL)`; if the panel is black, run the visible recovery `rec<N>h/recover.sh` (the devnode is removed and Windows falls back to the native mode).

### 6. Steps
1. **(small)** Check3 refuses layer records at non-native modes (truthful, so DWM composites instead); add a unit test.
2. **(small)** Make MPO default on via the opt-out reader, update the tests, drop ClipRead and JobDiagnostics from the normal set.
3. **(small, m1n1)** Default image with two layers and update the gitlink.
4. **(hardware, one boot, separate arms)** Verify all 11 modes through ChangeDisplaySettingsEx test-and-set, the Settings-path SetDisplayConfig, GravityMark full-screen at 1920x1080 and 1280x720, then restore; plus a fresh install with no registry value.
5. **(medium)** Map records from mode to panel coordinates, then re-admit layer records at scaled modes.
6. **(C4, small each)** HpdAwareness decision; GetScanLine from the vsync time; post-display hand-back of the scaled mode.
7. **(medium)** Synthesised descriptor from DCP display attributes (m1n1 publishes them).
8. **(medium, m1n1+KMD)** Blank the panel on hide.
9. **(F2/F3, medium-large)** The production/qualification profile.

### 7. Risks
- A bad mode persisted in Windows' display configuration could come back at the next boot. The recovery profile removes the devnode, which avoids this.
- A DCP swap refused because of the scale factor is handled by m1n1 (refusal or timeout); nothing in the scan-out path writes to storage.
- MPO default on an m1n1 without bit 9 does nothing (safe).
- A production build must keep the opt-out safety group as the rollback.
---
Architect notes (2026-11-13T14:05:57Z):
- ACCEPTED. Order: (1) Check3 refuses layer records at non-native modes [truthful, small]; (2) two-plane MPO default on
  through an opt-out reader, measurement values out of the bench normal set; (3) hardware: all 11 modes + fresh install;
  (4) mode-to-panel coordinate mapping, then layer records re-admitted at scaled modes; (5) descriptor (EDID) synthesised
  from DCP attributes, GetScanLine, HpdAwareness truth; (6) production/qualification build profile with a switch table.
- Scaling limits: DCP's own limits per Asahi are 2x down / 4x up; ours stay at the measured 2.0/0.5 until a mode above
  2x (1024x768, 800x600) has been set on hardware.
