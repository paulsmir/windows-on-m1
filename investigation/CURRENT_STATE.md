# Current J313 development state

Updated: 2026-08-27T19:27:00Z

This is the bounded session entry point. Detailed history is append-only in
`investigation/EXPERIMENTS.md`; raw evidence remains under ignored `.local`.

## Stable recovery

- Immutable recovery: exact EXP-123.
- Stable milestone: eight CPUs, native keyboard and Precision Touchpad, internal
  display, NVMe, xHCI and SSH hardware-validated before the GPU branch.
- Do not overwrite the stable recovery artifacts with an experimental GPU build.

## Repository identity

- Canonical checkout: `/Users/pavel/public_windows`.
- Branch: `feature/j313-gpu-acceleration`.
- Candidate root commit: `a36a6fcd1a2e67334690ba6f8d2ab1efb8376e2b`.
- EXP-139 package source: `8b5ab22ba9d7b7446d9919b62b9554589a51f14f`.
- Single-transaction runner correction:
  `b4906b9d7468b00d35dfc10411b91a4c9b70064d`.
- Candidate m1n1 pin: `4108e79c69bac112ffbebf452fccf352c93c1dd2`.
- Candidate Mu pin: `5acdb4a7459d6de20bccea5cc1cf14c9f9dea06b`.
- Candidate monitor image SHA-256:
  `67713a743f5b6e16e7f3d69cf016ad74b3cb57a0ef901b239b741cdc06651b7e`.
- Candidate UAT snapshot driver SHA-256:
  `09e74647911439b720cc32013114c95ad69e7697fe8ea3873b14f4fcc3828ee0`.
- Candidate UAT snapshot catalog SHA-256:
  `01ff42516dd8cfe791f2c8f6531914aaef5d90bb0b21b36079aad3c33c4498a9`.
- Preserve existing untracked/dirty submodule metadata; never stage it.

## Live machine

- Host: `DESKTOP-LS9L95M`, SSH `pavel@192.168.1.37`.
- Boot mode: assisted G2, display `both`, eight logical processors.
- m1n1 artifact SHA-256:
  `c7232200573956155c48ddf441723df21e5d8bfee069bf33b36b6b6065d36846`.
- Mu artifact SHA-256:
  `53c52005854d03c449c534c805df7c180d90e30ab29effbdc9e7003b3bef5c8d`.
- boot image SHA-256:
  `ff8695f0b5f43f853bfd1cbd604b71c621baf0251ec9e56398b6214eba8818e6`.
- EXP-139 lost SSH and ICMP during its only transaction. The Air remains
  unreachable; physical-screen state and installed-package state are unknown.
- Do not run another package transaction until exact EXP-123 recovery and the
  stable Windows device/service baseline are re-established.
- Never attach the m1n1 proxy client while this guest is running.

## Last known stable GPU package

- Device: `ACPI\APPL0002\0`.
- Package: `oem17.inf`, version `15.15.32.644`.
- Driver: `AppleAgx.sys`, service Stopped/Manual.
- Device state: Error, Problem 43.
- SYS SHA-256:
  `1ac19ede3267b2a836e177e96ad26f69c89298c3078a6412f1b9200882893beb`.
- Signer thumbprint: `BCE4F22D33D675EABA3B8A88FDB102E536E69F5A`.

## Last confirmed successful boundary

- Experiment: EXP-20260827-138.
- PnP invoked DriverEntry, AddDevice and StartDevice.
- Final StartDevice stage: 6.
- StartDevice/RTKit boot status: `0xC00000B5` (`STATUS_IO_TIMEOUT`).
- RTKit phase: 1 (`boot begun`).
- RTKit flags: `0x81` (`begun` and `CPU_READY`; no HELLO).
- Negotiated protocol version: 0.
- Final ASC CPU-status read succeeded; value `0x2d`.
- A2I inbox moved from empty at pointer 5, to one queued message, to empty at
  pointer 6. Firmware therefore consumed IOP INIT.
- I2A outbox remained empty at pointer 3; firmware produced no HELLO.
- Bounded stop returned `0xC00000BB` after the incomplete boot.
- `pnputil` configuration success is not driver success: PnP returned before the
  asynchronous StartDevice timeout became visible.

## Active hypothesis

EXP-138 proved mailbox publication and firmware consumption, rejecting timing,
barrier, trigger-order and unread-response explanations. Asahi and m1n1 create
and publish the context-zero UAT roots before RTKit boot; the active Windows
StartDevice path enters RTKit without doing so. Missing or invalid roots in the
fixed J313 GPU region are now the first falsifiable firmware prerequisite.
EXP-139 did not recover a root receipt and rejected private physical mapping as
an unsafe access method. The version-3 candidate now assigns the region through
Mu, m1n1 stage-2 and Windows translated resources.

## Single next action

Recover exact EXP-123, verify eight CPUs and stable input/storage/USB/SSH, then
qualify the version-3 resource contract and read-only root snapshot exactly
once. Do not publish roots, build initdata, rerun EXP-138/139, add delay, or
change the RTKit wire protocol until the assigned-resource snapshot succeeds.

## Rollback

- Device-level rollback must name exactly one recorded prior `oemNN.inf` and must
  not use `/force`.
- Stop after timeout, critical event, Event 129, identity drift, lost SSH, or lost
  input/storage/xHCI health; do not perform a second state-changing retry.
- If PnP cannot unload safely, use the exact EXP-123 assisted cold recovery.
- Standalone qualification is reserved for a completed milestone.

## Context budget

- Read this file first; do not load the complete experiment ledger by default.
- Read only the active experiment section and exact source boundary.
- Summarize raw logs into bounded JSON; retain raw evidence under `.local`.
- Run the exact unit test on each edit, the focused GPU suite before packaging,
  and the complete repository suite only before a significant push, merge or tag.
- Commentary is limited to state transitions, problems and results.
