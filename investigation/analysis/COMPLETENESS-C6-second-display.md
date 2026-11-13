C6 DESIGN NOTE: a second display in the KMD (external DP over USB-C on J313)
This was read-only work. No files were written and no hardware was touched. The scratch directory `/Users/pavel/public_windows/.local/night-20261110/wp-c6/` was created and is empty.

## 1. CURRENT STATE (arch/integrate-k2 b7d3df623)
Every display path in the driver assumes one panel. These places hard-code display 0 or a single display:

- **lifecycle.c**
  - :558-559 reports 1 source and 1 child.
  - :543-548 seeds the committed state with the J313 mode.
  - :1236-1243 SetPowerState ignores DeviceUid.
- **display.c**
  - :64 child-relations size check (2 descriptors means N+1 with N=1).
  - :73-88 one child: INTERNAL, interruptible HPD.
  - :107/:112 child 0 is always reported connected.
  - :129-132 no descriptor.
  - :27 UpdateMonitorLinkInfo, :42 SetPalette, :145/:158 pointer, :805 visibility, :851 commit, :1027-1030 active-path update, :1056 RecommendMonitorModes, :1133 GetScanLine, :1236 SetVidPnSourceAddress, :1314 and :1338 MPO3: each accepts id 0 only.
  - :415/:541/:923 accept only a 0->0 path.
  - :8-21 and :248-272 fixed panel signal.
- **ADMISSION_CONTEXT** (render_admission.h)
  - :926 one ScanoutRuntime.
  - :936-944 DisplayActive, SourceVisible, Committed*, DisplayModes.
  - :1269-1295 MPO hold, deadline timer and phase.
- **scanout_windows.c**
  - :14-70 the runtime embeds one APPLE_AGX_FIXED_PANEL.
  - :1080-1135 the mode list is the fixed-panel table.
  - :1601 source 0.
  - :1963 and :1970 vsync is notified with VidPnTargetId=0.
- **interrupt.c**
  - :14-34 one IRQ_STATUS at 0x414.
  - :193-198 one scan-out ISR.
  - :68-83 exactly one edge interrupt resource is required.
- **mpo_windows.c**
  - :599, :984 and :1035 source 0.
  - mpo_contract.c:446-449 enables two planes only for source 0.
- **allocation_windows.c:288** accepts a primary only for source 0.
- **Shared code**
  - apple_agx_fixed_panel.h:6-8 source and target ids are 0.
  - apple_agx_vsync.h:9-11 the vsync period is a compile-time constant.
  - apple_agx_scanout.h:70-73 J313 geometry constants.
- **UMD**
  - direct_flip_contract.c:21 gates on the panel mode table.
  - umd_win32_screen.c:1010 source 0.
- **m1n1 (s2v3)**
  - display.c:32 one `dcp`.
  - display.c:340 the IOMFB full owner is internal panel only.
  - hv_agx_power_mmio.c:35-36 one broker and one service; :1100-1121 one step and one injection on vINTID 889.

Correction to the TBX inventory: QueryChildRelations already zeroes two descriptors, but only because dxgkrnl passes N+1 entries.

## 2. WINDOWS CONTRACT
From WDK 26100 headers:
- **Child descriptor:** dispmprt.h:286 says a TypeVideoOutput child's ChildUid is its target id.
- **Child status:** DXGK_CHILD_STATUS (:328) carries HotPlug.Connected.
- **Hot-plug callback:** DxgkCbIndicateChildStatus is IRQL ≤ DISPATCH (:1717-1722), so it can be called from our DPC.
- **EDID:** DXGK_DEVICE_DESCRIPTOR carries DescriptorOffset and DescriptorLength (:296).
- **Not needed:** QueryConnectionChange and SetTimingsFromVidPn (:2922/:2930, WDDM 2.2). The USB4 info is not applicable either. Stay on the classic model (D5).

From Microsoft documentation (re-verify on the builder):
- dxgkrnl reads EDID in 128-byte blocks and expects STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA past the end.
- The source and child counts are fixed at StartDevice, so the external child always exists and starts disconnected.
- Windows freely maps source 0 to target 1 for "second screen only", so sources must be decoupled from targets.
- CRTC_VSYNC enable is adapter-wide.

DDI changes:
- QueryChildRelations returns 2 children. Child 1 is DISPLAYPORT_EXTERNAL with HpdAwarenessInterruptible.
- QueryChildStatus and QueryDeviceDescriptor are answered per child.
- IsSupported, EnumCofuncModality and Commit handle 0 to 2 paths, with any source on any target, no clone, and at most one source per target.
- CommitVidPn runs per affected source.
- SetVidPnSourceAddress, MPO3 and visibility route through the source-to-target binding.
- DxgkCbNotifyInterrupt carries the bound VidPnTargetId.
- The pointer stays a software cursor on both sources. Source 1 must return the same answers as source 0, not INVALID_PARAMETER.
- SetPowerState uses DeviceUid = ChildUid, which maps to blank or unblank for that display.
- MPO caps follow the bound target's layer count.

