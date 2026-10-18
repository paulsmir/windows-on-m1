# Windows on Apple Silicon Platform Roadmap

## Purpose

This roadmap orders the remaining J313/T8103 work by dependency rather than by
feature visibility. A milestone advances only after its acceptance gate is
recorded on real hardware. Host-only tests are necessary but never sufficient
for a hardware milestone.

The current supported machine is the 2020 M1 MacBook Air (`j313`). Support for
another board is a separate port with its own live ADT inventory, ACPI resource
contract, firmware build, and hardware acceptance record.

## Priority order

1. Platform stability and deterministic diagnostics.
2. Built-in keyboard and Windows Precision Touchpad.
3. CPU topology, performance and power management.
4. Guest memory-map recovery and validation.
5. Native-storage performance and durability.
6. GPU acceleration.
7. Internal audio.
8. External-display output.

Built-in input is the active next user-facing feature. The 2026-08-13 eight-core
checkpoint is the accepted development baseline after correcting the recurrent
timer/IPI and vGIC fast-path defects. Phase 0 remains a regression gate while
input is developed; its full production acceptance criteria are deliberately
stricter than this single accepted hardware session.

## Project-wide rules

- Apply the source-first workflow in `AGENTS.md`: inspect live J313 evidence,
  Asahi Linux, m1n1, Mu, and the supported Windows/WDK architecture before
  proposing or implementing a change.
- Keep one known-good production image and one diagnostic image. Never debug by
  modifying both at once.
- Every hardware experiment changes one independently observable variable.
- Preserve raw evidence under `.local/`; publish only sanitized summaries.
- A framebuffer that stops changing is not proof that Windows stopped running.
  Combine display, SSH/RDP, guest UART, per-CPU progress, timer, and interrupt
  evidence.
- Do not hide a watchdog by disabling the watchdog, suppressing IPIs, pinning all
  work to one core, or dropping interrupts.
- Do not add a Windows driver to compensate for an architectural timer, vGIC,
  PSCI, stage-2, or ACPI correctness defect.
- Hardware access must be derived from a reviewed J313 contract and must fail
  closed when the live machine does not match it.
- A milestone is not complete until cold boot, normal shutdown, recovery, and a
  sustained workload all pass on hardware.
- Do not commit firmware binaries, private keys, certificates, machine-specific
  serial paths, raw packet captures, or private Windows data.
- Do not add assistant attribution, session URLs, or `Co-Authored-By` trailers to
  commits.

## Phase 0: Stable eight-core execution

### Goal

Make all four efficiency and four performance cores run Windows without global
freezes, interrupt storms, timer starvation, or watchdog bugchecks.

### Work

1. Capture one complete eight-core failure with the diagnostic image and the
   procedure in `documentation/plans/2026-08-10-platform-stability-implementation.md`.
2. Correlate each visible pause with per-CPU progress, architectural timer state,
   vGIC list registers, SGI send/receive/EOI counters, Windows network liveness,
   and bugcheck data.
3. Prove or reject, in order:
   - lost virtual timer PPI or incorrect timer re-arm;
   - SGI pending/active/EOI state loss or an SGI feedback loop;
   - incorrect vGIC redistributor ownership or list-register lifecycle;
   - secondary-core wake/WFI race;
   - PSCI/per-CPU context mismatch;
   - E/P cluster coherency, barrier, or cache-maintenance mismatch;
   - incorrect ACPI MADT, GTDT, or PPTT data.
4. Add the smallest deterministic regression test for every confirmed defect.
5. Validate the correction first on the affected CPU topology and then on all
   eight cores.

### Acceptance gate

- 20 consecutive cold boots reach the Windows sign-in screen.
- A 60-minute mixed workload completes with all eight cores online: Steam
  download/install, sustained disk writes, network traffic, window movement,
  and an RDP/SSH liveness probe.
- No `CLOCK_WATCHDOG_TIMEOUT`, `IPI_WATCHDOG_TIMEOUT`, unbounded SGI rate, global
  pause longer than two seconds, or silent CPU-progress loss occurs.
- Diagnostic instrumentation can be disabled without changing guest behavior.
- Normal Windows shutdown completes and the next boot does not enter recovery.

## Phase 1: Built-in keyboard and Precision Touchpad

### Goal

Drive the physical Apple SPI HID transport from a test-signed ARM64 Windows
driver. The final frontend must be a Windows Precision Touchpad, not a permanent
relative-mouse emulation.

