## F1: standalone boot with the GPU (design note, read-only)

**Main finding.** The GPU brokers are already inside m1n1, and the one-shot arm is not a hypervisor feature at all. Standalone boot lacks the GPU for four reasons:
- the standalone image is built from the wrong m1n1 line with the wrong build flags;
- the Mu profile is wrong;
- three small host-side steps are missing from the standalone path (native SSD hand-off, CPPC, initial CPU/DRAM state), and the expanded guest RAM layout needs design;
- there is no production start policy or boot escape.

### 1. CURRENT STATE (corrects the audit)

**What the bench launch is.** It is `full-owner2392.sh` in `.local/experiments/EXP2392-mp1b`:
- It chainloads `m1n1-s2v3-gc.macho`, built with the make flags `AGX_LOCAL_RESERVE_V2 IOMFB_FULL_OWNER AGX_BROKER_LOCK_SPLIT AGX_SCANOUT_TWO_LAYERS` (`m1n1-build-info.json`).
- It then runs `run_uefi.py` with `--storage-mode ANS_NATIVE --ans-direct --display-mode physical --debug-mode off --low-mem --guest-ram-profile expanded`.
- Environment: `WOM1_CPPC=1 WOM1_AGX_G2_POWER_BROKER=1 WOM1_PCPU_PSTATE=12 WOM1_DRAMCFG_FOLLOW_PSTATE=1 WOM1_ANS_CONTRACT=...`.
- Mu firmware: MUANSCPPC002.

**Corrections to the audit (REPORT.md:71):**
- **Brokers.** The GPU VA, power and scan-out brokers are C hooks installed by `hv_agx_g2_resources_map()` (`hv_agx_power_mmio.c:1017-1100`). The standalone stage1 already calls it, unconditionally (`hv_autonomous_runtime.c:328`).
- **The arm.** It is not written by the launcher. The guest script `arm-gpuva-experiment.ps1:13-15` writes the device-key value `G3Armed=1` over SSH. StartDevice deletes and flushes it, then reports HVC 0x4d32 (`lifecycle.c:65-110, 289-293`). m1n1 only validates the version and prints the call (`hv_guest_ipa_pa.c:5-15`, `hv_exc.c:108-119`); it keeps no state.

**Every host-side step, with its size:**

| Step (source) | Status / size |
|---|---|
| Chainload of the bench m1n1 (full-owner script) | The standalone `boot.bin` packs stage0 and stage1 from the `m1n1_windows` gitlink 882985fb, without the four make flags (`build-standalone.sh:268-293`). **Small move**: build from the bench line with the flags. |
| `cpufreq_init` (`run_uefi.py:277`) | **Already in m1n1** (`hv_autonomous_runtime.c:412-426`) |
| P-cluster P-state 12 (`run_uefi.py:284-290`, `j313_cpufreq.py:17-51`) | **Small move** (one register plus a poll) |
| DRAMCFG initial state (`run_uefi.py:297-307`) | **Small move**. At runtime, `hv_cppc.c:121-124` already switches dramcfg. |
| Expanded RAM: 64 MiB proxy heap, base 0x818000000 (`run_uefi.py:291, 318-324`, `guest_memory.py`) | **Needs design**. Stage1 and its golden contract are hard-wired to legacy RAM: base 0x850000000, size 0x18f708000 (`hv_launch_golden_j313.c:54-63`). |
| Launch descriptor, guest-base prediction, contract checkpoints (`run_uefi.py:327-411`) | **Already** (stage1 publish and check). The golden values must be regenerated (**medium**). |
| `hv.init` registers, vUART, PCI/vNVMe, xHCI hook, low-mem map, ramdisk header clear, boot args/ADT (`hv/__init__.py:1749-1838`) | **Already** (`hv_autonomous_runtime.c:204-329, 451-460`) |
| AGX power broker plus retry after the stage-2 update (`hv/__init__.py:1933-1942, 2524-2526`) | **Already**. Stage1 maps RAM before the broker, so no retry is needed. |
| ANS native broker and ANS-direct (`hv/__init__.py:1880-1931`, JSON contract) | **Small move**. The C side already rejects any argument other than its constants (`hv_ans_platform.c:423-478`). It must run after the `/arm-io` pass-through map. |
| CPPC page, proxy op 0xc30 (`run_uefi.py:589-592`) | **Small move**: one `hv_cppc_map()` call |
| DCP surface and display handoff (`run_uefi.py:570-584`) | **Already** (READY stage, `runtime.c:461-481`) |
| Mu build profile (`build-in-container.sh`: `AGX_ABI_ADMISSION_PROFILE`, `AGX_RESERVE_V2_PROFILE`, `ANS_NATIVE`/`INSTALLED`/`DIRECT`) | `build-standalone.sh:66-73` only knows the old `--agx-g2-profile`. **Small move.** |
| Host runtime loop: framebuffer/telemetry events, SIGINT snapshots, watchdog | Host diagnostics only; nothing GPU-functional |
| Package staging, `G3Armed`, `Wom1*` switches, ordered restart (`stage*.ps1`) | **Needs design**: a production INF with defaults, plus a start policy |

