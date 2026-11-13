## B1/B3/B4 design note: display off/on, sleep and wake, lid (relation to B2 and A3)

**Verdict on sleep:** this stack cannot do real S3-style sleep. It never offered S3, hibernation is ruled out, and no open implementation (Asahi included) does it on the M1. The only reachable sleep is **modern standby (S0 low-power idle)**. So "sleep" means: display off with the backlight off, GPU idle (the firmware already powers the shader cores down), CPUs in WFI, and instant wake. Real adapter D3 is a later phase that needs B2 and the A3 cold-path broker operation.

### 1. CURRENT STATE
- **SetPowerState** is a no-op that returns success (`render-admission/src/lifecycle.c:1236-1244`).
- **NotifyAcpiEvent, SetPowerComponentFState, PowerRuntimeControlRequest** all return NOT_SUPPORTED (`callbacks.c:16-25`, `:624-630`, `:632-640`).
- `SupportRuntimePowerManagement` is set nowhere, and the power-component queries fall to the default refusal (`lifecycle.c:~1180`).
- **Correction to the audit:** SetVidPnSourceVisibility (`display.c:798-820`) does not blank the panel. It retires MPO slot 1, then `AppleAgxFixedPanelSetVisible` only sets a flag (`shared/src/apple_agx_fixed_panel.c:306-316`). The panel keeps showing the last frame with the backlight on.
- **Broker commands:** the scan-out broker has QUERY, REGISTER_POOL, PRESENT, RELEASE and COMMIT_LAYERS only (`shared/include/apple_agx_scanout.h:147-155`).
  - RELEASE swaps back to the m1n1 boot framebuffer; it does not blank (m1n1 `src/display.c:950-980`).
  - Every m1n1 swap writes a fixed backlight value 0x58f058d0 with power 0x40 (`dcp_iomfb_present.c:153-155`).
- **Platform (FADT, J313 = MacBookAirFamilyPkg `FADT.aslc:72,75`):** HW_REDUCED, LOW_POWER_S0_IDLE_CAPABLE and PWR_BUTTON flags, PSCI over SMC.
  - The DSDT has no `_Sx`, no PEP, no lid (PNP0C0D) and no power button (PNP0C0C).
  - `_LPI` is commented out (`DSDT.asl:265-267`).
  - Neither GPU SSDT (`J313AppleAgx.asl.inc`, `J313AppleAgxAbiAdmission.asl.inc`) has `_PSx`, `_S0W` or `_PRW`.
  - The keyboard interrupt is declared `Shared`, not `SharedAndWake` (`J313AppleInput.asl.inc`).
- **m1n1 PSCI (s2v3):**
  - SYSTEM_SUSPEND (0xC400000E) is not defined or advertised (`hv_psci.h:79-98`, `hv_psci.c:485-499`).
  - CPU_SUSPEND is shallow WFI only (`hv_psci.c:1567-1571`).
  - SYSTEM_OFF reboots (`:1619-1621`).
  - **Defect:** the SMC64 switch has no `default` case (`:1751-1763`). An unsupported 64-bit call returns its own function ID instead of NOT_SUPPORTED. Regression test needed.
- **Hibernation:** impossible, because AppleAns refuses the dump and hibernation path by design (`GPU_CURRENT_STATE.md:2465`, commit 1e7a690c4).
- **SMC:** no component in the Windows stack talks to the SMC after guest entry. m1n1 only uses it for HDMI GPIOs before entry (`src/dcp.c:844-848`).

### 2. WINDOWS CONTRACT
- **From the headers:**
  - `DXGKDDI_SET_POWER_STATE(DeviceUid, D-state, POWER_ACTION)`; `DISPLAY_ADAPTER_HW_ID` = 0xFFFFFFFF (`winsdk km/video.h:950`). Our only child is ChildUid 0, TypeVideoOutput (`display.c:74-88`).
  - `DXGK_EVENT_TYPE` includes DxgkPowerStateEvent (`dispmprt.h:222-228`); `PO_CB_LID_SWITCH_STATE` = 4 (`wdm.h:42463`).
  - Runtime PM: `SupportRuntimePowerManagement`, `DXGK_POWER_COMPONENT_{ENGINE,MONITOR,D3_TRANSITION}`, `ActiveInD3`, and the `DxgkCbSetPowerComponentActive/Idle/CompleteFStateTransition` callbacks (`d3dkmddi.h:2443`, `:3518-3611`; `dispmprt.h:2090-2096`).
  - Lid outside ACPI: the `GUID_DEVICE_LID` / `SYS_BUTTON_LID` interface (`poclass.h:50,554-570`).
