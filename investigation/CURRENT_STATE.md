# Current J313 development state

Updated: 2026-08-27T14:38:04Z

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
- Root at the EXP-136 hardware run: `704bf3e6a39414ead0025ba27ee70d9e53832a43`.
- EXP-136 diagnostic implementation: `e0087563daf45426675c2754571199ce8af6f00c`.
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
- No fresh stornvme Event 129 occurred after the EXP-136 package installation.
- Never attach the m1n1 proxy client while this guest is running.

## Installed GPU package

- Device: `ACPI\APPL0002\0`.
- Package: `oem17.inf`, version `14.25.24.601`.
- Driver: `AppleAgx.sys`, service Stopped/Manual.
- Device state: Error, Problem 43.
- SYS SHA-256:
  `af3a029572f0b45945a53cb15ff79fbfcd1e3ff0d6a12d0a6398a1eac31a950b`.
- Signer thumbprint: `92D87C083D104C19CF3E40E34139992A6D16D827`.

## Last confirmed boundary

- Experiment: EXP-20260827-136.
- PnP invoked DriverEntry, AddDevice and StartDevice.
- Final StartDevice stage: 6.
- StartDevice/RTKit boot status: `0xC00000B5` (`STATUS_IO_TIMEOUT`).
- RTKit phase: 1 (`boot begun`).
- RTKit flags: 1 (only `begun`; no HELLO).
- Negotiated protocol version: 0.
- Final ASC CPU-status read succeeded; value `0x2d`.
- Bounded stop returned `0xC00000BB` after the incomplete boot.
- `pnputil` configuration success is not driver success: PnP returned before the
  asynchronous StartDevice timeout became visible.

## Active hypothesis

The first failing boundary is before receipt of the RTKit management HELLO. The
current evidence does not yet distinguish absent firmware execution, missing ASC
mailbox visibility, or missing interrupt/poll delivery. No protocol correction is
authorized until the live state, Asahi, m1n1, Mu/ACPI and WDK contracts are
compared at this exact boundary.

## Single next action

Complete source-first comparison for the ASC RUN-to-first-HELLO sequence and
form one falsifiable hypothesis. The next hardware action, if justified, is one
Windows device hot cycle using a receipt-complete runner. Do not reboot merely
for a Windows-only driver diagnostic change.

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
