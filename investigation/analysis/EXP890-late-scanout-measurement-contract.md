# EXP890 late scanout measurement contract

Supervisor correction: EXP889's seq2 snapshots were taken at validation and 15 seconds after the first DCP latch. They cannot establish the state of the active scanout at 300–600 seconds. EXP889 proves 625.46266 seconds of original Code0/CPU8/no kernel stop; nonzero Present remains unknown, not disproven.

## Sources and observed contract

Hardware reference: EXP889 original monitor and full.log, first-latch DCP A408/D589 receipt, seq2 surface PA0x8e0110000/IOVA0x102a0000, immutable J313 reserveV2/1GiB and MuR143 artifact hashes in EXP889 manifest. No new ADT/register/interrupt values are inferred. The 15-second callback reads pixels without cleaning the cache; its `nonzero0` observation applies only to that time and PA.

Asahi Linux local GPU `mmu.rs`/`buffer.rs` describes GPU process mappings but contains no corresponding J313 DCP scanout snapshot implementation. Current m1n1 `hv_agx_scanout_service.c/.h`, `hv_agx_power_mmio.c`, `display.c/.h`, `dcp.c/.h` and DCP transport tests are primary for the DCP swap/latch, surface mapping, timing and observation path. `hv_exc.c` calls one bounded scanout service step on VM exit. Mu MemoryInitPeiLib and generated ACPI expose the same reserve to Windows; they do not own these pixels or DCP swaps. Microsoft `DXGKDDI_PRESENT` documentation is classified FULL GRAPHICS and describes source/primary/copy/flip semantics; no Windows DDI or package change is proposed.

Initialization/power/DART/interrupt/recovery owners stay with the validated EXP889 m1n1/Mu/launcher contracts. Windows KMD/UMD still own runtime graphics allocations and writes. m1n1 scanout service owns the latched DCP surface and may read it for diagnostics. This experiment changes only observation callbacks: no new DCP swap, mapping, cache cleaning, IRQ, power, GPU submission or Windows behavior.

## Measurement

After the first exact D589 latch, retain the already validated seq2 surface PA/IOVA and schedule three read-only pixel-stat callbacks at 120, 300 and 600 seconds after that latch. Log the elapsed time and absolute m1n1 uptime, request sequence, broker applied swap ID, current DCP latched swap ID and surface IOVA, surface-match verdict, captured PA/IOVA, nonzero count/hash/corners and existing `cache_clean=0`. If the DCP surface differs from the captured surface, treat that snapshot as a stale-surface observation, not as evidence for current Present. If the surface matches and the CPU-readable view is still zero late, the next offline question is producer/Present ownership; cache coherency remains an explicit measurement limit.

Extend the host original-boot monitor to at least 700 seconds to include the 600-second post-latch sample, preserving the same package889 and one original boot. The monitor proves Code0/CPU8/same boot; snapshot callbacks must run while this boot is still live. Each checkpoint should produce exactly one record. Pool release or a new latched sequence cancels pending reads before dereferencing the old surface.

WHAT IS STILL UNKNOWN: whether the DCP surface/ID remains seq2 at late checkpoints and whether the current matched surface has nonzero pixels. One measurement-only hardware run resolves this; no Windows or AGX behavior hypothesis is justified yet.

Smallest falsifiable checkpoint: at least one matched-surface late record after DWM starts, ideally 120/300/600 seconds after latch, plus the original-boot monitor. A missing callback, mismatch, or invalid PA is INCONCLUSIVE for Present rather than zero evidence. Immutable recovery artifacts, package889 exact hash, host-verified original ETL/dumps before Code45 package cleanup, and autologon retention follow EXP889 gates. The instrumented m1n1 image must be separately source/build/hash pinned; MuFD and package889 must remain byte-exact.
