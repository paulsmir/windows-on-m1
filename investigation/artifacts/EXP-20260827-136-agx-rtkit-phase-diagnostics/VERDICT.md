# EXP-20260827-136 verdict

Status: **diagnostic boundary validated; candidate rejected and closed without retry**.

The exact CI-signed RTKit qualification package matched its pinned INF, SYS, CAT,
certificate and signer identities on Windows. It replaced only the recorded
AppleAgx package without `/force` and requested one device-scoped start of
`ACPI\APPL0002\0`.

`pnputil` completed package configuration in about 80 milliseconds. An immediate
PnP query briefly reported `OK`, but the setup log and later durable receipts
prove that this preceded completion of asynchronous StartDevice. Package install
success is therefore not accepted as driver-start success.

The terminal device result was:

- PnP Problem 43, AppleAgx Stopped;
- StartDevice stage 6;
- StartDevice and RTKit boot status `0xC00000B5` (`STATUS_IO_TIMEOUT`);
- RTKit boot phase 1 and flags 1 (`begun` only);
- negotiated version zero and no management HELLO;
- cleanup status `0xC00000BB`;
- final ASC CPU-status read success, value `0x2d`.

This validates the purpose of the diagnostic change: the previous aggregate
timeout is now localized before receipt of the first RTKit management HELLO. It
does not identify why HELLO is absent and does not authorize a protocol change.

Windows stayed at the desktop and SSH-responsive with eight logical processors.
AppleInput, stornvme and USBXHCI stayed Running, and no fresh stornvme Event 129
occurred after installation. Five ACPI Error-level System events were recorded at
the device-start timestamp, independently violating the experiment health gate.

No endpoint map, firmware-ready state, UAT publication, initdata, interrupt,
queue, command, render, presentation or display-ownership action occurred. The
driver remained fail-closed and stopped. There was no retry, reboot or second
state-changing hardware operation.

The follow-up must compare the exact ASC RUN-to-first-HELLO sequence against live
state, Asahi, m1n1, Mu/ACPI and the Windows transport before defining one new
falsifiable hot-cycle experiment. Raw evidence is retained only in the ignored
`.local` experiment directory.
