# Standalone working profile: assisted vs autonomous launch contract

Status 2026-11-17: implemented and built offline, **never run on hardware**. Nothing here
has a hardware verdict. The preregistered checkpoint is in section 7.

The working profile is the bench standard (`.local/experiments/BENCH-M1N1.json`, accepted
2026-11-15): m1n1 6a9c2435 (gpu/bench-int 54c329a4) + Mu MUCPC001 d9fe3a34, GPU package
pkg/next 04fe217c2, AppleAns 0.4.8.9 (BounceEnable=1), AppleKbdBacklight LVL-9c930a8f,
validated by EXP2480/2481/2491/2492. Until now it existed only as an assisted launch.

## 1. Sources inspected

- Assisted launch, as the bench runs it (EXP2481 `full-owner2481.sh`):
  `run_uefi.py` (integration worktree, sha256 d26594fa…, recorded in the EXP2481 hardware
  manifest), proxyclient `m1n1/hv/__init__.py` and `m1n1/hv/guest_memory.py` from
  `.local/m1n1-wt-ans-direct-slc` (06616be7, the `M1N1_PROXYCLIENT` of that launcher),
  `tools/j313_cpufreq.py`, `config/j313-ans.json` (ans-native worktree),
  `config/j313-guest-layout.json`.
- Measured assisted state: EXP2481 `contract.bin` (four checkpoints, decoded with
  `tools/launch_contract.py`) and EXP2481 `full.log`.
- m1n1 autonomous runtime at 54c329a4: `src/hv_autonomous_runtime.c`,
  `src/hv_autonomous_boot_runtime.c`, `src/hv_autonomous_profile.c`,
  `src/hv_autonomous_manifest.[ch]`, `src/hv_launch_golden_j313.c`, `src/hv_launch_j313.c`,
  `src/hv_guest_cpu_state.c`, `src/hv.c` (`hv_prepare_guest_cpu_state`, `hv_start`),
  `src/hv_ans_platform.c`, `src/hv_agx_power_mmio.c` (`hv_agx_g2_resources_map`),
  `src/hv_cppc.c`, `src/cpufreq.c`, `src/mcc.c`, `src/proxy.c` (`seal_heap_below_guest`).
- Packaging: `scripts/build-standalone.sh`, `tools/pack_boot.py`, `standalone_image.py`,
  `bootstrap_image.py`, `launch_profile.py`, `guest_layout.py`, `tools/artifact_manifest.py`,
  `scripts/install-esp.sh`, `documentation/RUN.md`.
- Mu: `Silicon/Apple/AppleSiliconPkg/PrePi/AdtParser.c:36` (RAM size from boot args).
- Wi-Fi line (rows only): EXP2559 `launch-wifi2559.sh`, its proxyclient `m1n1/hv/__init__.py`.
- Keyboard backlight: side note `kbd-backlight.md` (KBDL is Mu ACPI only).

## 2. Assisted contract of the bench standard

`run_uefi.py` lines refer to sha256 d26594fa…; `hv/__init__.py` lines to 06616be7.

