# CPU frequency and identity on the J313 Windows path -- plan (2026-11-07)

User requirement: CPU at macOS-equivalent frequencies; Task Manager shows the correct CPU name and speeds.

## Observed contract (sources verified)
- run_uefi.py:255 p.cpufreq_init() -> m1n1 src/cpufreq.c t8103_clusters: ECPU 0x210e00000 default P5 (2064 MHz = max),
  PCPU 0x211e00000 default **P7 (1956 MHz)**. WOM1_PCPU_PSTATE (run_uefi.py:259, tools/j313_cpufreq.py) can fix P1..12.
- Nothing scales the P-state at runtime: DSDT CPU nodes have no _CPC/_PSD (_LPI commented out), HV does not emulate the
  cluster DVFS registers, no PEP/processor driver.
- Measured: P-core L1 hit 1.54 ns Windows (=3 cycles @1956 MHz) vs 0.94 ns macOS (=3 cycles @3204 MHz turbo P15), EXP2240/2241.
- Asahi t8103.dtsi OPP: P 1..12 = 600..2988 MHz, turbo 13..15 = 3096/3144/3204 ("not available until CPU deep sleep").
- SMBIOS Type 4 (mu/Silicon/Apple/AppleSiliconPkg/Drivers/SmbiosInfoDxe/SmbiosInfoDxe.c:229-319): Version "Not Specified"
  (-> generic name in Task Manager, hypothesis: ARM64 ProcessorNameString comes from Type 4 Version), MaxSpeed 3228,
  CurrentSpeed 0, Core/Enabled/Thread count hard-coded 6 (M1 has 8), Family2 ARM instead of ARMv8, ProcessorId 0;
  Type 7 caches not cluster-accurate.

## Layer ownership and plan
1. SMBIOS identity (owner: Mu). Type 4 Version "Apple M1", MaxSpeed 3204, CurrentSpeed = boot P-state MHz, counts 8 from
   PcdCoreCount, Family2 ARMv8, ProcessorId from MIDR; correct Type 7 (P: 192K I / 128K D per core, 12 MB L2; E: 128K/64K,
   4 MB L2; SLC 8 MB). Checkpoint: Task Manager / registry ProcessorNameString + ~MHz. Low risk, firmware-only.
2. Runtime DVFS (owner: m1n1 HV + ACPI): CPPC. _CPC per CPU (Highest/Nominal/Lowest, Nominal/Lowest Frequency in MHz,
   Desired Performance register in an HV-emulated SystemMemory page) + _PSD per cluster (shared domain, SW_ANY/HW_ALL).
   HV maps desired perf -> cluster P-state (max over the cluster's vCPUs), writes CLUSTER_PSTATE (+0x20020, SET|PS),
   returns delivered perf. Windows inbox PPM then scales like macOS. Turbo 13-15 only after deep idle exists.
   Unknown: Windows ARM64 acceptance of SystemMemory _CPC without a PEP (verify against Microsoft PPM docs before code).
3. Interim measurement: fixed P12 (2988 MHz) via WOM1_PCPU_PSTATE on a GPU/CPU run, with preregistered thermal stop
   criteria (fanless Air), to quantify CPU-bound gains before CPPC lands.
Related memory-side fix: T8103 SLC was disabled on this path (EXP2242); EXP2243 tests enabling it in m1n1.

## Live Windows view (2026-11-07 ~22:00Z, normal recovery, read-only)
- Registry CentralProcessor\0: ProcessorNameString "Not Specified" (= SMBIOS Type 4 Version -> confirms the name source),
  Identifier "ARMv8 (64-bit) Family 8 Model 22 Revision 101", VendorIdentifier "Apple Inc.", **~MHz = 22**.
- Win32_Processor: Name "Not Specified", Max/CurrentClockSpeed 22, cores/logical 8/8, L2 65536 KB, L3 8192 KB.
- ~MHz 22 does not come from SMBIOS (MaxSpeed 3228). Hypothesis: Windows ARM64 calibrates the clock with the PMU cycle
  counter against the generic timer; the HV maps PMCCNTR_EL0 -> APL PMC0 (m1n1 src/hv_exc.c:1252, Python hv
  PMCCNTR handler) and PMC0 is not counting EL1 cycles at the expected rate (PMCR0/PMCR1 state at guest entry).
  Next: verify the Windows ~MHz source in Microsoft documentation, then read PMCR0/PMCR1/PMC0 deltas at guest entry.

## macOS MCC behaviour (kext RE, com.apple.driver.AppleT8101, macOS 15.7.8; evidence scratchpad dramcfg-re/)
- start(): mccEnableCacheMode -> SLC ways = mcc config-data[0].u32[0] (16 if absent; boot-arg mcc_clampwaycount clamps);
  broadcast write plane+0x1c00, poll +0x1c04 data/tag ways on 4 planes. No dynamic way changes on T8103.
- _mccSelectDynamicDRAMCFGMode(0|1): mode 1 writes mcc dramcfg-data[0] (0xdc4 then 0xdbc) to every enabled DCS channel;
  mode 0 restores the per-channel values read at boot (iBoot's). Only caller: AppleT8101PMGR::setPerfState(domain 5,
  pstate) -> FAST while pstate >= frequencyMHzToPerfState(5, pmgr mcx-fast-pcpu-frequency) (domain 5 = P-cluster, inferred).
  No GPU path. Not called at start.
- Consequence for us: DRAMCFG FAST belongs to the CPU P-state policy (m1n1/HV CPPC owner): apply dramcfg-data[0] whenever
  the P-cluster is at/above mcx-fast-pcpu-frequency, restore boot values below. Needs the live ADT values (mcc_status.py
  now prints dramcfg-data, config-data, pmgr mcx-*).

## Windows ARM64 processor driver = FxPPM, which parses ACPI _CPC (2026-11-07 ~23:25Z, guest binaries, read-only)
- Processor devices bind service FxPPM (C:\Windows\system32\drivers\fxppm.sys, sha256 7fd6d9f63394f944bf301ad6246503fdcc3cbe85346d7c4502548972acc77f06); no PEP present.
- fxppm.sys strings: "_CPC", "_CPC.ResourcePriorityRegisters[]", "_PSD" with "ACPI Software All/Any", "ACPI Hardware All",
  "_OSC", "_LPI", HighestPerformance, NominalPerformance, LowestNonlinearPerformance, LowestPerformance,
  GuaranteedPerformance, DesiredPerformance, Minimum/MaximumPerformance, EnergyPerformancePreference,
  PerformanceReductionTolerance, ReferencePerformance, NominalFrequency, LowestFrequency, PerformanceLimited, "~MHz".
  -> Windows ARM64 consumes firmware _CPC/_PSD directly through FxPPM; a PEP is NOT required (Microsoft docs do not
  say this explicitly; evidence = the inbox binary). Microsoft-documented alternative remains the PEP PPM interface.
- Design consequence (owner: Mu ACPI + m1n1 HV): per-CPU _CPC (rev 3) with Highest/Nominal/Lowest(Nonlinear)
  performance + Nominal/Lowest Frequency, Desired Performance register + Reference/Delivered feedback counters in a
  SystemMemory page emulated by the HV; _PSD per cluster; platform _OSC must grant CPPC (and CPPC2 if autonomous).
  HV maps desired perf -> cluster P-state (max over the cluster's vCPUs, P1..P12, turbo excluded) and applies the
  DRAMCFG rule (FAST >= 2184 MHz). Feedback: reference = constant-rate counter, delivered = reference x current/nominal.
