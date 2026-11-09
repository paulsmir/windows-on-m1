# TDR firmware restart: unknowns U4 and U8 settled from sources

2026-11-09T20:11Z, offline. No hardware, no driver or m1n1 edits. Answers U4 and U8 of
`investigation/analysis/TDR-FIRMWARE-RESTART-RECOVERY.md` (7bedfb1fa), re-pins its `M/` citations.

Path prefixes: `B/` = `/Volumes/pwdev/public_windows/.local/experiments/m1n1-ansdirect-bugcheck4/src/m1n1/src/`
(bench m1n1 source snapshot), `BS/` = `.../m1n1-ansdirect-bugcheck4/src/drivers/apple-agx/shared/`,
`S/` = `drivers/apple-agx/shared/`, `K/` = `drivers/apple-agx/render-admission/`,
`A/` = `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/`,
`KC/` = `.local/reference/macos-kernelcache-j313-15.7.8/extracted/`. macOS addresses are kernelcache
virtual addresses (15.7.8, J313); `agx.ann` = `KC/kcdis/agx.ann`.

Grades: **[SRC]** source read at the cited line; **[DIS]** read from disassembly at the cited address;
**[SYM]** inferred from symbol names / strings / vtable slot names; **[HYP]** hypothesis.

## A. U4: the bench m1n1 broker

### A.1 Which m1n1 the bench boots

| Item | Value | Evidence |
|---|---|---|
| Image | `.local/experiments/m1n1-ansdirect-bugcheck4/m1n1-g2-abi6-iomfb-owner-ansdirect-bugcheck4.macho` | `.local/experiments/EXP2301-f0-dwm/full-owner2301.sh:11` (chainload line); same path in `EXP2301-f0-dwm/hardware-manifest.json` "Artifacts" |
| SHA-256 | `f01f5961bb4a6f5c18f4396f522352ff47399f0dd97a17437d5d50048e3e799d` | hardware-manifest.json; recomputed with `shasum -a 256` 2026-11-09 |
| Source commit | `83a9cf74` ("hv: OV5 beacon at broker offset 0xF00"), branch `exp/m1n1-ans-direct-slc`, git worktree `.local/m1n1-wt-ans-direct-slc` (HEAD 83a9cf745eee) | `m1n1-ansdirect-bugcheck4/m1n1-build-info.json` (`m1n1_commit`, `macho_sha256`) |
| Source tree read | snapshot `m1n1-ansdirect-bugcheck4/src/m1n1` | 16 AGX/RTKit/pmgr files hash-equal to `git show 83a9cf74:src/<file>` (retained_platform, retained_root, gpuva_v5, power_broker, power_platform, power_mmio, autonomous_runtime, rtkit, asc, pmgr, hv_psci, dcp, nvme, g2.generated.h, gpuva_v5_mmio, retained_mmio) |
| Build flags | `AGX_LOCAL_RESERVE_V2=1 IOMFB_FULL_OWNER=1 AGX_BROKER_LOCK_SPLIT=1`; `AGX_GPUVA_MAP_NO_TLBI` and `AGX_GUEST_VMID1` not set | build-info `make_flags`; no occurrence in `src/build.log` |
| Ledger | EXP2300 ACTUAL "Ordered restart ... on m1n1 ansdirect-bugcheck4" | `investigation/EXPERIMENTS.md:75431` |

Gaps and contradictions:
- The EXP2300/EXP2301 BEFORE "Hashes" lines say `m1n1 0d3a75b3` (`EXPERIMENTS.md:75426`, `:75456`). The launch
  script and manifest name the bugcheck4 image; the ledger field is a stale template value. The manifest is
  the evidence used here.
- MEMORY "Working GPU profile" (brokerlock-cppc3, 0467d900, sha e871092f) is the pre-ANS line. Its
  `hv_agx_retained_root.c` and `hv_agx_power_broker.c` are hash-equal to the bench; `retained_platform.c`,
  `gpuva_v5.c`, `power_mmio.c` differ. Not analysed further: the bench boots bugcheck4.
- Worktree checkout `m1n1_windows` 0d3a75b3 versus bench: `retained_root.c`, `power_broker.c`,
  `power_platform.c`, `asc.c`, `pmgr.c`, `hv_psci.c`, `dcp.c`, `hv_autonomous_runtime.c`, `g2.generated.h`
  identical; `retained_platform.c` (565 -> 607 lines), `gpuva_v5.c` (927 -> 937), `power_mmio.c`
  (776 -> 1081), `rtkit.c` (985 -> 1136), `nvme.c` (613 -> 915) differ. The broker state-machine logic
  (CLOSE/PREPARE/ACTIVATE conditions, job/lease functions, power gate) is textually the same; the
  differences are the lock split, the VMID-0 TLBI wrapper, the launch-memory check and two `#ifdef` blocks
  that are compiled out.

### A.2 Job / lease state machine (GPUVA v5)

