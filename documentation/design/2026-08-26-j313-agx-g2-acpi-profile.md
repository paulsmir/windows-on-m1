# J313 AGX G2 Opt-In ACPI Profile

## Purpose

Publish a Windows-visible Apple AGX device only in an explicit development
firmware profile.  This gate proves enumeration and exact resource delivery;
it does not start GPU firmware, map a GPU address space, install the KMD, or
change the stable Windows profile.

## Profile isolation

The normal J313 DSC/FDF build keeps its current bytes and contains no AGX SSDT.
The development build is selected with the EDK II command-line macro
`J313_AGX_G2_PROFILE=TRUE`.  Only that build adds a separate ACPI-table module
and packages its SSDT into the firmware volume.  A separate SSDT is preferred
over a conditional block in `DSDT.asl`: the stable DSDT remains unchanged and
the candidate table can be extracted, disassembled, and rejected independently.

The candidate image and its manifest must carry an explicit `agx-g2` profile
identity.  The stable recovery image is never overwritten by this task.

## ACPI contract

The SSDT creates `\_SB.AGX0` with:

- `_HID` equal to `APPL0002`;
- `_UID` equal to zero;
- `_CCA` equal to one;
- `_STA` equal to `0x0F` only because the whole SSDT is absent outside G2;
- one non-cacheable read/write `QWordMemory` consumer covering exactly
  `0x204000000..0x207FFFFFF` (`0x04000000` bytes);
- nine exclusive, level-sensitive, active-high interrupts with guest GSIVs
  `880..888` in the generated-contract order;
- `_DSD` properties containing contract version `1`, the immutable source
  contract SHA-256, firmware generation `G13`, and firmware version `V13_5`.

Nine interrupts are intentional.  The earlier parent plan said "one
level-sensitive interrupt", but the accepted G1R evidence, generated G2
contract, and fail-closed Windows resource parser all require the nine reviewed
routes.  Publishing only one would make the ACPI and KMD contracts disagree.

All ASL literals are generated from `config/j313-agx-g2.json`; hand-maintained
copies of addresses, interrupt IDs, versions, or hashes are forbidden.

## m1n1 boundary

An `agx-g2` launch contract explicitly permits the single SGX aperture and the
nine physical-to-guest interrupt routes from the generated contract.  The
default/stable contract contains neither permission.  Candidate launch fails
before Mu if the profile identity, generated-contract hash, MMIO aperture, or
any interrupt route differs.

This task changes policy only.  m1n1 does not start AGX, interpret commands,
forward submissions, or inject a synthetic completion.  Physical interrupts
remain masked until the later firmware-ownership task installs a reviewed
handler.

## Windows qualification

Host gates must prove:

1. the stable FDF/DSC does not package the AGX SSDT;
2. the G2 build packages exactly one AGX SSDT;
3. generated ASL matches the immutable JSON contract;
4. `iasl` compiles and disassembles the table without warnings or resource
   drift;
5. the KMD resource parser accepts the generated resource fixture and rejects
   missing, duplicate, reordered, edge-triggered, or out-of-range resources;
6. all stable recovery hashes remain unchanged.

Only after those gates pass may a preregistered enumeration experiment boot the
G2 firmware.  Windows must show `ACPI\APPL0002` with the exact resources while
`AppleAgx.sys` remains absent from the Driver Store.  Basic Display, native
keyboard, Precision Touchpad, NVMe, USB, shutdown, and the stable boot profile
must remain unchanged.

## Failure and rollback

Any mismatch prevents candidate construction or launch.  Enumeration failure
does not trigger driver installation.  Rollback is a cold boot of the immutable
stable image; no Windows driver removal is required because this task installs
none.
