# J313 AGX mailbox visibility qualification

## Goal

Distinguish whether the first RTKit IOP-init message is never published, remains
queued, or is consumed by running GPU firmware without producing HELLO.

## Source boundary

- Asahi Linux commit `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`,
  `drivers/soc/apple/mailbox.c`, publishes `msg0` then the endpoint-bearing
  `msg1`; the second write commits the A2I FIFO entry.
- Asahi `drivers/soc/apple/rtkit.c` executes `dma_wmb()` before that MMIO send.
- Pinned m1n1 `src/asc.c` uses the same A2I offsets and ordering and executes
  `dma_wmb()` before `SEND0` and `SEND1`.
- Windows uses `WRITE_REGISTER_ULONG64` for both writes. Microsoft's WDK
  contract states that this routine inserts a memory barrier, so adding another
  barrier is not a falsifiable difference:
  <https://learn.microsoft.com/windows-hardware/drivers/ddi/wdm/nf-wdm-write_register_ulong64>.
- EXP-137 proved `CPU_STATUS.RUNNING=1`, `STOPPED=0`, then timed out before
  HELLO with flags `0x81`. More startup delay is rejected.

## Single changed variable

Add read-only snapshots of A2I `INBOX_CTRL` before IOP INIT, immediately after
the endpoint/trigger write, and at receive failure, plus I2A `OUTBOX_CTRL` at
failure. Preserve the exact power sequence, RUN transition, payload, endpoint,
five-second deadline, cleanup, and fail-closed behavior.

The control register exposes FIFO count, write pointer, read pointer, FULL,
EMPTY, and ENABLE state. The snapshots are diagnostic reads only; they do not
acknowledge an interrupt, advance a FIFO, send a second message, or start a new
GPU stage.

## Interpretation

- No observable A2I transition: investigate mapping/register visibility.
- Entry present after publish and still present at timeout: firmware is running
  but not servicing the mailbox; investigate remaining reset/clock/power state.
- Entry appears and is later consumed without HELLO: mailbox transport works;
  investigate firmware-side RTKit prerequisites or firmware state.
- Non-empty I2A at failure: receive interpretation is wrong; preserve the raw
  control and stop before protocol changes.

## Verification

1. Host tests must prove typed offsets, snapshot ordering, timeout preservation,
   registry receipt names, and unchanged RTKit payload ordering.
2. Run the focused GPU transport/session/package suites.
3. Build the official signed ARM64 RTKit qualification package from the pinned
   public commit; verify SYS, INF, catalog, certificate, and signer identities.
4. Require eight CPUs and Running AppleInput, stornvme, and USBXHCI.
5. Use the corrected single-transaction lifecycle runner for exactly one
   Windows device hot cycle without reboot.
6. Save the terminal receipt and a delayed postflight. Do not retry.

## Stop conditions

Stop on identity drift, missing snapshot-valid bits, Event 129, critical event,
lost SSH/input/NVMe/xHCI health, bugcheck, reboot, or any request to advance to
endpoint map, UAT, initdata, interrupts, queues, commands, rendering, or display
ownership.
