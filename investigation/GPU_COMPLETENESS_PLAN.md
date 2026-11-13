# AppleAgx completeness plan

Date: 2026-11-13. Owner: GPU architect session. Status: active.

Operator decision (2026-11-13): the driver is first made complete for everyday use of the
machine; closing the GravityMark gap to macOS continues only after that, except for work
that is already staged. A change that is rejected as a GravityMark lever is still landed
when it is contract-correct or a real driver feature.

Input: read-only audit of the accepted package at 6ccc6ae7d
(`.local/night-20261110/completeness-audit/REPORT.md`), plus the experiment record of
2026-11-11..13. Every status below is from that audit unless marked otherwise; items marked
VERIFY must be re-read against the current tree before design, because the architecture
document is partly stale (for example the mode list and MPO defaults).

## 1. Definition of complete

The driver is complete when an ordinary user can install it and use the laptop for a
working day without knowing it is experimental:

1. The machine boots to an accelerated desktop on its own (no second computer).
2. Closing the lid, sleeping, waking, switching the display off and shutting down work
   every time.
3. Brightness keys, night light and resolution changes work.
4. A hung or faulted GPU job ends one application, not the session; a failed display swap
   does not freeze the screen; the driver can be disabled, updated and re-enabled without
   a reboot.
5. Applications get the API level the hardware can serve (Direct3D 12 without fail-closed
   holes for features the hardware has, Direct3D 11, a current OpenGL, video playback
   without software decode where the hardware allows), and features the hardware lacks are
   reported as unsupported rather than refused at call time.
6. The shipped binary contains no measurement switches or drills, reports only
   capabilities it implements, and validates what user mode hands to the kernel.

Out of scope for this plan (own sessions, own plans): keyboard backlight, audio, Wi-Fi and
Bluetooth, Thunderbolt and USB, webcam. External display is in scope only for the KMD side
(second source, child status, hot-plug); the port and PHY work belongs to the TBX and USB
sessions.

## 2. Working method

- Every work package (WP) goes through: source-first read and design note (four sections:
  WINDOWS CONTRACT, AGX/ASAHI CONTRACT, TRANSLATION, WHAT IS STILL UNKNOWN, plus the
  ownership table required by AGENTS.md) -> implementation with tests in its own branch and
  worktree -> architect review -> package build -> hardware check -> ledger rows -> merge.
- Design notes and implementations are produced by Opus 5.5 subagents with a written brief.
  The architect (this session) writes the briefs, decides between designs, reviews, gives
  verdicts and keeps this plan and `GPU_CURRENT_STATE.md` current. The implementer session
  (fork 6) owns the builder, the bench and the ledger for hardware runs.
- One behavioural variable per hardware boot. Independent WPs may share a boot only as
  separate arms with their own receipts (`ATOMIC CONTRACT: none`).
- The bench is one machine: offline work runs in parallel (up to four subagents at a time),
  hardware checks queue in the order of section 5.
- A WP is DONE only with a hardware experiment id. "Implemented" without hardware is
  recorded as such.
- Speed gates stay in force for every package (GravityMark and Counter-Strike baselines):
  completeness work must not cost performance silently.
- Operator involvement is needed for: anything that must be seen (picture, pointer,
  brightness), lid and power-button actions, booting macOS, and decisions about signing,
  licences and guest security settings.

## 3. Work packages

Status legend: MISSING (no code), STUB (entry point refuses or does nothing), PARTIAL
(works in tested cases), READY (implemented, not on hardware), VERIFY (status to confirm).

### A. Survive faults (the session must not die)