- **From Microsoft documentation as I recall it (verify before relying on it):**
  - Windows on Arm supports modern standby only, never S3.
  - Monitor off is SetVidPnSourceVisibility(FALSE) followed by SetPowerState(child 0, D3); wake is the reverse, followed by a re-commit and source address.
  - dxgkrnl idles the GPU before an adapter D3.
  - In modern standby the adapter reaches D3 only through runtime PM (the D3_TRANSITION component). Without it the adapter stays in D0.
  - Lid state reaches the miniport as NotifyAcpiEvent(DxgkPowerStateEvent, PO_CB_LID_SWITCH_STATE). The lid itself belongs to the lid device and the OS power policy, not to the display driver.

### 3. AGX/ASAHI CONTRACT
- **Sleep:** Asahi on t8103 uses s2idle only. CPUs come up by spin-table, with no PSCI (`t8103.dtsi:67`).
  - DCP keeps its power domains on across suspend because "full shutdown" is not supported (`drm/apple/dcp.c:1335-1355`).
  - drm/asahi has no suspend/resume at all (grep of `drivers/gpu/drm/asahi/*.rs`). The GPU firmware powers the GPU off after an idle delay (`hw/mod.rs:606-611`). We set that delay to 20 ms (`shared/src/apple_agx_regionc.c:7-16`).
- **DCP power-off (Asahi `iomfb_template.c:888-977`):**
  1. A clear swap: all surfaces null, black background, backlight value 0 and backlight power 0.
  2. abort_swaps_dcp (A467), then last_client_close (A457), then setPowerState(0) (A472).
- **DCP power-on (`:808-853`):** setDisplayDevice (A410), then setPowerState(1), then a forced backlight update on the next swap. Method IDs are from `iomfb_v13_3.c:20,29,32,33`.
  - Ordering difference: the m1n1 bootstrap calls A472 before A410 (`dcp_iomfb_bootstrap.c:137-169`).
  - Asahi ignores the hotplug_gated callbacks that follow SetPowerState on the main display (`:1029`).
- **Lid:** SMC event 0x7203 and key MSLD; the power button is key bHLD (`input/misc/macsmc-input.c:34,82-88,117-118`). The SMC is an RTKit coprocessor that AML cannot read.
- **GPU power-off:**
  - The m1n1 broker turns off sgx, then gfx-asc (`hv_agx_power_platform.c:16-38`).
  - It refuses OFF while the retained root is Prepared or Active (`hv_agx_power_mmio.c:980-985`, `hv_agx_retained_platform.c:604-607`). In other words, power-off is impossible while any GPU process is alive.
  - The ASC CPU state is lost. The firmware image, UAT tables and initdata stay in DRAM.
- **Cold bring-up in one boot is already proven on hardware:** EXP2307B/EXP2308 did power OFF→ON, a second RR_OPEN and a firmware boot (Wom1RtkitBootResult 0, StartStage 12 status 0). The only failure there was dxgkrnl AddAdapter 0xC0000017 after a PnP restart, which a D-state transition does not involve. This partly answers unknown U3 in `TDR-FIRMWARE-RESTART-RECOVERY.md`, at idle with the root closed.

### 4. TRANSLATION
**Ownership**

| Layer | Owns |
|---|---|
| m1n1 | DCP blank/power commands; PSCI fixes; later, CPU deep-WFI and the A3 cold-path operation |
| Mu/ACPI | Wake-capable interrupts; an SMC device node; later `_LPI` |
| KMD | D-state bookkeeping, quiescing, broker calls |
| A new SMC driver | Lid and power button |

**KMD state:**
- `Display ∈ {On, Blanking, Blank, Unblanking}`, driven by SetPowerState(child 0) and visibility.
- `Adapter ∈ {D0, D3Idle}`. Phase 1 sets D3Idle on adapter D3 only to refuse submissions; the firmware stays up and idles itself.
- `LidState`.

**Display off/on**
- **Off:** visibility FALSE (already defers presents, `scanout_windows.c:1705`), then child D3, then the new broker command DISPLAY_POWER(BLANK). That command is a clear swap with backlight 0, power 0, "unknown" field 1.
- **On:** child D0, then DISPLAY_POWER(ON). The next PRESENT puts the backlight back automatically, because m1n1 writes the fixed value on every swap.

**Broker ABI change**
- **Step A (I would pick this):** blank only.
- **Step B:** A467/A457/A472 power-off, plus A410/A472 power-on. It saves more power but adds more DCP risk.
- Define the C1 brightness register in the same ABI bump so the ABI only changes once.

**Other DDIs**
- **NotifyAcpiEvent:** on DxgkPowerStateEvent with PO_CB_LID_SWITCH_STATE, record the state and return success with AcpiFlags 0. Everything else stays NOT_SUPPORTED.
- **Lid:** a KMDF SMC driver that owns the RTKit SMC, reads MSLD and bHLD, never writes keys, and exposes GUID_DEVICE_LID and the power button. It would later carry AC and battery state too.
  - Alternative: m1n1 owns the SMC, and ACPI gets a GED (generic event device) plus PNP0C0D reading a broker register. It costs more EL2 code; reject it under the "SSD: no EL2 protection" direction.

