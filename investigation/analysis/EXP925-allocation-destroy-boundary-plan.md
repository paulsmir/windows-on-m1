# EXP925: distinguish KMD destroy body from Windows synchronization

WHY THIS HYPOTHESIS: EXP924 post-import SDK dump2c6396ba waits on DXGI proxy-window
creation and its worker waits on DWM ALPC connect. DWM dump8cb92447 shows its LPC
thread1728 in NtGdiDdDDIDestroyAllocation; exact private PDB local allocation is
0x800014c0 and loop i=1, the staging allocation. Existing UMD guards already require
no native association/map/source/submission/copy holds before deallocation; native
GPUVA retirement/unbind precedes disposal. The user-mode stack cannot identify
whether the KMD destroy callback was entered or where kernel synchronization waits.

WINDOWS CONTRACT: FULL GRAPHICS unchanged. Microsoft documents that pfnDeallocateCb
leads through VidMm/GPU scheduler synchronization and DxgkDdiDestroyAllocation before
actual video-memory release; callback runs PASSIVE_LEVEL with synchronization zero.
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_destroyallocation
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_deallocatecb
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_freegpuvirtualaddresscb
No AssumeNotInUse flag, busy-status normalization, blind deferred free or flush.
AGX/ASAHI CONTRACT: native batch completion/retirement before pool cleanup and BO
unreference is preserved. No BO, UAT, queue, interrupt, DMA, power or recovery change.
TRANSLATION: add paired in-memory entry/exit observations around the existing KMD
DestroyAllocation implementation, recording caller PID/thread, allocation count,
first driver handle and exact return status. Query through the existing diagnostic
DDI probe, using a new private ABI version and rebuilt matching probe. No registry,
MMIO, allocation, wait or callback in the new trace write. Preserve every old return.
WHAT IS STILL UNKNOWN: pending KMD body versus OS/VidMm/scheduler synchronization
outside it. Entry/exit count difference and caller information distinguish the
boundary; incomplete/dropped snapshot means inconclusive, never absent-call proof.

Sources inspected: umd_win32_screen.c Destroy/Map/Unmap/CpuAllocation,
umd_gpuva_windows.c Free/MakeResident/Evict/Wait/transfer_slot, asahi BO dispose,
GPUVA bind/unbind/submit/retire, native-device BO destroy, GPUVA batchBegin/Poll/Release,
native-asahi-batch-lifecycle.py and build-native-asahi-state.py projections; pinned
Asahi Mesa agx_batch.c/agx_bo.c/pool.c. Kernel allocation_windows.c existing destroy
and paging_windows.c; dwm_ddi_probe_windows.c/header and matching Windows probe.
No source bug is confirmed by the stacks alone. One offline source pass found no
precise violated invariant; do not repeat it before this discriminator.

Layer ownership unchanged: Windows owns allocation scheduling/synchronization;
UMD owns references/maps/retirement, KMD owns its allocation records and physical
release, m1n1/Mu retain their validated hardware launch/power/DMA/interrupt contract.
Only diagnostic memory counters and private query layout change. Existing relevant
ABI/diagnostic/allocation tests, WDK zero warnings/build/sign/hash gates required;
no artificial RED test for receipt-only instrumentation. Same SDK stage-gated
snapshot once and immediate counters/evidence collection, normal rollback. No
hardware before new package preregistration and previous exact924 rollback complete.
