# Engineering history

This document is a technical reconstruction of the bring-up. It describes observed
failures, evidence, root causes, implemented changes, and remaining limits. It intentionally
does not reproduce chat transcripts or attribute intent to earlier developers.

## Baseline

The starting Mu and m1n1 forks contained substantial Apple Silicon enablement, but their
J313 Windows path was not a complete machine contract. Firmware could enter a shell and
Windows loaders could execute, while interrupt virtualization, platform ACPI packaging,
storage queues, physical USB handoff, display, and multi-core behavior were incomplete or
disabled in the selected configuration.

The earliest work therefore separated three kinds of failure that were easy to conflate:
code absent from the source, code present but excluded from the selected build, and code
built correctly but invalid for firmware running as an EL2 guest. Each category required
different evidence.

## Establishing a reproducible Mu entry

The first guest firmware attempts failed before useful UEFI output. Experiments ruled out
the PE/COFF machine type and basic load address, then exposed three concrete contracts:

- the assembly entry needed a valid stack before calling C;
- boot arguments had to arrive in the register expected by the selected entry code;
- Mu PCDs containing firmware, boot-argument, and ADT addresses had to match the pinned
  placement selected by the launcher.

`run_uefi.py` stopped inheriting a moving heap-derived address and began validating the
predicted placement against the address selected after `load_raw()`. This converted an
intermittent early crash into reproducible firmware entry and made placement errors fail
before the guest ran.

## Build selection and firmware-volume packaging

Several necessary source files already existed in the upstream-derived trees but were not
part of the active Air DSC/FDF or selected backend. Findings included a MacBook Air family
package absent from the chosen platform, ACPI tables and serial-console code built but not
packaged into the active firmware volume, an internal-shell variant disabled in policy, and
Apple USB controller counts absent from the Air configuration.

The durable fix was not to duplicate those implementations. The J313 platform was made to
include and configure the intended modules, and every claim was checked against the linked
m1n1 ELF or Mu `FVMAIN.Fv`. The current build guide therefore treats binary inspection as
part of bring-up rather than trusting a successful compiler exit.

## GICv3 and Windows kernel entry

Early Windows boots reached `winload.efi` and then failed immediately in the kernel. Source
and binary inspection showed that required ICC system-register behavior and parts of the
vGIC path were missing from the active build. The fork implemented distributor and
redistributor state, interrupt injection, EOI, SGI/PPI/SPI handling, list-register queuing,
and maintenance behavior required by Windows.

The key lesson was to verify the linked binary, not just the source file. Code present in a
tree but excluded by comments, configuration, or platform selection has no runtime effect.

Apple Silicon did not provide the hardware virtual-GIC behavior expected by the inherited
path, so merely advertising GICv3 did not deliver interrupts. Earlier GICv2 CPU-interface
emulation was useful for characterizing register access, but the durable Windows path became
software-managed GICv3 state. Defects exposed during that transition included a zero virtual
priority mask, missing trap controls, and required ICC handlers inside an inactive block.

The timer was measured against host timestamps because guest logs cannot validate the clock
that timestamps them. Apparent timer-rate explanations were rejected after direct
measurement; later work focused on injection, latching, and duplicate pending state rather
than applying an arbitrary frequency correction.

## J313 Mu platform and ACPI delivery

Several failures that looked like Windows driver problems were actually firmware packaging
problems: the intended DSDT/MADT/MCFG or driver was not in the active J313 firmware volume.
The J313 platform configuration was made explicit, the virtual GIC backend was selected for
guest firmware, and compiled tables were checked in `FVMAIN.Fv` rather than searched only in
the compressed outer FD.

ACPI evolved to describe the virtual PCI root, GICv3 CPU topology, generic timer, debug UART,
xHCI, and CPU efficiency classes. Live-table validation through KD was added because source
ASL does not prove that Windows received the expected table.

## Boot media, low memory, and ExitBootServices

Large WinPE media could not be embedded in the firmware volume: PrePi decompresses that
volume as a whole and failed with resource exhaustion. Assisted mode therefore placed an
external image in a dedicated guest-RAM window with a small header consumed by a Mu block
driver. Physical USB later replaced this temporary installation-media path.