| WP | Item | Status | Done when |
|---|---|---|---|
| A1 | Scan-out swap failure recovery (RQ-1 KMD retry, RQ-2 m1n1 latch deadline + fault injection) | READY | injected refused swap and dropped latch both recover without a frozen screen; StopDevice works afterwards |
| A2 | TDR: a hung job ends with device-removed for the owning application, not bugcheck 0x116 | PARTIAL | drill with a deliberately hung job: desktop survives, the application gets DXGI_ERROR_DEVICE_REMOVED/RESET, the next application renders; three drills in one boot |
| A3 | Firmware (RTKit) crash: detect, restart the GPU coprocessor in the same boot, rebuild queues and contexts | MISSING | drill: forced firmware crash -> desktop back within seconds, no reboot |
| A4 | GPU page fault: report the faulting process with details by default, kill only its context | PARTIAL | fault drill: other applications keep rendering |
| A5 | Disable / enable / driver update without reboot (StopDevice -> StartDevice re-arm; fix 041495ca7 exists, never on hardware) | PARTIAL | disable-enable and an in-place driver update end at a working desktop, twice in a row |
| A6 | Shutdown and restart always complete (two hangs on 2026-11-12/13; one unexplained on the inert recovery profile) and ResetDevice hands the display back for bugcheck screens | STUB / unknown cause | 20 consecutive ordered restarts without a hang; a forced bugcheck shows its screen |
| A7 | Set3 status contract on the legacy entry (BUSY returned through the legacy thunk) and remaining Set3 audit rows | PARTIAL | audit table has no row that can return an invalid status |

### B. Power

| WP | Item | Status | Done when |
|---|---|---|---|
| B1 | Display off / on (monitor D-state, SetVidPnSourceVisibility -> DCP blank and unblank, backlight off) | STUB | idle timeout and Win+L display-off blank the panel and wake it with the picture intact |
| B2 | GPU idle power: adapter power components / F-states so the GPU is clock- and power-gated when idle, and woken on submit | STUB | receipt shows the GPU off while idle; first frame after idle is correct; measured idle power drop |
| B3 | System sleep and wake (adapter D3 <-> D0: quiesce queues, save what the firmware loses, re-init on wake). Depends on the platform exposing a sleep state (Mu ACPI, PSCI, m1n1): VERIFY which states exist | STUB | lid close -> sleep -> open -> desktop back with applications alive, ten cycles |
| B4 | Lid and ACPI notifications (NotifyAcpiEvent) | STUB | lid switch triggers the configured action |
| B5 | Hibernate (S4) on the GPU side: clean stop and full re-init at resume. Depends on storage and platform S4 | MISSING | hibernate and resume to the same desktop |

### C. Display features

| WP | Item | Status | Done when |
|---|---|---|---|
| C1 | Brightness (DXGK_BRIGHTNESS_INTERFACE, ACPI _BCL/_BCM or direct DCP backlight control) | MISSING | brightness slider and keys change the panel in at least 10 steps; value survives display off/on |
| C2 | Resolution list and mode changes (11-mode table exists; offered only with the scaled-source capability) | VERIFY | every listed mode can be set from Settings and by a full-screen application, and restored |
| C3 | Gamma ramp / night light / colour calibration loader (SetGamma ramp; DCP colour pipeline or GPU-side) | MISSING | night light visibly warms the picture; calibration loader applies |
| C4 | Truthful display caps: rotation (advertises smooth rotation, supports identity only), EDID / child descriptor for the internal panel, HDR and wide-colour answers | PARTIAL | Settings shows correct panel identity; no advertised capability without code |
| C5 | Production defaults for the plane line: two-plane MPO on by default, measurement switches removed from the path | PARTIAL | fresh install uses planes with no registry value set |
| C6 | KMD side of a second display: second source and child, child status, hot-plug, mode enumeration from EDID. Hardware path comes from the TBX/USB sessions | MISSING | KMD passes a simulated hot-plug sequence offline; hardware check when the port work exists |
| C7 | Pointer and stretched-window picture reviewed by a person; lead-time tuning only if a defect is seen | open check | operator confirms pointer and stretch look right |

### D. Kernel memory, scheduling and security

