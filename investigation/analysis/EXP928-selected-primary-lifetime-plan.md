# EXP928 selected-primary lifetime discriminator

WHY THIS HYPOTHESIS:
- EXP927 successful SetVidPnSourceAddress selected allocation ffff8b076cde8be0 at GPU address1500110000; DCP remained at physical8e0110000/sequence2 while sampled bytes changed and Present callbacks remained0. This narrows the physical artifact question to that exact allocation rather than the entire heap.
- ScanoutQueuePresent retains a fallback allocation only at offset0; the observed110000 primary receives no such lease. RetireAllocation checks ActiveLease only. Destruction of the first selected primary could therefore explain DCP reading reused bytes; it is not yet observed.
- Existing destroy counts665/665 retain only the last unrelated handle. They cannot exclude destruction of the selected primary. Native primary import explicitly sets LINEAR and verifies pitch/level offset, so arbitrary detiling or shifting the heap is less justified.

WINDOWS CONTRACT:
FULL GRAPHICS: official Microsoft DXGKDDI_SETVIDPNSOURCEADDRESS sets the primary associated with the display source; DXGKDDI_DESTROYALLOCATION releases allocations after scheduler synchronization. Compare opaque kernel allocation handles, not UMD handles. No admission/capability/return change.
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_setvidpnsourceaddress
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_destroyallocation

AGX/ASAHI CONTRACT:
DISPLAY_ONLY: AsahiLinux linux/asahi drivers/gpu/drm/apple/plane.c selects an owned GEM framebuffer, derives IOVA from its DMA address plus framebuffer offset and transmits explicit stride/format. Current m1n1 hv_agx_scanout_service.c retains registered pool translation and latched surface; current hv/__init__.py shuts down boot framebuffer before guest. Mu MemoryInitPeiLib reserves firmware framebuffer and separately validates local reserve. Mu/ACPI reservation does not independently retain a VidMM allocation. Live EXP927 reserveIPA=PA8e0000000/1GiB and sampled DCPPA8e0110000 are reference state; ordinary recovery has no AGX broker.

TRANSLATION:
Diagnostic only. The immutable first SourceAddressReceipt, published with state2, identifies the selected opaque KMD allocation. Compare every handle in DestroyAllocation with it before destruction; accumulate requested/successful/failed destruction observations with interlocked counters. Existing DWM destroy entries export cumulative requested via SourceCount, successful via DestinationCount, failed via Fence. ABI2 size/version and all return paths remain unchanged. Counts remain visible after unrelated destroys. No GPU/MMIO/registry/waits/allocation added.

WHAT IS STILL UNKNOWN:
Whether the initially selected nonzero primary is successfully destroyed while DCP continues scanning its pages. One short exact-package boot and existing read-only frame queries distinguish this: successful count>0 plus unchanged selected DCP sequence confirms lifetime boundary; zero requests excludes this cause for the observed interval. Negative result does not prove rendering/layout correct. Pointer reuse can produce later false matches; the first successful match is decisive, repeated counts are not separate identities.

Sources inspected: current render-admission scanout_windows.c/display.c/allocation_windows.c/memory_windows.c/memory_runtime_windows.c; native agx_win32_asahi_scene.c and batch.c; local primary Mesa agx_pipe.c; m1n1 hv_agx_scanout_service.c and hv/__init__.py; Mu MemoryInitPeiLib.c; Microsoft links above. Initialization/power/DART/DCP remains m1n1 broker-owned; VidMM allocation lifetime is Windows/KMD-owned, CPU transfers UMD/KMD-owned. No external source copied.

Verification: real DestroyAllocation wrapper replay under ASan/UBSan checks unpublished receipt, selected handle second in batch, successful and failed returns, and sticky counts after unrelated destruction. Existing DWM probe3 tests GREEN. This is receipt-only instrumentation; no manufactured bugfix RED. Recovery remains hash-pinned ordinary377/392 with exact package cleanup and Code28 checks. Hardware/build preregistration and hashes must be recorded before run. No unchanged SDK rerun is required to observe this lifetime boundary.
