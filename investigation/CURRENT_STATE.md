# Current J313 development state

Updated: 2026-08-27T15:27:27Z

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
- EXP-137 package source: `8252b9c759f447241fb5b28bfed522c9486dc080`.
- Single-transaction runner correction:
  `b4906b9d7468b00d35dfc10411b91a4c9b70064d`.
- m1n1 pin: `72dbbd2b0b279638ac53482a6d79d06adfa6aef7`.
- Mu pin: `c6108366201f869b297912a0ef8323b343256ecc`.
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
- Windows is at the desktop and SSH-responsive.
- AppleInput, stornvme and USBXHCI are Running.
- No fresh stornvme Event 129 occurred after the EXP-137 package installation.
- Never attach the m1n1 proxy client while this guest is running.

## Installed GPU package

- Device: `ACPI\APPL0002\0`.
- Package: `oem17.inf`, version `15.15.32.644`.
- Driver: `AppleAgx.sys`, service Stopped/Manual.
- Device state: Error, Problem 43.
- SYS SHA-256:
  `1ac19ede3267b2a836e177e96ad26f69c89298c3078a6412f1b9200882893beb`.
- Signer thumbprint: `BCE4F22D33D675EABA3B8A88FDB102E536E69F5A`.

## Last confirmed boundary

- Experiment: EXP-20260827-137.
- PnP invoked DriverEntry, AddDevice and StartDevice.
- Final StartDevice stage: 6.
- StartDevice/RTKit boot status: `0xC00000B5` (`STATUS_IO_TIMEOUT`).
- RTKit phase: 1 (`boot begun`).
- RTKit flags: `0x81` (`begun` and `CPU_READY`; no HELLO).
- Negotiated protocol version: 0.
- Final ASC CPU-status read succeeded; value `0x2d`.
- Bounded stop returned `0xC00000BB` after the incomplete boot.
- `pnputil` configuration success is not driver success: PnP returned before the
  asynchronous StartDevice timeout became visible.

## Active hypothesis

EXP-137 proved the ASC reached running and stopped cleared before IOP INIT, but
HELLO still never arrived. The stopped-to-running timing hypothesis is rejected.
The next source-first boundary is whether the IOP-init mailbox write and
doorbell are visible to the running GPU firmware.

## Single next action

Compare the exact Windows mailbox write and doorbell sequence with current
Asahi and m1n1, then add one read-only durable receipt that distinguishes a
published message from firmware consumption. Do not rerun EXP-137 or add delay.

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