| # | Knob | Value on the bench | Applied at |
|---|---|---|---|
| A1 | CPU frequency preflight | `cpufreq_init()` | run_uefi.py:281-284 |
| A2 | P-cluster P-state | `WOM1_PCPU_PSTATE=12` (2988 MHz), register write + BUSY poll | run_uefi.py:288-294, j313_cpufreq.py:27-52 |
| A3 | DRAMCFG follow | default on; FAST pair on 8 channels when P-cluster MHz >= `mcx-fast-pcpu-frequency` (2184) | run_uefi.py:301-311, j313_cpufreq.py:67-86 |
| A4 | Proxy heap | 64 MiB (expanded) | run_uefi.py:295 (host-side only) |
| A5 | Fixed Mu anchor | heap_top pinned to layout phys_base 0x850000000 | run_uefi.py:314-319 |
| A6 | Guest RAM base | expanded: 0x818000000 after `expanded_ram_base()` checks | run_uefi.py:322-326, guest_memory.py:5-41 |
| A7 | Launch descriptor + heap seal | `P_HV_LAUNCH_PUBLISH`; m1n1 seals the heap ceiling at the guest RAM base | run_uefi.py:361-395, m1n1 proxy.c `seal_heap_below_guest` |
| A8 | Contract checkpoints | PRE_HV_INIT, POST_HV_INIT, POST_MAPS, PRE_GUEST recorded to contract.bin | run_uefi.py:415, 506, 610-611 |
| A9 | EL2 register image | HACR traps, MDCR TDE/TDA/TDOSA/TDRA (0xf00), MDSCR.MDE, AMX EN_EL1, VMKEY, APSTS, ACTLR.EnMDSB | hv/__init__.py:1796-1842 |
| A10 | vUART | `hv_map_vuart(uart0, irq)` | hv/__init__.py:1831, 1846-1853 |
| A11 | Emulated PCIe/NVMe (bridge) | ECAM 0x690000000, BAR 0x400000000, INTx vSPI 64 | hv/__init__.py:1832, 1855-1878 |
| A12 | AGX G2 power broker | `WOM1_AGX_G2_POWER_BROKER=1`: `P_HV_MAP_AGX_POWER` (+broker page 0x300000000) | hv/__init__.py:1833, 1933-1942 |
| A13 | Native SSD | `--storage-mode ANS_NATIVE --ans-direct --ans-production-fd-sha256 d9fe3a34…`: `hv_map_ans_broker(0x300010000, 0x4000, 960, 1)` + `hv_ans_direct(1)` | run_uefi.py:102-122, hv/__init__.py:1834, 1880-1931 |
| A14 | ADT edits | product strings; USB debug-port nodes removed; no WDT CPU | hv/__init__.py:1844, 2078-2127 |
| A15 | xHCI hook | 16 KiB C hook kept over the /arm-io pass-through | run_uefi.py:510 |
| A16 | Boot args | phys_base/mem_size/virt_base/devtree from the guest RAM base; video = 2560x1600 fb at 0x85f000000 | run_uefi.py:511-518, hv/__init__.py:2252-2268 |
| A17 | Stage-2 RAM | RAM-HIGH 0x818000000..0x800000000+mem_size_actual minus MCC TZ carveouts (EXP2481: 3 unmaps) | hv/__init__.py:2205-2207, 2137-2148 |
| A18 | Secondary RVBARs | entry & ~0xfff into every non-running CPU | hv/__init__.py:2270-2277 |
| A19 | Ramdisk window | header cleared (no boot medium) | run_uefi.py:545-551 |
| A20 | Low memory | IPA 0x100000+0x3ff00000 -> 0x8a0100000 | run_uefi.py:553-559 |
| A21 | PMGR/CPU-start | Python traps (UART0/ATC PMGR, CPU_START) | hv/__init__.py:1944-2058 |
| A22 | AGX retry | second `P_HV_MAP_AGX_POWER` after the final pt_update (local reserve receipt) | hv/__init__.py:2397-2402 |
| A23 | Display | `fb_improve_logo`, `fb_shutdown(True)`, then `display_prepare_guest_surface` (physical) | hv/__init__.py:2408-2416, run_uefi.py:598-605 |
| A24 | CPPC | `WOM1_CPPC=1`: `P_HV_MAP_CPPC` 0xc30 after PRE_GUEST capture | run_uefi.py:612-617 |
| A25 | Entry | SPRR, GXF, `hv_start(entry, bootargs)`; `hv_start` writes MDCR_EL2=0 (EXP2256 guest owns debug) | hv/__init__.py:2418-2440, m1n1 hv.c:388-396 |
| A26 | vGIC, timers, PSCI, IRQ routes | same C `hv_init()` in both paths; routes (EXP2481 contract.bin): 857, nine AGX G2, AppleInput 330->865, ANS 590->960 | m1n1 hv.c:183-220 |
| A27 | Legacy escape hatch | `WOM1_ALLOW_LEGACY_LAUNCH_CONTRACT=1` (assisted-only A/B allowance) | run_uefi.py:381-395 |
| A28 | m1n1 build | `AGX_LOCAL_RESERVE_V2=1 IOMFB_FULL_OWNER=1 AGX_BROKER_LOCK_SPLIT=1 AGX_SCANOUT_TWO_LAYERS=1 CPPC_CAPACITY_SCALE=1`, no RELEASE | BENCH-M1N1.json |

## 3. Difference table

"Before" = m1n1 54c329a4 autonomous runtime; "after" = branch `standalone/working-profile`
(m1n1 ef1f9698). Manifest flag bits in brackets.

