# Mu wall-clock source (design, plan row B20)

Status: design only, plus a reference conversion with a unit test (`tools/rtc_wallclock`). Nothing has run on
hardware. Stage 1 can be approved by the Architect. Stage 2 needs the owner's word.

## 1. Problem (R)

- Our Mu exposes no wall time.
  - `AppleSiliconPkg.dsc.inc:606` builds `RealTimeClockRuntimeDxe` with `AppleSiliconPkg/Library/VirtualRealTimeClockLib`.
  - Its `LibGetTime` returns the year and month of a static `BaseTime` that is never set (so 0), plus the uptime
    from the generic timer.
  - Its `LibSetTime` returns `EFI_UNSUPPORTED`.
- Windows therefore starts from its own saved time and relies on network time.
  - Any Windows downtime shows up as an offset until W32Time syncs.
  - EXP3011/EXP3013 (no network): about 25.6 min, roughly the downtime before EXP3011.
  - This is I, consistent with the timeline. The read-only check in §7 confirms it.
- macOS reads the real RTC, but has been about 1194 s behind on this machine since at least EXP2831 (2026-11-20),
  natively.
  - So the hardware RTC (counter plus stored offset) is itself about 20 min off (I).
  - Consequence: a stage-1 reader would give Windows macOS's error (about 20 min) instead of the downtime error.
  - That is better on a cold boot, but still wrong until NTP.
  - Correcting the stored offset is stage 2, or a one-time owner action in macOS: Date & Time, "Set automatically".

## 2. The hardware RTC (R: behaviour from the Asahi sources, no code copied)

- Counter (R, Asahi rtc-macsmc).
  - SMC key `CLKM`: 6 bytes, a 48-bit counter of 32768 Hz ticks.
  - The driver requires exactly 6 bytes back.
- Offset (R, Asahi rtc-macsmc; binding `rtc/apple,smc-rtc.yaml`).
  - NVMEM cell `rtc_offset`: 48-bit, the same 32768 Hz ticks, stored in the PMU.
  - The SMC only abstracts the RTC; the binding says the RTC "is part of the PMU IC".
- Wall time (R, Asahi rtc-macsmc).
  - Formula: `sign_extend_48(CLKM + rtc_offset) >> 15` = Unix seconds, UTC.
  - The offset is zero-extended on read. The sum is taken modulo 2^48 and then sign-extended.
- Format span (R, Asahi range_min/range_max).
  - ±2^32 s around 1970, so about 1834 to 2106-02-07.
  - The unit test pins the wrap after 2106.
- Writer (stage 2 only; R, Asahi rtc-macsmc).
  - `off = (t << 15) - CLKM`, truncated to 6 bytes, written to the PMU cell.
  - Nothing is written to the SMC.
- PMU NVMEM access (R, Linux `drivers/nvmem/apple-spmi-nvmem.c`).
  - SPMI extended register access with a 16-bit address.
  - The cell offset is used directly as the register address; byte-wide, stride 1.
  - Both Linux files are "GPL-2.0-only OR MIT"; we use behaviour only.

Per-SoC table (R: Linux DTS `arch/arm64/boot/dts/apple/*`; cell offset + 6 bytes):

| SoC | SPMI controller (DT reg) | PMIC (compatible, USID) | rtc_offset |
|---|---|---|---|
| t8103 M1 (J313) | `spmi@23d0d9300` | `apple,sera-pmic`, 0xf | 0xd100 |
| t8112 M2 | `spmi@23d714000` | `apple,stowe-pmic`, 0xe | 0xf900 |
| t600x M1 Pro/Max/Ultra (die0) | `spmi@2920a1300` | `apple,maverick-pmic`, 0xf | 0x1411 |
| t602x M2 Pro/Max/Ultra (die0) | `spmi@29e114000` | `apple,maverick-pmic`, 0xb | 0x1411 |
| M4 Pro (next SoC, owner plan) | U | U | U: needs an Asahi DT or an ADT/SPMI census |

On M4 Pro the CLKM key and the 32768 Hz / 48-bit format are also unverified (U). The table is data: the logic holds no
SoC constants (owner portability rule).

## 3. Which layer reads it

- Mu has neither an SMC transport (RTKit mailbox to the SMC ASC) nor an SPMI driver. m1n1 has both building
  blocks, and its SPDX lines are MIT:
  - `src/smc.c` is an SMC RTKit client (endpoint 0x20). It has `smc_init`, `smc_write_u32` and `smc_shutdown`;
    `SMC_READ_KEY 0x10` is defined but has no reader yet.
  - Precedent (R): `src/dcp.c:782-786` already opens the SMC, writes keys and shuts it down before the guest.
  - The SPMI protocol is known from our MIT proxyclient `m1n1/hw/spmi.py`: STATUS/CMD/REPLY registers,
    `CMD_EXT_READL`.
