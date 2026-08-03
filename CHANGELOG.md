# Changelog

## Unreleased — public J313 development release

### Validated on hardware

- Windows 11 ARM64 deployed manually to previously unallocated internal-SSD space.
- Synthetic NVMe enumerated through PCI, bound to `stornvme`, and accessed the physical
  Apple ANS namespace for boot and normal filesystem I/O.
- Mu GOP displayed firmware, Setup, bugchecks, OOBE, and the Windows desktop through the
  assisted virtual framebuffer.
- Physical xHCI accepted installation media, hubs, mouse, and keyboard.
- Windows completed local-account OOBE, reached the desktop, accepted RDP, and ran long
  enough to install application software.
- All four efficiency and four performance cores were exposed in the latest assisted SMP
  configuration.

### Implemented and host-tested

- Canonical generated J313 guest-memory layout shared by Python, m1n1, and Mu.
- Versioned, CRC-checked, compressed standalone `boot.bin` format.
- Native autonomous m1n1 guest preparation and a three-second debug-host maintenance window.
- Reversible, explicit-target macOS ESP installer with atomic replacement and SHA-256.
- Location-independent assisted build, launch, reset, log, and display scripts.
- Complete English installation, operation, architecture, debugging, limitation, and
  engineering-history documentation.

Standalone hardware validation pending for the current packed image. Physical internal-panel
handoff also remains unvalidated on a J313 with a working panel.

### Known performance observations

- Physical-NVMe sequential read measured approximately 300 MB/s through the current
  synchronous bridge.
- Windows reports 8 GB installed while approximately 5-6 GB is practically available after
  fixed guest and hypervisor reservations.