State per slot 1..63: `{token, owner, jobs, occupied}` (`B/hv_agx_gpuva_v5.c:761-762`). The broker has no
notion of the firmware: it never reads a ring, a stamp or an ASC register in any v5 op.

| Op | Accepts when | Effect | Refuses | Source |
|---|---|---|---|---|
| LEASE(process, generation, slot) | process live, slot 1..63 free, ttbat slot reads zero, slot 63 only if legacy context63 inactive | writes ttbat slot = `root_pa \| 1 \| slot<<48`, TLBI ASID, reads back, new token | BUSY occupied; OWNERSHIP if ttbat slot not zero; TLB/TAINTED on rollback | `B/hv_agx_gpuva_v5.c:738-769` |
| JOB_BEGIN(slot, token) | slot occupied and token matches | `++jobs` | STALE; CAPACITY at UINT32_MAX | `:771-782` |
| JOB_END(slot, token) | slot occupied, token matches, `jobs != 0` | `--jobs`. **No other condition.** | STALE | `:784-794` |
| RELEASE(slot, token) | slot occupied, token matches, `jobs == 0` | ttbat slot = 0, TLBI ASID, slot record cleared | STALE; BUSY while `jobs != 0`; TAINTED if the write/TLBI fails | `:796-812` |

- Who may end or release: any caller presenting `(slot, token)` under the current epoch. Process id and
  generation are not checked for JOB_BEGIN/JOB_END/RELEASE (`B/hv_agx_retained_platform.c:249-257`).
- A job begun and never ended: `jobs` stays non-zero with no timeout and no forced end. While it does:
  RELEASE of that slot BUSY (`gpuva_v5.c:804`); every UPDATE_PARENT/UPDATE_LEAF of that owner BUSY
  (`publish_entry`, `:305-307`); FLUSH_TLB of that owner BUSY (`:831-833`); RELOCATE_ROOT BUSY (`:730-732`);
  DESTROY BUSY (`:856-858`); RR CLOSE refused (`retained_platform.c:185-193,443-444`).
- Envelope of every v5 op: broker power state ON, else STALE (`retained_platform.c:199,203`,
  `power_mmio.c:908-911`); `check()` passes (`gpuva_v5.c:170-199`: ttbat slot 0 equals the value captured at
  init, firmware TTBR1 prefix unchanged, every occupied slot reads back its expected value), else TAINTED,
  latched; epoch equal, else STALE (`:211`). The v5 window is **not handled at all** unless
  `gpuva_v5.active && root_owner.Active` (`retained_platform.c:523`; an unhandled hook returns false to the
  stage-2 abort path, `B/hv_vm.c:1154-1155,1193-1194`).
- The ASC CPU state is not an input to any v5 op: `cpu_stopped()` (`retained_platform.c:341-344`) is used
  only by RR PREPARE (`:362`), ACTIVATE (`:367`) and CLOSE (`:444`).

KMD side (for the call order): a graph holds at most one broker job; a depth-two second job is KMD-local
(`JobNested`, `S/src/apple_agx_gpuva_g3_graph.c:1139-1144,1150-1153`). `AppleAgxGpuvaG3GraphEndJob` sends
JOB_END and, for a non-retained graph, RELEASE (`:1146-1177`); a retained graph keeps the lease until
`AppleAgxGpuvaG3GraphReleaseIdle` (`:154-175`, refuses while `JobInFlight`, `JobNested` or `Uncertain`). The
B1 path has its own client (`K/src/gpuva_b1_windows.c:98-139`).

### A.3 What is refused while a process or slot is alive

| Request | Refused when | Source |
|---|---|---|
| RR CLOSE | CPU running, or any live process record, or any occupied slot 1..63, or epoch mismatch | `B/hv_agx_retained_platform.c:442-444,185-193` |
| RR CLOSE (root layer) | root Tainted (`TAINTED`) | `B/hv_agx_retained_root.c:682-683` |
| RR PREPARE | CPU running, root already Prepared/Active/Tainted, hwdata profile mismatch | `retained_platform.c:360-362`; `retained_root.c:246` |
| RR ACTIVATE | CPU stopped, epoch mismatch, root not Prepared or already Active (`STATE`) | `retained_platform.c:366-369`; `retained_root.c:280` |
| Power OFF | root Prepared or Active (result BUSY) | `B/hv_agx_power_mmio.c:940-945`; `retained_platform.c:604-607` |
| Power ON/OFF after a failed transition | state FAILED is neither OFF nor ON: both commands return TRANSITION_FAILED for good | `B/hv_agx_power_broker.c:41-43,66-67,47,54,71,77` |
| DESTROY (process) | any slot of the owner occupied, any backing or local grant left, any non-empty table | `gpuva_v5.c:856-864` |
| Anything, after taint | v5 taint has no clearing op: DESTROY needs `check()` OK, so processes cannot be removed, so CLOSE stays refused; root taint blocks PREPARE and CLOSE | `gpuva_v5.c:174,852-853`; `retained_root.c:246,682` |

A restart with DWM alive therefore cannot use CLOSE or power OFF (design 3.4 conclusion confirmed on the
bench source).

