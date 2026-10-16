# M2-f7: segment-3 reservations keep the Windows memory floor (2026-10-16)

## Evidence

- EXP1291, EXP1293, EXP1304, EXP1305: the guest stopped answering SSH in the PRESSURE step
  (`agx_mempressure`: RAM locked to 384 MiB available, then 32 MiB render targets up to 2560 MiB).
  EXP1300 showed the milder form: minutes of unresponsiveness, then recovery.
- EXP1305: DCP swaps (`dcp-iomfb ... D589 latch`) continued in the hypervisor log until the watchdog
  abort at 01:06:44Z. The display and DWM were alive while SSH got no service: the guest was
  starved for memory, not hung on the GPU.
- The `IRQ route disabled vINTID=865 AIC=330` line, the last hypervisor line in EXP1291 and
  EXP1293, is the Apple input route (`hv_apple_input.c` dynamic route). The NVMe interrupt is
  synthetic. That line does not explain the hang.

## WINDOWS CONTRACT

- `DxgkDdiCreateAllocation` returns STATUS_NO_MEMORY when memory it needs is not available. The
  runtime reports E_OUTOFMEMORY to the application (M2-f5).
- Standard Event Objects (Microsoft Learn, kernel): `\KernelObjects\LowMemoryCondition` is set
  while free physical memory is below a system-defined amount. "Drivers that have allocated large
  amounts of memory can wait for this event to be set as a signal to free unused memory." Open it
  with `IoCreateNotificationEvent`. It is a notification event and stays set while the condition
  holds.
- `MmAllocatePagesForMdlEx` takes free physical pages. Nothing stops it from taking the last ones.

## AGX/ASAHI CONTRACT

Unchanged. Segment-3 pages are 64 KiB granules in 2 MiB slabs below the HVC limit, mapped through
the m1n1 pool window (M2-f3).

## TRANSLATION

`AdmissionPoolSlabReserve`, the CreateAllocation path, asks `MayGrow` before taking each new slab.
On Windows, `MayGrow` is `KeReadStateEvent(LowMemoryCondition) == 0`. While the event is set, a
reservation the held slabs cannot cover is refused. The result is ReserveFailures + 1 and
FloorRefusals + 1, with no NewSlabFailures. CreateAllocation then fails with STATUS_NO_MEMORY.

The paging path (`AdmissionPoolSlabTake`, a move's overlap) is not gated. A refusal there poisons a
process (M2-f5).

The event is referenced once at pool start (PASSIVE_LEVEL) and read under the pool's fast mutex.
KeReadStateEvent allows IRQL <= DISPATCH_LEVEL.

Test: `render_pool_slab_test.c` `test_reserve_keeps_the_windows_memory_floor` went RED (no member)
then GREEN.

## WHAT IS STILL UNKNOWN

- Whether the system threshold leaves Windows enough to stay responsive under the PRESSURE test.
  An alternative cause would remain if the guest still starves while FloorRefusals > 0, for
  example the tool's locked RAM alone or the working sets of DWM.
- Hardware checkpoint (PRESSURE back in the pipeline, `RUN_PRESSURE=1`): agx_mempressure reports
  E_OUTOFMEMORY at creation. The receipt shows ReserveFailures >= 1 with NewSlabFailures 0. SSH,
  DWM and explorer stay alive, LONGJOB and the corpus run afterwards, and no device is removed.
- Recovery: the experiment's recXXXXh/recover.sh (hidden-profile rollback).