- Proposal:
  - m1n1 (the owner of pre-guest initialisation) reads CLKM and rtc_offset once, just before chainloading Mu.
  - It publishes an anchor in ADT `/chosen`: `m1n1-rtc-utc-ms` (u64), `m1n1-rtc-cntpct` (u64) and
    `m1n1-rtc-cntfrq` (u64). It samples CNTPCT right after the SMC read and logs `RTC: anchor ...` or
    `RTC: unavailable <why>`.
  - This is the same shape as the existing host anchor `m1n1-iomfb-utc-{ms,cntpct,cntfrq}`, which
    `proxyclient/tools/chainload.py:108-114` writes from the host's UTC on every proxy chainload and `src/dcp.c:176-190`
    consumes.
- Mu reads the anchor at DXE init (AppleDTLib) and copies it into runtime data.
  - `GetTime` = anchor_ms + (CNTPCT - anchor_cnt) / freq, converted to `EFI_TIME` (`rtc_wallclock.h`).
  - No SMC or SPMI access happens at runtime.
  - So the transport question after ExitBootServices does not arise. Windows' AppleSmc driver owns the SMC RTKit
    endpoint after boot, and Mu never touches it.
  - The answer stays valid for every `GetTime` call Windows makes, not only the first.
- Source priority, logged once at boot in the Mu log:
  1. Hardware anchor (`m1n1-rtc-*`). Standalone and bench boots alike.
  2. Host anchor (`m1n1-iomfb-utc-*`). Bench proxy chainloads only; it is the host's NTP time and corrects the
     ~20 min RTC error. As a stage 0 it needs no new hardware access at all and could ship first.
     Open question Q1: which source wins when both are present.
  3. Virtual clock (today's behaviour), with `RTC: no wall-clock anchor, virtual clock` in the boot log.
- Fail-closed rules for m1n1:
  - SMC read error or a short reply, an SPMI timeout, or a missing ADT node or table row → no anchor property,
    plus the log line.
  - A year outside 2020..2100 → no anchor (plausibility; the format span is larger).
  - `smc_shutdown` always runs.

## 4. Stage 2 (owner's word needed; one paragraph by order)

`SetTime` would compute `off = (t << 15) - CLKM` and write 6 bytes to the PMU cell. That cell is persistent machine
clock state shared with macOS and iBoot. It changes macOS's time on its next boot. It needs an SPMI write path from
the runtime (Mu runtime SPMI, or a deferred write by m1n1 at the next chainload). A fault there could corrupt
neighbouring PMU cells (`pm_setting`, `boot_stage`, `panic_count` sit near it). Not designed further until the owner
rules.

## 5. Regression test (done)

- `tools/rtc_wallclock/rtc_wallclock.h` (MIT, freestanding) holds the reference conversion.
  `tools/rtc_wallclock/rtc_wallclock_test.c` tests it, and `tests/test_rtc_wallclock.py` compiles and runs the test
  under UBSan with `-Werror`.
- Cases:
  - the epoch;
  - 2000-02-29 (leap) and 2100-03-01 (no 2100-02-29);
  - 2024-02-29 with milliseconds;
  - 1900-01-01 (the EFI lower bound), with 1899 and 10000 refused;
  - floor division before 1970;
  - the 48-bit counter wrap between two reads (offset set 5 s before the wrap, read 10 s later);
  - negative sums (bit 47);
  - 100000 seeded writer/reader round trips (1900..2106);
  - the documented wrap after 2106;
  - the CNTPCT anchor across a 64-bit wrap.
- Result: `RTC_WALLCLOCK_TEST PASS fails=0`.

## 6. Hardware checkpoint (proposal, stage 1 or stage 0)

- One standing boot. Exclude the network effect: record the time before W32Time syncs.
- Pass:
  - Kernel-General ID 12 (OS start) agrees with the host stamp of the same moment within 2 s, taking the boot
    duration from the launcher log.
  - No Kernel-General ID 1 correction larger than 2 s at the first sync.
- With the hardware anchor alone, expect about −20 min (the RTC error of §1). The check then confirms the reader,
  and the residual is the stored offset.
- Recovery: the standing Mu image. The change is firmware-only and read-only.

## 7. Read-only evidence now (bench)

`tools/rtc_wallclock/clockcheck.ps1` reads:
- the guest's current UTC;
- Kernel-General ID 1 (time changes: old/new/reason), ID 12 (start) and ID 13 (shutdown);
- `ShutdownTime` (FILETIME) and LastBootUpTime;
- `w32tm /query /status` and `/query /source`.

It writes nothing. Mechanism confirmed if this boot's start ≈ the previous ShutdownTime + boot duration while the host
shows the real downtime.

## 8. Open questions

- Q1: source priority when the host anchor and the hardware anchor are both present (proposal: host first on bench
  chainloads, hardware otherwise).
- Q2: is the macOS ~20 min error in the stored offset, or does macOS simply not sync? Read-only check in the next
  macOS session: `date -u` vs the host, and `sntp` without `-S`.
- Q3: does Windows' AppleSmc driver read or write CLKM or touch the RTC? If it ever sets the time, that is an
  unreviewed stage-2 path.
- Q4: M4 Pro table row (PMIC USID, offset, key) — needs a source.
- Q5: does Windows call `GetTime` after boot (resume, periodic)? It doesn't matter for the design, since runtime
  `GetTime` is self-contained.

PORTABILITY: per-SoC table only (ADT path or DT reg, PMIC USID, cell offset). The conversion and the anchor are
SoC-independent. KEEP/DROP: to be decided on the §6 checkpoint.

## 9. Corrections and stage-1 findings (2026-11-27, after the Architect rulings)

- Correction to §1 (R): `LibRtcInitialize` sets `BaseTime` to 2019-01. Windows therefore sees 2019-01-01 + uptime
  (day modulo 28), not year 0. The conclusion is unchanged: there is no wall time.
- Stage 0 is implemented as a separate library instance, `tools/rtc_wallclock/mu/AnchorRealTimeClockLib`.
  - It is selected in `AppleSiliconPkg.dsc.inc` only when `J313_RTC_ANCHOR=TRUE` (platform default FALSE), so the base
    build is untouched when the flag is off.
  - It reads both anchors at init. The host anchor wins. It logs `RTC: wall-clock source=...` and, when both anchors
    exist, `RTC: hardware minus host = <ms>`.
  - `repro-rtc0.sh` defines gates G1-G4: base = 170a91b4 bit for bit; only `RealTimeClockRuntimeDxe.efi` differs;
    every ACPI table is identical; the AGX IRQ AML check passes.
- Bench host anchor (R): the bench launcher runs the integration tree's `proxyclient/tools/chainload.py:111-114`, which
  writes `m1n1-iomfb-utc-{ms,cntpct,cntfrq}`. `HV.init` builds the guest ADT from `u.get_adt()` (hv/__init__.py:1736).
  `setup_adt` deletes nodes but not `/chosen` props, so Mu should see the host anchor (I; the Mu log line confirms it).
- Stage-1 findings that change the design (R):
  1. Standing m1n1 (WIFI_PCIE_LINK) already opens the SMC in `pcie_wlan_rail_on` (`pcie_wlan.c:119-120`) and keeps the
     session for m1n1's lifetime. The file comment says: "quiescing the SMC IOP from the AP reset the SoC (EXP2410)".
     `smc_shutdown()` = `rtkit_quiesce`, so it must NOT run on J313. The `dcp.c` open/shutdown path never runs here:
     the J313 ADT has no `function-hdmi_pwr_en`. To leave the Windows AppleSmc start state unchanged, the RTC reader must
     reuse the one open session (one extra READ_KEY) and never shut it down.
  2. m1n1's C `adt_setprop` can only replace an existing property (`rust/src/adt.rs:706`, `named_prop_mut`). A
     `/chosen` anchor written by m1n1 therefore needs zero placeholders that the loader creates first (opt-in, so
     standing launches stay byte-identical).
  3. In the bench flow, the guest ADT is fetched in `HV.init` before the wireless-profile `pcie_init` opens the SMC
     (full.log order). A value filled at rail time arrives too late, unless the launcher refreshes the three props
     after `pcie_init`.
  4. The ADT describes the RTC itself (R, J313 ADT): `/arm-io/nub-spmi/spmi-pmu` has `reg` slave 0x0f,
     `info-clock_offset = [0xD100, 6]` (= `info-rtc_scrpad`, = the Asahi DT cell) and `info-rtc = [0xD002]` (a PMU RTC
     register, meaning U). The offset location can come from the ADT instead of a per-SoC table.
- Q3 answered (power line, 2026-11-27, R from source at 8e2e0ba2 = 0.1.0.23): AppleSmc never reads or writes CLKM,
  any RTC key or rtc_offset, and never sets the time. Its write policy is `smc_policy_read_only` with an empty
  allow-list (common/smc_client.c:113, smc_proto.c:11). Its user key reads admit only the key classes T/P/V/I/B
  (smc_keys.c:23), so CLKM (class C) is refused. There is no stage-2-like path in AppleSmc.
- Coordination: TZ1P01 edits neither .dsc file (only AcpiTables DSDT*.asl and J313AppleSmcThermal.asl.inc). The two
  images merge cleanly.
- Guest timezone (R, bench read-only query on the EXP3016 guest, 2026-11-27T15:09:46Z, evidence
  .local/experiments/EXP3016-c4/evidence/tzquery.txt): `RealTimeIsUniversal` is absent; TimeZoneKeyName "Romance
  Standard Time"; Bias = ActiveTimeBias = 0xffffffc4 (-60 min, i.e. UTC+1 now, no DST active). The UEFI side is clear
  (Mu returns TimeZone = 0 = UTC); whether Windows honours it is undocumented (U). So the stage-0 kit's likely
  outcomes are O1 (UTC honoured) or O2 (local assumed: boot time = host - 60 min). O2 would make RealTimeIsUniversal=1
  an installer requirement (owner decision; no registry write on the bench guest).
