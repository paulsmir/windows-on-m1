# Native Apple Keyboard and Trackpad Driver Design

## Status

Approved for implementation on 2026-08-09.

This design covers the first Windows-only milestone for the built-in keyboard
and trackpad on the MacBook Air M1 (`j313`). It deliberately separates the
hardware transport from the Windows HID presentation so the same transport can
later support a full Windows Precision Touchpad and, if desired, a conventional
SPB/HID transport stack.

## Goals

- Drive the physical Apple SPI HID device directly from an ARM64 Windows
  kernel driver.
- Make the built-in keyboard work at the Windows sign-in screen and desktop.
- Provide basic trackpad cursor movement and primary click.
- Keep m1n1 limited to firmware bring-up, stage-2 pass-through, and physical to
  virtual interrupt routing. m1n1 must not parse input packets or emulate a USB
  input device.
- Make transport failures local to the input devnode instead of allowing an
  unbounded poll, interrupt storm, or malformed report to hang Windows.
- Preserve the protocol and controller layers for a later Windows Precision
  Touchpad implementation and a possible SPB/HID minidriver frontend.

## Non-goals for Milestone 1

- Input in Mu, the UEFI shell, Windows Boot Manager, or Windows PE before the
  Windows driver loads.
- Precision Touchpad certification behavior, multi-finger gestures, scrolling,
  palm rejection, right-click zones, haptics, or wake from sleep.
- A general Apple GPIO, SPI SPB, or power-domain driver for unrelated devices.
- Production driver signing. The first hardware builds use Windows test-signing.

## Hardware Contract

The J313 input device is connected to Apple SPI3 and uses Apple's SPI HID
transport. The initial contract is based on the J313 ADT and the working Asahi
Linux hardware description:

- SPI3 MMIO: `0x23510c000`, length `0x4000`.
- SPI source clock: 120 MHz; device bus frequency: 8 MHz.
- Input-controller enable: AP GPIO 195.
- Device interrupt: nub GPIO 13, active low.
- AP GPIO MMIO: `0x23c100000`, length `0x100000`.
- nub GPIO MMIO: `0x23d1f0000`, length `0x4000`.
- SPI chip-select setup and hold delay: 65 microseconds each.
- SPI chip-select inactive delay: 250 microseconds.

These values are not permission to write hardware blindly. Before the first
hardware write, an inventory tool must resolve the live ADT nodes and record
their registers, pins, interrupt parents, power domains, and compatibility
strings. The driver validates the ACPI resources and `_DSD` properties against
the supported J313 contract and fails its own start if they do not match.

The nub GPIO interrupt is parented by an Apple GPIO interrupt controller. Its
live interrupt-group assignment must be resolved during bring-up. The firmware
and m1n1 contract may expose the required parent AIC route or a bounded set of
nub GPIO parent routes, but it must not guess a route without an observable ADT
or GPIO-register check.

## Architecture

### m1n1 firmware layer

m1n1 performs only the platform work that Windows cannot perform before the
driver exists:

1. Resolve the J313 SPI HID, SPI3, AP GPIO, nub GPIO, pinmux, power-domain, and
   interrupt relationships from the ADT.
2. Keep the required SPI/GPIO fabric and power domains enabled across guest
   handoff.
3. Preserve the SPI pinmux and input-controller GPIO state required for driver
   startup.
4. Map the selected MMIO regions through stage 2.
5. Route the selected physical AIC interrupt to a guest GIC INTID with correct
   level semantics, masking, and EOI translation.

m1n1 does not read Apple SPI HID packets, translate contacts, or publish a
synthetic keyboard, mouse, or USB controller.

### Mu and ACPI layer

Mu publishes one J313-specific ACPI device with a project-owned hardware ID,
initially `APPL0001`. Its `_CRS` describes only the MMIO and interrupt resources
owned by the input driver. Its `_DSD` carries the supported hardware contract:
SPI frequency, GPIO indices, interrupt polarity, chip-select delays, and the
contract version.

The node remains disabled until m1n1 preflight confirms the corresponding
resources are mapped and routed. ACPI tests verify that the declared ranges do
not overlap guest RAM, the virtual framebuffer, PCI ECAM/BAR space, or another
exclusive device resource.

### Windows driver layers

