# VHF Keyboard and Windows Precision Touchpad Design

## Status

The architecture and written specification were approved on 2026-08-24.

This document supersedes only the temporary relative-mouse frontend described
in `2026-08-09-native-apple-input.md`. The validated J313 hardware transport,
ACPI resources, m1n1 stage-2 mappings and level interrupt route remain
unchanged.

## Goal

Publish the built-in J313 keyboard and trackpad as native Windows input devices
after the existing Apple SPI HID transport reaches `READY`:

1. publish the keyboard through Virtual HID Framework (VHF), using the Apple
   keyboard's validated report descriptor and forwarding valid keyboard input
   reports without semantic translation;
2. publish the trackpad as a Microsoft-compatible Windows Precision Touchpad
   collection, translating Apple multi-contact reports into the Windows report
   contract;
3. keep input failure local to the `APPL0001` devnode and preserve the current
   stable boot, storage, display and external USB recovery paths.

## Non-goals

- Input in m1n1, Mu, the UEFI shell, Windows Boot Manager or Windows PE.
- USB keyboard, USB mouse or PS/2 emulation.
- Publishing the raw Apple trackpad descriptor as a generic HID trackpad.
- A disposable relative-mouse milestone before Precision Touchpad support.
- Production signing or Windows Hardware Compatibility certification in this
  phase. Development packages continue to use test signing.
- Changes to platform timers, CPU topology, NVMe, display, xHCI or the stable
  standalone launch contract.

## Evidence and Sources

The design is based on these current contracts:

- Live EXP-057/EXP-046 hardware evidence: `APPL0001` reaches discovery phase
  `READY` with bounded SPI transfers, no CRC failure and no timeout. Device 1 is
  the keyboard and device 2 is the trackpad.
- The discovered keyboard report-descriptor payload is 182 bytes; the trackpad
  report-descriptor payload is 110 bytes. These values are evidence for this
  J313, not hard-coded protocol constants.
- Asahi Linux's Apple SPI HID implementation treats the hardware as one
  transport with separately discovered HID devices and dispatches incoming
  reports by Apple device identifier.
- m1n1 owns preserved platform state, stage-2 MMIO mapping and the physical IRQ
  330 to guest INTID 865 level route. It does not parse or synthesize input.
- Mu exposes the versioned `APPL0001` ACPI resource contract. It does not expose
  a standard HID-over-SPI bus because Apple's wire protocol is proprietary.
- Microsoft VHF is the supported KMDF mechanism for a kernel-mode HID source
  driver. VHF objects are created after `WdfDeviceCreate`, started only after
  their descriptors and callbacks are valid, and deleted during orderly
  teardown.
- Microsoft permits a Windows Precision Touchpad on an alternate bus through a
  third-party HID source/minidriver, provided the required Precision Touchpad
  collections, reports and feature semantics are implemented.

Primary Windows references:

- [Virtual HID Framework](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-)
- [VhfCreate](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/vhf/nf-vhf-vhfcreate)
- [Windows Precision Touchpad protocol implementation](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-protocol-implementation)
- [Windows Precision Touchpad collection](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-windows-precision-touchpad-collection)
- [Windows Precision Touchpad bus connectivity](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-device-bus-connectivity)

The implementation must remain original project code. External source is used
to understand observable protocol behavior, not copied into this repository.

## Chosen Architecture

### One physical transport, two VHF devices

The existing KMDF function driver continues to own the Apple SPI controller,
GPIO reset/enable sequence, physical interrupt, message reassembly, discovery,
recovery and diagnostics. After discovery succeeds, it creates two independent
VHF devices:

- `AppleKeyboardVhf`: hardware descriptor and report pass-through;
- `ApplePrecisionTouchpadVhf`: fixed Windows descriptor and translated reports.

The two frontends share no mutable HID state. They consume validated messages
from a small transport-to-frontend interface and can be started, stopped and
tested independently.

### Why the trackpad is translated

The Apple trackpad's discovered HID descriptor describes Apple's hardware
reports; discovery alone does not demonstrate the required Windows Precision
Touchpad top-level collections or feature-report semantics. Passing it directly
to VHF could produce a generic or unusable HID device and would not satisfy the
project goal.

A conventional HID minidriver or HIDSPICx stack is also rejected for this
milestone. HIDSPICx assumes Microsoft's HID-over-SPI contract, while the proven
transport is Apple's proprietary SPI HID protocol. Replacing the working
transport would add risk without improving the Windows-facing result.

## Component Boundaries

### `DescriptorStore`

Copies each validated discovered descriptor into bounded nonpaged driver-owned
storage before the reassembly buffer is reused. It records device identifier,
length and SHA-256-compatible diagnostic digest metadata, but never exposes raw
descriptor or input payload bytes through the normal diagnostic snapshot.

