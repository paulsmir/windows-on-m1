# EXP930 live update verdict

Windows was not rebooted. Boot identity remained2026-10-02T17:06:29.489308Z.
PnP installed929/oem6 and then930/oem7. New KMD execution is demonstrated by
Wom1GpuvaArmBuild930, not inferred solely from the on-disk file version.
930 contains929 private preparation correction plus startup diagnostics.

The graphical correction remains software-tested, not hardware-validated.
ACQUIRE/PREPARE software entry passed real KMD/m1n1 broker lifecycle16/64,
retained lease protection, flag rejection and legacy RELEASE tests. The same
boot refused startup before reaching that GPU path.

Actual block: stage1 Code43, arm diagnostic phase4 ZwFlushKey returns
0xc000014d STATUS_REGISTRY_IO_FAILED. The arm value was deleted; hypervisor
acknowledgment was not attempted. Independent user-mode RegFlushKey returned
Win32 error1016. An owned131072-byte WriteThrough+Flush(true) test succeeded,
so this is not merely lack of free space or universal write failure.

Ntfs55 identifies corrupted MFT record0x10000000201f2 for
C:\Windows\System32\config\SYSTEM.LOG2. Ntfs50 records delayed-write failure
for the same file. C: is Dirty. Events include16:48 during earlier normal
recovery, preceding929 installation. This proves the current startup blocker;
it does not prove the cause of all earlier physical display artifacts.

A scan-only Repair-Volume -DriveLetter C -Scan was started. It reported35%
and did not update its state through subsequent observations. Graceful
Stop-StorageJob for the exact owned job returned Not Supported. No offline
repair, forced dismount, registry-log deletion, gate bypass or OS reboot was
performed. Check ROOT930/final-live-state.out for latest scan state.

Current constraints: preserve Windows session, AutoReboot0 (prior1 saved in
ROOT929/live-before.json), G3Arm absent. Do not repeat GPU restarts until SYSTEM
hive persistence is healthy. Necessary next work is NTFS/system-hive recovery;
any offline/system-volume action must respect the user's explicit no-reboot
constraint and requires a new decision if it would interrupt this boot.

Evidence: ROOT.local/experiments/EXP930-live-arm-status/live-update-evidence/
host-gate.json validates12 install/rearm receipts. registry-flush-probe.out,
storage-flush-discriminator.out, scan-job-detail.out and stop-owned-scan.out
retain independent observations. Signed929/930 packages, manifests and matched
PDBs are preserved. No stable-screen claim.
