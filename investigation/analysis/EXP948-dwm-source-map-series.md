# EXP948: retain the bounded DWM source-map series

WHY THIS HYPOTHESIS:
1. EXP947's valid local PTE receipt selected DWM GPUVA `0x7f0000`, but its read-only DWM frame probe identified the actually presented source at GPUVA `0x1f0000` in the same process. The physical comparison failed because the single registry value was overwritten by a later FrameArm for a different BO, not because PTE translation failed.
2. The UMD limits FrameArm observations to eight per device; DWM used two contexts on EXP947. A 16-slot bounded series can preserve the exact source call without adding a GPU workload or changing timing-sensitive mappings.
3. The selected DCP source remained segment 2 at PA `0x8e0110000`. The still-unknown relation is one process GPUVA -> local IPA compared with this selected PA. There is no support for another flag or format change.

WINDOWS CONTRACT: FULL GRAPHICS WDDM 3.0. Microsoft `DXGKARG_SETVIDPNSOURCEADDRESS` identifies the primary's segment-relative scanout address, while process GPUVA is separately translated in the submitting context (`DxgkDdiSubmitCommandVirtual`, GPU Virtual Memory in WDDM 2.0). KMD must correlate them by backing allocation/physical page, not numeric VA equality. Pinned WDK 26100 is unchanged.

AGX/ASAHI CONTRACT: Asahi's Apple DCP plane uses explicit DMA address/stride; current m1n1 `dcp_iomfb_present.c` and `hv_agx_power_mmio.c` latch and sample the selected J313 source. Mu's APPL0002 ACPI and all GPU/DCP hardware and power state remain unchanged. Current KMD `AdmissionGpuvaG3FrameArmEscape` already observes each DWM process/context/VA and reads the G3 CPU logical PTE while holding its existing lock.

TRANSLATION: Keep EXP947's read-only PTE sample. Add a per-adapter ordinal and persist the first 16 valid FrameArm samples under separate `Wom1DwmSourceMap00`–`15` registry names, retaining the latest value for compatibility. The UMD's existing eight-attempt cap per device bounds the writes. No mapping, page-table update, DMA, render, fence, Present, DCP, interrupt or capability change. The host decoder selects the record whose allocation/GPUVA matches the read-only DWM frame probe's presented source, then compares its local IPA to the selected DCP PA.

WHAT IS STILL UNKNOWN: Whether a map record for the presented source GPUVA `0x1f0000` is captured and valid; if so, whether its IPA equals the DCP-selected PA. If absent even in the bounded series, revisit when/which context arms the source rather than infer a physical mismatch. One Code0 boot and read-only frame/map query suffices when DWM arms on boot; use the same 12-second SDK only if no matching source is present. Freeze evidence then remove exact EXP948 package, restore normal GPU-visible Code28; hidden immutable recovery only if normal cannot recover.

The last full m1n1 EXP933/Mu R143 and normal recovery EXP377/Mu392 remain byte-identical. EXP947 original evidence is host-gated. WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? This is receipt-only hardware observation; no artificial RED test. Run existing G3 graph/PTE and ledger checks, zero-warning ARM64 WDK build, signatures, hashes and PDB gate.
