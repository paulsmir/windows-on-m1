# Windows 11 on the M1 MacBook Air — with its own Apple GPU driver

Native Windows 11 ARM64 on the 2020 M1 MacBook Air (`j313`, Apple T8103): no virtual machine,
Windows runs from the internal SSD with all eight CPU cores, the built-in keyboard and trackpad,
USB, and an **Apple GPU driver written for Windows from scratch**.

![GravityMark Direct3D 12, 1920x1080, 100,000 asteroids: score 5,396 on Windows 11 on an M1 MacBook Air](documentation/images/gravitymark-d3d12-fhd-5396.jpg)

> **Experimental.** This project exposes the internal SSD to experimental firmware and a custom
> NVMe bridge, and the GPU driver is development-grade. A wrong `diskpart` command can destroy
> macOS, Recovery, or the Asahi boot environment. Keep a complete backup and read the whole
> installation guide first. Only the 2020 M1 MacBook Air (`j313`) is supported.

## Where it stands (November 2026)

| Area | State |
|---|---|
| Boot | Windows 11 ARM64 boots from the internal Apple SSD through Project Mu firmware and a small m1n1-based hypervisor |
| CPU | all 8 cores (4 Firestorm performance + 4 Icestorm efficiency), with Windows efficiency classes |
| Storage | Apple ANS SSD exposed to Windows as an NVMe controller by the hypervisor |
| Input | USB, built-in keyboard and Precision Touchpad (own SPI-HID driver) |
| Display | internal panel through Apple's display coprocessor (DCP) |
| **GPU** | own WDDM driver: **Direct3D 12 at feature level 12_1**, Direct3D 11, OpenGL 2.1 (also for x86 apps under emulation), desktop composition (DWM) on the GPU |
| Benchmarks | GravityMark D3D12 runs at **~68 % of macOS Metal** on the same machine; 3DMark Night Raid completes; Counter-Strike 1.6 (OpenGL, x86) about 47 FPS in its timedemo |
| Standalone | basic system boots standalone; the GPU driver still needs the assisted (second-Mac) setup |

### GPU performance

GravityMark 1.89, Direct3D 12, 100,000 asteroids, temporal anti-aliasing off, windowed, compared
with macOS 15 + Metal **on the same MacBook Air**:

| Resolution | Windows (this driver) | macOS + Metal | Ratio |
|---|---|---|---|
| 1920x1080 | **5,396** (32.3 FPS) | 7,814 (46.8 FPS) | 69 % |
| 1280x720 | **6,769** (40.5 FPS) | 9,946 (59.5 FPS, near the 60 Hz cap) | 68 % |

![GravityMark 1920x1080 score by day, approaching the macOS reference](documentation/images/gravitymark-fhd-progress.svg)

One month earlier the same driver needed about 30 seconds to redraw the desktop. Recent gains came
from measuring the whole frame and removing waits between GPU jobs: giving compute work its own
firmware queue, keeping GPU address spaces resident, and compiler fixes.

### OpenGL: Counter-Strike 1.6

Counter-Strike 1.6 is a 32-bit x86 game; it runs under Windows' x86 emulation and draws through our
x86 OpenGL driver (Mesa, OpenGL 2.1) on the Apple GPU. In a fixed recorded-demo benchmark
(`timedemo`) it went from 40.5 FPS on its first measured run to about 47 FPS a few days later
(3,771 frames, mid-October 2026); live play on a server measured 37–42 FPS (median).

<details>
<summary>1280x720 result</summary>

![GravityMark Direct3D 12, 1280x720, 100,000 asteroids: score 6,769](documentation/images/gravitymark-d3d12-hd-6769.jpg)

</details>

## How it works

```
  Windows 11 ARM64
    ├─ D3D12 / D3D11 user-mode driver  — Mesa's Asahi compiler + DXIL→NIR translation
    ├─ WDDM kernel-mode driver         — talks to the Apple GPU firmware (RTKit) directly
    └─ ACPI-described platform         — CPUs, timers, interrupts, PCIe, USB, NVMe
  Project Mu UEFI firmware (Apple Silicon fork)
  m1n1-based EL2 hypervisor             — GICv3 emulation, NVMe bridge, GPU page-table broker
  Apple M1 (T8103)
```

- **Interrupts:** Windows requires an ARM GIC; Apple uses its own AIC, so the hypervisor emulates a
  GICv3 (routing, timers, inter-processor interrupts).
- **GPU memory:** Windows manages GPU memory in 4 KB pages, the Apple GPU uses 16 KB pages; the
  hypervisor keeps both page-table views consistent.