**Standalone today.** iBoot runs the Asahi stub, which loads `ESP/m1n1/boot.bin` (stage0 m1n1, then the bootstrap, then stage1 with Mu xz-compressed). Stage1 runs autonomously with the vNVMe bridge, legacy RAM and the physical DCP surface (`documentation/RUN.md:9-39`). The ledger has no standalone boot with any GPU-era m1n1. The documented cold boots are the pre-GPU Phase 0 boots. The bench ESP holds a proxy-only m1n1.

### 2. WINDOWS CONTRACT

- **Failed StartDevice.** It leaves a problem code and the desktop on the boot framebuffer. This is observed (EXP1049, EXP2307) and is the safe fallback.
- **Start once per boot.** The PnP restart in the same boot is still broken: dxgkrnl's AddAdapter returns 0xC0000017 after a second StartDevice (EXP2308). Standalone needs only one start per boot.
- **Shutdown signal.** `DXGKDDI_SET_POWER_STATE` takes a `POWER_ACTION ActionType` (`dispmprt.h:2318-2326`). Microsoft documentation describes shutdown/reset arriving there as `PowerActionShutdown*`. The KMD ignores `ActionType` today (`lifecycle.c:1238-1242`).
- **Recovery modes.** No per-driver start-once facility exists (Microsoft documentation). Safe Mode uses Basic Display. Whether AppleAns loads in Safe Mode or WinRE is unknown.
- **Signing.** Test signing is required until the package is production-signed. That belongs to the installer session.

### 3. AGX/ASAHI CONTRACT

- The hardware needs no arm (START-GATE-PRODUCTION-CONTRACT.md, "AGX/ASAHI CONTRACT").
- **Mu boot hazard.** Mu PEI refuses to boot without a valid v3 reserve receipt (`MemoryInitPeiLib.c:562-569`, `AGX_LOCAL_RESERVE_V2`). So "GPU off" must keep the receipt valid.
- **Existing fail-closed path.** When the receipt does not validate, the ACPI `_CRS` falls back to `BAS0` (`J313AppleAgxAbiAdmission.asl.inc:71-99`). The KMD then refuses with `STATUS_DEVICE_NOT_READY` before touching the arm or the GPU (`lifecycle.c:114-170, 281-285`).
- **m1n1 inputs.** m1n1 has a Rust NVMe/FAT reader (`chainload.c:215-232`) but no FAT writer, no NV variable store and no SPI keyboard. Mu has no NV store either (`MacBookAirMid2020.dsc:155-163`), and only a USB keyboard on the free Type-C port. So m1n1 cannot persist a boot counter.

### 4. TRANSLATION (design)

**A. One C platform profile owned by m1n1.** Add `hv_platform_profile_apply(profile)` to m1n1. It does, in stage1 order:
1. RAM and low-mem maps;
2. the `/arm-io` pass-through;
3. AGX resources;
4. ANS broker plus direct mode;
5. CPPC;
6. P-cluster P-state;
7. DRAMCFG.

Stage1 calls it from its PLATFORM/STAGE2 stages. A new proxy op lets `run_uefi.py` call the same routine instead of its Python steps when `--profile production` is given. Experiments keep every existing switch, and production runs and the bench execute the same code.

**B. Expanded RAM in C.** Port the `expanded_ram_base` carveout and heap checks. Stage1's heap must end below 0x818000000. Regenerate the golden contract deterministically from an accepted run's `--contract-output contract.bin` (EXP2392 has one). Do not hand-edit it.

**C. Start policy in the KMD (layer L0, automatic).** Implement the crash latch already designed in START-GATE-PRODUCTION-CONTRACT.md:
- `Wom1StartPolicy=1` in the production INF;
- a pending value cleared on error, on clean stop, or T=120 s after start;
- a stale pending value latches, and the adapter then fails closed.

Extend it with an unclean-boot counter. Set `Wom1Running` at start. Clear it on SetPowerState with a shutdown action, or on a clean stop. If it is still set at the next start, increment a consecutive count; latch at N=3; reset after a clean shutdown. This covers late crash loops.

The core is a pure state machine in shared C with host tests:
- stale pending latches;
- an error clears;
- a clean stop clears;
- the timer clears;
- a latched adapter refuses;
- the counter latches at N and resets on clean shutdown;
- policy 0 stays byte-identical to today.

**D. Firmware escape (layer L1, manual and independent of Windows).**
- Add a field `LADM` at receipt offset +0x28 that m1n1 writes.
- In Mu, `_CRS` returns `BAS0` unless `LADM==1`. The PEI reservation and receipt stay valid, so Mu still boots, and the existing KMD branch refuses.
- The source of `LADM` is a new `boot.bin` flag bit (`HV_AUTONOMOUS_KNOWN_FLAGS` 0x1f, `hv_autonomous_profile.c`), set by `pack_boot.py --agx off`, plus a small `flip-flag` tool that recomputes the CRC.
- The "safe" image is the same binaries with the bit cleared.

