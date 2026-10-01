# EXP920: longer-lived healthy boot; desktop still not established

Package30.0.920.0/source d16f42c0 contains competing-notifier correctiona5c2c1af
and first-fault origin tags. Actual production replay is RED before the change,
GREEN afterward; earlier worker-retirement and queue wake/replay tests pass.
Native builds/signing/PDB gates pass. Final1195 tests have exactly the original
15failures/38errors/2skips, with no new IDs. The initially missing CAS test shim
was repaired separately; the driver package source did not change.

Original boot `2026-10-01T22:15:48.7628060Z` reached Code0/CPU8, DWM1248 and
Explorer4680. It passed the early38.769s boundary where919 had crashed. The same
boot remained Code0 at876.881374s and at the original collector22:38:50, over20min.
No kernel reset was observed. This is nonrecurrence over one run, not proof that
the exact competing-notifier interleaving occurred in hardware.

## Storage failure limits the observation

Host `/Volumes/pwdev` became full. The monitor final file froze at359s while
its log recorded good samples through487s, then file writes failed. Frame polling
stopped and live ETL copies failed. The hypervisor owner was not interrupted.
The host-only repair and evidence relocation are documented in
`EXP920-host-storage-recovery.md`. Four snapshots left on the guest were later
copied and independently verified; old partial host copies were preserved.
Do not claim continuous ETL/frame/serial coverage through this gap.

Matched current DCP swap10/sequence2/IOVA102a0000/PA8e0110000 was zero at+120/+300s.
The +600s serial snapshot is unavailable after the host write failure. It must
not be replaced with an earlier result. Cache_clean0 remains a CPU-view limit.

After host storage recovery, the existing registered one-shot creation probe
ran once asPID7088/session1 on AppleLUID41828. D3D11FL10 device, composition chain
and GetBuffer0 all returnedS_OK; 2560x1600/BGRA87/one layer/sample. Output707B SHA
`0e8e513358a4c7e20dd4fd37b609507cb64630fe3d54470eb6f5b412fbd68bbd`,
independently verified; task removed. This preserves the918 creation fix.

Fresh frame observation22:33:52 still has physical/virtualPresent0, TDRcapture0,
SourceAddress count1. DWM completed-fence progress continues. A physical-screen
question was asked asynchronously; no response has been received. Desktop
acceptance remains FAIL/unproven and no package is accepted as working desktop.
No DWM termination occurred in920.

## Evidence and next causal step

Root `.local/experiments/EXP920-preempt-notifier-race/`:
- original15files including manifest host gate
  `fc3b1475629be129673b5067c98cd93a3682e24ccd7d7b6b45a4eda2529cc14a`;
- frozen85files/eight complete snapshots (four recovered), with one explicitly
  listed old partial copy, host gate
  `0730ace7858b4393db4556bbaa342581c40bf5601b150a7a54a260e96133602d`;
- `observer-resume-sample.out`, `frame-after-storage-recovery.out`,
  `composition-create-host-gate.json`, `recovered-original-snapshot-host-gate.json`;
- `host-storage-recovery-receipt.json`; original evidence paths remain valid via
  verified symlinks to `/Users/pavel/J313-evidence-archive/2026-10-02/`.

Next921 is the still-unexecuted guarded DWM reinitialization diagnostic on exact
920package, with corrected host storage controls. It distinguishes fresh
composition on the ready adapter from retained adapter-transition state.
It is not a permanent restart workaround. No broad DDI/capability rewrite is
justified by the current evidence alone.

Original ordered restart was accepted22:42:17 after all original host gates.
Normal377/392 recovery, separate evidence verification, exact920 removal and
final Code28 durability must complete before921 staging. Results follow below.

Recovery completed: ordinary377/392 boot22:47:38.904384Z Code43/Stopped,
recovery15file hostgate134d10e53c0b4472713837484f87bb4808aba83a85f6972002b3d72be78e358c.
Owned diagnostics and exact920/oem5 removed after gates; two verified guest ETL
duplicates removed to restore guest headroom. Final ordinary boot
`2026-10-01T22:57:24.2676410Z` passed22:59:54 and23:00:51: Code28/oneAPPL0002,
package/modules/service/signer/arms/diagnostics0, CPU8/disks2/USB5, SSH/RDPservice,
autologon1, freeC4814950400/shadows0. Cleanup3file hostgate
`3aaa75088160432ac4d339d4cabb3935e6e576bde623f946b466d1d8ada7c420`.
No forced signal or hidden boot. EXP920 closed with the observation limitations
above; EXP921 prepared, not staged at this closure.
