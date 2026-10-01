# J313 GPU current state — 2026-10-01 / EXP916 closed

**Desktop is still FAIL; accepted package: NONE.** No new candidate is staged.

## Current machine and recovery

Ordinary immutable EXP377 m1n1 + EXP392 Mu, boot
`2026-10-01T18:30:53.4670870Z`; checks 18:31:17 and18:34:50:
one inert APPL0002 Code28; package/SYS/UMD/service/signer/arm/diagnostics absent;
CPU8, disks2, USB5, SSH, RDP service running, autologon1. Free C4739559424,
ShadowCount0. Original and recovery evidence were independently host verified
before exact Code43/oem5/package916 cleanup. No hidden boot or forced signal.
Active clean launcher PID53077 (verify fresh before control).
Artifacts root `.local/experiments/EXP916-notification-admission/`.
Cleanup host gate SHA326b284d73c404086b3580150a2393ff5cf1cc2a385aa8b8c7eeed977cd719f5.
Before any physical request check bounded SSH, USB endpoints and actual owner.
Never issue standalone proxy NOP while SSH/launcher is alive.

## Proven boundary

EXP916 source9136ff1b/package30.0.916.0, fix1e8ac1c3:
773.762302s original Code0/CPU8 without observed0x119/2; deterministic
production-code RED/GREEN proves queue admission during NotificationClaimed
plus postcommit wake. Hardware window is nonrecurrence, not a measured replay
of that exact interleaving. Prior worker-retirement fix038cd941 is retained.
Current DCP matched surface PA8e0110000/IOVA102a0000, swap10/sequence2,
zero pixels +120/+300/+600; physical/virtual DWM Present counters0.
Read-only Session1 SDK probe proves active primary AppleLUID41b92 source0,
target0 available,2560x1600. Missing active topology is excluded.

## Current causal decision

WHY CONTINUE COMPARISON: no historical comparison. Current native source only
supports D3D10 DDI; pinned WDK/Microsoft FULL GRAPHICS requirements expose a
confirmed modern UMD contract gap. Derive the owning native ABI/resource/
presentation implementation offline before more capability experiments.
**This gap is not yet proven to be the immediate black-screen cause.**
Do not simply advertise D3D11.1/DXGI1.2 or change NO_REDIRECTION to S_OK.
Native return propagation is direct and was verified; non-native S_OK code
is not this package's path. Do not repeat that hypothesis.

Exact guest metadata names event468 Dx_Flip_Consumed: earlier wording calling
it an output binding event is corrected. Its absence cannot prove missing
output-target binding. First frozen ETL has62 postApple drawlist-frame events
with HW/WARP draw counts0; early window only. No captured precise DWM rejection
branch/HRESULT yet. Do not claim all-boot absence from circular ETL windows.

## Read only these relevant references next

- `analysis/EXP916-notification-admission-verdict.md`: current evidence, limits,
  original162/collector16/recovery15 hash gates, topology, exact ETW correction.
- `analysis/EXP916-native-umd-contract-gap.md`: Windows -> translation -> Asahi
  offline prerequisite and ownership; no next hardware experiment prepared.
- `analysis/EXP915-notification-race-verdict.md`: exact previous dump504e0652,
  CPU0/System119/2/80000011, validPaging1 F972 afterF971/emptyqueues/phase3.
- `analysis/EXP914-preempt-worker-verdict.md`: actual DWM preempt/resubmit/complete
  proof of worker reservation fix; already excluded, do not reopen.
- `EXPERIMENTS.md` EXP916 and referenced recovery entries only; full ledger
  preserves superseded results. `CHANGES.csv` indexes implementation commits.

Recovery artifact hashes remain immutable: EXP377 fae3444cc289cf52ea12b81b9db8f3d8bf24bd084f899a751321d2048d9a525a;
EXP39216c177182e96b63eac852dcfb185cebba9c1d91943c6402106a640848ddc5e06.
Hidden EXP385279bd36ad3bbb1ee5e2393fa965343ea856b4c2b0dd4df2b2add6a8010e3f32c is emergency only.
