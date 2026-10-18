# EXP918: displayable BGRA creation fixed; desktop output still absent

**The observed creation defect is hardware validated. Desktop acceptance remains
FAIL; no package is accepted as working desktop.**

Source `d5c685596bb3073b758d43385177561dee50b5be`, package30.0.918.0,
correction `b5291548`: accept the documented displayable marker for the existing
linear/displayable BGRA allocation contract. No caps, DDI version, scheduler,
firmware or NO_REDIRECTION change. Actual production validator replay was RED
before the fix and GREEN afterward under ASan/UBSan. Native WDK contract case
also passed. KMD/UMD/frame-probe/x64-contract builds had zero warnings/errors;
CAT signer/membership, native archive and both PE/PDB identities passed.
Full1194 retained the known baseline plus the same two active-serial IDs; it is
not an entirely green suite.

Original boot `2026-10-01T19:58:23.7712030Z` reached773.877312s Code0/CPU8,
DWM1220/Explorer4344, no observed TDR. The same creation-only SDK probe as917,
SHA `cd7d043e4b598e6f11006df672ac2e12973aa89cefa49264403e8910fac880d8`,
ran once afterward as PID4640/session1 on AppleLUID43431. D3D11FL10 device,
composition flip-chain creation and GetBuffer0 all returnedS_OK. Buffer0 was
2560x1600/BGRA87/one layer/one sample/public bind20. In917 the same call returned
887a0005 after exact UMD reject-primary80070057 on misc20002. This closes that
specific causal boundary without a speculative interface-version change.

Probe output707B SHA
`817f3a9bbc604cf987af2039b613e9d60033ad1690e9456496f0ca43c670ebf9` passed
independent host size/SHA verification; task removed, native exit0.

Physical/virtual tracked DWM Present remained0, including the last post-probe
frame receipt at20:12:45. DWM context0 submit51/complete51, context1 submit13/
complete13. Current DCP swap9/sequence2, IOVA102a0000/PA8e0110000, remained zero
at120/300/600s; cache_clean0 remains the CPU-view limitation. No ordinary visible
updating desktop proof exists.

The first frozen ETL has30972 Dwm-Core events. Same DWM1220 first created a Basic
Render device, destroyed it, then created AppleFL10.0. The current DWM module
snapshot at20:17:44 includes DXGI/D3D11/AppleUMD/WARP, without D3D9; this is a
snapshot, not proof of all prior loads. No captured DWM reject-primary call for
the fixed tuple. Next discriminator: one fresh console compositor after the
adapter is ready, compared with the pre-probe918 baseline, using unchanged918
package. A restart is diagnostic only, not an accepted production fix.

Root evidence `.local/experiments/EXP918-displayable-resource/`:
- original15 files including manifest host gate
  `064790c0aa261eb074361c0e3c1c49cde6901248dc785a426e2af09b252c5001`;
- 11 complete snapshots /176 frozen files host gate
  `b52c02cac7b41bc5e7d0ae6885ee1eaccd81cad6c9f246e22fdf69945a20f619`;
- recovery15 files including manifest host gate
  `6f040102666382ba5dc57c6866c1d28d6817ec10f7562ed7ca11340d5caa19c0`;
- `composition-create-host-gate.json`, `composition-create.out`,
  `dwm-early-decisions.json`, `dwm-module-observation.out`, `full.log`.

Original ordered restart completed without forced reset. Ordinary377/392 recovery
boot20:28:10.048456Z reached Code43/Start2/Stopped/CPU8. An unnecessary SIGINT
snapshot was sent because a newly successful SSH response was not inspected
before the queued action; the initial ledger premise was explicitly corrected.
The log confirms continuation, not reboot. This occurred in recovery after the
original evidence was frozen, not in the comparison window. No hidden boot.

All host gates preceded exact cleanup. Owned AutoLogger/WER were removed; two
exact hash-verified guest ETL duplicates were removed to keep4GiB plus256MiB
snapshot margin, retaining host copies. Exact918/oem5 cleanup at20:41:43 scheduled
ordinary restart. Final durable recovery receipts follow below.

Final ordinary377/392 boot `2026-10-01T20:45:14.8128160Z` passed at20:45:56
and20:47:52: one inertAPPL0002 Code28; package/modules/service/signer/arms/
diagnostics absent; CPU8/disks2/USB5, SSH, RDPservice, autologon1.
FreeC5006991360/shadows0. Cleanup3file host gate
`a343e24f52f64a9b4f858fa3b3a01745a37b7b0706a056c4c238582a6bc6eb65`.
EXP918 closed. EXP919 is prepared but not staged at this closure.