The keyboard VHF object may be created only when the stored descriptor passes a
bounded HID descriptor validation pass. The trackpad descriptor is retained as
hardware evidence and parser metadata; it is not supplied to Windows.

### `AppleKeyboardVhf`

Owns one `VHFHANDLE`, the keyboard submission buffer and keyboard release state.
It starts only after transport discovery is complete and the keyboard
descriptor is validated. In `READY`, a valid Apple device-1 input message is
submitted to VHF without changing its report ID or key semantics.

Every submission verifies the report ID and exact descriptor-derived size.
Removal, reset, recovery or transition offline submits a neutral release report
before deleting the VHF object when the object is still usable. No VHF call is
made from the physical ISR.

### `AppleTrackpadParser`

Decodes Apple device-2 input reports into an internal, bus-independent contact
frame. The frame contains only validated values required by Windows: frame/scan
time, contact identifiers, active state, confidence, X/Y position, pressure
when proven by captured fixtures, button state and contact count.

The parser is descriptor-driven and bounds every offset, field width, contact
count and arithmetic conversion. Unknown report IDs, short/long reports,
duplicate contact IDs and coordinates outside the declared physical range are
rejected before they reach VHF. The parser does not infer pressure, confidence
or palm state until sanitized hardware fixtures demonstrate the corresponding
Apple fields.

### `ApplePrecisionTouchpadVhf`

Owns a second `VHFHANDLE`, a project-owned fixed Precision Touchpad report
descriptor and the required feature-report state. It implements the Microsoft
required collections and reports, including:

- Digitizers/Touch Pad application collection;
- multiple parallel finger logical collections;
- contact identifier, tip switch, confidence, X/Y and contact count;
- scan time and button state where required by the selected descriptor;
- Contact Count Maximum and the required device-capability feature reports;
- Configuration collection and Input Mode handling required for Precision
  Touchpad operation.

The exact maximum contact count is chosen from verified Apple report capacity,
with a conservative implementation cap. It is not guessed from the physical
sensor's marketing capability. Unsupported optional usages are omitted rather
than populated with fabricated data.

### `VhfFrontendManager`

Owns frontend lifetime and enforces this state machine:

`Absent -> DescriptorsReady -> Starting -> Running -> Stopping -> Absent`

Transport `READY` is necessary but not sufficient for `Running`; both device
descriptors and all VHF allocations must succeed. Keyboard and trackpad start
independently, so a trackpad parser error cannot remove a working keyboard.
Repeated start/stop and D0 transitions are idempotent.

## Data Flow and Execution Context

1. The physical ISR performs only bounded acknowledgement/masking work and
   schedules the existing worker.
2. The passive-level transport worker reads, validates and reassembles Apple
   messages.
3. Discovery responses are copied into `DescriptorStore` before the transport
   buffer is reused.
4. Once discovery reaches `READY`, `VhfFrontendManager` starts the keyboard and
   trackpad VHF objects at `PASSIVE_LEVEL`.
5. Keyboard reports are length/report-ID checked and submitted directly.
6. Trackpad reports are parsed into an internal contact frame, normalized to the
   declared Windows logical ranges and encoded into a Precision Touchpad input
   report.
7. VHF feature-report callbacks return or update only bounded frontend state;
   they never perform SPI I/O or wait for the hardware.

The data path uses fixed-size context-owned buffers. It performs no pageable
allocation, synchronous USB logging or unbounded wait per input report.

## Error Handling and Recovery

- VHF publication never weakens the existing transport length, CRC,
  fragmentation, timeout or phase validation.
- A malformed keyboard or trackpad report increments a device-specific reject
  counter and is dropped. It does not reset the transport on its own.
- A VHF submission failure is counted separately from parser and transport
  failures. A bounded repeated-failure threshold stops only that frontend.
- Transport recovery first quiesces report delivery, releases all visible keys,
  buttons and contacts, then performs the existing bounded hardware recovery.
- D0 exit, surprise removal and driver unload stop new submissions, wait for
  already queued passive work, delete VHF objects and only then release MMIO and
  interrupt resources.
- The driver never requests a system reboot and never blocks shutdown waiting
  indefinitely for input hardware.
- External USB keyboard and mouse remain the recovery path throughout hardware
  validation.

## Diagnostics and Privacy

The versioned diagnostic snapshot gains counters and frontend state only:

- keyboard/trackpad VHF lifecycle state;
- accepted, rejected and submitted reports per device;
- VHF submission failures and last status;
- active contact count and parser rejection reason counters;
- descriptor lengths and digests.