The ARM64 KMDF driver is split into independent units:

- `AppleSpiController` owns SPI3 MMIO, FIFO setup, chip select, bounded polled
  transfers, and controller reset. It has no HID knowledge.
- `AppleGpio` performs only the two operations required by this device: the
  enable/reset sequence on AP GPIO 195 and acknowledgement/status handling for
  nub GPIO 13.
- `AppleSpiHidTransport` implements the 256-byte transfer packet, CRC16,
  message fragmentation, request/response correlation, device discovery,
  descriptor retrieval, and bounded recovery state machine.
- `AppleInputDevices` owns key/button release state and the minimum trackpad
  contact model needed for safe translation.
- `VhfFrontend` publishes the two Windows HID devices and is the only layer
  coupled to Virtual HID Framework.
- `Diagnostics` provides WPP/ETW events, counters, state snapshots, and a
  read-only diagnostic IOCTL.

The portable transport code lives under `drivers/apple-input/protocol/`. KMDF,
ACPI-resource, MMIO, interrupt, VHF, INF, and test-certificate integration live
under `drivers/apple-input/windows/`.

The implementation must be original project code. External implementations can
be used to validate observable behavior and hardware parameters, but source
code is not copied into this MIT-licensed tree.

## Windows HID Presentation

Virtual HID Framework is used only as the Windows-facing HID publication API.
It does not replace or emulate the Apple hardware transport. The KMDF function
driver still performs every physical SPI transaction and handles the physical
device interrupt.

After transport discovery completes:

- The keyboard is published with the hardware-provided HID report descriptor,
  and valid keyboard reports are forwarded without semantic translation.
- The trackpad is published as a small, fixed relative-mouse collection. A
  descriptor-driven decoder extracts the first active contact, converts contact
  deltas into bounded relative X/Y motion, and maps a valid press state to the
  primary button.

The transport retains complete raw multi-contact reports and parsed contact
metadata in bounded diagnostics. This avoids repeating transport reverse
engineering when the Precision Touchpad frontend is added.

VHF objects are created only after identity, interface metadata, descriptors,
and at least one valid transport exchange have passed validation. A preliminary
transport-only mode performs discovery and capture without publishing input.

## Startup and Data Flow

1. Windows enumerates `APPL0001` and loads the test-signed KMDF driver.
2. The driver parses resources and rejects unsupported contract versions or
   unexpected addresses before any write.
3. The driver maps MMIO and checks read-only register invariants.
4. It performs the controller enable sequence: high for 5 ms, low for 5 ms,
   then high for at least 50 ms.
5. It initializes SPI3 at 8 MHz and configures the documented chip-select
   delays.
6. It enables the input GPIO interrupt. The ISR acknowledges or masks the
   source and schedules passive-level work; it never performs a complete SPI
   transaction itself.
7. The worker reads 256-byte packets, validates packet and message CRCs,
   reassembles fragments, and completes discovery requests.
8. Discovery requests device identity, interface information, and keyboard and
   trackpad HID report descriptors.
9. Transport-only mode records bounded samples. Normal mode creates VHF
   keyboard and basic-mouse children and begins submitting reports.
10. On removal or shutdown, the driver stops new work, releases all keys and
    buttons, disables the device interrupt, cancels timers, and powers down the
    input controller when safe.

## Failure Handling

- Every MMIO wait and SPI operation has an explicit deadline. The initial SPI
  transfer timeout is at most 200 ms and is refined from hardware measurements.
- The ISR does constant bounded work and never allocates memory.
- Packet CRC, message CRC, invalid length, invalid offset, duplicate fragment,
  and out-of-order fragment failures discard the affected message and increment
  separate counters.
- Reassembly buffers have fixed maximum sizes and checked arithmetic.
- A bounded sequence of transport failures moves the device through
  `Running -> Recovering -> Offline`. Recovery uses a limited reset count and
  exponential backoff; it never loops indefinitely.
- Before reset, removal, or transition to `Offline`, the frontend submits
  release reports for every tracked key and button.
- Interrupt masking state is paired with cleanup paths so an error cannot leave
  a physical level interrupt permanently masked or create an interrupt storm.
- An input failure fails or restarts only the input devnode. It must not invoke
  a system reset or block an unrelated PnP/power callback indefinitely.