### A.4 Which ops touch what

| Resource | Touched by | Source |
|---|---|---|
| ASC CPU_CONTROL (0x44 bit 4) | read only, as a gate (PREPARE/ACTIVATE/CLOSE). No m1n1 HV path writes it; the ASC MMIO is passthrough (`map_arm_io_ranges`) | `retained_platform.c:341-344`; `B/hv_autonomous_runtime.c:182-202,328` |
| SGX / GFX-ASC power domains | power window ON: `pmgr_adt_power_enable("/arm-io/gfx-asc")` then `"/arm-io/sgx"`; OFF: sgx then gfx-asc | `B/hv_agx_power_broker.c:37-83`; `B/hv_agx_power_platform.c:16-38` |
| pmgr reset pulse | exists (`pmgr_adt_reset`, `pmgr_reset`: DEV_DISABLE + RESET for 10 us on an active device) but is not reachable from the guest and is not used for gfx-asc/sgx (callers: dcp, nvme, proxy) | `B/pmgr.c:275-325,327`; `B/dcp.c:958,1066`; `B/nvme.c:390,515-516,611-612,645-646`; `B/proxy.c:445` |
| ttbat (slot table at `HV_AGX_G2_GPU_BASE` 0x9fffb8000) | slot 0: ACTIVATE writes TTBR0/TTBR1 (`:371-372`), CLOSE zeroes TTBR0 (`:447`); slots 1..63: LEASE/RELEASE (`:137-145`). Guest writes are trapped: only slot 63, and not while slot 63 is leased (`:566-581`, `BS/include/apple_agx_retained_root_abi.h:32-36`) | `retained_platform.c` |
| UAT tables | kernel TTBR0 root and its children: RR MAP/UNMAP/ACTIVATE/CLOSE (`retained_root.c`); per-process tables: UPDATE_PARENT/UPDATE_LEAF store through `publish_entry` (`gpuva_v5.c:300-333`) | |
| TLB invalidation | `TLBI ASIDE1OS` executed on the calling host CPU: ASID 0 for RR sync (`retained_platform.c:108-117`), ASID = slot for LEASE/RELEASE/publish/FLUSH_TLB (`:152-159`). No dependency on the ASC CPU or on SGX power in the code | `retained_platform.c:83-106` |
| DART | not used for the GPU; scanout only | `B/hv_agx_power_mmio.c:599-611` |
| Handoff page (0x9fff70000) | not hooked (plain guest-visible memory) | `B/hv_agx_g2.generated.h:14-15`; only `GPU_BASE` is hooked, `power_mmio.c:993` |

Whether a broadcast TLBI reaches the SGX TLB while the ASC CPU is stopped, and whether SGX keeps stale TLB
entries across a stop, is hardware behaviour: [HYP], not answerable from source.

### A.5 Locks, epochs and other invariants a new op must keep

| Invariant | Source |
|---|---|
| RR-class ops hold `broker_lock` then `v5_lock`; v5 ops hold `v5_lock` only; power holds `broker_lock` only. Order never reversed | `power_mmio.c:40-72,900-903,908-912,940` |
| The v5 class reads `broker.state` without `broker_lock`, relying on "OFF is refused while the root is Prepared/Active", so the state it sees while working is always ON. An op that power-cycles with the root Active must hold `v5_lock` for the whole cycle and leave the state ON (or FAILED, which makes every v5/RR op fail closed as STALE/STATE) | `power_mmio.c:59-65`; `retained_platform.c:203,358` |
| Epoch: PREPARE takes `++next_epoch`; ACTIVATE re-initialises GPUVA v5 with it (`memset`), which makes every KMD-held process/table/backing record STALE. Keeping the epoch means: no PREPARE, no v5 init | `retained_platform.c:363-364,383-398`; `gpuva_v5.c:401-408` |
| A second ACTIVATE on an Active root returns STATE and does nothing; a second PREPARE returns STATE | `retained_root.c:280-281,246-247` |
| `hv_agx_gpuva_v5_init` on an already active broker returns INVALID and the caller then sets `gpuva_v5.active = false` | `gpuva_v5.c:394-400`; `retained_platform.c:387-396` |
| Slot 0 and the firmware TTBR1 prefix (L0 entries 0 and 1) must read back unchanged at every op, else permanent taint | `gpuva_v5.c:180-185`; `retained_root.c:74-85`; `retained_platform.c:458-463` |
| Tokens are monotonic (`++next_token`), so a stale token can never match a new lease | `gpuva_v5.c:761-763` |
| Process records, tables, backings, local grants, leaf reference counts, the mailbox page are independent of slots and of the CPU; nothing in them refers to firmware state | `gpuva_v5.c:434-455,469-532,873-902` |

### A.6 Re-pin of the design's `M/` citations

"same" = the cited text is at the same lines in the bench source (file hash-identical unless noted).

