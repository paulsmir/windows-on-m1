# EXP-20260827-130 verdict

Status: **passed and closed**.

The exact CI-signed lifecycle-only package bound as `oem17.inf` in one
display-`both` G2 boot. A device-scoped same-boot restart produced fresh
current-package receipts:

- DriverEntry stage 2 and `DxgkInitialize` success;
- AddDevice stage 2, status `0x00000000`;
- StartDevice stage 7, status `0xC00000BB`
  (`STATUS_NOT_SUPPORTED`), the designed fail-closed endpoint;
- one full translated resource list containing 13 descriptors.

The package was compile-incapable of the power and MMIO qualification paths.
No power, map, subview, unmap, pointer-dereference, register, firmware, RTKit,
interrupt, UAT, queue, command, render, present or display-ownership receipt
occurred.

The candidate retained eight logical processors and Running AppleInput,
`stornvme` and `USBXHCI`. The measured iteration recorded zero critical System
events and zero `stornvme` Event 129 resets. The bounded runner completed in
approximately 11 seconds without rebooting the guest.

The guest then shut down normally. Exact EXP-123 recovery removed only
`oem17.inf` and signer `A7847E0FB9AEAF201CD0CA24D9822CBF55632536`
without `/force`; the required cleanup boot proved no present APPL0002,
package, service, module or exact signer, eight logical processors, Running
input/NVMe/xHCI, zero critical events and zero Event 129.

This result proves the full fail-closed render-only WDDM callback contract and
authorizes a separately pinned inert SGX map/subview/unmap qualification. It
does not authorize register access, firmware start, interrupts, UAT, queues,
rendering, presentation or display ownership.

