# EXP-20260827-135 verdict

Status: **powered read validated; candidate rejected and closed without retry**.

The mandatory recovery preflight was clean and every pinned identity matched.
The exact signed powered-status package was staged as `oem17.inf`, followed by
one display-`both` cold G2 boot. Because the staged package matched APPL0002,
Windows started it during that boot before the planned explicit device restart.
No second start or retry was performed.

The single StartDevice execution completed the authorized transaction:

- SGX map status: `0`
- ASC subview status: `0`
- broker sequence: `ON -> QUERY -> OFF`, all results `0`
- ASC CPU-status read status: `0`
- ASC CPU-status value: `0x0000002a`
- SGX unmap status: `0`
- final StartDevice stage: `9`, `STATUS_NOT_SUPPORTED` (`0xc00000bb`)

The hypervisor log independently recorded exactly three broker receipts:
sequence 1 command ON, sequence 2 command QUERY, and sequence 3 command OFF.
The reported state was ON for the first two receipts and OFF after release.
There was no CPU RUN write, mailbox traffic, firmware start, RTKit exchange,
interrupt enablement, UAT publication, allocation, queue, command, render,
present or display-ownership action.

This validates the narrow hardware conclusion that the J313 ASC CPU-status
register is readable through the bounded SGX mapping while the GPU domain is
explicitly powered and confirmed ON, and that the domain can then be powered
OFF cleanly.

The candidate is nevertheless rejected as a system qualification. Windows
recorded one fresh `stornvme` Event 129 after candidate boot, before any manual
device cycle. That violates the preregistered zero-reset health gate. It also
made the planned restart unnecessary and unsafe, so the experiment closed
without one. The successful GPU transaction does not authorize firmware,
interrupt, UAT, queue or rendering work on this candidate baseline.

Windows shut down normally. Exact EXP-123 recovery removed only `oem17.inf`
and its pinned signer without force. The first cleanup boot was not accepted
because it recorded Event 129. One fresh control boot then proved APPL0002 and
AppleAgx absent, eight logical processors, Running AppleInput/stornvme/USBXHCI,
zero Event 129 and zero critical events. That clean recovery state is the final
machine state.