| Design citation (0d3a75b3) | Bench line (83a9cf74) | Status |
|---|---|---|
| `hv_agx_retained_platform.c:149-157` (gpuva_idle) | `:185-193` | same text, shifted |
| `:259-261` (default -> INVALID, "no QUERY_CAPS/NEGOTIATE_LIVE case") | `:295-297` | same text. **The bench has no such case either**; the envelope also rejects any command above ATTACH_MAILBOX (`gpuva_v5.c:212-214`). The design's inference "so this checkout is older than the bench" does not follow from this line; the bench is newer for other reasons (A.1) |
| `:305-308` (cpu_stopped) | `:341-344` | same text |
| `:323-364` (PREPARE/ACTIVATE) | `:360-401` | same logic; `sync_tables` now goes through `agx_tlbi_aside1os_host_vmid` (`:83-117`), compiled to the same instruction sequence without `AGX_GUEST_VMID1` |
| `:404-418` (CLOSE) | `:442-455` | same text |
| `:562-565` (can_power_off) | `:604-607` | same text |
| `hv_agx_power_broker.c:45-59`, `:40-84` | same | identical file |
| `hv_agx_power_platform.c:16-38` | same | identical file |
| `hv_agx_power_mmio.c:657-661` (OFF refused -> BUSY) | `:940-945` | same logic; the lock is now taken by `broker_lock_enter(..., BROKER_CLASS_POWER)` |
| `hv_agx_power_mmio.c:326-337` (scanout DART map/unmap) | `:599-611` | same text |
| `hv_agx_power_mmio.c:638` (scanout window) | `:922-923` | same text |
| `hv_agx_retained_root.c:246,682` | same | identical file |
| `hv_agx_gpuva_v5.c:211` (epoch -> STALE) | `:211` | same line; file differs only at `:310-316,320-322` (compiled-out `AGX_GPUVA_MAP_NO_TLBI`) |
| `rtkit.c:80-84` (power state enum) | `:81-85` | same values |
| `rtkit.c:919-984` (switch_power_state / sleep) | `rtkit_switch_power_state :979`, `rtkit_quiesce :1032-1035`, `rtkit_sleep :1037-1045` | same sequence (target state, then `asc_cpu_stop`); the bench adds `rtkit_sleep_bounded :1091-1127` |
| `asc.c:10-11,73-81` | same | identical file |
| `hv_autonomous_runtime.c:182-202,328` | same | identical file |
| `hv_psci.c:1603-1609` | same | identical file; additionally the bench prints the bugcheck data, the raw AGX ASC outbox control and the OV5 beacon at the reset trap (`B/hv_exc.c:1741-1776`) |
| `pmgr.c:327`, `dcp.c:958` | same | identical files |
| `nvme.c:418` | `:515-516` (also `:390,611-612,645-646`) | file differs (ANS line); same use of `pmgr_reset` |
| `hv_agx_g2.generated.h:46-55` | same | identical file |
| `S/include/apple_agx_gpuva_broker_v5.h:34-61` | same | the bench build's copy `BS/include/apple_agx_gpuva_broker_v5.h` is byte-identical to the worktree header |

No statement of design sections 0.3, 3.3, 3.4 changes meaning after the re-pin, except the `:259-261` remark.

### A.7 Verdict for the design

**Warm path (stages 2-3): yes, existing broker ops suffice.** JOB_END is pure bookkeeping; the broker cannot
tell an ended job from a never-ended one.

Order, inside ResetFromTimeout, after the stop proof (T2/T3) and before the DDI returns:

1. Per graph with a broker job: JOB_END(slot, token) exactly once. KMD: `AppleAgxGpuvaG3GraphEndJob`, called
   twice when `JobNested` is set (the first call only clears the KMD-local nesting, `graph.c:1150-1153`).
2. Per held lease: RELEASE(slot, token). KMD: implicit in step 1 for non-retained graphs, else
   `AppleAgxGpuvaG3GraphReleaseIdle`.
3. Nothing else. No DESTROY, CLOSE, PREPARE, ACTIVATE or power op; the epoch and all process records stay.

Constraints the source imposes on that order:
- It must run with the broker power state ON and the root Active (A.2 envelope). The ASC CPU may be stopped.
- It must follow the stop proof: RELEASE zeroes the ttbat slot and invalidates the ASID, which removes the
  translation of a job the GPU might still be executing.
- It must precede the VidMm purge window (W5): until JOB_END, every page-table update of that owner returns
  BUSY (`gpuva_v5.c:305-307`).
- KMD preconditions: `graph->Uncertain` must be clear (`graph.c:1148-1149,159`); a graph marked Uncertain
  cannot be ended through the existing client functions.
- RestartFromTimeout on the warm path must skip section 3.2 step 4 (PREPARE/ACTIVATE): both return STATE on
  an Active root. Slot 0 is therefore already valid when the CPU starts, unlike the first boot, where
  ACTIVATE requires a running CPU and writes slot 0 after the start (`retained_platform.c:367,371-372`).
  Whether the firmware tolerates that order is [HYP]; it is part of what H1 tests.

