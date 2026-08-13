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
- an ARM64 KMDF resource-validation scaffold exists, but it does not yet
  initialize SPI3 or publish VHF input devices.

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
4. read-only SPI register sanity checks;
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

The first hardware driver build is test-signed ARM64. Build and install it only
after the corresponding implementation checkpoint supplies exact commands;
the current resource-only scaffold is not useful to install.

The detailed design and implementation sequence are in
`documentation/design/2026-08-09-native-apple-input.md` and
`documentation/plans/2026-08-09-native-apple-input-implementation.md`.
