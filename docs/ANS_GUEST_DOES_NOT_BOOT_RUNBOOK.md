# Runbook: the guest does not boot after a storage-driver install

Used once on hardware: EXP2447 (AppleAns 0.4.8.5, stop 0x7B on every boot path) and EXP2448 (the
repair). From stop to a running guest took 54 minutes, most of it writing the tools; with the
tools in the tree the repair itself is about ten minutes.

## When this applies

- The guest stops early with INACCESSIBLE_BOOT_DEVICE (0x7B) or hangs before SSH, on the ANS path.
- The host log of the ANS boot has no `IRQ route enabled vINTID=960 AIC=590` line: the storage
  driver did not start (image not loaded, or FindAdapter failed).
- The recovery profile boots the same disk and the same installed driver, so it fails the same way.
- The bridge path (stornvme on the emulated NVMe) fails with 0x7B when
  `Services\stornvme\StartOverride` value `0` = 3 is on the disk (see "StartOverride" below).

## Rules

- Stop booting. Two identical failures are enough. Do not try a third bridge boot: after repeated
  failed boots Windows may start automatic repair, and on the bridge path it can see the disk and
  write to it unattended.
- Nothing is written to the disk except the single reviewed change below. Preregister it as its own
  experiment; the architect reads the receipt and says yes before `--execute`.
- No PCIe call while ANS is up. The tools use only `nvme_init`, `nvme_read`, `nvme_flush`,
  `nvme_shutdown`, memory access to their own buffers and one `P_CALL`.

## Steps

All commands run on the Mac with the machine at `Running proxy` on the bench m1n1, with
`python3 -I`. `<ev>` is a fresh evidence directory; `B` is the build directory of the m1n1 image at
the proxy (its `m1n1.elf` and `m1n1.macho`; the image hash is in `.local/experiments/BENCH-M1N1.json`).

1. Reconnaissance, read-only (EXP2448: 7366 blocks, about one minute):

       python3 -I tools/ans_repair/ans_offline_recon.py --proxyclient <proxyclient> \
           --output <ev>/recon.json --save-blocks <ev>/recon-blocks --old-store-id <16 hex digits>

   `RECON_OK volume=N`. Read in the receipt:
   - `hive.base_block`: `clean` true, `checksum_ok` true; `bins_verified` > 0;
   - `hive.log_entries_at_or_after_hive_sequence`: empty;
   - `hive_path_trace`: four components, sequence numbers equal, the last one a file;
   - `hive.services.AppleAns.ImagePath.value`: which store directory is active;
   - `hive.services.stornvme.StartOverride`: value `0` = 3 blocks the bridge path;
   - `driver_store.packages`: the directory to go back to exists and its `AppleAns.sys` has the
     known-good SHA-256;
   - `hive.imagepath_patch`: `same_length` true, and the location (one or two blocks).
   If the hive is not clean or a log entry touches a target: stop; this runbook does not cover it.
   A failed run can be replayed offline from the saved blocks with `--blocks <ev>/recon-blocks`.

2. Choose the change.
   - `imagepath`: AppleAns `ImagePath` back to the known-good store directory (32 bytes). Restores
     the ANS path, the bench launcher and the recovery profile. Needs the old directory with the
     right file hash.
   - `startoverride`: stornvme `StartOverride` `0` 3 -> 0 (one byte). Makes only the bridge path
     bootable; use it when the old directory is missing or the first change did not help.

3. Dry run (reads and checks everything, proves the write routine, writes nothing):

       python3 -I tools/ans_repair/ans_offline_patch.py --mode imagepath \
           --receipt <ev>/recon.json --receipt-sha256 <hash> --proxyclient <proxyclient> \
           --elf B/m1n1.elf --elf-sha256 <hash> --macho B/m1n1.macho --macho-sha256 <image hash> \
           --expect-sys-sha256 <known-good AppleAns.sys hash> \
           --backup-dir <ev>/backup --output <ev>/patch-dry.json

   `PATCH_DRY_RUN_OK` and a `PLAN ...` line naming the LBA, offset, old and new bytes.

4. Write: the same command with `--execute` and `--output <ev>/patch.json`.
   `WROTE LBA ... and read it back identical`, `PATCH_DONE`. The original block is in
   `<ev>/backup/lba-<n>.before.bin`.

5. Reconnaissance again into new paths. The value shows the change, the hive is still clean, and
   exactly the patched block differs from the first run's blocks.

6. Boot the path that was repaired. ANS path: the `IRQ route enabled vINTID=960` line, then SSH.

7. In the running guest: verify the running driver's file hash, canary and chkdsk, remove the
   failed package from the driver store, delete `stornvme\StartOverride`, and prove the bridge
   path boots, so a rollback path exists again.

Undo: `--mode restore --lba <n> --expect-current-sha256 <after hash> --execute` writes the saved
block back under the same checks.

## What the tools check so that a wrong write is not possible by accident

- The target block's SHA-256 and the bytes to be replaced must equal what the reconnaissance saw.
- The m1n1 image file must have the pinned hash and contain the ELF's NVMe code exactly once; the
  same code must be in memory at the proxy; a read made through the same call path must return the
  same block as the proxy's own read. Only then is `nvme_write` called, once per block.
- The block is verified in m1n1 memory before the write, then flushed, read back and compared.

## StartOverride

- Observed on this guest (EXP2448): at every boot Windows writes `StartOverride` value `0` = 3 under
  the boot-start storage driver that controlled no device in that boot: under `stornvme` after an
  ANS boot, under `AppleAns` after a bridge boot. The other path then stops with 0x7B.
- Documented by Microsoft ("Error 0x7B after you reconfigure hardware devices"): a built-in feature
  switches a boot-start driver that was not required during startup to demand start; "This behavior
  is by design"; the only resolution given is safe mode, which re-creates the values. The article
  does not name the registry value, and gives no setting that turns the feature off.
- `BootFlags` (INF AddService directive) only promotes a driver to boot start in listed scenarios
  (network, VHD, USB, SD, measured boot, verifier, WinPE); it is not documented to prevent the
  demotion.
- So the value is cleared by housekeeping, not prevented: see the pre-install gate
  (`.local/experiments/ANSBOUNCE1/tools/PREINSTALL-GATE.md`).

## Limits

- Tested on one machine, one failure. A hive that is not clean, a value in a "big data" cell and a
  compressed or sparse hive file are refused, not handled.
- Whether winload ignores the transaction logs of a clean hive was not verified separately; in
  EXP2448 no log entry was pending and the change took effect.
