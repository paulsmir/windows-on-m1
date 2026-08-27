# J313 AGX ASC-ready hardware qualification

Date: 2026-08-27
Experiment: EXP-20260827-137

## Boundary

This experiment changes one operation between the validated powered ASC status
read and the rejected EXP-136 RTKit transaction: after asserting ASC `RUN`, the
driver waits for `CPU_STATUS.RUNNING=1` and `CPU_STATUS.STOPPED=0` before sending
the first RTKit IOP power-state message.

Current Asahi starts the ASC before constructing the GPU manager and RTKit
transport. The Windows qualification path previously asserted `RUN` and wrote
the first mailbox message immediately. EXP-135 observed powered pre-start status
`0x2a` (stopped); EXP-136 observed final status `0x2d` (running and idle) after a
first-HELLO timeout. This makes an unobserved ASC startup transition the only
source-backed variable in this experiment.

## Falsifiable hypothesis

The first IOP-init message in EXP-136 was published before the ASC completed its
stopped-to-running transition. Waiting for the typed running condition will
allow the first management HELLO to arrive.

The hypothesis passes only if the durable StartDevice receipts contain the new
`CPU_READY` flag and advance beyond `AwaitingHello`. It is rejected if CPU ready
is observed but HELLO remains absent, or if the ready wait itself times out.

## Invariants

- Use the existing assisted G2 boot, display `both`, eight CPUs, exact m1n1 and
  Mu pins, AGX broker, ACPI resources and stable platform drivers.
- Change only the signed Windows `AppleAgx` qualification package.
- Do not reboot for the driver-only candidate.
- Do not publish UAT roots, initdata, queues or commands, connect GPU interrupts,
  render, present or take display ownership.
- Replace and remove only the exact recorded OEM package; never use `/force`.
- Stop after one completed device-scoped lifecycle transaction.

## Evidence and verdict

The receipt-complete lifecycle runner must wait for the final
`Wom1StartDeviceStatus`, then save device state, RTKit flags and phase, negotiated
version, final CPU status, eight-CPU/input/NVMe/xHCI/SSH health, and new critical
or storage-reset events. Raw evidence is local-only under
`.local/experiments/EXP-20260827-137-agx-asc-ready/`.

The candidate remains fail-closed even if HELLO succeeds. Any lost SSH, input,
NVMe or xHCI health, any critical event, Event 129, reboot, bugcheck or identity
drift rejects the experiment and requires exact device rollback or EXP-123.