- **GPU work submission:** the kernel driver builds Apple firmware work queues the way Asahi Linux
  does; render and compute run on separate firmware queues.
- **Shaders:** DXIL from Windows applications is translated to NIR and compiled with Mesa's AGX
  backend, plus Windows-specific passes.

Every hardware run is recorded with its hypothesis, package hashes, and result in the investigation
ledgers on the development branches.

## Next steps

**Main goal: the same GPU performance as macOS on the same machine.** Measurements on macOS show
that Metal runs independent graphics and compute passes at the same time; Direct3D 12 allows the
same when the application declares no dependency between them, so that comes next, followed by
faster compute and shader code.

Then the rest of the laptop:

- **Storage speed:** the SSD is reached through an NVMe bridge in the hypervisor, which limits disk
  performance; rework it for much faster I/O.
- **Wi-Fi.**
- **External display.**
- **Sound.**
- **Keyboard backlight** (the keyboard itself works).
- **Built-in camera.**
- **GPU without the second Mac:** bring the GPU driver to the standalone boot path.
- More applications and games, then installable packages.

## Two operating modes

1. **Standalone mode:** iBoot loads an Asahi-provisioned boot entry, m1n1 starts the embedded Mu
   firmware, and Mu starts Windows from the internal SSD. No second computer is involved.
2. **Assisted development mode:** another Mac chainloads matching m1n1 and Mu builds over USB and
   captures UART, hypervisor logs, framebuffer updates, telemetry, and kernel-debug traffic. This
   mode can test firmware and driver changes without rewriting the Air ESP. The GPU driver currently
   runs in this mode.

## Documentation

- [Standalone installation](documentation/INSTALL.md)
- [Build and packaging](documentation/BUILD.md)
- [Standalone and assisted operation](documentation/RUN.md)
- [Launch profiles](documentation/CONFIGURATION.md)
- [Architecture](documentation/ARCHITECTURE.md)
- [GPU bring-up](documentation/AGX_BRINGUP.md)
- [Platform roadmap and milestone gates](documentation/ROADMAP.md)
- [Current stability checkpoint and iteration workflow](documentation/PLATFORM_STABILITY.md)
- [Validated J313 eight-core and native-input checkpoint](documentation/STABLE_8CORE_INPUT.md)
- [Validated J313 four-efficiency-core baseline](documentation/STABLE_4E_BASELINE.md)
- [Built-in Apple keyboard and Precision Touchpad](documentation/APPLE_INPUT.md)
- [Debugging and KD tools](documentation/DEBUGGING.md)
- [Known limitations](documentation/LIMITATIONS.md)
- [Engineering history](documentation/DEVELOPMENT_HISTORY.md)
- [Historical artifact provenance](documentation/history/ARTIFACT_PROVENANCE.md)
- [Changelog](CHANGELOG.md)

## Repository layout

- `m1n1_windows/` — pinned hypervisor fork submodule.
- `mu/` — pinned Apple Silicon Project Mu fork submodule.
- `drivers/` — Windows drivers: built-in input and the Apple GPU driver (the newest GPU work is on
  the [`integration/ad04-windows-compiler`](https://github.com/paulsmir/windows-on-m1/tree/integration/ad04-windows-compiler) branch).
- `scripts/` — build, ESP-install, standalone, and assisted-mode entry points.
- `tools/` — layout, packaging, and analysis tools.
- `config/j313-guest-layout.json` — canonical guest physical-memory contract.
- `run_uefi.py` — assisted guest launcher used for development.
- `tools/kd/` — Windows serial kernel-debug utilities.
- `extra/` — framebuffer, UART, and diagnostic helpers.

## Release truthfulness

Documentation distinguishes three states:

- **validated:** observed on the development J313;
- **implemented:** present in source and host-tested, awaiting the relevant hardware run;
- **planned:** not yet implemented.

Benchmark numbers above are single best runs on one machine with the experimental driver, measured
with identical GravityMark settings on Windows and macOS; run-to-run spread is about 1.5–2 %.

## Upstream projects

This work builds on [Asahi Linux](https://asahilinux.org/),
[m1n1](https://github.com/AsahiLinux/m1n1), [Mesa](https://mesa3d.org/),
[Project Mu](https://github.com/microsoft/mu), and the Apple Silicon Windows work from
[NT-for-ASi](https://github.com/NT-for-ASi). The active source forks are
[paulsmir/m1n1_windows](https://github.com/paulsmir/m1n1_windows) and
[paulsmir/apple_silicon_platforms_mu](https://github.com/paulsmir/apple_silicon_platforms_mu).
See the submodule histories and licenses for their respective copyrights and terms.

Developed with substantial help from AI coding assistants.