### Work

Follow the approved design in
`documentation/design/2026-08-09-native-apple-input.md` and the implementation
plan in `documentation/plans/2026-08-09-native-apple-input-implementation.md`.

The first hardware checkpoints are deliberately transport-only: live ADT
inventory, ACPI enumeration, read-only MMIO validation, bounded SPI discovery,
and raw report capture. Keyboard publication follows. Basic cursor/click is an
intermediate diagnostic frontend; Windows Precision Touchpad is the completion
frontend.

### Acceptance gate

- Built-in keyboard works at sign-in and on the desktop.
- Precision Touchpad reports multiple contacts, click, scroll, right click,
  confidence/palm state, and Windows gestures without stuck contacts.
- Restarting the input devnode restores input without rebooting.
- Driver Verifier and a 60-minute mixed-input run complete without a bugcheck,
  interrupt storm, stuck key/button, or unbounded recovery loop.
- External USB keyboard and mouse continue to work as a recovery path.

## Phase 2: CPU topology, performance, and power

### Goal

Tell Windows which cores are efficient and which are performant, expose useful
performance limits, and control Apple DVFS without placing undocumented PMGR
sequences in a general Windows function driver.

### Architecture

- Mu describes the stable core/cache hierarchy in MADT/PPTT and the standard
  timer in GTDT.
- m1n1 owns Apple PMGR, clock, voltage, and hardware sequencing.
- A test-signed ARM64 Platform Extension Plug-in (PEP) communicates with m1n1
  through a small versioned paravirtual MMIO mailbox.
- The mailbox is a slow control path for performance, thermal, and idle-state
  transitions. Instruction execution, timer reads, scheduling, and IPIs do not
  pass through it.

### Work

1. Record the topology Windows currently exposes, including core efficiency
   classes and cache relationships.
2. Correct MADT/PPTT so 4E+4P topology is stable and testable without a PEP.
3. Specify and host-test the paravirtual CPU-control ABI.
4. Implement read-only frequency, residency, temperature, and limit telemetry.
5. Add bounded performance-state requests and thermal constraints.
6. Add idle states only after timer/IPI wake behavior has a dedicated hardware
   stress test.

### Acceptance gate

- Windows reports the intended 4E+4P topology and distinct efficiency classes.
- Sustained single- and multi-core workloads show no watchdog, starvation, or
  frequency oscillation.
- Performance requests never block at elevated IRQL and cannot hang guest boot.
- Removing or disabling the PEP falls back to a safe fixed-performance mode.
- Production logging adds less than one percent overhead in a repeatable CPU
  benchmark.

## Phase 3: Guest memory-map recovery

### Goal

Reduce the current approximately 2.8 GiB hardware-reserved region while keeping
all firmware, hypervisor, framebuffer, MMIO, DMA, and crash-capture allocations
explicit and non-overlapping.

### Work

1. Generate a machine-readable map from iBoot through m1n1, Mu ExitBootServices,
   and the final Windows physical-memory view.
2. Attribute every unavailable page range to its owner and lifetime.
3. Replace oversized fixed reservations with aligned measured allocations.
4. Reclaim boot-only memory only after its final consumer has released it.
5. Add guard regions and collision tests for NVMe, ECAM/BAR, framebuffer, DART,
   firmware, and hypervisor allocations.

### Acceptance gate

- The 8 GiB machine exposes at least 7 GiB usable to Windows, or every remaining
  unavailable range has a documented non-reclaimable hardware owner.
- MemTest-style pressure, hibernate-disabled reboot, NVMe stress, USB traffic,
  and physical/assisted display all pass without corruption.
- Assisted and standalone boots produce equivalent final memory contracts.

## Phase 4: Native-storage performance and durability

### Goal

Move from the correctness-first synchronous synthetic NVMe path toward safe
queue-parallel storage while preserving the physical GPT and macOS partitions.

### Work

1. Establish reproducible latency, sequential, random, queue-depth, CPU-cost,
   flush, and shutdown baselines.
2. Separate guest NVMe queue handling from Apple ANS backend completion.
3. Add bounded asynchronous requests, batching, multiple queue pairs, and
   correct interrupt coalescing.
4. Implement and test flush/FUA, reset, timeout, cancellation, surprise shutdown,
   and error propagation before optimizing throughput.
5. Investigate a native ANS Windows driver only after the shared physical disk
   can be protected by an equally strong IOMMU and recovery contract.