**E. Layer L2, always available.** Apple startup options lead to macOS or 1TR, where `install-esp.sh install --image boot-safe.bin` or `restore` applies (`RUN.md:98-108`). Updates from Windows use `mountvol /s` and the same hash-verified copy. The installer session consumes `dist/j313/<profile>/boot.bin`, `boot-safe.bin` and `MANIFEST.json`.

**F. Bench profile.** A `debug=uart` image takes the proxy when a host enumerates within 3 s (`hv_autonomous_profile.c:31-45`). For unattended pipelines, add a "host-wins" variant: if a host enumerates, hold indefinitely; otherwise boot Windows.

### 5. WHAT IS STILL UNKNOWN

1. **Heap placement.** Where stage1's heap and image land when Asahi's stub chainloads `boot.bin`, and whether 0x818000000 is free. *Checkpoint:* a stage1 `debug=monitor` print, replayed in assisted mode (`chainload.py -r boot.bin`; raw chainload is a hypothesis to verify offline first). *Recovery:* reboot to the proxy ESP.
2. **Console with no host.** Whether broker `printf` volume blocks when no host is attached in the release profile. *Checkpoint:* the release image replayed, with the host detached after chainload.
3. **SSD after the stub.** Whether ANS-direct cold boot works after the Asahi stub's own NVMe use. *Checkpoint:* the same replay.
4. **Durability and shutdown.** Whether registry flushes are durable across an EL2 reset on AppleAns, and whether `PowerActionShutdown` arrives (START-GATE unknowns 2-3). *Checkpoint:* latch hardware checks L1/L2.
5. **Safe Mode / WinRE.** Whether Safe Mode or WinRE sees the disk. *Checkpoint:* one `bcdedit /set safeboot minimal` boot in assisted mode.

### 6. STEPS

Each step changes one variable per boot.

1. **Build `boot.bin` from the bench m1n1 line, with the flags and the Mu `gpu-direct` profile.** Verify offline by comparing manifests. *Small.*
2. **`hv_platform_profile_apply` with ANS-direct, CPPC, P-state and DRAMCFG, plus the proxy op.** Hardware: an assisted run using the op must match EXP2392 (suite 119/0, perf gate). *Medium.*
3. **Expanded RAM in stage1 and regenerated golden values.** Hardware: assisted replay of `boot.bin`, launch contract equal to the accepted run. *Medium-large.*
4. **KMD start policy.** Hardware: L0, policy absent, must equal the accepted package. L1, policy 1 with no arm: boot plus ordered restart. *Medium.*
5. **`LADM` receipt field (m1n1 + Mu `_CRS`) and the flag tool.** Hardware: the safe image in assisted replay gives Code 10/43 with the desktop on the framebuffer; the normal image gives Code 0. *Small-medium.*
6. **Latch check L2.** A forced stop (DcomLaunch kill) inside T, then the next boot. *Small.*
7. **Bench "host-wins" profile.** *Small.*
8. **First true standalone boot.** Back up the ESP, install the release `boot.bin` from macOS, cold boot without the host. Pass = accelerated desktop, suite PASS, perf within gate. *Recovery:* startup options, macOS, `install-esp.sh restore`. *Small to run.*

### 7. RISKS

- **Unbootable Windows.** A driver crash loop is covered by L0. If L0 has a defect, L1 and L2 still work, because they do not depend on Windows. The ESP backup plus macOS restore is Apple-guaranteed.
- **Mu refusing to boot.** This happens if the receipt is invalid. The safe mode therefore keeps the receipt and changes only `_CRS`.
- **Wrong RAM layout or stale golden values.** Stage1's fail-closed preflight refuses entry rather than booting a mismatched layout.
- **Disk corruption.** ANS-direct is the same path the bench already uses daily. Do not mix it with the vNVMe bridge in one image.
- **Assisted-only fixes.** Never install a `boot.bin` that was not first replayed in assisted mode (`PLATFORM_STABILITY.md:166-185`).
- **No other recovery inside Windows.** No firmware-level automatic counter is possible. The only automatic protection is L0; every other escape is manual.

Nothing was written to the repository. No hardware or guest machine was touched.
---
Architect notes (2026-11-13T14:02:51Z):
- ACCEPTED. One C platform profile in m1n1 used by both the standalone stage1 and the assisted launcher; KMD start
  policy (crash latch + unclean-boot counter) as the automatic protection; firmware flag (LADM) as the manual escape;
  macOS restore as the last resort. No boot.bin is installed that was not first replayed in assisted mode.
- Implementation split: F1-m (m1n1 steps 1, 2, 3, 5, 7) and F1-k (KMD start policy, step 4). Hardware order: steps 2,
  3, 4, 5, 6 in assisted mode, then the first true standalone boot with the operator (ESP backup first).