Unplug while applications present:
- Indicate Connected=FALSE from the DPC.
- Until dxgkrnl commits a VidPN without that path, flips to it complete without touching hardware. Their vsync comes from that display's software timeline at the last period.
- No present may wait on the missing monitor.
- Windows per-monitor DPI ("scale") needs nothing from the driver. The driver supplies only the mode list and the scaling caps.

## 3. AGX/ASAHI CONTRACT
- **Device tree:** t8103.dtsi:1434-1500 has dcpext, dcpext_mbox (AIC 466-469) and the dispext0/dcpext DARTs. They are disabled on every t8103 board, so Linux has no proven M1 external path.
- **Hot-plug from the coprocessor:**
  - Hot-plug arrives as the coprocessor's hotplug callback (iomfb_template.c:1025-1061).
  - After an unplug, a swap does not complete until the next set_digital_out_mode (:1050-1055).
- **Modes:** the TimingElements property (:574) gives active, total and sync timings with an id, plus colour modes; parse_mode (parser.c:444-510) drops virtual modes and modes without a colour mode. The mode is set with set_digital_out_mode(colour, timing).
- **EDID:** comes from the DPAVSERV copy-EDID call, which Asahi flags as unstable (dcp.c:57, epic/dpavservep.c:158).
- **m1n1 iBoot protocol:** getTimingModes returns width, height and fps only, with no totals (dcp_iboot.h:72-78).
- **Limit:** one external pipe (display-crossbar0, TBX DESIGN 2.1).

## 4. Review of the TBX external ABI proposal (BROKER_EXT_ABI.md, 201aed63)
What fits: CAP bit 16 gating; a present block with the panel's layout; connector STATE and EVENT_SEQ; IRQ_HOTPLUG; EXPECT_SEQ stale-list guard; disjoint command and result ranges; the lifecycle rules.

What I would change:
1. **Placement.** The window does not fit the 4 KiB broker page.
   - The page is in use at 0x100, 0x280, 0x400-0x550, 0x600, 0x700, 0x800-0xb60, 0xc00, 0xd00, 0xe00 and 0xf00, so there is no free 0x800 span. Offsets are from the shared ABI headers and m1n1 power_mmio.
   - Use a second 4 KiB broker page added as a QWordMemory resource in the Mu generator.
   - The current KMD ignores unknown memory resources (interrupt.c:61-77), so an old KMD with the new ACPI is safe.
   - Keep the capability bit. Replace REG_EXT_WINDOW with that resource (identified by a _DSD key).
2. **Interrupt.** Keep the shared vINTID 889 and per-instance IRQ_STATUS. The ISR reads and acknowledges both.
   - A second edge resource would make today's KMD fail resource discovery (interrupt.c:82).
   - dxgkrnl gives MessageNumber 0 for every line anyway.
   - Per-instance "edge taken" state already re-arms correctly (broker.c:622-629, :808-813).
3. **Latch and vsync.** Each present block raises LATCHED on its own coprocessor's swap-complete. The KMD keeps one timeline per display with its own period.
4. **Mode record.** Add HTotal|VTotal and ColorId|bpc to the 8 free bytes of the 32-byte record. Windows' D3DKMDT_VIDEO_SIGNAL_INFO needs TotalSize and an exact rational refresh. The DCP records are the source of truth for target modes; EDID is for identity and colorimetry only.
5. **Pool.** Allow the same pool IPA to be registered in both instances, with m1n1 mapping it into the dispext DARTs. Primaries live in one segment and move between targets.
6. **SET_MODE completion.** Raise an IRQ cause or bump EVENT_SEQ when ACTIVE_MODE settles, so CommitVidPn can wait, bounded, at PASSIVE.
7. **Unplug completion.**
   - A swap already pending at unplug, and every later present, completes with LATCHED|NO_SWAP. That needs a new flag bit, so the KMD does not use it as a vblank.
8. **BLANK.** Use the same command semantics as B1's DISPLAY_POWER in both instances.
9. **Panel connector block.** The panel instance should also expose a connector block: always CONNECTED, one mode record. The KMD then has one code path and no J313 constants.

KMD interface points against the proposal:
- capability probe
- EVENT_SEQ, STATE and LINK reads in the DPC
- EDID window, read at PASSIVE with an EVENT_SEQ check before and after
- MODE_SELECT and RECORD
- SET_MODE with EXPECT_SEQ
- per-instance present block through the existing shared client, given a base pointer
- ISR acknowledgement of both instances

## 5. TRANSLATION and ownership
| Function | Owner |
|---|---|
| PHY, crossbar, the external coprocessor, DPTX, EDID and modes, swaps, DART | m1n1 |
| Port events and DP entry (PHY_PLAN/RESULT mailbox) | m1n1 alt-mode policy with the USB session's UrsCx port driver |
| Second page resource | Mu |
| Per-display state, VidPN, child status, flips, vsync | KMD |
| Geometry-based direct-flip gate | UMD |

