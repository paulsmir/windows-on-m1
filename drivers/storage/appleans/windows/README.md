# AppleAns physical StorPort driver

The default profile is a qualification stop: FindAdapter validates the supplied
resources and Initialize returns FALSE before broker mapping or controller access.
The opt-in native read-only implementation is described in [NATIVE_PATH.md](NATIVE_PATH.md).
Neither profile is installed or hardware-qualified by an offline build.

## Profiles

`build-offline.ps1` uses an existing ARM64 WDK-capable Visual Studio installation,
pins WDK 10.0.26100.0 for this build and disables inherited Directory.Build.props
and targets. It does not modify the toolchain, register a service or install a
certificate. Code Analysis and warnings-as-errors are enabled; automatic signing
is off. Outputs and intermediates are separate per profile.

- Default: resource-only qualification, no mapped broker or hardware operation.
- `-NativeReadOnly -QualificationStop 1`: QUERY/CLAIM, stop before controller boot.
- `-NativeReadOnly -QualificationStop 2`: admin boot, stop before Identify.
- `-NativeReadOnly -QualificationStop 3`: both Identify commands, then stop.
- `-NativeReadOnly -QualificationStop 0`: full read-only candidate, not admitted to hardware.

Each intended qualification stop obtains a real release receipt before returning
failure to Storport. Unproven DMA cleanup uses terminal EL2 containment; retaining
a pointer in an extension is not enough to prevent Windows recycling DMA memory.
The native path never copies READ payload and never allows data WRITE commands.
Payload-free FLUSH is supported with actual completion and truthful shutdown.

## Package

AppleAns.inf binds ACPI\APPL0003 in class SCSIAdapter. Service AppleAns uses
AppleAns.sys, StartType=0 and load-order group SCSI Miniport. The project is
ARM64 Release, physical WDM/Storport, no KMDF, WDK 10.0.26100.0, with the normal
GsDriverEntry and Windows 11 24H2 ARM64 (`10_GE_ARM64`) catalog target.

The originally requested APPLANS0 failed the ACPI compiler because its final four
characters are not hexadecimal; APPL0003 is the corrected canonical identifier.
See [ACPI 6.4 section 6.1.5](https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/06_Device_Configuration/Device_Configuration.html#hid-hardware-id).

`sign-offline.ps1` requires an explicitly selected existing code-signing certificate.
It signs SYS and catalog, recreates the exact-target catalog, and runs Authenticode
and catalog-membership verification. It never imports a certificate or installs a
driver. A test signature is not a production/kernel-policy signature.

## Tests and historical evidence

`tests/run.sh` runs portable ASan/UBSan transport, Identify and read-only request
packing, resource and [read-only telemetry](TELEMETRY.md) tests. `../common/tests/run.sh` covers SCSI, SG/PRP and SIMPLE/ORDERED/HEAD
lifetime/order contracts. The actual Windows DDI path requires separate frozen
source, pinned ARM64 WDK build and Code Analysis gates.

ANSBUILD001-H applies only to the earlier qualification skeleton. That candidate
passed actual ARM64 compilation, DriverMinimumRules Code Analysis, InfVerif and
catalog generation. It imported only StorPortInitialize, StorPortNotification and
StorPortLogError. The source/package/hashes/logs are preserved in the parent local
build-host directory and experiment ledger. These results do not automatically
validate the newer native implementation or its additional DDIs.

The existing WDK test certificate E9BE15BD2A184BFABA0C8035B3C620C58037A241 could sign
the H files, but `/pa` and `/kp` verification failed with UntrustedRoot. Read-only
inspection of CurrentUser and LocalMachine My stores found no other certificate
with a private key and Code Signing EKU. No trust-store change was made. This gate
remains pending until explicitly resolved; no source/build success implies it passed.