| # | Before | After | Commit |
|---|---|---|---|
| A1 | same (`cpufreq_init`, PLATFORM stage) | same | — |
| A2 | **missing** (stays at P7) | PLATFORM stage requests the manifest P-state on the P-cluster and reads it back [bits 12..15] | f43609a9 |
| A3 | **missing** | PLATFORM stage applies the FAST DRAMCFG pair by the same rule (pure, host-tested) | f43609a9 |
| A4 | n/a (host-side) | n/a | — |
| A5 | same (layout anchors) | same | — |
| A6 | **different**: RAM base fixed at 0x850000000 | expanded base 0x818000000 after the ported validation [0x100] | 47ee22af |
| A7 | **missing** heap seal | heap sealed at the guest RAM base after decompression, fail closed | 47ee22af |
| A8 | different by design: checked against the golden contract, fail closed | golden gains expanded RAM and the ANS route | 47ee22af, 8a8b88be |
| A9 | **defect**: MDCR 0 before POST_HV_INIT (f9712bb0) -> standalone POST_HV_INIT fails | 0xf00 at the boundary, 0 at entry as before | 485c1524 |
| A10 | same | same | — |
| A11 | same | same | — |
| A12 | **different**: mapped unconditionally, once | only with [0x40]; failure is a named PREFLIGHT line | 76a8c17b |
| A13 | **missing** | PCI_NVME stage maps the broker and arms direct mode with the ABI constants [0x20] | 8a8b88be |
| A14 | different, unchanged: no ADT edits (quiet profile has no USB debug port) | unchanged | — |
| A15 | same (XHCI stage) | same | — |
| A16 | different for expanded | follows the guest RAM base; `mem_size_actual`=mem_size stays (Mu reads only phys_base and mem_size, AdtParser.c:36) | 47ee22af |
| A17 | **different**: only up to the boot-args RAM end 0x9df708000, no carveout handling | same segments as assisted (pure, host-tested) | de52a0f3 |
| A18 | same | same | — |
| A19 | same | same | — |
| A20 | same | same | — |
| A21 | different by design (native C hooks / pass-through, hv_launch_j313.c comment) | unchanged; validated only by the August release cold boot | — |
| A22 | **missing** | retried at READY before the display hand-off | 76a8c17b |
| A23 | different, unchanged: no `fb_improve_logo`/`fb_shutdown`; `display_prepare_guest_surface` present | unchanged | — |
| A24 | **missing** | after the PRE_GUEST gate, before entry [0x80] | ef1f9698 |
| A25 | same | same | — |
| A26 | same C | same | — |
| A27 | n/a | n/a | — |
| A28 | packaging built m1n1 with RELEASE=1 and no flags | `build-standalone.sh --working-profile` uses the bench flags, no RELEASE | root 69cd1ca08 |

Packaging: the manifest flag ABI (root e8732314e), `pack_boot.py --working-profile
--firmware-sha256` with a layout-window check (root d6bba7894), and `build-standalone.sh
--working-profile --mu-fd --mu-fd-sha256`, which packs the pinned bench FD without
rebuilding Mu (root 69cd1ca08). The working profile encodes as flags 0xc1e1
(physical/off) and 0xc1f1 (physical/monitor).

## 4. Per-driver needs at autonomous boot

| Driver | From m1n1 | From Mu (MUCPC001) | Standalone status |
|---|---|---|---|
| AGX GPU (AppleAgx pkg/next 04fe217c2) | bench make flags (A28); G2 resources + broker page (A12); retry (A22); stage-2 RAM tail incl. retained root 0x9fff78000 (A17); nine AGX IRQ routes (A26); scanout full owner + two layers | APPL0002 G2 ACPI that touches broker 0x300000d00 | implemented |
| Native SSD (AppleAns 0.4.8.9) | ANS broker + direct mode (A13); heap seal (A7); IRQ 590->960 | APPL0003, EBS hand-off to the broker | implemented |
| CPPC / P-state | P12 + DRAMCFG (A2, A3); CPPC page (A24); `CPPC_CAPACITY_SCALE` build | `_CPC` capacity-scaled DSDT | implemented |
| AppleInput keyboard/trackpad | `hv_apple_input_prepare_runtime` in `hv_init` (route 330->865) | APPL input ACPI | same C in both paths |
| Keyboard backlight (KBDL, APPL0005) | nothing: FPWM1 MMIO is /arm-io pass-through; PMGR PS written only by ASL | Device KBDL | no m1n1 change needed |
| Expanded guest RAM | A6, A16, A17 | RAM HOB from boot args | implemented |
| Display | `display_prepare_guest_surface` (physical); full-owner scanout via the AGX broker | GOP on the guest surface | same as release + A12 |
| Wi-Fi (optional, not in the default image) | see section 5 | MUCPCWIFI001 (fd94b781) | rows only |

