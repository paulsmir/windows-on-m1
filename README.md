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

- eight virtual CPUs backed by all four efficiency and four performance cores;
- a synthetic PCIe NVMe controller backed by the physical Apple ANS storage device;
- physical USB xHCI with keyboard, mouse, hubs, and installation media;
- a Project Mu GOP framebuffer and a remotely observable virtual framebuffer;
- virtual UART and Windows kernel-debug transport for assisted development.

The self-contained standalone `boot.bin` path is implemented and covered by host-side
tests, but cold-boot hardware validation of the current packed image is still pending. The
host-assisted path remains the verified development and recovery workflow.

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
- [Debugging and KD tools](documentation/DEBUGGING.md)
- [Engineering history](documentation/DEVELOPMENT_HISTORY.md)
- [Known limitations](documentation/LIMITATIONS.md)
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

The physical internal-panel handoff and full-panel 2560x1600 Windows framebuffer have been
validated on J313 in assisted mode. The standalone monitor image has cold-booted through all
launch checkpoints into the Windows kernel with eight CPUs and live NVMe. The quiet production
image remains a separate no-host acceptance test; monitor-mode timing is not production timing.