### Acceptance gate

- No filesystem/GPT corruption across forced error injection and repeated clean
  shutdown tests.
- Sequential and random performance scale with queue depth without starving
  timers or flooding virtual interrupts.
- Target: at least 1 GiB/s sequential read on the current bridge before deciding
  whether a native ANS driver is justified.

## Phase 5: GPU acceleration

### Current checkpoint

G0 and the firmware-only G1 gate are implemented for the development J313.
EXP-20260825-078 completed ten independent V13_5/G13 firmware lifecycles with a
hardware reboot and fresh-proxy receipt after every cycle.  This proves the
reviewed resource contract, firmware management heartbeat, shared fault
observation, context-zero UAT cleanup, and cold-reset boundary.  It does not
provide a Windows graphics adapter or submit render work.  The exact contract,
operator procedure, qualified evidence, and direct no-loss target architecture
are documented in [`AGX_BRINGUP.md`](AGX_BRINGUP.md).

### Goal

Replace software rendering and boot-framebuffer-only presentation with an ARM64
Windows WDDM stack for Apple AGX while preserving the working DCP scanout path.

### Work

1. Split the project into display, memory-management/command-submission, and
   user-mode rendering milestones.
2. Define and qualify render-domain power ownership, a dedicated non-zero UAT
   context, one bounded queue/fence submission, and reset recovery before
   exposing AGX to Windows.
3. Bring up a signed WDDM kernel display miniport with reset and timeout recovery.
4. Add protected GPU virtual-address spaces, validated command submission, and
   fault containment.
5. Implement the user-mode Direct3D path using a legally compatible strategy;
   external source may inform behavior but cannot be copied without license
   review.
6. Add production power, thermal, suspend, and crash recovery after correctness.

### Acceptance gate

- Windows uses a WDDM accelerated adapter rather than Microsoft Basic Display.
- Desktop composition, video playback, and representative Direct3D workloads
  survive repeated device resets and 60-minute stress runs.
- Invalid submissions cannot access arbitrary guest or host physical memory.
- GPU load does not regress CPU watchdog, NVMe, input, or physical display.

## Phase 6: Internal audio

### Goal

Provide safe internal playback and capture through Apple MCA/I2S, codecs, and
speaker amplifiers using a Windows audio driver with explicit power sequencing.

### Work

1. Inventory the live J313 audio graph, clocks, codecs, amps, DMA, and interrupts.
2. Start with bounded low-volume playback to the safest available output.
3. Implement WaveRT/PortCls endpoints, format negotiation, DMA, jack/microphone
   routing, and power transitions.
4. Add speaker calibration and protection before enabling normal internal-
   speaker volume.

### Acceptance gate

- Playback and capture run for 60 minutes without DMA underrun, interrupt storm,
  clipping, or corruption.
- Mute, volume, endpoint switching, shutdown, and resume are safe.
- Speaker protection fails closed when calibration data is absent or invalid.

## Phase 7: External displays

### Goal

Support USB-C DisplayPort output using the Apple external DCP/display pipeline,
including hot-plug, EDID, modesetting, and display audio where available.

### Work

1. Inventory Type-C orientation/Alt Mode, PHY, DCP endpoint, DART, hot-plug IRQ,
   and framebuffer relationships.
2. Preserve or initialize the external pipeline behind a versioned platform
   contract.
3. Add EDID and safe modesetting before hot-plug or multi-display support.
4. Integrate external targets with the WDDM display stack from Phase 5.
5. Add display audio only after Phase 6 establishes the Windows audio model.

### Acceptance gate

- Cold-plug and hot-plug work on both USB-C ports and orientations.
- Disconnect/reconnect does not hang DCP, Windows graphics, USB, or the guest.
- Internal-only, external-only, mirror, and extended-desktop modes survive a
  60-minute display stress run and normal shutdown.

## Release gates

Each completed phase produces:

1. a focused design and implementation plan;
2. host/unit/contract tests;
3. a sanitized real-hardware acceptance record;
4. updated architecture, debugging, limitations, and run documentation;
5. a reviewable commit series in each affected repository;
6. a tagged known-good recovery artifact only after cold-boot validation.

No later phase may weaken a previous phase's acceptance test. The complete
platform milestone is a stable standalone boot with eight cores, usable memory,
durable storage, built-in input, accelerated internal display, audio, and
external display, while assisted debug remains available as a separate launch
profile.
