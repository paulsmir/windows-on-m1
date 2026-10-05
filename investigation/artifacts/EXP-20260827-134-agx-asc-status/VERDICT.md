# EXP-20260827-134 verdict

Status: **rejected and closed without retry**.

The exact CI-signed firmware-qualification package was staged only after the
mandatory clean preflight passed. One cold G2 boot then reached the sole
authorized 32-bit ASC CPU-status load at physical address `0x206400048`.
Windows used a valid mapped VA and m1n1 had the SGX aperture mapped through
stage 2, but the physical access raised an external abort:

- guest instruction: `ldr w8, [x8]`
- guest VA base: `0xffffe1f1ed800000`
- guest VA read: `0xffffe1f1ed800048`
- physical read: `0x206400048`
- `L2C_ERR_STS`: `0x11000ffc00000080`
- `L2C_ERR_ADR`: `0x2300000206400048`
- `L2C_ERR_INF`: `0x5`

This falsifies the experiment's assumption that an inert SGX mapping is enough
to read ASC status. The GPU power domain was off: the G2 broker resource was
exposed, but this driver profile never issued broker ON/QUERY. No register
write, CPU RUN transition, mailbox traffic, firmware start, interrupt, UAT,
queue, render, present or display action occurred.

The exact recovery pair restored Windows and non-force cleanup removed only
the recorded candidate package and signer. The shared raw `hv.log` was lost
when the recovery launcher cleared it; this is a process defect and no missing
raw file is represented as retained evidence. The next candidate must preserve
its log separately and bracket the one read with a fail-closed broker session:
ON, QUERY=ON, one read, OFF, then both unmaps. It remains unauthorized until a
fresh recovery preflight reports zero Event 129.
