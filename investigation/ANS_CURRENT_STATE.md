# ANS current state — OFFLINE ONLY

2026-09-05. User requests complete supplied native ANS plan, first ALL feasible
offline work, then ask permission before any MacBook Air tests. User additionally
requires no conflict with concurrent GPU development on original branch.

## Absolute isolation

ANS root: /Users/pavel/public_windows/.worktrees/ans-native
Root, m1n1_windows, mu, mu/MU_BASECORE each use separate worktrees on
feature/j313-ans-native-offline. Original /Users/pavel/public_windows GPU
checkout and its dirty files MUST NOT be changed. Never stash/reset/checkout it.
No Air SSH, proxy, USB, hardware launches, installs, reboots or diagnostics.
No shared GPU build folders/containers/processes. No merge/push authorized.
Only separate FRYZZING x64 build server is allowed for offline compilation;
verified hostname and actual WDK26100, dedicated C:\Users\pauls\ans-native-offline-20260905.

## Completed independent changes

- Architecture audit documentation/NATIVE_ANS_STORAGE.md and original supplied
  spec documentation/plans/J313_NATIVE_ANS_REQUEST.md.
- SHA-verified separately read-only EXP377 m1n1 + EXP392 ordinary G2 FD and
  EXP474 final ordinary health copied to .local/ans-native/recovery.
  Manifest investigation/ans/RECOVERY_BASELINE.json. No new hardware health claim.
- Root79850a5/f516317 foundation: canonical config + generated broker ABI/ASL,
  resource collision checks and conservative native launch gate ALWAYS CLOSED.
- m1n11186818 broker owner/generation/tag and shadow PRP portable helpers.
- Root5420daf portable SCSI+SG helpers, common tests ASan/UBSan, 1024 PRP offsets.
- m1n1a562468 shared ans_queue engine used by nvme.c; wrapper remains synchronous
  recovery tag0. Root review discovered post-timeout PRP-list mutation; fixed
  m1n1a3c09f1 with executable actual-function RED/GREEN regression.
- Full Linux tagset audit corrected reserved admin slots1→2 (not active admin
  request count). m1n1f305631, root90201d3. Generated ABI macro ANS_ADMIN_TAGS2.
- tools/test_ans_offline.py passes9 Python config tests, quarantine test, common
  suite, broker/PRP sanitizer tests and freestanding ARM64 helper object builds.
  This is not a full native SYS/platform candidate or complete offline plan.
- CHANGES.csv appended independently in this worktree only; ledger baseline
  test already fails on old diagnostic change_type rows (not introduced here).

## Active work and agents

- ans_scsi_core agent now implements full-range hv_ans_memory and authoritative
  hv_stage2_get_leaf in hv_vm.c/hv.h. Does NOT commit until root coordinates.
- ans_backend_refactor agent now builds physical StorPort qualification skeleton
  under drivers/storage/appleans/windows. No transport offsets yet; must return
  FALSE at HwInitialize TRANSPORT_UNAVAILABLE, no fake QUERY/CLAIM/readiness.
  VS2022 instance C:\VS2022Community has ARM64 and WDK26100; initial VS18-only
  absence finding corrected. Read-only builder key ~/.ssh/windows_builder,
  strict known hosts copied to .local/ans-native/build-host/known_hosts.
- review_ans_offline agent reviewed helper/backend, withdrew generic SQID finding:
  ANS DWORD2 reserved, sourceCQ+CID determine identity. Now derives Mu EBS hook.

## Build ledger

ANSBUILD001 in investigation/EXPERIMENTS.md: two configuration attempts A/B
occurred without advance entry; recorded retrospectively explicitly (no SYS,
no source compile). C preregistered exact WDK properties before build, final
archive03 f6d46b83218a4e8733910639a8e5ddd65b8f3ef63484065a25e7ded5cc259564.
Archive02 superseded without building. Agent authorized C; result pending.
Update AFTER actual result; every further hardware-target package/image build
gets pre-entry with exact source/command and post-result. Never install/launch.

## Native integration still required

Controller/RTKit bounded boot/stop/refactor, real DMA owner/mapping lifetime,
MMIO transport offsets and dispatcher, admin whitelist/private queue backing,
physical AIC590→dedicated guest889 routing, native mode and vPCI ownership gate,
Mu ANS profile/DSDT inclusion/EBS handoff, real physical StorPort initialization
and SRB path, full native ARM64/analysis/InfVerif/sign gates, exact ANS001 plan.
No hardware permission request until offline work complete.

Source differences needing explicit native policy: Linux TCB status0x28120 vs
recovery0x29120; Linux TCB opcode0 vs recoverycommand opcode. Current rtkit_sleep
ignores boolean failure and returns0; power loops unbounded. Keep recovery
unchanged artifacts; don't confuse old teardown with proven DMA stop.

Accepted EBS design ruling: CoreTerminateMemoryMap only validates. Add
DXE-core-linked allocation-free timer-independent EL2 gate AFTER successful
MapKey validation, BEFORE gMemoryMapTerminated and exit event notifications.
On failure nonreturning fatal recovery policy, not invented EBS status or fake
invalid MapKey. Recovery uses null library. No diagnostic wrapper dependency.
