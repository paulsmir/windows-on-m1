C1 DESIGN NOTE: internal panel brightness (J313). This was a read-only pass: no hardware was touched and nothing was written to the repository or the scratch directory.

**Key finding first:** today every DCP swap that m1n1 builds hard-codes the backlight to about 99 nits. The fields are set to on, value 0x58f058d0, power 0x40 at m1n1 src/dcp_iomfb_present.c:153-155 (one-slot builder) and :281-283 (two-slot builder). A test pins this value (tests/dcp_iomfb_present_test.c:61), and proxyclient/experiments/dcp.py:213 labels it "~99 nits". The panel's maximum is 420 nits. So Windows currently shows a fixed level of roughly 24% of the panel's maximum, whatever macOS had set.

**1. CURRENT STATE**
- **KMD:** `AdmissionDdiQueryInterface` is FAIL2 and returns `STATUS_NOT_SUPPORTED` for every GUID (render-admission/src/callbacks.c:28-29; wired in driver.c:76 and in the status wrapper render_ddi_status_wrappers.h:156). The audit is correct here, but it missed the hard-coded swap value above.
- **Child:** one `TypeVideoOutput` child with `D3DKMDT_VOT_INTERNAL`, ChildUid 0 and AcpiUid 0 (display.c:73-88).
- **Power:** `AdmissionDdiSetPowerState` is a no-op (lifecycle.c:1236-1244). `NotifyAcpiEvent` returns not supported (callbacks.c:15-26).
- **ACPI:** the J313 Mu tables (mu/Platform/MacBookAirMid2020Pkg/AcpiTables) have no `_BCL`, `_BCM`, `_BQC` or `_DOD`. AGX0 has no display-output child device.
- **m1n1 bootstrap** (dcp_iomfb_bootstrap.c):
  - D112 `create_backlight_service` returns 0 (:308-311). This was hardware-proven in EXP-248 and differs from Asahi, which returns "has panel".
  - D207 still calls A132 `backlight_service_matched` (:429-432).
  - D300 `pr_publish` (where the firmware publishes the current nits), D414 `sr_set_property_int` (`Brightness_Scale`) and D593 `enable_backlight_message_ap_gated` are acknowledged and their values discarded (:320-333, :391-395). So m1n1 currently cannot read the boot brightness.
- **Broker:** the scan-out broker ABI (hv_agx_scanout_broker.h:75-200) has no brightness register or command. An unknown command gets `INVALID_COMMAND` (broker.c:281-282). Offsets beyond the V3 window are refused (power_mmio.c:960-968). The KMD checks capabilities as a subset (scanout_windows.c:664-666), so a new capability bit is forward-compatible.

**2. WINDOWS CONTRACT** (headers read; behaviour marked "doc" is from Microsoft documentation as I recall it, not verified)
- **Interfaces** (dispmprt.h):
  - `GUID_DEVINTERFACE_BRIGHTNESS` / `_2` / `_3` (:402-427).
  - v1 has GetPossible, Set and Get with UCHAR percent levels (:806-847).
  - v2 adds GetCaps, SetState, SetBacklightOptimization and GetBacklightReduction (:850-902).
  - v3 is millinit-based and takes a ChildUid: Set (`BrightnessMillinits`, `TransitionTimeMs`), Get (Current and Target), GetCaps, GetNitRanges (up to 16 ranges) and SetBacklightOptimization (:904-968). The structures are in winsdk shared/d3dkmdt.h:1827-1932.
- **IRQL:** every callback and `DxgkDdiQueryInterface` itself is PASSIVE_LEVEL (dispmprt.h:2358-2364).
- **Doc:** dxgkrnl queries these GUIDs for an adapter with an integrated panel. If the miniport refuses, it falls back to ACPI `_BCL`/`_BCM`/`_BQC` on the output device whose `_ADR` matches the child's AcpiUid. The OS (power manager) owns persistence: it keeps the level per power scheme (AC/DC) and calls SetBrightness after boot and resume.
- **Keys:** the keyboard session must report HID Consumer usages 0x6F and 0x70 (winsdk hidusage.h:582-583). Windows routes these to the active brightness interface (doc). ACPI notifies 0x86/0x87 (dispmprt.h:111-114) only apply to ACPI hotkeys and are not used here. For the keys to work later, this package only has to expose a working interface.