Normal diagnostics never expose keys, coordinates, raw reports or arbitrary
kernel memory. Exact raw report capture, when required to reverse engineer the
trackpad layout, is an explicit test-only build/run, stored under ignored local
evidence, sanitized before any fixture is committed and disabled in production.

## Implementation and Hardware Gates

### Gate C1: descriptor ownership

- Add host tests proving descriptor bytes survive reassembly-buffer reuse.
- Reject oversized, empty and structurally invalid descriptors.
- Hardware snapshot must show the same descriptor lengths/digests across
  repeated discovery without changing the stable ESP.

### Gate C2: keyboard VHF

- Add host/contract tests for VHF lifecycle, exact report sizes and teardown
  release behavior.
- Build and test-sign the ARM64 package reproducibly.
- Install only after recording the artifact SHA-256 and rollback package.
- Validate letters, modifiers, function keys, repeat, simultaneous keys,
  release, sign-in input, devnode restart and normal shutdown.

### Gate D1: sanitized trackpad evidence

- Capture a bounded set of reports for no contact, one stationary contact,
  X-only and Y-only movement, physical click and two contacts.
- Keep raw captures ignored and local; commit only reviewed sanitized fixtures.
- Demonstrate every parsed field by controlled deltas before implementing it.

Gate D1 uses a separate `AppleInputCapture` driver, INF, administrator-only
device ACL, CLI and manually dispatched CI workflow. The normal Debug and
Release `AppleInput` projects do not define `AI_ENABLE_TRACKPAD_CAPTURE`, do not
compile `trackpad_capture.c` and do not recognize the capture IOCTLs. The test
driver accepts only reassembled device-2 reports after protocol CRC validation,
stores at most 16 reports of at most 512 bytes in fixed kernel memory, and
clears the capture on transport start, stop or explicit cancellation. The CLI
requires an explicit new output path and refuses overwrites; raw output belongs
under ignored `.local/apple-input/trackpad-captures/` only.

This boundary follows Microsoft's requirement that a VHF source driver submit
validated input reports from kernel mode and its documented rule that an SDDL
assigned with `WdfDeviceInitAssignSDDLString` requires a named device object.
The Apple packet filter follows the upstream Linux `applespi` transport split:
read packet device 1 is the keyboard and device 2 is the touchpad. These
references justify the boundary but do not substitute for J313 controlled-
delta evidence; no Linux touch field is assumed to match this machine until
Gate D1 proves it.

Primary references:

- <https://learn.microsoft.com/windows-hardware/drivers/hid/virtual-hid-framework--vhf->
- <https://learn.microsoft.com/windows-hardware/drivers/ddi/wdfdevice/nf-wdfdevice-wdfdeviceinitassignsddlstring>
- <https://github.com/torvalds/linux/blob/master/drivers/input/keyboard/applespi.c>

### Gate D2: Precision Touchpad VHF

- Add parser golden tests, malformed-report tests and report-encoding tests.
- Validate required feature reports and Input Mode transitions in host/contract
  tests.
- Hardware-check cursor motion, tap/click, two-finger scrolling, right click,
  three/four-finger Windows gestures, contact release and devnode restart.

### Gate D3: stability

- Run a 30-minute mixed keyboard/trackpad session with no bugcheck, hang, stuck
  key, stuck contact, interrupt storm or unbounded reset.
- Validate repeated cold boot, sign-in, sleep-disabled D0 restart and normal
  shutdown.
- Run the applicable Driver Verifier checks only after the normal path passes.

Each hardware gate changes one observable variable and receives pre/post entries
in `investigation/EXPERIMENTS.md`. Every implementation change receives a
corresponding `investigation/CHANGES.csv` row after its implementation commit.

## Rollback

The accepted 4-E-core stable ESP remains untouched during assisted tests. Before
driver installation, preserve the currently installed package and record its
published INF. Rollback removes the experimental published INF and reinstalls
the recorded transport-only package. If the input devnode fails, external USB
input remains available and no firmware reinstall is required.

## Acceptance Criteria

The feature is complete only when:

- the built-in keyboard works at sign-in and desktop with no stuck keys;
- Device Manager identifies the trackpad as a Windows Precision Touchpad;
- pointer motion, primary/right click, two-finger scrolling and Windows
  multi-finger gestures work through the built-in trackpad;
- malformed reports and transport recovery cannot expose stuck input or hang
  Windows;
- keyboard and trackpad survive devnode restart and normal shutdown;
- a recorded 30-minute mixed-input hardware run passes with the stable external
  USB recovery path still functional;
- all host, contract, WDK build, INF and ledger tests pass;
- no platform, boot, storage, display or external USB regression is observed.
