# EXP918: preserve the already supported displayable BGRA resource contract

WHY THIS HYPOTHESIS:
1. EXP917 SDK probe PID4284 created an AppleLUID3dd7e D3D11 device at FL10.0,
   then CreateSwapChainForComposition returned887a0005. Guest Code0 remained.
2. That exact PID's UMD log contains the first buffer request: format87,
   dimension3, usage0, bind0xa8, map0, misc0x20002, one mip/layer/sample,
   2560x1600x1, no primary/initial data. AdmissionUmdDescribePrimary rejects it,
   SetError80070057 follows, then the runtime reports device removed.
3. Every shape/format predicate passes except the allowedMiscFlags mask: it
   accepts SHARED2 and DISCARD8 but omits DISPLAYABLE_SURFACE20000. Its accepted
   BGRA path already describes the allocation as Linear1/Displayable1/segment2.

WINDOWS CONTRACT: FULL GRAPHICS. Pinned WDK26100 d3d10umddi.h and Microsoft's
D3D10_DDI_RESOURCE_MISC_FLAG define DISPLAYABLE_SURFACE0x20000 since Windows10:
the resource contains a displayable surface. This is the DDI marker actually
received on D3D10_0_x, not permission to advertise the separate Windows11
flexible-presentation API or a new DDI version. Preserve SHARED ownership and all
existing dimensions, format, sample, bind, allocation and lifetime checks.

AGX/ASAHI CONTRACT: current Mesa agx_pipe.c selects linear layout for shared/
scanout resources when linear is supported; imports require16-byte stride.
Current AgxWin32AsahiImportLinearColor32 constructs BGRA8 linear layout, verifies
AIL size/stride/offset and imports the same owned BO. Existing KMD class0
allocation uses local segment,64KiB backing alignment and its established
open-count/retirement ownership. No new format, layout, DMA or hardware behavior.

TRANSLATION: accept the displayable marker for the existing supported BGRA
presentation resource and preserve the exact allocation descriptor. Reject the
marker for RGBA, which this path explicitly marks Displayable0, and continue to
reject protected/cross-adapter/tiled/unknown flags and unsupported shapes. Do not
change NO_REDIRECTION, GetSupportedVersions, capabilities, firmware, scheduler,
sharing callback protocol or general Windows11 displayable-surface support.

WHAT IS STILL UNKNOWN: whether accepting this exact resource closes chain
creation through allocation/import, and whether desktop output then starts.
The probe failure cause is confirmed; desktop-wide causality remains to test.
If another owning allocation/import failure emerges, use its exact input/result.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Replay the exact observed request against the real production descriptor
function: it must reach a valid BGRA linear/displayable descriptor with unchanged
pitch/size/segment. The old code must fail it. Retain forbidden flag/shape and
non-displayable-format rejection tests. Add the same observed tuple to the
native pinned-WDK contract suite. Do not merely assert a source-text mask.

Inspected: umd.c DescribePrimary/CreateResource; agx_d3d10_windows.cpp
PresentationCreate and attach_presentation_render_resource; render_allocation.c;
allocation_windows.c CreateAllocation; agx_win32_asahi_scene.c ImportLinearColor32;
current reference Mesa agx_pipe.c import and modifier selection; pinned WDK enum;
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/ne-d3d10umddi-d3d10_ddi_resource_misc_flag.
Existing m1n1/Mu EXP916/917 full-owner contract remains unchanged downstream;
EXP917 live resources and DCP+120300600 provide the current machine reference.
No external implementation code is copied. Owner of this defect is UMD resource
validation. Next hardware single behavior variable is this acceptance correction;
reuse the same creation probe after750s. Exact917 evidence/Code43rollback/Code28
must complete before staging a new signed package. Recovery377/392; hidden385
emergency only; visible updating desktop remains the final acceptance criterion.
