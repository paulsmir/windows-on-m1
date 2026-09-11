# EXP-20260827-131 AGX inert MMIO contract verdict

Status: **passed and closed** after the single preregistered G2 execution.

The exact MMIO-qualification package crossed the full render-only WDDM
lifecycle and produced fresh receipts for the bounded operation:

- SGX physical aperture: `0x204000000`, length `0x4000000`;
- ASC contained subview: offset `0x2400000`, length `0x6c000`;
- map status: `0`;
- subview-validation status: `0`;
- unmap status: `0`;
- deliberate fail-closed StartDevice boundary: stage 7,
  `STATUS_NOT_SUPPORTED` (`0xC00000BB`).

The test did not dereference either mapping. Source and hypervisor-log audits
found no GPU-register access, power transaction, firmware boot, GPU RTKit,
interrupt, UAT, queue, command, render, present or display-ownership action.

The candidate retained eight logical processors and Running AppleInput,
stornvme and USBXHCI. No stornvme Event 129 or critical System event occurred.
Windows shut down normally. Exact EXP-123 recovery removed only the recorded
driver package and signer without `/force`; its cleanup boot proved no present
APPL0002, package, service, module or signer, eight processors, all three
platform services Running and a quiet event window.

This result authorizes work on the next separately gated boundary. It does not
authorize GPU-register access, power sequencing, firmware, interrupts, UAT,
queues, rendering, presentation or display ownership.
