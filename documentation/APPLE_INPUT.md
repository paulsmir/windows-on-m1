# Built-in Apple keyboard and trackpad

## Current status

The built-in J313 keyboard and trackpad are under active development and are
not part of the accepted stable platform baseline yet.

The following foundation is implemented:

- one versioned J313 resource contract generates matching m1n1, Mu ACPI, and
  Windows-driver constants;
- Mu publishes `ACPI\APPL0001\0` with the reviewed SPI3, AP-GPIO, nub-GPIO,
  and guest interrupt resources;
- m1n1 resolves the live ADT nodes and validates their compatible strings,
  translated register ranges, GPIO bindings, interrupt parent, and complete
  parent IRQ list;
- assisted and standalone launches use the same `hv_init` passthrough gate;
- after validation, m1n1 maps only the three reviewed MMIO regions and installs
  the level route `physical IRQ 330 -> guest INTID 865`;
- no m1n1 input emulation and no m1n1 SPI/GPIO data-path writes are used;
- the portable Apple SPI HID packet, CRC, reassembly, discovery, and bounded
  recovery core has host tests;
- an ARM64 KMDF resource-validation scaffold maps the three translated MMIO
  resources, performs bounded read-only SPI/GPIO register sanity checks, and
  unmaps every partial allocation on failure or release;
- the official pinned WDK NuGet toolchain builds the scaffold as a test-signed
  ARM64 PE, passes strict INF/catalog verification, and publishes the complete
  `.sys`/`.inf`/`.cat`/`.cer`/PDB package in GitHub Actions run `31697195976`;
- portable tests lock the Apple SPI register layout, FIFO depth, 200 ms maximum
  transfer deadline, clock-divider calculation, and GPIO pin/group offsets.

The driver still performs no register write, creates no interrupt object, and
publishes no VHF input device.

Consequently, external USB keyboard and mouse remain mandatory for recovery
and for all current Windows operation.

## Architecture

The final driver is native rather than a USB-emulation bridge:

1. m1n1 preserves platform state, maps the reviewed resources through stage 2,
   and translates the physical level interrupt into the guest GIC namespace.
2. Mu describes those resources as the `APPL0001` ACPI device.
3. The test-signed ARM64 KMDF function driver owns SPI3 and the two GPIO
   controllers, validates every translated resource before its first write,
   and performs bounded Apple SPI HID discovery.
4. VHF publishes the Windows keyboard and trackpad-facing HID collections.

Milestone 1 will provide the built-in keyboard plus basic pointer movement and
primary click. Milestone 2 is explicitly required and replaces the temporary
mouse frontend with a full Windows Precision Touchpad collection while keeping
the same hardware transport.

## Safety gates

Hardware testing must retain an external USB keyboard and mouse. Development
advances in this order:

1. read-only live ADT inventory;
2. ACPI enumeration and resource validation;
3. stage-2 mappings and level IRQ route;
4. read-only SPI/GPIO register sanity checks and ARM64 WDK package build
   (implemented; live devnode validation pending);
5. bounded GPIO reset and one SPI boot transaction;
6. descriptor discovery and transport-only packet capture;
7. VHF keyboard;
8. basic trackpad;
9. Windows Precision Touchpad.

Any mismatch in path identity, compatibility, MMIO range, GPIO binding, parent
interrupt list, or guest route disables input passthrough before the driver can
touch hardware. The guest platform remains bootable so external USB input can
be used to recover.

## Verification

Run the portable and platform tests from the repository root:

```sh
proxyenv/bin/python -m unittest discover -s tests -v
m1n1_windows/tests/run_host_tests.sh
```

Build the matching assisted diagnostic artifacts without installing them:

```sh
scripts/build-development.sh --display physical --debug monitor
```

The first hardware driver package is now reproducibly built and test-signed as
ARM64. Do not install it merely to obtain input: this checkpoint deliberately
performs only resource validation and read-only register sanity checks. Live
devnode validation remains the next reversible hardware checkpoint and must be
performed with external USB input attached.

The detailed design and implementation sequence are in
`documentation/design/2026-08-09-native-apple-input.md` and
`documentation/plans/2026-08-09-native-apple-input-implementation.md`.
