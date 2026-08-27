# Current J313 development state

Updated: 2026-08-27T20:40:00Z

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
- Exact EXP-123 recovery is live and responsive with display `both`, eight
  processors and Running platform services.
- EXP-144 showed that IRQ gating removes the approximately nine-to-one ISR
  amplification but was invalidated by an unplanned VHF parameter reset.
- EXP-145 preserved explicit VHF `0/1/1`, installed exact candidate
  `oem17.inf`, kept Keyboard and Touchpad children healthy, and produced no new
  Event 129 or System errors.  The user confirmed built-in input works.
- AGX0 is phantom under recovery. Driver Store contains no display package
  after ordinary removal of the disconnected devnode and old `oem17.inf`
  without `/force`.
- A one-variable recovery candidate is built from EXP-123 plus bounded IRQ
  transition logging; it has not yet been launched.
- Never attach the m1n1 proxy client while this guest is running.

## Stale GPU package isolated by EXP-140

- Device: `ACPI\APPL0002\0`.
- Package: `oem17.inf`, version `15.47.29.978`.
- Driver: `AppleAgx.sys`; it remains attached only to a disconnected phantom
  devnode under EXP-123.
- Candidate-G2 state: Problem 43 with two boot-time Event 129 records before
  any new package transaction.
- SYS SHA-256:
  `841dc5cb713ea3a61731a8b915ec0827c18add102f3de31da515fd3f77d4300a`.
- Signer display name: `WDKTestCert runneradmin,134323192909486495`.

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

EXP-141 rejected stale AppleAgx startup as the reset cause. EXP-143 bounded the
IRQ-route logging and identified the sustained route as physical AIC 330 to
guest INTID 865, the AppleInput GPIO parent rather than NVMe or xHCI. EXP-144
confirmed the IRQ amplification mechanism.  EXP-145 removed the installer
confound and qualified the complete input path plus clean storage window.

## Single next action

Cleanly stop exact recovery, cold-launch the already-built version-three GPU
resource candidate, and run only the read-only assigned UAT-root snapshot.
Do not publish roots, build initdata, rerun EXP-138/139, add delay, or change
the RTKit wire protocol until that assigned-resource snapshot succeeds.

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