- D0 entry and D0 exit are supported in milestone 1. System sleep, wake input,
  and low-power optimization remain disabled and documented until implemented.

## Diagnostics

Diagnostics are designed for driver development without making synchronous USB
logging part of the normal input path.

Required counters include:

- physical and guest interrupt counts;
- queued and completed worker runs;
- SPI transfers, timeouts, and controller resets;
- packet CRC and message CRC failures;
- invalid and dropped fragments;
- completed keyboard and trackpad reports;
- VHF submissions and rejected submissions;
- recovery attempts and final offline transitions.

The read-only diagnostic IOCTL returns a versioned snapshot and a bounded ring
of recent packet headers, never arbitrary kernel memory. KD helpers report the
ACPI devnode, driver state, interrupt progression, transport phase, and error
counters. Production builds default to ETW counters without per-packet logging.

## Verification Strategy

### Host tests

The portable protocol library is tested without Windows or hardware:

- CRC16 golden vectors;
- packet encode/decode and length validation;
- single and multi-packet message assembly;
- corrupt, duplicate, missing, overlapping, and out-of-order fragments;
- discovery request/response matching and timeouts;
- recovery state transitions and retry bounds;
- forced key/button release on every teardown path;
- descriptor-driven extraction using captured, sanitized J313 fixtures.

### Contract tests

- Generated ACPI contains the exact versioned resource contract.
- m1n1 and ACPI agree on MMIO ranges and virtual interrupt routes.
- Stage-2 mappings expose only the intended hardware ranges.
- No route collides with the synthetic NVMe interrupt or existing xHCI route.
- Quiet production profiles do not enable packet tracing.

### Hardware checkpoints

Hardware work advances only after the previous checkpoint is recorded:

1. Live ADT inventory, with no hardware writes.
2. ACPI devnode enumeration with the driver disabled.
3. Resource parse and read-only register sanity checks.
4. Enable/reset GPIO sequence.
5. SPI boot packet and device identity.
6. Interface information and both report descriptors.
7. Transport-only keyboard and trackpad report capture.
8. VHF keyboard input.
9. Basic cursor motion and primary click.
10. Error injection, driver restart, normal shutdown, and repeated cold boot.

External USB keyboard and mouse remain connected as a recovery path throughout
bring-up.

## Milestone 1 Acceptance Criteria

- The built-in keyboard works at the Windows sign-in screen and desktop.
- The built-in trackpad provides stable cursor movement and primary click.
- A 30-minute mixed-input run produces no Windows hang, bugcheck, stuck key,
  stuck button, unbounded interrupt rate, or unbounded recovery loop.
- Restarting the devnode restores input without rebooting when the hardware is
  responsive.
- A cold boot and a normal Windows shutdown complete with the driver installed.
- External USB input remains functional.
- Driver Verifier passes the selected KMDF, pool, IRQL, I/O, and deadlock checks
  that are safe for this early hardware driver.
- The documented diagnostic workflow can distinguish no IRQ, SPI timeout, CRC
  failure, parser rejection, VHF rejection, and frontend inactivity.

## Required Milestone 2: Windows Precision Touchpad

Milestone 2 replaces only the basic-mouse frontend and extends power behavior.
It publishes a Windows Precision Touchpad top-level collection with the
required capabilities and certification-status feature reports, multiple
contacts, confidence and pressure data, buttons, scan time, and contact count.
It then validates scrolling, right click, palm rejection, and Windows system
gestures.

The SPI controller, GPIO handling, Apple packet protocol, discovery, CRC,
fragmentation, interrupt routing, diagnostics, and recovery state machine remain
unchanged. This milestone is a committed continuation of the input project, not
an optional future idea.

## Migration Path to a Conventional Windows Bus Stack

If a later release requires a full SPB architecture, `AppleSpiController` can be
promoted into an Apple SPI controller driver and `AppleSpiHidTransport` can be
hosted by a HID transport minidriver. `VhfFrontend` is then removed. The wire
protocol, descriptors, parsing, tests, ACPI resource inventory, and m1n1 IRQ
route remain reusable.

HIDSPICx is not assumed to understand Apple's proprietary transport. Any future
HIDSPICx/SPB design must first demonstrate protocol compatibility rather than
replacing the working frontend on architectural grounds alone.
