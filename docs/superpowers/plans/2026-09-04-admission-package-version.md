# Admission package version repair

Goal: make INF, KMD and UMD numeric versions identical and independent of the
builder clock. This is packaging repair within the approved FULL GRAPHICS
architecture, not a WDDM model change or a validation bypass.

Evidence/spec: EXP455 executable sections equal EXP444; EXP459 proves miniport
Add success then Windows Add failure; EXP460 registry timeline ends after
DriverVersion/DriverDate/Rank reads. Current Windows dxgkrnl SHA recorded in
EXP460 local evidence; interoperability inspection at RVA1fd6f0,1fdc60 finds
INF/file version equality and compatibility threshold major21. The only
failure status of that mismatch path is C0000182. KMD has no resource section.
No Windows implementation code is copied into the driver.

Primary contracts inspected: Microsoft display version-format and software
registry documentation; current WDK WindowsDriver.Common.targets uses
Inf.SpecifyDriverVerDirectiveVersion and Inf.TimeStamp. Existing build log
shows stampinf -v "*", generating hour.minute.second.millisecond. This explains
19.xx admission versus 23.xx failure without a firmware source regression.

Ownership: WDK/MSBuild own stamping/signing; our package owns coherent version
metadata; Windows owns validation. Firmware, memory, IRQ, power and recovery
ownership remain unchanged. Asahi/m1n1/Mu protocols are not involved at this
boundary; frozen platform and previous EXP444 run are the hardware reference.

- [ ] Add real artifact verifier using FileVersionInfo numeric parts and INF
  DriverVer; reject missing versions and any mismatch. Run on EXP455: RED.
- [ ] Add shared version.props (30.0.build.0, build461 default) and one shared
  VERSIONINFO resource source compiled as driver/DLL respectively. Set WDK
  Inf.TimeStamp explicitly. No wall-clock version generation.
- [ ] Call verifier after build; test a real matching built package, a copied
  INF with changed numeric version and a copied package with missing SYS.
- [ ] Archive exact EXP444 source plus only packaging patch, build pinned WDK
  on FRYZZING, sign/hash/manifest. Verify .text/INIT of KMD still match control.
- [ ] One clean full-owner hardware bind; PASS is restored StartStage8 /
  BackendResult6, not acceleration. Exact cleanup then firmware ordering work.

Recovery: exact-package uninstall on SSH; frozen current-compatible
EXP377/EXP385 emergency pair if needed. No new approval or worktree.
Self-review: one independently observable invariant (matching versions);
no registry disabling of version validation, no claimed hardware proof yet.