**3. AGX/ASAHI CONTRACT**
- **Backlight path:** brightness is a DCP swap field: `bl_unk`/`bl_update` = 1, `bl_value` (a DAC value), `bl_power` 0x40.
  - Asahi applies it only with a swap (iomfb_template.c:1413-1422). A brightness change forces a CRTC commit after waiting about one frame (dcp_backlight.c:139-201).
  - Nits are converted to DAC by interpolating two tables, 2-99 nits and 103-510 nits (dcp_backlight.c:17-137). The firmware minimum is 0x10000000, about 4 nits (iomfb_template.h:72; m1n1 ipc.py:468).
- **No PWM:** J313 has no backlight PWM. fpwm1 drives the keyboard LED only. The panel node gives `apple,max-brightness = 420` (asahi t8103-j313.dts:24-44).
- **Reading the current level:** the firmware publishes current nits as property 15 (scaled by `Brightness_Scale`) for firmware below 14.7 (iomfb.h:80-82; iomfb_template.c:200-252). So the boot level is readable only if m1n1 captures D300 and D414.
- **Power cycles:**
  - On power-off Asahi sends a swap with backlight value 0 and power 0; after power-on it forces a resend (iomfb_template.c:852-853, 915-931).
  - D593 also forces an update, because the firmware loses its brightness state after DPMS (:505-514).
- **macOS:** a static read of the kernelcache shows the strings `backlight-level`, `backlight-nits`, `IOMFBBrightnessLevel` and `backlight-marketing-table`. HYPOTHESIS: iBoot restores the macOS level from NVRAM.

**4. TRANSLATION** (my pick: the KMD brightness interface plus a new broker capability; ACPI `_BCM` is rejected)
- **Why not ACPI:** `_BCM` would mean AML writing the broker page, which splits broker ownership between ACPI.sys and the KMD, gives no nits, and needs Mu changes.
- **m1n1 owns the hardware contract:**
  - Advertise CAP_BRIGHTNESS (bit 10) only for the 13.5 internal-panel owner.
  - New registers in an extended V3 window (0x140-0x14f), decoded only when the capability is advertised:
    - TARGET_MNITS (read/write);
    - APPLIED_MNITS (read-only; the value carried by the last latched swap);
    - RANGE (read-only; 4 and 420 nits);
    - BOOT_MNITS (read-only; from D300 and D414, 0 if never seen).
  - New command 5, SET_BRIGHTNESS:
    - It goes through the existing single pending slot and sequence/receipt machinery.
    - It is BUSY while a request is pending.
    - In ACTIVE state it re-swaps the last applied layer request with only the backlight fields changed.
    - In READY state it only stores the value.
  - Every PRESENT/COMMIT swap carries the current target. The default target stays 0x58f058d0, so a new m1n1 with an old KMD behaves bit-identically to today.
  - The broker always clamps to [4, 420] nits and converts nits to DAC itself. The Asahi table is dual GPL-2.0/MIT and needs the AGENTS licence review before use.
  - Rate limit: one swap in flight, plus at least 16 ms between brightness-only swaps.
  - The brightness path can never set power 0; only the B1 display-off command may. After power-on the first swap resends the target.
- **KMD:**
  - QueryInterface returns v2 (and v1) only when the broker advertises the capability and the device is started. It checks Size and Version exactly, and v3 stays not-supported in the first step.
  - 101 levels (0-100). Percent maps to millinits through a fixed perceptual curve, with 0% equal to the 4-nit floor.
  - Caps are all 0 (no smooth, adaptive or nits).
  - GetBrightness returns the cached level, seeded from APPLIED/BOOT.
  - Set coalesces to the newest target. On BUSY it re-arms and submits on the next latch, at DISPATCH (it is only an MMIO write).
  - Interface reference/dereference counters; calls after StopDevice fail closed.
  - New files: shared/src/apple_agx_brightness.c (pure: mapping, clamp, coalescing state) and render-admission/src/brightness_windows.c. callbacks.c gets a GUID switch. apple_agx_scanout.h gets the capability, register and command values.
