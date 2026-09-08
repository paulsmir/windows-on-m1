# AppleAgx recovery playbook

## Golden checkpoint

`J313-GOLDEN-20260903` is the accepted clean Windows development baseline.
It is not a GPU-driver result.  Its platform is the matching EXP377 m1n1
artifact (`fae3444cc289cf52ea12b81b9db8f3d8bf24bd084f899a751321d2048d9a525a`)
with accepted Mu (`e8312e967604dbbac4780a50ed37dea98a25aea45cbfc8636535fad04c3be074`),
physical display and diagnostics off.  The guest is launched assisted-only;
never install a standalone image for development recovery.

Launch sequence:

1. Return the Air to `Running proxy` and issue a full m1n1 reset.
2. Chainload `.local/experiments/EXP-20260903-377-secondary-cpu-receipt/assisted-boot/m1n1.macho`.
3. Launch `.local/experiments/EXP-20260831-241-nvme-level-intx/assisted-boot/J313_EFI.fd`
   with `--display-mode physical --debug-mode off --low-mem`.
4. Confirm Windows SSH, eight CPUs, `APPL0002=OK`, AppleInput, NVMe, xHCI and
   `Event129=0` before any driver package is changed.

## Path A — online cleanup (default)

Use only when Windows is bootable and administrative SSH is available.

1. Run `scripts/cleanup-appleagx-online.ps1` without `-Execute` for a
   read-only identity check.
2. It must find zero or exactly one `AppleAgx.inf` package, and the exact
   APPL0002 binding must agree with that package.  Any ambiguity is a stop.
3. Run it with `-Execute` only after that check.  It disables the exact bound
   devnode, removes that exact INF with `pnputil`, removes only a stopped stale
   `AppleAgx` service, rescans PnP, and writes a receipt.
4. Pass criteria: APPL0002 present, AppleAgx INF/service/module absent, no
   stale binding, 8 CPUs, and SSH/AppleInput/stornvme/USBXHCI healthy.

Target time: under five minutes.  Do not use WinPE if this path works.

## Path B — offline cleanup (only when Windows cannot boot)

Use the prebuilt recovery WinPE image; do not rebuild it per failed package.
Its cleanup script must locate exactly one Windows installation and exactly one
AppleAgx package, save durable receipts, remove only that package/service state,
and reboot.  If either identity is ambiguous it must fail closed.

The current reusable image is
`.local/experiments/EXP-20260903-378-offline-appleagx-boot-recovery/winpe-exp378.img`.
Before the next boot-breaking test, its packaged command must be revalidated
against the current generic cleanup script and its SHA-256 recorded in that
experiment's preregistration.

Target time: under 10–15 minutes.  If this fails, restore the golden system
image instead of beginning a new recovery investigation.

## Golden data

The VSS snapshot set lives on Air at
`C:\Users\pavel\J313-GOLDEN-20260903\vss-20260903T124740Z`. It contains
SYSTEM/SOFTWARE/SAM/SECURITY/DEFAULT with LOG1/LOG2, pavel's NTUSER hive, BCD
export, EFI metadata and an inventory/hash manifest.  The hive payload is not
copied into the repository because it may contain credential secrets.  A full
offline Windows partition capture is still required on an external data drive;
the currently attached install ISO has no free space and is not a backup target.