Windows Boot Manager also requested pages at guest physical addresses below Apple DRAM. A
low-IPA window backed by reserved high DRAM was added and described consistently to Mu.
Reaching `ExitBootServices` then proved that boot services, ACPI, timers, and the loader's
memory-map transition were coherent enough for the kernel to take ownership.

This phase established a recurring rule: every fixed buffer must be reserved in both the
hypervisor stage-2 layout and Mu's UEFI memory map. Mapping only one side creates either a
guest abort or silent page reuse and corruption.

## PCI ECAM visibility

The synthetic PCI endpoint initially disappeared even though ECAM handlers existed. A later
broad stage-2 hardware mapping replaced the fine-grained ECAM trap immediately before guest
entry. The fix made hook ordering explicit and reserved trap pages from replacement. Mu and
Windows then enumerated `VEN_1B36&DEV_0010` as an NVMe-class device and bound `stornvme`.

## NVMe controller completion

The inherited device model exposed registers but did not implement a Windows-usable NVMe
controller. The fork added controller enable/ready state, admin and I/O submission/completion
queues, Identify Controller/Namespace, feature negotiation, Read, Write, CQE production,
doorbells, and interrupt delivery.

UEFI enumeration proved register-level progress, while the Windows PnP tree exposed the next
failure as `FAILED_START`. Instrumentation around CQE creation and vGIC injection isolated
the missing completion-interrupt path.

## IPA-to-PA translation

Queue and PRP addresses supplied by the guest are IPAs. Treating them as physical CPU
addresses happened to work in identity-mapped areas and crashed or corrupted memory once
Windows allocated through the low-memory alias. A stage-2 walker, `hv_ipa_to_pa()`, was added
and applied to all guest-memory access sites. Untranslatable addresses now fail the command
instead of crashing EL2.

## Physical ANS backend

The virtual namespace was connected to the physical Apple ANS SSD. Both GPT copies and
partition entries became readable through the synthetic controller, proving end-to-end
queue, translation, media, and completion behavior. The synchronous implementation was kept
for determinism; the resulting throughput is approximately 300 MB/s and remains a known
optimization target.

## Virtual GOP and remote framebuffer

With a physically unavailable internal panel, visual progress required a display independent
of DCP. Mu gained a simple GOP over a reserved 1280x800 B8G8R8X8 RAM buffer. Windows Boot
Manager, Setup, bugcheck UI, OOBE, and Basic Display could then draw into ordinary guest RAM.

m1n1 gained asynchronous framebuffer events with chunk ordering, generation IDs, CRC, atomic
publication, and pacing. Observer backpressure skips a frame rather than blocking Windows.
The same GOP abstraction is designed to accept an iBoot/DCP framebuffer later.

An early synchronous screenshot path interfered with timer and proxy progress. The durable
stream uses the existing proxy event loop rather than a competing reader. It never issues a
nested proxy request from a framebuffer callback because doing so interleaves request/reply
packets and recreates command-mismatch failures.

## USB xHCI and DART

Review of neighboring Mu platforms showed that Apple Type-C and xHCI firmware support
already existed but was not enabled consistently for the Air. The J313 build enabled the
firmware chain, then exposed xHCI to Windows through ACPI. m1n1 preserved the DART state and
routed the physical level interrupt from AIC into a guest SPI with correct mask/EOI mapping.

The development machine then accepted a hub, installation flash drive, separate Logitech
receivers, mouse, and keyboard. Intermittent input failures demonstrated why USB bus activity
alone is not proof of correct interrupt delivery.

## Manual Windows deployment

Graphical Windows Setup rejected the experimental machine requirements and was unstable when
re-entering Command Prompt. It was retained only as a convenient WinPE launcher. The actual
installation used DiskPart to create MSR, Windows ESP, and NTFS partitions in preallocated
free space; DISM applied an edition from `install.wim`; BCDBoot created the BCD; and
`bootmgfw.efi` was copied to `\EFI\BOOT\BOOTAA64.EFI`.

The local-account OOBE path used `start ms-cxh:localonly`. This avoided dependence on an
unreliable network/Microsoft-account stage.

## Automatic Windows boot

UEFI filesystem aliases changed between boots, and writable UEFI variables were not a
reliable policy store. `WindowsAutoBootDxe` therefore scans internal block devices for the
architecture fallback loader. This removed repeated manual `FS3:` commands and preserved
the UEFI shell when no installed Windows loader exists.

## SMP and watchdog work