## 5. Optional Wi-Fi profile (not implemented)

Assisted source: EXP2559 `launch-wifi2559.sh` with m1n1 macho 609a98bc (wifi bench-int
257d408e) and Mu MUCPCWIFI001 fd94b781. For a future manifest profile it needs:

| Knob | Assisted | Standalone equivalent needed |
|---|---|---|
| `WOM1_WIFI_PCIE=1` | `pcie_init()` before `hv_init` (link trained, kept), then `map_apcie_ecam` (segment 1 ECAM filter, `hv_map_apcie_ecam`) | build flag `WIFI_PCIE_LINK=1`; PLATFORM stage `pcie_init()`, PCI stage ECAM filter |
| `WOM1_WIFI_PCIE_BAR_TRAP=1` (or `_BAR_RO`, or plain map) | `hv_apcie_bar_trap(bus, cpu, 16 MiB)` | same call, flag bit |
| `WOM1_WIFI_PCIE_INTX=1` | `hv_apcie_intx(port_base, irq, irq, 0x3)` | same call; golden route |
| `WOM1_WIFI_PCIE_DMA=1` | `hv_apcie_dma(0x100000, 0x3ff00000, sid 1)` | same call |
| `WOM1_HV_SERROR_INJECT=1` | `hv_serror_policy(True)` (`P_HV_SERROR_POLICY` 0xc45) | same call |
| `WOM1_WIFI_PERST_FIRST` (opt-in) | `wlan_perst_first` | flag bit |
| Mu | MUCPCWIFI001 (MCFG entry 1, PCI1, _PRT) | pinned FD in a second manifest profile |

A free manifest bit range remains (0x200..0x800, bits 16+), so this can be a second profile
with its own golden routes. No code was written for it.

## 6. Layer ownership

- m1n1 owns guest RAM layout, stage-2, IRQ routes, the ANS hand-off, the AGX broker, CPPC
  and the CPU/DRAM operating point. All knobs above live in the m1n1 autonomous runtime,
  and each one is gated by a manifest flag.
- `build-standalone.sh` and `pack_boot.py` own packing, the m1n1 make flags and the Mu FD pin.
- Mu owns ACPI (APPL0002/3/5, `_CPC`, KBDL) and the EBS hand-off request. The bench FD is
  packed unchanged.
- Windows drivers own runtime operation after entry. None of them changed.

## 7. Hardware checkpoint (preregistered, not run)

The run sheet is `.local/handoff/night-20261117/m1n1-standalone-working.md`, section
HARDWARE-READY. Summary: one standalone cold boot of `dist2/working/boot.bin` (sha256
5a4dae7f…, m1n1 b2b07e10; supersedes `dist/working` 37309f5c…, which lacks the D209 fix
P2) plus one ordered restart after staging the GPU package. Success means the
installed Windows reaches the desktop on native AppleAns, with APPL0002 at Code 0 after
that restart. The bench assisted launch remains the rollback.

## 8. Proxy-side step audit (2026-11-17, second pass)

Every step the bench launcher performs on the host before or after the m1n1 payload starts,
for the working profile (EXP2491 `full-owner2491.sh`: chainload.py from
integration-ad04 `m1n1_windows` 0d3a75b3, run_uefi.py with the proxyclient
`m1n1-wt-ans-direct-slc`; Wi-Fi from EXP2585 `launch-wifi2585.sh`). Status is that of
`standalone/working-profile` b2b07e10 + root bb153a97e.

