# EXP945: DWM displayable write provenance

WHY THIS HYPOTHESIS:
1. In 944 the SDK's direct displayable write reached `SubmitCommand` with `NumPrimaries=1`, proving the new path works for an allocation created by that process. DWM's successful native Present used allocation `0x40003900`, but normal UMD diagnostics capped at 128 records before its corresponding `OpenResource` and submit records could be observed.
2. The same DWM context completed 114 render submissions while KMD `Present`/`VirtualPresent` remained zero and the DCP-selected surface stayed at `0x1500110000`. The unknown is whether DWM's own written allocation was marked and reported, not whether SDK's was.
3. Microsoft defines per-resource private data on `Allocate` and its return on `OpenResource`, and requires every directly GPU-written displayable allocation in `SubmitCommand.WrittenPrimaries`. Both links must be observed for the DWM consumer before another behavior fix is justified.

WINDOWS CONTRACT: FULL GRAPHICS WDDM 3.0. `D3DDDICB_ALLOCATE.pPrivateDriverData` is per-resource metadata; `D3D10DDIARG_OPENRESOURCE.pPrivateDriverData` returns that metadata for a shared resource. `D3DDDICB_SUBMITCOMMAND` must enumerate directly written displayable allocations in `WrittenPrimaries`. Pinned WDK 26100 defines the same structures and 16-handle limit. Sources: Microsoft D3DDDICB_ALLOCATE, D3D10DDIARG_OPENRESOURCE, D3DDDICB_SUBMITCOMMAND documentation and the pinned WDK headers.

AGX/ASAHI CONTRACT: Asahi's Apple DCP plane (`drivers/gpu/drm/apple/plane.c`, branch `asahi` inspected 2026-10-03) maps linear BGRA with explicit width, height, stride and DMA address. The current m1n1 `src/dcp_iomfb_present.c` and `src/hv_agx_power_mmio.c` configure and sample the J313 DCP-selected surface. In the 944 trace the selected source address, 2560x1600/10240 stride and PA `0x8e0110000` did not change. Mu's `J313AppleAgxAbiAdmission.asl.inc` exposes one APPL0002; the same ACPI, power and DART state is inherited. None of these layers changes in 945.

TRANSLATION: The UMD's Create/Open resource wrapper retains a `WrittenPrimary` marker, and `AgxWin32GpuvaSubmit` maps native written BO tokens to Windows allocation handles. Add only bounded, diagnostic `measure-` records for the exact DWM present source marker and a power-of-two sample of DWM `SubmitCommand.NumPrimaries`/first handle. Reuse the existing file logger and exact package pipeline. No flags, handles, queues, residency, fences, rendering, KMD, m1n1, Mu, ACPI or DCP behavior changes.

WHAT IS STILL UNKNOWN: Whether DWM's presented allocation is marked `WrittenPrimary`, whether the corresponding GPU submissions carry its handle, and whether the physical screen remains corrupted on the resulting boot. One 945 run distinguishes `marker absent`, `marker present but no submitted write`, and `write reported yet output unchanged`.

Single variable: receipt-only DWM resource/write provenance. Evidence checkpoint: one Code0 boot followed by the same bounded `ce544e78` windowed Present stimulus; inspect the exact DWM allocation marker and sampled WrittenPrimaries. Stop and collect immediately. The previous 944 original evidence is host-verified. Roll back exact 945 package to the normal GPU-visible Code28 profile; use immutable GPU-hidden Mu385 only if that normal recovery cannot be reached. The emergency recovery artifacts and hash gates remain unchanged.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? This is a callback-observation probe. No artificial RED test is warranted; existing UMD callback and package build tests must remain GREEN.