**Relation to A3 and B2**
- A3 and phase 2 of B3 share one routine, "cold cycle": quiesce, RTKit stop, mailbox ISR close and flush DPCs, broker power-cycle while keeping the root epoch, then power on, SGX pre-ASC write, RTKit boot, new initdata and queue lifetime.
- That routine needs the TDR-design `AGX_RR_RECOVER` operation (section 4.4), because processes stay alive across sleep.
- B2 supplies the ENGINE, MONITOR and D3_TRANSITION components that let Windows request runtime D3 at all.
- Phase 2 also needs `PreservedDuringStandby` on segments backed by RAM; it is unset today (`memory_windows.c:25-50`).

**Offline tests**
- KMD display/adapter state machine: ordering, idempotence, an unknown UID returns INVALID_PARAMETER, no PRESENT while blank.
- Scan-out client: encoding of the new command, monotonic sequence, BUSY handling.
- m1n1 host tests: DISPLAY_POWER refused while a present is pending or unregistered; the BLANK swap fields; RELEASE while blank.
- The SMC64 PSCI default case.
- The NotifyAcpiEvent table.

### 5. WHAT IS STILL UNKNOWN
- **U1:** do sleep states and modern standby actually exist on the guest? Settle with read-only `powercfg /a` and `/availablesleepstates`. No risk.
- **U2:** the real dxgkrnl call order for monitor off. Settle with a receipt-only package and the monitor timeout set to 1 minute; recover with recover.sh.
- **U3:** does the blank swap darken the panel and backlight, and does the next present bring it back? Settle with H1. If the panel stays dark, SSH still works, and a reboot re-initialises the DCP through m1n1.
- **U4:** does a keypress leave modern standby without SharedAndWake and wake arming in the input driver? Settle with H2.
- **U5:** does AppleAns survive modern-standby D-state requests? Read its power code before H2.
- **U6:** does the power manager accept GUID_DEVICE_LID from a non-ACPI driver? Settle with H4.
- **U7:** the DCP A472/A410 order, and the hotplug_gated callbacks in m1n1. Settle with H3.

### 6. STEPS (one variable per boot)
1. PSCI SMC64 default case plus test. No hardware needed beyond a regression boot. Small.
2. Read-only U1 on the guest: `powercfg /a`, `/availablesleepstates`, and confirm `powercfg /h off` with fast startup off. Small.
3. KMD SetPowerState/visibility receipts, and NotifyAcpiEvent lid accept-and-record (H0, U2). Small.
4. Broker DISPLAY_POWER(BLANK/ON), shared client and KMD wiring (H1: idle timeout, Win+L, picture intact). Medium.
5. ACPI SharedAndWake on the keyboard interrupt plus wake arming in the input driver; H2 is Start→Sleep→keypress, ten cycles. Medium.
6. DCP setPowerState(0/1) (H3). Medium.
7. SMC lid driver plus an SMC ACPI node (H4: lid close goes to modern standby, lid open wakes). Large.
8. m1n1 CPU deep WFI plus `_LPI`. Large, optional.
9. After A3 stage 4 and B2: runtime adapter D3 through the cold cycle (H5). Large.

### 7. RISKS
- **Backlight:** only ever send 0 or the proven value 0x58f058d0. Asahi logs "iDAC out of range" for bad values.
- **GPU power while busy:** the broker's OFF refusal, plus the KMD quiesce proof, prevent power-gating the GPU in the middle of a job.
- **SMC writes:** the SMC driver must be read-only on keys. Writing SMC keys touches fans and power.
- **Storage:** hibernation and fast startup must stay off, because a hibernate attempt on AppleAns would lose data. Do not run H2 before U5 is answered.
- **Display:** a DCP that stays dark is recoverable over SSH plus a reboot. Keep the known-good m1n1 as the recovery image for every m1n1 step.

Nothing was written to the repository. The scratch directory `/Users/pavel/public_windows/.local/night-20261110/wp-b1b3/` was created and is empty.
---
Architect notes (2026-11-13T14:02:24Z):
- ACCEPTED: "sleep" on this platform = modern standby (S0 low-power idle): display blank with backlight off, GPU idle,
  instant wake. No S3, no hibernate (the SSD driver refuses the hibernation path by design).
- Broker ABI: DISPLAY_POWER is defined in the same ABI bump as C1 brightness; B1 is implemented on top of the C1
  branches (gpu/c1-brightness, arch/c1-brightness). Step A (blank only) first; DCP setPowerState is a later step.
- Lid and power button: a read-only Windows SMC driver exposing GUID_DEVICE_LID; no EL2 ownership of the SMC.
  This is a driver of its own (it will also carry AC/battery state) - new work package B4-SMC, own design read.
- The PSCI SMC64 missing-default defect is fixed as its own small m1n1 change with a regression test.