**Cold path (stage 4): what must be added.** Only the power cycle; lease cleanup needs no new op.

| Needed | Why | Source |
|---|---|---|
| A broker op that cycles sgx/gfx-asc with the root Active | power OFF is refused while Prepared/Active | `power_mmio.c:940-945` |
| Preconditions: CPU stopped; no occupied slot 1..63 (the KMD has released them with existing ops); root Active, not Tainted; epoch match | keeps `check()` consistent across the cycle; no forced slot clearing inside the op | `retained_platform.c:341-344`; `gpuva_v5.c:186-197` |
| RR-class locking (`broker_lock` + `v5_lock`) held across the whole cycle; final state ON, or FAILED (fail closed) | v5 reads `broker.state` under `v5_lock` only | `power_mmio.c:59-65,908-912` |
| The root stays Prepared+Active; the op must not go through ACTIVATE or `hv_agx_gpuva_v5_init` | ACTIVATE needs an empty retained root (`retained_root.c:285-287`), allocates a new system page and re-inits v5; v5 window is unhandled while not Active; v5 init on an active broker deactivates it | `retained_root.c:272-323`; `retained_platform.c:387-396,523` |
| A way out of power FAILED (only through a complete successful cycle) | FAILED is terminal today | `power_broker.c:41-43,66-67` |
| Post-condition check: slot 0 and the TTBR1 prefix unchanged after power-up | else permanent taint at the next op | `gpuva_v5.c:180-185` |

Design 4.4 asks the op to "unbind slots 1..63, drop leases and job state" and to put the root "back to
prepared, not active so ACTIVATE runs after the next CPU start without re-initialising GPUVA". Neither is
needed and the second is incompatible with the bench source (table rows 4 and A.5). An existing, unexposed
alternative to a full power cycle is the pmgr reset pulse (`pmgr_adt_reset`, A.4); which of the two leaves
the image bootable is U3 and stays a hardware question.

## B. U8: macOS firmware stop / boot (one pass)

Tooling: the annotated AGX kext disassembly already in `KC/kcdis/agx.ann`; the firmware kext
(`com.apple.AGXFirmwareKextG13GRTBuddy`, text 0xfffffe000907c000, 0x27a8 bytes) and `com.apple.driver.RTBuddy`
disassembled in the same way with the scripts in `KC/kcscripts` and the installed llvm-objdump (session
scratch, not kept); vtable slots resolved by decoding the kernel-cache chained fixups
(target = 0xfffffe0007004000 + low 30 bits; the decoded PAC diversities match the call sites, e.g. 0x7e52,
0xd2f3). Kernel callees resolved through the `com.apple.kernel` symbol table.

### B.1 Findings

