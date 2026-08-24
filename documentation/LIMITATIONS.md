# Known limitations

## Validation status

- Internal Windows installation, OOBE, USB input, internal-NVMe boot, desktop, and RDP were
  validated on one 2020 M1 MacBook Air (`j313`).
- The current self-contained standalone packed image has cold-booted installed Windows on the
  development J313. Intermittent whole-system pauses of about 20 seconds remain unresolved.
- Physical internal-panel handoff and a full-panel 2560x1600 Windows desktop were validated in
  assisted and quiet standalone operation on the development machine.

## Hardware scope

Only J313/T8103 is supported. Device addresses, interrupt routes, ACPI, firmware packaging,
and memory layout are machine-specific. Other M1 models are not expected to work by merely
changing a board name.

There is no accelerated GPU driver, audio driver, Wi-Fi, Bluetooth, camera, sleep,
battery-management integration, or production power management. A test-signed native
driver for the built-in Apple keyboard and Windows Precision Touchpad is validated on the
development J313, but external USB input remains the required installation and recovery
path. See `APPLE_INPUT.md` for its exact bounded checkpoint and limitations.

## Storage

The synthetic NVMe bridge performs correctness-first synchronous I/O. A measured sequential
read was roughly 300 MB/s, far below the several-GB/s native device capability. Queue depth,
parallel command execution, batching, and reduced proxy instrumentation remain future work.

The controller exposes the physical SSD namespace. Firmware or hypervisor defects can
corrupt any partition, not only Windows.

## Memory

Windows reports 8 GB installed on the development machine but approximately 5-6 GB is
practically available. Large fixed guest reservations, m1n1 heap/backing regions, firmware,
low-memory alias backing, framebuffer, and hardware windows account for the gap. The map has
not yet been compacted for production use.

## SMP and timing

All eight cores can enter Windows, but the vGIC, SGI, timer, and list-register paths remain
experimental. Earlier runs produced `CLOCK_WATCHDOG_TIMEOUT` and `IPI_WATCHDOG_TIMEOUT`.
The current build substantially reduces immediate watchdog failures and boot latency, but can
still stop all visible input and UI progress for roughly 20 seconds before recovering. Sustained
RDP or SSH operation is evidence of progress, not a guarantee of workstation stability.

## USB and debugging

The assisted framebuffer is intentionally paced to avoid starving proxy commands and guest
timers. It is not a high-frame-rate remote desktop. Losing the proxy can leave the last frame
visible while Windows continues running.

KD helpers contain offsets and protocol assumptions for the tested Windows ARM64 build.
They are diagnostic tools, not a general Windows debugger implementation.

## Installation

There is no automated GPT validator or partition installer. The manual DISM flow is
documented for experienced users and remains destructive if applied to the wrong disk.
Windows Setup's online account and hardware-requirement paths are not relied upon.