| WP | Item | Status | Done when |
|---|---|---|---|
| D1 | BuildPagingBuffer: remaining operations (special-lock transfer and any other the claimed model can issue) | PARTIAL | every operation dxgkrnl can send for the advertised caps is handled; stress test under memory pressure passes |
| D2 | Offer / reclaim and eviction under memory pressure, kernel and D3D12 UMD sides | MISSING in UMD | memory-pressure run: applications survive, memory is returned |
| D3 | Concurrent processes: remove the single-VM-slot serialisation so two GPU applications run without refusal 55 | PARTIAL | two benchmarks side by side at the expected combined rate |
| D4 | Preemption and cancellation: implement what is advertised or stop advertising it | PARTIAL | caps equal behaviour; long job does not block the desktop beyond the documented bound |
| D5 | Kernel validation of user-mode command streams and descriptors (what a hostile process can make the GPU read or write) | PARTIAL | threat note with every user-controlled field, and a check or a containment argument for each |
| D6 | Broker TLB invalidation scope (per-range instead of whole-ASID per page), contract corrections to firmware init data and queue parameters (CM-1..CM-4) | open | each landed as a contract correction with no regression |

### E. User-mode API coverage

| WP | Item | Status | Done when |
|---|---|---|---|
| E1 | Direct3D 12 fail-closed DDIs that the hardware can serve: CreateHeapAndResource / OpenHeapAndResource, ResolveSubresourceRegion, WriteBufferImmediate, AtomicCopyBufferRegion, Blt, queue fence signal/wait, scheduling groups, shader comment / allocation-handle queries | STUB (about 20 of 30) | each has a probe step PASS on hardware; conformance probe suite has no AGX_REJECT hit for these |
| E2 | Direct3D 12 features the hardware lacks (raytracing, mesh and amplification shaders, VRS, sampler feedback): reported unsupported through caps so the runtime never calls them | STUB | caps audit: no path reaches a reject for a feature that caps deny |
| E3 | MSAA 8x and the known MSAA failure; doubles; enhanced barriers / newer interface level where the hardware allows | PARTIAL | probe steps PASS or caps deny with a reason recorded |
| E4 | SRV buffer reads as device loads by default (SRV-1 + SRV-1b robust table form), loads in flight per wait, barrier elimination at single-wave group sizes | READY / open | default on behind one regression boot |
| E5 | Direct3D 11: threading caps (driver command lists) if worthwhile, MPO present path, UMD interface level 2.2 | PARTIAL | D3D11 probe suite and a real application set pass |
| E6 | OpenGL: native ARM64 ICD in the package, and the version the Mesa Asahi driver can expose instead of 2.1 | PARTIAL | glxinfo-equivalent reports the raised version in ARM64 and x86 processes; a GL 3.3 / 4.x application runs |
| E7 | Vulkan ICD (port of the Mesa Honeykrisp driver onto the Windows winsys) | MISSING | vulkaninfo and a Vulkan sample run; large package, own plan |
| E8 | Video decode (the M1 has a separate video decoder block; DXVA / D3D11 video profiles) | MISSING | own driver-sized project; plan first, decision by the operator before work |

### F. Shipping

| WP | Item | Status | Done when |
|---|---|---|---|
| F1 | Standalone boot with the GPU: the hypervisor pieces the driver needs (GPU VA broker, scan-out broker, one-shot arm) run without the assisting computer | MISSING | cold boot from the internal disk reaches the accelerated desktop |
| F2 | Production build configuration: no drills, no measurement switches, no experiment receipts on hot paths; a separate qualification configuration keeps them | open | production binary has no Wom1 opt-in drill names; qualification build still passes the suite |
| F3 | Environment-variable downgrades and file-writing traces in the UMDs gated out of production | open | production UMDs ignore APPLE_AGX_* except a documented minimal set |
| F4 | Installer path (separate session) consumes the production package; device name, INF and documentation (LIMITATIONS.md is stale) corrected | open | installer session confirms |
| F5 | Signing beyond test signing | operator decision | decision recorded |

### G. Performance (after A-F, unless already staged)

Staged and allowed to finish: MP-2 (physical placement of buffers) on the bench. Then
frozen until this plan's sections A-C are done: GPU memory path, loads in flight in the
traversals, sampling residual. Their state is in `GPU_CURRENT_STATE.md`.

## 4. Dependencies that decide the order