| # | Statement | Grade | Coordinate |
|---|---|---|---|
| M1 | `AGXAccelerator::restart()` is a work-queue recovery entry, not a firmware reboot: it try-locks, runs a command-gate block that runs `AGXAccelerator::restartWorkQueue(AGXWorkQueue*)` on a second gate; if the try-lock fails it only re-arms `startHardwareProgressTimer` | DIS | 0xfffffe0009084d34 (`IOLockTryLock` 0x...879cd98, `runActionBlock` 0x...8f2829c), block 0x...9084e74, timer path 0x...9084dfc |
| M2 | `restartWorkQueue` first calls `fw->stopFirmwareForRecoveryInspection(restartInfo)`; a false return leaves the function | DIS | 0x...9084f98 (vtable slot 0x1a8 of `AGXArmFirmware`), `cbz` 0x...9084f9c |
| M3 | `stopFirmwareForRecoveryInspection` is a **cooperative halt request**: it sends an asynchronous note `0x85<<48 \| (recovery counter & 44 bits)` to the firmware through the firmware kext (`AGXFirmwareKextRTBuddy::sendAsyncNoteToFirmware`, slot 0x8a8), then polls every 1 ms (`IOSleep(1)` + `drainFirmwareRings(false)`) until `getNumDeviceRecoveries()` equals counter+1, for `accel[0x284] * 1000` iterations | DIS | 0x...90e7c0c; note built at 0x...90e7c74-7c78; loop 0x...90e7cc8-7d4c and 0x...90e7d58-7da8 |
| M4 | If the firmware does not acknowledge in time, macOS **panics**: "Failed to stop GFX FW for GPU recovery" (`agxk_fwc_arm.cpp:4363`) | DIS | 0x...90e7dac-7dc4 (`_panic` 0x...902e790; string 0x...711bf4d) |
| M5 | 0x85 is the message type Asahi names `MSG_HALT` and never sends | SRC | `A/gpu.rs:105` (commented out) |
| M6 | The acknowledgement and the rest of the handshake live in a firmware-shared block (`fw[0x868]`): +0x4a00 u64 = number of device recoveries; +0x4a10 u32 = phase; +0x4a20 u32 != 0 = "stopped for host recovery" | DIS | `getNumDeviceRecoveries` 0x...90e7aa4; `isFirmwareStoppedForHostRecovery` 0x...90e7a88; phase readers/writers in M7 |
| M7 | Phase protocol: firmware sets 1 ("stopped for pre-recovery"); host writes 2 (`resumeFirmwarePreRecovery`, panics unless the phase is 1); firmware sets 3 ("stopped for post-recovery"); host writes 0 (`resumeFirmwarePostRecovery`, panics unless 3). `restartWorkQueue` does exactly this: resume-pre at 0x...908827c, poll for 3 with `IODelay(1)` at 0x...90882c0-8300, queue work, resume-post at 0x...90885c8, then `AGXRestartReport::finalizeAndSendReports` | DIS | 0x...90de46c (==1), 0x...90de54c (write 2), 0x...90de454 (==3), 0x...90de4b8 (write 0) |
| M8 | The three fields sit at the offsets of Asahi's `halt_count` (+0x00), `halted` (+0x10), `resume` (+0x20). Asahi's recovery writes `halted = 0`, `resume = 1` in one step; macOS drives `halted` through 1 -> 2 -> 3 -> 0 and, in the functions read, only reads the +0x20 field | SYM (layout match) + DIS (macOS side) | `A/fw/initdata.rs:38-44`; `A/gpu.rs:1083-1086` |
| M9 | `AGXArmFirmware::stopFirmware()` touches no hardware: recursive lock, clear the byte `this[0x1630]`, unlock | DIS | 0x...90e7e70 (`IORecursiveLockLock` 0x...8ebad68, `IORecursiveLockUnlock` 0x...8ebae68) |
| M10 | `AGXArmFirmware::bootFirmware()` calls `AGXFirmwareKextRTBuddy::bootFirmware(IOMemoryMap*)` (slot 0x888), then waits for an "alive" flag (`this[0x1631]`) in 50 ms command-gate sleeps up to max(50 ms, timeout s x 1000); on timeout it returns false, or panics "Timeout while waiting for the GPU firmware to come up" when the override at `accel[0xf458]` is set | DIS | 0x...90e7eb4; call 0x...90e7f0c; wait loop 0x...90e7f4c-7fd0; panic 0x...90e8078-80b0 |
| M11 | The firmware kext's `bootFirmware` checks `RTBuddyService::iBootLoaded()`, publishes build/version, calls `RTBuddyService::loadFirmware(...)` and registers matching notifications for the GFX endpoints. No register write in the AGX kext or the firmware kext on this path | DIS | 0x...907d31c; callees 0x...b7d7760, 0x...b7d75e8; notifications 0x...907d898-d914 |
| M12 | The only reference to "Failed to reboot the firmware" is `AGXAccelerator::systemDidWake()`: `fw->enableGFXPower(true)`, `fw->prepareFirmwareForBoot()`, `accel->enableInterrupts(true)`, `fw->bootFirmware()`; a false return is a **panic**; then `accel->postBootFirmware()` | DIS | 0x...90815b4; calls 0x...9081640, 0x...908167c, 0x...90816bc, 0x...90816f8; panic 0x...9081754-1778 |
| M13 | Sleep side (`systemWillSleep`): queries `isFirmwareInitialised`, `isFirmwareRunning`, `anyOutstandingFirmwareEvents` first (branch conditions not read); then `enableSleepInterrupt(false)`, `fw->stopFirmware()` (flag only, M9), `enableInterrupts(false)`, `fw->enableGFXPower(false)`, `enableSleepInterrupt(true)` | DIS (call order), SYM (slot names) | 0x...90838f8-3bd8 |
| M14 | `prepareFirmwareForBoot` resets a small boot-argument area (offset counter `this[0x162c]`, 0x18 bytes zeroed) and calls `accel` slot 0xa68 with 1; `initFirmwareData` (also called from `restartWorkQueue` at 0x...90881fc, before resume-pre) rebuilds firmware data structures | DIS (prepare), SYM (initFirmwareData content not read) | 0x...90e7b60; 0x...90ea84c |
| M15 | The coprocessor power state is owned by RTBuddy, not by the AGX kexts. Power-down in `RTBuddy::_performPowerStateChangeGated`: `_setIopStatus(7)`, `RTBuddyManagementEndpoint::requestPowerState(state)`, wait with a deadline, `waitForIdleGated`, then two calls on the platform processor object (`this[0xb0]`, slots 0x8c0 with argument 0 and 0x890), `enableSRAM(false)`. Power-up: `enableSRAM(true)`, `RTBuddyFirmware::fixup`, `_setIopStatus(4)`, processor slot 0x8c0 with argument 1, processor slot 0x888, `wakeQuiesced()`, `_iopValidate()` | DIS (order), SYM (meaning of the processor slots) | 0x...b7cbf1c-c278 (down), 0x...b7cc2f0-c46c (up) |
| M16 | RTKit power values RTBuddy sends: IOP power state message (type 6) = 0x201, 0x220, 0x202, 0x0 for `RTBuddyPowerState` 0..3; `wakeQuiesced` sends type 6 value 0x220; AP power state message (type 0xb) = 0x201, 0x20, 0x0 for arguments 0, 1, 3 | DIS | `requestPowerState` 0x...b7dec40, table at 0xfffffe0007aa6d30; `wakeQuiesced` 0x...b7df854; `_signalApPowerGated` 0x...b7de8c8 |
| M17 | RTBuddy has a client-initiated `forcePowerOff(bool)` ("RTBuddy force power off initiated by client driver"): endpoint list reset, processor slot 0x8c0 with 0, slot 0x890, `enableSRAM(false)`. No caller in the AGX kext or the firmware kext was found in this pass | DIS (body), SYM | 0x...b7cd8a0; string 0x...7aa16ea |
| M18 | The CPU control register write and the power-domain gate are below RTBuddy, in the platform processor driver (class of `RTBuddy[0xb0]`, "IOSlaveProcessor" in `RTBuddyManagementEndpoint::withOptions`), which is not among the extracted kexts | SYM | `KC/kc/entries` (five entries); `rtb.dsyms` |

