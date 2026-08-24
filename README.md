# Windows 11 ARM64 on the M1 MacBook Air

Experimental native Windows boot for the 2020 M1 MacBook Air (`j313`, Apple T8103), built
from a Project Mu firmware fork and an m1n1 EL2 hypervisor fork.

> **Warning:** This project exposes the internal SSD to experimental firmware and a custom
> NVMe bridge. A wrong `diskpart` command can destroy macOS, Recovery, or the Asahi boot
> environment. Keep a complete backup and read the entire installation guide before making
> disk changes.

## Current status

Windows 11 ARM64 has booted from the internal Apple SSD, completed OOBE with a local
account, reached the desktop, accepted USB keyboard and mouse input, and sustained an RDP
session long enough to install software. The working development configuration exposes:

- four virtual CPUs backed by the four Icestorm efficiency cores;
- a synthetic PCIe NVMe controller backed by the physical Apple ANS storage device;
- physical USB xHCI with keyboard, mouse, hubs, and installation media;
- native built-in Apple keyboard and Precision Touchpad through the J313 SPI-HID driver;
- a Project Mu GOP framebuffer and a remotely observable virtual framebuffer;
- virtual UART and Windows kernel-debug transport for assisted development.

The validated four-efficiency-core baseline completed a fresh Windows installation, OOBE,
and reached a responsive desktop without the earlier micro-freezes in its initial hardware
session. Firestorm performance cores are deliberately disabled in the guest MADT: previous
mixed E/P configurations produced watchdog bugchecks, global pauses, and inconsistent
secondary-core startup. This is still an experimental checkpoint, not a production release;
long-duration stress and repeated cold-boot qualification remain pending.

Only `j313` is currently supported. This is not a general Apple Silicon Windows installer.

## Two operating modes

1. **Standalone mode:** iBoot loads an Asahi-provisioned boot entry, m1n1 starts the embedded
   Mu firmware, and Mu starts Windows from the internal SSD. A second computer is not part
   of the runtime path.
2. **Assisted development mode:** another Mac chainloads matching m1n1 and Mu builds over
   USB and captures UART, hypervisor logs, framebuffer updates, telemetry, and KD traffic.
   This mode can test firmware and driver changes without rewriting the Air ESP.

## Documentation

- [Standalone installation](documentation/INSTALL.md)
- [Build and packaging](documentation/BUILD.md)
- [Standalone and assisted operation](documentation/RUN.md)
- [Launch profiles](documentation/CONFIGURATION.md)
- [Architecture](documentation/ARCHITECTURE.md)
- [Platform roadmap and milestone gates](documentation/ROADMAP.md)
- [Current stability checkpoint and iteration workflow](documentation/PLATFORM_STABILITY.md)
- [Validated J313 four-efficiency-core baseline](documentation/STABLE_4E_BASELINE.md)
- [Debugging and KD tools](documentation/DEBUGGING.md)
- [Engineering history](documentation/DEVELOPMENT_HISTORY.md)
- [Historical artifact provenance](documentation/history/ARTIFACT_PROVENANCE.md)
- [Known limitations](documentation/LIMITATIONS.md)
- [Built-in Apple keyboard and Precision Touchpad](documentation/APPLE_INPUT.md)
- [Changelog](CHANGELOG.md)

## Repository layout

- `m1n1_windows/` — pinned hypervisor fork submodule.
- `mu/` — pinned Apple Silicon Project Mu fork submodule.
- `scripts/` — public build, ESP-install, standalone, and assisted-mode entry points.
- `tools/` — deterministic layout and image-packaging tools.
- `config/j313-guest-layout.json` — canonical guest physical-memory contract.
- `run_uefi.py` — Python-assisted guest launcher used for development.
- `tools/kd/` — focused Windows serial kernel-debug utilities.
- `extra/` — framebuffer, UART, and source-level diagnostic helpers.

## Upstream projects

This work builds on [Asahi Linux](https://asahilinux.org/),
[m1n1](https://github.com/AsahiLinux/m1n1),
[Project Mu](https://github.com/microsoft/mu), and the Apple Silicon Windows work from
[NT-for-ASi](https://github.com/NT-for-ASi). The active source forks are
[paulsmir/m1n1_windows](https://github.com/paulsmir/m1n1_windows) and
[paulsmir/apple_silicon_platforms_mu](https://github.com/paulsmir/apple_silicon_platforms_mu).
See the submodule histories and licenses for their respective copyrights and terms.

## Release truthfulness

Documentation distinguishes three states:

- **validated:** observed on the development J313;
- **implemented:** present in source and host-tested but awaiting the relevant hardware run;
- **planned:** not yet implemented.

The physical internal-panel handoff, full-panel 2560x1600 Windows framebuffer, four Icestorm
guest CPUs, synthetic NVMe bridge, and physical USB have been validated together on J313.
Firestorm guest CPUs and GPU acceleration remain implementation milestones; they are not
claimed as working here. The test-signed built-in Apple keyboard and Precision Touchpad
driver is hardware validated on J313 at the bounded checkpoint documented below.

Native built-in input build, installation, diagnostics, and rollback are documented in
[`documentation/APPLE_INPUT.md`](documentation/APPLE_INPUT.md).