- A1 before any further display work reaches users (a failed swap must not wedge).
- A2/A3 share the firmware restart sequence; A3's design comes first, A2 uses it.
- B1 before B3 (sleep needs display off/on); B3 needs the platform sleep state (VERIFY in
  the first design read); B2 is independent.
- C1 depends only on how the panel backlight is driven (DCP parameter or PWM): read first.
- C2 is VERIFY first: it may already work on the accepted package.
- F1 is independent of everything else and is the largest single visible gain; it is
  mostly hypervisor and launcher work.
- E-items are independent of each other and of A-C; they are parallel offline work with
  probe steps bundled into shared boots.

## 5. Order of hardware checks

1. MP-2 (staged) - one boot.
2. A1 (RQ-1 then RQ-2) - two boots.
3. C2 verification and C5 defaults - one boot, separate arms.
4. A5 disable/enable/update - one boot.
5. C1 brightness - one boot with the operator.
6. B1 display off/on - one boot with the operator.
7. A3 then A2 (firmware restart, TDR to device-removed) - several boots, drills.
8. B2 idle power, then B3 sleep/wake with B4 lid - several boots with the operator.
9. C3 gamma / night light, C4 caps truth - one boot each.
10. D-items and E-items in bundles as they become ready.
11. F1 standalone GPU boot whenever its package is ready (it does not block the queue
    because it uses a different boot path); F2/F3 production build before the final
    acceptance run.

Acceptance run for the whole plan: one cold standalone boot of the production build and a
scripted day (applications, video, resolution changes, brightness, display off, sleep and
wake, two GPU applications at once, a forced GPU fault, driver update, shutdown), plus the
speed gates.

## 5a. Corrections after the first design reads (2026-11-13)

Design notes: `investigation/analysis/COMPLETENESS-*.md` (A2A3, B1B3B4, C1, C2C4C5, F1).

- A2: a render hang the firmware detects already ends per application (EXP1277). Bugcheck 0x116 comes from three
  classes: halted-and-resumed job that never retires, firmware crash (not reported to Windows today), compute loop
  the firmware never detects. Recovery ladder: host halt + resume in ResetEngine -> adapter restart -> cold cycle
  through a new m1n1 operation -> render-dead mode with the display alive.
- B3: real S3 sleep and hibernation are not possible on this stack. "Sleep" = modern standby (display blank with
  backlight off, GPU idle, instant wake). B5 (hibernate) is withdrawn; the SSD driver refuses that path by design.
- B4: the lid and power button are only readable through the SMC coprocessor. New package **B4-SMC**: a read-only
  Windows SMC driver exposing the lid and power button (later AC and battery). Own design read.
- B1: SetVidPnSourceVisibility does not blank today; a broker DISPLAY_POWER command is needed, defined in the same
  ABI bump as brightness.
- C1: every swap hard-codes ~99 nits (panel maximum 420). Brightness needs a broker command; the Asahi nits table is
  used under MIT with attribution (operator decision 2026-11-13).
- C2: the 11-mode table IS offered on the bench line (scaled-source capability present); no mode change has been run
  on the current line. Found by reading: plane records at non-native modes use mode coordinates as panel coordinates.
- C4: rotation is not over-claimed (identity only is offered). Under-claims: no EDID/descriptor, no tearing presents.
- F1: the GPU brokers are already in m1n1 and run in standalone stage1; standalone lacks the GPU because the image is
  built from an old m1n1 line without the bench flags, with an old Mu profile, and without three small host-side steps
  and the expanded RAM layout. The one-shot arm is a registry value written inside Windows.
- A small m1n1 defect found on the way: the PSCI SMC64 dispatcher has no default case.

## 6. First wave (started 2026-11-13)

Design reads, in parallel, each returning the four sections and an ownership table:
A2+A3 (firmware restart and TDR), B1+B3 (display off and sleep, including which sleep
states the platform exposes), C1 (brightness), C2+C4+C5 (verification of modes, caps and
defaults against the current tree), F1 (standalone GPU boot). Implementation briefs follow
each design note after architect review.