KMD data model:
- `ADMISSION_DISPLAY Display[MAX]`, each holding: broker base, runtime, committed mode, visible and active flags, connection state and EVENT_SEQ, EDID cache, mode list, timeline, MPO hold and deadline (target 0 only at first).
- `SourceToTarget[]`.

Connection state machine: Disconnected → LinkPending → Connected (indicate) → Disconnected or LinkFailed (indicate disconnected).

The deadline commit stays on target 0. The external display queues on its own free gate at latch, one outstanding swap per instance.

Offline tests (deterministic):
- Extend the fake VidPN manager (test_rc106_vidpn_scaled_modes.py) to two targets: extend, second-only (source 0 to target 1), clone refused.
- Simulated hot-plug sequence driven against TBX's axd_extconn model: plug, link fail, unplug mid-flip, stale SET_MODE, EDID changing mid-read.
- Vsync replay at a non-panel period.
- Panel-only equality with today's outputs.

## 6. WHAT IS STILL UNKNOWN (with checkpoint and recovery)
- **U7:** is the external coprocessor alive after iBoot, and which IOMFB ABI does it use? Settle with a read-only proxy check of DISPEXT_CPU0 power state and RTKit hello. Recovery: power cycle.
- **DPAVSERV EDID on M1.** Settle at the proxy: dump the EDID with a monitor attached. Recovery: blank, reboot.
- **Unplug swap behaviour on J313.** Settle at the proxy by unplugging during a running present loop.
- **Bandwidth with two pipes:** is there panel underflow? Settle with the panel plus 4K60 at the proxy, then GravityMark.

## 7. STEPS (one behavioural variable per boot)
1. **KMD: per-display struct, display 0 only. Small to medium.** Hardware: perf gate, panel unchanged.
2. **Mode provider interface (panel table or broker records) and runtime vsync period. Medium.** Hardware: perf gate; vsync must be identical.
3. **VidPN for N paths with source-to-target binding, N still 1 at runtime. Medium.** Host tests only.
4. **Shared external connector client against axd_extconn. Medium.** Host only.
5. **Hot-plug wiring and two children, gated on the capability. Medium.** Hardware:
   - W1: new KMD with old m1n1 behaves exactly as today.
   - W2: m1n1 with the capability and no monitor shows one display.
6. **Target-1 flips, per-target vsync, unplug completion, MPO caps by target, UMD geometry gate. Medium.** Hardware W3: cold plug, extend.
7. **Hot-plug and unplug under load.** Hardware W4: 20 cycles. W5: unplug during a full-screen game.
8. **Later, each its own step:** scaled sources on the external display, clone, two layers on the external display, external deadline commit, DP audio. Large.

Proxy-only hardware order before any Windows run: TBX-S0 (port-controller snapshot), U7, TBX-S1 (solid colour on the monitor), then the broker external instance driven by a Python stand-in for the KMD (register pool, EDID and mode dump, SET_MODE, pattern, unplug receipts).

## 8. RISKS
- **Hung swap after unplug, then DWM or TDR.** Avoided by broker-side completion plus the KMD's software timeline.
- **Bench input loss.** The bench's dock on port 1 carries input; DP pin modes keep USB2 for it.
- **Panel regression.** Avoided by the capability gate and Steps 1-3 being behaviour-neutral.
- **Monitor safety.** Timings come only from the external coprocessor's list.
- **KMD reach.** The KMD touches only broker MMIO; an m1n1 fault is a reset, not damage.

## PORTABILITY
Machine- and SoC-specific parts, and where they live:
- Coprocessor paths, DARTs, power domains, crossbar and PHY: m1n1 per-SoC display config (display.c:50-115 style) and the Apple device tree.
- Firmware layouts: m1n1's versioned IOMFB ABI module (v13_5).
- Broker page address and interrupt: generated ACPI.

KMD rules:
- No panel constants in new logic: panel and external geometry and refresh come from connector mode records.
- Display count = number of broker instances advertised.
- No J313 or T8103 in new names.

A later SoC port changes the m1n1 display table, the IOMFB ABI backend, the PHY backend and the ACPI generator. The KMD and UMD should not change.
---
Architect notes (2026-11-13T14:25:20Z):
- ACCEPTED, including the nine changes to the external-pipe ABI proposal (second broker page as its own ACPI memory
  resource instead of a window inside the panel page; shared interrupt with per-instance status; per-display latch and
  timeline; totals and colour id in the mode record; shared pool; SET_MODE completion event; NO_SWAP completion after
  unplug; BLANK = DISPLAY_POWER semantics; a connector block for the panel too, so the KMD has one code path).
- KMD steps 1-3 (per-display structure, mode provider, VidPN for N paths with N = 1) are behaviour-neutral for the
  panel and start after the C2 mode work returns, because both edit display.c and the scan-out code.
- Licence for the PHY import decided by the operator the same day (see the Thunderbolt session's ledger).
