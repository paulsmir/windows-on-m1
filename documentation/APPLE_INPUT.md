# Built-in Apple keyboard and trackpad

## Current status

Native J313 input is under active development and is not part of the accepted
stable platform baseline. External USB input remains mandatory for every
hardware test and recovery operation.

The current feature branch implements the complete software path for the
built-in keyboard and a hardware-gated Precision Touchpad candidate:

- one versioned J313 resource contract generates matching m1n1, Mu ACPI and
  Windows-driver constants;
- Mu publishes `ACPI\APPL0001\0` with the reviewed SPI3, AP-GPIO, nub-GPIO and
  guest interrupt resources;
- m1n1 validates the live ADT identities, register ranges, GPIO bindings and
  interrupt route before mapping only those reviewed resources;
- the ARM64 KMDF driver owns the bounded Apple SPI HID transport and its
  level-triggered interrupt path;
- keyboard and trackpad HID descriptors are copied into fixed driver-owned
  storage before the discovery reassembly buffer can be reused;
- a bounded HID parser derives exact input-report sizes from the hardware
  keyboard descriptor;
- descriptor diagnostics expose only length, SHA-256 and parser status, never
  descriptor bytes, key values or report payloads;
- a VHF keyboard frontend publishes the exact hardware descriptor and accepts
  only reports matching the parsed contract;
- VHF start, submission and synchronous teardown are behind a fail-closed
  `TransportOnly` service parameter.

The package default is `TransportOnly=1`. Therefore installing the package in
its default mode must not create a VHF keyboard child. Keyboard publication is
enabled only by the explicit `-PublishKeyboard` installer switch after the
transport-only hardware gate succeeds.

The VHF keyboard path has passed its live J313 gate and has been used at Windows
sign-in. The trackpad software path now implements the full Windows Precision
Touchpad collection; there is no temporary basic-mouse frontend. Trackpad
publication remains disabled by default until its independent live gate passes.

The live 110-byte Apple trackpad descriptor contains only a relative mouse
collection and vendor reports, so it cannot supply the absolute geometry that
Windows Precision Touchpad requires. The driver instead follows the native
Apple contract used by Asahi: it requests sensor dimensions through feature
report `0xd9`, validates width, height and signed X/Y bounds, converts the
physical dimensions from hundredths of a millimetre to hundredths of an inch,
and only then permits the VHF touchpad to start. A missing or malformed response
fails closed while leaving the already validated keyboard path independent.

## Architecture

The implementation is native rather than a USB-emulation bridge:

1. m1n1 preserves platform state, maps the reviewed resources through stage 2
   and translates physical IRQ 330 to guest INTID 865.
2. Mu describes those resources as the `APPL0001` ACPI device.
3. The KMDF function driver owns SPI3 and the two GPIO controllers, performs
   bounded discovery, validates descriptor-derived keyboard reports and obtains
   trackpad geometry through Apple's bounded `0xd9` feature exchange.
4. VHF publishes Windows-facing HID collections without changing the hardware
   transport or exposing input payloads to diagnostics.

No VHF function is called from the ISR. Report publication runs from the
passive transport worker. PnP teardown stops new submissions, synchronously
deletes the VHF object and only then releases MMIO mappings.

## Software verification checkpoint

The accepted software-only checkpoint is commit
`bb426e00ee683be17fd8872cbce050e8db56a58b` on
`feature/j313-native-input`.

Local verification:

```sh
proxyenv/bin/python -m unittest discover -s tests -v
m1n1_windows/tests/run_host_tests.sh
```

The public suite passed 286/286. The nested m1n1 host suite also passed before
the CI submission.

Official WDK verification:

- workflow: `Apple input ARM64 WDK`;
- run: [32705632141](https://github.com/paulsmir/windows-on-m1/actions/runs/32705632141);
- job: `97366009946`;
- artifact: `AppleInput-ARM64-Debug`;
- artifact type: unsigned development package;
- package default: `TransportOnly=1`;
- hardware status: not installed and not hardware validated at this checkpoint.

Verified ARM64 files and SHA-256:

| File | SHA-256 |
| --- | --- |
| `AppleInput.sys` | `bc457c288cef25eeb1445305629ffb9f8147b7beaf1d7d258c5cc81a2de6104e` |
| `AppleInput.inf` | `0f74306484403b97b81ad1350488cbed7a1af000b8c7d7e4f793cfe1101fe67d` |
| `appleinput.cat` | `2adf691aab8f2252601bb6f55dff4bf15c29f52eefc76de058d49e604f95251c` |
| `AppleInputDiag.exe` | `2e060e2bb050baf6b2a1ccd889f9245d0a4754417e530de162a83fbe434490b8` |

Both `AppleInput.sys` and `AppleInputDiag.exe` were independently identified as
PE32+ AArch64 binaries after artifact download. The package needs the documented
Windows test-signing workflow before installation; the CI artifact is not a
production-signed driver.

## Hardware gates

Hardware testing must retain an external USB keyboard and mouse. The remaining
gates are deliberately separate:

1. **Gate C1, transport only:** install with `TransportOnly=1`; require repeated
   phase-8 snapshots, keyboard descriptor length 182, trackpad descriptor
   length 110, stable nonzero descriptor digests, a valid keyboard report
   contract, VHF state absent and no new HID child.
2. **Gate C2, keyboard publication:** install the exact same package with the
   explicit `-PublishKeyboard` switch; require a VHF keyboard child, correct key
   make/break behavior, zero rejected/submission-failure counters and clean
   disable/enable plus reboot teardown.
3. **Precision Touchpad publication:** first install the exact candidate with
   `PublishTrackpad=0` and require valid `0xd9` axis diagnostics. Only then set
   `PublishTrackpad=1`; require the Precision Touchpad child, Windows feature
   negotiation, controlled one- and two-finger input, clean disable/enable and
   cold boot without a transport, VHF or bugcheck failure.

Any boot regression, descriptor mismatch, changing digest, parser rejection,
transport timeout, CRC/fragment/offline counter, bugcheck or loss of external
recovery input fails the active gate and requires rollback.

Gate C2 has reached a successful partial hardware checkpoint on J313. The
built-in keyboard was used at Windows sign-in; six observed reports were all
accepted and submitted through a running VHF frontend; and the parent, VHF and
keyboard children returned `OK` after a devnode restart. No transport or VHF
failure counter increased. Physical typing after that restart, a controlled
reboot and the 30-minute mixed-input stability run are still required before
the gate is called complete.

## Design and implementation records

- `documentation/design/2026-08-24-vhf-keyboard-precision-touchpad.md`
- `documentation/plans/2026-08-24-vhf-keyboard-implementation.md`
- `documentation/design/2026-08-09-native-apple-input.md`
- `documentation/plans/2026-08-09-native-apple-input-implementation.md`