### B.2 Order of operations, as far as one pass shows

Runtime GPU recovery (hang, fault, firmware-initiated): progress timer or event -> `restart()` ->
`restartWorkQueue`: (1) halt request 0x85 with the recovery counter, wait for the firmware's acknowledgement,
panic if none; (2) inspect/snapshot, reset fences, `initFirmwareData`; (3) phase 1 -> 2, wait for 3;
(4) queue/context work; (5) phase -> 0; (6) restart report. The firmware keeps running throughout; no power
state is requested from the coprocessor and no reboot is visible. [DIS for the call order; one unresolved
call, Q1.]

Firmware reboot: only on system wake (M12), after a system sleep in which IOKit power management took the
coprocessor down through RTBuddy (M13, M15). A failed boot is a panic.

Answers to the bounded questions:

| Question | Answer | Grade |
|---|---|---|
| Who triggers a restart | `AGXAccelerator::restart()` from the hardware progress timer / recovery path; it restarts a work queue | DIS |
| What is quiesced first | the firmware itself, cooperatively (note 0x85); host keeps draining the firmware rings while waiting | DIS |
| Power state requested from the coprocessor at recovery | none | DIS (no RTBuddy power call in `restartWorkQueue`'s firmware-object calls) |
| Power states used at sleep/wake | IOP 0x201/0x202/0x0 down, 0x220 up; AP 0x201/0x20/0x0 (which `RTBuddyPowerState` the GFX node uses on sleep was not traced) | DIS / open |
| CPU control written directly by the AGX driver | no; delegated to RTBuddy and the processor driver | DIS (absence in the read paths), SYM |
| Power domain cycled | not at recovery. At sleep/wake RTBuddy calls the processor driver and `enableSRAM`; whether that gates the pmgr domain is below the extracted kexts | DIS / open |
| What is rebuilt before boot | `enableGFXPower(true)`, `prepareFirmwareForBoot` (boot-argument area), interrupts on, then RTBuddy `loadFirmware`; after "alive": `postBootFirmware` | DIS, SYM |
| What happens when the reboot fails | kernel panic ("Failed to reboot the firmware"; boot timeout panic when the override is set) | DIS |
| What happens when the firmware does not answer the halt request | kernel panic ("Failed to stop GFX FW for GPU recovery") | DIS |

### B.3 Open after this pass (not pursued)

| # | Question | What would settle it |
|---|---|---|
| Q1 | `restartWorkQueue` calls vtable slot 0x190 on an object loaded from `accel + 0xf43c + 0x86c` (0x...908820c-823c), right after `initFirmwareData`. Slot 0x190 of `AGXArmFirmware` is `bootFirmware`, but every other firmware call in the function loads `accel[0x4e8]`; the class of this member was not resolved. If it is the firmware object, macOS re-boots the firmware inside recovery | find the store to `accel+0xfca8` and decode that class's vtable |
| Q2 | Which conditions select the 0x85 halt path (`restartInfo` byte 8 == 1, and the object at `fw[0x190]`) versus the direct path | read `AGXRestartInfo` producers |
| Q3 | Who writes the +0x4a20 field and whether the host ever writes it (Asahi does: `resume = 1`) | search all accesses of `fw[0x868]+0x4a20` |
| Q4 | `RTBuddyPowerState` used for the GFX node at system sleep, and whether the processor driver gates the pmgr domain or only stops the CPU | `RTBuddy::setPowerState` / `_changePowerState` mapping; extract the ASC processor kext |
| Q5 | Content of the `enableGFXPower(false)` note to the firmware and of `initFirmwareData` | read 0x...90e5274, 0x...90ea84c |

## Consequences for the design

| Design item | Result | Basis |
|---|---|---|
| 0.4 "macOS restarts the same iBoot-loaded image at runtime" | **Correct to: at system wake.** Runtime GPU recovery on macOS is a cooperative firmware halt/resume (note 0x85 + phase handshake) with the firmware running; a firmware that does not answer the halt request, or does not boot at wake, is a kernel panic. macOS is therefore not a reference for restarting an unresponsive (F2/F3) firmware; a forced stop + reboot goes beyond what macOS does, and U1/U2 stay pure hardware questions. Q1 is the one residual doubt | M1-M4, M12 |
| T2 stop sequence | (a) For F1 (firmware halted and answering) the macOS-conformant action is not a stop at all but the two-phase resume (write 2, wait for 3, fix queues, write 0). The KMD follows Asahi's one-step `halted = 0, resume = 1` (comment at `K/src/backend_platform_windows.c:3679-3686`, which gives the same 0x10 field spacing); EXP1964/1965/2214 are "resumed, never retires". Whether the one-step resume is the cause is [HYP]; it is a cheaper experiment than a firmware restart and should be ranked before H2 for class F1. (b) For an orderly stop, macOS does not use the KMD's AP 0x10 / IOP 0x10 pair anywhere in the read paths: RTBuddy sends IOP 0x201, 0x202 or 0x0 and AP 0x201 / 0x0. The KMD pair is the Linux/m1n1 convention (`B/rtkit.c:81-85,1032-1045`); neither convention is proven on the GFX ASC for a later re-boot. (c) A halt request (0x85) before any stop gives the firmware-side acknowledgement (recovery counter +1) that the design otherwise lacks; it needs a firmware that answers, so it helps F1 only | M3, M6-M8, M15, M16 |
| T3 silence proof | macOS offers no host-side proof: its "stopped" evidence is the firmware's own acknowledgement (counter and phase fields), and without it macOS panics. For F2/F3 the design's proof must come from hardware state (CPU RUN bit read-back plus a power or reset cycle of sgx); the warm path's restriction to "job provably never reached the GPU" stands | M3, M4 |
| Lease cleanup (T5, 4.3 row "VM slots", U4) | Settled: JOB_END then RELEASE with existing ops, CPU stopped, power ON, root Active; no broker change for the warm path. It must run inside ResetFromTimeout after the stop proof and before the DDI returns (page-table updates of an owner with a pending job are refused as BUSY during the VidMm purge) | A.2, A.7 |
| 4.4 `AGX_RR_RECOVER` | Reduce to the power (or reset) cycle with the root kept Active; drop "unbind slots / drop leases / job state" and "root back to prepared, not active". Add: way out of power FAILED, RR-class locking across the cycle | A.7 |
| Caveat on `M/` (section 0) | Re-pinned (A.6). Bench = 83a9cf74, image f01f5961. Remove the sentence deriving the age of the checkout from the missing QUERY_CAPS case | A.1, A.6 |
| H1 expected checkpoint | Add: (1) the restart path issues neither PREPARE nor ACTIVATE (or records their STATE refusal) and the epoch reported by the broker is unchanged; (2) after the boot, the first GPUVA v5 response has Status OK and Flags bit 0 (tainted) clear, and an RR QUERY response has `AGX_RR_FLAG_ACTIVE` and `AGX_RR_FLAG_PREFIX_UNCHANGED` (`B/hv_agx_retained_platform.c:338,472-474`); (3) a LEASE of a previously used slot succeeds (ttbat slot reads zero, `gpuva_v5.c:753-754`); (4) record the firmware status block (recovery counter, phase, +0x20 field) before the stop and after the boot; no expected value is derivable from source | A.2, A.5, M6 |
| H1 failure criterion | Add: any broker TAINTED status after the restart (the rebooted firmware rewrote ttbat slot 0 or its TTBR1 prefix). This is unrecoverable in the same boot and rejects the warm path as designed, independent of whether the firmware itself came up; record the m1n1 "retained op" print (`retained_platform.c:475-476`) as evidence. Also name a known confound in the preregistration: slot 0 is valid before the CPU starts, unlike the first boot, so a boot failure alone does not separate "image does not boot twice" from "firmware rejects a pre-populated slot 0" | A.3 last row, A.7 |
| H1 stop kind | H1 as written uses the KMD orderly stop (AP 0x10, IOP 0x10, RUN off). macOS gives no support for or against that pair on GFX (M16); keep it as the single variable, but do not cite macOS `stopFirmware` as evidence that the protocol supports it: `AGXArmFirmware::stopFirmware` is a flag clear (M9) |

Sources inspected for this note: bench m1n1 `hv_agx_retained_platform.c`, `hv_agx_gpuva_v5.c`,
`hv_agx_power_broker.c`, `hv_agx_power_platform.c` (complete); `hv_agx_retained_root.c` (prepare, activate,
close, taint paths), `hv_agx_power_mmio.c` (lock contract, dispatch), `pmgr.c` (reset), `rtkit.c`, `hv_vm.c`
(hook return) by line; KMD `S/src/apple_agx_gpuva_g3_graph.c:140-175,1060-1185`; Asahi `gpu.rs`,
`fw/initdata.rs` at the cited lines; macOS AGX kext, firmware kext and RTBuddy at the cited addresses.
No external code or disassembly listing is reproduced.