| # | Host step | Source | Standalone | Status |
|---|---|---|---|---|
| P1 | `PMU(u).reset_panic_counter()`, twice: SPMI write of 0 to the PMU scratchpad error-count byte | `m1n1/setup.py:29,33` (imported by chainload.py:20), `hw/pmu.py:28-31` | not done | **not done, decision**. It is an NVMEM write, under the "no NVRAM/SPI writes" rule and the open user decision of 2026-11-17. The standalone boot never resets it; macOS resets it when it boots (hypothesis, not measured). Consequence if it grows: iBoot may offer recoveryOS after repeated failed boots. The operator then picks the startup disk; nothing is lost. |
| P2 | IOMFB UTC anchor in /chosen (`m1n1-iomfb-utc-ms/-cntpct/-cntfrq`) from the host clock | chainload.py:108-114 | none: no host. `dcp.c` D209 then failed the IOMFB bootstrap (`fail(bootstrap)`, dcp_iomfb_bootstrap.c:342-355) | **was missing, fixed**: m1n1 b2b07e10 `IOMFB_UTC_UPTIME=1` answers D209 from the uptime clock, as Linux without an RTC (Asahi iomfb_template.c:713 `ktime_get_real`); root bb153a97e adds the flag to the working build |
| P3 | SEPFW and preoslog copied next to the new image, /chosen memory-map updated | chainload.py:42-74 | `prepare_boot_data` reads TrustCache, SEPFW and preoslog from the memory map (hv_autonomous_runtime.c:213-230) | done |
| P4 | Secondary CPU RVBARs = entry & ~0xfff | chainload.py:116-124 | A18 | done |
| P5 | Boot args, stub, `p.reload` into the new m1n1 | chainload.py:128-190 | iBoot -> stage1 -> boot.bin stage chainload in C | done (by design) |
| P6 | run_uefi.py steps A1..A27 | section 2 | section 3 | done, except the by-design rows A14, A21, A23, A27 |
| P7 | `WOM1_ALLOW_LEGACY_LAUNCH_CONTRACT=1` on every bench launcher | full-owner2491.sh:12, launch-wifi2585.sh:14 | golden contract enforced, fail closed | different by design. The bench does not prove the golden for itself; EXP2481 `contract.bin` matches it (hv_launch_golden_j313_test) |
| P8 | `WOM1_ANS_CONTRACT=config/j313-ans.json`: broker base/size/GSIV/ABI | hv/__init__.py:1880-1931 | ABI constants in `hv_ans_broker_abi.h`, the same values | done (8a8b88be) |
| P9 | ANS-PRIVATE / ANS-BROKER RESERVED tracers keep the broad /arm-io replay off the C ANS hooks | hv/__init__.py:1919-1931 | `map_arm_io_ranges` runs at STAGE2, before the ANS hooks at PCI_NVME, so the hooks win | done (order) |
| P10 | `WOM1_AGX_G2_POWER_BROKER=1` map + retry | hv/__init__.py:1933-1942, 2397-2402 | flag 0x40, retry at READY | done (76a8c17b) |
| P11 | `--guest-ram-profile expanded`, `--low-mem` | run_uefi.py:322-326, 553-559 | flag 0x100; low memory = A20 | done |
| P12 | `WOM1_PCPU_PSTATE=12`, `WOM1_DRAMCFG_FOLLOW_PSTATE=1`, `WOM1_CPPC=1` | run_uefi.py:288-311, 614-617 | P-state bits, PLATFORM stage, flag 0x80 | done |
| P13 | Wi-Fi: `WOM1_WIFI_PCIE=1` (pcie_init before hv_init), `_INTX`, `_DMA`, `_BAR_TRAP`, `WOM1_HV_SERROR_INJECT`, `_PERST_FIRST`; m1n1 build `WIFI_PCIE_LINK=1` (plus `APCIE_BAR_DIAG`/`APCIE_BAR_BOUND` on wifi/bar-diag) | hv/__init__.py (wifi bench-int) 1749-1760, 1929-1960; launch-wifi2585.sh:14 | none | **missing by decision**: optional second profile (section 5), not in the default image |
| P14 | Host services while the guest runs: USB proxy console, Python traps, hang/reset pollers | hv/__init__.py, bench scripts | native C hooks; no host | by design (A21); evidence comes from the guest (SSH) and the monitor variant |

## PORTABILITY

- SoC data stays in tables: the P-cluster MHz table and cluster index
  (`hv_autonomous_platform.c`) and the expanded RAM base (`HV_AUTONOMOUS_EXPANDED_RAM_BASE`)
  are T8103/J313 values. The DRAMCFG threshold comes from the ADT
  (`mcx-fast-pcpu-frequency`), carveouts from the ADT and the MCC, and ANS constants from
  `hv_ans_broker_abi.h`. On another SoC, P-state and DRAMCFG fail closed (`chip_id != T8103`).
- The manifest ABI is versioned (format 1, layout 1). New knobs take new bits, unknown bits
  are rejected, and the P-state field rejects turbo states.
- The golden contract is per target (J313). A new target needs its own measured assisted
  `contract.bin`, the same way EXP2481 supplied these values.