- **Offline tests:**
  - percent-to-millinit monotonicity and floor/ceiling;
  - QueryInterface size/version/GUID refusal and capability gating;
  - coalescing under BUSY;
  - broker: capability-off offsets refused; clamp; BUSY while pending; default swap bytes unchanged (keep dcp_iomfb_present_test.c:61 as the regression); brightness swap changes only the backlight bytes.
- **Persistence:** the Windows power policy re-applies its stored level at boot. We do not write NVRAM.

**5. WHAT IS STILL UNKNOWN**
- **U1: does firmware 13.5 apply `bl_value` when D112 is false?**
  - Zero-change check: with macOS at maximum brightness, boot into Windows and see whether the panel drops to a fixed level at the first owner swap.
  - Alternative: proxyclient experiments/dcp.py with two `bl_val` values. That Python manager also returns D112 false (manager.py:98), so it is the same condition.
  - Recovery: normal reboot.
- **U2: dxgkrnl's QueryInterface order** (v3 before v2? queried only for an internal child? when?).
  - Check: a package whose QueryInterface records each requested GUID in the DDI receipt and still refuses. No m1n1 change.
- **U3: is D300 property 15 published on J313 13.5?** Check with an m1n1 log line.
- **U4: does the Asahi DAC table match this panel?** Operator judges by eye at 4, 99 and 420 nits; the clamp makes this safe.
- **U5: is an identical value re-applied after a DCP power cycle?** This belongs to the B1 boot.

**6. STEPS**
1. KMD QueryInterface receipt instrumentation (still refusing). One boot. Small.
2. m1n1 capability, registers, command and per-swap target, with the default unchanged. Hardware check: no visible change and the receipt shows the capability. Medium.
3. KMD v2 interface plus the shared mapping. Operator checks the slider gives at least 10 visible steps and that 0% stays lit. Medium.
4. m1n1 captures BOOT_MNITS and the KMD seeds GetBrightness from it. Small.
5. Survive display off/on, together with B1. Small.
6. v3 nits interface with GetNitRanges 4-420 (smooth stepping later). Medium.
7. Keys: verify once the keyboard session emits 0x6F/0x70. No code expected here.

**Is there a first step with no m1n1 change?** Only observation: U1, the U2 instrumentation package, and KMD groundwork that stays capability-gated off. Brightness cannot change without m1n1, because DCP is owned solely by the broker and the value is compiled into the swap builder.

**7. RISKS**
- **Black panel:** the broker floor (0x10000000, about 4 nits) blocks it, and the brightness path never sets power 0. A broker clamp at 420 prevents overdrive.
- **Malformed swap crashing DCP:** only the backlight bytes vary on the proven builder. Recovery is SSH / recover.sh.
- **Version mismatch:**
  - Old m1n1 with a new KMD: the capability is clear, so there is no interface, as today.
  - New m1n1 with an old KMD: the default value is unchanged.
  - The KMD must never touch 0x140 and above without the capability, because the window refuses those accesses.
- **NVRAM and data:** untouched, so macOS and iBoot keep their own level.
---
Architect notes (2026-11-13T14:01:44Z):
- Design ACCEPTED: KMD brightness interface (v1/v2 first) plus a scan-out broker capability; ACPI _BCM rejected.
- LICENCE DECISION (operator, 2026-11-13, in chat: "да, бери таблицу с указанием авторства"): the nits-to-DAC
  table of the Asahi Linux DCP backlight source (dual GPL-2.0/MIT) is taken under its MIT option with attribution,
  in its own file in the m1n1 tree. Compatibility: this repository and m1n1 are MIT. Scope of the decision: this
  table only.
- Implementation started offline: m1n1 branch gpu/c1-brightness, driver branch arch/c1-brightness.
