# Architecture

## Component boundaries

The root repository coordinates two pinned forks:

- **m1n1_windows** runs at EL2, initializes Apple hardware, constructs stage-2 mappings,
  virtualizes GIC state, exposes synthetic PCI/NVMe, routes physical USB, and optionally
  provides the proxy/debug transports.
- **Project Mu fork** runs as the guest firmware, publishes ACPI and GOP, initializes USB
  during firmware boot, and selects the Windows ARM64 fallback loader.
- **Windows** uses inbox PCI, `stornvme`, xHCI, hub, HID, Basic Display, and boot-manager
  components. No custom Windows storage or USB driver is required by the current design.

## Boot paths

Standalone `boot.bin` appends a versioned manifest and compressed Mu FD after the native
m1n1 payload. m1n1 validates magic, version, bounds, alignment, decompressed size, layout,
and CRC before guest setup. A malformed image remains recoverable through the proxy path.

Assisted mode performs the same setup from `run_uefi.py`; it is intentionally retained as a
reference implementation and instrumentation surface.

## Guest physical memory

Apple DRAM begins above the low addresses expected by Windows Boot Manager. The hypervisor
therefore exposes a pinned guest physical layout plus a low-IPA alias backed by real high
DRAM. Mu reserves firmware, ACPI, framebuffer, and other fixed windows so Windows cannot
allocate them.

Device models must translate guest IPA to host physical addresses through the live stage-2
tables before accessing queue memory. Directly treating an IPA as a CPU pointer worked only
for accidentally identity-mapped ranges and caused crashes once Windows allocated queues in
the low alias.

## vGIC and CPUs

Mu describes a GICv3 topology in MADT. m1n1 emulates distributor, redistributors, system
register behavior, SGIs, PPIs, SPIs, list-register queuing, EOI, and maintenance behavior.
PSCI starts secondary vCPUs with their complete launch context.

The validated firmware describes all eight T8103 cores and distinguishes the four Icestorm
efficiency cores from the four Firestorm performance cores through ACPI topology and
efficiency-class data. Windows watchdog failures exposed missing SGI/list-register behavior;
they were not solved by merely advertising more processors.

## Synthetic PCI and physical NVMe

Windows sees an ACPI PCI root and a synthetic class `010802` NVMe endpoint. ECAM and BAR
accesses trap into m1n1. The controller implements admin and I/O queues, Identify, namespace
geometry, Read, Write, completions, and interrupt delivery.

The namespace backend is the physical Apple ANS storage device. Queue entries and data PRPs
are translated through stage-2. The current implementation favors correctness and performs
storage operations synchronously, which explains throughput far below native macOS.

The ECAM and BAR trap pages must be installed after broad hardware mappings or explicitly
reserved from later mapping passes. Earlier code installed the hooks and then replaced them
immediately before guest entry, making the device disappear from Windows.

## USB xHCI handoff

Mu's Apple USB Type-C bring-up initializes PHY, power, xHCI, and DART state. For Windows,
xHCI is described as an ACPI device with MMIO and a guest SPI. m1n1 preserves the DART
bypass/configuration across `ExitBootServices`, routes the physical AIC interrupt into the
virtual GIC, and handles level-triggered mask/EOI semantics.

The proxy USB controller remains owned by m1n1; the other controller is handed to Windows.
This separation allows physical keyboard, mouse, hub, and installation-media traffic while
the debug cable remains attached to the host.

## Display

Mu publishes GOP over a reserved framebuffer. Development mode uses a fixed 1280x800,
32-bit B8G8R8X8 RAM buffer. Windows Boot Manager and Basic Display write pixels without
knowing that the buffer is observed remotely.

m1n1 streams complete framebuffer generations asynchronously over the existing proxy event
loop. Chunks are ordered and checksummed; only a complete generation replaces `fb.raw`.
Backpressure skips observer work rather than blocking Windows.

Standalone physical-display support reuses an iBoot/DCP framebuffer behind the same GOP
contract. Its code path is implemented but remains awaiting validation on a J313 with a
working internal panel.

## Virtual UART and KD

The second USB ACM endpoint is a virtual PL011-like guest UART. During firmware boot it
carries Mu console output. Windows can use the same transport for KDCOM. Focused host tools
implement just enough of the KD packet protocol to inspect liveness, processes, drivers,
PnP devnodes, ACPI, stacks, bugchecks, and reboot state.

## Automatic Windows selection

Mu's `WindowsAutoBootDxe` scans non-removable block devices for
`\EFI\BOOT\BOOTAA64.EFI`. It does not depend on changing `BootOrder`, `BootNext`, or a
volatile `FSn:` alias. If no internal fallback loader is found, normal BDS and the UEFI
shell remain available for installation or recovery.
