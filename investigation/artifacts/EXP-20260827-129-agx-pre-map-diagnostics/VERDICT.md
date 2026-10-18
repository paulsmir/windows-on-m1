# EXP-20260827-129 verdict

Status: **rejected and closed without retry**.

The single authorized G2 boot used the exact preregistered identities. The
current qualification package was staged as `oem17.inf`; Windows configured
`ACPI\APPL0002\0` as a Display-class device and loaded the AppleAgx service far
enough to persist fresh lifecycle receipts.

The fresh receipts localize the failure before `DxgkDdiStartDevice`:

- `DriverEntry` reached stage 2 and `DxgkInitialize` returned success;
- `DxgkDdiAddDevice` reached stage 2 and returned success;
- no `DxgkDdiStartDevice` stage or status was written;
- no SGX map, ASC subview or unmap receipt was written;
- Kernel-PnP Event 411 reported Problem 31 with status `0xC0000182`;
- SetupAPI classified the boundary as `CM_PROB_FAILED_ADD` after AddDevice.

Therefore EXP-129 disproves translated resources and the inert MMIO mapping
coordinator as the cause of this failure: neither boundary was reached. A
comparison with Microsoft's full render-only KMD sample identifies the next
offline correction as completing the `DRIVER_INITIALIZATION_DATA` callback
contract before another hardware qualification.

The candidate retained eight logical processors and Running AppleInput, NVMe
and xHCI, with no critical event, but recorded two stornvme Event 129 resets and
failed the storage health gate. No GPU register access, firmware ownership,
RTKit traffic, interrupt connection, UAT, queue, command, render, present or
display ownership occurred. The package and signer were removed without force,
and the final recovery quiet window returned to eight processors, Running
AppleInput/NVMe/xHCI, zero critical events and zero Event 129.

Raw SetupAPI, PnP, event and hypervisor evidence remains local and ignored.
Only this sanitized verdict is public.
