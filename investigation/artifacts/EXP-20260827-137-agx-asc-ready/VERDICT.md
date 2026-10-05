# EXP-20260827-137 verdict

Status: rejected after one Windows device hot cycle on 2026-08-27.

The official ARM64 WDK artifact came from GitHub Actions run `33086632205`
at source commit `8252b9c759f447241fb5b28bfed522c9486dc080`.  The package
identity was:

- `AppleAgx.sys` SHA-256
  `1ac19ede3267b2a836e177e96ad26f69c89298c3078a6412f1b9200882893beb`;
- `AppleAgx.inf` SHA-256
  `8cc6f88cef5c664f92387fce6f0ad80ac006e35c525f30f0e1006c6c7966fceb`;
- catalog SHA-256
  `ea25133a3c3b76450d73b3e1d1259566c713650f1ca5105114f757e16ba7df42`;
- signer thumbprint `BCE4F22D33D675EABA3B8A88FDB102E536E69F5A`.

The exact prior `oem17.inf` package was replaced without `/force`.  The final
durable receipts reported `Wom1RtkitBootFlags=0x81`: boot began and the new
bounded wait observed `CPU_STATUS.RUNNING=1` with `STOPPED=0`.  Final CPU status
was again `0x2d`.  RTKit nevertheless remained at phase 1, negotiated version
zero, and returned `0xC00000B5` without receiving the first management HELLO.
The device settled at StartDevice stage 6, stopped service, and Problem 43.

This rejects the hypothesis that the first IOP-init message was lost merely
because Windows sent it during the ASC stopped-to-running transition.  More
delay is not justified.  The next source-first boundary is whether the IOP-init
mailbox write and doorbell are visible to the running GPU firmware.

Windows remained responsive with eight logical processors and Running
AppleInput, stornvme, and USBXHCI.  There was no fresh stornvme Event 129 and no
reboot or bugcheck.  ACPI error records accompanied the failed device start, so
the candidate remains fail-closed and is not a milestone package.

The original lifecycle runner also exposed a process flaw: it issued
add/install, scan, and restart transactions after clearing receipts only once.
Its immediate `Problem 0` snapshot belonged to an intermediate transaction;
the later postflight contained the final Problem 43 receipt.  Commit
`b4906b9d7468b00d35dfc10411b91a4c9b70064d` makes future package cycles a single
add/install transaction and clears preparation receipts immediately before it.
No retry was performed for this experiment.
