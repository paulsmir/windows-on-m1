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
session long enough to install software. The current validated assisted configuration exposes:

- all eight M1 CPU cores: four Icestorm efficiency cores and four Firestorm
  performance cores, with distinct Windows efficiency and scheduling classes;
- a synthetic PCIe NVMe controller backed by the physical Apple ANS storage device;
- physical USB xHCI with keyboard, mouse, hubs, and installation media;
- native built-in Apple keyboard and Precision Touchpad through the J313 SPI-HID driver;
- a Project Mu GOP framebuffer and a remotely observable virtual framebuffer;
- virtual UART and Windows kernel-debug transport for assisted development.

The eight-core assisted checkpoint reached the lock screen inside the hardware boot gate,
completed bounded load on all processors, remained responsive during independent SSH probes,
and retained healthy built-in keyboard and Precision Touchpad devices. It is still an
experimental checkpoint: standalone cold boot, suspend/resume, long-duration thermal stress,
and GPU acceleration remain separate qualification gates.

### GPU (Apple AGX) -- experimental

The AGX is a Windows WDDM render adapter (`drivers/apple-agx`): a kernel-mode driver
that runs the AGX firmware queues, and user-mode drivers that compile and submit work
through the Asahi Gallium driver from Mesa.

- **Direct3D 11 at feature level 11_0** is the default: the accelerated DWM desktop and
  D3D11 applications render on the AGX (system GL/D3D smoke suites pass).
- **OpenGL** through the AGX OpenGL ICD: Counter-Strike 1.6 plays fullscreen (standard
  benchmark about 43 fps; vsync-capped at 60 Hz without the Steam overlay).
- **Direct3D 12** (in progress, opt-in profile): device creation at FL 11_0, buffers,
  textures, compute and graphics pipelines with root signatures and descriptor tables,
  depth, flip-model swapchain present, placed buffers, UAV clears, query heaps with GPU
  timestamps, copy/compute queues and ExecuteIndirect pass their hardware checkpoints.
  DXBC shaders only; DXIL (shader model 6) is the next major step.
- Not yet: MSAA, video decode/encode, power management, standalone (non-assisted) GPU boot.

Progress and every hardware experiment are recorded in
[investigation/GPU_CURRENT_STATE.md](investigation/GPU_CURRENT_STATE.md) and
[investigation/EXPERIMENTS.md](investigation/EXPERIMENTS.md).

Only `j313` is currently supported. This is not a general Apple Silicon Windows installer.

## Development branches

| Branch | Purpose | State |
| --- | --- | --- |
| `main` | Published platform baseline and documentation | Eight CPU cores, NVMe, USB, built-in keyboard and touchpad; AGX GPU (D3D11 FL 11_0, OpenGL, D3D12 in progress) as merged from integration |
| `integration/ad04-windows-compiler` | AGX GPU development (KMD, Mesa-based D3D10/11/12 and OpenGL UMDs, m1n1 GPU VA broker) | Active development; merged into `main` at milestones |
| `feature/j313-gpu-acceleration` | Earlier AGX GPU bring-up (first GPU-composed desktop) | Historical |
| `feature/j313-native-input` | Built-in SPI-HID keyboard and Precision Touchpad | Validated checkpoint, merged into `main` |
| `feature/j313-4e4p-cpu-stability` | Eight-core (4E+4P) bring-up | Validated, merged into `main` |
| `feature/j313-cpu-stability`, `feature/j313-4e2p-cpu-stability`, `feature/j313-4e3p-cpu-stability` | Incremental P-core bring-up (4E+1P … 4E+3P) | Historical validated checkpoints |
| `stable/j313-4e-baseline` | Frozen four-efficiency-core baseline | Historical recovery reference |
| `codex/*` | Earlier stability and release-preparation snapshots | Historical |

The submodule forks follow the same naming: [`m1n1_windows`](https://github.com/paulsmir/m1n1_windows)
and [`apple_silicon_platforms_mu`](https://github.com/paulsmir/apple_silicon_platforms_mu)
carry the matching GPU work on their own branches (currently `m2/pool-window` and
`m2/reserve-v3`), and the root branch pins the exact submodule commits it was tested with. Every hardware run on the
GPU branch is recorded in `investigation/EXPERIMENTS.md` and indexed in
`investigation/CHANGES.csv`.

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
- [Validated J313 eight-core and native-input checkpoint](documentation/STABLE_8CORE_INPUT.md)
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

The physical internal-panel handoff, full-panel 2560x1600 Windows framebuffer, all four
Icestorm and all four Firestorm guest CPUs, synthetic NVMe bridge, physical USB, and the
test-signed built-in Apple keyboard and Precision Touchpad driver have been validated together
in the assisted J313 checkpoint. The firmware-only AGX checkpoint is documented in
[`documentation/AGX_BRINGUP.md`](documentation/AGX_BRINGUP.md); GPU acceleration remains
an implementation milestone.

Native built-in input build, installation, diagnostics, and rollback are documented in
[`documentation/APPLE_INPUT.md`](documentation/APPLE_INPUT.md).

## Author and license

Original author: [paulsmir](https://github.com/paulsmir), 2026 -- the Windows on M1
bring-up and the Apple AGX WDDM drivers in this repository were first developed here.
This repository is licensed under the [MIT License](LICENSE); keep the copyright notice
when you reuse it.