Bring-up proceeded from one efficiency core to four efficiency cores and then all eight
cores. Secondary launch required complete PSCI context, per-vCPU redistributor state, SGI
delivery, timer handling, and correct CPU topology/efficiency data in ACPI. Missing behavior
manifested as long boots, UI freezes, `IPI_WATCHDOG_TIMEOUT`, and `CLOCK_WATCHDOG_TIMEOUT`.

Timer/list-register coalescing and queued interrupt retry removed several immediate failures.
Windows ultimately reached the desktop with eight CPUs and sustained an RDP session long
enough to install software, although long-run stability remains experimental.

## Diagnostic discipline

The project accumulated focused tools rather than relying on a single visual signal:

- host-timestamped virtual UART;
- live hypervisor logs;
- atomic framebuffer generations;
- proxy and device counters;
- Windows KD liveness, PnP, module, ACPI, process, thread, stack, bugcheck, and reboot tools.

This distinguished a stale browser frame from a stopped KD target, a device interrupt stall,
a proxy desynchronization, and a real Windows watchdog.

Proxy recovery itself required discipline. Terminating `run_uefi.py` does not terminate an
already running EL2 guest, and a second proxy reader can consume replies intended for the
first. Public scripts now refuse a second runner, open the virtual UART before firmware
entry, and use firmware reset or KD reboot where possible. Event framing and checksum work
reduced observer corruption, but single proxy ownership remains the supported model.

## Standalone packaging

The assisted Python path proved the machine contract but required another Mac. The next
stage moved the fixed layout into generated shared data, added a versioned packed-image
manifest, embedded compressed Mu after m1n1, implemented the native guest preparation stages,
and added a bounded debug-host maintenance window.

The image parser, layout generation, native boot state machine, build, and reversible ESP
installer are host-tested. The current image has not yet completed its final physical
cold-boot validation, so standalone is described as implemented rather than validated.

## Standalone CPU1 toolchain control

The first passive standalone monitor capture reset while Windows issued PSCI `CPU_ON` for
CPU1. The GCC 13.3.0 image published the guest entry but raised an EL2 synchronous exception
before `HV: Entering guest secondary 1`. This left two variables relative to the successful
assisted path: compiler code generation and execution directly from the first m1n1 entered
by iBoot.

A controlled image rebuilt every m1n1 C and assembly object with Homebrew Clang 22.1.8,
while retaining the same m1n1 sources, Mu firmware, guest layout, and physical/monitor launch
profile. Its immutable identifiers were:

- `boot.bin` SHA-256: `f6df78592dc4c6b395c99e9cc6a6cd961e59001282355532adad74fcc9239ef1`;
- Mu FD SHA-256: `64763cc61e0fdba693438386ea2125d3fe750ee1c1ff8845b8d62f63e7ea462a`;
- m1n1 commit: `c6dc965a4d3312e4aa437835236e6b0e98c32c16`;
- manifest flags: `0x11` (`display=physical`, `debug=monitor`).

Two consecutive cold-boot generations reproduced the same failure:

```text
PSCI DEBUG: turning on CPU1 MPIDR: 0x1
HV: Initializing secondary 1
HV: Secondary 1 published entry=... x0=0x10e000
Exception: SYNC
MPIDR: 0x80000001
PC: ... (rel: 0x12d54)
ESR: 0x2000000 (unknown)
L2C_ERR_STS: 0x11000ffc00000000
Unhandled exception, rebooting...
```

The exact Clang ELF maps relative PC `0x12d54` to `display_configure()` at the assertion on
`display.c:580`; `hv_init_secondary()` is at relative address `0x16cb8`. CPU1 therefore did
not reach the function submitted by `smp_call4()`. The PC may be where an L2/context error
was reported rather than its original cause, so this result does not yet prove corruption of
the spin-table target. It does prove that GCC code generation is not the trigger and that the
failure precedes Windows guest entry on CPU1.

The remaining controlled difference is startup context. Assisted boot first uses `P_VECTOR`
to enter a fresh m1n1 image and then initializes the hypervisor; direct standalone performs
display, payload, and hypervisor work in the first m1n1 entered by iBoot. The next checkpoint
is therefore a native Stage 0 self-chainload that preserves the same inner image and Mu
firmware, then verifies CPU1 from a fresh Stage 1 context before any shared-engine refactor.
